#include "prism/gui/preview_renderer.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace prism::gui {

std::optional<Image> render_stage_image(const Image& base, const EditSettings& settings,
                                        StageId stop_after, int threads, std::stop_token stop,
                                        const StageCallback& on_stage) {
    const ProcessSettings process_settings = to_process_settings(settings, stop_after);
    if (stop.stop_requested()) {
        return std::nullopt;
    }
    Image img = base;  // the base is shared and never changes; the pipeline works in place
    if (process(img, process_settings, threads, stop, on_stage) == ProcessResult::cancelled ||
        stop.stop_requested()) {
        return std::nullopt;
    }
    return img;
}

std::optional<PreviewFrame> render_preview(const Image& base, const EditSettings& settings,
                                           int threads, std::stop_token stop,
                                           const StageCallback& on_stage, StageId stop_after) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const ProcessSettings process_settings = to_process_settings(settings, stop_after);

    PreviewFrame frame;
    frame.stage = stop_after;
    if (stop_after == StageId::decode) {
        if (stop.stop_requested()) {
            return std::nullopt;
        }
        frame.width = base.width;
        frame.height = base.height;
        frame.rgb8 = encode_srgb8(base, threads);
        frame.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return frame;
    }

    Image img = base;  // the base is shared and never changes; the pipeline works in place
    const auto record = [&](const StageTime& t) {
        frame.stages.push_back(t);
        if (on_stage) {
            on_stage(t);
        }
    };
    if (process(img, process_settings, threads, stop, record) == ProcessResult::cancelled ||
        stop.stop_requested()) {
        return std::nullopt;
    }
    frame.width = img.width;
    frame.height = img.height;
    frame.rgb8 = encode_srgb8(img, threads);
    frame.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return frame;
}

PreviewRenderer::PreviewRenderer(int threads)
    : PreviewRenderer([threads](const Image& base, const EditSettings& settings, StageId stop_after,
                                std::stop_token stop) {
          return render_preview(base, settings, threads, stop, {}, stop_after);
      }) {}

PreviewRenderer::PreviewRenderer(RenderFunction render)
    : PreviewRenderer(
          [r = std::move(render)](const Image& base, const EditSettings& settings, StageId,
                                  std::stop_token stop) { return r(base, settings, stop); }) {}

// The worker starts last, in the constructor's body, once render_ is set.
PreviewRenderer::PreviewRenderer(StageRenderFunction render) : render_(std::move(render)) {
    worker_ = std::jthread([this](std::stop_token thread_stop) { run(thread_stop); });
}

PreviewRenderer::~PreviewRenderer() {
    {
        std::lock_guard lock(mutex_);
        render_stop_.request_stop();  // the render in progress, if any, gives up at its next check
    }
    worker_.request_stop();  // wakes the worker if it is waiting for a request
    worker_.join();
}

std::uint64_t PreviewRenderer::submit(std::shared_ptr<const Image> base,
                                      const EditSettings& settings, StageId stop_after) {
    if (!base) {
        throw std::invalid_argument("PreviewRenderer::submit: no image");
    }
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        generation = ++latest_generation_;
        // Replaces a waiting request and supersedes a running one.
        pending_ = Request{generation, std::move(base), settings, stop_after};
        render_stop_.request_stop();
    }
    wake_.notify_one();
    return generation;
}

std::optional<PreviewFrame> PreviewRenderer::poll() {
    std::lock_guard lock(mutex_);
    return std::exchange(ready_, std::nullopt);
}

bool PreviewRenderer::wait_until_idle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [&] { return !pending_ && !busy_; });
}

void PreviewRenderer::run(std::stop_token thread_stop) {
    std::unique_lock lock(mutex_);
    while (true) {
        // Sleeps until there is a request or the destructor asks the thread to stop. The lock is
        // released while waiting and held again when wait() returns.
        wake_.wait(lock, thread_stop, [&] { return pending_.has_value(); });
        if (thread_stop.stop_requested()) {
            return;
        }
        Request request = std::move(*pending_);
        pending_.reset();
        // A fresh stop source for this request: submit() stops it if a newer request arrives.
        render_stop_ = std::stop_source{};
        const std::stop_token render_stop = render_stop_.get_token();
        busy_ = true;

        lock.unlock();  // never hold the lock while rendering, so submit() and poll() don't wait
        std::optional<PreviewFrame> frame;
        try {
            frame = render_(*request.base, request.settings, request.stop_after, render_stop);
        } catch (const std::exception& e) {
            // An exception must not leave the thread (that would call std::terminate).
            frame = PreviewFrame{};
            frame->error = e.what();
        }
        lock.lock();

        // Publish only if no newer request arrived meanwhile: a superseded render's frame would
        // show settings the user has already moved away from.
        if (frame && request.generation == latest_generation_) {
            frame->generation = request.generation;
            frame->stage = request.stop_after;
            ready_ = std::move(frame);
        }
        busy_ = false;
        idle_.notify_all();
    }
}

}  // namespace prism::gui

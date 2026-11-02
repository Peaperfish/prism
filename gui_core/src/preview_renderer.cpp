#include "prism/gui/preview_renderer.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace prism::gui {

namespace {

// Whether process() runs `stage` for these settings (the same conditions as lib/process.cpp).
bool stage_runs(const ProcessSettings& s, StageId stage) {
    switch (stage) {
        case StageId::exposure: return s.exposure_stops && *s.exposure_stops != 0.0f;
        case StageId::matrix: return s.matrix && *s.matrix != identity_matrix;
        case StageId::saturation: return s.saturation && *s.saturation != 1.0f;
        case StageId::contrast: return s.contrast && *s.contrast != 1.0f;
        case StageId::curve: return s.curve.has_value();
        case StageId::decode:
        case StageId::encode: return false;
    }
    return false;
}

}  // namespace

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
                                           const StageCallback& on_stage, StageId stop_after,
                                           PreviewOptions options) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const ProcessSettings process_settings = to_process_settings(settings, stop_after);

    PreviewFrame frame;
    frame.stage = stop_after;
    // Decided once here; the per-stage callback below checks the recorder, never a pixel loop.
    std::optional<ClipRecorder> recorder;
    if (options.clip_report) {
        recorder.emplace(base.width, base.height);
    }

    // Pixels that a stage before the viewed one clamped away (colours outside the gamut, shadows):
    // after that stage they look like ordinary pixels, so they are remembered and stay marked on
    // the mask of every later stage. Only kept while the mask is wanted: `pending` is the negative
    // marks of the image going into the next stage, and only made when that stage clamps.
    std::vector<std::uint8_t> lost;
    std::vector<std::uint8_t> pending;
    bool have_pending = false;
    // Once a stage has clamped negatives there are none left, and no later stage makes new ones
    // (the matrix, which can, comes before them), so nothing more is worth snapshotting.
    bool negatives_clamped = false;
    // The stage that runs right after `after` (nullopt: the first one) in this render, if any.
    const auto next_stage = [&](std::optional<StageId> after) -> std::optional<StageId> {
        for (int i = after ? static_cast<int>(*after) + 1 : static_cast<int>(StageId::exposure);
             i <= static_cast<int>(StageId::curve); ++i) {
            if (stage_runs(process_settings, static_cast<StageId>(i))) {
                return static_cast<StageId>(i);
            }
        }
        return std::nullopt;
    };
    const auto prepare_for_next = [&](std::optional<StageId> after, const Image& img) {
        const std::optional<StageId> next = next_stage(after);
        if (!negatives_clamped && next && stage_clamps(*next, ClipDirection::shadows)) {
            compute_negative_mask(img, threads, pending);
            have_pending = true;
        }
    };

    // Everything that reads the image at the viewed stage: the picture and its histogram, then
    // the scopes and the clip mask, which are only made if asked for.
    const auto show = [&](const Image& shown) {
        frame.width = shown.width;
        frame.height = shown.height;
        encode_srgb8_with_histogram(shown, threads, frame.rgb8, frame.histogram);
        if (options.scopes) {
            frame.scopes = compute_scopes(frame.rgb8, frame.width, frame.height);
        }
        if (options.clip_mask) {
            compute_clip_mask(shown, threads, frame.clip_mask, stop_after == StageId::decode,
                              lost.empty() ? nullptr : &lost);
        }
    };
    const auto finish = [&] {
        frame.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return std::move(frame);
    };

    // The stages after `stop_after` only run to measure their clipping. Every stage is a per-pixel
    // operation, so they run on the sampled pixels only (a small copy), not on the whole image.
    const auto run_rest = [&](const Image& current) {
        if (!recorder) {
            return true;
        }
        if (stop_after != final_stage) {
            Image grid = recorder->sample_grid(current);
            const auto record_stage = [&](const StageTime& t) {
                if (const std::optional<StageId> id = stage_from_name(t.name)) {
                    recorder->stage_done_samples(*id, grid);
                }
            };
            const ProcessSettings rest =
                process_settings_after(to_process_settings(settings), stop_after);
            // The grid is at most 250,000 pixels: one thread, below the parallel threshold anyway.
            if (process(grid, rest, 1, stop, record_stage) == ProcessResult::cancelled ||
                stop.stop_requested()) {
                return false;
            }
        }
        recorder->finish();
        frame.clips = recorder->report();
        return true;
    };

    if (stop_after == StageId::decode) {
        if (stop.stop_requested()) {
            return std::nullopt;
        }
        show(base);
        if (recorder) {
            recorder->decoded(base);
            if (!run_rest(base)) {
                return std::nullopt;
            }
        }
        return finish();
    }

    Image img = base;  // the base is shared and never changes; the pipeline works in place
    if (recorder) {
        recorder->decoded(img);
    }
    if (options.clip_mask) {
        prepare_for_next(std::nullopt, img);
    }
    const auto record = [&](const StageTime& t) {
        frame.stages.push_back(t);
        const std::optional<StageId> id = stage_from_name(t.name);
        if (recorder && id) {
            recorder->stage_done(*id, img);
        }
        if (options.clip_mask && id) {
            if (have_pending && stage_clamps(*id, ClipDirection::shadows)) {
                // This stage clamped what came in: those pixels are lost from here on.
                negatives_clamped = true;
                if (lost.empty()) {
                    lost = std::move(pending);
                } else {
                    for (std::size_t i = 0; i < lost.size(); ++i) {
                        lost[i] |= pending[i];
                    }
                }
            }
            have_pending = false;
            prepare_for_next(id, img);
        }
        if (on_stage) {
            on_stage(t);
        }
    };
    if (process(img, process_settings, threads, stop, record) == ProcessResult::cancelled ||
        stop.stop_requested()) {
        return std::nullopt;
    }
    show(img);
    if (!run_rest(img)) {
        return std::nullopt;
    }
    return finish();
}

PreviewRenderer::PreviewRenderer(int threads)
    : PreviewRenderer(OptionsRenderFunction(
          [threads](const Image& base, const EditSettings& settings, StageId stop_after,
                    const PreviewOptions& options, std::stop_token stop) {
              return render_preview(base, settings, threads, stop, {}, stop_after, options);
          })) {}

PreviewRenderer::PreviewRenderer(RenderFunction render)
    : PreviewRenderer(
          [r = std::move(render)](const Image& base, const EditSettings& settings, StageId,
                                  std::stop_token stop) { return r(base, settings, stop); }) {}

PreviewRenderer::PreviewRenderer(StageRenderFunction render)
    : PreviewRenderer(ScopeRenderFunction(
          [r = std::move(render)](const Image& base, const EditSettings& settings,
                                  StageId stop_after, bool, std::stop_token stop) {
              return r(base, settings, stop_after, stop);
          })) {}

PreviewRenderer::PreviewRenderer(ScopeRenderFunction render)
    : PreviewRenderer(OptionsRenderFunction(
          [r = std::move(render)](const Image& base, const EditSettings& settings,
                                  StageId stop_after, const PreviewOptions& options,
                                  std::stop_token stop) {
              return r(base, settings, stop_after, options.scopes, stop);
          })) {}

// The worker starts last, in the constructor's body, once render_ is set.
PreviewRenderer::PreviewRenderer(OptionsRenderFunction render) : render_(std::move(render)) {
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
                                      const EditSettings& settings, StageId stop_after,
                                      PreviewOptions options) {
    if (!base) {
        throw std::invalid_argument("PreviewRenderer::submit: no image");
    }
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        generation = ++latest_generation_;
        // Replaces a waiting request and supersedes a running one.
        pending_ = Request{generation, std::move(base), settings, stop_after, options};
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
            frame = render_(*request.base, request.settings, request.stop_after, request.options,
                            render_stop);
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

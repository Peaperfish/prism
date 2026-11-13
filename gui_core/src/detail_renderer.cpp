#include "prism/gui/detail_renderer.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

#include "prism/gui/session.hpp"

namespace prism::gui {

bool box_contains(const FrameBox& outer, const FrameBox& inner) noexcept {
    if (inner.width <= 0 || inner.height <= 0) {
        return true;
    }
    // long long: x0 + width can pass the largest int for boxes the caller never checked.
    const auto end = [](int first, int count) {
        return static_cast<long long>(first) + static_cast<long long>(count);
    };
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
           end(inner.x0, inner.width) <= end(outer.x0, outer.width) &&
           end(inner.y0, inner.height) <= end(outer.y0, outer.height);
}

bool detail_stops_running(const DetailJob& running, const DetailJob& wanted) noexcept {
    return running.base != wanted.base || running.base_id != wanted.base_id ||
           !box_contains(running.region, wanted.region);
}

DetailAdmit detail_admit(RegionBudget budget, bool gesture) noexcept {
    if (budget == RegionBudget::never) {
        return DetailAdmit::refused_too_big;
    }
    return region_renders_now(budget, gesture) ? DetailAdmit::render : DetailAdmit::refused_gesture;
}

bool detail_frame_wanted(std::uint64_t frame_base, std::uint64_t frame_generation,
                         std::uint64_t current_base, std::uint64_t shown_generation) noexcept {
    return frame_base == current_base && frame_generation > shown_generation;
}

DetailRenderer::DetailRenderer(int threads)
    : DetailRenderer(
          [threads](const Photo& base, const EditSettings& settings, const FrameBox& region,
                    std::stop_token stop) {
              return render_region(base, settings, region, threads, std::move(stop));
          },
          threads) {}

// The worker starts last, in the constructor's body, once render_ is set.
DetailRenderer::DetailRenderer(RenderFunction render, int threads)
    : render_(std::move(render)), threads_(threads) {
    worker_ = std::jthread([this](std::stop_token thread_stop) { run(thread_stop); });
}

DetailRenderer::~DetailRenderer() {
    {
        // Both under the lock, as in PreviewRenderer: the worker can't take the waiting request
        // between the two and start it with a stop source nothing stops.
        std::lock_guard lock(mutex_);
        worker_.request_stop();
        render_stop_.request_stop();
    }
    worker_.join();
}

std::uint64_t DetailRenderer::submit(DetailRenderRequest request, bool gesture) {
    if (!request.base) {
        throw std::invalid_argument("DetailRenderer::submit: no base");
    }
    const FrameBox& region = request.request.region;
    if (region.width <= 0 || region.height <= 0) {
        throw std::invalid_argument("DetailRenderer::submit: an empty region");
    }
    if (request.output.width < 1 || request.output.height < 1 ||
        request.output.width > region.width || request.output.height > region.height) {
        throw std::invalid_argument(
            "DetailRenderer::submit: the output size is 0 or larger than "
            "the region");
    }
    // Below 100% the output must be what downscale makes of the region, or the whole region would
    // be rendered for an error frame.
    if (request.output != PixelSize{region.width, region.height} &&
        request.output != downscaled_size(region.width, region.height,
                                          std::max(request.output.width, request.output.height))) {
        throw std::invalid_argument(
            "DetailRenderer::submit: the output size is not the region shrunk by downscale");
    }
    const DetailJob wanted{request.base.get(), request.key.base, region};
    const bool admitted = detail_admit(request.request.budget, gesture) == DetailAdmit::render;

    std::uint64_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        if (running_ && detail_stops_running(*running_, wanted)) {
            render_stop_.request_stop();
        }
        discard_pending();
        if (admitted) {
            generation = ++latest_generation_;
            pending_ = Job{generation, std::move(request)};
        }
    }
    wake_.notify_one();
    return generation;
}

void DetailRenderer::clear() {
    {
        std::lock_guard lock(mutex_);
        if (running_) {
            render_stop_.request_stop();
        }
        discard_pending();
    }
    wake_.notify_one();
}

void DetailRenderer::discard_pending() {
    if (pending_) {
        discarded_.push_back(std::move(*pending_));
        pending_.reset();
    }
}

std::optional<DetailFrame> DetailRenderer::poll(std::uint64_t current_base) {
    std::optional<DetailFrame> frame;
    {
        std::lock_guard lock(mutex_);
        frame = std::exchange(ready_, std::nullopt);
    }
    if (!frame ||
        !detail_frame_wanted(frame->key.base, frame->generation, current_base, shown_generation_)) {
        return std::nullopt;
    }
    shown_generation_ = frame->generation;
    return frame;
}

bool DetailRenderer::wait_until_idle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [&] { return !pending_ && discarded_.empty() && !busy_; });
}

std::optional<DetailFrame> DetailRenderer::render(const Job& job, std::stop_token stop) const {
    const DetailRenderRequest& r = job.request;
    std::optional<Image> pixels = render_(*r.base, r.settings, r.request.region, stop);
    if (!pixels || stop.stop_requested()) {
        return std::nullopt;
    }
    DetailFrame frame;
    frame.generation = job.generation;
    frame.key = r.key;
    frame.region = r.request.region;
    frame.picture =
        plan_export_frame(r.settings.frame, r.base->image.width, r.base->image.height).size;
    frame.frame = r.settings.frame;
    if (r.output != PixelSize{pixels->width, pixels->height}) {
        // Below 100%: shrink to what the canvas covers, as detail_output_size worked it out.
        *pixels = downscale(*pixels, std::max(r.output.width, r.output.height), threads_);
        if (stop.stop_requested()) {
            return std::nullopt;
        }
    }
    if (r.output != PixelSize{pixels->width, pixels->height}) {
        frame.error = "The detail render is " + std::to_string(pixels->width) + " x " +
                      std::to_string(pixels->height) + ", not the " +
                      std::to_string(r.output.width) + " x " + std::to_string(r.output.height) +
                      " asked for.";
        return frame;
    }
    frame.size = r.output;
    frame.rgb8 = encode_srgb8(*pixels, threads_);
    return frame;
}

DetailFrame DetailRenderer::failed(const Job& job, std::string message) {
    DetailFrame frame;
    frame.generation = job.generation;
    frame.key = job.request.key;
    frame.region = job.request.request.region;
    frame.error = std::move(message);
    return frame;
}

void DetailRenderer::run(std::stop_token thread_stop) {
    while (true) {
        std::optional<Job> job;
        std::vector<Job> discarded;
        std::stop_token render_stop;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, thread_stop,
                       [&] { return pending_.has_value() || !discarded_.empty(); });
            if (thread_stop.stop_requested()) {
                return;
            }
            discarded.swap(discarded_);
            job = std::exchange(pending_, std::nullopt);
            if (job) {
                // A fresh stop source for this render: submit() stops it by the stop rule.
                render_stop_ = std::stop_source{};
                render_stop = render_stop_.get_token();
                running_ = DetailJob{job->request.base.get(), job->request.key.base,
                                     job->request.request.region};
            }
            busy_ = true;
        }
        // Requests replaced or cleared before they ran: their bases are let go here, on this
        // thread, rather than on the UI thread that replaced them.
        discarded.clear();

        std::optional<DetailFrame> frame;
        if (job) {
            try {
                frame = render(*job, render_stop);
            } catch (const std::exception& e) {
                // An exception must not leave the thread (that would call std::terminate).
                frame = failed(*job, e.what());
            } catch (...) {
                frame = failed(*job, "The detail render failed.");
            }
        }

        std::optional<DetailFrame> dropped;  // a frame never polled, freed outside the lock
        {
            std::lock_guard lock(mutex_);
            running_.reset();
            // Checked under the lock where submit() and clear() ask for the stop: a stop asked
            // for before this point keeps the frame out, whatever the render returned.
            if (frame && !render_stop.stop_requested()) {
                dropped = std::exchange(ready_, std::move(frame));
            }
        }
        // The job, and with it the base it holds, is let go here, outside the lock and before the
        // worker counts as idle: a render holding the last reference frees the base now.
        job.reset();
        dropped.reset();
        {
            std::lock_guard lock(mutex_);
            busy_ = false;
        }
        idle_.notify_all();
    }
}

}  // namespace prism::gui

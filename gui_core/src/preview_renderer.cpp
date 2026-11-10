#include "prism/gui/preview_renderer.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "prism/parallel.hpp"

namespace prism::gui {

std::optional<Image> render_stage_image(const Image& base, const EditSettings& settings,
                                        StageId stop_after, int threads, std::stop_token stop,
                                        const StageCallback& on_stage,
                                        const std::optional<Chromaticity>& as_shot_white) {
    const ProcessSettings process_settings =
        to_process_settings(settings, stop_after, as_shot_white,
                            stage_grid(base.width, base.height));
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

namespace {

// The strokes' field on the grid of the image being rendered (from the field cache), or nothing
// (empty): then the Dodge & burn stage rasterises it itself.
struct FieldView {
    std::span<const float> values;
    PixelGrid grid;
};

// Hands the field to the Dodge & burn stage of `p`, if `p` has one and the field is on its grid.
// The clip report's sample grid and a band are parts of the picture: their values are copied
// into `storage`, which must live as long as `p` is used.
void use_field(ProcessSettings& p, const FieldView& view, std::vector<float>& storage) {
    if (p.dodge_burn.field.strokes.empty() || view.values.empty()) {
        return;
    }
    const PixelGrid& g = p.dodge_burn.grid;
    const PixelGrid& v = view.grid;
    if (g.full_w == v.full_w && g.full_h == v.full_h && g.x0 == v.x0 && g.y0 == v.y0 &&
        g.stride == v.stride && g.width() == v.width() && g.height() == v.height()) {
        p.dodge_burn.precomputed = view.values;
    } else if (field_at(view.values, view.grid, g, storage)) {
        p.dodge_burn.precomputed = storage;
    }
}

// render_preview for an image that sits on the photo at `grid` (the whole picture, or a band of
// it): the strokes of Dodge & burn are placed by it. render_preview is this on the whole picture.
// `field` is the strokes' field on `grid`, or empty.
std::optional<PreviewFrame> render_plain(const Image& base, const PixelGrid& grid,
                                         const EditSettings& settings, int threads,
                                         std::stop_token stop, const StageCallback& on_stage,
                                         StageId stop_after, PreviewOptions options,
                                         const FieldView& field);

// The test strip: every band is the same render_preview of its crop of `base` at its own exposure,
// so a band is bit-identical to rendering that crop alone, and the pipeline itself is untouched.
//
// The bands render at the same time, one thread each: a band (a fifth of a preview) is below
// parallel_min_pixels, so on its own it runs on one thread and the bands one after another would
// leave the other cores idle. At most min(bands, threads) run at once (threads 1: one at a time).
// A band big enough to use threads itself gets an equal share of `threads`, so the total never
// goes over what was asked. The calling thread renders the middle band (the one on_stage reports,
// live, as before), then helps with the rest; the other bands only ever run on the workers.
// A stop request, or an exception in any band, stops every band at its next stage; all workers
// are joined before this returns or rethrows the first exception.
std::optional<PreviewFrame> render_strip(const Image& base, const EditSettings& settings,
                                         int threads, std::stop_token stop,
                                         const StageCallback& on_stage, StageId stop_after,
                                         const PreviewOptions& options, const FieldView& field) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const TestStrip strip = *options.test_strip;
    const std::vector<StripRect> bands = strip_bands(base.width, base.height, strip.count);
    const auto middle = static_cast<std::size_t>(strip_middle(strip.count));
    constexpr std::size_t ch = Image::channels;
    const auto width = static_cast<std::size_t>(base.width);

    PreviewOptions band_options;  // what a band renders: the picture, and its scopes and mask
    band_options.scopes = options.scopes;
    band_options.clip_mask = options.clip_mask;
    band_options.as_shot_white = options.as_shot_white;

    PreviewFrame frame;
    frame.stage = stop_after;
    frame.width = base.width;
    frame.height = base.height;
    frame.rgb8.assign(width * static_cast<std::size_t>(base.height) * ch, 0);
    if (options.clip_mask) {
        frame.clip_mask.assign(width * static_cast<std::size_t>(base.height), 0);
    }
    frame.strip.resize(bands.size());

    // The bands with pixels, the middle one first (the calling thread takes it).
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < bands.size(); ++i) {
        frame.strip[i].rect = bands[i];
        if (bands[i].width > 0 && bands[i].height > 0) {
            order.push_back(i);
        }
    }
    if (const auto m = std::find(order.begin(), order.end(), middle); m != order.end()) {
        std::rotate(order.begin(), m, m + 1);
    }
    const int asked = threads <= 0 ? hardware_threads() : threads;
    const int concurrent = std::max(1, std::min(static_cast<int>(order.size()), asked));
    const int share = std::max(1, asked / concurrent);

    // Stops every band: the caller's stop, a band that gave up, or an exception.
    std::stop_source band_stop;
    const std::stop_callback forward_stop(stop, [&band_stop] { band_stop.request_stop(); });
    std::mutex error_mutex;
    std::exception_ptr error;

    // Each band writes only its own pixels of rgb8 and clip_mask and its own entry of strip.
    const auto render_band = [&](std::size_t i, const StageCallback& report) {
        const StripRect& r = bands[i];
        Image crop;
        crop.width = r.width;
        crop.height = r.height;
        crop.pixels.resize(static_cast<std::size_t>(r.width) * r.height * ch);
        for (int y = 0; y < r.height; ++y) {
            const float* from = base.pixels.data() + ((static_cast<std::size_t>(r.y + y) * width) + r.x) * ch;
            std::copy(from, from + static_cast<std::size_t>(r.width) * ch,
                      crop.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * r.width * ch));
        }
        // The thread count this band would have alone, capped at its share of `threads`.
        const int alone = threads_for_pixels(crop.pixels.size() / ch, threads);
        const int band_threads = alone > 1 ? std::min(alone, share) : 1;
        const EditSettings band_settings =
            strip_band_settings(settings, strip.step, strip.count, static_cast<int>(i));
        // The band's place on the picture: the strokes keep their positions in every band.
        const PixelGrid band_grid =
            stage_grid(base.width, base.height, r.x, r.y, 1, r.width, r.height);
        // The band's part of the picture's field (read-only here: every band reads it at once).
        std::vector<float> band_field;
        FieldView band_view;
        if (field_at(field.values, field.grid, band_grid, band_field) && !band_field.empty()) {
            band_view = {band_field, band_grid};
        }
        std::optional<PreviewFrame> part =
            render_plain(crop, band_grid, band_settings, band_threads, band_stop.get_token(),
                         report, stop_after, band_options, band_view);
        if (!part) {
            band_stop.request_stop();
            return;
        }
        for (int y = 0; y < r.height; ++y) {
            const auto src = static_cast<std::size_t>(y) * r.width;
            const auto dst = static_cast<std::size_t>(r.y + y) * width + r.x;
            std::copy_n(part->rgb8.begin() + static_cast<std::ptrdiff_t>(src * ch),
                        static_cast<std::size_t>(r.width) * ch,
                        frame.rgb8.begin() + static_cast<std::ptrdiff_t>(dst * ch));
            if (options.clip_mask) {
                std::copy_n(part->clip_mask.begin() + static_cast<std::ptrdiff_t>(src),
                            static_cast<std::size_t>(r.width),
                            frame.clip_mask.begin() + static_cast<std::ptrdiff_t>(dst));
            }
        }
        frame.strip[i].histogram = part->histogram;
        frame.strip[i].scopes = std::move(part->scopes);
        if (i == middle) {
            frame.stages = std::move(part->stages);
        }
    };
    const auto keep_error = [&] {
        {
            std::lock_guard lock(error_mutex);
            if (!error) {
                error = std::current_exception();
            }
        }
        band_stop.request_stop();
    };
    // The calling thread takes order[0] itself; everyone then takes the next band left.
    std::atomic<std::size_t> next{1};
    const auto take_bands = [&] {
        try {
            for (std::size_t k = next++; k < order.size() && !band_stop.stop_requested(); k = next++) {
                render_band(order[k], {});
            }
        } catch (...) {
            keep_error();
        }
    };

    {
        // Joined on every way out of this block, so no worker outlives the call.
        std::vector<std::jthread> workers;
        try {
            workers.reserve(static_cast<std::size_t>(concurrent - 1));
            for (int w = 1; w < concurrent; ++w) {
                workers.emplace_back(take_bands);
            }
            if (!order.empty() && !band_stop.stop_requested()) {
                render_band(order[0], order[0] == middle ? on_stage : StageCallback{});
            }
        } catch (...) {
            keep_error();  // a worker that could not start, or the middle band
        }
        take_bands();
    }
    if (error) {
        std::rethrow_exception(error);
    }
    if (band_stop.stop_requested() || stop.stop_requested()) {
        return std::nullopt;
    }
    frame.histogram = middle < frame.strip.size() ? frame.strip[middle].histogram : StageHistogram{};
    if (middle < frame.strip.size()) {
        frame.scopes = frame.strip[middle].scopes;
    }
    frame.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return frame;
}

}  // namespace

namespace {

std::optional<PreviewFrame> render_plain(const Image& base, const PixelGrid& grid,
                                         const EditSettings& settings, int threads,
                                         std::stop_token stop, const StageCallback& on_stage,
                                         StageId stop_after, PreviewOptions options,
                                         const FieldView& field) {
    using Clock = std::chrono::steady_clock;
    options.test_strip.reset();
    const auto start = Clock::now();
    std::vector<float> field_part;  // only if `field` is not on `grid` (it always is)
    ProcessSettings process_settings =
        to_process_settings(settings, stop_after, options.as_shot_white, grid);
    use_field(process_settings, field, field_part);

    PreviewFrame frame;
    frame.stage = stop_after;
    // The viewed stage as compare is no comparison. `compare_early`: the compare stage comes
    // first, so its picture is taken on the way to the viewed one; otherwise the render goes on
    // from the viewed stage to it.
    std::optional<StageId> compare = options.compare_stage;
    if (compare == stop_after) {
        compare.reset();
    }
    const bool compare_early = compare && static_cast<int>(*compare) < static_cast<int>(stop_after);
    const bool compare_late = compare && !compare_early;
    bool compare_taken = false;
    const auto take_compare = [&](const Image& at) {
        CompareFrame c;
        c.stage = *compare;
        c.width = at.width;
        c.height = at.height;
        c.rgb8 = encode_srgb8(at, threads);
        frame.compare = std::move(c);
        compare_taken = true;
    };
    // Decided once here; the per-stage callback below checks the recorder, never a pixel loop.
    std::optional<ClipRecorder> recorder;
    if (options.clip_report) {
        recorder.emplace(base.width, base.height);
    }

    // Pixels that a stage before the viewed one clamps away (colours outside the gamut, shadows):
    // after that stage they look like ordinary pixels, so each clamping stage first marks what it
    // is about to lose in `lost`, with one read-only pass over its input. The viewed stage's own
    // mask is then combined with it. Only made while the mask is wanted.
    std::vector<std::uint8_t> lost;
    // Once a stage has clamped negatives there are none left, and no later stage makes new ones
    // (the matrix, which can, comes before them), so no further pass is needed.
    bool negatives_clamped = false;
    const auto before_stage = [&](std::string_view name, const Image& input) {
        const std::optional<StageId> id = stage_from_name(name);
        // The first stage that runs after the compare stage receives the compare stage's picture
        // (a stage that is off never runs, and the picture passes through it unchanged).
        if (compare_early && !compare_taken && id &&
            static_cast<int>(*id) > static_cast<int>(*compare)) {
            take_compare(input);
        }
        if (options.clip_mask && id && !negatives_clamped &&
            stage_clamps(*id, ClipDirection::shadows)) {
            mark_lost_negatives(input, threads, lost);
            negatives_clamped = true;
        }
    };

    // Everything that reads the image at the viewed stage: the picture and its histogram, then
    // the scopes and the clip mask, which are only made if asked for.
    const auto show = [&](const Image& shown) {
        frame.width = shown.width;
        frame.height = shown.height;
        encode_srgb8_with_histogram(shown, threads, frame.rgb8, frame.histogram,
                                    stop_after == StageId::decode);
        if (options.scopes) {
            frame.scopes = compute_scopes(frame.rgb8, frame.width, frame.height);
        }
        if (options.clip_mask) {
            compute_clip_mask(shown, threads, frame.clip_mask, stop_after == StageId::decode,
                              lost.empty() ? nullptr : &lost);
        }
    };
    // The compare stage after the viewed one: carries on from the viewed picture, on the same
    // buffer, once everything that reads the viewed stage is done. No recorder: the clip report
    // describes the viewed stage and was made without this.
    const auto run_compare_late = [&](Image& current) {
        if (!compare_late) {
            return true;
        }
        ProcessSettings more = process_settings_after(
            to_process_settings(settings, *compare, options.as_shot_white, grid), stop_after);
        use_field(more, field, field_part);
        if (process(current, more, threads, stop) == ProcessResult::cancelled ||
            stop.stop_requested()) {
            return false;
        }
        take_compare(current);
        return true;
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
            Image sampled = recorder->sample_grid(current);
            const auto record_stage = [&](const StageTime& t) {
                if (const std::optional<StageId> id = stage_from_name(t.name)) {
                    recorder->stage_done_samples(*id, sampled);
                }
            };
            ProcessSettings rest = process_settings_after(
                to_process_settings(settings, final_stage, options.as_shot_white,
                                    recorder->grid(grid)),
                stop_after);
            std::vector<float> sampled_field;  // the field at the sampled pixels
            use_field(rest, field, sampled_field);
            // The grid is at most 250,000 pixels: one thread, below the parallel threshold anyway.
            if (process(sampled, rest, 1, stop, record_stage) == ProcessResult::cancelled ||
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
        if (compare_late) {
            Image img = base;
            if (!run_compare_late(img)) {
                return std::nullopt;
            }
        }
        return finish();
    }

    Image img = base;  // the base is shared and never changes; the pipeline works in place
    if (recorder) {
        recorder->decoded(img);
    }
    const auto record = [&](const StageTime& t) {
        frame.stages.push_back(t);
        if (recorder) {
            if (const std::optional<StageId> id = stage_from_name(t.name)) {
                recorder->stage_done(*id, img);
            }
        }
        if (on_stage) {
            on_stage(t);
        }
    };
    // Only a mask render looks at the stages' input; a report-only one does no extra pass.
    if (process(img, process_settings, threads, stop, record,
                options.clip_mask || compare_early
                    ? BeforeStageCallback(before_stage)
                    : BeforeStageCallback{}) == ProcessResult::cancelled ||
        stop.stop_requested()) {
        return std::nullopt;
    }
    if (compare_early && !compare_taken) {
        take_compare(img);  // no stage after the compare stage ran: the viewed picture is it
    }
    show(img);
    if (!run_rest(img) || !run_compare_late(img)) {
        return std::nullopt;
    }
    return finish();
}

}  // namespace

std::optional<PreviewFrame> render_preview(const Image& base, const EditSettings& settings,
                                           int threads, std::stop_token stop,
                                           const StageCallback& on_stage, StageId stop_after,
                                           PreviewOptions options, FieldCache* field_cache) {
    const PixelGrid whole = stage_grid(base.width, base.height);
    // Whether the Dodge & burn stage can run in this render: up to the viewed stage, or after it
    // for the clip report or a later compare stage.
    const auto reaches = [](StageId stage) {
        return static_cast<int>(stage) >= static_cast<int>(StageId::dodge_burn);
    };
    const bool stage_runs =
        !settings.strokes.empty() && settings.dodge_burn_on &&
        (reaches(stop_after) || options.clip_report ||
         (options.compare_stage && reaches(*options.compare_stage)));
    const bool mask = options.stroke_mask;
    FieldView field;
    FieldCache just_this_render;  // for a mask without a cache
    if (field_cache && (settings.strokes.empty() || stage_runs || mask)) {
        field_cache->update(settings.strokes, whole, threads);
        field = {field_cache->field(), whole};
    } else if (!field_cache && mask && !settings.strokes.empty()) {
        just_this_render.update(settings.strokes, whole, threads);
        field = {just_this_render.field(), whole};
    }
    std::optional<PreviewFrame> frame;
    if (options.test_strip && stop_after != StageId::decode && options.test_strip->count >= 1 &&
        base.width > 0 && base.height > 0) {
        frame = render_strip(base, settings, threads, stop, on_stage, stop_after, options, field);
    } else {
        frame = render_plain(base, whole, settings, threads, stop, on_stage, stop_after,
                             std::move(options), field);
    }
    if (frame && mask) {
        // One byte per pixel of the picture, from the same field the stage used.
        const auto width = static_cast<std::size_t>(base.width);
        frame->stroke_mask.assign(width * static_cast<std::size_t>(base.height), 128);
        if (!field.values.empty()) {
            const int n = threads_for_pixels(frame->stroke_mask.size(), threads);
            parallel_for_rows(base.height, n, [&](int begin, int end) {
                for (std::size_t p = static_cast<std::size_t>(begin) * width;
                     p < static_cast<std::size_t>(end) * width; ++p) {
                    frame->stroke_mask[p] = stroke_mask_byte(field.values[p]);
                }
            });
        }
    }
    return frame;
}

// The cache is the worker's: only the worker thread calls render_.
PreviewRenderer::PreviewRenderer(int threads)
    : PreviewRenderer(OptionsRenderFunction(
          [threads, cache = std::make_shared<FieldCache>()](
              const Image& base, const EditSettings& settings, StageId stop_after,
              const PreviewOptions& options, std::stop_token stop) {
              return render_preview(base, settings, threads, stop, {}, stop_after, options,
                                    cache.get());
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
        // Both under the lock, the thread's stop first. With the thread's stop asked for after
        // the lock was let go, a worker whose render had just given up could take the pending
        // request in between and start it with a fresh stop source that nothing would stop,
        // so join() waited for that whole render.
        std::lock_guard lock(mutex_);
        worker_.request_stop();       // wakes the worker if it is waiting for a request
        render_stop_.request_stop();  // the render in progress, if any, gives up at its next check
    }
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
        // Replaces a waiting request. The running render is left to finish (and be published)
        // unless it is of another image, whose frame nobody wants.
        if (busy_ && running_base_ != base) {
            render_stop_.request_stop();
        }
        pending_ = Request{generation, std::move(base), settings, stop_after, options};
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
        running_base_ = request.base;
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

        // Publish whatever finished, even if a newer request has arrived meanwhile: during a drag
        // that is what keeps the picture moving. Only a stopped render (another image, shutdown)
        // is dropped. Requests run in order, so published generations only increase.
        running_base_.reset();
        if (frame && !render_stop.stop_requested()) {
            frame->generation = request.generation;
            frame->stage = request.stop_after;
            ready_ = std::move(frame);
        }
        busy_ = false;
        idle_.notify_all();
    }
}

}  // namespace prism::gui

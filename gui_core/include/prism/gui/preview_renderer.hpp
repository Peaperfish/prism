#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "prism/gui/clipping.hpp"
#include "prism/gui/field_cache.hpp"
#include "prism/gui/histogram.hpp"
#include "prism/gui/scopes.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"

namespace prism::gui {

// What a render computes besides the picture and its histogram. Each costs something, so each is
// asked for only while it is on screen; with all off, a render does exactly what it did before.
struct PreviewOptions {
    bool scopes = false;       // the colour scopes of the picture
    bool clip_report = false;  // the clipping of every stage (runs the stages after the viewed one)
    bool clip_mask = false;    // which pixels of the viewed stage are out of range, or were clipped
    // A second picture, of this stage, to compare with the viewed one (PreviewFrame::compare). It
    // carries no histogram, scopes or clip data: those always describe the viewed stage. The
    // viewed stage itself as compare means no comparison, the same as nothing.
    std::optional<StageId> compare_stage;
    // The test strip (test_strip.hpp): the picture is cut into bands along its long side and each
    // band is rendered at the current exposure plus its own offset, into one frame of the normal
    // size (PreviewFrame::strip has each band's histogram and scopes). Only for the Exposure stage
    // and later: a strip of the Decode stage is ignored. The clip report is not computed, and
    // compare_stage is ignored, while it is on (the UI keeps the two exclusive anyway).
    std::optional<TestStrip> test_strip;
    // Not something extra to compute, but what the photo is: its as-shot light, which a chosen
    // white balance is measured against (see to_process_settings). It rides here so that it
    // reaches the worker with the request. None for a photo without one.
    std::optional<Chromaticity> as_shot_white;
    // The stroke mask (PreviewFrame::stroke_mask): where the Dodge & burn strokes are, and how
    // strongly they dodge or burn, one byte per pixel of the picture.
    bool stroke_mask = false;
    // Like as_shot_white, what the picture is rather than something to compute: where the base sits
    // on the photo when it is the framed base (Session::preview_framing), so that the strokes land
    // where they were drawn. None for a base that is the whole photo. It must match the settings'
    // frame and the base's size (check_framed_base), or the render fails with that message.
    std::optional<PictureFrame> framing;

    PreviewOptions() = default;
    // Implicit, so that "scopes on/off" can still be written as a plain bool.
    PreviewOptions(bool scopes_on) : scopes(scopes_on) {}  // NOLINT(google-explicit-constructor)

    bool operator==(const PreviewOptions&) const = default;
};

// The picture of the compare stage: 8-bit sRGB only, with nothing computed from it.
struct CompareFrame {
    StageId stage = StageId::decode;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb8;  // width * height * 3 bytes, as PreviewFrame::rgb8
};

// One band of a test strip render: where it is in the picture, and what describes it.
struct StripBandFrame {
    StripRect rect;                 // in picture pixels; empty if the picture has fewer pixels than bands
    StageHistogram histogram;       // of this band only
    std::optional<Scopes> scopes;   // of this band only; only if options.scopes
};

// One finished preview, ready to upload as a texture, together with the histogram of the stage
// it shows (computed from that stage's values before the 8-bit encode).
struct PreviewFrame {
    std::uint64_t generation = 0;  // the number submit() returned for its request
    StageId stage = final_stage;   // the stage whose output this frame shows
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb8;  // width * height * 3 bytes of 8-bit sRGB, row-major
    StageHistogram histogram;        // R, G, B and luminance bins + clip counts for this stage
    std::optional<Scopes> scopes;    // parade, waveform and vectorscope; only if requested
    std::optional<ClipReport> clips;  // clipping at every stage; only if requested
    // One byte per pixel (clip_mask_high / _low / _gamut bits) of this stage; only if requested.
    // A pixel that an earlier stage clamped (colours outside the gamut, shadows) stays marked.
    std::vector<std::uint8_t> clip_mask;
    // One byte per pixel (width * height, row-major) of the strokes' field on the picture,
    // stroke_mask_byte(field): 128 where no stroke reaches, above it dodged, below it burned, 255
    // and 1 at 2 stops or more. All 128 without strokes. Only if options.stroke_mask. It shows the
    // strokes whatever stage is viewed and whether Dodge & burn is on, and with a test strip it
    // covers the whole picture (the strokes are the same in every band).
    std::vector<std::uint8_t> stroke_mask;
    // With a test strip: one entry per band, in order. `histogram` and `scopes` above then
    // describe the middle band (the current exposure), and `clips` is never set. The clip mask is
    // per band: each band's pixels are marked by that band's own exposure.
    std::vector<StripBandFrame> strip;
    // The compare stage's picture, from the same render as `rgb8` (so the same settings); only if
    // options.compare_stage was set to a stage other than the viewed one.
    std::optional<CompareFrame> compare;
    std::vector<StageTime> stages;   // the pipeline stages that ran, with their times
    double total_ms = 0;             // the whole render: copy, pipeline, histogram and sRGB encode
    std::string error;               // if not empty, rendering failed and rgb8 is empty
};

// Runs the pipeline on a copy of `base` up to and including `stop_after` (`final_stage` runs
// every edit stage) and returns the linear float image at that point, before the 8-bit sRGB
// encode. `framing` is where `base` sits on the photo (PreviewOptions::framing). Nothing if `stop`
// was requested before it finished. Throws std::invalid_argument for invalid settings (see
// to_process_settings) or a `framing` that does not fit them (check_framed_base).
[[nodiscard]] std::optional<Image> render_stage_image(
    const Image& base, const EditSettings& settings, StageId stop_after, int threads = 0,
    std::stop_token stop = {}, const StageCallback& on_stage = {},
    const std::optional<Chromaticity>& as_shot_white = std::nullopt,
    const std::optional<PictureFrame>& framing = std::nullopt);

// Renders `base` with `settings` up to `stop_after` on the calling thread: runs only the stages
// up to `stop_after` and then encodes that stage's image to 8-bit sRGB with encode_srgb8. With
// `options.scopes`, the frame also carries the colour scopes of the encoded picture. With
// `clip_report`, the stages after `stop_after` also run (on the same buffer, after the picture is
// encoded) so that every stage's clipping is measured; the frame still shows `stop_after`. With
// `clip_mask`, the frame carries the out-of-range pixels of `stop_after`. With none of them,
// nothing extra is computed. With `compare_stage` (other than `stop_after`), the same render also
// encodes that stage's picture into `compare`: one pass through the pipeline up to the later of the
// two, not two renders. Everything else in the frame is exactly what it is without it. Nothing if
// `stop` was requested before it finished. Throws std::invalid_argument for invalid settings (see
// to_process_settings).
//
// With `field_cache`, the strokes' field is taken from it (brought up to date first: see
// FieldCache) instead of being rasterised by the Dodge & burn stage, by every band of a test
// strip and by the clip report's sample grid; the frame is bit-identical either way. Only the
// thread that owns the cache may pass it. Without one, a render rasterises as it always did (and
// a stroke mask is made from a field rasterised just for it).
//
// With options.framing, `base` is the framed base and every grid (the picture, each band, the clip
// report's sample grid) is a framed grid of the photo (stage_grid). Throws std::invalid_argument if
// options.framing does not fit the settings and `base` (check_framed_base).
[[nodiscard]] std::optional<PreviewFrame> render_preview(const Image& base,
                                                         const EditSettings& settings, int threads,
                                                         std::stop_token stop,
                                                         const StageCallback& on_stage = {},
                                                         StageId stop_after = final_stage,
                                                         PreviewOptions options = {},
                                                         FieldCache* field_cache = nullptr);

// Renders previews on a worker thread, so the UI never waits for one.
//
//   UI thread, when the settings change:  renderer.submit(session.preview_base(), settings);
//   UI thread, every frame:               if (auto frame = renderer.poll()) upload(*frame);
//
// Latest wins, one pending slot: submit() replaces a request that has not started yet, so a slider
// drag never builds up a queue. A render already running is NOT abandoned for a newer request of
// the same image: it finishes and is published, and then the worker renders the latest pending
// request. (Cancelling it would mean that when a render takes longer than the gap between
// changes, as in a drag, nothing is ever published.) A frame can therefore be older than the
// latest request; its `generation` says so. Only a request for another base image (another
// photo) stops the running render, through the std::stop_token passed to process(), which checks
// it between stages; so does the destructor. A stopped render never produces a frame. The
// finished frame waits in a mailbox until poll() takes it; nothing ever calls back into UI code.
// The destructor stops the current render and waits for the thread.
//
// All public functions are thread-safe, but are meant to be called from one (UI) thread.
class PreviewRenderer {
public:
    // How a request becomes a frame. The default is render_preview with the renderer's thread
    // count; tests pass their own to control timing. It runs on the worker thread, gets the stop
    // token of its request, and returns nothing if it gave up. If it throws, the frame carries
    // the exception's message in `error`.
    using RenderFunction = std::function<std::optional<PreviewFrame>(
        const Image& base, const EditSettings& settings, std::stop_token stop)>;
    using StageRenderFunction = std::function<std::optional<PreviewFrame>(
        const Image& base, const EditSettings& settings, StageId stop_after, std::stop_token stop)>;
    // As above, and told whether the frame should carry colour scopes.
    using ScopeRenderFunction = std::function<std::optional<PreviewFrame>(
        const Image& base, const EditSettings& settings, StageId stop_after, bool scopes,
        std::stop_token stop)>;
    // As above, and told everything the frame should carry.
    using OptionsRenderFunction = std::function<std::optional<PreviewFrame>(
        const Image& base, const EditSettings& settings, StageId stop_after,
        const PreviewOptions& options, std::stop_token stop)>;

    // `threads` as for the pipeline: 0 = one per hardware thread. The worker keeps a FieldCache
    // of its own for the strokes, used by no other thread.
    explicit PreviewRenderer(int threads = 0);
    explicit PreviewRenderer(RenderFunction render);
    explicit PreviewRenderer(StageRenderFunction render);
    explicit PreviewRenderer(ScopeRenderFunction render);
    explicit PreviewRenderer(OptionsRenderFunction render);
    ~PreviewRenderer();

    // A running thread can't be copied or moved somewhere else.
    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;

    // Asks for a preview of `base` with `settings` stopped after `stop_after` (with whatever
    // `options` asks for), and returns its generation number (1, 2, 3... in the order of the
    // calls). Returns at once: it only swaps the request in under a lock that the worker never
    // holds while rendering. It stops the running render only if that one has another `base`.
    // `base` is shared, not copied, so it must not change (Session's preview
    // base never does). Throws std::invalid_argument if `base` is null.
    std::uint64_t submit(std::shared_ptr<const Image> base, const EditSettings& settings,
                         StageId stop_after = final_stage, PreviewOptions options = {});

    // The newest finished frame, if one arrived since the last poll(); otherwise nothing.
    // Its generation can be older than the latest submit()'s (see above); they only increase.
    [[nodiscard]] std::optional<PreviewFrame> poll();

    // Waits until no request is waiting or being rendered, for at most `timeout`; false if that
    // did not happen in time. For tests and headless use; the UI just polls.
    bool wait_until_idle(std::chrono::milliseconds timeout);

private:
    struct Request {
        std::uint64_t generation;
        std::shared_ptr<const Image> base;
        EditSettings settings;
        StageId stop_after = final_stage;
        PreviewOptions options;
    };

    void run(std::stop_token thread_stop);

    OptionsRenderFunction render_;

    std::mutex mutex_;  // guards everything below except worker_
    // condition_variable_any rather than condition_variable: only it can also wait for a
    // std::stop_token, so destroying the renderer wakes the worker at once.
    std::condition_variable_any wake_;  // a request arrived (or the thread should stop)
    std::condition_variable_any idle_;  // the worker finished a request
    std::optional<Request> pending_;
    std::optional<PreviewFrame> ready_;
    std::uint64_t latest_generation_ = 0;
    bool busy_ = false;
    std::shared_ptr<const Image> running_base_;  // the base of the request being rendered
    std::stop_source render_stop_;  // stops the request being rendered

    // Declared last so it starts after everything above exists, and is destroyed (joined) first.
    std::jthread worker_;
};

}  // namespace prism::gui

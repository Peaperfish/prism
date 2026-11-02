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
#include "prism/gui/histogram.hpp"
#include "prism/gui/scopes.hpp"
#include "prism/gui/settings.hpp"
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
    // The compare stage's picture, from the same render as `rgb8` (so the same settings); only if
    // options.compare_stage was set to a stage other than the viewed one.
    std::optional<CompareFrame> compare;
    std::vector<StageTime> stages;   // the pipeline stages that ran, with their times
    double total_ms = 0;             // the whole render: copy, pipeline, histogram and sRGB encode
    std::string error;               // if not empty, rendering failed and rgb8 is empty
};

// Runs the pipeline on a copy of `base` up to and including `stop_after` (`final_stage` runs
// every edit stage) and returns the linear float image at that point, before the 8-bit sRGB
// encode. Nothing if `stop` was requested before it finished. Throws std::invalid_argument for
// invalid settings (see to_process_settings).
[[nodiscard]] std::optional<Image> render_stage_image(const Image& base,
                                                      const EditSettings& settings,
                                                      StageId stop_after, int threads = 0,
                                                      std::stop_token stop = {},
                                                      const StageCallback& on_stage = {});

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
[[nodiscard]] std::optional<PreviewFrame> render_preview(const Image& base,
                                                         const EditSettings& settings, int threads,
                                                         std::stop_token stop,
                                                         const StageCallback& on_stage = {},
                                                         StageId stop_after = final_stage,
                                                         PreviewOptions options = {});

// Renders previews on a worker thread, so the UI never waits for one.
//
//   UI thread, when the settings change:  renderer.submit(session.preview_base(), settings);
//   UI thread, every frame:               if (auto frame = renderer.poll()) upload(*frame);
//
// Latest wins: submit() replaces a request that has not started yet and stops the one being
// rendered (through the std::stop_token passed to process(), which checks it between stages),
// so a slider drag never builds up a queue. A render that was superseded before it finished never
// produces a frame. The finished frame waits in a mailbox until poll() takes it; nothing ever
// calls back into UI code. The destructor stops the current render and waits for the thread.
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

    // `threads` as for the pipeline: 0 = one per hardware thread.
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
    // holds while rendering. `base` is shared, not copied, so it must not change (Session's preview
    // base never does). Throws std::invalid_argument if `base` is null.
    std::uint64_t submit(std::shared_ptr<const Image> base, const EditSettings& settings,
                         StageId stop_after = final_stage, PreviewOptions options = {});

    // The newest finished frame, if one arrived since the last poll(); otherwise nothing.
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
    std::stop_source render_stop_;  // stops the request being rendered

    // Declared last so it starts after everything above exists, and is destroyed (joined) first.
    std::jthread worker_;
};

}  // namespace prism::gui

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

#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"

namespace prism::gui {

// One finished preview, ready to upload as a texture.
struct PreviewFrame {
    std::uint64_t generation = 0;  // the number submit() returned for its request
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb8;  // width * height * 3 bytes of 8-bit sRGB, row-major
    std::vector<StageTime> stages;   // the pipeline stages that ran, with their times
    double total_ms = 0;             // the whole render: copy, pipeline and sRGB encode
    std::string error;               // if not empty, rendering failed and rgb8 is empty
};

// Renders `base` with `settings` on the calling thread: copies it, runs process() and encodes
// it to 8-bit sRGB with encode_srgb8. Nothing if `stop` was requested before it finished.
// Throws std::invalid_argument for invalid settings (see to_process_settings).
[[nodiscard]] std::optional<PreviewFrame> render_preview(const Image& base,
                                                         const EditSettings& settings, int threads,
                                                         std::stop_token stop,
                                                         const StageCallback& on_stage = {});

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

    // `threads` as for the pipeline: 0 = one per hardware thread.
    explicit PreviewRenderer(int threads = 0);
    explicit PreviewRenderer(RenderFunction render);
    ~PreviewRenderer();

    // A running thread can't be copied or moved somewhere else.
    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;

    // Asks for a preview of `base` with `settings` and returns its generation number (1, 2, 3...
    // in the order of the calls). Returns at once: it only swaps the request in under a lock that
    // the worker never holds while rendering. `base` is shared, not copied, so it must not change
    // (Session's preview base never does). Throws std::invalid_argument if `base` is null.
    std::uint64_t submit(std::shared_ptr<const Image> base, const EditSettings& settings);

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
    };

    void run(std::stop_token thread_stop);

    RenderFunction render_;

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

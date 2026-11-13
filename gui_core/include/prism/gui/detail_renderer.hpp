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

#include "prism/frame.hpp"
#include "prism/gui/detail_view.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/resize.hpp"

// Real-pixel zoom (#37): renders the requested region of the picture from the detail base on a
// worker thread of its own, for the canvas to draw over the enlarged preview.

namespace prism::gui {

// --- The renderer's decisions, as pure functions -------------------------------------------------

// What a detail render is for, as the caller numbers it. A frame for another `base` than the one
// now shown is never shown; `settings` says which settings it shows, for the caller.
struct DetailKey {
    std::uint64_t base = 0;      // the detail base: another photo, or the same photo read again,
                                 // gets another number
    std::uint64_t settings = 0;  // the settings generation the request was made with
    bool operator==(const DetailKey&) const = default;
};

// Whether every pixel of `inner` is a pixel of `outer`. An empty `inner` is inside anything.
[[nodiscard]] bool box_contains(const FrameBox& outer, const FrameBox& inner) noexcept;

// A render, as the stop rule sees it: which base, by pointer and by the caller's number, and which
// region of the framed picture.
struct DetailJob {
    const void* base = nullptr;
    std::uint64_t base_id = 0;
    FrameBox region;
};

// The stop rule (#37 section 5): a newer request stops the running render when it is of another
// base (pointer or number), or when its region is not inside the running one (a pan out). A
// request for the same or a covered region with other settings does not stop it: it finishes and
// is shown, so the picture keeps moving during a slider drag, and the newer one runs next.
[[nodiscard]] bool detail_stops_running(const DetailJob& running, const DetailJob& wanted) noexcept;

// Whether a request is rendered, from its budget (detail_request) and whether the caller says a
// gesture is under way (a drag, pan, wheel or brush stroke); the renderer never guesses that.
enum class DetailAdmit {
    render,           // rendered (RegionBudget::live, or on_release with no gesture)
    refused_too_big,  // RegionBudget::never: over 30 MP, never rendered
    refused_gesture,  // RegionBudget::on_release during a gesture: ask again once it is over
};
[[nodiscard]] DetailAdmit detail_admit(RegionBudget budget, bool gesture) noexcept;

// Whether a finished frame is shown: only one of the base now shown (`current_base`), and only one
// newer than the frame already shown (generations are submit()'s numbers; 0 is none shown).
[[nodiscard]] bool detail_frame_wanted(std::uint64_t frame_base, std::uint64_t frame_generation,
                                       std::uint64_t current_base,
                                       std::uint64_t shown_generation) noexcept;

// --- The renderer --------------------------------------------------------------------------------

// What the canvas asks for.
struct DetailRenderRequest {
    // The full-size photo to render from (Session::detail_base). Held for the render, so the
    // session may let go of it meanwhile; never copied.
    std::shared_ptr<const Photo> base;
    EditSettings settings;
    DetailRequest request;  // the region (detail_request's snapped one) and its budget
    PixelSize output;       // detail_output_size for that region and the view
    DetailKey key;
};

// One finished region, ready to upload as a texture.
struct DetailFrame {
    std::uint64_t generation = 0;  // submit()'s number for the request
    DetailKey key;
    FrameBox region;  // the file pixels it shows, in pixels of the whole framed picture
    // The size of that whole framed picture: plan_export_frame(settings.frame, base size).size for
    // the request's settings and base. A frame made before the frame (crop or turn) changed is in
    // another picture, and the canvas must not place it on this one. 0 x 0 on an error frame.
    PixelSize picture{0, 0};
    // width x height: the region itself at 100% and above, smaller below (the request's output).
    PixelSize size;
    // size.width * size.height * 3 bytes of 8-bit sRGB, row-major, encoded by encode_srgb8 as the
    // preview's pictures are. Below 100% the region is first shrunk to `size` by area averaging
    // (prism::downscale) on linear light.
    std::vector<std::uint8_t> rgb8;
    std::string error;  // if not empty, the render failed and rgb8 is empty
};

// Renders detail regions on a worker thread, so the UI never waits for one.
//
//   UI thread, when the view or the settings change:  renderer.submit(request, gesture);
//   UI thread, every frame:                           if (auto f = renderer.poll(base)) upload(*f);
//
// At most one render runs and one request waits; a newer request replaces the waiting one (latest
// wins). The running render is stopped by the stop rule (detail_stops_running): a request of
// another base, or one whose region is not inside it. Otherwise it finishes and is shown, and the
// waiting request runs next. A stopped render never produces a frame: render_region gives nothing
// once stopped, and the worker checks the stop again before the shrink, before the encode, and
// under the lock where it posts, so a stop asked for before a frame is posted always keeps it
// out. A refused request (detail_admit) renders nothing; it drops the waiting request, which the
// view has moved on from, and the stop rule still applies to the running one.
//
// The render uses `threads` (0: one per hardware thread) for render_region, the shrink and the
// encode, as the preview render does; the two share the machine. The finished frame waits until
// poll() takes it; nothing calls back into UI code. The destructor stops the running render and
// waits for the thread.
class DetailRenderer {
public:
    // How a region of the framed picture becomes linear pixels. The default is render_region with
    // the renderer's thread count; tests pass their own to control timing. Runs on the worker
    // thread; nothing means it was stopped. If it throws, the frame carries the message in
    // `error` (a fixed one for an exception not derived from std::exception).
    using RenderFunction =
        std::function<std::optional<Image>(const Photo& base, const EditSettings& settings,
                                           const FrameBox& region, std::stop_token stop)>;

    explicit DetailRenderer(int threads = 0);
    // `threads` is then only for the shrink and the encode.
    explicit DetailRenderer(RenderFunction render, int threads = 1);
    ~DetailRenderer();

    DetailRenderer(const DetailRenderer&) = delete;
    DetailRenderer& operator=(const DetailRenderer&) = delete;

    // UI thread. Asks for `request`, with `gesture` saying whether a gesture is under way, and
    // returns its generation (1, 2, 3... in the order of the requests admitted; a request replaced
    // while it waits never renders, so its number never comes back in a frame), or 0 if it was
    // refused (detail_admit). Returns at once. Throws std::invalid_argument for a null base, an
    // empty region, or an output size that is neither the region nor the region shrunk by
    // downscale (detail_output_size gives one of the two).
    std::uint64_t submit(DetailRenderRequest request, bool gesture);

    // UI thread. Stops the running render and drops the waiting request, for when nothing of the
    // current base is wanted any more: the session dropped or replaced the detail base, or real
    // pixels went off. Neither posts a frame, and the bases they hold are let go on the worker
    // thread. A frame already posted stays for poll(), which drops it if it is of another base.
    void clear();

    // UI thread. The newest finished frame, if one arrived since the last poll() and is wanted
    // (detail_frame_wanted) for the base now shown, `current_base` (DetailKey::base); otherwise
    // nothing. Frames of other bases, or older than the one last returned, are dropped. A frame
    // can show older settings than the current ones (a render the stop rule let finish, while the
    // newer request waits or was refused during a gesture): its key.settings says which, for the
    // caller to compare with its own.
    [[nodiscard]] std::optional<DetailFrame> poll(std::uint64_t current_base);

    // Waits until no request is waiting or being rendered, for at most `timeout`; false if that did
    // not happen in time. For tests and headless use; the UI just polls.
    bool wait_until_idle(std::chrono::milliseconds timeout);

private:
    struct Job {
        std::uint64_t generation = 0;
        DetailRenderRequest request;
    };

    void run(std::stop_token thread_stop);
    // Moves the waiting request, if any, to discarded_ for the worker to free. Under mutex_.
    void discard_pending();
    // An error frame for `job`.
    [[nodiscard]] static DetailFrame failed(const Job& job, std::string message);
    // The frame for `job`, or nothing if `stop` was asked for.
    std::optional<DetailFrame> render(const Job& job, std::stop_token stop) const;

    RenderFunction render_;
    int threads_ = 0;

    std::uint64_t shown_generation_ = 0;  // UI thread only: the last frame poll() returned

    std::mutex mutex_;                  // guards everything below except worker_
    std::condition_variable_any wake_;  // a request arrived (or the thread should stop)
    std::condition_variable_any idle_;  // the worker finished a request
    std::optional<Job> pending_;
    // Requests replaced or cleared before they ran, for the worker to free: they may hold the last
    // reference to a base, and freeing ~700 MB is not for the UI thread.
    std::vector<Job> discarded_;
    std::optional<DetailFrame> ready_;
    std::uint64_t latest_generation_ = 0;
    std::optional<DetailJob> running_;  // the render under way, if any
    bool busy_ = false;                 // a job was taken and is not yet finished and freed
    std::stop_source render_stop_;      // stops the render under way

    // Declared last so it starts after everything above exists, and is destroyed (joined) first.
    std::jthread worker_;
};

}  // namespace prism::gui

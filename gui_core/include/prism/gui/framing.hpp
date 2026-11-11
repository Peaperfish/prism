#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prism/frame.hpp"
#include "prism/gui/canvas_input.hpp"
#include "prism/gui/session.hpp"
#include "prism/resize.hpp"

namespace prism::gui {

// Framing on the easel (#58): everything the framing mode decides, as pure state and functions with
// no window involved. The app draws what these say and routes the canvas's pointer and keys here.
//
// While framing, the canvas shows the WHOLE photo (Session::whole_base) turned by the draft frame's
// angle, so that the frame's straightened axes are upright on screen: the blades stay square to you
// and the picture turns under them. The frame is edited as a draft; each gesture (a handle drag, a
// move, a horizon line, a slider drag, an aspect pick, X, Reset) ends by committing the draft to
// the settings as one undo step, and Esc during a gesture puts the draft back exactly as it was
// before the gesture, with no undo step. Leaving framing (Return, Done, C, Esc) keeps the frame.
//
// Sizes are in pixels of the photo the frame is edited on: the whole preview base, whose fractions
// are the file's too (prism/frame.hpp).

// --- The view while framing -----------------------------------------------------------------------

// A position on the canvas, in the canvas's own units (screen units in the app), from its top left
// corner, y down.
struct CanvasPoint {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const CanvasPoint&, const CanvasPoint&) = default;
};

// An upright rectangle on the canvas.
struct CanvasRect {
    CanvasPoint min;
    CanvasPoint max;

    friend bool operator==(const CanvasRect&, const CanvasRect&) = default;
};

// A guide line on the canvas.
struct CanvasLine {
    CanvasPoint from;
    CanvasPoint to;

    friend bool operator==(const CanvasLine&, const CanvasLine&) = default;
};

// How the turned photo sits on the canvas: a straightened position s (photo pixels along the
// frame's axes, prism/frame.hpp, with the photo's centre at (W / 2, H / 2)) is drawn at
//   canvas = centre + scale * (s - (W / 2, H / 2)),
// so straightened axes are the canvas's axes, and photo pixel positions turn by `angle` (clockwise
// on screen for a positive angle) about the photo's centre.
struct FramingView {
    float angle = 0.0f;
    int photo_width = 0;
    int photo_height = 0;
    double scale = 0.0;  // canvas units per photo pixel
    CanvasPoint centre;  // where the photo's centre is drawn

    friend bool operator==(const FramingView&, const FramingView&) = default;
};

// The view that fits the photo, turned by `angle`, into a canvas_width x canvas_height canvas with
// `margin` canvas units left clear on every side: the turned photo's bounding box is as large as
// fits, centred. A canvas no larger than twice the margin is treated as one unit wide (the photo is
// tiny, never inside out).
[[nodiscard]] FramingView framing_view(float angle, int photo_width, int photo_height,
                                       double canvas_width, double canvas_height, double margin);

// Where photo position `photo` (photo pixels, (0, 0) at its top left corner) is drawn, and its
// inverse. Round trips are exact to double rounding.
[[nodiscard]] CanvasPoint canvas_from_photo(const FramingView& view, CanvasPoint photo) noexcept;
[[nodiscard]] CanvasPoint photo_from_canvas(const FramingView& view, CanvasPoint canvas) noexcept;

// The photo's four corners on the canvas, in the order top left, top right, bottom right, bottom
// left of the photo (its texture's (0, 0), (1, 0), (1, 1), (0, 1)): the quad to draw the whole base
// on, and what the veil darkens outside the frame.
[[nodiscard]] std::array<CanvasPoint, 4> photo_corners(const FramingView& view) noexcept;

// `frame`'s rectangle on the canvas (upright: the view turns with the frame's angle).
[[nodiscard]] CanvasRect frame_rect(const FramingView& view, const Frame& frame) noexcept;

// --- Handles and drags ----------------------------------------------------------------------------

enum class FrameHandle {
    none,
    left,
    top,
    right,
    bottom,
    top_left,
    top_right,
    bottom_right,
    bottom_left,
    inside,  // the blades move together
};

// What a press at `at` takes hold of on `frame` drawn in `view`: a corner within `reach` canvas
// units of the point (the nearest, if several), else an edge within `reach` of its line between the
// corners, else the inside of the frame, else nothing.
[[nodiscard]] FrameHandle frame_handle_at(const FramingView& view, const Frame& frame,
                                          CanvasPoint at, double reach) noexcept;

// The smallest side a frame may have, in photo pixels: 1% of the photo's short side, and at least
// one pixel.
[[nodiscard]] double framing_min_side(int photo_width, int photo_height) noexcept;

// The result of an edit that fit_frame finished.
struct FrameEdit {
    Frame frame;
    // Fitting changed what was asked for (a smaller frame, or one moved back onto the photo), so
    // the frame is not where the pointer put it (framing_fit_note).
    bool fitted = false;
};

// `start` (the frame when the drag began) with `handle` dragged from `from` to `to` (canvas points
// in `view`, which shows `start`'s angle), on a photo_width x photo_height photo:
//  - an edge moves on its own, and the aspect lock switches to Free (#58);
//  - a corner moves with the opposite corner held; with an aspect lock the frame keeps its shape,
//    the largest of that shape inside the rectangle the pointer spans;
//  - inside moves the whole frame by the drag, keeping its size, and stops (sliding along) where a
//    corner would leave the photo;
// then fit_frame, so no corner shows. No side goes below framing_min_side. Nothing for
// FrameHandle::none, or if the result would be smaller than that (the caller keeps what it had).
// Throws as fit_frame if `start` is invalid.
[[nodiscard]] std::optional<FrameEdit> drag_frame(const Frame& start, FrameHandle handle,
                                                  CanvasPoint from, CanvasPoint to,
                                                  const FramingView& view, int photo_width,
                                                  int photo_height);

// `start` turned to `angle` degrees (clamped to -45..45) about the FRAME's own centre: the photo
// point under the frame's centre stays where it is, the frame keeps its size in pixels, and fit_frame
// then closes the blades in so no corner shows (moving the centre straight towards the photo's
// centre only if it had left the photo). Turning back to `start`'s own angle gives `start` exactly,
// so a gesture that computes every step from its start never shrinks for good (#58).
[[nodiscard]] FrameEdit turn_frame(const Frame& start, float angle, int photo_width,
                                   int photo_height);

// The angle that makes the line from `a` to `b` (canvas points in `view`) level, if it is within 45
// degrees of level on screen, otherwise upright: the Option/Alt-drag along a horizon (#58). Clamped
// to -45..45. Nothing for a line too short to say (under 4 canvas units).
[[nodiscard]] std::optional<float> straighten_angle(const FramingView& view, CanvasPoint a,
                                                    CanvasPoint b) noexcept;

// `f` with aspect `aspect` chosen: Free only unlocks it; any other shape is apply_aspect about the
// frame's centre, then fit_frame.
[[nodiscard]] FrameEdit choose_aspect(const Frame& f, Aspect aspect, int photo_width,
                                      int photo_height);

// `f` turned between landscape and portrait (X): Frame::portrait flips, the rectangle swaps its
// width and height in pixels about its centre (as far as the photo allows), then apply_aspect and
// fit_frame.
[[nodiscard]] FrameEdit turn_aspect(const Frame& f, int photo_width, int photo_height);

// --- The framing state ------------------------------------------------------------------------------

// What a gesture under way is.
enum class FramingGesture {
    none,
    handle,   // an edge or a corner
    move,     // inside the frame
    horizon,  // Option/Alt-drag along a line that should be level or upright
    turn,     // the Straighten slider
};

// Framing mode: the draft frame and the gesture under way. Not thread-safe; the UI thread's.
class FramingState {
public:
    // Enters framing with the settings' frame on a photo_width x photo_height photo (the whole
    // preview base). With no frame the draft is the whole photo, upright, with `lock` as its aspect
    // lock (Session::frame_lock), so a lock chosen on an uncropped photo is still there. Throws
    // std::invalid_argument if either size is below 1.
    void begin(const std::optional<Frame>& current, const FrameLock& lock, int photo_width,
               int photo_height);
    // The same for `session`: its settings' frame, its lock and its whole base. False (and framing
    // stays off) with no photo open.
    bool begin(const Session& session);
    // Leaves framing, keeping the frame: a gesture under way is committed first.
    [[nodiscard]] MaybeError leave(Session& session);
    [[nodiscard]] bool on() const noexcept { return on_; }

    [[nodiscard]] const Frame& draft() const noexcept { return draft_; }
    [[nodiscard]] PixelSize photo() const noexcept { return {photo_width_, photo_height_}; }
    [[nodiscard]] FramingGesture gesture() const noexcept { return gesture_; }
    [[nodiscard]] FrameHandle handle() const noexcept { return handle_; }
    // The last edit was changed by fitting (FrameEdit::fitted); cleared when a gesture begins.
    [[nodiscard]] bool fitted() const noexcept { return fitted_; }
    // The line of a horizon gesture under way, on the canvas.
    [[nodiscard]] std::optional<CanvasLine> horizon() const noexcept;

    // The canvas: a press in `view` (framing_view of the draft's angle) begins a gesture on the
    // handle under it (frame_handle_at with `reach`), or with Alt held a horizon line; false if
    // nothing was taken (outside the frame, not framing, or a gesture already under way).
    bool press(CanvasPoint at, bool alt, const FramingView& view, double reach);
    // The pointer moved: the draft follows, each step worked out from the gesture's start.
    void move(CanvasPoint at);
    // The release ends the gesture and commits the draft (commit).
    [[nodiscard]] MaybeError release(CanvasPoint at, Session& session);

    // The Straighten slider: begin_turn when its drag starts, turn_to as it moves, then commit.
    void begin_turn();
    void turn_to(float angle);

    // Ends the gesture under way (if any) and makes the draft the settings' frame through
    // Session::set_settings, which stores a whole-photo frame as none: one undo step, or none if
    // that is the frame there already. The draft's lock becomes the session's (FrameLock).
    [[nodiscard]] MaybeError commit(Session& session);
    // Esc during a gesture: the draft goes back exactly to what it was when the gesture began, and
    // no undo step is made. False (and nothing changes) with no gesture under way.
    bool cancel() noexcept;

    // One-step edits, each committed as one undo step: a typed angle, an aspect pick, X, and Reset
    // (upright, and the largest centred frame of the lock: the whole photo for Original or Free).
    [[nodiscard]] MaybeError set_angle(float angle, Session& session);
    [[nodiscard]] MaybeError set_aspect(Aspect aspect, Session& session);
    [[nodiscard]] MaybeError flip_aspect(Session& session);
    [[nodiscard]] MaybeError reset(Session& session);

    // The settings' frame changed from outside (undo, redo): the draft follows it, unless a gesture
    // is under way. With no frame it is the whole photo, keeping the draft's lock.
    void follow(const std::optional<Frame>& current);

private:
    void start(FramingGesture gesture);
    void apply(const FrameEdit& edit);

    bool on_ = false;
    int photo_width_ = 0;
    int photo_height_ = 0;
    Frame draft_;
    Frame start_;  // the draft when the gesture began
    FramingGesture gesture_ = FramingGesture::none;
    FrameHandle handle_ = FrameHandle::none;
    FramingView view_;  // the view at the press
    CanvasPoint from_;
    CanvasPoint to_;
    bool fitted_ = false;
};

// --- Guides -----------------------------------------------------------------------------------------

enum class FramingGuides {
    none,
    thirds,  // while a handle or the frame is dragged: the rule of thirds
    grid,    // while turning: a finer grid to line the picture up against
};

// The guides a gesture shows (#58): thirds for a handle or a move, the grid for a horizon line or
// the slider, none otherwise.
[[nodiscard]] FramingGuides framing_guides(FramingGesture gesture) noexcept;

// How many cells across and down the turning grid has.
inline constexpr int framing_grid_cells = 8;

// The guide lines inside `frame` (frame_rect): for thirds, two vertical lines then two horizontal
// ones; for the grid, framing_grid_cells - 1 of each, vertical first; none for none.
[[nodiscard]] std::vector<CanvasLine> framing_guide_lines(const CanvasRect& frame,
                                                          FramingGuides guides);

// --- Keys and the other tools -----------------------------------------------------------------------

enum class FramingKey {
    c,           // C: framing on or off
    return_key,  // Return: done
    escape,      // Esc: during a gesture, cancel it (otherwise escape_action decides)
    x,           // X: turn the aspect
};

enum class FramingKeyAction {
    none,
    begin_framing,  // enter framing (FramingState::begin)
    leave_framing,  // leave, keeping the frame (FramingState::leave)
    cancel_gesture,  // FramingState::cancel
    turn_aspect,    // FramingState::flip_aspect
};

struct FramingKeyContext {
    bool has_file = false;
    bool framing = false;       // framing mode is on
    bool gesture = false;       // a framing gesture is under way
    OpenSheets sheets;          // a modal sheet keeps its own keys (#54)
    bool text_editing = false;  // a text field has the keyboard
    bool stroking = false;      // a brush stroke is under way
};

// What `key` does: nothing behind a modal sheet (Return keeps its meaning there), while a text
// field has the keyboard, or during a brush stroke. C enters framing with a photo open and leaves it
// when on; Return leaves; Esc cancels a framing gesture under way (Esc otherwise goes to
// escape_action, where leaving framing comes first); X turns the aspect while framing.
[[nodiscard]] FramingKeyAction framing_key(FramingKey key, const FramingKeyContext& c) noexcept;

// The canvas tools when framing begins: all off (Brush, Sample, Pick and the test strip), and the
// comparison is dropped as well (#58).
[[nodiscard]] CanvasTools tools_when_framing(CanvasTools tools) noexcept;

// --- Scopes -----------------------------------------------------------------------------------------

enum class ScopeSource {
    framed_base,  // the picture as rendered: the framed part
    whole_base,   // while framing: the whole photo, as the canvas shows it
};

// What the scopes describe: the whole photo while framing (#58), the framed picture otherwise.
[[nodiscard]] ScopeSource scope_source(bool framing) noexcept;

// The words in the scopes' title for that: "whole photo, while framing", or nothing.
[[nodiscard]] std::string_view scope_source_detail(ScopeSource source) noexcept;

// --- Explanations -----------------------------------------------------------------------------------

// The angle in words, to a tenth of a degree: "Upright" at exactly 0, "turned 2.5° clockwise",
// "turned 0.5° anticlockwise", and "turned under 0.1° clockwise" for a turn too small to show.
[[nodiscard]] std::string framing_angle_text(float angle);

// The frame against the photo: "Frame 5120 × 3413 of 6000 × 4000 (73% of the photo)", the share
// by area, rounded, "under 1%" when it rounds to nothing. With `exact` false both sizes read
// "about …" (size_estimate_prefix).
[[nodiscard]] std::string framing_size_text(PixelSize framed, PixelSize photo, bool exact = true);

// The live line: the size, then the angle: "Frame 5120 × 3413 of 6000 × 4000 (73% of the photo),
// turned 1.2° clockwise." or "…, upright."
[[nodiscard]] std::string framing_live_line(PixelSize framed, PixelSize photo, float angle,
                                            bool exact = true);

// The aspect's short name: "Original", "Free", "1:1", "4:5", "5:7", "3:2", "16:9".
[[nodiscard]] std::string_view framing_aspect_name(Aspect aspect) noexcept;

// The aspect menu's entry: "4:5 — an 8×10 inch sheet", "Original — the photo's own shape", …
[[nodiscard]] std::string_view framing_aspect_menu_text(Aspect aspect) noexcept;

// Said when fitting changed what was dragged (FrameEdit::fitted), otherwise empty.
[[nodiscard]] std::string_view framing_fit_note(bool fitted) noexcept;

// #58's teaching words, for the Easel line, the Straighten slider, the resampling note and the
// framing bar's second line.
inline constexpr std::string_view easel_summary =
    "Easel: frames and straightens the print. It moves no light, so every stage sees only the "
    "framed part.";
inline constexpr std::string_view straighten_explanation =
    "Turns the easel under the enlarger: the picture turns about the frame's centre, and the "
    "blades close in so no corner runs past the photo's edge.";
inline constexpr std::string_view resampling_note =
    "Turning makes each pixel from the 16 around it, in linear light; at 0° no pixel moves.";
// The framing bar's second line: ⌥ on macOS, Alt elsewhere.
[[nodiscard]] std::string_view framing_hint(bool mac) noexcept;

}  // namespace prism::gui

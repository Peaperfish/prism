#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prism/frame.hpp"
#include "prism/gui/canvas_input.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/view_transform.hpp"
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

// --- The view while framing ----------------------------------------------------------------------

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

// The zoomable view while framing. The canvas's own zoom and pan (view_transform.hpp, so the wheel,
// pinch, Space and the zoom keys work as ever, #58) act on the turned photo's bounding box: a
// picture of framing_box_size, centred on the photo's centre, in a canvas `margin` units inside
// `canvas` on every side so the handles at the photo's edge stay on it. Points from these are in the
// canvas's screen units, with ViewGeometry's origin.
//
// The bounding box of the photo turned by `angle`, in photo pixels, rounded up (at least 1 x 1).
[[nodiscard]] PixelSize framing_box_size(float angle, int photo_width, int photo_height) noexcept;
// `canvas` (the canvas as it is) made into the geometry the view works on while framing: inset by
// `margin` (never below one unit across), with framing_box_size as its picture.
[[nodiscard]] ViewGeometry framing_geometry(const ViewGeometry& canvas, float angle,
                                            int photo_width, int photo_height, float margin) noexcept;
// The framing view for view state `s` on `g` (framing_geometry): the box placed as view_placement
// places a picture, the photo's centre at the box's centre. At fit, framing_view of the inset canvas
// to within a pixel's rounding.
[[nodiscard]] FramingView framing_view_placed(float angle, int photo_width, int photo_height,
                                              const ViewState& s, const ViewGeometry& g) noexcept;

// --- Handles and drags ---------------------------------------------------------------------------

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
    // Fitting made the frame smaller than what was asked for, or moved it back onto the photo
    // (framing_fit_note says so). Changes under a hundredth of a pixel do not count.
    bool fitted = false;
};

// `start` (the frame when the drag began) with `handle` dragged from `from` to `to` (canvas points
// in `view`, which shows `start`'s angle), on a photo_width x photo_height photo:
//  - an edge moves on its own, and the aspect lock switches to Free if it moved (#58);
//  - a corner moves with the opposite corner held; with an aspect lock the frame keeps its shape,
//    the largest of that shape inside the rectangle the pointer spans;
//  - an edge or a corner goes only as far as all four corners stay on the turned photo, the held
//    sides staying where they are;
//  - inside moves the whole frame by the drag, keeping its size: as far along the drag as every
//    corner stays on the photo, then each axis's rest on its own, so it slides along an upright
//    side of the straightened bounds (a slanted side of the turned photo stops it);
// then fit_frame as a last guard, so no corner shows. No side goes below framing_min_side. No move
// at all gives `start` back unchanged. Nothing for FrameHandle::none, or if the result would be
// smaller than the minimum (the caller keeps what it had). Throws as fit_frame if `start` is
// invalid.
[[nodiscard]] std::optional<FrameEdit> drag_frame(const Frame& start, FrameHandle handle,
                                                  CanvasPoint from, CanvasPoint to,
                                                  const FramingView& view, int photo_width,
                                                  int photo_height);

// `start` turned to `angle` degrees (clamped to -45..45) about the FRAME's own centre: the photo
// point under the frame's centre stays where it is, the frame keeps its size in pixels, and
// fit_frame then closes the blades in so no corner shows. The centre moves only if it would leave
// the photo or the straightened bounds the edges must keep to, and then straight towards the
// photo's centre. Turning back to `start`'s own angle gives `start` exactly, so a gesture that
// works out every step from its start never shrinks for good (#58).
[[nodiscard]] FrameEdit turn_frame(const Frame& start, float angle, int photo_width,
                                   int photo_height);

// The angle that makes the line from `a` to `b` (canvas points in `view`) level, if it is within 45
// degrees of level on screen, otherwise upright: the Option/Alt-drag along a horizon (#58). Clamped
// to -45..45. Nothing for a line too short to say (under 4 canvas units).
[[nodiscard]] std::optional<float> straighten_angle(const FramingView& view, CanvasPoint a,
                                                    CanvasPoint b) noexcept;

// `f` with aspect `aspect` chosen: Free only unlocks it; any other shape is apply_aspect about the
// frame's centre, then fit_frame. Choosing the aspect `f` has already changes nothing.
[[nodiscard]] FrameEdit choose_aspect(const Frame& f, Aspect aspect, int photo_width,
                                      int photo_height);

// `f` turned between landscape and portrait (X): Frame::portrait flips, the rectangle swaps its
// width and height in pixels about its centre (as far as the photo allows), then apply_aspect and
// fit_frame.
[[nodiscard]] FrameEdit turn_aspect(const Frame& f, int photo_width, int photo_height);

// --- The framing state ---------------------------------------------------------------------------

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

// --- Guides --------------------------------------------------------------------------------------

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

// --- Keys and the other tools --------------------------------------------------------------------

enum class FramingKey {
    c,           // C: framing on or off
    return_key,  // Return: done
    escape,      // Esc: during a gesture, cancel it (otherwise escape_action decides)
    x,           // X: turn the aspect
};

enum class FramingKeyAction {
    none,
    begin_framing,   // enter framing (FramingState::begin)
    leave_framing,   // leave, keeping the frame (FramingState::leave)
    cancel_gesture,  // FramingState::cancel
    turn_aspect,     // FramingState::flip_aspect
};

struct FramingKeyContext {
    bool has_file = false;
    bool framing = false;       // framing mode is on
    bool gesture = false;       // a framing gesture is under way
    OpenSheets sheets;          // a modal sheet keeps its own keys (#54)
    bool text_editing = false;  // a text field has the keyboard
    bool stroking = false;      // a brush stroke is under way
    bool popup_open = false;    // a menu or a list (the aspect menu) is open: its keys are its own
};

// What `key` does: nothing behind a modal sheet (Return keeps its meaning there), while a text
// field has the keyboard, during a brush stroke, or while a menu or list is open (Return picks in
// it, Esc closes it). C enters framing with a photo open and leaves
// it when on; Return leaves; Esc cancels a framing gesture under way (Esc otherwise goes to
// escape_action, where leaving framing comes first); X turns the aspect while framing.
[[nodiscard]] FramingKeyAction framing_key(FramingKey key, const FramingKeyContext& c) noexcept;

// The canvas tools when framing begins: all off (Brush, Sample, Pick and the test strip, #58). The
// caller also drops the comparison, which is not one of these.
[[nodiscard]] CanvasTools tools_when_framing(CanvasTools tools) noexcept;

// What entering framing does to the canvas's tools and the comparison (#58): every tool off
// (tools_when_framing) and no compare stage.
struct FramingEntry {
    CanvasTools tools;
    std::optional<StageId> compare;
};
[[nodiscard]] FramingEntry framing_entry(CanvasTools tools,
                                         std::optional<StageId> compare) noexcept;

// What may happen while framing, and whether it ends framing first (keeping the frame).
enum class FramingEvent {
    tool_chosen,     // I, W, B or T, a menu item or a row's button for a canvas tool
    compare_chosen,  // a compare stage was chosen on the Light path
    photo_opened,    // another photo (or the same again) was opened
    stage_viewed,    // another stage is viewed: framing works at any stage, Decode included
    undo_redo,       // the settings changed from History: the draft follows (FramingState::follow)
};
// True when `event` leaves framing: a tool or a comparison needs the framed picture on screen (the
// tools exclude each other the same way, canvas_input.hpp), and a new photo has no draft. Viewing
// another stage and undo or redo keep framing on. False while framing is off.
[[nodiscard]] bool framing_leaves(bool framing, FramingEvent event) noexcept;

// Whether a tool key (W, B, I or T) leaves framing, given the tools before it and after it
// (tools_after_key): only when the key really turns its own tool on. A key for a tool the viewed
// stage doesn't have (B before Dodge & burn, W where white balance isn't visible) changes nothing,
// and framing stays on (#58). False while framing is off. The app asks it for W and B; I and T
// call framing_leaves themselves, only once they know their tool turns on.
[[nodiscard]] bool tool_key_leaves_framing(bool framing, ToolKey key, CanvasTools before,
                                           CanvasTools after) noexcept;

// Whether the wheel and the pinch move the view while framing: only with no framing gesture under
// way. A drag maps the pointer through the view at its press, so the view holds still until it
// ends; what arrives meanwhile is dropped, not kept for later (#58).
[[nodiscard]] bool framing_view_takes_input(FramingGesture gesture) noexcept;

// Whether the render on hand (`render`, the texture's size) may be drawn on the canvas: while
// framing only the whole photo's (the size of the whole base, `whole`), and otherwise anything but
// a whole-photo render left over from framing while the picture is framed (`framed`, the preview
// base, differs from `whole`): drawn as the picture it would show the whole photo until the framed
// render arrives a moment later. The canvas stays empty, with "Rendering…", meanwhile.
[[nodiscard]] bool render_drawable(bool framing, PixelSize render, PixelSize whole,
                                   PixelSize framed) noexcept;

// --- Scopes --------------------------------------------------------------------------------------

enum class ScopeSource {
    framed_base,  // the picture as rendered: the framed part
    whole_base,   // while framing: the whole photo, as the canvas shows it
};

// What the scopes describe: the whole photo while framing (#58), the framed picture otherwise.
[[nodiscard]] ScopeSource scope_source(bool framing) noexcept;

// The words in the scopes' title for that: "whole photo, while framing", or nothing.
[[nodiscard]] std::string_view scope_source_detail(ScopeSource source) noexcept;

// The Scopes title's words in brackets: the viewed stage's name (none at the final picture), a test
// strip band's title, and while framing scope_source_detail, joined by ", ": "Exposure, whole photo,
// while framing". Empty when there is nothing to say.
[[nodiscard]] std::string scopes_title_detail(std::string_view stage_name,
                                              std::string_view band_title, ScopeSource source);

// The picture the canvas shows against the file, for the zoom chip's and Sample's lines: the
// longest side the picture stands for in the file, and whether that is the frame's (preview_base
// framed, outside framing) or the whole photo's (no frame, or while framing, when the canvas shows
// the whole photo).
struct CanvasMeasure {
    int file_long_side = 0;
    bool framed = false;
};
[[nodiscard]] CanvasMeasure canvas_measure(const Session& session, bool framing);

// --- Explanations --------------------------------------------------------------------------------

// The angle in words, to a tenth of a degree: "Upright" at exactly 0, "turned 2.5° clockwise",
// "turned 0.5° anticlockwise", and "turned under 0.1° clockwise" for a turn too small to show.
[[nodiscard]] std::string framing_angle_text(float angle);

// The angle without "turned", for the Easel line and the framing bar (#58: "4:5 · 1.2° clockwise",
// "Straighten … 1.2° clockwise"): "1.2° clockwise", "0.5° anticlockwise", "under 0.1° clockwise",
// and "upright" at exactly 0.
[[nodiscard]] std::string framing_turn_words(float angle);

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

// The Easel line's value (#58): "Whole photo" without a frame; otherwise the shape's name, then
// the turn in words when the frame is turned ("4:5 · 1.2° clockwise") or, upright, its size in the
// file ("Free · 5120 × 3413", "about …" while the size is an estimate). `framed` is the frame's
// size in the file (Session::framed_file_size).
[[nodiscard]] std::string easel_value(const std::optional<Frame>& frame, PixelSize framed,
                                      bool exact = true);

// The Straighten slider's typed angle: a number of degrees with an optional sign ("-", "+" or "−")
// and "°", or without a sign followed by "clockwise" or "anticlockwise" ("1.2° anticlockwise"), the
// words in any case; clamped to -45..45. Nothing for anything else (a sign with a word, an
// unknown word, no number).
[[nodiscard]] std::optional<float> typed_angle(std::string_view text);
// What the field holds when it opens: the angle to a hundredth with its direction, "1.25°
// clockwise", "0.5° anticlockwise", or "0°" upright. It reads back as the angle to a hundredth (an
// angle drawn along a horizon can have more digits): the field takes Enter on this text unchanged
// as no edit.
[[nodiscard]] std::string typed_angle_text(float angle);

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

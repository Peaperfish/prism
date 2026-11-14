#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "prism/gui/settings.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/gui/wheel_scaling.hpp"

namespace prism::gui {

// What the mouse, the trackpad and the keyboard do to the picture on the canvas, as pure functions.
// The window turns the answers into view_transform.hpp's calls.

// Where the app runs, as far as its pointer events differ.
enum class PointerPlatform {
    macos,
    windows,
    wayland,
    other,  // X11 and the rest
};

// How many screen units of pan one 1.0 of an SDL wheel delta is, before the display's scale. SDL's
// macOS and Wayland backends report a trackpad's travel at 0.1 per point, so there it follows the
// fingers. Elsewhere 1.0 is a wheel notch, with no real distance behind it: about three lines of text.
[[nodiscard]] float wheel_pan_unit(PointerPlatform platform) noexcept;

// What one wheel event does.
enum class CanvasScrollKind {
    none,
    pan,
    zoom,
};

struct CanvasScroll {
    CanvasScrollKind kind = CanvasScrollKind::none;
    float dx = 0.0f;      // pan: how far the picture moves, in screen units (> 0: right, down)
    float dy = 0.0f;
    float factor = 1.0f;  // zoom: what to multiply the zoom by
};

// `wheel` is SDL's own delta, before WheelScaler. A trackpad's scroll pans; a wheel's notch zooms, and
// so does either with the zoom key (Cmd on a Mac, Ctrl elsewhere). SDL's deltas already follow the
// system's natural-scrolling setting, so the direction SDL also reports must not be applied again.
[[nodiscard]] CanvasScroll canvas_scroll(WheelDelta wheel, bool trackpad, bool zoom_key,
                                         float pan_unit) noexcept;

// Whether a scroll belongs to the canvas: a gesture stays with what it started on, so momentum from
// flicking the panel doesn't start panning the picture when the cursor drifts onto it, nor the other
// way round. A gesture is over after trackpad_memory_ms without an event.
class ScrollLatch {
public:
    [[nodiscard]] bool on_canvas(bool hovering_canvas, std::uint64_t now_ms) noexcept;

private:
    bool started_ = false;
    bool on_canvas_ = false;
    std::uint64_t last_ms_ = 0;
};

// Whether a plain drag on the canvas moves the picture: only when there is somewhere to move it to,
// and never over a control (the compare divider), on the test strip, whose click sets the exposure,
// or in Brush mode, where a plain drag paints (Space and a drag still moves it there).
[[nodiscard]] bool plain_drag_pans(bool fit, bool strip_on, bool over_control,
                                   bool brushing = false) noexcept;

// Whether a double-click on the canvas toggles the zoom: not on the strip or a control, not with the
// hand, and not in Brush mode, where each click is a dot.
[[nodiscard]] bool double_click_zooms(bool strip_on, bool over_control, bool hand_held,
                                      bool brushing = false) noexcept;

// Whether a press on the canvas is the Brush's (a stroke, or with Space held the hand's: see
// BrushGesture): only in Brush mode with the Brush available, and not on a control (the compare
// divider) or on a chip on the canvas.
[[nodiscard]] bool brush_takes_press(bool brushing, bool available, bool over_control,
                                     bool on_chip) noexcept;

// Whether `p` lies in one of `rects` (edges included).
[[nodiscard]] bool view_point_in_any(ViewPoint p, const std::vector<ViewRect>& rects) noexcept;

// Whether the canvas may take the Space key, which otherwise presses the widget with the keyboard
// focus: only while the cursor is on it and nothing else is using the keyboard or the mouse.
[[nodiscard]] bool canvas_claims_space(bool canvas_hovered, bool text_field_active,
                                       bool control_active) noexcept;

// The modal sheets and prompts that can be up over the photo.
struct OpenSheets {
    bool recipe_sheet = false;  // Save recipe…
    bool export_sheet = false;  // Export…
    bool close_prompt = false;  // the unexported-edits question
    bool saving = false;        // the "Saving…" box, while an export runs or the close waits for one
    bool operator==(const OpenSheets&) const = default;
};

// Whether the photo's own keys (J, T, I, W, B, X, M, brackets, zoom, undo, Space for the hand,
// open, export, interface size…) do nothing: while any modal is up. Each sheet still reads its own
// keys (Esc, Return, typing); quitting stays with the app.
[[nodiscard]] bool shortcuts_blocked(const OpenSheets& open) noexcept;

// A drag of the picture, from press to release. It moves nothing until the cursor is further than the
// threshold from where it went down, and then catches up all at once, so the picture stays with the
// hand. A threshold of 0 starts at once.
class HandDrag {
public:
    void press(ViewPoint at, float threshold) noexcept;
    // How far to move the picture for this cursor position, if at all.
    [[nodiscard]] std::optional<ViewPoint> move(ViewPoint to) noexcept;
    void release() noexcept;

    [[nodiscard]] bool pressed() const noexcept { return pressed_; }
    // Past the threshold: the picture is moving with the hand.
    [[nodiscard]] bool dragging() const noexcept { return dragging_; }

private:
    bool pressed_ = false;
    bool dragging_ = false;
    float threshold_ = 0.0f;
    ViewPoint last_;
};

// --- Sample mode ----------------------------------------------------------------------------------
//
// While Sample mode is on, the cursor reads the pixel under it, and a click without a drag pins that
// pixel. A drag still moves the picture and a double-click still zooms (plain_drag_pans and
// double_click_zooms do not change).

// A pixel of the preview base.
struct SamplePin {
    int x = 0;
    int y = 0;

    bool operator==(const SamplePin&) const = default;
};

// Where a pixel's marker goes on the screen.
struct SampleMarker {
    bool on_canvas = false;  // its middle is on the canvas: otherwise there is nothing to draw
    ViewPoint centre;
    ViewRect square;         // the pixel itself
    bool shows_square = false;  // above 200% the pixel can be outlined; below it, a crosshair
};

[[nodiscard]] SampleMarker sample_marker(const ViewState& s, const ViewGeometry& g,
                                         SamplePin pixel) noexcept;

// Whether a press on the canvas can become a pin: only in Sample mode, not on a control, not with the
// hand held (that press is a drag from the start), and not on the test strip, whose click sets the
// exposure.
[[nodiscard]] bool sample_press_can_pin(bool sampling, bool strip_on, bool over_control,
                                        bool hand_held) noexcept;

// One press in Sample mode, from press to release: was it a click? Past HandDrag's threshold it is a
// drag. This keeps its own HandDrag, since at fit, where a drag moves nothing, the hand isn't pressed
// at all, and a click there must still pin.
class SampleClick {
public:
    void press(ViewPoint at, float threshold) noexcept;
    void move(ViewPoint to) noexcept;
    // The press of a double-click: it zooms, so its release must not pin as well.
    void cancel() noexcept;
    // True if the press ended as a click: pin the pixel under `at`.
    [[nodiscard]] bool release(ViewPoint at) noexcept;

private:
    HandDrag drag_;
};

// Whether a sample pin (or the last pixel read) stays when the photo's frame changes from `before`
// to `after`: only when the frame is the same (a whole-photo frame counts as none). Moving the
// frame or turning the photo at the same pixel size puts different photo points under the same
// pixel coordinates, so any frame change clears the pin (GUIDE.md).
[[nodiscard]] bool sample_pin_survives_frame(const std::optional<Frame>& before,
                                             const std::optional<Frame>& after) noexcept;

// Whether Sample mode is on, its one pin, and the last pixel the cursor read, which stays on show
// when the cursor leaves the picture, so that the rows can be pointed at. These are in image pixels,
// so they stay on the same spot of the photo through zooming and panning. The stage and the settings
// don't matter to them either: only another picture or a frame change does. Leaving the mode clears
// them.
class SampleMode {
public:
    [[nodiscard]] bool on() const noexcept { return on_; }
    [[nodiscard]] const std::optional<SamplePin>& pin() const noexcept { return pin_; }
    [[nodiscard]] const std::optional<SamplePin>& last_read() const noexcept { return last_read_; }

    void toggle() noexcept;  // I, or View > Sample Colour
    void leave() noexcept;   // Esc (EscapeAction::clear_sample)

    // Pins `pixel` of a width x height base (framed by `frame`), replacing any pin. Nothing if the
    // mode is off or the pixel is off the picture.
    void place(SamplePin pixel, int width, int height,
               const std::optional<Frame>& frame = std::nullopt) noexcept;

    // The cursor is over `pixel` of a width x height base (framed by `frame`). The same conditions
    // as place().
    void read(SamplePin pixel, int width, int height,
              const std::optional<Frame>& frame = std::nullopt) noexcept;

    // A file was opened: the pin and the last read go, even if the new picture has the same size.
    void opened() noexcept;

    // The photo's frame changed from `before` to `after` (on the easel, or by undo/redo): the pin
    // and the last read go unless sample_pin_survives_frame(before, after).
    void frame_changed(const std::optional<Frame>& before,
                       const std::optional<Frame>& after) noexcept;

    // The base is now width x height (framed by `frame`). The pin and the last read go if the size
    // or the frame isn't what they were on; they stay through a re-decode of the same size and
    // frame (a new camera matrix).
    void base_changed(int width, int height,
                      const std::optional<Frame>& frame = std::nullopt) noexcept;

private:
    bool on_ = false;
    std::optional<SamplePin> pin_;
    std::optional<SamplePin> last_read_;
    int width_ = 0;
    int height_ = 0;
    std::optional<Frame> frame_;
};

// --- Pick mode (the white balance picker, #21) -------------------------------------------------
//
// While Pick mode is on, a click on the photo (not a drag, which still moves the picture) sets the
// White balance from the grey under it. It is Sample mode's sibling: only one of the two, and the
// test strip, is on at a time.

// Whether a press on the canvas can become a pick: only in Pick mode, with white balance visible in
// what is shown (pick_available), not on a control, not with the hand held, and not on the test
// strip, whose click sets the exposure.
[[nodiscard]] bool pick_press_can_pick(bool picking, bool available, bool strip_on,
                                       bool over_control, bool hand_held) noexcept;

// Whether Pick mode is on. Leaving it is Esc (EscapeAction::leave_pick), W again, or I or T.
class PickMode {
public:
    [[nodiscard]] bool on() const noexcept { return on_; }
    void toggle() noexcept { on_ = !on_; }  // W, or View > Pick Neutral
    void leave() noexcept { on_ = false; }

private:
    bool on_ = false;
};

// --- Brush mode (Dodge & burn, #22) ------------------------------------------------------------
//
// Brush mode is the tool that will paint the strokes. It is Pick mode's sibling: one of Sample, Pick
// and Brush at a time, and the test strip is off with it. It is available only while Dodge & burn or
// a later stage is on view: before that the strokes' effect is not in the picture.

// The Brush's strength, in stops: the range of its slider, its step and where it starts. A tool
// setting, not an edit: it is not in EditSettings or the history, and is not saved.
inline constexpr float brush_strength_min = 0.1f;
inline constexpr float brush_strength_max = 2.0f;
inline constexpr float brush_strength_step = 0.1f;
inline constexpr float brush_strength_default = 0.5f;

// The Brush's size, as a radius in fractions of the picture's long side (FieldStroke::radius), and
// its edge (FieldStroke::softness: 0 hard, 1 the whole radius fading). The stroke field itself takes
// any radius above 0 and clamps the softness to 0 to 1; the size's range is the Brush's own: from
// half a percent of the long side (13 px on a 2560 px preview) to a quarter of it. ] and [ make it
// 1.2 times bigger or smaller, Shift+[ and Shift+] a tenth softer or harder. Tool settings, like the
// strength.
inline constexpr float brush_radius_min = 0.005f;
inline constexpr float brush_radius_max = 0.25f;
inline constexpr float brush_radius_default = 0.05f;
inline constexpr float brush_size_step = 1.2f;
inline constexpr float brush_softness_min = 0.0f;
inline constexpr float brush_softness_max = 1.0f;
inline constexpr float brush_softness_default = 0.5f;
inline constexpr float brush_softness_step = 0.1f;

// `radius` inside the range (the default for a number that isn't one).
[[nodiscard]] float clamp_brush_radius(float radius) noexcept;
// `softness` moved to the nearest tenth inside the range (the default for a number that isn't one).
[[nodiscard]] float snap_brush_softness(float softness) noexcept;

// Whether the Brush can be turned on with `viewed` on view: Dodge & burn or later.
[[nodiscard]] bool brush_available(StageId viewed) noexcept;

// Why the Brush can't be turned on, for the greyed-out button and menu item.
inline constexpr const char* brush_unavailable_hint =
    "Not visible here: view Dodge & burn or a later stage to use the Brush.";

// `strength` moved to the nearest step inside the range (the default for a number that isn't one).
[[nodiscard]] float snap_brush_strength(float strength) noexcept;

// "0.5 stop", "1.0 stop", "1.5 stops": singular up to one stop. Of the snapped strength.
[[nodiscard]] std::string brush_strength_text(float strength);

// What a stroke does to the light: dodge holds light back (lighter), burn adds it (darker).
enum class BrushTone {
    dodge,
    burn,
};

// The other one: Alt at a press, and X.
[[nodiscard]] constexpr BrushTone other_tone(BrushTone tone) noexcept {
    return tone == BrushTone::dodge ? BrushTone::burn : BrushTone::dodge;
}

// Whether Brush mode is on, the tone its strokes have without Alt, and the strength, size and edge
// it paints with. Leaving it is Esc
// (EscapeAction::leave_brush), the Brush button or key again, I, T or W, the view moving to a stage
// before Dodge & burn, or another photo.
class BrushMode {
public:
    [[nodiscard]] bool on() const noexcept { return on_; }
    void toggle() noexcept { on_ = !on_; }  // the row's Brush button
    void leave() noexcept { on_ = false; }
    void opened() noexcept { on_ = false; }  // a photo was opened: the strength stays

    [[nodiscard]] float strength() const noexcept { return strength_; }
    void set_strength(float strength) noexcept { strength_ = snap_brush_strength(strength); }

    [[nodiscard]] BrushTone tone() const noexcept { return tone_; }  // dodge to start with
    void set_tone(BrushTone tone) noexcept { tone_ = tone; }
    void swap_tone() noexcept { tone_ = other_tone(tone_); }  // X

    [[nodiscard]] float radius() const noexcept { return radius_; }
    void set_radius(float radius) noexcept { radius_ = clamp_brush_radius(radius); }
    [[nodiscard]] float softness() const noexcept { return softness_; }
    void set_softness(float softness) noexcept { softness_ = snap_brush_softness(softness); }

private:
    bool on_ = false;
    float strength_ = brush_strength_default;
    BrushTone tone_ = BrushTone::dodge;
    float radius_ = brush_radius_default;
    float softness_ = brush_softness_default;
};

// What can end Brush mode besides the tool keys (tools_after_key).
enum class BrushEvent {
    view_changed,  // the viewed stage is now `viewed`
    photo_opened,  // another photo was opened
    escape,        // Esc with no stroke under way (BrushGesture::escape says leave_brush)
};

// Whether `event` ends Brush mode, which is `on`: moving the view to a stage before Dodge & burn
// (the strokes' effect isn't in the picture there), opening another photo, and Esc. Never when it
// is off already.
[[nodiscard]] bool brush_leaves(bool on, BrushEvent event, StageId viewed) noexcept;

// View > Brush: checked while Brush mode is on; it can always be turned off, and turned on only
// with a photo open and the Brush available at the viewed stage (as tools_after_key's brush_ok).
struct BrushMenuItem {
    bool checked = false;
    bool enabled = false;
    bool shows_hint = false;  // greyed out for the stage: the tooltip says why (brush_unavailable_hint)

    bool operator==(const BrushMenuItem&) const = default;
};
[[nodiscard]] BrushMenuItem brush_menu_item(bool on, bool has_file, StageId viewed) noexcept;

// Which of the canvas's tools are on.
struct CanvasTools {
    bool sampling = false;
    bool picking = false;
    bool strip = false;
    bool brushing = false;

    bool operator==(const CanvasTools&) const = default;
};

enum class ToolKey {
    pick,    // W
    sample,  // I
    strip,   // T
    brush,   // the Brush button
};

// What the tools are after a key: a key toggles its tool, and turning one on turns the others off
// (Sample and the strip can be on together; Pick and Brush are on alone). A tool that is not
// available (`pick_ok`: white balance is not visible at the viewed stage; `strip_ok`: see
// strip_available; `brush_ok`: see brush_available) can be turned off but not on, and then nothing
// else changes. I, T and W leave Brush mode.
[[nodiscard]] CanvasTools tools_after_key(CanvasTools tools, ToolKey key, bool pick_ok,
                                          bool strip_ok, bool brush_ok = false) noexcept;

// Where Pick mode's ring goes on the screen: around the piece of the photo a click averages
// (file_pick_marker in detail_canvas.hpp).
struct PickMarker {
    bool on_canvas = false;  // the pixel's middle is on the canvas: otherwise there is nothing to draw
    ViewRect patch;
};

// What a pinch's scale means. SDL's header says each event carries the change since the one before,
// which is what its macOS backend sends; its Wayland backend passes on the protocol's scale "relative
// to the initial finger position", and its X11 backend XInput's, taken to be the same (not checked).
enum class PinchScale {
    per_event,
    running_total,
};
[[nodiscard]] PinchScale pinch_scale_of(PointerPlatform platform) noexcept;

// Turns a pinch's events into the factor to multiply the zoom by, whichever kind the platform sends.
class PinchTracker {
public:
    explicit PinchTracker(PinchScale kind) noexcept : kind_(kind) {}

    // SDL_EVENT_PINCH_BEGIN. Its scale, like END's, is 0 and means nothing.
    void begin() noexcept { last_total_ = 1.0f; }

    // SDL_EVENT_PINCH_UPDATE: the factor for this event. 1 for a scale that can't be one.
    [[nodiscard]] float update(float scale) noexcept;

private:
    PinchScale kind_;
    float last_total_ = 1.0f;
};

}  // namespace prism::gui

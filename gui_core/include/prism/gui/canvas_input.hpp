#pragma once

#include <cstdint>
#include <optional>

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
// and never over a control (the compare divider) or on the test strip, whose click sets the exposure.
[[nodiscard]] bool plain_drag_pans(bool fit, bool strip_on, bool over_control) noexcept;

// Whether a double-click on the canvas toggles the zoom: not on the strip or a control.
[[nodiscard]] bool double_click_zooms(bool strip_on, bool over_control, bool hand_held) noexcept;

// Whether the canvas may take the Space key, which otherwise presses the widget with the keyboard
// focus: only while the cursor is on it and nothing else is using the keyboard or the mouse.
[[nodiscard]] bool canvas_claims_space(bool canvas_hovered, bool text_field_active,
                                       bool control_active) noexcept;

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

// The pixel under a point in image coordinates (view_screen_to_image), if it is on the picture.
[[nodiscard]] std::optional<SamplePin> sample_pixel_at(ViewPoint image, int width,
                                                       int height) noexcept;

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

// Whether Sample mode is on, and its one pin. Pins are in image pixels, so they stay on the same
// spot of the photo through zooming and panning. The stage and the settings don't matter to them
// either: only another picture does. Leaving the mode clears the pin.
class SampleMode {
public:
    [[nodiscard]] bool on() const noexcept { return on_; }
    [[nodiscard]] const std::optional<SamplePin>& pin() const noexcept { return pin_; }

    void toggle() noexcept;  // I, or View > Sample Colour
    void leave() noexcept;   // Esc (EscapeAction::clear_sample)

    // Pins `pixel` of a width x height base, replacing any pin. Nothing if the mode is off or the
    // pixel is off the picture.
    void place(SamplePin pixel, int width, int height) noexcept;

    // A file was opened: the pin goes, even if the new picture has the same size.
    void opened() noexcept;

    // The base is now width x height. The pin goes if that isn't what it was placed on; it stays
    // through a re-decode of the same size (a new camera matrix).
    void base_changed(int width, int height) noexcept;

private:
    bool on_ = false;
    std::optional<SamplePin> pin_;
    int width_ = 0;
    int height_ = 0;
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

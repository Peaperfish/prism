#pragma once

#include <cstdint>

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

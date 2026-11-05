#include "prism/gui/canvas_input.hpp"

#include <cmath>

namespace prism::gui {

namespace {

// One wheel notch of zoom, and as much for a trackpad's ten points of travel.
constexpr float zoom_per_wheel_unit = 1.25f;

}  // namespace

float wheel_pan_unit(PointerPlatform platform) noexcept {
    switch (platform) {
        case PointerPlatform::macos:
        case PointerPlatform::wayland:
            return 10.0f;
        case PointerPlatform::windows:
        case PointerPlatform::other:
            break;
    }
    return 48.0f;
}

CanvasScroll canvas_scroll(WheelDelta wheel, bool trackpad, bool zoom_key,
                           float pan_unit) noexcept {
    CanvasScroll out;
    if (zoom_key || !trackpad) {
        if (wheel.y != 0.0f) {
            out.kind = CanvasScrollKind::zoom;
            out.factor = std::pow(zoom_per_wheel_unit, wheel.y);
        }
        return out;
    }
    if (wheel.x != 0.0f || wheel.y != 0.0f) {
        out.kind = CanvasScrollKind::pan;
        // SDL's x is positive to the right, as a wheel scrolls: the picture goes the other way. Its y
        // is positive away from the user, which brings the picture down.
        out.dx = -wheel.x * pan_unit;
        out.dy = wheel.y * pan_unit;
    }
    return out;
}

bool ScrollLatch::on_canvas(bool hovering_canvas, std::uint64_t now_ms) noexcept {
    const bool same_gesture =
        started_ && now_ms >= last_ms_ && now_ms - last_ms_ <= trackpad_memory_ms;
    if (!same_gesture) {
        on_canvas_ = hovering_canvas;
    }
    started_ = true;
    last_ms_ = now_ms;
    return on_canvas_;
}

}  // namespace prism::gui

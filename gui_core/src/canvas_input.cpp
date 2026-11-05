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

bool plain_drag_pans(bool fit, bool strip_on, bool over_control) noexcept {
    return !fit && !strip_on && !over_control;
}

bool double_click_zooms(bool strip_on, bool over_control, bool hand_held) noexcept {
    return !strip_on && !over_control && !hand_held;
}

bool canvas_claims_space(bool canvas_hovered, bool text_field_active,
                         bool control_active) noexcept {
    return canvas_hovered && !text_field_active && !control_active;
}

void HandDrag::press(ViewPoint at, float threshold) noexcept {
    pressed_ = true;
    dragging_ = threshold <= 0.0f;
    threshold_ = threshold;
    last_ = at;
}

std::optional<ViewPoint> HandDrag::move(ViewPoint to) noexcept {
    if (!pressed_) {
        return std::nullopt;
    }
    const float dx = to.x - last_.x;
    const float dy = to.y - last_.y;
    if (!dragging_ && dx * dx + dy * dy < threshold_ * threshold_) {
        return std::nullopt;
    }
    dragging_ = true;
    last_ = to;
    if (dx == 0.0f && dy == 0.0f) {
        return std::nullopt;
    }
    return ViewPoint{dx, dy};
}

void HandDrag::release() noexcept {
    pressed_ = false;
    dragging_ = false;
}

PinchScale pinch_scale_of(PointerPlatform platform) noexcept {
    switch (platform) {
        case PointerPlatform::wayland:
        case PointerPlatform::other:
            return PinchScale::running_total;
        case PointerPlatform::macos:
        case PointerPlatform::windows:
            break;
    }
    return PinchScale::per_event;
}

float PinchTracker::update(float scale) noexcept {
    if (!(scale > 0.0f) || !std::isfinite(scale)) {
        return 1.0f;
    }
    if (kind_ == PinchScale::per_event) {
        return scale;
    }
    const float factor = scale / last_total_;
    last_total_ = scale;
    return factor;
}

}  // namespace prism::gui

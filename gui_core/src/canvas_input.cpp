#include "prism/gui/canvas_input.hpp"

#include <cmath>

#include "prism/gui/neutral_pick.hpp"

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

SampleMarker sample_marker(const ViewState& s, const ViewGeometry& g, SamplePin pixel) noexcept {
    const auto x = static_cast<float>(pixel.x);
    const auto y = static_cast<float>(pixel.y);
    SampleMarker marker;
    marker.square = {view_image_to_screen(s, g, {x, y}),
                     view_image_to_screen(s, g, {x + 1.0f, y + 1.0f})};
    marker.centre = view_image_to_screen(s, g, {x + 0.5f, y + 0.5f});
    const std::optional<ViewPixel> under = view_pixel_at(s, g, marker.centre);
    marker.on_canvas = under.has_value();
    marker.shows_square = view_shows_pixels(view_zoom(s, g));
    return marker;
}

bool sample_press_can_pin(bool sampling, bool strip_on, bool over_control,
                          bool hand_held) noexcept {
    return sampling && !strip_on && !over_control && !hand_held;
}

void SampleClick::press(ViewPoint at, float threshold) noexcept {
    drag_.press(at, threshold);
}

void SampleClick::move(ViewPoint to) noexcept {
    (void)drag_.move(to);
}

void SampleClick::cancel() noexcept {
    drag_.release();
}

bool SampleClick::release(ViewPoint at) noexcept {
    move(at);  // a press and release can arrive in one frame, with no move between them
    const bool click = drag_.pressed() && !drag_.dragging();
    drag_.release();
    return click;
}

void SampleMode::toggle() noexcept {
    if (on_) {
        leave();
    } else {
        on_ = true;
    }
}

void SampleMode::leave() noexcept {
    on_ = false;
    pin_.reset();
    last_read_.reset();
}

void SampleMode::place(SamplePin pixel, int width, int height) noexcept {
    if (!on_ || pixel.x < 0 || pixel.y < 0 || pixel.x >= width || pixel.y >= height) {
        return;
    }
    pin_ = pixel;
    width_ = width;
    height_ = height;
}

void SampleMode::read(SamplePin pixel, int width, int height) noexcept {
    if (!on_ || pixel.x < 0 || pixel.y < 0 || pixel.x >= width || pixel.y >= height) {
        return;
    }
    last_read_ = pixel;
    width_ = width;
    height_ = height;
}

void SampleMode::opened() noexcept {
    pin_.reset();
    last_read_.reset();
}

void SampleMode::base_changed(int width, int height) noexcept {
    if (width != width_ || height != height_) {
        pin_.reset();
        last_read_.reset();
    }
}

bool pick_press_can_pick(bool picking, bool available, bool strip_on, bool over_control,
                         bool hand_held) noexcept {
    return picking && available && !strip_on && !over_control && !hand_held;
}

CanvasTools tools_after_key(CanvasTools tools, ToolKey key, bool pick_ok, bool strip_ok) noexcept {
    switch (key) {
        case ToolKey::pick:
            if (tools.picking) {
                tools.picking = false;
            } else if (pick_ok) {
                tools = {false, true, false};
            }
            break;
        case ToolKey::sample:
            tools.sampling = !tools.sampling;
            tools.picking = tools.picking && !tools.sampling;
            break;
        case ToolKey::strip:
            if (tools.strip) {
                tools.strip = false;
            } else if (strip_ok) {
                tools.strip = true;
                tools.picking = false;
            }
            break;
    }
    return tools;
}

PickMarker pick_marker(const ViewState& s, const ViewGeometry& g, SamplePin pixel, int width,
                       int height) noexcept {
    PickMarker marker;
    const std::optional<PatchBounds> bounds = pick_patch_bounds(width, height, pixel.x, pixel.y);
    if (!bounds) {
        return marker;
    }
    marker.patch = {view_image_to_screen(s, g, {static_cast<float>(bounds->x0),
                                                static_cast<float>(bounds->y0)}),
                    view_image_to_screen(s, g, {static_cast<float>(bounds->x1 + 1),
                                                static_cast<float>(bounds->y1 + 1)})};
    const ViewPoint centre = view_image_to_screen(
        s, g, {static_cast<float>(pixel.x) + 0.5f, static_cast<float>(pixel.y) + 0.5f});
    marker.on_canvas = view_pixel_at(s, g, centre).has_value();
    return marker;
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

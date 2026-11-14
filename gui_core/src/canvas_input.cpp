#include "prism/gui/canvas_input.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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

bool plain_drag_pans(bool fit, bool strip_on, bool over_control, bool brushing) noexcept {
    return !fit && !strip_on && !over_control && !brushing;
}

bool double_click_zooms(bool strip_on, bool over_control, bool hand_held, bool brushing) noexcept {
    return !strip_on && !over_control && !hand_held && !brushing;
}

bool brush_takes_press(bool brushing, bool available, bool over_control, bool on_chip) noexcept {
    return brushing && available && !over_control && !on_chip;
}

bool view_point_in_any(ViewPoint p, const std::vector<ViewRect>& rects) noexcept {
    for (const ViewRect& r : rects) {
        if (p.x >= r.min.x && p.x <= r.max.x && p.y >= r.min.y && p.y <= r.max.y) {
            return true;
        }
    }
    return false;
}

bool canvas_claims_space(bool canvas_hovered, bool text_field_active,
                         bool control_active) noexcept {
    return canvas_hovered && !text_field_active && !control_active;
}

bool shortcuts_blocked(const OpenSheets& open) noexcept {
    return open.recipe_sheet || open.export_sheet || open.close_prompt || open.saving;
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

bool sample_pin_survives_frame(const std::optional<Frame>& before,
                               const std::optional<Frame>& after) noexcept {
    return normalised_frame(before) == normalised_frame(after);
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
    frame_.reset();
}

void SampleMode::place(SamplePin pixel, int width, int height,
                       const std::optional<Frame>& frame) noexcept {
    if (!on_ || pixel.x < 0 || pixel.y < 0 || pixel.x >= width || pixel.y >= height) {
        return;
    }
    pin_ = pixel;
    width_ = width;
    height_ = height;
    frame_ = normalised_frame(frame);
}

void SampleMode::read(SamplePin pixel, int width, int height,
                      const std::optional<Frame>& frame) noexcept {
    if (!on_ || pixel.x < 0 || pixel.y < 0 || pixel.x >= width || pixel.y >= height) {
        return;
    }
    last_read_ = pixel;
    width_ = width;
    height_ = height;
    frame_ = normalised_frame(frame);
}

void SampleMode::opened() noexcept {
    pin_.reset();
    last_read_.reset();
    frame_.reset();
}

void SampleMode::frame_changed(const std::optional<Frame>& before,
                               const std::optional<Frame>& after) noexcept {
    if (!sample_pin_survives_frame(before, after)) {
        pin_.reset();
        last_read_.reset();
        frame_ = normalised_frame(after);
    }
}

void SampleMode::base_changed(int width, int height, const std::optional<Frame>& frame) noexcept {
    if (width != width_ || height != height_ || !sample_pin_survives_frame(frame_, frame)) {
        pin_.reset();
        last_read_.reset();
    }
}

bool pick_press_can_pick(bool picking, bool available, bool strip_on, bool over_control,
                         bool hand_held) noexcept {
    return picking && available && !strip_on && !over_control && !hand_held;
}

float clamp_brush_radius(float radius) noexcept {
    if (std::isnan(radius)) {
        return brush_radius_default;
    }
    return std::clamp(radius, brush_radius_min, brush_radius_max);
}

float snap_brush_softness(float softness) noexcept {
    if (std::isnan(softness)) {
        return brush_softness_default;
    }
    const float tenths = std::round(std::clamp(softness, brush_softness_min, brush_softness_max) /
                                    brush_softness_step);
    // Over 10, not times 0.1: 7 * 0.1f is a little over 0.7f, and 7 / 10.0f is 0.7f itself.
    return std::clamp(tenths / 10.0f, brush_softness_min, brush_softness_max);
}

bool brush_available(StageId viewed) noexcept {
    return static_cast<int>(viewed) >= static_cast<int>(StageId::dodge_burn);
}

bool brush_leaves(bool on, BrushEvent event, StageId viewed) noexcept {
    if (!on) {
        return false;
    }
    switch (event) {
        case BrushEvent::view_changed:
            return !brush_available(viewed);
        case BrushEvent::photo_opened:
        case BrushEvent::escape:
            return true;
    }
    return false;
}

BrushMenuItem brush_menu_item(bool on, bool has_file, StageId viewed) noexcept {
    const bool available = brush_available(viewed);
    return {on, on || (has_file && available), has_file && !on && !available};
}

float snap_brush_strength(float strength) noexcept {
    if (std::isnan(strength)) {
        return brush_strength_default;
    }
    const float steps = std::round(std::clamp(strength, brush_strength_min, brush_strength_max) /
                                   brush_strength_step);
    return std::clamp(steps * brush_strength_step, brush_strength_min, brush_strength_max);
}

std::string brush_strength_text(float strength) {
    const float snapped = snap_brush_strength(strength);
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", static_cast<double>(snapped));
    // Judged on the tenths, so that 1.0 is a stop and 1.1 is stops.
    const bool one_or_less = std::lround(snapped * 10.0f) <= 10;
    return std::string(buffer) + (one_or_less ? " stop" : " stops");
}

CanvasTools tools_after_key(CanvasTools tools, ToolKey key, bool pick_ok, bool strip_ok,
                            bool brush_ok) noexcept {
    switch (key) {
        case ToolKey::pick:
            if (tools.picking) {
                tools.picking = false;
            } else if (pick_ok) {
                tools = {false, true, false, false};
            }
            break;
        case ToolKey::sample:
            tools.sampling = !tools.sampling;
            tools.picking = tools.picking && !tools.sampling;
            tools.brushing = false;
            break;
        case ToolKey::strip:
            if (tools.strip) {
                tools.strip = false;
            } else if (strip_ok) {
                tools.strip = true;
                tools.picking = false;
                tools.brushing = false;
            }
            break;
        case ToolKey::brush:
            if (tools.brushing) {
                tools.brushing = false;
            } else if (brush_ok) {
                tools = {false, false, false, true};
            }
            break;
    }
    return tools;
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

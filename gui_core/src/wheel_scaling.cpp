#include "prism/gui/wheel_scaling.hpp"

#include <cmath>

namespace prism::gui {

namespace {

// SDL's macOS backend reports a trackpad's travel in points times this.
constexpr float sdl_trackpad_unit = 0.1f;
// Dear ImGui scrolls this many font heights per 1.0 of wheel (imgui.cpp, UpdateMouseWheel).
constexpr float imgui_steps_x = 2.0f;
constexpr float imgui_steps_y = 5.0f;

bool has_fraction(float v) {
    return v != std::floor(v);
}

}  // namespace

WheelDelta WheelScaler::scale(WheelDelta wheel, float font_size, std::uint64_t now_ms) {
    if (has_fraction(wheel.x) || has_fraction(wheel.y)) {
        seen_fractional_ = true;
        last_fractional_ms_ = now_ms;
    }
    const bool trackpad = seen_fractional_ && now_ms >= last_fractional_ms_ &&
                          now_ms - last_fractional_ms_ <= trackpad_memory_ms;
    last_was_trackpad_ = trackpad;
    if (!trackpad || font_size <= 0.0f) {
        return wheel;
    }
    const float per_point = trackpad_scroll_speed / sdl_trackpad_unit;
    return {wheel.x * per_point / (imgui_steps_x * font_size),
            wheel.y * per_point / (imgui_steps_y * font_size)};
}

}  // namespace prism::gui

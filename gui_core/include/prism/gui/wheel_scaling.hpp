#pragma once

#include <cstdint>

namespace prism::gui {

// How fast a Mac trackpad scrolls: points of scroll per point of finger travel. 1 makes the content
// follow the fingers; raise it to scroll faster, lower it to scroll slower. It does not touch a
// mouse wheel.
inline constexpr float trackpad_scroll_speed = 1.0f;

// A trackpad that sends whole numbers mid-gesture is still a trackpad for this long after its last
// fractional event.
inline constexpr std::uint64_t trackpad_memory_ms = 100;

struct WheelDelta {
    float x = 0.0f;
    float y = 0.0f;
};

// Makes scroll events from a trackpad feel like a normal Mac app, and leaves a mouse wheel alone.
//
// SDL turns a trackpad's travel into 0.1 per point, and Dear ImGui scrolls 2 (sideways) or 5
// (vertically) font heights per 1.0, meant for mouse-wheel notches. Together a short swipe flies
// across the panel. For a trackpad this scales the delta so that trackpad_scroll_speed points of
// scroll follow each point of travel. The same goes for the momentum events after the fingers lift.
//
// Trackpad or wheel: SDL doesn't say, so an event is a trackpad's if it has a fractional part, or
// comes within trackpad_memory_ms of one that had (a trackpad sends whole numbers now and then).
// A whole-number event with no recent fractional one is a wheel notch and passes unchanged.
class WheelScaler {
public:
    // `font_size` is Dear ImGui's current font size in points, which sets how far it scrolls per
    // 1.0; `now_ms` is the event's time. Zero or negative font_size passes the delta unchanged.
    [[nodiscard]] WheelDelta scale(WheelDelta wheel, float font_size, std::uint64_t now_ms);

    // Whether the last event given to scale() was a trackpad's.
    [[nodiscard]] bool last_was_trackpad() const noexcept { return last_was_trackpad_; }

private:
    bool last_was_trackpad_ = false;
    bool seen_fractional_ = false;
    std::uint64_t last_fractional_ms_ = 0;
};

}  // namespace prism::gui

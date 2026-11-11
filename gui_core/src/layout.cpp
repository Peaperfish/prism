#include "prism/gui/layout.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "prism/gui/preferences.hpp"

namespace prism::gui {

float panel_width_pixels(float preferred_points, float scale, float available,
                         float text_minimum) {
    if (available <= 0 || scale <= 0) {
        return 0;
    }
    const float minimum = std::max(static_cast<float>(panel_width_min) * scale, text_minimum);
    const float maximum = std::max(0.4f * available, std::min(minimum, 0.6f * available));
    return std::clamp(preferred_points * scale, std::min(minimum, maximum), maximum);
}

int widen_panel_width(int points, float scale, float text_minimum) {
    if (scale <= 0 || !(text_minimum > 0)) {
        return clamp_panel_width(points);
    }
    const int needed = static_cast<int>(std::ceil(text_minimum / scale));
    return clamp_panel_width(std::max(points, needed));
}

float window_minimum_width(float text_minimum, float splitter, float scale, float base) {
    // The panel may take 60% of the window when its minimum is more than 40%: the window is wide
    // enough that its text minimum is no more than that.
    return std::max({base, text_minimum + splitter + static_cast<float>(canvas_width_min) * scale,
                     text_minimum / 0.6f});
}

int panel_points_from_pixels(float pixels, float scale) {
    if (scale <= 0) {
        return panel_width_default;
    }
    return clamp_panel_width(static_cast<int>(std::lround(pixels / scale)));
}

SplitterGrab splitter_grab(float edge_x, float splitter, float scale) {
    const float width = std::max(splitter, splitter_grab_min * scale);
    return {edge_x - width / 2, width};
}

bool splitter_grab_hit(const SplitterGrab& grab, float mouse_x) {
    return mouse_x >= grab.x && mouse_x < grab.x + grab.width;
}

float dragged_panel_pixels(float panel_pixels, float mouse_dx) {
    return panel_pixels - mouse_dx;
}

std::string ellipsize_middle(std::string_view text, float max_width,
                             const std::function<float(std::string_view)>& measure) {
    if (measure(text) <= max_width) {
        return std::string(text);
    }
    constexpr std::string_view dots = "…";
    if (measure(dots) > max_width) {
        return "";
    }
    // Where each character starts: the only places it may be cut.
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {  // not a continuation byte
            starts.push_back(i);
        }
    }
    const std::size_t chars = starts.size();
    const auto byte_at = [&](std::size_t char_index) {
        return char_index < chars ? starts[char_index] : text.size();
    };
    // Keep `keep` characters, about two thirds at the end; find the most that fits (the width
    // grows with `keep`, so a binary search works).
    const auto candidate = [&](std::size_t keep) {
        const std::size_t tail = (keep * 2 + 2) / 3;
        const std::size_t head = keep - tail;
        return std::string(text.substr(0, byte_at(head))) + std::string(dots) +
               std::string(text.substr(byte_at(chars - tail)));
    };
    std::size_t lo = 0;          // fits
    std::size_t hi = chars - 1;  // keeping every character but one: may not fit
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (measure(candidate(mid)) <= max_width) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return candidate(lo);
}

float clamp_split_fraction(float fraction) noexcept {
    if (std::isnan(fraction)) {
        return split_fraction_default;
    }
    return std::clamp(fraction, split_fraction_min, split_fraction_max);
}

float split_fraction_from_x(float mouse_x, float x, float width) noexcept {
    if (!(width > 0)) {
        return split_fraction_default;
    }
    return clamp_split_fraction((mouse_x - x) / width);
}

SplitLayout split_layout(float x, float width, float fraction) noexcept {
    SplitLayout out;
    out.left_x0 = x;
    if (!(width > 0)) {
        out.divider_x = x;
        out.right_x1 = x;
        out.u_divider = 0;
        return out;
    }
    out.right_x1 = x + width;
    out.divider_x = std::clamp(std::round(x + width * clamp_split_fraction(fraction)), x, x + width);
    out.u_divider = (out.divider_x - x) / width;
    return out;
}

}  // namespace prism::gui

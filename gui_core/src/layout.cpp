#include "prism/gui/layout.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "prism/gui/preferences.hpp"

namespace prism::gui {

float panel_width_pixels(float preferred_points, float scale, float available) {
    if (available <= 0 || scale <= 0) {
        return 0;
    }
    const float minimum = static_cast<float>(panel_width_min) * scale;
    const float maximum = std::max(0.4f * available, std::min(minimum, 0.6f * available));
    return std::clamp(preferred_points * scale, std::min(minimum, maximum), maximum);
}

int panel_points_from_pixels(float pixels, float scale) {
    if (scale <= 0) {
        return panel_width_default;
    }
    return clamp_panel_width(static_cast<int>(std::lround(pixels / scale)));
}

std::string ellipsize_middle(std::string_view text, float max_width,
                             const std::function<float(std::string_view)>& measure) {
    if (measure(text) <= max_width) {
        return std::string(text);
    }
    constexpr std::string_view dots = "...";
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

}  // namespace prism::gui

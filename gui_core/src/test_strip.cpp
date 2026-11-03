#include "prism/gui/test_strip.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace prism::gui {

int strip_step_divisor(StripStep step) noexcept {
    switch (step) {
        case StripStep::third:
            return 3;
        case StripStep::half:
            return 2;
        case StripStep::whole:
            return 1;
    }
    return 2;
}

float strip_step_stops(StripStep step) noexcept {
    return 1.0f / static_cast<float>(strip_step_divisor(step));
}

const char* strip_step_name(StripStep step) noexcept {
    switch (step) {
        case StripStep::third:
            return "1/3 stop";
        case StripStep::half:
            return "1/2 stop";
        case StripStep::whole:
            return "1 stop";
    }
    return "1/2 stop";
}

bool strip_vertical(int width, int height) noexcept {
    return width >= height;
}

std::vector<StripRect> strip_bands(int width, int height, int count) {
    std::vector<StripRect> bands;
    if (width <= 0 || height <= 0 || count < 1) {
        return bands;
    }
    const bool vertical = strip_vertical(width, height);
    const long long length = vertical ? width : height;
    bands.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int from = static_cast<int>(length * i / count);
        const int to = static_cast<int>(length * (i + 1) / count);
        bands.push_back(vertical ? StripRect{from, 0, to - from, height}
                                 : StripRect{0, from, width, to - from});
    }
    return bands;
}

int strip_middle(int count) noexcept {
    return count / 2;
}

float strip_offset(StripStep step, int count, int band) noexcept {
    return static_cast<float>(band - strip_middle(count)) /
           static_cast<float>(strip_step_divisor(step));
}

float strip_light_factor(float offset_stops) noexcept {
    return std::exp2(offset_stops);
}

namespace {

// The single glyph for n/d (n < d), or "" if there is none.
const char* fraction_glyph(int n, int d) {
    if (d == 2 && n == 1) {
        return "\u00BD";
    }
    if (d == 3 && n == 1) {
        return "\u2153";
    }
    if (d == 3 && n == 2) {
        return "\u2154";
    }
    return "";
}

// |k|/d as "1", "½", "1½", "⅔".
std::string magnitude_text(int k, int d) {
    const int whole = std::abs(k) / d;
    const int rest = std::abs(k) % d;
    std::string text;
    if (whole > 0 || rest == 0) {
        text = std::to_string(whole);
    }
    text += fraction_glyph(rest, d);
    return text;
}

std::string factor_text(float factor) {
    if (factor == 0.5f) {
        return "\u00BD\u00D7";
    }
    if (factor == 0.25f) {
        return "\u00BC\u00D7";
    }
    const float nearest = std::round(factor);
    if (nearest >= 1.0f && std::fabs(factor - nearest) < 0.005f) {
        return std::to_string(static_cast<int>(nearest)) + "\u00D7";
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f\u00D7", static_cast<double>(factor));
    return buf;
}

}  // namespace

std::string strip_label(StripStep step, int count, int band) {
    if (band < 0 || band >= count) {
        return {};
    }
    const int k = band - strip_middle(count);
    if (k == 0) {
        return "0 \u00B7 now";
    }
    const int d = strip_step_divisor(step);
    std::string text = k < 0 ? "\u2212" : "+";
    text += magnitude_text(k, d);
    const bool first = band == 0;
    if (first) {
        text += std::abs(k) <= d ? " stop" : " stops";  // up to one whole stop is "stop"
    }
    text += " \u00B7 " + factor_text(strip_light_factor(strip_offset(step, count, band)));
    if (first) {
        text += " light";
    }
    return text;
}

std::optional<int> strip_band_at(int width, int height, int count, float x, float y) noexcept {
    if (width <= 0 || height <= 0 || count < 1 || !(x >= 0.0f) || !(y >= 0.0f) ||
        x >= static_cast<float>(width) || y >= static_cast<float>(height)) {
        return std::nullopt;
    }
    const auto px = static_cast<int>(x);
    const auto py = static_cast<int>(y);
    const std::vector<StripRect> bands = strip_bands(width, height, count);
    for (std::size_t i = 0; i < bands.size(); ++i) {
        const StripRect& b = bands[i];
        if (px >= b.x && px < b.x + b.width && py >= b.y && py < b.y + b.height) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

float strip_centre_stops(const EditSettings& s) noexcept {
    return s.exposure_on ? s.exposure_stops : 0.0f;
}

EditSettings strip_band_settings(const EditSettings& s, StripStep step, int count, int band) {
    EditSettings out = s;
    out.exposure_on = true;
    out.exposure_stops = strip_centre_stops(s) + strip_offset(step, count, band);
    return out;
}

bool apply_strip_band(EditSettings& s, StripStep step, int count, int band) {
    if (band < 0 || band >= count || band == strip_middle(count)) {
        return false;
    }
    const EditSettings next = strip_band_settings(s, step, count, band);
    if (next == s) {
        return false;
    }
    s = next;
    return true;
}

}  // namespace prism::gui

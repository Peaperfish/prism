#include "prism/ranges.hpp"

#include <cmath>
#include <sstream>

namespace prism {

bool in_range(float v, ValueRange r) noexcept {
    return std::isfinite(v) && v >= static_cast<float>(r.min) && v <= static_cast<float>(r.max);
}

bool is_positive(float v) noexcept {
    return std::isfinite(v) && v > 0.0f;
}

std::string shown_number(float v) {
    std::ostringstream text;
    text << (v == 0.0f ? 0.0f : v);  // -0 shows as 0
    std::string out = text.str();
    if (!out.empty() && out.front() == '-') {
        out.replace(0, 1, "\xE2\x88\x92");  // U+2212 "−"
    }
    return out;
}

std::string range_text(ValueRange r) {
    return shown_number(static_cast<float>(r.min)) + " to " +
           shown_number(static_cast<float>(r.max));
}

std::string range_message(std::string_view name, float v, ValueRange r) {
    return std::string(name) + " must be from " + range_text(r) + " (it is " + shown_number(v) +
           ")";
}

std::string positive_message(std::string_view name, float v) {
    return std::string(name) + " must be a number above 0 (it is " + shown_number(v) + ")";
}

}  // namespace prism

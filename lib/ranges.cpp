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

std::string range_message(std::string_view name, float v, ValueRange r) {
    std::ostringstream message;
    message << name << " must be from " << static_cast<float>(r.min) << " to "
            << static_cast<float>(r.max) << " (it is " << v << ")";
    return message.str();
}

std::string positive_message(std::string_view name, float v) {
    std::ostringstream message;
    message << name << " must be a number above 0 (it is " << v << ")";
    return message.str();
}

}  // namespace prism

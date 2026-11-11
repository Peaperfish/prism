#pragma once

#include <string>
#include <string_view>

namespace prism {

// The values each edit accepts, in one place: the CLI's range checks, the GUI's validate() and
// recipe files (prism/recipe.hpp) all use these, so they can never disagree.
//
//   exposure    any finite number of stops
//   saturation  0 to 10 (1 = unchanged, 0 = greyscale)
//   contrast    0.1 to 10 (1 = unchanged)
//   gamma       any finite number above 0 (default 2.2)
//   white       Reinhard's white point: any finite number above 0 (default 4)
//   kelvin      2000 to 15000 (the light the photo was lit by; see prism/white_balance.hpp)
//   tint        -100 to 100 (positive = a greener light, which makes the photo more magenta)
//   frame angle -45 to 45 degrees (positive turns the picture clockwise; see prism/frame.hpp)
//
// The upper limits only catch typos, such as 125 for 1.25.

// A closed range. The limits are doubles because the CLI compares the double it parsed against
// them; float checks (validate(), recipes) use the limits rounded to float.
struct ValueRange {
    double min = 0;
    double max = 0;
};

inline constexpr ValueRange saturation_range{0.0, 10.0};
inline constexpr ValueRange contrast_range{0.1, 10.0};
inline constexpr ValueRange kelvin_range{2000.0, 15000.0};
inline constexpr ValueRange tint_range{-100.0, 100.0};
inline constexpr ValueRange frame_angle_range{-45.0, 45.0};

inline constexpr float gamma_default = 2.2f;
// 2 stops of highlight headroom above 1.0.
inline constexpr float reinhard_white_default = 4.0f;

// Whether v is finite and within r (both limits included, compared as floats).
[[nodiscard]] bool in_range(float v, ValueRange r) noexcept;

// Whether v is finite and above 0, as gamma and the white point must be.
[[nodiscard]] bool is_positive(float v) noexcept;

// The messages for a value outside its range, e.g. "Saturation must be from 0 to 10 (it is 12)"
// and "Gamma must be a number above 0 (it is -1)". `name` starts the sentence as given.
[[nodiscard]] std::string range_message(std::string_view name, float v, ValueRange r);
[[nodiscard]] std::string positive_message(std::string_view name, float v);

}  // namespace prism

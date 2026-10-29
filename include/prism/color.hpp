#pragma once

#include <cstdint>

namespace prism {

// Exact piecewise sRGB transfer functions (IEC 61966-2-1), on values in [0, 1].
// "Linear" means proportional to light intensity, which is what exposure and colour
// matrices need; "sRGB" is the gamma-encoded form stored in ordinary PNG/JPEG files.
[[nodiscard]] float srgb_to_linear(float srgb) noexcept;
[[nodiscard]] float linear_to_srgb(float linear) noexcept;

// 8-bit helpers used for file I/O. linear_to_srgb8 clamps to [0, 1] (NaN becomes 0)
// and rounds to the nearest integer.
[[nodiscard]] float srgb8_to_linear(std::uint8_t srgb) noexcept;
[[nodiscard]] std::uint8_t linear_to_srgb8(float linear) noexcept;

}  // namespace prism

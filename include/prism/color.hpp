#pragma once

#include <cstddef>
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

// linear_to_srgb8 for `count` values. Bit-identical to calling linear_to_srgb8 on each, but faster
// in a loop, which is what encoding an image is. `in` and `out` must not overlap.
void linear_to_srgb8(const float* in, std::uint8_t* out, std::size_t count) noexcept;

// The straightforward definition of linear_to_srgb8 (clamp, linear_to_srgb with pow, round).
// The fast versions above are table lookups that must agree with this for every possible input;
// it is kept as the reference for tests and for prism_bench's exhaustive check.
[[nodiscard]] std::uint8_t linear_to_srgb8_reference(float linear) noexcept;

}  // namespace prism

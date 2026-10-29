#include "prism/color.hpp"

#include <array>
#include <cmath>

namespace prism {

float srgb_to_linear(float srgb) noexcept {
    if (srgb <= 0.04045f) {
        return srgb / 12.92f;
    }
    return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float linear) noexcept {
    if (linear <= 0.0031308f) {
        return linear * 12.92f;
    }
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

float srgb8_to_linear(std::uint8_t srgb) noexcept {
    // Only 256 possible inputs, so compute them all once instead of calling pow per pixel.
    // A function-local static is initialised on first use, and C++ guarantees that is thread-safe.
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            t[i] = srgb_to_linear(static_cast<float>(i) / 255.0f);
        }
        return t;
    }();
    return table[srgb];
}

std::uint8_t linear_to_srgb8(float linear) noexcept {
    // Written as !(x > 0) rather than x <= 0 so NaN also takes this branch.
    if (!(linear > 0.0f)) {
        return 0;
    }
    if (linear >= 1.0f) {
        return 255;
    }
    return static_cast<std::uint8_t>(std::lround(linear_to_srgb(linear) * 255.0f));
}

}  // namespace prism

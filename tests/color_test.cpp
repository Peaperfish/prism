#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "prism/color.hpp"

TEST(Color, Srgb8RoundTripsExactly) {
    for (int v = 0; v < 256; ++v) {
        const auto srgb = static_cast<std::uint8_t>(v);
        EXPECT_EQ(prism::linear_to_srgb8(prism::srgb8_to_linear(srgb)), srgb) << "value " << v;
    }
}

TEST(Color, KnownLinearToSrgb8Values) {
    EXPECT_EQ(prism::linear_to_srgb8(0.0f), 0);
    EXPECT_EQ(prism::linear_to_srgb8(1.0f), 255);
    EXPECT_EQ(prism::linear_to_srgb8(0.5f), 188);   // 187.52 before rounding
    EXPECT_EQ(prism::linear_to_srgb8(0.18f), 118);  // photographic "middle grey"
}

TEST(Color, KnownSrgbToLinearValues) {
    EXPECT_NEAR(prism::srgb8_to_linear(128), 0.21586f, 1e-5f);
    EXPECT_NEAR(prism::srgb8_to_linear(188), 0.50289f, 1e-5f);
    EXPECT_FLOAT_EQ(prism::srgb8_to_linear(0), 0.0f);
    EXPECT_FLOAT_EQ(prism::srgb8_to_linear(255), 1.0f);
}

TEST(Color, PiecewiseSegmentsMeetAtThreshold) {
    // Both formulas should give (almost) the same value at the switch-over point.
    EXPECT_NEAR(prism::srgb_to_linear(0.04045f), 0.0031308f, 1e-6f);
    EXPECT_NEAR(prism::linear_to_srgb(0.0031308f), 0.04045f, 1e-6f);
    // Below the threshold the curve is a straight line.
    EXPECT_FLOAT_EQ(prism::srgb_to_linear(0.02f), 0.02f / 12.92f);
}

TEST(Color, Srgb8ClampsOutOfRange) {
    EXPECT_EQ(prism::linear_to_srgb8(-0.5f), 0);
    EXPECT_EQ(prism::linear_to_srgb8(4.0f), 255);
    EXPECT_EQ(prism::linear_to_srgb8(std::numeric_limits<float>::infinity()), 255);
    EXPECT_EQ(prism::linear_to_srgb8(std::nanf("")), 0);
}

namespace {

// Smallest float in (0, 1) whose reference result is >= k, found by bisecting on the float's bit
// pattern. This deliberately repeats the library's idea in the test so the two are compared
// at the exact places where an off-by-one ULP error would show.
float reference_boundary(int k) {
    std::uint32_t lo = 0;
    std::uint32_t hi = std::bit_cast<std::uint32_t>(1.0f);
    while (hi - lo > 1) {
        const std::uint32_t mid = lo + (hi - lo) / 2;
        (prism::linear_to_srgb8_reference(std::bit_cast<float>(mid)) >= k ? hi : lo) = mid;
    }
    return std::bit_cast<float>(hi);
}

}  // namespace

TEST(Color, FastEncodeMatchesReferenceAroundEveryBoundary) {
    for (int k = 1; k <= 255; ++k) {
        const std::uint32_t at = std::bit_cast<std::uint32_t>(reference_boundary(k));
        for (std::uint32_t bits = at - 8; bits <= at + 8; ++bits) {
            const float x = std::bit_cast<float>(bits);
            ASSERT_EQ(prism::linear_to_srgb8(x), prism::linear_to_srgb8_reference(x))
                << "k " << k << " bits " << bits - at << " from boundary";
        }
    }
}

TEST(Color, FastEncodeMatchesReferenceOnDenseSweep) {
    constexpr int steps = 2'000'000;
    std::vector<float> in;
    in.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        in.push_back(static_cast<float>(i) / steps);
    }
    std::vector<std::uint8_t> batch(in.size());
    prism::linear_to_srgb8(in.data(), batch.data(), in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const std::uint8_t expected = prism::linear_to_srgb8_reference(in[i]);
        ASSERT_EQ(prism::linear_to_srgb8(in[i]), expected) << in[i];
        ASSERT_EQ(batch[i], expected) << in[i];
    }
}

TEST(Color, FastEncodeMatchesReferenceOutsideZeroToOne) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    const float values[] = {-0.0f, -1e-30f, -1.0f, -inf, std::numeric_limits<float>::denorm_min(),
                            std::numeric_limits<float>::min(), std::nextafter(1.0f, 0.0f), 1.0f,
                            std::nextafter(1.0f, 2.0f), 1.5f, 1e30f,
                            std::numeric_limits<float>::max(), inf, std::nanf(""), -std::nanf(""),
                            // Signalling NaNs (top mantissa bit clear): float min/max instructions
                            // treat these differently from quiet NaNs, which once made them 255.
                            std::bit_cast<float>(0x7F800001u), std::bit_cast<float>(0x7FA00000u),
                            std::bit_cast<float>(0xFFA00000u), std::bit_cast<float>(0x7FFFFFFFu)};
    for (float x : values) {
        EXPECT_EQ(prism::linear_to_srgb8(x), prism::linear_to_srgb8_reference(x)) << x;
    }
}

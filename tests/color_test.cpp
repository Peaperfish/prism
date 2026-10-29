#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>

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

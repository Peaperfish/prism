#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "prism/ranges.hpp"

// The shared value ranges (prism/ranges.hpp). The CLI's own messages are checked in cli_test.cpp
// and validate()'s in gui/settings_test.cpp; these check the pieces both are built from.

TEST(Ranges, LimitsAreIncludedAndCompareAsFloats) {
    EXPECT_TRUE(prism::in_range(0.0f, prism::saturation_range));
    EXPECT_TRUE(prism::in_range(10.0f, prism::saturation_range));
    EXPECT_FALSE(prism::in_range(std::nextafter(10.0f, 11.0f), prism::saturation_range));
    // 0.1 as a float is a little above 0.1, and still in range: a recipe's "0.1" must pass.
    EXPECT_TRUE(prism::in_range(0.1f, prism::contrast_range));
    EXPECT_FALSE(prism::in_range(std::nextafter(0.1f, 0.0f), prism::contrast_range));
    EXPECT_FALSE(prism::in_range(std::numeric_limits<float>::quiet_NaN(), prism::contrast_range));
}

TEST(Ranges, PositiveMeansFiniteAndAboveZero) {
    EXPECT_TRUE(prism::is_positive(std::numeric_limits<float>::denorm_min()));
    EXPECT_FALSE(prism::is_positive(0.0f));
    EXPECT_FALSE(prism::is_positive(-0.0f));
    EXPECT_FALSE(prism::is_positive(std::numeric_limits<float>::infinity()));
}

TEST(Ranges, MessagesInPlainWords) {
    EXPECT_EQ(prism::range_message("Contrast", 12.5f, prism::contrast_range),
              "Contrast must be from 0.1 to 10 (it is 12.5)");
    EXPECT_EQ(prism::positive_message("Gamma", -1.0f), "Gamma must be a number above 0 (it is -1)");
}

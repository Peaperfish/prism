#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include "comma_locale.hpp"
#include "prism/camera_profile.hpp"
#include "prism/pipeline.hpp"
#include "prism/recipe.hpp"

namespace fs = std::filesystem;

// Prism reads "1.5" as one and a half whatever the C locale is: SDL changes LC_ALL for a moment
// on Linux. Each test first checks that the locale really did change (a printf gives a comma).

namespace {

#define SKIP_WITHOUT_COMMA_LOCALE(locale)                                                  \
    if (!(locale).active()) {                                                              \
        GTEST_SKIP() << "this machine has no locale with a decimal comma (tried de_DE, fr_FR)"; \
    }                                                                                      \
    {                                                                                      \
        char shown[32];                                                                    \
        std::snprintf(shown, sizeof shown, "%.1f", 1.5);                                   \
        ASSERT_STREQ(shown, "1,5");                                                        \
    }

}  // namespace

TEST(LocaleNumbers, RecipeNumbersReadTheSame) {
    CommaLocale locale;
    SKIP_WITHOUT_COMMA_LOCALE(locale);
    const prism::Recipe r =
        prism::parse_recipe("prism_recipe = 1\n[exposure]\nstops = -1.25\n[saturation]\n"
                            "amount = 1_0.5e-1\n");
    ASSERT_TRUE(r.exposure.has_value());
    EXPECT_EQ(r.exposure->stops, -1.25f);
    ASSERT_TRUE(r.saturation.has_value());
    EXPECT_EQ(r.saturation->amount, 1.05f);
}

TEST(LocaleNumbers, MatrixFileNumbersReadTheSame) {
    CommaLocale locale;
    SKIP_WITHOUT_COMMA_LOCALE(locale);
    const fs::path path = fs::path(testing::TempDir()) / "prism_locale_matrix.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << "1.05 0.5 -0.25\n0 1 0\n0 0 1\n";
    }
    const prism::Matrix3 m = prism::load_matrix(path);
    fs::remove(path);
    EXPECT_EQ(m[0], 1.05f);
    EXPECT_EQ(m[1], 0.5f);
    EXPECT_EQ(m[2], -0.25f);
}

TEST(LocaleNumbers, CurveParameterReadsTheSame) {
    CommaLocale locale;
    SKIP_WITHOUT_COMMA_LOCALE(locale);
    // gamma:2 maps 0.25 to 0.5 (the square root); a parameter read as 2,5 or cut at the dot
    // would not.
    EXPECT_NEAR(prism::parse_curve("gamma:2.0").map(0.25f), 0.5f, 1e-3f);
    EXPECT_THROW((void)prism::parse_curve("gamma:2,5"), std::runtime_error);
}

TEST(LocaleNumbers, ProfileJsonNumbersReadTheSame) {
    CommaLocale locale;
    SKIP_WITHOUT_COMMA_LOCALE(locale);
    const prism::LightCraftProfile p = prism::parse_lightcraft_profile(
        R"({"version": 1, "model": "X", "files": 1, "samples": 2, )"
        R"("matrix": [[1.2, -0.3, 0.1], [-0.1, 1.2, -0.1], [0.0, -0.8, 1.8]]})");
    EXPECT_DOUBLE_EQ(p.matrix[0][0], 1.2);
    EXPECT_DOUBLE_EQ(p.matrix[2][1], -0.8);
}

// The same without a comma locale: a number beyond a float is refused, not cast.
TEST(LocaleNumbers, ANumberBeyondAFloatIsRefused) {
    const fs::path path = fs::path(testing::TempDir()) / "prism_huge_matrix.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << "1e300 0 0\n0 1 0\n0 0 1\n";
    }
    EXPECT_THROW((void)prism::load_matrix(path), std::runtime_error);
    fs::remove(path);
    EXPECT_THROW((void)prism::parse_curve("gamma:1e39"), std::runtime_error);
    EXPECT_NO_THROW((void)prism::parse_curve("gamma:1e38"));
}

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "prism/image.hpp"
#include "prism/pipeline.hpp"

namespace fs = std::filesystem;

namespace {

// A 2x1 image with two distinct pixels: (0.1, 0.2, 0.3) and (0.5, 0.25, 1.0).
prism::Image make_test_image() {
    prism::Image img;
    img.width = 2;
    img.height = 1;
    img.pixels = {0.1f, 0.2f, 0.3f, 0.5f, 0.25f, 1.0f};
    return img;
}

fs::path write_temp_file(const char* name, std::string_view contents) {
    const fs::path path = fs::path(testing::TempDir()) / name;
    std::ofstream(path) << contents;
    return path;
}

}  // namespace

// --- exposure ---------------------------------------------------------------------------------

TEST(Exposure, ZeroStopsChangesNothing) {
    prism::Image img = make_test_image();
    prism::exposure(img, 0.0f);
    EXPECT_EQ(img.pixels, make_test_image().pixels);
}

TEST(Exposure, PlusOneStopDoubles) {
    prism::Image img = make_test_image();
    prism::exposure(img, 1.0f);
    const prism::Image original = make_test_image();
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        EXPECT_FLOAT_EQ(img.pixels[i], 2.0f * original.pixels[i]);
    }
}

TEST(Exposure, MinusTwoStopsQuarters) {
    prism::Image img = make_test_image();
    prism::exposure(img, -2.0f);
    const prism::Image original = make_test_image();
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        EXPECT_FLOAT_EQ(img.pixels[i], 0.25f * original.pixels[i]);
    }
}

// --- apply_matrix -----------------------------------------------------------------------------

TEST(ApplyMatrix, IdentityChangesNothing) {
    prism::Image img = make_test_image();
    prism::apply_matrix(img, prism::identity_matrix);
    EXPECT_EQ(img.pixels, make_test_image().pixels);
}

TEST(ApplyMatrix, SwapsRedAndBlue) {
    const prism::Matrix3 swap_rb = {0, 0, 1,  //
                                    0, 1, 0,  //
                                    1, 0, 0};
    prism::Image img = make_test_image();
    prism::apply_matrix(img, swap_rb);
    const std::vector<float> expected = {0.3f, 0.2f, 0.1f, 1.0f, 0.25f, 0.5f};
    EXPECT_EQ(img.pixels, expected);
}

TEST(ApplyMatrix, MixesChannelsRowMajor) {
    // Output R = average of input R, G, B; G and B unchanged.
    const prism::Matrix3 m = {1.0f / 3, 1.0f / 3, 1.0f / 3,  //
                              0, 1, 0,                       //
                              0, 0, 1};
    prism::Image img = make_test_image();
    prism::apply_matrix(img, m);
    EXPECT_FLOAT_EQ(img.pixels[0], (0.1f + 0.2f + 0.3f) / 3);
    EXPECT_FLOAT_EQ(img.pixels[1], 0.2f);
    EXPECT_FLOAT_EQ(img.pixels[2], 0.3f);
    EXPECT_FLOAT_EQ(img.pixels[3], (0.5f + 0.25f + 1.0f) / 3);
}

// --- load_matrix ------------------------------------------------------------------------------

TEST(LoadMatrix, ReadsNineNumbersWithComments) {
    const fs::path path = write_temp_file("prism_matrix_ok.txt",
                                          "# a comment line\n"
                                          "1.05 0.03 -0.01  # red row\n"
                                          "0 1 0\n"
                                          "\n"
                                          "-2e-2 +0.0 0.94\n");
    const prism::Matrix3 m = prism::load_matrix(path);
    fs::remove(path);
    const prism::Matrix3 expected = {1.05f, 0.03f, -0.01f, 0, 1, 0, -0.02f, 0, 0.94f};
    EXPECT_EQ(m, expected);
}

TEST(LoadMatrix, TooFewNumbersThrows) {
    const fs::path path = write_temp_file("prism_matrix_short.txt", "1 0 0\n0 1 0\n0 0\n");
    EXPECT_THROW((void)prism::load_matrix(path), std::runtime_error);
    fs::remove(path);
}

TEST(LoadMatrix, TooManyNumbersThrows) {
    const fs::path path = write_temp_file("prism_matrix_long.txt", "1 0 0\n0 1 0\n0 0 1\n7\n");
    EXPECT_THROW((void)prism::load_matrix(path), std::runtime_error);
    fs::remove(path);
}

TEST(LoadMatrix, NonNumberThrowsWithLineNumber) {
    const fs::path path = write_temp_file("prism_matrix_bad.txt", "1 0 0\n0 x 0\n0 0 1\n");
    try {
        (void)prism::load_matrix(path);
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("line 2"), std::string::npos) << e.what();
    }
    fs::remove(path);
}

TEST(LoadMatrix, MissingFileThrows) {
    EXPECT_THROW((void)prism::load_matrix(fs::path(testing::TempDir()) / "no_such_matrix.txt"),
                 std::runtime_error);
}

// --- tone curves ------------------------------------------------------------------------------

TEST(ToneCurve, GammaOneChangesNothing) {
    prism::Image img = make_test_image();
    prism::tone_curve(img, prism::parse_curve("gamma:1"));
    const prism::Image original = make_test_image();
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        EXPECT_FLOAT_EQ(img.pixels[i], original.pixels[i]);
    }
}

TEST(ToneCurve, GammaTwoIsSquareRoot) {
    const prism::ToneCurve curve = prism::parse_curve("gamma:2");
    EXPECT_FLOAT_EQ(curve.map(0.25f), 0.5f);
    EXPECT_FLOAT_EQ(curve.map(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.map(1.0f), 1.0f);
}

TEST(ToneCurve, ReinhardMapsWhitePointToOne) {
    EXPECT_FLOAT_EQ(prism::parse_curve("reinhard").map(4.0f), 1.0f);  // default white = 4
    EXPECT_FLOAT_EQ(prism::parse_curve("reinhard:8").map(8.0f), 1.0f);
    EXPECT_FLOAT_EQ(prism::parse_curve("reinhard").map(0.0f), 0.0f);
}

TEST(ToneCurve, FilmicStartsAtZeroAndReachesWhite) {
    const prism::ToneCurve curve = prism::parse_curve("filmic");
    EXPECT_NEAR(curve.map(0.0f), 0.0f, 1e-6f);
    // 2x exposure bias and white point 11.2: linear 5.6 is the input that reaches 1.0.
    EXPECT_NEAR(curve.map(5.6f), 1.0f, 1e-5f);
}

TEST(ToneCurve, CurvesAreMonotonic) {
    for (const char* spec : {"gamma:2.2", "gamma:0.5", "reinhard", "filmic"}) {
        const prism::ToneCurve curve = prism::parse_curve(spec);
        float previous = curve.map(0.0f);
        for (float x = 0.01f; x <= 8.0f; x += 0.01f) {
            const float y = curve.map(x);
            ASSERT_GE(y, previous) << spec << " at x = " << x;
            previous = y;
        }
    }
}

TEST(ToneCurve, NegativeInputsGiveZeroNotNaN) {
    // A colour matrix can push a channel slightly below zero.
    for (const char* spec : {"gamma:2.2", "reinhard", "filmic"}) {
        const float y = prism::parse_curve(spec).map(-0.1f);
        EXPECT_FALSE(std::isnan(y)) << spec;
        EXPECT_NEAR(y, 0.0f, 1e-6f) << spec;
    }
}

TEST(ToneCurve, BadSpecsThrow) {
    for (const char* spec :
         {"", "nope", "gamma", "gamma:", "gamma:0", "gamma:-1", "gamma:abc", "gamma:2.2x",
          "reinhard:0", "filmic:2"}) {
        EXPECT_THROW((void)prism::parse_curve(spec), std::runtime_error) << "spec '" << spec << "'";
    }
}

TEST(ToneCurve, SpecsListEveryCurve) {
    // Every advertised name must parse (using a valid parameter where one is required).
    for (std::string_view spec : prism::curve_specs()) {
        const std::string name(spec.substr(0, spec.find_first_of(":[")));
        const std::string example = name == "gamma" ? "gamma:2.2" : name;
        EXPECT_EQ(prism::parse_curve(example).name, name);
    }
}

// --- saturation -------------------------------------------------------------------------------

namespace {

float luminance(const float* p) {
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

// A row of assorted colours, including a negative channel (as a camera matrix can produce),
// zero, black and values above 1.
prism::Image make_colour_image() {
    prism::Image img;
    img.pixels = {0.1f, 0.2f,  0.3f, 0.5f, 0.25f, 1.0f, 0.8f, 0.05f, 0.02f, 0.0f,  0.4f, 0.1f,
                  2.0f, 1.5f,  0.3f, 0.3f, -0.02f, 0.1f, 0.0f, 0.0f,  0.0f,  0.18f, 0.18f, 0.18f};
    img.width = static_cast<int>(img.pixels.size() / 3);
    img.height = 1;
    return img;
}

}  // namespace

TEST(Saturation, OneIsANoOp) {
    prism::Image img = make_colour_image();
    prism::saturation(img, 1.0f);
    EXPECT_EQ(img.pixels, make_colour_image().pixels);  // bit-identical, negatives included
}

TEST(Saturation, GreysStayGrey) {
    prism::Image img;
    img.width = 5;
    img.height = 1;
    img.pixels = {0.0f, 0.0f, 0.0f, 0.01f, 0.01f, 0.01f, 0.18f, 0.18f, 0.18f,
                  1.0f, 1.0f, 1.0f, 3.0f,  3.0f,  3.0f};
    for (float s : {0.0f, 0.5f, 1.5f, 4.0f}) {
        prism::Image out = img;
        prism::saturation(out, s);
        for (std::size_t i = 0; i < out.pixels.size(); i += 3) {
            EXPECT_EQ(out.pixels[i], out.pixels[i + 1]) << "s " << s << " pixel " << i / 3;
            EXPECT_EQ(out.pixels[i], out.pixels[i + 2]) << "s " << s << " pixel " << i / 3;
            EXPECT_NEAR(out.pixels[i], img.pixels[i], 1e-6f) << "s " << s;
        }
    }
}

TEST(Saturation, KeepsLuminanceAndScalesDistanceFromIt) {
    const prism::Image in = make_colour_image();
    prism::Image out = in;
    prism::saturation(out, 1.3f);
    for (std::size_t i = 0; i < in.pixels.size(); i += 3) {
        const float y = luminance(&in.pixels[i]);
        if (y <= 0.0f) {
            continue;
        }
        EXPECT_NEAR(luminance(&out.pixels[i]), y, 1e-5f) << "pixel " << i / 3;
        for (int c = 0; c < 3; ++c) {
            EXPECT_GE(out.pixels[i + c], 0.0f) << "pixel " << i / 3;
        }
    }
    // Pixel 0 (0.1, 0.2, 0.3) has room to grow: exactly y + 1.3 * (c - y).
    const float y = luminance(&in.pixels[0]);
    for (int c = 0; c < 3; ++c) {
        EXPECT_NEAR(out.pixels[c], y + 1.3f * (in.pixels[c] - y), 1e-6f);
    }
}

TEST(Saturation, ZeroGivesGreyscale) {
    prism::Image img = make_colour_image();
    prism::saturation(img, 0.0f);
    for (std::size_t i = 0; i < img.pixels.size(); i += 3) {
        EXPECT_EQ(img.pixels[i], img.pixels[i + 1]);
        EXPECT_EQ(img.pixels[i], img.pixels[i + 2]);
    }
}

TEST(Saturation, LimitsTheBoostInsteadOfGoingNegative) {
    // A deep orange: boosting by 3 would send blue to y + 3 * (0.02 - y) < 0. Instead blue lands on
    // exactly 0 and red and green keep their ratio to each other's distance from y (same hue).
    prism::Image img;
    img.width = 1;
    img.height = 1;
    img.pixels = {0.8f, 0.3f, 0.02f};
    const float y = luminance(img.pixels.data());
    prism::saturation(img, 3.0f);
    EXPECT_NEAR(img.pixels[2], 0.0f, 1e-6f);
    const float k = y / (y - 0.02f);
    EXPECT_NEAR(img.pixels[0], y + k * (0.8f - y), 1e-5f);
    EXPECT_NEAR(img.pixels[1], y + k * (0.3f - y), 1e-5f);
}

TEST(Saturation, IsMonotonicInStrength) {
    // More saturation never brings a colour closer to grey.
    const prism::Image in = make_colour_image();
    float previous_spread = 0.0f;
    for (float s : {0.0f, 0.25f, 0.5f, 1.0f, 1.25f, 2.0f, 4.0f}) {
        prism::Image out = in;
        prism::saturation(out, s);
        const float* p = &out.pixels[0];
        const float spread = *std::max_element(p, p + 3) - *std::min_element(p, p + 3);
        EXPECT_GE(spread, previous_spread) << "s " << s;
        previous_spread = spread;
    }
}

TEST(Saturation, NonPositiveLuminanceBecomesBlack) {
    prism::Image img;
    img.width = 1;
    img.height = 1;
    img.pixels = {-0.1f, 0.0f, 0.05f};  // y < 0
    prism::saturation(img, 1.2f);
    EXPECT_EQ(img.pixels, (std::vector<float>{0.0f, 0.0f, 0.0f}));
}

// --- contrast ---------------------------------------------------------------------------------

namespace {

// Values from 0 to 20 on a log-ish grid (plus 0, negatives, and the pivot itself).
prism::Image make_ramp() {
    prism::Image img;
    img.pixels = {-0.1f, 0.0f};
    for (float x = 1e-6f; x < 20.0f; x *= 1.07f) {
        img.pixels.push_back(x);
    }
    img.pixels.push_back(prism::contrast_pivot);
    std::sort(img.pixels.begin(), img.pixels.end());
    while (img.pixels.size() % 3 != 0) {
        img.pixels.push_back(img.pixels.back() * 1.07f);
    }
    img.width = static_cast<int>(img.pixels.size() / 3);
    img.height = 1;
    return img;
}

float contrast_of(float x, float c) {
    prism::Image img;
    img.width = 1;
    img.height = 1;
    img.pixels = {x, x, x};
    prism::contrast(img, c);
    return img.pixels[0];
}

}  // namespace

TEST(Contrast, OneIsANoOp) {
    prism::Image img = make_ramp();
    prism::contrast(img, 1.0f);
    EXPECT_EQ(img.pixels, make_ramp().pixels);
}

TEST(Contrast, PivotIsUnchanged) {
    for (float c : {0.25f, 0.8f, 1.15f, 2.0f, 5.0f}) {
        EXPECT_NEAR(contrast_of(0.18f, c), 0.18f, 1e-7f) << "c " << c;
    }
}

TEST(Contrast, IsStrictlyIncreasing) {
    for (float c : {0.1f, 0.5f, 0.9f, 1.15f, 1.5f, 3.0f, 10.0f}) {
        prism::Image img = make_ramp();
        prism::contrast(img, c);
        for (std::size_t i = 1; i < img.pixels.size(); ++i) {
            ASSERT_GT(img.pixels[i], img.pixels[i - 1])
                << "c " << c << " at input " << make_ramp().pixels[i];
        }
    }
}

TEST(Contrast, StretchesStopsAroundMiddleGrey) {
    // Near the pivot one stop becomes about c stops.
    const float c = 1.15f;
    const float up = std::log2(contrast_of(0.18f * std::exp2(0.01f), c) / 0.18f);
    EXPECT_NEAR(up / 0.01f, c, 1e-3f);
    // Above the pivot it brightens, below it darkens (c > 1); the reverse for c < 1.
    EXPECT_GT(contrast_of(0.5f, c), 0.5f);
    EXPECT_LT(contrast_of(0.05f, c), 0.05f);
    EXPECT_LT(contrast_of(0.5f, 0.8f), 0.5f);
    EXPECT_GT(contrast_of(0.05f, 0.8f), 0.05f);
    // Far from the pivot the shift is bounded by (c - 1) * w stops.
    const float far = std::log2(contrast_of(0.18f * 1024.0f, c) / (0.18f * 1024.0f));
    EXPECT_LE(far, (c - 1.0f) * prism::contrast_range_stops + 1e-4f);
    EXPECT_GT(far, 0.95f * (c - 1.0f) * prism::contrast_range_stops);
}

TEST(Contrast, BehavesTheSameAtAnyExposure) {
    // Working in stops: the same ratio of light around the pivot gets the same treatment
    // whatever its absolute level, i.e. out(0.18 * 2^e) / 0.18 = 2^f(e) for one function f.
    // So a pixel one stop over the pivot and one stop under move by equal and opposite stops.
    const float c = 1.3f;
    const float over = std::log2(contrast_of(0.36f, c) / 0.18f);
    const float under = std::log2(contrast_of(0.09f, c) / 0.18f);
    EXPECT_NEAR(over, -under, 1e-5f);
}

TEST(Contrast, NonPositiveValuesStayFiniteAndContinuous) {
    for (float c : {0.5f, 1.15f, 4.0f}) {
        EXPECT_EQ(contrast_of(0.0f, c), 0.0f);
        const float neg = contrast_of(-0.01f, c);
        EXPECT_TRUE(std::isfinite(neg));
        EXPECT_LT(neg, 0.0f);
        // Just above 0 the curve approaches the slope it has below 0.
        const float slope_below = neg / -0.01f;
        EXPECT_NEAR(contrast_of(1e-30f, c) / 1e-30f, slope_below, 1e-3f * slope_below);
    }
}

TEST(Contrast, ActsOnEachChannel) {
    prism::Image img;
    img.width = 1;
    img.height = 1;
    img.pixels = {0.05f, 0.18f, 0.6f};
    prism::contrast(img, 1.5f);
    EXPECT_NEAR(img.pixels[0], contrast_of(0.05f, 1.5f), 1e-7f);
    EXPECT_NEAR(img.pixels[1], 0.18f, 1e-7f);
    EXPECT_NEAR(img.pixels[2], contrast_of(0.6f, 1.5f), 1e-7f);
}

TEST(ToneCurve, ImageMatchesMapBitForBit) {
    // tone_curve runs a loop specialised (and vectorised) per curve type; map() handles one value.
    // Both must give exactly the same bits, for values spread over every float exponent: zero,
    // denormals, negatives, huge values, infinities and NaN (any NaN counts as equal).
    prism::Image img;
    for (std::uint64_t bits = 0; bits < (1ull << 32); bits += 4093) {
        img.pixels.push_back(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
    }
    img.pixels.resize(img.pixels.size() / 3 * 3);
    img.width = static_cast<int>(img.pixels.size() / 3);
    img.height = 1;
    for (const char* spec : {"gamma:2.2", "gamma:0.5", "reinhard", "reinhard:1.5", "filmic"}) {
        const prism::ToneCurve curve = prism::parse_curve(spec);
        prism::Image out = img;
        prism::tone_curve(out, curve);
        for (std::size_t i = 0; i < img.pixels.size(); ++i) {
            const float expected = curve.map(img.pixels[i]);
            if (std::isnan(expected)) {
                ASSERT_TRUE(std::isnan(out.pixels[i])) << spec << " input " << img.pixels[i];
            } else {
                ASSERT_EQ(std::bit_cast<std::uint32_t>(out.pixels[i]),
                          std::bit_cast<std::uint32_t>(expected))
                    << spec << " input " << img.pixels[i];
            }
        }
    }
}

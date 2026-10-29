#include <gtest/gtest.h>

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

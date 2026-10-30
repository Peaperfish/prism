#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "prism/camera_profile.hpp"
#include "prism/pipeline.hpp"

namespace fs = std::filesystem;

namespace {

void expect_near(const prism::Mat3& actual, const prism::Mat3& expected, double tolerance) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_NEAR(actual[i][j], expected[i][j], tolerance) << "[" << i << "][" << j << "]";
        }
    }
}

void expect_rows_sum_to_one(const prism::Mat3& m) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(m[i][0] + m[i][1] + m[i][2], 1.0, 1e-12) << "row " << i;
    }
}

// A made-up but plausible camera: XYZ -> camera RGB (ColorMatrix-like), well conditioned.
constexpr prism::Mat3 sample_color_matrix = {{{0.81, -0.27, -0.05},  //
                                              {-0.44, 1.22, 0.24},
                                              {-0.08, 0.17, 0.62}}};

// A generated LightCraft-style profile; the real LightCraft files are not used in tests.
std::string profile_json(const std::string& matrix, const std::string& extra = "") {
    return R"({"version": 1, "model": "TEST-CAM", "files": 12, "samples": 3456, "matrix": )" +
           matrix + extra + "}";
}
const std::string sample_matrix = "[[1.2, -0.3, 0.1], [-0.1, 1.2, -0.1], [0.0, -0.8, 1.8]]";

}  // namespace

// --- 3x3 maths -----------------------------------------------------------------------------------

TEST(Mat3, MultiplyByIdentityChangesNothing) {
    expect_near(prism::multiply(sample_color_matrix, prism::identity3), sample_color_matrix, 0);
    expect_near(prism::multiply(prism::identity3, sample_color_matrix), sample_color_matrix, 0);
}

TEST(Mat3, MultiplyIsRowTimesColumn) {
    const prism::Mat3 a = {{{1, 2, 0}, {0, 1, 0}, {0, 0, 1}}};
    const prism::Mat3 b = {{{1, 0, 0}, {3, 1, 0}, {0, 0, 2}}};
    // Not commutative: a * b != b * a, which is why the order of a colour chain matters.
    expect_near(prism::multiply(a, b), {{{7, 2, 0}, {3, 1, 0}, {0, 0, 2}}}, 0);
    expect_near(prism::multiply(b, a), {{{1, 2, 0}, {3, 7, 0}, {0, 0, 2}}}, 0);
}

TEST(Mat3, InverseTimesMatrixIsIdentity) {
    for (const prism::Mat3& m : {sample_color_matrix, prism::srgb_to_xyz(), prism::rec2020_to_xyz(),
                                 prism::Mat3{{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}}}) {
        const prism::Mat3 inv = prism::invert(m);
        expect_near(prism::multiply(inv, m), prism::identity3, 1e-12);
        expect_near(prism::multiply(m, inv), prism::identity3, 1e-12);
    }
}

TEST(Mat3, InvertIgnoresOverallScale) {
    // A well-conditioned matrix stays invertible however small its entries are.
    prism::Mat3 tiny{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            tiny[i][j] = sample_color_matrix[i][j] * 1e-6;
        }
    }
    expect_near(prism::multiply(prism::invert(tiny), tiny), prism::identity3, 1e-9);
}

TEST(Mat3, SingularMatrixThrows) {
    const prism::Mat3 zero{};
    const prism::Mat3 equal_rows = {{{1, 2, 3}, {1, 2, 3}, {0, 1, 0}}};
    const prism::Mat3 dependent = {{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}};  // row 3 = 2 * row 2 - row 1
    EXPECT_THROW((void)prism::invert(zero), std::domain_error);
    EXPECT_THROW((void)prism::invert(equal_rows), std::domain_error);
    EXPECT_THROW((void)prism::invert(dependent), std::domain_error);
    const prism::Mat3 nan = {{{NAN, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    EXPECT_THROW((void)prism::invert(nan), std::domain_error);
}

TEST(Mat3, NormalizeRowsMakesRowsSumToOne) {
    const prism::Mat3 m = prism::normalize_rows(sample_color_matrix);
    expect_rows_sum_to_one(m);
    // Each row is only scaled, so ratios within a row are kept.
    EXPECT_NEAR(m[1][1] / m[1][0], sample_color_matrix[1][1] / sample_color_matrix[1][0], 1e-12);
    EXPECT_THROW((void)prism::normalize_rows({{{1, -1, 0}, {0, 1, 0}, {0, 0, 1}}}),
                 std::domain_error);
}

TEST(Mat3, ToMatrix3IsRowMajor) {
    const prism::Matrix3 m = prism::to_matrix3({{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}});
    EXPECT_EQ(m, (prism::Matrix3{1, 2, 3, 4, 5, 6, 7, 8, 9}));
}

// --- Standard colour-space matrices --------------------------------------------------------------

TEST(ColourSpaces, SrgbToXyzMatchesPublishedValues) {
    // IEC 61966-2-1 gives this matrix to 4 decimals, from the same primaries and D65 xy.
    // (Tables such as Lindbloom's use D65 Z = 1.08883 and differ by 2e-4 in the last entry.)
    expect_near(prism::srgb_to_xyz(),
                {{{0.4124, 0.3576, 0.1805}, {0.2126, 0.7152, 0.0722}, {0.0193, 0.1192, 0.9505}}},
                1e-4);
    // The Y row is the luminance of R, G and B: Rec. 709 luma weights.
    EXPECT_NEAR(prism::srgb_to_xyz()[1][0], 0.2126, 1e-4);
    EXPECT_NEAR(prism::srgb_to_xyz()[1][2], 0.0722, 1e-4);
}

TEST(ColourSpaces, XyzToSrgbMatchesPublishedValues) {
    expect_near(prism::xyz_to_srgb(),
                {{{3.2404542, -1.5371385, -0.4985314},
                  {-0.9692660, 1.8760108, 0.0415560},
                  {0.0556434, -0.2040259, 1.0572252}}},
                2e-3);
}

TEST(ColourSpaces, Rec2020ToXyzMatchesPublishedValues) {
    // ITU-R BT.2020 primaries with D65 (values as in SMPTE RP 177 derivations).
    expect_near(prism::rec2020_to_xyz(),
                {{{0.6369580, 0.1446169, 0.1688810},
                  {0.2627002, 0.6779981, 0.0593017},
                  {0.0000000, 0.0280727, 1.0609851}}},
                1e-6);
}

TEST(ColourSpaces, Rec2020ToSrgbMatchesBt2087) {
    // ITU-R BT.2087 gives this linear Rec. 2020 -> Rec. 709 matrix.
    expect_near(
        prism::multiply(prism::xyz_to_srgb(), prism::rec2020_to_xyz()),
        {{{1.6605, -0.5876, -0.0728}, {-0.1246, 1.1329, -0.0083}, {-0.0182, -0.1006, 1.1187}}},
        1e-4);
}

TEST(ColourSpaces, WhiteMapsToWhite) {
    for (const prism::Mat3& m : {prism::srgb_to_xyz(), prism::rec2020_to_xyz()}) {
        const prism::Vec3 white = prism::multiply(m, prism::Vec3{1, 1, 1});
        const prism::Vec3 d65 = prism::xyz_of(prism::white_d65);
        for (int i = 0; i < 3; ++i) {
            EXPECT_NEAR(white[i], d65[i], 1e-12);
        }
    }
}

TEST(ColourSpaces, SrgbXyzRoundTrip) {
    expect_near(prism::multiply(prism::xyz_to_srgb(), prism::srgb_to_xyz()), prism::identity3,
                1e-12);
}

TEST(Bradford, MatchesPublishedD50ToD65) {
    // Lindbloom's Bradford D50 -> D65 (his whites differ from xy-derived ones in the 4th decimal).
    expect_near(prism::bradford(prism::white_d50, prism::white_d65),
                {{{0.9555766, -0.0230393, 0.0631636},
                  {-0.0282895, 1.0099416, 0.0210077},
                  {0.0122982, -0.0204830, 1.3299098}}},
                5e-4);
}

TEST(Bradford, MapsWhiteToWhiteAndRoundTrips) {
    const prism::Mat3 d50_to_d65 = prism::bradford(prism::white_d50, prism::white_d65);
    const prism::Mat3 d65_to_d50 = prism::bradford(prism::white_d65, prism::white_d50);
    const prism::Vec3 white = prism::multiply(d50_to_d65, prism::xyz_of(prism::white_d50));
    const prism::Vec3 d65 = prism::xyz_of(prism::white_d65);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(white[i], d65[i], 1e-12);
    }
    expect_near(prism::multiply(d65_to_d50, d50_to_d65), prism::identity3, 1e-12);
    expect_near(prism::bradford(prism::white_d65, prism::white_d65), prism::identity3, 1e-12);
}

// --- Camera -> sRGB ------------------------------------------------------------------------------

TEST(CameraToSrgb, FromRec2020UndoesSrgbToRec2020) {
    // A "camera" whose RGB is already linear sRGB, described as camera -> Rec. 2020, must come
    // back as the identity.
    const prism::Mat3 srgb_to_rec2020 =
        prism::multiply(prism::invert(prism::rec2020_to_xyz()), prism::srgb_to_xyz());
    const prism::Mat3 m = prism::camera_to_srgb_from_rec2020(srgb_to_rec2020);
    expect_near(m, prism::identity3, 1e-12);
    expect_rows_sum_to_one(prism::camera_to_srgb_from_rec2020(sample_color_matrix));
}

TEST(CameraToSrgb, ForwardAndColorMatrixAgreeForTheSameCamera) {
    // Build a consistent pair from one made-up camera, as a DNG would carry them:
    //   ColorMatrix  = XYZ -> raw camera RGB
    //   ForwardMatrix = white-balanced camera RGB -> XYZ D50
    //                 = bradford(D65, D50) * invert(ColorMatrix) * diag(neutral)
    // (exactly true only for a camera whose matrix is valid at both whites; true here by design).
    const prism::Mat3& color = sample_color_matrix;
    const prism::Vec3 neutral = prism::multiply(color, prism::xyz_of(prism::white_d65));
    prism::Mat3 balance{};
    for (int i = 0; i < 3; ++i) {
        balance[i][i] = neutral[i];
    }
    const prism::Mat3 forward = prism::multiply(prism::bradford(prism::white_d65, prism::white_d50),
                                                prism::multiply(prism::invert(color), balance));

    const prism::Mat3 via_forward = prism::camera_to_srgb_from_forward_matrix(forward);
    const prism::Mat3 via_color = prism::camera_to_srgb_from_color_matrix(color);
    expect_rows_sum_to_one(via_forward);
    expect_rows_sum_to_one(via_color);
    expect_near(via_forward, via_color, 1e-12);
}

TEST(CameraToSrgb, ColorMatrixOfAnSrgbCameraGivesIdentity) {
    // A camera that sees exactly linear sRGB has ColorMatrix = xyz_to_srgb (up to any per-channel
    // gain, which white balance removes).
    prism::Mat3 color = prism::xyz_to_srgb();
    for (int j = 0; j < 3; ++j) {
        color[0][j] *= 0.5;  // a weak red channel
        color[2][j] *= 2.0;  // a strong blue channel
    }
    expect_near(prism::camera_to_srgb_from_color_matrix(color), prism::identity3, 1e-12);
}

TEST(CameraToSrgb, DngPicksTheCalibrationClosestToD65) {
    const prism::Mat3 near_d65 = sample_color_matrix;
    const prism::Mat3 tungsten = prism::xyz_to_srgb();
    std::array<prism::DngCalibration, 2> cal{};
    cal[0] = {17, tungsten, std::nullopt};  // standard light A
    cal[1] = {21, near_d65, std::nullopt};  // D65
    expect_near(prism::camera_to_srgb_from_dng(cal),
                prism::camera_to_srgb_from_color_matrix(near_d65), 1e-12);
    std::swap(cal[0], cal[1]);
    expect_near(prism::camera_to_srgb_from_dng(cal),
                prism::camera_to_srgb_from_color_matrix(near_d65), 1e-12);

    // A ForwardMatrix wins over the ColorMatrix of the same calibration.
    const prism::Mat3 forward =
        prism::multiply(prism::bradford(prism::white_d65, prism::white_d50), prism::srgb_to_xyz());
    cal[0].forward_matrix = forward;
    expect_near(prism::camera_to_srgb_from_dng(cal), prism::identity3, 1e-12);

    EXPECT_THROW((void)prism::camera_to_srgb_from_dng({}), std::invalid_argument);
}

TEST(CameraToSrgb, IlluminantTemperatures) {
    EXPECT_EQ(prism::illuminant_kelvin(21), 6504);
    EXPECT_EQ(prism::illuminant_kelvin(17), 2856);
    EXPECT_FALSE(prism::illuminant_kelvin(0).has_value());
    EXPECT_FALSE(prism::illuminant_kelvin(255).has_value());
}

// --- Interpolating DNG calibrations by temperature -----------------------------------------------

namespace {

constexpr int light_a = 17;    // 2856 K
constexpr int light_d65 = 21;  // 6504 K

// The weight of the 2856 K calibration at 4000 K, worked by hand in inverse kelvin:
//   1/2856 = 0.000350140056    1/6504 = 0.000153751538    1/4000 = 0.000250000000
//   g = (1/4000 - 1/6504) / (1/2856 - 1/6504)
//     = 0.000096248462 / 0.000196388519
//     = 0.490092...
// (Interpolating in kelvin instead would give (4000 - 6504) / (2856 - 6504) = 0.686.)
constexpr double weight_a_at_4000 = 0.4900921;

// Two made-up calibrations, different enough that interpolation visibly moves the result.
constexpr prism::Mat3 color_a = {{{1.02, -0.38, -0.11},  //
                                  {-0.52, 1.36, 0.19},
                                  {-0.12, 0.29, 0.85}}};
constexpr prism::Mat3 forward_a = {{{0.61, 0.24, 0.11},  //
                                    {0.20, 0.86, -0.06},
                                    {0.05, -0.18, 0.95}}};
constexpr prism::Mat3 forward_d65 = {{{0.72, 0.13, 0.11},  //
                                      {0.31, 0.71, -0.02},
                                      {0.02, -0.06, 0.86}}};

prism::Mat3 blend(const prism::Mat3& a, const prism::Mat3& b, double weight_a) {
    prism::Mat3 out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i][j] = weight_a * a[i][j] + (1 - weight_a) * b[i][j];
        }
    }
    return out;
}

std::array<prism::DngCalibration, 2> color_pair() {
    return {{{light_a, color_a, std::nullopt}, {light_d65, sample_color_matrix, std::nullopt}}};
}

std::array<prism::DngCalibration, 2> forward_pair() {
    return {{{light_a, color_a, forward_a}, {light_d65, sample_color_matrix, forward_d65}}};
}

std::array<prism::DngCalibration, 2> swapped(std::array<prism::DngCalibration, 2> cal) {
    std::swap(cal[0], cal[1]);
    return cal;
}

// Expects std::runtime_error whose message contains `expected`.
void expect_dng_error(const std::array<prism::DngCalibration, 2>& cal, double kelvin,
                      const std::string& expected) {
    try {
        (void)prism::camera_to_srgb_from_dng(cal, kelvin);
        ADD_FAILURE() << "no error at " << kelvin << " K; expected \"" << expected << "\"";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find(expected), std::string::npos) << e.what();
    }
}

}  // namespace

TEST(DngInterpolation, WeightIsLinearInInverseTemperature) {
    EXPECT_NEAR(prism::dng_interpolation_weight(4000, 2856, 6504), weight_a_at_4000, 1e-7);
    // Halfway in mireds (1e6 / K): 1e6 / 4000 = 250 is halfway between 200 and 300.
    EXPECT_NEAR(prism::dng_interpolation_weight(4000, 1e6 / 300, 1e6 / 200), 0.5, 1e-12);
}

TEST(DngInterpolation, WeightDoesNotDependOnOrder) {
    // Swapping the calibrations swaps which one the weight is for.
    EXPECT_NEAR(prism::dng_interpolation_weight(4000, 6504, 2856), 1 - weight_a_at_4000, 1e-7);
    for (double k : {3000.0, 4000.0, 5000.0, 6000.0}) {
        EXPECT_NEAR(prism::dng_interpolation_weight(k, 2856, 6504) +
                        prism::dng_interpolation_weight(k, 6504, 2856),
                    1.0, 1e-12)
            << k;
    }
}

TEST(DngInterpolation, WeightClampsOutsideTheCalibrationsAndIsExactAtThem) {
    for (double below : {2856.0, 2855.9, 2000.0, 1.0}) {
        EXPECT_EQ(prism::dng_interpolation_weight(below, 2856, 6504), 1.0) << below;
        EXPECT_EQ(prism::dng_interpolation_weight(below, 6504, 2856), 0.0) << below;
    }
    for (double above : {6504.0, 6504.1, 10000.0, 1e9}) {
        EXPECT_EQ(prism::dng_interpolation_weight(above, 2856, 6504), 0.0) << above;
        EXPECT_EQ(prism::dng_interpolation_weight(above, 6504, 2856), 1.0) << above;
    }
}

TEST(DngInterpolation, WeightRejectsBadTemperatures) {
    for (double bad : {0.0, -4000.0, std::nan(""), HUGE_VAL}) {
        EXPECT_THROW((void)prism::dng_interpolation_weight(bad, 2856, 6504), std::invalid_argument);
        EXPECT_THROW((void)prism::dng_interpolation_weight(4000, bad, 6504), std::invalid_argument);
        EXPECT_THROW((void)prism::dng_interpolation_weight(4000, 2856, bad), std::invalid_argument);
    }
    EXPECT_THROW((void)prism::dng_interpolation_weight(4000, 5500, 5500), std::invalid_argument);
}

TEST(DngInterpolation, InterpolatesForwardMatrices) {
    const prism::Mat3 expected =
        prism::camera_to_srgb_from_forward_matrix(blend(forward_a, forward_d65, weight_a_at_4000));
    const prism::Mat3 m = prism::camera_to_srgb_from_dng(forward_pair(), 4000);
    expect_near(m, expected, 1e-6);
    expect_rows_sum_to_one(m);
    expect_near(prism::camera_to_srgb_from_dng(swapped(forward_pair()), 4000), m, 1e-12);
}

TEST(DngInterpolation, InterpolatesColorMatricesNotTheResults) {
    const prism::Mat3 expected = prism::camera_to_srgb_from_color_matrix(
        blend(color_a, sample_color_matrix, weight_a_at_4000));
    const prism::Mat3 m = prism::camera_to_srgb_from_dng(color_pair(), 4000);
    expect_near(m, expected, 1e-6);
    expect_rows_sum_to_one(m);
    expect_near(prism::camera_to_srgb_from_dng(swapped(color_pair()), 4000), m, 1e-12);

    // Blending the two final sRGB matrices instead gives something measurably different, because
    // the ColorMatrix is inverted on the way. (For ForwardMatrices the two would agree: every
    // ForwardMatrix maps (1, 1, 1) to D50 white, so normalize_rows scales them all alike.)
    const prism::Mat3 blended_results =
        blend(prism::camera_to_srgb_from_color_matrix(color_a),
              prism::camera_to_srgb_from_color_matrix(sample_color_matrix), weight_a_at_4000);
    double largest_difference = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            largest_difference =
                std::max(largest_difference, std::abs(m[i][j] - blended_results[i][j]));
        }
    }
    EXPECT_GT(largest_difference, 1e-3);
}

TEST(DngInterpolation, ClampsToTheCloserCalibration) {
    for (const auto& cal : {color_pair(), swapped(color_pair())}) {
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 2000),
                  prism::camera_to_srgb_from_color_matrix(color_a));
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 12000),
                  prism::camera_to_srgb_from_color_matrix(sample_color_matrix));
    }
    for (const auto& cal : {forward_pair(), swapped(forward_pair())}) {
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 2000),
                  prism::camera_to_srgb_from_forward_matrix(forward_a));
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 12000),
                  prism::camera_to_srgb_from_forward_matrix(forward_d65));
    }
}

TEST(DngInterpolation, EndpointsGiveExactlyThatCalibration) {
    for (const auto& cal : {color_pair(), swapped(color_pair())}) {
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 2856),
                  prism::camera_to_srgb_from_color_matrix(color_a));
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 6504),
                  prism::camera_to_srgb_from_color_matrix(sample_color_matrix));
    }
    for (const auto& cal : {forward_pair(), swapped(forward_pair())}) {
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 2856),
                  prism::camera_to_srgb_from_forward_matrix(forward_a));
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 6504),
                  prism::camera_to_srgb_from_forward_matrix(forward_d65));
    }
    // The D65 default matches the one-argument version for a Standard A + D65 profile.
    EXPECT_EQ(prism::camera_to_srgb_from_dng(color_pair(), prism::dng_default_kelvin),
              prism::camera_to_srgb_from_dng(color_pair()));
}

TEST(DngInterpolation, SrgbCameraGivesIdentityBetweenCalibrations) {
    // As in ColorMatrixOfAnSrgbCameraGivesIdentity, with different channel gains per calibration.
    // Blending two gain-scaled xyz_to_srgb matrices only blends the gains, which white balance
    // removes, so the result is the identity at any temperature.
    prism::Mat3 warm = prism::xyz_to_srgb();
    prism::Mat3 cool = prism::xyz_to_srgb();
    for (int j = 0; j < 3; ++j) {
        warm[0][j] *= 0.5;
        warm[2][j] *= 2.0;
        cool[0][j] *= 1.5;
        cool[1][j] *= 0.8;
    }
    const std::array<prism::DngCalibration, 2> cal = {
        {{light_a, warm, std::nullopt}, {light_d65, cool, std::nullopt}}};
    expect_near(prism::camera_to_srgb_from_dng(cal, 4000), prism::identity3, 1e-12);
}

TEST(DngInterpolation, RejectsAForwardMatrixOnOnlyOneCalibration) {
    std::array<prism::DngCalibration, 2> cal = color_pair();
    cal[0].forward_matrix = forward_a;
    // Rejected at every temperature, including those that would use one calibration alone.
    for (double k : {2000.0, 2856.0, 4000.0, 6504.0, 12000.0}) {
        expect_dng_error(cal, k, "ForwardMatrix for only one of its two calibrations");
        expect_dng_error(swapped(cal), k, "ForwardMatrix for only one of its two calibrations");
    }
}

TEST(DngInterpolation, RejectsIlluminantsWithoutATemperature) {
    for (double k : {2000.0, 4000.0, 6504.0}) {
        std::array<prism::DngCalibration, 2> cal = color_pair();
        cal[0].illuminant = 0;
        expect_dng_error(cal, k, "calibration 1 has illuminant 0 (unknown)");
        cal[0].illuminant = 255;
        expect_dng_error(cal, k, "calibration 1 has illuminant 255 (other");
        cal[0].illuminant = light_a;
        cal[1].illuminant = 255;
        expect_dng_error(cal, k, "calibration 2 has illuminant 255 (other");
        cal[1].illuminant = 7;  // not a defined LightSource value
        expect_dng_error(cal, k, "calibration 2 has illuminant 7, which has no known colour");
    }
    std::array<prism::DngCalibration, 2> same = color_pair();
    same[0].illuminant = 1;  // daylight, 5500 K
    same[1].illuminant = 4;  // flash, 5500 K
    expect_dng_error(same, 4000, "illuminants 1 and 4, both 5500 K");
}

TEST(DngInterpolation, UsesASingleCalibrationAtAnyTemperature) {
    // No temperature is needed, so even an unknown illuminant is fine.
    for (int illuminant : {0, 255, light_a}) {
        std::array<prism::DngCalibration, 2> cal{};
        cal[1] = {illuminant, color_a, std::nullopt};
        for (double k : {2000.0, 4000.0, 9000.0}) {
            EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, k),
                      prism::camera_to_srgb_from_color_matrix(color_a));
        }
        cal[1].forward_matrix = forward_a;
        EXPECT_EQ(prism::camera_to_srgb_from_dng(cal, 4000),
                  prism::camera_to_srgb_from_forward_matrix(forward_a));
    }
    EXPECT_THROW((void)prism::camera_to_srgb_from_dng({}, 4000), std::invalid_argument);
    for (double bad : {0.0, -1.0, std::nan(""), HUGE_VAL}) {
        EXPECT_THROW((void)prism::camera_to_srgb_from_dng(color_pair(), bad),
                     std::invalid_argument);
    }
}

// --- LightCraft profiles -------------------------------------------------------------------------

TEST(LightCraftProfile, ParsesFields) {
    const prism::LightCraftProfile p = prism::parse_lightcraft_profile(
        profile_json(sample_matrix, R"(, "hue_sat": {"hue_divisions": 4, "sat_divisions": 2,
            "val_divisions": 1, "data": [[5.0, 1.2, 1.0]], "srgb_value": false})"));
    EXPECT_EQ(p.version, 1);
    EXPECT_EQ(p.model, "TEST-CAM");
    EXPECT_EQ(p.files, 12);
    EXPECT_EQ(p.samples, 3456);
    EXPECT_DOUBLE_EQ(p.matrix[0][0], 1.2);
    EXPECT_DOUBLE_EQ(p.matrix[2][1], -0.8);
    EXPECT_TRUE(p.has_hue_sat);
    EXPECT_EQ(p.hue_divisions, 4);
    EXPECT_EQ(p.sat_divisions, 2);
    EXPECT_EQ(p.val_divisions, 1);

    const prism::LightCraftProfile none =
        prism::parse_lightcraft_profile(profile_json(sample_matrix, R"(, "hue_sat": null)"));
    EXPECT_FALSE(none.has_hue_sat);
}

TEST(LightCraftProfile, HandlesJsonDetails) {
    // Escapes, exponents, whitespace and key order.
    const prism::LightCraftProfile p = prism::parse_lightcraft_profile(
        "\n{ \"matrix\" : [[1.2e0,-3E-1,0.1],[-0.1,1.2,-0.1],[0,-0.8,1.8]],\r\n"
        "  \"model\": \"A\\u00e9\\\"B\", \"samples\": 1, \"files\": 2, \"version\": 1 }\n");
    EXPECT_EQ(p.model, "A\xC3\xA9\"B");
    EXPECT_DOUBLE_EQ(p.matrix[0][1], -0.3);
}

TEST(LightCraftProfile, RejectsBadInput) {
    const auto bad = [](const std::string& json) {
        EXPECT_THROW((void)prism::parse_lightcraft_profile(json), std::runtime_error) << json;
    };
    bad("");
    bad("{not json");
    bad("[1, 2, 3]");
    bad(profile_json(sample_matrix) + " trailing");
    bad(profile_json("[[1, 0, 0], [0, 1, 0]]"));
    bad(profile_json("[[1, 0, 0], [0, 1, 0], [0, 0]]"));
    bad(profile_json("[[1, 0, 0], [0, 1, 0], [0, 0, \"1\"]]"));
    bad(profile_json("[[1, 0, 0], [0, 1, 0], [0, 0, 1e999]]"));
    bad(R"({"version": 2, "model": "X", "files": 1, "samples": 1, "matrix": )" + sample_matrix +
        "}");
    bad(R"({"version": 1, "model": "", "files": 1, "samples": 1, "matrix": )" + sample_matrix +
        "}");
    bad(R"({"version": 1, "model": "X", "files": -1, "samples": 1, "matrix": )" + sample_matrix +
        "}");
    bad(std::string(1000, '['));  // too deeply nested, without overflowing the stack
}

TEST(LightCraftProfile, LoadReportsMissingFile) {
    EXPECT_THROW((void)prism::load_lightcraft_profile(
                     (fs::path(testing::TempDir()) / "prism_no_such_profile.json").string()),
                 std::runtime_error);
}

// --- prism_profile -------------------------------------------------------------------------------

TEST(PrismProfile, WritesAMatrixThatLoadMatrixReads) {
    const fs::path dir = fs::path(testing::TempDir());
    const fs::path json = dir / "prism_profile_test.json";
    const fs::path out = dir / "prism_profile_test.txt";
    std::ofstream(json) << profile_json(sample_matrix, R"(, "hue_sat": {"hue_divisions": 72,
        "sat_divisions": 5, "val_divisions": 5, "data": []})");

    const std::string command = "\"" + std::string(PRISM_PROFILE_EXE) + "\" \"" + json.string() +
                                "\" -o \"" + out.string() + "\" --note \"from a test\" > \"" +
                                (dir / "prism_profile_log.txt").string() + "\" 2>&1";
    const int status = std::system(command.c_str());
#ifndef _WIN32
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#else
    ASSERT_EQ(status, 0);
#endif

    std::ifstream file(out);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("TEST-CAM"), std::string::npos);
    EXPECT_NE(text.find("#   " + json.filename().string() + "\n"), std::string::npos)
        << "should say where it came from";
    EXPECT_EQ(text.find(json.parent_path().string()), std::string::npos) << "but not its directory";
    EXPECT_NE(text.find("not a colorimetric calibration"), std::string::npos);
    EXPECT_NE(text.find("#   from a test\n"), std::string::npos);
    EXPECT_NE(text.find("Skipped: the profile's hue/saturation/value table (72x5x5)"),
              std::string::npos);

    const prism::Matrix3 loaded = prism::load_matrix(out);
    const prism::Matrix3 expected = prism::to_matrix3(prism::camera_to_srgb_from_rec2020(
        prism::parse_lightcraft_profile(profile_json(sample_matrix)).matrix));
    for (std::size_t i = 0; i < loaded.size(); ++i) {
        EXPECT_NEAR(loaded[i], expected[i], 1e-6f) << i;
    }
    for (int row = 0; row < 3; ++row) {
        EXPECT_NEAR(loaded[row * 3] + loaded[row * 3 + 1] + loaded[row * 3 + 2], 1.0f, 1e-5f);
    }
    fs::remove(json);
    fs::remove(out);
}

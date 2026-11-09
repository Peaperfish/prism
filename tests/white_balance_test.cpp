#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "prism/camera_profile.hpp"
#include "prism/pipeline.hpp"
#include "prism/white_balance.hpp"

namespace {

using prism::Chromaticity;
using prism::Uv;
using prism::WhiteBalance;

// A grey pixel (linear sRGB) through a Matrix3.
std::array<double, 3> through(const prism::Matrix3& m, double v = 0.18) {
    return {m[0] * v + m[1] * v + m[2] * v, m[3] * v + m[4] * v + m[5] * v,
            m[6] * v + m[7] * v + m[8] * v};
}

// Whether a and b are proportional, within a relative tolerance.
void expect_proportional(const prism::Vec3& a, const prism::Vec3& b, double tolerance) {
    const double k = b[1] / a[1];
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(a[i] * k, b[i], tolerance * std::fabs(b[i])) << "channel " << i;
    }
}

}  // namespace

TEST(WhiteBalance, KrystekMatchesTheColourScienceValueAt6504K) {
    // The value the colour-science library gives for this formula.
    const Uv uv = prism::planckian_uv(6504.38938305);
    EXPECT_NEAR(uv.u, 0.2004720, 1e-7);
    EXPECT_NEAR(uv.v, 0.3102929, 1e-7);
}

TEST(WhiteBalance, UvAndXyRoundTrip) {
    for (const Chromaticity xy : {Chromaticity{0.3127, 0.3290}, {0.4476, 0.4074}, {0.25, 0.28}}) {
        const Chromaticity back = prism::xy_of(prism::uv_of(xy));
        EXPECT_NEAR(back.x, xy.x, 1e-12);
        EXPECT_NEAR(back.y, xy.y, 1e-12);
    }
    const Uv uv{0.2, 0.31};
    const Uv back = prism::uv_of(prism::xy_of(uv));
    EXPECT_NEAR(back.u, uv.u, 1e-12);
    EXPECT_NEAR(back.v, uv.v, 1e-12);
}

TEST(WhiteBalance, TheNormalIsUnitPerpendicularToTheTangentAndPointsToLargerV) {
    for (double k = 1500; k <= 15000; k += 500) {
        const Uv n = prism::locus_normal(k);
        const Uv t = prism::planckian_uv_slope(k);
        EXPECT_NEAR(std::hypot(n.u, n.v), 1.0, 1e-12) << k;
        EXPECT_NEAR(n.u * t.u + n.v * t.v, 0.0, 1e-12 * std::hypot(t.u, t.v)) << k;
        EXPECT_GT(n.v, 0.0) << k;
    }
}

TEST(WhiteBalance, TheSlopeIsTheDerivativeOfTheLocus) {
    const double k = 5000;
    const double h = 1e-3;
    const Uv a = prism::planckian_uv(k - h);
    const Uv b = prism::planckian_uv(k + h);
    const Uv s = prism::planckian_uv_slope(k);
    EXPECT_NEAR(s.u, (b.u - a.u) / (2 * h), 1e-10);
    EXPECT_NEAR(s.v, (b.v - a.v) / (2 * h), 1e-10);
}

TEST(WhiteBalance, DuvOfAWhiteMeasuredBackIsTintTimesTheUnit) {
    for (const double k : {2000.0, 3200.0, 5000.0, 6500.0, 9000.0, 15000.0}) {
        for (const double tint : {-100.0, -37.5, 0.0, 12.0, 100.0}) {
            const Uv uv = prism::uv_of(prism::white_of({k, tint}));
            const Uv locus = prism::planckian_uv(k);
            const Uv n = prism::locus_normal(k);
            const double duv = (uv.u - locus.u) * n.u + (uv.v - locus.v) * n.v;
            EXPECT_NEAR(duv, tint * 0.0002, 1e-9) << k << " K, tint " << tint;
        }
    }
}

TEST(WhiteBalance, TheInverseRoundTripsOnTheGrid) {
    for (double k = 2000; k <= 15000; k += 250) {
        for (double tint = -100; tint <= 100; tint += 25) {
            const WhiteBalance back = prism::white_balance_of(prism::white_of({k, tint}));
            EXPECT_NEAR(back.kelvin, k, 0.5) << k << " K, tint " << tint;
            EXPECT_NEAR(back.tint, tint, 0.01) << k << " K, tint " << tint;
        }
    }
}

TEST(WhiteBalance, AHigherKelvinIsWarmerAndALowerOneCooler) {
    const Chromaticity as_shot = prism::white_of(prism::default_as_shot);
    const auto warm = through(prism::white_balance_matrix({8000, 0}, as_shot));
    EXPECT_GT(warm[0] / warm[2], 1.0);
    const auto cool = through(prism::white_balance_matrix({4000, 0}, as_shot));
    EXPECT_LT(cool[0] / cool[2], 1.0);
}

TEST(WhiteBalance, APositiveTintMakesTheImageMoreMagenta) {
    const Chromaticity as_shot = prism::white_of(prism::default_as_shot);
    const auto magenta = through(prism::white_balance_matrix({6500, 50}, as_shot));
    EXPECT_LT(magenta[1], 0.5 * (magenta[0] + magenta[2]));
    const auto green = through(prism::white_balance_matrix({6500, -50}, as_shot));
    EXPECT_GT(green[1], 0.5 * (green[0] + green[2]));
}

TEST(WhiteBalance, BradfordTakesTheChosenWhiteToTheAsShotWhite) {
    // The argument order bradford(from, to) needs: from = the chosen light, to = as shot.
    const Chromaticity as_shot = prism::white_of({6500, 0});
    for (const WhiteBalance chosen : {WhiteBalance{3000, 20}, {8000, -60}, {15000, 100}}) {
        const Chromaticity light = prism::white_of(chosen);
        const prism::Vec3 mapped =
            prism::multiply(prism::bradford(light, as_shot), prism::xyz_of(light));
        expect_proportional(mapped, prism::xyz_of(as_shot), 1e-9);
    }
}

TEST(WhiteBalance, TheMatrixMapsTheChosenWhiteToTheAsShotWhiteInSrgb) {
    const Chromaticity as_shot = prism::white_of({5200, -15});
    const WhiteBalance chosen{7300, 30};
    const prism::Matrix3 m = prism::white_balance_matrix(chosen, as_shot);
    const prism::Vec3 rgb =
        prism::multiply(prism::xyz_to_srgb(), prism::xyz_of(prism::white_of(chosen)));
    const prism::Vec3 out = {m[0] * rgb[0] + m[1] * rgb[1] + m[2] * rgb[2],
                             m[3] * rgb[0] + m[4] * rgb[1] + m[5] * rgb[2],
                             m[6] * rgb[0] + m[7] * rgb[1] + m[8] * rgb[2]};
    expect_proportional(out, prism::multiply(prism::xyz_to_srgb(), prism::xyz_of(as_shot)), 1e-5);
}

TEST(WhiteBalance, ChoosingTheAsShotLightIsTheIdentity) {
    const WhiteBalance wb{4321, -17};
    const prism::Matrix3 m = prism::white_balance_matrix(wb, prism::white_of(wb));
    for (std::size_t i = 0; i < 9; ++i) {
        EXPECT_NEAR(m[i], prism::identity_matrix[i], 1e-12);
    }
    // And the underlying adaptation, before the exact shortcut and the rounding to float.
    const Chromaticity w = prism::white_of(wb);
    const prism::Mat3 b = prism::bradford(w, w);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_NEAR(b[i][j], prism::identity3[i][j], 1e-12);
        }
    }
}

// --- The as-shot white of a RAW file, from its multipliers and colour matrix --------------------

namespace {

// A plausible XYZ -> camera matrix (rows: camera red, green, blue).
const prism::Mat3 camera_xyz = {{{0.9, -0.3, -0.1}, {-0.4, 1.2, 0.1}, {0.0, 0.1, 0.7}}};

}  // namespace

TEST(AsShotWhite, RecoversTheLightTheMultipliersWereMadeFor) {
    for (const WhiteBalance wb : {WhiteBalance{2800, 5}, {5500, -10}, {7500, 20}}) {
        const Chromaticity light = prism::white_of(wb);
        const prism::Vec3 camera = prism::multiply(camera_xyz, prism::xyz_of(light));
        // Multipliers scale each channel so the light comes out neutral; only ratios matter.
        const std::array<double, 3> mul = {2.0 / camera[0], 2.0 / camera[1], 2.0 / camera[2]};
        const auto white = prism::as_shot_white(mul, camera_xyz);
        ASSERT_TRUE(white);
        EXPECT_NEAR(white->x, light.x, 1e-12);
        EXPECT_NEAR(white->y, light.y, 1e-12);
    }
}

TEST(AsShotWhite, HasNoWhiteWithoutUsableNumbers) {
    const std::array<double, 3> good = {2.0, 1.0, 1.5};
    EXPECT_TRUE(prism::as_shot_white(good, camera_xyz));
    EXPECT_FALSE(prism::as_shot_white({0.0, 1.0, 1.5}, camera_xyz));
    EXPECT_FALSE(prism::as_shot_white({2.0, -1.0, 1.5}, camera_xyz));
    EXPECT_FALSE(prism::as_shot_white({2.0, 1.0, std::nan("")}, camera_xyz));
    EXPECT_FALSE(prism::as_shot_white(good, prism::Mat3{}));  // all zero
    const prism::Mat3 singular = {{{1, 2, 3}, {2, 4, 6}, {0, 1, 1}}};
    EXPECT_FALSE(prism::as_shot_white(good, singular));
}

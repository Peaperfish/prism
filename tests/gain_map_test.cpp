#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include "prism/gain_map.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"

// #103: gain map maths. Expected values are literals: the gain map numbers from a separate
// Python calculation (sRGB decode formula, Rec. 709 weights, offsets 1/64), the fractions from
// libultrahdr 2.0.2 itself (floatToSignedFraction / floatToUnsignedFraction, linked from
// Homebrew's libuhdr.a in a probe outside the repo).

namespace {

// Four pixels as base (8-bit sRGB) and HDR (linear):
//   black            -> 0                 gain 1
//   white (255)      -> 4                 white on H = 4
//   grey (128)       -> 1.149             a mid-tone lifted by the HDR curve
//   red (255, 0, 0)  -> (4, 0.5, 0.5)     saturated: its luminance gain would lift red past H
const std::vector<std::uint8_t> base = {0, 0, 0, 255, 255, 255, 128, 128, 128, 255, 0, 0};

prism::Image four_hdr() {
    return {4, 1, {0, 0, 0, 4, 4, 4, 1.149f, 1.149f, 1.149f, 4, 0.5f, 0.5f}};
}

std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out;
    out.reserve(img.pixels.size());
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

}  // namespace

// --- making the map ------------------------------------------------------------------------------

TEST(GainMap, FourPixelsWithThePerPixelLimit) {
    const prism::GainMap map = prism::make_gain_map(base, four_hdr(), 2.0f);
    EXPECT_EQ(map.width, 4);
    EXPECT_EQ(map.height, 1);
    // Gains (log2): 0, 1.983257, 2.330872, and red limited to white's 1.983257.
    EXPECT_EQ(map.codes, (std::vector<std::uint8_t>{0, 216, 255, 216}));
    EXPECT_NEAR(map.metadata.min_log2, 0.0, 1e-6);
    EXPECT_NEAR(map.metadata.max_log2, 2.330872, 2e-5);
    EXPECT_EQ(map.metadata.gamma, 1.0f);
    EXPECT_EQ(map.metadata.offset_sdr, 1.0f / 64.0f);
    EXPECT_EQ(map.metadata.offset_hdr, 1.0f / 64.0f);
    EXPECT_EQ(map.metadata.hdr_capacity_min, 0.0f);
    EXPECT_EQ(map.metadata.hdr_capacity_max, 2.0f);  // N, not GainMapMax (#103, #96)
}

TEST(GainMap, FourPixelsWithoutTheLimit) {
    prism::GainMapOptions options;
    options.keep_channels_within_headroom = false;
    const prism::GainMap map = prism::make_gain_map(base, four_hdr(), 2.0f, options);
    // Red's own gain, 2.464580 stops, is now the largest.
    EXPECT_EQ(map.codes, (std::vector<std::uint8_t>{0, 205, 241, 255}));
    EXPECT_NEAR(map.metadata.max_log2, 2.464580, 2e-5);
    EXPECT_EQ(map.metadata.hdr_capacity_max, 2.0f);
}

TEST(GainMap, OneGainEverywhereGivesCodeZero) {
    const std::vector<std::uint8_t> greys = {0, 0, 0, 0, 0, 0};
    const prism::GainMap map =
        prism::make_gain_map(greys, prism::Image{2, 1, std::vector<float>(6, 0.0f)}, 1.0f);
    EXPECT_EQ(map.codes, (std::vector<std::uint8_t>{0, 0}));
    EXPECT_EQ(map.metadata.min_log2, 0.0f);
    EXPECT_EQ(map.metadata.max_log2, 0.0f);
}

TEST(GainMap, NegativeLightCountsAsNone) {
    // A negative HDR luminance (possible with no tone curve after a matrix) is a gain below 1, not
    // a NaN: Y = 0 gives (0 + 1/64) / (Y_sdr + 1/64).
    const std::vector<std::uint8_t> one = {128, 128, 128, 0, 0, 0};
    const prism::GainMap map =
        prism::make_gain_map(one, prism::Image{2, 1, {-0.5f, -0.5f, -0.5f, 0, 0, 0}}, 2.0f);
    // log2((0 + 1/64) / (0.2158605 + 1/64)) = -3.888994
    EXPECT_NEAR(map.metadata.min_log2, -3.888994, 2e-5);
    EXPECT_EQ(map.metadata.max_log2, 0.0f);
    EXPECT_EQ(map.codes, (std::vector<std::uint8_t>{0, 255}));
}

TEST(GainMap, RoundTripIsWithinOneCode) {
    // A spread of bases and HDR values: every decoded gain is within one code step of the pixel's
    // own gain (half a step when rounding, a whole step when the limit made it round down).
    prism::Image hdr{64, 48, {}};
    std::vector<std::uint8_t> b;
    for (int i = 0; i < 64 * 48; ++i) {
        const auto code = static_cast<std::uint8_t>((i * 37) % 256);
        const auto other = static_cast<std::uint8_t>((i * 11) % 256);
        b.insert(b.end(), {code, other, code});
        const float lift = 1.0f + static_cast<float>(i % 97) / 24.0f;  // 1 to 5
        for (const float c : {0.3f, 0.7f, 0.5f}) {
            hdr.pixels.push_back(std::min(c * lift, 4.0f));
        }
    }
    for (const bool limit : {true, false}) {
        prism::GainMapOptions options;
        options.keep_channels_within_headroom = limit;
        const prism::GainMap map = prism::make_gain_map(b, hdr, 2.0f, options);
        const prism::Image sdr = prism::decode_srgb8(b, 64, 48, 1);
        const double step = (map.metadata.max_log2 - map.metadata.min_log2) / 255.0;
        ASSERT_GT(step, 0.0);
        for (std::size_t i = 0; i < map.codes.size(); ++i) {
            const float* s = &sdr.pixels[i * 3];
            const float* h = &hdr.pixels[i * 3];
            const double o = 1.0 / 64.0;
            double g =
                std::log2((std::max(0.2126 * h[0] + 0.7152 * h[1] + 0.0722 * h[2], 0.0) + o) /
                          (std::max(0.2126 * s[0] + 0.7152 * s[1] + 0.0722 * s[2], 0.0) + o));
            if (limit) {
                g = std::min(g, std::log2((4.0 + o) / (std::max({s[0], s[1], s[2], 0.0f}) + o)));
            }
            const double back = prism::gain_map_log2(map.codes[i], map.metadata);
            ASSERT_LE(std::fabs(back - g), step * (limit ? 1.0 : 0.5) + 1e-5) << i;
        }
    }
}

TEST(GainMap, BadInputIsRefused) {
    EXPECT_THROW((void)prism::make_gain_map(std::vector<std::uint8_t>(9), four_hdr(), 2.0f),
                 std::invalid_argument);
    for (const float n : {0.0f, -1.0f, 4.5f, std::numeric_limits<float>::quiet_NaN()}) {
        EXPECT_THROW((void)prism::make_gain_map(base, four_hdr(), n), std::invalid_argument) << n;
    }
    prism::Image broken = four_hdr();
    broken.pixels.pop_back();
    EXPECT_THROW((void)prism::make_gain_map(base, broken, 2.0f), std::invalid_argument);
}

// --- applying it
// ----------------------------------------------------------------------------------

TEST(GainMap, ApplyAtFullWeightKeepsEveryChannelWithinHeadroom) {
    const prism::GainMap map = prism::make_gain_map(base, four_hdr(), 2.0f);
    const prism::Image out = prism::apply_gain_map(base, map, 1.0f);
    const float expected[] = {0,        0,        0,        3.97538f, 3.97538f, 3.97538f,
                              1.14900f, 1.14900f, 1.14900f, 3.97538f, 0.04578f, 0.04578f};
    ASSERT_EQ(out.pixels.size(), std::size(expected));
    for (std::size_t i = 0; i < out.pixels.size(); ++i) {
        EXPECT_NEAR(out.pixels[i], expected[i], 1e-4f) << i;
        EXPECT_LE(out.pixels[i], 4.0f) << i;
    }
}

TEST(GainMap, WithoutTheLimitASaturatedRedPassesHeadroom) {
    prism::GainMapOptions options;
    options.keep_channels_within_headroom = false;
    const prism::GainMap map = prism::make_gain_map(base, four_hdr(), 2.0f, options);
    const prism::Image out = prism::apply_gain_map(base, map, 1.0f);
    EXPECT_NEAR(out.pixels[9], 5.59028f, 1e-4f);  // red: 1.4 stops-worth past H = 4
    EXPECT_NEAR(out.pixels[3], 3.99464f, 1e-4f);
    EXPECT_NEAR(out.pixels[6], 1.14771f, 1e-4f);
}

TEST(GainMap, ApplyAtHalfWeight) {
    const prism::GainMap map = prism::make_gain_map(base, four_hdr(), 2.0f);
    const prism::Image out = prism::apply_gain_map(base, map, 0.5f);
    EXPECT_NEAR(out.pixels[3], 1.99767f, 1e-4f);
    EXPECT_NEAR(out.pixels[6], 0.50360f, 1e-4f);
    EXPECT_NEAR(out.pixels[9], 1.99767f, 1e-4f);
    EXPECT_NEAR(out.pixels[10], 0.01535f, 1e-4f);
}

TEST(GainMap, WeightZeroIsTheDecodedBaseExactly) {
    const prism::GainMap map = prism::make_gain_map(base, four_hdr(), 2.0f);
    EXPECT_EQ(bits(prism::apply_gain_map(base, map, 0.0f)), bits(prism::decode_srgb8(base, 4, 1)));
    // Every 8-bit level: (c + 1/64) * 1 - 1/64 is not c for all of them, so weight 0 must not go
    // through the formula.
    std::vector<std::uint8_t> levels;
    prism::Image hdr{256, 1, {}};
    for (int c = 0; c < 256; ++c) {
        levels.insert(levels.end(), 3, static_cast<std::uint8_t>(c));
        hdr.pixels.insert(hdr.pixels.end(), 3, 2.0f);
    }
    const prism::GainMap ramp = prism::make_gain_map(levels, hdr, 2.0f);
    EXPECT_EQ(bits(prism::apply_gain_map(levels, ramp, 0.0f)),
              bits(prism::decode_srgb8(levels, 256, 1)));
}

TEST(GainMap, ApplyRefusesMismatchedSizes) {
    prism::GainMap map = prism::make_gain_map(base, four_hdr(), 2.0f);
    EXPECT_THROW((void)prism::apply_gain_map(std::vector<std::uint8_t>(9), map, 1.0f),
                 std::invalid_argument);
    map.codes.pop_back();
    EXPECT_THROW((void)prism::apply_gain_map(base, map, 1.0f), std::invalid_argument);
}

// --- the viewer's weight
// --------------------------------------------------------------------------

TEST(GainMap, ViewerWeightFromDisplayHeadroom) {
    prism::GainMapMetadata m;
    m.hdr_capacity_max = 2.0f;
    // Design report: with N = 2, a display headroom of 1, 2, 4 or 16 gives 0, 0.5, 1, 1.
    EXPECT_EQ(prism::gain_map_weight(1.0f, m), 0.0f);
    EXPECT_EQ(prism::gain_map_weight(2.0f, m), 0.5f);
    EXPECT_EQ(prism::gain_map_weight(4.0f, m), 1.0f);
    EXPECT_EQ(prism::gain_map_weight(16.0f, m), 1.0f);
    EXPECT_NEAR(prism::gain_map_weight(1.2f, m), 0.131517, 1e-6);  // the XDR screen at 1.2x
    for (const float none : {0.5f, 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()}) {
        EXPECT_EQ(prism::gain_map_weight(none, m), 0.0f) << none;
    }
    m.hdr_capacity_max = 4.0f;
    EXPECT_EQ(prism::gain_map_weight(4.0f, m), 0.5f);
    m.hdr_capacity_max = 0.0f;  // no span: all or nothing
    EXPECT_EQ(prism::gain_map_weight(1.0f, m), 1.0f);
    EXPECT_EQ(prism::gain_map_weight(0.5f, m), 0.0f);
}

// --- threads
// --------------------------------------------------------------------------------------

TEST(GainMap, OneAndEightThreadsGiveTheSameBits) {
    // At least 1,572,864 px, and 1213 rows: 8 does not divide them.
    const int width = 1301;
    const int height = 1213;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    ASSERT_GE(pixels, 1572864u);
    ASSERT_NE(height % 8, 0);
    ASSERT_EQ(prism::threads_for_pixels(pixels, 8), 8);
    std::vector<std::uint8_t> b(pixels * 3);
    prism::Image hdr{width, height, std::vector<float>(pixels * 3)};
    for (std::size_t i = 0; i < b.size(); ++i) {
        b[i] = static_cast<std::uint8_t>((i * 7 + i / 3001) % 256);
        hdr.pixels[i] = static_cast<float>((i * 13) % 1000) / 250.0f;  // 0 to 4
    }
    const prism::GainMap one = prism::make_gain_map(b, hdr, 2.0f, {}, 1);
    const prism::GainMap eight = prism::make_gain_map(b, hdr, 2.0f, {}, 8);
    EXPECT_EQ(one.metadata, eight.metadata);
    EXPECT_EQ(one.codes, eight.codes);
    // Every code is used, so a chunk that skipped pixels would show.
    EXPECT_EQ(std::count(one.codes.begin(), one.codes.end(), 0),
              std::count(eight.codes.begin(), eight.codes.end(), 0));
    EXPECT_EQ(bits(prism::apply_gain_map(b, one, 0.7f, 1)),
              bits(prism::apply_gain_map(b, one, 0.7f, 8)));
}

// --- ISO 21496-1 fractions
// ------------------------------------------------------------------------

TEST(GainMapFractions, MatchLibUltraHdr) {
    struct Signed {
        float v;
        std::int32_t n;
        std::uint32_t d;
    };
    for (const Signed& c :
         {Signed{0.015625f, 1, 64}, Signed{2.0f, 2, 1}, Signed{0.5f, 1, 2}, Signed{0.0f, 0, 1},
          Signed{-0.0f, 0, 1}, Signed{1.0f, 1, 1}, Signed{2.5f, 5, 2}, Signed{-1.0f, -1, 1},
          Signed{-0.0112f, -3006477, 268435456}, Signed{2.4703f, 10361189, 4194304},
          Signed{3.14159265f, 13176795, 4194304}, Signed{1.6180339887f, 13573053, 8388608},
          Signed{1.98326528f, 16636835, 8388608}, Signed{2.330872f, 4888193, 2097152},
          Signed{0.3333333f, 5592405, 16777216}, Signed{0.1f, 13421773, 134217728},
          Signed{1e-7f, 77, 769999991}, Signed{1e-10f, 0, 1}}) {
        const std::optional<prism::SignedFraction> f = prism::signed_fraction(c.v);
        ASSERT_TRUE(f) << c.v;
        EXPECT_EQ(f->numerator, c.n) << c.v;
        EXPECT_EQ(f->denominator, c.d) << c.v;
    }
    EXPECT_EQ(prism::unsigned_fraction(3e9f), (prism::UnsignedFraction{3000000000u, 1}));
    EXPECT_EQ(prism::unsigned_fraction(2147483647.0f), (prism::UnsignedFraction{2147483648u, 1}));
    EXPECT_EQ(prism::unsigned_fraction(1.5f), (prism::UnsignedFraction{3, 2}));
}

TEST(GainMapFractions, WhatCannotBeAFraction) {
    // libultrahdr refuses these too.
    for (const float v : {3e9f, 2147483647.0f, 5e9f, std::numeric_limits<float>::quiet_NaN()}) {
        EXPECT_FALSE(prism::signed_fraction(v)) << v;
    }
    for (const float v :
         {-1.0f, -0.0112f, 4294967295.0f, 5e9f, std::numeric_limits<float>::quiet_NaN()}) {
        EXPECT_FALSE(prism::unsigned_fraction(v)) << v;
    }
}

TEST(GainMapFractions, TheMetadataAsIsoFractions) {
    prism::GainMapMetadata m;
    m.min_log2 = -0.0112f;
    m.max_log2 = 2.4703f;
    m.hdr_capacity_max = 2.0f;
    const std::optional<prism::IsoGainMapFractions> f = prism::iso_fractions(m);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->gain_map_min, (prism::SignedFraction{-3006477, 268435456}));
    EXPECT_EQ(f->gain_map_max, (prism::SignedFraction{10361189, 4194304}));
    EXPECT_EQ(f->gamma, (prism::UnsignedFraction{1, 1}));
    EXPECT_EQ(f->base_offset, (prism::SignedFraction{1, 64}));
    EXPECT_EQ(f->alternate_offset, (prism::SignedFraction{1, 64}));
    EXPECT_EQ(f->base_hdr_headroom, (prism::UnsignedFraction{0, 1}));
    EXPECT_EQ(f->alternate_hdr_headroom, (prism::UnsignedFraction{2, 1}));
    m.min_log2 = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(prism::iso_fractions(m));
}

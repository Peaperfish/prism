#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "prism/color.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/histogram.hpp"
#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"

using prism::gui::ChannelClips;
using prism::gui::StageHistogram;
using prism::gui::StageId;

namespace {

std::uint64_t sum_bins(const StageHistogram::Counts& counts) {
    return std::accumulate(counts.begin(), counts.end(), std::uint64_t{0});
}

}  // namespace

TEST(StageHistogram, FlatGreyFillsExactBinAcrossAllChannelsWithZeroClips) {
    prism::Image img;
    img.width = 4;
    img.height = 3;
    img.pixels.assign(static_cast<std::size_t>(img.width * img.height * 3), 0.5f);

    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    EXPECT_EQ(h.pixel_count, 12u);
    // Linear 0.5 is sRGB 0.7354, times 255 = 187.5 -> code 188.
    EXPECT_EQ(h.red[188], 12u);
    EXPECT_EQ(h.green[188], 12u);
    EXPECT_EQ(h.blue[188], 12u);
    EXPECT_EQ(h.luminance[188], 12u);
    EXPECT_EQ(sum_bins(h.red), 12u);
    EXPECT_EQ(sum_bins(h.green), 12u);
    EXPECT_EQ(sum_bins(h.blue), 12u);
    EXPECT_EQ(sum_bins(h.luminance), 12u);
    EXPECT_EQ(h.red_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.green_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.blue_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.luminance_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.any_under_black, 0u);
    EXPECT_EQ(h.any_over_white, 0u);
}

TEST(StageHistogram, SrgbRampPopulatesEveryBinExactlyOnce) {
    // Linear values of the 256 sRGB codes: each must land in its own code's bin.
    prism::Image img;
    img.width = 256;
    img.height = 1;
    for (int i = 0; i < 256; ++i) {
        const float v = prism::srgb8_to_linear(static_cast<std::uint8_t>(i));
        img.pixels.insert(img.pixels.end(), {v, v, v});
    }

    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    EXPECT_EQ(h.pixel_count, 256u);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(h.red[static_cast<std::size_t>(i)], 1u) << "bin " << i;
        EXPECT_EQ(h.green[static_cast<std::size_t>(i)], 1u) << "bin " << i;
        EXPECT_EQ(h.blue[static_cast<std::size_t>(i)], 1u) << "bin " << i;
        EXPECT_EQ(h.luminance[static_cast<std::size_t>(i)], 1u) << "bin " << i;
    }
    EXPECT_EQ(h.red_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.green_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.blue_clips, (ChannelClips{0, 0}));
}

TEST(StageHistogram, MidGreyLinear018LandsInBin118NotBin46) {
    prism::Image img;
    img.width = 2;
    img.height = 1;
    img.pixels.assign(6, 0.18f);

    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    // sRGB(0.18) = 0.4614, times 255 = 117.7 -> code 118 (a linear scale would give bin 46).
    EXPECT_EQ(h.red[118], 2u);
    EXPECT_EQ(h.green[118], 2u);
    EXPECT_EQ(h.blue[118], 2u);
    EXPECT_EQ(h.luminance[118], 2u);
    EXPECT_EQ(h.red[46], 0u);
}

TEST(StageHistogram, PureRedGivesExactChannelAndRec709LuminanceBins) {
    prism::Image img;
    img.width = 5;
    img.height = 2;
    for (int i = 0; i < 10; ++i) {
        img.pixels.push_back(1.0f);
        img.pixels.push_back(0.0f);
        img.pixels.push_back(0.0f);
    }

    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    EXPECT_EQ(h.pixel_count, 10u);
    EXPECT_EQ(h.red[255], 10u);
    EXPECT_EQ(h.green[0], 10u);
    EXPECT_EQ(h.blue[0], 10u);
    // Rec. 709 luminance of pure red (1, 0, 0) is 0.2126 in linear light; encoded that is
    // sRGB 0.4984, times 255 = 127.10 -> code 127.
    EXPECT_EQ(h.luminance[127], 10u);
    EXPECT_EQ(sum_bins(h.red), 10u);
    EXPECT_EQ(sum_bins(h.green), 10u);
    EXPECT_EQ(sum_bins(h.blue), 10u);
    EXPECT_EQ(sum_bins(h.luminance), 10u);
    // Exact 0.0 and 1.0 are in-range black and white, not out-of-range clips.
    EXPECT_EQ(h.red_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.green_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.blue_clips, (ChannelClips{0, 0}));
}

TEST(StageHistogram, OutOfRangeValuesCountAsUnderBlackAndOverWhiteSeparateFromBins) {
    // 4 pixels:
    //   0: (-0.2,  1.5,  0.0) -> R under, G over, B bin 0
    //   1: ( 1.1, -0.01, 1.0) -> R over,  G under, B bin 255
    //   2: ( 2.0,  0.5, -3.0) -> R over,  G bin 188, B under
    //   3: ( 0.0,  1.0,  0.5) -> R bin 0, G bin 255, B bin 188
    prism::Image img;
    img.width = 4;
    img.height = 1;
    img.pixels = {-0.2f, 1.5f,   0.0f,   //
                  1.1f,  -0.01f, 1.0f,   //
                  2.0f,  0.5f,   -3.0f,  //
                  0.0f,  1.0f,   0.5f};

    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    EXPECT_EQ(h.pixel_count, 4u);

    // Clip counts are kept separate from bins[0] and bins[255].
    EXPECT_EQ(h.red_clips, (ChannelClips{1, 2}));
    EXPECT_EQ(h.red[0], 1u);
    EXPECT_EQ(h.red[255], 0u);
    EXPECT_EQ(sum_bins(h.red), 1u);

    EXPECT_EQ(h.green_clips, (ChannelClips{1, 1}));
    EXPECT_EQ(h.green[0], 0u);
    EXPECT_EQ(h.green[188], 1u);
    EXPECT_EQ(h.green[255], 1u);
    EXPECT_EQ(sum_bins(h.green), 2u);

    EXPECT_EQ(h.blue_clips, (ChannelClips{1, 0}));
    EXPECT_EQ(h.blue[0], 1u);
    EXPECT_EQ(h.blue[188], 1u);
    EXPECT_EQ(h.blue[255], 1u);
    EXPECT_EQ(sum_bins(h.blue), 3u);

    EXPECT_DOUBLE_EQ(prism::gui::pixel_percent(h.red_clips.under_black, h.pixel_count), 25.0);
    EXPECT_DOUBLE_EQ(prism::gui::pixel_percent(h.red_clips.over_white, h.pixel_count), 50.0);
    EXPECT_GT(h.any_under_black, 0u);
    EXPECT_GT(h.any_over_white, 0u);
}

TEST(StageHistogram, FusedEncodeAndHistogramMatchesSeparateCallsAcrossThreads) {
    prism::Image img;
    img.width = 1200;
    img.height = 1000;
    img.pixels.resize(static_cast<std::size_t>(img.width) * img.height * 3);
    std::uint32_t state = 42;
    for (float& v : img.pixels) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(state >> 8) / 16777216.0f * 1.4f - 0.2f;
    }

    const std::vector<std::uint8_t> expected_rgb8 = prism::encode_srgb8(img, 1);
    const StageHistogram expected_hist = prism::gui::compute_stage_histogram(img, 1);

    for (const int threads : {1, 4}) {
        std::vector<std::uint8_t> rgb8;
        StageHistogram hist;
        prism::gui::encode_srgb8_with_histogram(img, threads, rgb8, hist);
        EXPECT_EQ(rgb8, expected_rgb8) << threads;
        EXPECT_EQ(hist, expected_hist) << threads;
        EXPECT_EQ(prism::gui::compute_stage_histogram(img, threads), expected_hist) << threads;
    }
}

TEST(StageHistogram, PreviewFrameDeliversHistogramMatchingTheViewedStage) {
    prism::Image base;
    base.width = 4;
    base.height = 1;
    base.pixels = {0.4f, 0.4f, 0.4f, 0.6f, 0.6f, 0.6f, 0.8f, 0.8f, 0.8f, 0.9f, 0.9f, 0.9f};

    prism::gui::EditSettings s;
    s.exposure_stops = 1.0f;  // doubles values: 0.6, 0.8, 0.9 become 1.2, 1.6, 1.8 (> 1)

    const auto at_decode = prism::gui::render_preview(base, s, 1, {}, {}, StageId::decode);
    const auto at_exposure = prism::gui::render_preview(base, s, 1, {}, {}, StageId::exposure);
    ASSERT_TRUE(at_decode && at_exposure);

    EXPECT_EQ(at_decode->histogram.red_clips.over_white, 0u);
    EXPECT_EQ(at_decode->histogram.any_over_white, 0u);

    EXPECT_EQ(at_exposure->histogram.red_clips.over_white, 3u);
    EXPECT_EQ(at_exposure->histogram.red[231], 1u);
    EXPECT_EQ(at_exposure->histogram.any_over_white, 3u);
}

TEST(StageHistogram, RejectsInconsistentImageAndHandlesEmptyImage) {
    const StageHistogram empty = prism::gui::compute_stage_histogram(prism::Image{});
    EXPECT_EQ(empty.pixel_count, 0u);
    EXPECT_DOUBLE_EQ(prism::gui::pixel_percent(1, 0), 0.0);
    EXPECT_EQ(empty.any_under_black, 0u);
    EXPECT_EQ(empty.any_over_white, 0u);

    prism::Image bad;
    bad.width = 2;
    bad.height = 2;
    bad.pixels = {0.1f, 0.2f, 0.3f};
    EXPECT_THROW((void)prism::gui::compute_stage_histogram(bad), std::invalid_argument);
}

TEST(StageHistogram, NaNCountsAsBlackLikeTheEncodeAndInfinitiesAsClips) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // 3 pixels: (NaN, 0.5, 0.5), (+inf, +inf, +inf), (-inf, 0.0, 1.0)
    prism::Image img;
    img.width = 3;
    img.height = 1;
    img.pixels = {nan, 0.5f, 0.5f, inf, inf, inf, -inf, 0.0f, 1.0f};

    // The display encode turns NaN into 0, so the histogram must show it as black too.
    EXPECT_EQ(prism::encode_srgb8(img, 1)[0], 0);

    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    EXPECT_EQ(h.pixel_count, 3u);
    EXPECT_EQ(h.red[0], 1u);  // the NaN
    EXPECT_EQ(h.red_clips, (ChannelClips{1, 1}));
    EXPECT_EQ(sum_bins(h.red), 1u);
    EXPECT_EQ(h.green[188], 1u);
    EXPECT_EQ(h.green[0], 1u);
    EXPECT_EQ(h.green_clips, (ChannelClips{0, 1}));
    EXPECT_EQ(h.blue[188], 1u);
    EXPECT_EQ(h.blue[255], 1u);
    EXPECT_EQ(h.blue_clips, (ChannelClips{0, 1}));
    // Every channel accounts for every pixel exactly once, bins plus clips.
    EXPECT_EQ(sum_bins(h.red) + h.red_clips.under_black + h.red_clips.over_white, h.pixel_count);
    EXPECT_EQ(sum_bins(h.green) + h.green_clips.under_black + h.green_clips.over_white,
              h.pixel_count);
    EXPECT_EQ(sum_bins(h.blue) + h.blue_clips.under_black + h.blue_clips.over_white,
              h.pixel_count);
    // Luminance: pixel 0 = NaN-bearing -> NaN -> black bin; pixel 1 = +inf -> over; pixel 2 mixes
    // -inf and +0 and 1 -> -inf -> under.
    EXPECT_EQ(h.luminance[0], 1u);
    EXPECT_EQ(h.luminance_clips, (ChannelClips{1, 1}));
    EXPECT_EQ(sum_bins(h.luminance) + h.luminance_clips.under_black + h.luminance_clips.over_white,
              h.pixel_count);
}

TEST(StageHistogram, LuminanceWeightsAreRec709InLinearLightThenEncoded) {
    prism::Image img;
    img.width = 3;
    img.height = 1;
    img.pixels = {1.0f, 0.0f, 0.0f,   // red
                  0.0f, 1.0f, 0.0f,   // green
                  0.0f, 0.0f, 1.0f};  // blue

    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    // Linear 0.2126 / 0.7152 / 0.0722, sRGB-encoded and times 255: 127.10, 219.93, 75.96.
    EXPECT_EQ(h.luminance[127], 1u);
    EXPECT_EQ(h.luminance[220], 1u);
    EXPECT_EQ(h.luminance[76], 1u);
    EXPECT_EQ(sum_bins(h.luminance), 3u);
    // Each channel is 1.0 in one pixel (bin 255) and 0.0 in two (bin 0).
    EXPECT_EQ(h.red[255], 1u);
    EXPECT_EQ(h.red[0], 2u);
}

// --- Any-channel counts and the readout under the scope ------------------------------------------

namespace {

prism::Image image_of(const std::vector<float>& samples) {
    prism::Image img;
    img.width = static_cast<int>(samples.size() / 3);
    img.height = 1;
    img.pixels = samples;
    return img;
}

}  // namespace

TEST(StageHistogramAnyChannel, APixelCountsOnceHoweverManyChannelsAreOut) {
    const prism::Image img = image_of({
        -0.1f, 0.5f,  0.5f,   // red under
        -0.1f, -0.2f, -0.3f,  // all three under: still one pixel
        1.5f,  0.5f,  0.5f,   // red over
        1.5f,  1.5f,  0.5f,   // two over: still one pixel
        -0.1f, 0.5f,  1.5f,   // one at each end: counts at both
        0.5f,  0.5f,  0.5f,   // in range
        0.0f,  1.0f,  0.0f,   // exactly on the limits: in range
    });
    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    EXPECT_EQ(h.any_under_black, 3u);
    EXPECT_EQ(h.any_over_white, 3u);
    // Not the sum of the channels, and never fewer than the worst of them.
    EXPECT_EQ(h.red_clips.under_black + h.green_clips.under_black + h.blue_clips.under_black, 5u);
    EXPECT_GE(h.any_under_black, h.red_clips.under_black);
    EXPECT_GE(h.any_over_white, h.green_clips.over_white);
}

TEST(StageHistogramAnyChannel, InfinitiesCountAndNaNDoesNot) {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const StageHistogram h = prism::gui::compute_stage_histogram(
        image_of({inf, 0.5f, 0.5f, -inf, 0.5f, 0.5f, nan, nan, nan}), 1);
    EXPECT_EQ(h.any_over_white, 1u);
    EXPECT_EQ(h.any_under_black, 1u);
}

TEST(StageHistogramAnyChannel, TheCodeValuesAtTheEndsDoNotCount) {
    // These encode to 255 and 0, the end bins, and have lost nothing: the limits are 0 and 1.
    const StageHistogram h =
        prism::gui::compute_stage_histogram(image_of({0.9999f, 0.9999f, 0.9999f, 1e-6f, 1e-6f, 1e-6f}), 1);
    EXPECT_EQ(h.red[255], 1u);
    EXPECT_EQ(h.red[0], 1u);
    EXPECT_EQ(h.any_over_white, 0u);
    EXPECT_EQ(h.any_under_black, 0u);
}

TEST(StageHistogramAnyChannel, TheCountsAreTheSameAtAnyThreadCount) {
    prism::Image img;
    img.width = 301;
    img.height = 211;
    img.pixels.resize(static_cast<std::size_t>(img.width) * img.height * 3);
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        img.pixels[i] = static_cast<float>(static_cast<int>((i * 2654435761u) % 3001u)) / 1000.0f - 1.0f;
    }
    const StageHistogram one = prism::gui::compute_stage_histogram(img, 1);
    EXPECT_GT(one.any_under_black, 0u);
    EXPECT_GT(one.any_over_white, 0u);
    for (const int threads : {2, 3, 7, 16}) {
        EXPECT_EQ(prism::gui::compute_stage_histogram(img, threads), one) << threads;
        std::vector<std::uint8_t> rgb8;
        StageHistogram encoded;
        prism::gui::encode_srgb8_with_histogram(img, threads, rgb8, encoded);
        EXPECT_EQ(encoded, one) << threads;
    }
}

// The overlay's "above white" and the readout's are one count, for any stage, and so are the two
// "below black" counts (the blue and the violet marks): both ask pixel_below_black. A value too
// small to change an 8-bit code, on a pixel that still has light, is in neither.
TEST(StageHistogramAnyChannel, TheCountsAgreeWithTheClipMaskOnTheSamePicture) {
    const float ceiling = 1.0f - prism::gui::file_ceiling_epsilon;
    const prism::Image img = image_of({
        1.5f,  0.2f,  0.2f,   // over
        1.0f,  0.2f,  0.2f,   // at the file's ceiling
        ceiling, 0.2f, 0.2f,  // at the bottom of the band
        0.99f, 0.2f,  0.2f,   // below the band
        -0.5f, -0.5f, -0.5f,  // no light: low
        -0.5f, 0.8f,  0.8f,   // has light: gamut
        -1e-5f, 0.2f, 0.2f,   // has light, too little under 0 to be gamut
        0.0f,  0.0f,  0.0f,   // black
    });
    for (const bool file_ceiling : {false, true}) {
        const StageHistogram h = prism::gui::compute_stage_histogram(img, 1, file_ceiling);
        std::vector<std::uint8_t> mask;
        prism::gui::compute_clip_mask(img, 1, mask, file_ceiling);
        std::uint64_t high = 0;
        std::uint64_t low_or_gamut = 0;
        for (const std::uint8_t m : mask) {
            high += (m & prism::gui::clip_mask_high) != 0;
            low_or_gamut += (m & (prism::gui::clip_mask_low | prism::gui::clip_mask_gamut)) != 0;
        }
        EXPECT_EQ(h.any_over_white, high) << file_ceiling;
        EXPECT_EQ(low_or_gamut, 2u);
        EXPECT_EQ(h.any_under_black, low_or_gamut);
    }
    EXPECT_EQ(prism::gui::compute_stage_histogram(img, 1, true).any_over_white, 3u);
    EXPECT_EQ(prism::gui::compute_stage_histogram(img, 1, false).any_over_white, 1u);
}

TEST(StageHistogramAnyChannel, ThePreviewCountsTheFilesCeilingAtDecodeOnly) {
    prism::Image base = image_of({1.0f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f});
    prism::gui::EditSettings settings;
    const auto decode = prism::gui::render_preview(base, settings, 1, {}, {}, StageId::decode);
    ASSERT_TRUE(decode.has_value());
    EXPECT_EQ(decode->histogram.any_over_white, 1u);
    EXPECT_EQ(decode->histogram.red_clips.over_white, 0u);  // nothing is above 1.0 ...
    EXPECT_EQ(decode->histogram.red_clips.at_ceiling, 1u);  // ... one is at the ceiling
    EXPECT_EQ(decode->histogram.red_clips.high(), 1u);      // which the tooltip's R counts
    EXPECT_EQ(decode->histogram.green_clips.high(), 0u);
    const auto exposure = prism::gui::render_preview(base, settings, 1, {}, {}, StageId::exposure);
    ASSERT_TRUE(exposure.has_value());
    EXPECT_EQ(exposure->histogram.any_over_white, 0u);
    EXPECT_EQ(exposure->histogram.red_clips.high(), 0u);  // the ceiling is Decode's alone
}

TEST(StageHistogramAnyChannel, TinyNegativesDoNotCountButRealOnesDo) {
    const float tiny = -prism::gui::gamut_tolerance / 2.0f;
    const prism::Image img = image_of({
        tiny,  0.5f,  0.5f,   // rounding noise on a pixel with light: not below black
        tiny,  tiny,  tiny,   // no light at all: below black
        -0.01f, 0.5f, 0.5f,   // a real negative, a colour outside sRGB: below black
        0.2f,  0.2f,  0.2f,
    });
    const StageHistogram h = prism::gui::compute_stage_histogram(img, 1);
    EXPECT_EQ(h.any_under_black, 2u);
    // The channel figures are plainer: every negative channel.
    EXPECT_EQ(h.red_clips.under_black, 3u);
}

TEST(StageHistogramAnyChannel, TheHistogramAndTheMaskUseTheSharedPredicates) {
    // Every combination of a red channel around the limits, with and without light elsewhere.
    for (const float red : {-1.0f, -1e-3f, -1.5e-4f, -1e-5f, 0.0f, 1e-3f, 0.5f, 1.0f, 1.0f - 1e-7f, 1.5f}) {
        for (const float other : {0.0f, 0.3f, 1.0f}) {
            const prism::Image img = image_of({red, other, other});
            const prism::gui::PixelRange range = prism::gui::pixel_range(img.pixels.data());
            for (const bool file_ceiling : {false, true}) {
                const StageHistogram h = prism::gui::compute_stage_histogram(img, 1, file_ceiling);
                std::vector<std::uint8_t> mask;
                prism::gui::compute_clip_mask(img, 1, mask, file_ceiling);
                EXPECT_EQ(h.any_under_black, prism::gui::pixel_below_black(range) ? 1u : 0u)
                    << red << " " << other;
                EXPECT_EQ(h.any_over_white, prism::gui::pixel_above_white(range, file_ceiling) ? 1u : 0u)
                    << red << " " << other << " " << file_ceiling;
                EXPECT_EQ((mask[0] & (prism::gui::clip_mask_low | prism::gui::clip_mask_gamut)) != 0,
                          h.any_under_black != 0);
                EXPECT_EQ((mask[0] & prism::gui::clip_mask_high) != 0, h.any_over_white != 0);
            }
        }
    }
}

TEST(ScopeReadoutText, IsTheSpecsFormWithOneDecimal) {
    StageHistogram h;
    h.pixel_count = 1000;
    h.any_under_black = 1;
    EXPECT_EQ(prism::gui::scope_readout(h),
              (prism::gui::ScopeReadout{"0.1% below black", "0.0% above white"}));
    h.any_under_black = 123;
    h.any_over_white = 70;
    EXPECT_EQ(prism::gui::scope_readout(h),
              (prism::gui::ScopeReadout{"12.3% below black", "7.0% above white"}));
}

TEST(ScopeReadoutText, ZeroMeansNoPixelAtAll) {
    StageHistogram h;
    h.pixel_count = 1'000'000;
    h.any_under_black = 1;
    h.any_over_white = 999'999;
    EXPECT_EQ(prism::gui::scope_readout(h).below_black, "<0.1% below black");
    EXPECT_EQ(prism::gui::scope_readout(h).above_white, ">99.9% above white");
    h.any_under_black = 500;  // 0.05% rounds up to the first figure shown
    h.any_over_white = 1'000'000;
    EXPECT_EQ(prism::gui::scope_readout(h).below_black, "0.1% below black");
    EXPECT_EQ(prism::gui::scope_readout(h).above_white, "100.0% above white");
    EXPECT_EQ(prism::gui::scope_readout(StageHistogram{}),
              (prism::gui::ScopeReadout{"0.0% below black", "0.0% above white"}));
}

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "prism/color.hpp"
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
    // 0.5 * 255 + 0.5 = 128.0 -> bin 128.
    EXPECT_EQ(h.red[128], 12u);
    EXPECT_EQ(h.green[128], 12u);
    EXPECT_EQ(h.blue[128], 12u);
    EXPECT_EQ(h.luminance[128], 12u);
    EXPECT_EQ(sum_bins(h.red), 12u);
    EXPECT_EQ(sum_bins(h.green), 12u);
    EXPECT_EQ(sum_bins(h.blue), 12u);
    EXPECT_EQ(sum_bins(h.luminance), 12u);
    EXPECT_EQ(h.red_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.green_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.blue_clips, (ChannelClips{0, 0}));
    EXPECT_EQ(h.luminance_clips, (ChannelClips{0, 0}));
    EXPECT_FALSE(prism::gui::has_shadow_clipping(h));
    EXPECT_FALSE(prism::gui::has_highlight_clipping(h));
}

TEST(StageHistogram, LinearRampPopulatesEveryBinExactlyOnce) {
    prism::Image img;
    img.width = 256;
    img.height = 1;
    img.pixels.reserve(256 * 3);
    for (int i = 0; i < 256; ++i) {
        const float v = static_cast<float>(i) / 255.0f;
        img.pixels.push_back(v);
        img.pixels.push_back(v);
        img.pixels.push_back(v);
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
    // Rec. 709 luminance of pure red (1, 0, 0) is 0.2126; 0.2126 * 255 + 0.5 = 54.713 -> bin 54.
    EXPECT_EQ(h.luminance[54], 10u);
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
    //   2: ( 2.0,  0.5, -3.0) -> R over,  G bin 128, B under
    //   3: ( 0.0,  1.0,  0.5) -> R bin 0, G bin 255, B bin 128
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
    EXPECT_EQ(h.green[128], 1u);
    EXPECT_EQ(h.green[255], 1u);
    EXPECT_EQ(sum_bins(h.green), 2u);

    EXPECT_EQ(h.blue_clips, (ChannelClips{1, 0}));
    EXPECT_EQ(h.blue[0], 1u);
    EXPECT_EQ(h.blue[128], 1u);
    EXPECT_EQ(h.blue[255], 1u);
    EXPECT_EQ(sum_bins(h.blue), 3u);

    EXPECT_DOUBLE_EQ(prism::gui::pixel_percent(h.red_clips.under_black, h.pixel_count), 25.0);
    EXPECT_DOUBLE_EQ(prism::gui::pixel_percent(h.red_clips.over_white, h.pixel_count), 50.0);
    EXPECT_TRUE(prism::gui::has_shadow_clipping(h));
    EXPECT_TRUE(prism::gui::has_highlight_clipping(h));
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
    EXPECT_FALSE(prism::gui::has_highlight_clipping(at_decode->histogram));

    EXPECT_EQ(at_exposure->histogram.red_clips.over_white, 3u);
    EXPECT_EQ(at_exposure->histogram.red[static_cast<std::size_t>(0.8f * 255.0f + 0.5f)], 1u);
    EXPECT_TRUE(prism::gui::has_highlight_clipping(at_exposure->histogram));
}

TEST(StageHistogram, RejectsInconsistentImageAndHandlesEmptyImage) {
    const StageHistogram empty = prism::gui::compute_stage_histogram(prism::Image{});
    EXPECT_EQ(empty.pixel_count, 0u);
    EXPECT_DOUBLE_EQ(prism::gui::pixel_percent(1, 0), 0.0);
    EXPECT_FALSE(prism::gui::has_shadow_clipping(empty));
    EXPECT_FALSE(prism::gui::has_highlight_clipping(empty));

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
    EXPECT_EQ(h.green[128], 1u);
    EXPECT_EQ(h.green[0], 1u);
    EXPECT_EQ(h.green_clips, (ChannelClips{0, 1}));
    EXPECT_EQ(h.blue[128], 1u);
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

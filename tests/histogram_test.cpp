#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "prism/color.hpp"
#include "prism/histogram.hpp"
#include "prism/image.hpp"

namespace {

// Builds an image from 8-bit sRGB values; srgb8_to_linear round-trips exactly (see color_test),
// so each pixel lands in exactly the bin we asked for.
prism::Image make_image(int width, int height,
                        std::initializer_list<std::array<std::uint8_t, 3>> colours) {
    prism::Image img;
    img.width = width;
    img.height = height;
    for (const auto& rgb : colours) {
        for (std::uint8_t v : rgb) {
            img.pixels.push_back(prism::srgb8_to_linear(v));
        }
    }
    return img;
}

std::uint64_t total(const prism::Histogram::Counts& counts) {
    return std::accumulate(counts.begin(), counts.end(), std::uint64_t{0});
}

}  // namespace

TEST(Histogram, SolidColourFillsOneBinPerChannel) {
    prism::Image img;
    img.width = 4;
    img.height = 3;
    for (int i = 0; i < 12; ++i) {
        for (std::uint8_t v : {200, 100, 30}) {
            img.pixels.push_back(prism::srgb8_to_linear(v));
        }
    }
    const prism::Histogram h = prism::compute_histogram(img);

    EXPECT_EQ(h.pixel_count, 12u);
    EXPECT_EQ(h.red[200], 12u);
    EXPECT_EQ(h.green[100], 12u);
    EXPECT_EQ(h.blue[30], 12u);
    // 0.2126*200 + 0.7152*100 + 0.0722*30 = 116.206, which rounds to 116.
    EXPECT_EQ(h.luma[116], 12u);
    for (const auto* counts : {&h.red, &h.green, &h.blue, &h.luma}) {
        EXPECT_EQ(total(*counts), 12u);  // nothing anywhere else
    }
}

TEST(Histogram, TwoColoursSplitCounts) {
    // Three black pixels and one white one.
    const prism::Image img = make_image(2, 2, {{0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}});
    const prism::Histogram h = prism::compute_histogram(img);

    for (const auto* counts : {&h.red, &h.green, &h.blue, &h.luma}) {
        EXPECT_EQ((*counts)[0], 3u);
        EXPECT_EQ((*counts)[255], 1u);
        EXPECT_EQ(total(*counts), 4u);
    }
}

TEST(Histogram, ClippingPercentages) {
    // Red: 2 of 4 at 255, 1 at 0. Green: 1 at 0, none at 255. Blue: none clipped.
    const prism::Image img =
        make_image(4, 1, {{255, 0, 10}, {255, 50, 20}, {0, 60, 30}, {128, 70, 40}});
    const prism::Histogram h = prism::compute_histogram(img);

    EXPECT_DOUBLE_EQ(prism::bin_percent(h, h.red, 255), 50.0);
    EXPECT_DOUBLE_EQ(prism::bin_percent(h, h.red, 0), 25.0);
    EXPECT_DOUBLE_EQ(prism::bin_percent(h, h.green, 0), 25.0);
    EXPECT_DOUBLE_EQ(prism::bin_percent(h, h.green, 255), 0.0);
    EXPECT_DOUBLE_EQ(prism::bin_percent(h, h.blue, 0), 0.0);
    EXPECT_DOUBLE_EQ(prism::bin_percent(h, h.blue, 255), 0.0);
}

TEST(Histogram, OutOfRangeLinearValuesCountAsClipped) {
    // After exposure or a matrix, linear values can go below 0 or above 1.
    prism::Image img;
    img.width = 2;
    img.height = 1;
    img.pixels = {-0.5f, 3.0f, 1.0f, 2.0f, -1.0f, 0.0f};
    const prism::Histogram h = prism::compute_histogram(img);

    EXPECT_EQ(h.red[0], 1u);
    EXPECT_EQ(h.red[255], 1u);
    EXPECT_EQ(h.green[255], 1u);
    EXPECT_EQ(h.green[0], 1u);
    EXPECT_EQ(h.blue[255], 1u);
    EXPECT_EQ(h.blue[0], 1u);
}

TEST(Histogram, EmptyImageGivesZeroPercent) {
    const prism::Histogram h = prism::compute_histogram(prism::Image{});
    EXPECT_EQ(h.pixel_count, 0u);
    EXPECT_DOUBLE_EQ(prism::bin_percent(h, h.red, 0), 0.0);
}

TEST(Histogram, FromEncodedBytesMatchesFromImage) {
    // A 1200x1000 image is over the multithreading threshold; the values cover the full range
    // and beyond, so every bin and the clamping paths are exercised.
    prism::Image img;
    img.width = 1200;
    img.height = 1000;
    std::uint32_t state = 7;
    for (std::size_t i = 0; i < std::size_t{1200} * 1000 * 3; ++i) {
        state = state * 1664525u + 1013904223u;
        img.pixels.push_back(static_cast<float>(state >> 8) / 16777216.0f * 1.4f - 0.2f);
    }
    const std::vector<std::uint8_t> bytes = prism::encode_srgb8(img);
    for (int threads : {1, 4}) {
        const prism::Histogram a = prism::compute_histogram(img, threads);
        const prism::Histogram b = prism::compute_histogram(bytes, img.width, img.height, threads);
        EXPECT_EQ(a.red, b.red) << threads;
        EXPECT_EQ(a.green, b.green) << threads;
        EXPECT_EQ(a.blue, b.blue) << threads;
        EXPECT_EQ(a.luma, b.luma) << threads;
        EXPECT_EQ(a.pixel_count, b.pixel_count) << threads;
    }
}

TEST(Histogram, FromBytesRejectsWrongSize) {
    const std::vector<std::uint8_t> bytes(10);
    EXPECT_THROW((void)prism::compute_histogram(bytes, 2, 2), std::invalid_argument);
}

TEST(RenderHistogram, HasRequestedSize) {
    const prism::Image out = prism::render_histogram(prism::Histogram{}, 512, 200);
    EXPECT_EQ(out.width, 512);
    EXPECT_EQ(out.height, 200);
    EXPECT_EQ(out.pixels.size(), static_cast<std::size_t>(512 * 200 * 3));
}

TEST(RenderHistogram, DrawsBarForFilledBinOnly) {
    // All-white image: every count is in bin 255, i.e. the rightmost columns.
    const prism::Image img = make_image(1, 1, {{255, 255, 255}});
    const prism::Image out = prism::render_histogram(prism::compute_histogram(img), 512, 200);

    auto pixel = [&](int x, int y) { return &out.pixels[(y * out.width + x) * 3]; };
    // Bottom-right is inside all four bars: grey + R + G + B shows as near-white.
    EXPECT_GT(prism::linear_to_srgb8(pixel(511, 199)[0]), 200);
    EXPECT_EQ(pixel(511, 199)[0], pixel(511, 199)[1]);
    // Bottom-left (bin 0) is empty, so it is plain background.
    EXPECT_LT(prism::linear_to_srgb8(pixel(0, 199)[0]), 40);
}

TEST(RenderHistogram, BadSizeThrows) {
    EXPECT_THROW((void)prism::render_histogram(prism::Histogram{}, 0, 200), std::invalid_argument);
    EXPECT_THROW((void)prism::render_histogram(prism::Histogram{}, 512, -1), std::invalid_argument);
}

TEST(RenderHistogram, ClippedSpikeDoesNotSetScale) {
    // 3 pixels clipped to 255 and 1 mid-grey: the mid-grey bar should still be full height.
    const prism::Image img =
        make_image(2, 2, {{255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {128, 128, 128}});
    const prism::Image out = prism::render_histogram(prism::compute_histogram(img), 256, 100);

    auto red_at = [&](int x, int y) { return out.pixels[(y * out.width + x) * 3]; };
    EXPECT_GT(prism::linear_to_srgb8(red_at(128, 0)), 200);  // bin 128 reaches the top row
    EXPECT_GT(prism::linear_to_srgb8(red_at(255, 0)), 200);  // bin 255 is cut off at the top
}

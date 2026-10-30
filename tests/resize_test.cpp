#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "prism/image.hpp"
#include "prism/resize.hpp"

namespace {

prism::Image make_image(int width, int height) {
    prism::Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    return img;
}

float* pixel(prism::Image& img, int x, int y) {
    return &img.pixels[(static_cast<std::size_t>(y) * img.width + x) * 3];
}

// Varied, repeatable values: a tiny linear congruential generator, scaled to [0, 2).
prism::Image make_noise(int width, int height) {
    prism::Image img = make_image(width, height);
    std::uint32_t state = 1;
    for (float& v : img.pixels) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(state >> 8) / static_cast<float>(1u << 23);
    }
    return img;
}

std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out;
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

// The slow, obvious way: for each output pixel, the overlap of its rectangle with every source
// pixel, in double precision. Output pixel (ox, oy) covers source x from ox * W / w to
// (ox + 1) * W / w, and likewise for y.
prism::Image reference_downscale(const prism::Image& src, int w, int h) {
    prism::Image out = make_image(w, h);
    const double sx = static_cast<double>(src.width) / w;
    const double sy = static_cast<double>(src.height) / h;
    for (int oy = 0; oy < h; ++oy) {
        for (int ox = 0; ox < w; ++ox) {
            double sum[3] = {0, 0, 0};
            for (int y = 0; y < src.height; ++y) {
                const double oy_overlap =
                    std::min(y + 1.0, (oy + 1) * sy) - std::max(1.0 * y, oy * sy);
                for (int x = 0; x < src.width && oy_overlap > 0; ++x) {
                    const double ox_overlap =
                        std::min(x + 1.0, (ox + 1) * sx) - std::max(1.0 * x, ox * sx);
                    if (ox_overlap > 0) {
                        for (int c = 0; c < 3; ++c) {
                            sum[c] +=
                                ox_overlap * oy_overlap *
                                src.pixels[(static_cast<std::size_t>(y) * src.width + x) * 3 + c];
                        }
                    }
                }
            }
            for (int c = 0; c < 3; ++c) {
                pixel(out, ox, oy)[c] = static_cast<float>(sum[c] / (sx * sy));
            }
        }
    }
    return out;
}

}  // namespace

TEST(Downscale, HalvingAveragesEachTwoByTwoBlockExactly) {
    // 4x2 -> 2x1. Left block: red 0, 1, 1, 2 (mean 1), green 0, 1, 0, 1 (mean 0.5: the average
    // of the linear light, not of sRGB values), blue 3, 3, 3, 3. Right block: red 1, 2, 3, 6.
    prism::Image img = make_image(4, 2);
    const float values[2][4][3] = {{{0, 0, 3}, {1, 1, 3}, {1, 0.25f, 0}, {2, 0.25f, 0}},
                                   {{1, 0, 3}, {2, 1, 3}, {3, 0.25f, 0}, {6, 0.25f, 4}}};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x) {
            std::copy(values[y][x], values[y][x] + 3, pixel(img, x, y));
        }
    }
    const prism::Image out = prism::downscale(img, 2);
    ASSERT_EQ(out.width, 2);
    ASSERT_EQ(out.height, 1);
    EXPECT_EQ(out.pixels, (std::vector<float>{1, 0.5f, 3, 3, 0.25f, 1}));
}

TEST(Downscale, KeepsTheAspectRatio) {
    const struct {
        int w, h, max, out_w, out_h;
    } cases[] = {
        {3000, 2000, 2560, 2560, 1707},  // 2000 * 2560 / 3000 = 1706.67, rounded
        {2000, 3000, 2560, 1707, 2560},  // portrait: the height is the long side
        {1000, 1000, 10, 10, 10},       {1001, 1000, 10, 10, 10},  // 9.99 rounds to 10
        {1000, 3, 10, 10, 1},  // 0.03 would round to 0: kept at 1 pixel
        {3, 1000, 10, 1, 10},
    };
    for (const auto& c : cases) {
        const prism::Image out = prism::downscale(make_image(c.w, c.h), c.max, 1);
        EXPECT_EQ(out.width, c.out_w) << c.w << "x" << c.h;
        EXPECT_EQ(out.height, c.out_h) << c.w << "x" << c.h;
        EXPECT_EQ(out.pixels.size(), static_cast<std::size_t>(c.out_w) * c.out_h * 3);
    }
}

TEST(Downscale, NeverEnlarges) {
    const prism::Image img = make_noise(30, 20);
    for (const int max : {30, 31, 1000}) {
        const prism::Image out = prism::downscale(img, max);
        EXPECT_EQ(out.width, 30);
        EXPECT_EQ(out.height, 20);
        EXPECT_EQ(bits(out), bits(img)) << max;
    }
    const prism::Image empty = prism::downscale(prism::Image{}, 10);
    EXPECT_EQ(empty.width, 0);
    EXPECT_TRUE(empty.pixels.empty());
}

TEST(Downscale, GradientMatchesAHandComputation) {
    // 6x3 -> 4x2: each output pixel covers 1.5 x 1.5 source pixels. Red = x + 10 y, green twice
    // that, blue 0.5 everywhere.
    //   Across: output 0 covers source [0, 1.5): 1 of pixel 0 and 0.5 of pixel 1, so
    //   (0 * 1 + 1 * 0.5) / 1.5 = 1/3. Likewise output 1 = (1 * 0.5 + 2 * 1) / 1.5 = 5/3,
    //   output 2 = (3 + 4 * 0.5) / 1.5 = 10/3, output 3 = (4 * 0.5 + 5) / 1.5 = 14/3.
    //   Down, the same weights give 10 * 1/3 and 10 * 5/3.
    // x + 10 y is a sum of a function of x and one of y, so the 2-D average is the sum of the two.
    prism::Image img = make_image(6, 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 6; ++x) {
            const float v = static_cast<float>(x + 10 * y);
            float* p = pixel(img, x, y);
            p[0] = v;
            p[1] = 2 * v;
            p[2] = 0.5f;
        }
    }
    prism::Image out = prism::downscale(img, 4);
    ASSERT_EQ(out.width, 4);
    ASSERT_EQ(out.height, 2);
    const double across[] = {1.0 / 3, 5.0 / 3, 10.0 / 3, 14.0 / 3};
    const double down[] = {10.0 / 3, 50.0 / 3};
    for (int oy = 0; oy < 2; ++oy) {
        for (int ox = 0; ox < 4; ++ox) {
            const float* p = pixel(out, ox, oy);
            EXPECT_NEAR(p[0], across[ox] + down[oy], 1e-5) << ox << "," << oy;
            EXPECT_NEAR(p[1], 2 * (across[ox] + down[oy]), 1e-5) << ox << "," << oy;
            EXPECT_NEAR(p[2], 0.5, 1e-6) << ox << "," << oy;
        }
    }
}

TEST(Downscale, SizesThatDoNotDivideEvenlyMatchTheReference) {
    const struct {
        int w, h, max;
    } cases[] = {{5, 3, 2}, {7, 5, 3}, {13, 11, 4}, {101, 37, 10}, {37, 101, 9}, {64, 48, 47}};
    for (const auto& c : cases) {
        const prism::Image img = make_noise(c.w, c.h);
        const prism::Image out = prism::downscale(img, c.max);
        const prism::Image ref = reference_downscale(img, out.width, out.height);
        ASSERT_EQ(out.pixels.size(), ref.pixels.size());
        for (std::size_t i = 0; i < out.pixels.size(); ++i) {
            ASSERT_NEAR(out.pixels[i], ref.pixels[i], 1e-5) << c.w << "x" << c.h << " -> " << c.max;
        }
    }
}

TEST(Downscale, ConstantImageStaysConstant) {
    prism::Image img = make_image(97, 61);
    std::fill(img.pixels.begin(), img.pixels.end(), 0.18f);
    const prism::Image out = prism::downscale(img, 20);
    for (const float v : out.pixels) {
        ASSERT_FLOAT_EQ(v, 0.18f);  // within 4 float steps: the weights are rounded to float
    }
}

TEST(Downscale, ResultIsTheSameForAnyThreadCount) {
    // Over parallel_min_pixels (~1 MP), so several threads really run.
    const prism::Image img = make_noise(1300, 900);
    const std::vector<std::uint32_t> one = bits(prism::downscale(img, 700, 1));
    for (const int threads : {2, 3, 7, 12, 0}) {
        EXPECT_EQ(bits(prism::downscale(img, 700, threads)), one) << threads << " threads";
    }
}

TEST(Downscale, BadArgumentsThrow) {
    EXPECT_THROW((void)prism::downscale(make_image(4, 4), 0), std::invalid_argument);
    prism::Image broken = make_image(4, 4);
    broken.pixels.pop_back();
    EXPECT_THROW((void)prism::downscale(broken, 2), std::invalid_argument);
}

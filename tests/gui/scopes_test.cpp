#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#include "prism/color.hpp"
#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/scopes.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"

using prism::gui::compute_scopes;
using prism::gui::Scopes;
using prism::gui::VectorscopeCell;

namespace {

constexpr std::size_t N = Scopes::size;

std::vector<std::uint8_t> solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::vector<std::uint8_t> px;
    px.reserve(static_cast<std::size_t>(w) * h * 3);
    for (int i = 0; i < w * h; ++i) {
        px.insert(px.end(), {r, g, b});
    }
    return px;
}

std::uint64_t total(const Scopes::Grid& g) {
    return std::accumulate(g.begin(), g.end(), std::uint64_t{0});
}

}  // namespace

TEST(Scopes, FlatGreyPutsOneLevelPerColumnAndTheVectorscopeCentre) {
    // 256 wide, 4 tall: one image column per scope column, 4 samples each.
    const Scopes s = compute_scopes(solid(256, 4, 100, 100, 100), 256, 4);
    ASSERT_EQ(s.stride, 1);
    EXPECT_EQ(s.sample_count, 1024u);
    for (std::size_t col = 0; col < N; ++col) {
        for (std::size_t level = 0; level < N; ++level) {
            const std::uint32_t expect = level == 100 ? 4u : 0u;
            EXPECT_EQ(s.waveform[level * N + col], expect);
            EXPECT_EQ(s.parade_red[level * N + col], expect);
            EXPECT_EQ(s.parade_green[level * N + col], expect);
            EXPECT_EQ(s.parade_blue[level * N + col], expect);
        }
    }
    EXPECT_EQ(s.vectorscope[128 * N + 128], 1024u);
    EXPECT_EQ(total(s.vectorscope), 1024u);
}

TEST(Scopes, HorizontalRampGivesADiagonalWaveform) {
    std::vector<std::uint8_t> px;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 256; ++x) {
            px.insert(px.end(), {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(x),
                                 static_cast<std::uint8_t>(x)});
        }
    }
    const Scopes s = compute_scopes(px, 256, 3);
    for (std::size_t col = 0; col < N; ++col) {
        EXPECT_EQ(s.waveform[col * N + col], 3u) << "column " << col;
        EXPECT_EQ(s.parade_green[col * N + col], 3u);
    }
    EXPECT_EQ(total(s.waveform), 768u);  // and nothing off the diagonal
}

TEST(Scopes, PrimariesLandInTheirVectorscopeCells) {
    // Computed by hand: Cb = (B - Y) / 1.8556, Cr = (R - Y) / 1.5748, in codes, plus 128.
    struct Case {
        std::uint8_t r, g, b;
        int cb, cr;
    };
    const Case cases[] = {
        {255, 0, 0, 99, 255},   // red:   Cb -29.3, Cr +127.5 (clamped)
        {0, 255, 0, 30, 12},    // green: Cb -98.3, Cr -115.8
        {0, 0, 255, 255, 116},  // blue:  Cb +127.5 (clamped), Cr -11.7
    };
    for (const Case& c : cases) {
        const Scopes s = compute_scopes(solid(8, 8, c.r, c.g, c.b), 8, 8);
        EXPECT_EQ(s.vectorscope[static_cast<std::size_t>(c.cr) * N + c.cb], 64u)
            << int(c.r) << "," << int(c.g) << "," << int(c.b);
        EXPECT_EQ(total(s.vectorscope), 64u);
        EXPECT_EQ((prism::gui::vectorscope_cell(c.r, c.g, c.b)), (VectorscopeCell{c.cb, c.cr}));
    }
}

TEST(Scopes, LumaOfGreysIsTheGreyAndOfPrimariesIsRec709) {
    for (int v : {0, 1, 100, 254, 255}) {
        const auto u = static_cast<std::uint8_t>(v);
        EXPECT_EQ(prism::gui::luma8(u, u, u), v);
    }
    EXPECT_EQ(prism::gui::luma8(255, 0, 0), 54);   // 0.2126 * 255 = 54.2
    EXPECT_EQ(prism::gui::luma8(0, 255, 0), 182);  // 182.4
    EXPECT_EQ(prism::gui::luma8(0, 0, 255), 18);   // 18.4
}

TEST(Scopes, SamplesAtMostAQuarterMillionPixelsAndStaysInRange) {
    const int w = 2000;
    const int h = 1500;  // 3 M pixels
    const Scopes s = compute_scopes(solid(w, h, 10, 20, 30), w, h);
    EXPECT_GT(s.stride, 1);
    EXPECT_LE(s.sample_count, Scopes::max_samples);
    EXPECT_GT(s.sample_count, Scopes::max_samples / 2);  // a real sample, not a handful
    EXPECT_EQ(total(s.waveform), s.sample_count);
    EXPECT_EQ(total(s.vectorscope), s.sample_count);
}

TEST(Scopes, SmallPicturesSpreadTheirColumnsAcrossTheWidth) {
    // 2 columns of different greys: left half in the first columns, right half in the last.
    std::vector<std::uint8_t> px = {0, 0, 0, 255, 255, 255};
    const Scopes s = compute_scopes(px, 2, 1);
    EXPECT_EQ(s.waveform[0 * N + 0], 1u);
    EXPECT_EQ(s.waveform[255 * N + 128], 1u);
    EXPECT_EQ(s.sample_count, 2u);
}

TEST(Scopes, EmptyOrMismatchedPicturesGiveZeroGridsWithoutCrashing) {
    for (const Scopes& s : {compute_scopes({}, 0, 0), compute_scopes({}, 5, 0),
                            compute_scopes({}, 0, 5), compute_scopes({1, 2, 3}, 4, 4),
                            compute_scopes({}, -1, 3)}) {
        EXPECT_EQ(s.sample_count, 0u);
        EXPECT_EQ(s.vectorscope.size(), N * N);
        EXPECT_EQ(total(s.waveform), 0u);
    }
    // A single pixel and a single row are fine.
    EXPECT_EQ(compute_scopes(solid(1, 1, 9, 9, 9), 1, 1).sample_count, 1u);
    EXPECT_EQ(compute_scopes(solid(7, 1, 9, 9, 9), 7, 1).sample_count, 7u);
}

TEST(Scopes, RenderPreviewCarriesScopesOnlyWhenAsked) {
    prism::Image img;
    img.width = 16;
    img.height = 16;
    img.pixels.assign(16 * 16 * 3, 0.18f);
    const prism::gui::EditSettings settings;

    const auto off = prism::gui::render_preview(img, settings, 1, {});
    ASSERT_TRUE(off);
    EXPECT_FALSE(off->scopes.has_value());

    const auto on = prism::gui::render_preview(img, settings, 1, {}, {}, prism::gui::final_stage,
                                               true);
    ASSERT_TRUE(on);
    ASSERT_TRUE(on->scopes.has_value());
    EXPECT_EQ(*on->scopes, compute_scopes(on->rgb8, on->width, on->height));
    EXPECT_EQ(on->rgb8, off->rgb8);
    EXPECT_EQ(on->histogram, off->histogram);

    const auto decode = prism::gui::render_preview(img, settings, 1, {}, {},
                                                   prism::gui::StageId::decode, true);
    ASSERT_TRUE(decode);
    EXPECT_TRUE(decode->scopes.has_value());
}

TEST(ScopeGuides, AreWhereThoseSharesOfWhitesLightLandOnTheDisplayScale) {
    const auto guides = prism::gui::scope_guides();
    ASSERT_EQ(guides.size(), 5u);
    EXPECT_EQ(guides[0].level, 255);
    EXPECT_EQ(guides[0].label, "white");
    EXPECT_EQ(guides[1].level, 188);  // linear 0.5, as in the histogram's bins
    EXPECT_EQ(guides[1].label, "50%");
    EXPECT_EQ(guides[2].level, 118);  // mid grey
    EXPECT_EQ(guides[2].label, "18%");
    EXPECT_EQ(guides[3].level, prism::linear_to_srgb8(0.05f));
    EXPECT_EQ(guides[3].label, "5%");
    EXPECT_EQ(guides[4].level, 0);
    EXPECT_EQ(guides[4].label, "black");
}

TEST(VectorscopeCellColour, TheNeutralAxisIsAGrey) {
    const prism::gui::ScopeRgb c = prism::gui::vectorscope_cell_colour(128, 128);
    EXPECT_EQ(c.r, c.g);
    EXPECT_EQ(c.g, c.b);
    EXPECT_GT(c.r, 150);
}

TEST(VectorscopeCellColour, EachColoursCellIsDrawnInThatColoursHue) {
    const auto colour_of = [](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        const prism::gui::VectorscopeCell cell = prism::gui::vectorscope_cell(r, g, b);
        return prism::gui::vectorscope_cell_colour(cell.cb, cell.cr);
    };
    const prism::gui::ScopeRgb red = colour_of(255, 0, 0);
    EXPECT_GT(red.r, 240);
    EXPECT_LT(red.g, 40);
    EXPECT_LT(red.b, 40);
    const prism::gui::ScopeRgb green = colour_of(0, 255, 0);
    EXPECT_GT(green.g, 240);
    EXPECT_LT(green.r, 40);
    EXPECT_LT(green.b, 40);
    const prism::gui::ScopeRgb blue = colour_of(0, 0, 255);
    EXPECT_GT(blue.b, 240);
    EXPECT_LT(blue.r, 40);
    EXPECT_LT(blue.g, 40);
    const prism::gui::ScopeRgb yellow = colour_of(255, 255, 0);
    EXPECT_GT(yellow.r, 240);
    EXPECT_GT(yellow.g, 240);
    EXPECT_LT(yellow.b, 40);
    // A faint tint stays close to the grey, leaning the right way.
    const prism::gui::ScopeRgb faint = colour_of(140, 128, 128);
    EXPECT_GT(faint.r, faint.g);
    EXPECT_GT(faint.g, 120);
}

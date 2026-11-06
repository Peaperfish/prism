#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "prism/gui/histogram.hpp"
#include "prism/gui/histogram_paint.hpp"
#include "prism/gui/preferences.hpp"

using prism::gui::histogram_paint_colours;
using prism::gui::HistogramPaintCache;
using prism::gui::HistogramPaintColours;
using prism::gui::HistogramPaintSize;
using prism::gui::paint_histogram;
using prism::gui::Rgba8;
using prism::gui::StageHistogram;
using prism::gui::Theme;

namespace {

// 256 columns, one per bin, and 100 rows: a bin of 60 of 100 is exactly 60 rows.
constexpr int inset_x = 4;
constexpr int inset_top = 8;
constexpr int picture_width = 256 + 2 * inset_x;
constexpr int picture_height = 100 + inset_top;

HistogramPaintSize plain_size() {
    HistogramPaintSize s;
    s.width = picture_width;
    s.height = picture_height;
    s.inset_x = inset_x;
    s.inset_top = inset_top;
    s.triangle_width = 0;  // these tests look at the fills
    s.triangle_height = 0;
    s.grid_line = 1;
    return s;
}

// Pure, full-strength colours on a black plot, with no grid and no luminance: what a screen does
// to them can be read off directly.
HistogramPaintColours pure_colours() {
    HistogramPaintColours c;
    c.plot = {0, 0, 0, 255};
    c.luminance = {0, 0, 0, 0};
    c.red = {255, 0, 0, 255};
    c.green = {0, 255, 0, 255};
    c.blue = {0, 0, 255, 255};
    c.grid = {0, 0, 0, 0};
    c.shadows_lit = {1, 2, 3, 255};
    c.highlights_lit = {4, 5, 6, 255};
    c.triangle_unlit = {7, 8, 9, 255};
    return c;
}

// Sets the inner bins of `counts` to `value` (the end bins are left out of the scale anyway).
void fill(StageHistogram::Counts& counts, std::uint64_t value) {
    std::fill(counts.begin() + 1, counts.end() - 1, value);
}

struct Picture {
    std::vector<std::uint8_t> bytes;
    int width;

    // The bin's column, `up` rows above the bottom row of a plain_size() picture.
    [[nodiscard]] std::array<int, 4> fill_at(int bin, int up) const {
        return at(inset_x + bin, picture_height - 1 - up);
    }
    [[nodiscard]] std::array<int, 4> at(int x, int y) const {
        const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
        return {bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]};
    }
};

Picture painted(const StageHistogram& h, const HistogramPaintSize& size,
                const HistogramPaintColours& colours) {
    Picture p{{}, size.width};
    paint_histogram(h, size, colours, p.bytes);
    return p;
}

Picture painted(const StageHistogram& h) {
    return painted(h, plain_size(), pure_colours());
}

using Bytes = std::array<int, 4>;

}  // namespace

TEST(HistogramPaint, MakesFourBytesPerPixel) {
    const Picture p = painted(StageHistogram{});
    EXPECT_EQ(p.bytes.size(), static_cast<std::size_t>(picture_width * picture_height * 4));
}

TEST(HistogramPaint, EmptyIsThePlotColourEverywhere) {
    const Picture p = painted(StageHistogram{});
    for (int y = 0; y < picture_height; ++y) {
        for (int x = 0; x < picture_width; ++x) {
            ASSERT_EQ(p.at(x, y), (Bytes{0, 0, 0, 255})) << x << " " << y;
        }
    }
}

TEST(HistogramPaint, OneChannelAloneStandsOnTheBottomEdgeAtItsHeight) {
    StageHistogram h;
    fill(h.red, 60);
    h.red[100] = 100;  // the tallest: the full 100 rows
    const Picture p = painted(h);
    EXPECT_EQ(p.fill_at(50, 0), (Bytes{255, 0, 0, 255}));
    EXPECT_EQ(p.fill_at(50, 59), (Bytes{255, 0, 0, 255}));
    EXPECT_EQ(p.fill_at(50, 60), (Bytes{0, 0, 0, 255}));
    EXPECT_EQ(p.fill_at(100, 99), (Bytes{255, 0, 0, 255}));
    // Outside the side insets, and in the top inset, nothing.
    EXPECT_EQ(p.at(inset_x - 1, picture_height - 1), (Bytes{0, 0, 0, 255}));
    EXPECT_EQ(p.at(picture_width - inset_x, picture_height - 1), (Bytes{0, 0, 0, 255}));
    EXPECT_EQ(p.at(inset_x + 100, inset_top - 1), (Bytes{0, 0, 0, 255}));
}

TEST(HistogramPaint, TwoOverlappingChannelsReadAsTheirSecondary) {
    const auto overlap = [](auto first, auto second) {
        StageHistogram h;
        fill(h.*first, 100);
        fill(h.*second, 100);
        return painted(h).fill_at(50, 10);
    };
    EXPECT_EQ(overlap(&StageHistogram::red, &StageHistogram::green), (Bytes{255, 255, 0, 255}));
    EXPECT_EQ(overlap(&StageHistogram::green, &StageHistogram::blue), (Bytes{0, 255, 255, 255}));
    EXPECT_EQ(overlap(&StageHistogram::red, &StageHistogram::blue), (Bytes{255, 0, 255, 255}));
}

TEST(HistogramPaint, ThreeOverlappingChannelsReadAsWhite) {
    StageHistogram h;
    fill(h.red, 100);
    fill(h.green, 100);
    fill(h.blue, 100);
    h.blue[50] = 50;
    const Picture p = painted(h);
    EXPECT_EQ(p.fill_at(40, 10), (Bytes{255, 255, 255, 255}));
    // Above blue's top at bin 50 only red and green are left.
    EXPECT_EQ(p.fill_at(50, 49), (Bytes{255, 255, 255, 255}));
    EXPECT_EQ(p.fill_at(50, 50), (Bytes{255, 255, 0, 255}));
}

TEST(HistogramPaint, ScreenIsTheFormulaOnPartColours) {
    // 1 - (1 - a)(1 - b): 100 and 100 of 255 make 161, not 200 as adding would.
    HistogramPaintColours c = pure_colours();
    c.red = {100, 0, 0, 255};
    c.green = {100, 0, 0, 255};
    StageHistogram h;
    fill(h.red, 100);
    fill(h.green, 100);
    EXPECT_EQ(painted(h, plain_size(), c).fill_at(50, 10), (Bytes{161, 0, 0, 255}));
}

TEST(HistogramPaint, APartCoveredTopPixelTakesItsShare) {
    HistogramPaintSize size = plain_size();
    size.height = inset_top + 10;
    StageHistogram h;
    fill(h.red, 100);
    h.red[50] = 25;  // 2.5 of 10 rows
    const Picture p = painted(h, size, pure_colours());
    const auto at_up = [&](int up) { return p.at(inset_x + 50, size.height - 1 - up); };
    EXPECT_EQ(at_up(1), (Bytes{255, 0, 0, 255}));
    EXPECT_EQ(at_up(2), (Bytes{127, 0, 0, 255}));  // 255 * 128 / 256
    EXPECT_EQ(at_up(3), (Bytes{0, 0, 0, 255}));
}

TEST(HistogramPaint, AnEndBinTallerThanTheInnerOnesStopsAtTheTop) {
    StageHistogram h;
    fill(h.green, 5);
    h.green[40] = 10;
    h.green[0] = 1000;
    h.green[255] = 1000;
    const Picture p = painted(h);
    EXPECT_EQ(p.fill_at(0, 99), (Bytes{0, 255, 0, 255}));
    EXPECT_EQ(p.fill_at(255, 99), (Bytes{0, 255, 0, 255}));
    // The inner bins set the scale: 5 of 10 is half the height, not 5 of 1000.
    EXPECT_EQ(p.fill_at(128, 49), (Bytes{0, 255, 0, 255}));
    EXPECT_EQ(p.fill_at(128, 50), (Bytes{0, 0, 0, 255}));
}

TEST(HistogramPaint, OnlyTheEndBinsStillDrawSomething) {
    StageHistogram h;
    h.red[0] = 10;
    const Picture p = painted(h);
    EXPECT_EQ(p.fill_at(0, 99), (Bytes{255, 0, 0, 255}));
}

TEST(HistogramPaint, AHugeCountDoesNotOverflow) {
    StageHistogram h;
    const std::uint64_t full = std::uint64_t{1} << 62;
    h.red[10] = full;
    h.red[20] = full / 2;
    const Picture p = painted(h);
    EXPECT_EQ(p.fill_at(10, 99), (Bytes{255, 0, 0, 255}));
    EXPECT_EQ(p.fill_at(20, 49), (Bytes{255, 0, 0, 255}));
    EXPECT_EQ(p.fill_at(20, 50), (Bytes{0, 0, 0, 255}));
}

TEST(HistogramPaint, TheDesignsColoursGiveTheseBytes) {
    // Dark theme: plot #0d0d0f. Red fill rgba(255, 92, 80, .30) screened over it.
    const HistogramPaintColours c = histogram_paint_colours(Theme::dark);
    EXPECT_EQ(c.plot, (Rgba8{13, 13, 15, 255}));
    HistogramPaintSize size = plain_size();
    StageHistogram h;
    fill(h.red, 100);
    EXPECT_EQ(painted(h, size, c).fill_at(50, 10), (Bytes{86, 40, 38, 255}));
    // All three, on top of the luminance's grey: at the design's strengths a light neutral. (Worked
    // by hand: grey (54, 54, 57), then the three screens in turn.)
    fill(h.green, 100);
    fill(h.blue, 100);
    fill(h.luminance, 100);
    EXPECT_EQ(painted(h, size, c).fill_at(50, 10), (Bytes{142, 141, 147, 255}));
    // The plot is dark in both themes.
    EXPECT_EQ(histogram_paint_colours(Theme::light).plot, (Rgba8{21, 21, 23, 255}));
}

TEST(HistogramPaint, GridLinesAreAtTheQuartersAndBehindTheFills) {
    HistogramPaintColours c = pure_colours();
    c.grid = {255, 255, 255, 255};
    c.red = {100, 0, 0, 255};
    HistogramPaintSize size = plain_size();
    StageHistogram h;
    h.red[1] = 100;
    const Picture p = painted(h, size, c);
    const int bottom = picture_height - 1;
    for (const int x : {inset_x + 64, inset_x + 128, inset_x + 192}) {
        EXPECT_EQ(p.at(x, bottom), (Bytes{255, 255, 255, 255})) << x;
        EXPECT_EQ(p.at(x + 1, bottom), (Bytes{0, 0, 0, 255})) << x;
        EXPECT_EQ(p.at(x, inset_top), (Bytes{255, 255, 255, 255})) << x;
        EXPECT_EQ(p.at(x, inset_top - 1), (Bytes{0, 0, 0, 255})) << x;  // not in the top inset
    }
    // A fill over a line: screened on it.
    StageHistogram over_line;
    fill(over_line.red, 100);
    c.grid = {100, 100, 100, 255};
    const Picture q = painted(over_line, size, c);
    EXPECT_EQ(q.at(inset_x + 64, bottom), (Bytes{161, 100, 100, 255}));
}

TEST(HistogramPaint, APixelOnAGridLineStillBlendsWithTheLine) {
    // Off a line the bytes come from a table: on one they must not.
    const HistogramPaintColours c = histogram_paint_colours(Theme::dark);
    StageHistogram h;
    fill(h.red, 100);
    const Picture p = painted(h, plain_size(), c);
    const int bottom = picture_height - 1;
    EXPECT_EQ(p.at(inset_x + 63, bottom), (Bytes{86, 40, 38, 255}));
    EXPECT_NE(p.at(inset_x + 64, bottom), (Bytes{86, 40, 38, 255}));
}

namespace {

HistogramPaintSize triangle_size() {
    HistogramPaintSize s = plain_size();
    s.triangle_width = 14;
    s.triangle_height = 9;
    return s;
}

// The left edge of each triangle's 14 x 9 box, whose top is at the top inset.
constexpr int left_x = inset_x;
constexpr int right_x = picture_width - inset_x - 14;

}  // namespace

TEST(HistogramPaint, TheTrianglesAreUnlitWhenTheirEndHasNoPixels) {
    const Picture p = painted(StageHistogram{}, triangle_size(), pure_colours());
    const Bytes unlit{7, 8, 9, 255};
    EXPECT_EQ(p.at(left_x + 7, inset_top + 8), unlit);
    EXPECT_EQ(p.at(right_x + 7, inset_top + 8), unlit);
}

TEST(HistogramPaint, EachTriangleLightsForItsOwnEnd) {
    const Bytes unlit{7, 8, 9, 255};
    StageHistogram below;
    below.any_under_black = 1;
    const Picture a = painted(below, triangle_size(), pure_colours());
    EXPECT_EQ(a.at(left_x + 7, inset_top + 8), (Bytes{1, 2, 3, 255}));
    EXPECT_EQ(a.at(right_x + 7, inset_top + 8), unlit);

    StageHistogram above;
    above.any_over_white = 1;
    const Picture b = painted(above, triangle_size(), pure_colours());
    EXPECT_EQ(b.at(left_x + 7, inset_top + 8), unlit);
    EXPECT_EQ(b.at(right_x + 7, inset_top + 8), (Bytes{4, 5, 6, 255}));
}

TEST(HistogramPaint, TheTriangleHasItsTipUpAndStaysInItsBox) {
    StageHistogram h;
    h.any_under_black = 1;
    const Picture p = painted(h, triangle_size(), pure_colours());
    const Bytes clear{0, 0, 0, 255};
    const Bytes lit{1, 2, 3, 255};
    EXPECT_EQ(p.at(left_x, inset_top), clear);           // beside the tip
    EXPECT_EQ(p.at(left_x + 13, inset_top), clear);
    EXPECT_NE(p.at(left_x + 7, inset_top), clear);       // the tip
    EXPECT_EQ(p.at(left_x + 7, inset_top + 4), lit);     // its middle
    EXPECT_EQ(p.at(left_x + 2, inset_top + 8), lit);     // towards the ends of the base
    EXPECT_EQ(p.at(left_x + 11, inset_top + 8), lit);
    EXPECT_EQ(p.at(left_x - 1, inset_top + 8), clear);   // outside the box
    EXPECT_EQ(p.at(left_x + 14, inset_top + 8), clear);
    EXPECT_EQ(p.at(left_x + 7, inset_top + 9), clear);
}

TEST(HistogramPaint, TheDesignsTrianglesAreSolidOrFaint) {
    const HistogramPaintColours c = histogram_paint_colours(Theme::dark);
    HistogramPaintSize size = triangle_size();
    StageHistogram h;
    h.any_over_white = 1;
    const Picture p = painted(h, size, c);
    EXPECT_EQ(p.at(right_x + 7, inset_top + 8), (Bytes{0xff, 0x7a, 0x3d, 255}));
    // Unlit: white at .22 over #0d0d0f.
    EXPECT_EQ(p.at(left_x + 7, inset_top + 8), (Bytes{66, 66, 68, 255}));
}

TEST(HistogramPaint, ASizeWithNoRoomDoesNotCrash) {
    StageHistogram h;
    fill(h.red, 100);
    h.any_under_black = h.any_over_white = 1;
    std::vector<std::uint8_t> bytes;
    for (const auto& [w, ht] : {std::array<int, 2>{0, 0}, {1, 1}, {4, 200}, {200, 3}, {30, 12}}) {
        const HistogramPaintSize s = prism::gui::histogram_paint_size(w, ht, 2.0f);
        paint_histogram(h, s, histogram_paint_colours(Theme::dark), bytes);
        EXPECT_EQ(bytes.size(), static_cast<std::size_t>(w * ht * 4)) << w << " " << ht;
    }
}

TEST(HistogramPaintSizes, AreTheDesignsScaledToThePicture) {
    const HistogramPaintSize one = prism::gui::histogram_paint_size(400, 160, 1.0f);
    EXPECT_EQ(one.inset_x, 12);  // 10 mockup px
    EXPECT_EQ(one.inset_top, 10);
    EXPECT_EQ(one.triangle_width, 17);
    EXPECT_EQ(one.triangle_height, 11);
    EXPECT_EQ(one.grid_line, 1);
    const HistogramPaintSize two = prism::gui::histogram_paint_size(800, 320, 2.0f);
    EXPECT_EQ(two.inset_x, 25);
    EXPECT_EQ(two.triangle_width, 34);
    EXPECT_EQ(two.grid_line, 2);
}

TEST(HistogramPaintCacheTest, PaintsOnlyWhenWhatItShowsChanges) {
    HistogramPaintCache cache;
    StageHistogram h;
    fill(h.red, 100);
    const HistogramPaintSize size = prism::gui::histogram_paint_size(300, 120, 1.0f);
    const HistogramPaintColours dark = histogram_paint_colours(Theme::dark);
    EXPECT_TRUE(cache.update(h, size, dark));
    EXPECT_FALSE(cache.update(h, size, dark));

    const std::vector<std::uint8_t> first = cache.pixels();
    StageHistogram other = h;
    other.green[40] = 3;
    EXPECT_TRUE(cache.update(other, size, dark));
    EXPECT_NE(cache.pixels(), first);
    EXPECT_FALSE(cache.update(other, size, dark));

    StageHistogram lit = other;
    lit.any_over_white = 1;
    EXPECT_TRUE(cache.update(lit, size, dark));

    // The size and the scale.
    EXPECT_TRUE(cache.update(lit, prism::gui::histogram_paint_size(301, 120, 1.0f), dark));
    EXPECT_TRUE(cache.update(lit, prism::gui::histogram_paint_size(301, 121, 1.0f), dark));
    EXPECT_TRUE(cache.update(lit, prism::gui::histogram_paint_size(301, 121, 2.0f), dark));
    // The theme.
    EXPECT_TRUE(cache.update(lit, prism::gui::histogram_paint_size(301, 121, 2.0f),
                             histogram_paint_colours(Theme::light)));
    EXPECT_FALSE(cache.update(lit, prism::gui::histogram_paint_size(301, 121, 2.0f),
                              histogram_paint_colours(Theme::light)));
}

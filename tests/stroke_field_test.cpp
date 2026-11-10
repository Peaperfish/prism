#include "prism/stroke_field.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using prism::FieldPoint;
using prism::FieldStroke;
using prism::GridRect;
using prism::PixelGrid;
using prism::RasterStats;
using prism::StrokeField;

std::shared_ptr<const FieldStroke> stroke(std::vector<FieldPoint> points, float radius,
                                          float softness, float weight) {
    return std::make_shared<const FieldStroke>(FieldStroke{std::move(points), radius, softness, weight});
}

std::vector<float> rasterise(const StrokeField& f, const PixelGrid& g, RasterStats* stats = nullptr) {
    std::vector<float> out(static_cast<std::size_t>(g.width()) * static_cast<std::size_t>(g.height()));
    prism::rasterise_field(f, g, 0, g.height(), out, stats);
    return out;
}

// Index of the first value whose bits differ, or -1.
long first_bit_difference(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        return 0;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i])) {
            return static_cast<long>(i);
        }
    }
    return -1;
}

// A field with soft, hard, fully soft, one-point and multi-point strokes, some reaching off the
// edges, and weights chosen so the sum's rounding depends on its order.
StrokeField sample_field(float aspect) {
    StrokeField f;
    f.aspect = aspect;
    f.strokes.push_back(stroke({{0.2f, 0.3f}, {0.5f, 0.35f}, {0.7f, 0.6f}}, 0.08f, 0.5f, 0.7123f));
    f.strokes.push_back(stroke({{0.45f, 0.5f}}, 0.12f, 1.0f, -1.3711f));
    f.strokes.push_back(stroke({{0.9f, 0.1f}, {1.05f, 0.4f}}, 0.06f, 0.0f, 0.3333f));
    f.strokes.push_back(stroke({{0.1f, 0.9f}, {0.6f, 0.2f}, {0.3f, 0.2f}, {0.55f, 0.85f}}, 0.05f,
                               0.3f, -0.2171f));
    return f;
}

// --- The slow reference: the formula, in double, pixel by pixel, over the stroke's real segments
// (no extra disc for the first point: it is inside the first capsule anyway).

struct RefPixel {
    double value = 0;
    bool near_edge = false;  // within 1e-5 of r or r_in: float and double may disagree on a side
};

RefPixel reference_at(const StrokeField& f, int x, int y, int w, int h) {
    const double aspect = f.aspect;
    const double sx = std::min(aspect, 1.0);
    const double sy = std::min(1.0 / aspect, 1.0);
    const double X = (x + 0.5) / w * sx;
    const double Y = (y + 0.5) / h * sy;
    RefPixel out;
    for (const auto& s : f.strokes) {
        const double r = s->radius;
        const double r_in = r * (1.0 - s->softness);
        double best = 0;
        const std::size_t n = s->points.size();
        for (std::size_t k = 0; k < std::max<std::size_t>(n, 2) - 1; ++k) {
            const auto& a = s->points[k];
            const auto& b = s->points[std::min(k + 1, n - 1)];
            const double ax = a.u * sx, ay = a.v * sy;
            const double dx = b.u * sx - ax, dy = b.v * sy - ay;
            const double len2 = dx * dx + dy * dy;
            const double t =
                len2 > 0 ? std::clamp(((X - ax) * dx + (Y - ay) * dy) / len2, 0.0, 1.0) : 0.0;
            const double d = std::hypot(X - ax - t * dx, Y - ay - t * dy);
            if (std::abs(d - r) < 1e-5 || std::abs(d - r_in) < 1e-5) {
                out.near_edge = true;
            }
            double c;
            if (d <= r_in) {
                c = 1;
            } else if (d >= r) {
                c = 0;
            } else {
                const double u = (d - r_in) / (r - r_in);
                c = 1 - u * u * (3 - 2 * u);
            }
            best = std::max(best, c);
        }
        out.value += s->weight * best;
    }
    return out;
}

TEST(StrokeField, RasterEqualsPointwiseAtAnySize) {
    struct Size {
        int w, h;
    };
    // Landscape, portrait, square and odd sizes.
    for (const Size sz : {Size{64, 48}, Size{97, 131}, Size{200, 200}, Size{33, 257},
                          Size{300, 120}}) {
        const StrokeField f = sample_field(prism::aspect_of(sz.w, sz.h));
        const std::vector<float> got = rasterise(f, PixelGrid::whole(sz.w, sz.h));
        int compared = 0;
        int nonzero = 0;
        for (int y = 0; y < sz.h; ++y) {
            for (int x = 0; x < sz.w; ++x) {
                const RefPixel ref = reference_at(f, x, y, sz.w, sz.h);
                if (ref.near_edge) {
                    continue;
                }
                const float g = got[static_cast<std::size_t>(y) * sz.w + x];
                ASSERT_NEAR(g, ref.value, 1e-4)
                    << sz.w << "x" << sz.h << " at (" << x << ", " << y << ")";
                ++compared;
                nonzero += ref.value != 0;
            }
        }
        EXPECT_GT(compared, sz.w * sz.h * 9 / 10) << sz.w << "x" << sz.h;
        EXPECT_GT(nonzero, sz.w * sz.h / 10) << sz.w << "x" << sz.h;
    }
}

TEST(StrokeField, SameStrokeSameAreaAtAnySize) {
    // The same normalised stroke on a small and a 4x larger image covers the same fraction of it:
    // mean coverage agrees to 0.5% (relative). Measured: well under 0.1%.
    StrokeField f;
    f.aspect = 1.5f;
    f.strokes.push_back(stroke({{0.3f, 0.4f}, {0.6f, 0.55f}, {0.7f, 0.3f}}, 0.04f, 0.6f, 1.0f));
    const auto mean = [&](int w, int h) {
        const std::vector<float> v = rasterise(f, PixelGrid::whole(w, h));
        double sum = 0;
        for (const float x : v) {
            sum += x;
        }
        return sum / static_cast<double>(v.size());
    };
    const double small = mean(600, 400);
    const double large = mean(2400, 1600);
    EXPECT_GT(small, 0.01);
    EXPECT_NEAR(small / large, 1.0, 0.005) << small << " vs " << large;
}

// --- Grids: every way of asking for a pixel gives the same bits as the whole-image field.

class StrokeFieldGrid : public ::testing::Test {
protected:
    static constexpr int W = 157;
    static constexpr int H = 103;
    StrokeField f = sample_field(prism::aspect_of(W, H));
    std::vector<float> whole = rasterise(f, PixelGrid::whole(W, H));

    float at(int x, int y) const { return whole[static_cast<std::size_t>(y) * W + x]; }

    // The whole field sampled at the grid's pixels.
    std::vector<float> expected(const PixelGrid& g) const {
        std::vector<float> e;
        for (int j = 0; j < g.height(); ++j) {
            for (int i = 0; i < g.width(); ++i) {
                e.push_back(at(g.x0 + i * g.stride, g.y0 + j * g.stride));
            }
        }
        return e;
    }
};

TEST_F(StrokeFieldGrid, BandIsBitIdenticalToTheWholeImage) {
    for (const int y : {0, 1, 40, 77, H - 1}) {
        for (const int x : {0, 3, 60}) {
            const PixelGrid g{W, H, x, y, 1};
            EXPECT_EQ(first_bit_difference(rasterise(f, g), expected(g)), -1)
                << "band from (" << x << ", " << y << ")";
        }
    }
}

TEST_F(StrokeFieldGrid, CroppedBandThatStopsShortOfTheEdgesIsBitIdenticalToTheWholeImage) {
    // A test strip band: a rectangle of the picture that reaches neither the right nor bottom edge.
    for (const PixelGrid g : {PixelGrid{W, H, 20, 0, 1, 31, 0}, PixelGrid{W, H, 0, 41, 1, 0, 17},
                              PixelGrid{W, H, 50, 30, 1, 40, 25}, PixelGrid{W, H, 3, 4, 3, 10, 9},
                              PixelGrid{W, H, 5, 5, 1, 1000, 1000}}) {
        ASSERT_LE(g.width(), (W - g.x0 + g.stride - 1) / g.stride);
        const std::vector<float> got = rasterise(f, g);
        ASSERT_EQ(got.size(), static_cast<std::size_t>(g.width()) * g.height());
        EXPECT_EQ(first_bit_difference(got, expected(g)), -1)
            << g.x0 << "," << g.y0 << " " << g.width() << "x" << g.height();
    }
    EXPECT_EQ((PixelGrid{W, H, 20, 0, 1, 31, 0}).width(), 31);
    EXPECT_EQ((PixelGrid{W, H, 20, 0, 1, 31, 0}).height(), H);
    EXPECT_EQ((PixelGrid{W, H, 5, 5, 1, 1000, 1000}).width(), W - 5);  // a limit past the edge
}

TEST_F(StrokeFieldGrid, StridedGridIsBitIdenticalToTheWholeImage) {
    for (const int stride : {2, 3, 4, 7}) {
        for (const int first : {0, 1, stride / 2, stride - 1}) {
            const PixelGrid g{W, H, first, first, stride};
            EXPECT_EQ(first_bit_difference(rasterise(f, g), expected(g)), -1)
                << "stride " << stride << " first " << first;
        }
    }
}

TEST_F(StrokeFieldGrid, SinglePixelIsBitIdenticalToTheWholeImage) {
    int nonzero = 0;
    for (int y = 0; y < H; y += 5) {
        for (int x = 0; x < W; x += 3) {
            // A grid of one pixel: a stride longer than the image leaves only (x, y).
            const PixelGrid one{W, H, x, y, std::max(W, H)};
            ASSERT_EQ(one.width(), 1);
            ASSERT_EQ(one.height(), 1);
            const std::vector<float> v = rasterise(f, one);
            ASSERT_EQ(std::bit_cast<std::uint32_t>(v[0]), std::bit_cast<std::uint32_t>(at(x, y)))
                << "(" << x << ", " << y << ")";
            nonzero += v[0] != 0.0f;
        }
    }
    EXPECT_GT(nonzero, 50);
}

TEST_F(StrokeFieldGrid, RowRangesSplitAnywhereAreBitIdentical) {
    for (const PixelGrid g : {PixelGrid::whole(W, H), PixelGrid{W, H, 1, 2, 3}}) {
        const int gh = g.height();
        const std::size_t gw = static_cast<std::size_t>(g.width());
        // Cuts at odd places, including an empty range and one-row ranges at both ends.
        for (const std::vector<int>& cuts :
             {std::vector<int>{0, 1, gh}, std::vector<int>{0, gh / 3, gh / 3 + 1, gh - 7, gh},
              std::vector<int>{0, gh - 1, gh}, std::vector<int>{0, 5, 5, gh / 2 + 3, gh}}) {
            std::vector<float> joined;
            for (std::size_t c = 0; c + 1 < cuts.size(); ++c) {
                std::vector<float> part(static_cast<std::size_t>(cuts[c + 1] - cuts[c]) * gw);
                prism::rasterise_field(f, g, cuts[c], cuts[c + 1], part);
                joined.insert(joined.end(), part.begin(), part.end());
            }
            EXPECT_EQ(first_bit_difference(joined, expected(g)), -1)
                << "stride " << g.stride << ", second cut " << cuts[1];
        }
    }
}

// One PreparedField rasterising tiles of rows equals rasterise_field, a tile that rows_untouched
// says no stroke reaches is all 0, and the per-tile cull evaluates exactly the pixels a whole-image
// pass does (no segment is evaluated twice or skipped).
TEST_F(StrokeFieldGrid, PreparedFieldTilesAreBitIdenticalAndCulled) {
    for (const PixelGrid g : {PixelGrid::whole(W, H), PixelGrid{W, H, 1, 2, 3}}) {
        const prism::PreparedField prepared(f, g);
        const int gh = g.height();
        const std::size_t gw = static_cast<std::size_t>(g.width());
        RasterStats whole_stats;
        (void)rasterise(f, g, &whole_stats);
        for (const int tile : {1, 7, 16}) {
            std::vector<float> joined;
            RasterStats stats;
            int untouched = 0;
            for (int t = 0; t < gh; t += tile) {
                const int end = std::min(t + tile, gh);
                std::vector<float> part(static_cast<std::size_t>(end - t) * gw);
                prepared.rasterise(t, end, part, &stats);
                if (prepared.rows_untouched(t, end)) {
                    ++untouched;
                    EXPECT_TRUE(std::all_of(part.begin(), part.end(),
                                            [](float v) { return v == 0.0f; }))
                        << "rows " << t << " to " << end;
                }
                joined.insert(joined.end(), part.begin(), part.end());
            }
            EXPECT_EQ(first_bit_difference(joined, expected(g)), -1)
                << "stride " << g.stride << ", tile " << tile;
            EXPECT_EQ(stats.evaluated, whole_stats.evaluated) << "tile " << tile;
            if (g.stride == 1 && tile == 1) {
                EXPECT_GT(untouched, 0);  // sample_field leaves some rows alone
            }
        }
    }
}

// --- The formula's properties.

TEST(StrokeField, SelfCrossingEqualsOnce) {
    // A hard-edged figure-of-eight that crosses itself, with sharp joints: every pixel it touches
    // is exactly the weight, crossings and joints included, never more.
    constexpr int W = 200, H = 200;
    StrokeField f;
    f.aspect = 1.0f;
    f.strokes.push_back(stroke({{0.2f, 0.2f}, {0.8f, 0.8f}, {0.8f, 0.2f}, {0.2f, 0.8f}, {0.25f, 0.2f}},
                               0.05f, 0.0f, 0.75f));
    const std::vector<float> v = rasterise(f, PixelGrid::whole(W, H));
    int covered = 0;
    for (const float x : v) {
        ASSERT_TRUE(x == 0.0f || x == 0.75f) << x;
        covered += x != 0.0f;
    }
    EXPECT_GT(covered, 1000);
    // The crossing at the centre and a joint.
    EXPECT_EQ(v[100 * W + 100], 0.75f);
    EXPECT_EQ(v[static_cast<std::size_t>(0.8 * H) * W + static_cast<std::size_t>(0.8 * W) - 1], 0.75f);

    // A soft stroke back over itself: the same as drawing it once, not doubled. (Not bit for
    // bit: the return segment measures from its other end, which rounds differently.)
    StrokeField once;
    once.aspect = 1.0f;
    once.strokes.push_back(stroke({{0.3f, 0.5f}, {0.7f, 0.5f}}, 0.1f, 0.8f, 1.0f));
    StrokeField twice;
    twice.aspect = 1.0f;
    twice.strokes.push_back(stroke({{0.3f, 0.5f}, {0.7f, 0.5f}, {0.3f, 0.5f}}, 0.1f, 0.8f, 1.0f));
    const std::vector<float> a = rasterise(once, PixelGrid::whole(W, H));
    const std::vector<float> b = rasterise(twice, PixelGrid::whole(W, H));
    for (std::size_t p = 0; p < a.size(); ++p) {
        ASSERT_NEAR(a[p], b[p], 1e-5) << p;
    }
}

TEST(StrokeField, TwoStrokesAdd) {
    constexpr int W = 180, H = 120;
    const StrokeField all = sample_field(prism::aspect_of(W, H));
    std::vector<std::vector<float>> each;
    for (const auto& s : all.strokes) {
        StrokeField one;
        one.aspect = all.aspect;
        one.strokes.push_back(s);
        each.push_back(rasterise(one, PixelGrid::whole(W, H)));
    }
    // Two strokes: exactly the sum.
    StrokeField two;
    two.aspect = all.aspect;
    two.strokes = {all.strokes[0], all.strokes[1]};
    const std::vector<float> got2 = rasterise(two, PixelGrid::whole(W, H));
    int overlap = 0;
    for (std::size_t p = 0; p < got2.size(); ++p) {
        ASSERT_EQ(got2[p], each[0][p] + each[1][p]) << p;
        overlap += each[0][p] != 0.0f && each[1][p] != 0.0f;
    }
    EXPECT_GT(overlap, 100);
    // All four: summed left to right, in stroke order, bit for bit.
    const std::vector<float> got = rasterise(all, PixelGrid::whole(W, H));
    std::vector<float> want(got.size());
    for (std::size_t p = 0; p < got.size(); ++p) {
        float acc = 0.0f;
        for (const auto& e : each) {
            acc = acc + e[p];
        }
        want[p] = acc;
    }
    EXPECT_EQ(first_bit_difference(got, want), -1);
}

TEST(StrokeField, SoftnessZeroIsAHardEdge) {
    constexpr int W = 100, H = 100;
    StrokeField f;
    f.aspect = 1.0f;
    f.strokes.push_back(stroke({{0.5f, 0.5f}}, 0.2f, 0.0f, -2.0f));
    const std::vector<float> v = rasterise(f, PixelGrid::whole(W, H));
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double d = std::hypot((x + 0.5) / W - 0.5, (y + 0.5) / H - 0.5);
            const float want = d < 0.2 - 1e-6 ? -2.0f : (d > 0.2 + 1e-6 ? 0.0f : v[y * W + x]);
            ASSERT_EQ(v[y * W + x], want) << x << ", " << y;
            ASSERT_TRUE(v[y * W + x] == 0.0f || v[y * W + x] == -2.0f);
        }
    }
}

TEST(StrokeField, SoftnessOneFadesFromTheCentre) {
    constexpr int W = 101, H = 101;
    StrokeField f;
    f.aspect = 1.0f;
    // Centred on pixel 50's centre, so that pixel is at distance 0.
    f.strokes.push_back(stroke({{50.5f / W, 50.5f / H}}, 0.3f, 1.0f, 1.0f));
    const std::vector<float> v = rasterise(f, PixelGrid::whole(W, H));
    const float* row = &v[50 * W];
    EXPECT_EQ(row[50], 1.0f);
    // Strictly decreasing out to the radius, then 0.
    for (int x = 51; x < W; ++x) {
        const double d = (x - 50) / static_cast<double>(W);
        if (d < 0.3) {
            ASSERT_LT(row[x], row[x - 1]) << x;
            ASSERT_GT(row[x], 0.0f) << x;
        } else {
            ASSERT_EQ(row[x], 0.0f) << x;
        }
    }
    // Halfway out, smoothstep gives exactly one half.
    EXPECT_NEAR(row[50 + 15], 1.0 - 0.5 * 0.5 * (3 - 2 * 0.5), 0.05);
}

TEST(StrokeField, StrokeWhollyOffTheImageGivesAZeroField) {
    StrokeField f;
    f.aspect = 1.5f;
    f.strokes.push_back(stroke({{1.2f, 0.5f}, {1.5f, 0.2f}}, 0.1f, 0.5f, 1.0f));
    f.strokes.push_back(stroke({{-0.3f, -0.3f}}, 0.2f, 0.0f, -1.0f));
    RasterStats stats;
    const std::vector<float> v = rasterise(f, PixelGrid::whole(150, 100), &stats);
    for (const float x : v) {
        ASSERT_EQ(std::bit_cast<std::uint32_t>(x), 0u);  // +0, not -0
    }
    EXPECT_EQ(stats.evaluated, 0u);
    EXPECT_TRUE(prism::segment_bounds(*f.strokes[0], 1, f.aspect, PixelGrid::whole(150, 100)).empty());
}

TEST(StrokeField, PointSpacing) {
    // 0.15 r for an ordinary brush, one pixel for a tiny one.
    EXPECT_FLOAT_EQ(prism::min_point_spacing(0.03f, 2560), 0.0045f);
    EXPECT_FLOAT_EQ(prism::min_point_spacing(0.001f, 2560), 1.0f / 2560);
    const float s = prism::min_point_spacing(0.03f, 2560);
    // Landscape 3:2: u is in long-side units, v is 2/3 of them.
    EXPECT_FALSE(prism::far_enough({0.5f, 0.5f}, {0.5f + 0.004f, 0.5f}, 1.5f, s));
    EXPECT_TRUE(prism::far_enough({0.5f, 0.5f}, {0.5f + 0.005f, 0.5f}, 1.5f, s));
    EXPECT_FALSE(prism::far_enough({0.5f, 0.5f}, {0.5f, 0.5f + 0.006f}, 1.5f, s));  // 0.004
    EXPECT_TRUE(prism::far_enough({0.5f, 0.5f}, {0.5f, 0.5f + 0.0072f}, 1.5f, s));  // 0.0048
    // Portrait 2:3: the other way round.
    EXPECT_FALSE(prism::far_enough({0.5f, 0.5f}, {0.5f + 0.006f, 0.5f}, 2.0f / 3, s));
    EXPECT_TRUE(prism::far_enough({0.5f, 0.5f}, {0.5f, 0.5f + 0.005f}, 2.0f / 3, s));
    // Diagonal: Euclidean in long-side units (0.003, 0.004) -> 0.005.
    EXPECT_TRUE(prism::far_enough({0.5f, 0.5f}, {0.503f, 0.504f}, 1.0f, s));
    EXPECT_FALSE(prism::far_enough({0.5f, 0.5f}, {0.502f, 0.5035f}, 1.0f, s));
}

TEST(StrokeField, RasteriseOnlyInsideBounds) {
    constexpr int W = 400, H = 300;
    const PixelGrid g = PixelGrid::whole(W, H);
    StrokeField f;
    f.aspect = prism::aspect_of(W, H);
    // Hard-edged, so the outermost covered pixels are full strength and a box one pixel short
    // would visibly lose them.
    f.strokes.push_back(stroke({{0.3f, 0.4f}, {0.5f, 0.45f}}, 0.05f, 0.0f, 1.0f));
    const FieldStroke& s = *f.strokes[0];

    const GridRect disc = prism::segment_bounds(s, 0, f.aspect, g);
    const GridRect seg = prism::segment_bounds(s, 1, f.aspect, g);
    // A disc of radius 0.05 * 400 = 20 px: the tight box is at most 41 px, plus one each side.
    EXPECT_LE(disc.i_end - disc.i_begin, 43);
    EXPECT_LE(disc.j_end - disc.j_begin, 43);
    EXPECT_GE(disc.i_end - disc.i_begin, 40);

    // The whole-field rasteriser evaluates exactly the segments' boxes.
    RasterStats stats;
    const std::vector<float> v = rasterise(f, g, &stats);
    EXPECT_EQ(stats.evaluated, disc.area() + seg.area());

    // Every covered pixel is inside a box, and the boxes are not loose: covered pixels touch
    // within one margin pixel of each edge of the segment's box.
    const auto inside = [](const GridRect& r, int x, int y) {
        return x >= r.i_begin && x < r.i_end && y >= r.j_begin && y < r.j_end;
    };
    int min_x = W, max_x = -1, min_y = H, max_y = -1;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const bool ref = reference_at(f, x, y, W, H).value != 0.0;
            ASSERT_EQ(v[static_cast<std::size_t>(y) * W + x] != 0.0f, ref) << x << ", " << y;
            if (ref) {
                ASSERT_TRUE(inside(disc, x, y) || inside(seg, x, y)) << x << ", " << y;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }
    EXPECT_LE(min_x - seg.i_begin, 2);
    EXPECT_LE(seg.i_end - 1 - max_x, 2);

    // Exactly one pixel of margin: a hard dab centred on pixel (100, 150) with a radius of
    // 20.48 px covers columns 80..120 and rows 130..170 (the pixels within 20 px of the centre
    // along its row and column), so its box is columns [79, 122) and rows [129, 172).
    StrokeField dab;
    dab.aspect = f.aspect;
    dab.strokes.push_back(stroke({{100.5f / W, 150.5f / H}}, 20.48f / W, 0.0f, 1.0f));
    const GridRect dab_box = prism::segment_bounds(*dab.strokes[0], 0, dab.aspect, g);
    EXPECT_EQ(dab_box, (GridRect{79, 122, 129, 172}));
    const std::vector<float> dv = rasterise(dab, g);
    EXPECT_EQ(dv[150 * W + 80], 1.0f);
    EXPECT_EQ(dv[150 * W + 120], 1.0f);
    EXPECT_EQ(dv[130 * W + 100], 1.0f);
    EXPECT_EQ(dv[170 * W + 100], 1.0f);
    EXPECT_EQ(dv[150 * W + 79], 0.0f);
    EXPECT_EQ(dv[150 * W + 121], 0.0f);

    // The incremental call writes only inside the box it returns.
    std::vector<float> cov(static_cast<std::size_t>(W) * H, -1.0f);
    RasterStats one;
    const GridRect r = prism::add_segment_coverage(s, 1, f.aspect, g, cov, &one);
    EXPECT_EQ(r, seg);
    EXPECT_EQ(one.evaluated, seg.area());
    std::size_t written = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float c = cov[static_cast<std::size_t>(y) * W + x];
            if (c != -1.0f) {
                ++written;
                ASSERT_TRUE(inside(r, x, y)) << x << ", " << y;
            }
        }
    }
    EXPECT_EQ(written, r.area());
}

TEST(StrokeField, IncrementalEqualsFresh) {
    for (const PixelGrid g : {PixelGrid::whole(240, 160), PixelGrid{240, 160, 1, 0, 3}}) {
        const std::size_t n = static_cast<std::size_t>(g.width()) * static_cast<std::size_t>(g.height());
        StrokeField field = sample_field(prism::aspect_of(g.full_w, g.full_h));
        const std::vector<float> before = rasterise(field, g);

        // A soft stroke that grows a point at a time, crossing itself and the earlier strokes.
        const std::vector<FieldPoint> path = {{0.1f, 0.5f}, {0.4f, 0.3f}, {0.7f, 0.5f}, {0.6f, 0.8f},
                                              {0.4f, 0.6f}, {0.45f, 0.2f}, {0.9f, 0.4f}};
        FieldStroke growing{{}, 0.07f, 0.6f, -0.4567f};
        std::vector<float> coverage(n, 0.0f);
        std::vector<float> incremental = before;
        field.strokes.push_back(nullptr);
        for (std::size_t k = 0; k < path.size(); ++k) {
            growing.points.push_back(path[k]);
            const GridRect r = prism::add_segment_coverage(growing, k, field.aspect, g, coverage);
            prism::apply_stroke_coverage(before, growing.weight, coverage, g, r, incremental);

            field.strokes.back() = std::make_shared<const FieldStroke>(growing);
            EXPECT_EQ(first_bit_difference(incremental, rasterise(field, g)), -1)
                << "stride " << g.stride << ", after point " << k;
        }
    }
}

// The same growing stroke with its coverage kept only over a box of the grid (the preview's
// cache keeps the last stroke's coverage that way): bit-identical to the whole-grid coverage.
TEST(StrokeField, IncrementalInABoxEqualsFresh) {
    for (const PixelGrid g : {PixelGrid::whole(240, 160), PixelGrid{240, 160, 1, 0, 3}}) {
        StrokeField field = sample_field(prism::aspect_of(g.full_w, g.full_h));
        const std::vector<float> before = rasterise(field, g);
        const std::vector<FieldPoint> path = {{0.3f, 0.5f}, {0.5f, 0.3f}, {0.6f, 0.5f},
                                              {0.45f, 0.6f}, {0.4f, 0.35f}};
        FieldStroke growing{{}, 0.05f, 0.5f, 0.789f};
        // A box that holds every segment of the path, and not the whole grid.
        GridRect box;
        for (std::size_t k = 0; k < path.size(); ++k) {
            growing.points.push_back(path[k]);
            const GridRect r = prism::segment_bounds(growing, k, field.aspect, g);
            box = box.empty() ? r
                              : GridRect{std::min(box.i_begin, r.i_begin), std::max(box.i_end, r.i_end),
                                         std::min(box.j_begin, r.j_begin), std::max(box.j_end, r.j_end)};
        }
        ASSERT_LT(box.area(), static_cast<std::size_t>(g.width()) * static_cast<std::size_t>(g.height()));
        growing.points.clear();
        const PixelGrid sub = prism::sub_grid(g, box);
        ASSERT_EQ(sub.width(), box.i_end - box.i_begin);
        ASSERT_EQ(sub.height(), box.j_end - box.j_begin);

        std::vector<float> coverage(box.area(), 0.0f);
        std::vector<float> incremental = before;
        field.strokes.push_back(nullptr);
        for (std::size_t k = 0; k < path.size(); ++k) {
            growing.points.push_back(path[k]);
            GridRect r = prism::add_segment_coverage(growing, k, field.aspect, sub, coverage);
            r = {r.i_begin + box.i_begin, r.i_end + box.i_begin, r.j_begin + box.j_begin,
                 r.j_end + box.j_begin};
            EXPECT_EQ(r, prism::segment_bounds(growing, k, field.aspect, g));
            prism::apply_stroke_coverage(before, growing.weight, coverage, box, g, r, incremental);

            field.strokes.back() = std::make_shared<const FieldStroke>(growing);
            EXPECT_EQ(first_bit_difference(incremental, rasterise(field, g)), -1)
                << "stride " << g.stride << ", after point " << k;
        }
        // A rectangle outside the box, or a buffer of the wrong size, is refused.
        EXPECT_THROW(prism::apply_stroke_coverage(before, 1.0f, coverage, box, g,
                                                  GridRect{0, box.i_end, 0, 1}, incremental),
                     std::invalid_argument);
        coverage.pop_back();
        EXPECT_THROW(prism::apply_stroke_coverage(before, 1.0f, coverage, box, g, box, incremental),
                     std::invalid_argument);
    }
}

TEST(StrokeField, EqualityByPointerThenContent) {
    const auto s = stroke({{0.1f, 0.2f}}, 0.05f, 0.5f, 1.0f);
    StrokeField a;
    a.aspect = 1.5f;
    a.strokes = {s};
    StrokeField b = a;
    EXPECT_EQ(a, b);  // same pointer
    b.strokes = {stroke({{0.1f, 0.2f}}, 0.05f, 0.5f, 1.0f)};
    EXPECT_EQ(a, b);  // same content
    b.strokes = {stroke({{0.1f, 0.2f}}, 0.05f, 0.5f, -1.0f)};
    EXPECT_NE(a, b);
    b = a;
    b.aspect = 1.0f;
    EXPECT_NE(a, b);
    b = a;
    b.strokes.push_back(s);
    EXPECT_NE(a, b);
}

TEST(StrokeField, GainOfAnUntouchedPixelIsExactlyOne) {
    EXPECT_EQ(prism::stroke_gain(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(prism::stroke_gain(1.0f), 2.0f);
    EXPECT_FLOAT_EQ(prism::stroke_gain(-2.0f), 0.25f);
}

TEST(StrokeField, RejectsBadArguments) {
    const StrokeField f = sample_field(1.5f);
    std::vector<float> out(10);
    EXPECT_THROW(prism::rasterise_field(f, PixelGrid{0, 10, 0, 0, 1}, 0, 1, out),
                 std::invalid_argument);
    EXPECT_THROW(prism::rasterise_field(f, PixelGrid{10, 10, 0, 0, 0}, 0, 1, out),
                 std::invalid_argument);
    EXPECT_THROW(prism::rasterise_field(f, PixelGrid::whole(10, 10), 0, 2, out),
                 std::invalid_argument);  // buffer too small
    EXPECT_THROW(prism::rasterise_field(f, PixelGrid::whole(10, 10), 9, 11, out),
                 std::invalid_argument);  // rows past the end
    StrokeField no_aspect = f;
    no_aspect.aspect = 0.0f;
    EXPECT_THROW(prism::rasterise_field(no_aspect, PixelGrid::whole(10, 1), 0, 1, out),
                 std::invalid_argument);
    std::vector<float> cov(100);
    EXPECT_THROW((void)prism::add_segment_coverage(*f.strokes[0], 3, 1.5f, PixelGrid::whole(10, 10), cov),
                 std::invalid_argument);
}

}  // namespace

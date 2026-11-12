// The stroke field on a framed grid (stroke_field.hpp, GridFrame): cropped and turned, at any
// output scale, with the strokes still in whole-photo (u, v).

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "prism/dodge_burn.hpp"
#include "prism/frame.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/stroke_field.hpp"

namespace {

using prism::FieldPoint;
using prism::FieldStroke;
using prism::Frame;
using prism::FrameMap;
using prism::FramePoint;
using prism::GridRect;
using prism::PixelGrid;
using prism::RasterStats;
using prism::StrokeField;

std::shared_ptr<const FieldStroke> stroke(std::vector<FieldPoint> points, float radius,
                                          float softness, float weight) {
    return std::make_shared<const FieldStroke>(
        FieldStroke{std::move(points), radius, softness, weight});
}

std::vector<float> rasterise(const StrokeField& f, const PixelGrid& g,
                             RasterStats* stats = nullptr) {
    std::vector<float> out(static_cast<std::size_t>(g.width()) *
                           static_cast<std::size_t>(g.height()));
    prism::rasterise_field(f, g, 0, g.height(), out, stats);
    return out;
}

float at(const std::vector<float>& field, const PixelGrid& g, int i, int j) {
    return field[static_cast<std::size_t>(j) * static_cast<std::size_t>(g.width()) +
                 static_cast<std::size_t>(i)];
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

// Soft, hard, fully soft, one-point and multi-point strokes, some reaching off the photo's edges,
// with weights chosen so the sum's rounding depends on its order.
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

// One dab of `radius` at photo point (u, v), fully soft so it peaks at its centre and falls off
// with the distance alone.
StrokeField dab(int w, int h, double u, double v, float radius) {
    StrokeField f;
    f.aspect = prism::aspect_of(w, h);
    f.strokes.push_back(
        stroke({{static_cast<float>(u), static_cast<float>(v)}}, radius, 1.0f, 1.0f));
    return f;
}

Frame turned(float angle) {
    Frame f;
    f.angle = angle;
    return f;
}

// The framed grid of `frame` (fitted) on a w x h photo at `scale`: the image is the framed size
// divided by the scale, rounded.
PixelGrid fitted_grid(const Frame& frame, int w, int h, float scale = 1.0f) {
    const Frame fitted = prism::fit_frame(frame, w, h);
    const prism::PixelSize size = prism::framed_size(fitted, w, h);
    const int gw = std::max(1, static_cast<int>(std::lround(size.width / scale)));
    const int gh = std::max(1, static_cast<int>(std::lround(size.height / scale)));
    return PixelGrid::framed(fitted, w, h, scale, gw, gh);
}

// The grid pixel (i, j) where `field` is largest (the first, if several are).
struct Peak {
    int i = 0;
    int j = 0;
};
Peak peak_of(const std::vector<float>& field, const PixelGrid& g) {
    const auto it = std::max_element(field.begin(), field.end());
    const int k = static_cast<int>(it - field.begin());
    return {k % g.width(), k / g.width()};
}

// --- 1, 2: at 0 degrees the framed grid is the existing grid.

// A whole-photo frame at 0 degrees and scale 1 gives the very bits of PixelGrid::whole, at the
// preview base's size (the long side at 2560 px), an export reduced to 2048 px and an odd size:
// an export reduced before the stages still evaluates the field on its own reduced image at
// scale 1, as today.
TEST(FramedField, WholePhotoAtZeroIsBitIdenticalToTheWholeGrid) {
    struct Size {
        int w, h;
    };
    for (const Size sz : {Size{2560, 1707}, Size{1707, 2560}, Size{2048, 1365}, Size{1501, 701}}) {
        const StrokeField f = sample_field(prism::aspect_of(sz.w, sz.h));
        const std::vector<float> whole = rasterise(f, PixelGrid::whole(sz.w, sz.h));
        const PixelGrid framed = PixelGrid::framed(Frame{}, sz.w, sz.h, 1.0f, sz.w, sz.h);
        ASSERT_EQ(framed.width(), sz.w);
        ASSERT_EQ(framed.height(), sz.h);
        EXPECT_EQ(first_bit_difference(rasterise(f, framed), whole), -1) << sz.w << "x" << sz.h;
        EXPECT_NE(std::count(whole.begin(), whole.end(), 0.0f), static_cast<std::ptrdiff_t>(whole.size()));
    }
}

// A 0 degree crop at scale 1 is the matching crop of the whole-photo field, bit for bit. The crop
// is narrower and shorter than the photo, so a position held to the framed size instead of the
// photo's would show.
TEST(FramedField, CroppedFrameAtZeroIsTheCropOfTheWholeField) {
    constexpr int W = 1200;
    constexpr int H = 800;
    const StrokeField f = sample_field(prism::aspect_of(W, H));
    const std::vector<float> whole = rasterise(f, PixelGrid::whole(W, H));
    Frame crop;
    crop.left = 0.3f;
    crop.top = 0.2f;
    crop.right = 0.95f;
    crop.bottom = 0.9f;
    const prism::FrameBox box = prism::framed_box(crop, W, H);
    const PixelGrid g = PixelGrid::framed(crop, W, H, 1.0f, box.width, box.height);
    const std::vector<float> got = rasterise(f, g);
    std::vector<float> expected;
    for (int j = 0; j < box.height; ++j) {
        for (int i = 0; i < box.width; ++i) {
            expected.push_back(
                whole[static_cast<std::size_t>(box.y0 + j) * W + static_cast<std::size_t>(box.x0 + i)]);
        }
    }
    EXPECT_EQ(first_bit_difference(got, expected), -1);
    EXPECT_NE(std::count(expected.begin(), expected.end(), 0.0f),
              static_cast<std::ptrdiff_t>(expected.size()));
}

// --- 3, 4, 5: turned.

// One round dab at photo point P peaks, on a turned grid, at the framed pixel nearest
// photo_to_frame(P): within half a pixel along each axis, for both turns.
TEST(FramedField, TurnedDabPeaksWhereTheMapPutsIt) {
    constexpr int W = 3000;
    constexpr int H = 2000;
    for (const float angle : {30.0f, -30.0f}) {
        const PixelGrid g = fitted_grid(turned(angle), W, H);
        const Frame frame = g.framing->frame();
        // Off the centre in both directions, so a turn the wrong way lands far from it.
        const prism::PhotoPoint p{0.43, 0.57};
        const StrokeField f = dab(W, H, p.u, p.v, 0.02f);
        const std::vector<float> field = rasterise(f, g);
        const FramePoint expected = prism::photo_to_frame(frame, W, H, p);
        const Peak peak = peak_of(field, g);
        EXPECT_LE(std::abs(peak.i + 0.5 - expected.x), 0.5) << angle << " degrees";
        EXPECT_LE(std::abs(peak.j + 0.5 - expected.y), 0.5) << angle << " degrees";
        EXPECT_GT(at(field, g, peak.i, peak.j), 0.99f);
    }
}

// On a 3:2 photo, turned and cropped to a frame of another shape, the brush is still round: the
// field at distance r along the frame's x equals the field at distance r along its y. The dab is
// centred on a grid pixel, so both are grid pixels. Positions are floats of up to 3000 px, good to
// 2.5e-4 px; the field changes by at most 1.5 / (radius in px) = 0.005 per px, so the two agree
// to about 1e-6, well inside 1e-5. Measured with the framed size instead of the photo's they would
// differ by up to ~0.3.
TEST(FramedField, BrushStaysRoundUnderTurnAndCrop) {
    constexpr int W = 3000;
    constexpr int H = 2000;
    for (const float angle : {30.0f, -30.0f}) {
        Frame crop = turned(angle);
        crop.left = 0.3f;
        crop.right = 0.7f;
        crop.top = 0.1f;
        crop.bottom = 0.9f;
        const PixelGrid g = fitted_grid(crop, W, H);
        ASSERT_GT(std::abs(static_cast<double>(g.width()) / g.height() - 1.5), 0.3)
            << "the frame should not keep the photo's shape";
        const FrameMap map(g.framing->frame(), W, H);
        const int ic = g.width() / 2;
        const int jc = g.height() / 2;
        const prism::PhotoPoint p = map.to_photo({ic + 0.5, jc + 0.5});
        const StrokeField f = dab(W, H, p.u, p.v, 0.05f);  // 150 px
        const std::vector<float> field = rasterise(f, g);
        for (int k = 10; k <= 140; k += 10) {
            ASSERT_LT(k, std::min(ic, jc));
            const float right = at(field, g, ic + k, jc);
            const float left = at(field, g, ic - k, jc);
            const float down = at(field, g, ic, jc + k);
            const float up = at(field, g, ic, jc - k);
            EXPECT_GT(right, 0.0f) << k;
            EXPECT_NEAR(right, down, 1e-5) << angle << " degrees, " << k << " px";
            EXPECT_NEAR(left, up, 1e-5) << angle << " degrees, " << k << " px";
            EXPECT_NEAR(right, left, 1e-5) << angle << " degrees, " << k << " px";
        }
    }
}

// The field worked out on a turned grid against the whole-photo field turned by apply_frame.
// Catmull-Rom reproduces any linear function exactly and samples within 2 px along each axis, so
// its error on a smooth field is at most (sum of |weights|)^2 * M2 * d^2 / 2 with d^2 <= 8 px^2
// and sum |w| <= 1.25: 6.25 * M2, M2 the largest second derivative; clamping to the 2 x 2 nearest
// can add at most M2 / 2 more. One straight, fully soft stroke of radius r px has
// M2 = 6 / r^2 (from the smoothstep, and from its slope over the distance), so the two must agree
// to 7 * 6 / r^2: 2.9e-3 for r = 120 px. A field half a pixel off would differ by ~6e-3.
TEST(FramedField, TurnedFieldMatchesTurningTheWholeField) {
    constexpr int W = 1200;
    constexpr int H = 800;
    StrokeField f;
    f.aspect = prism::aspect_of(W, H);
    f.strokes.push_back(stroke({{0.3f, 0.35f}, {0.7f, 0.6f}}, 0.1f, 1.0f, 1.0f));
    const double r_px = 0.1 * W;
    const double tolerance = 7.0 * 6.0 / (r_px * r_px);

    const std::vector<float> whole = rasterise(f, PixelGrid::whole(W, H));
    prism::Image photo;
    photo.width = W;
    photo.height = H;
    for (const float v : whole) {
        photo.pixels.insert(photo.pixels.end(), {v, v, v});
    }
    for (const float angle : {7.0f, -23.0f}) {
        const Frame frame = prism::fit_frame(turned(angle), W, H);
        const prism::Image turned_field = prism::apply_frame(photo, frame, 1);
        const PixelGrid g = PixelGrid::framed(frame, W, H, 1.0f, turned_field.width,
                                              turned_field.height);
        const std::vector<float> got = rasterise(f, g);
        double worst = 0.0;
        for (std::size_t k = 0; k < got.size(); ++k) {
            worst = std::max(worst, std::abs(static_cast<double>(got[k]) - turned_field.pixels[k * 3]));
        }
        EXPECT_LT(worst, tolerance) << angle << " degrees";
        RecordProperty(angle > 0 ? "worst_plus" : "worst_minus", std::to_string(worst));
    }
}

// --- 6: a reduced grid.

// At scale s, grid pixel (i, j) is at frame point ((i + 0.5) * s, (j + 0.5) * s): a dab centred on
// that point peaks at (i, j), and the field is symmetric about it along both of the grid's axes
// (the dab is round on the photo, and the grid's axes are the frame's). With pixels at
// i * s + 0.5 instead, the dab would sit (s - 1) / 2 framed px off (i, j) and the two sides would
// differ by ~0.01.
TEST(FramedField, ReducedTurnedGridPutsPixelsAtHalfPlusIndexTimesScale) {
    constexpr int W = 3000;
    constexpr int H = 2000;
    for (const float scale : {2.0f, 2.5f}) {
        const PixelGrid g = fitted_grid(turned(-17.0f), W, H, scale);
        const FrameMap map(g.framing->frame(), W, H);
        const int ic = g.width() / 3;
        const int jc = g.height() * 2 / 3;
        const prism::PhotoPoint p = map.to_photo({(ic + 0.5) * scale, (jc + 0.5) * scale});
        const StrokeField f = dab(W, H, p.u, p.v, 0.01f);  // 30 photo px
        const std::vector<float> field = rasterise(f, g);
        const Peak peak = peak_of(field, g);
        EXPECT_EQ(peak.i, ic) << "scale " << scale;
        EXPECT_EQ(peak.j, jc) << "scale " << scale;
        for (int k = 1; k <= 4; ++k) {
            EXPECT_NEAR(at(field, g, ic - k, jc), at(field, g, ic + k, jc), 1e-5)
                << "scale " << scale << ", " << k;
            EXPECT_NEAR(at(field, g, ic, jc - k), at(field, g, ic, jc + k), 1e-5)
                << "scale " << scale << ", " << k;
            EXPECT_LT(at(field, g, ic + k, jc), at(field, g, ic + k - 1, jc));
        }
    }
}

// --- 7: threads.

// Dodge & burn on a 1601 x 1001 framed image, turned -30 degrees and reduced: 1 and 8 threads give
// the same bits, and both equal the field rasterised in one go and handed to the stage. 8 threads
// split the rows into 8 chunks, each rasterised 64 rows at a time from its own start.
TEST(FramedField, OneAndEightThreadsAreBitIdenticalOnATurnedGrid) {
    constexpr int W = 4500;
    constexpr int H = 3000;
    constexpr int GW = 1601;
    constexpr int GH = 1001;
    const std::size_t grid_pixels = static_cast<std::size_t>(GW) * static_cast<std::size_t>(GH);
    ASSERT_GE(grid_pixels, prism::parallel_min_pixels * 3 / 2);
    ASSERT_EQ(prism::threads_for_pixels(grid_pixels, 8), 8);
    Frame frame = turned(-30.0f);
    frame.left = 0.05f;
    frame.right = 0.95f;
    frame.top = 0.2f;
    frame.bottom = 0.8f;
    frame = prism::fit_frame(frame, W, H);
    const prism::PixelSize size = prism::framed_size(frame, W, H);
    ASSERT_GE(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height),
              prism::parallel_min_pixels * 3 / 2);
    const float scale = static_cast<float>(size.width) / GW;
    ASSERT_GT(scale, 1.0f);
    const PixelGrid g = PixelGrid::framed(frame, W, H, scale, GW, GH);
    prism::LocalExposure local;
    local.field = sample_field(prism::aspect_of(W, H));
    local.grid = g;

    const auto image = [&] {
        prism::Image img;
        img.width = GW;
        img.height = GH;
        img.pixels.assign(static_cast<std::size_t>(GW) * GH * 3, 0.25f);
        return img;
    };
    prism::Image one = image();
    prism::dodge_burn(one, local, 1);
    prism::Image eight = image();
    prism::dodge_burn(eight, local, 8);
    EXPECT_EQ(first_bit_difference(one.pixels, eight.pixels), -1);

    // The same field rasterised in one go and handed to the stage, which then only applies it.
    const std::vector<float> field = rasterise(local.field, g);
    prism::LocalExposure handed = local;
    handed.precomputed = field;
    prism::Image expected_image = image();
    prism::dodge_burn(expected_image, handed, 1);
    const std::vector<float>& expected = expected_image.pixels;
    EXPECT_EQ(first_bit_difference(one.pixels, expected), -1);
    EXPECT_GT(std::count_if(field.begin(), field.end(), [](float v) { return v != 0.0f; }),
              static_cast<std::ptrdiff_t>(field.size() / 10));
}

// --- 8: the branches.

// At exactly 0 degrees a framed grid takes the axis-aligned path: no pixel's position is worked out
// on its own, at scale 1 or not. Any other angle maps each pixel the strokes reach.
TEST(FramedField, ZeroAngleTakesTheAxisAlignedPath) {
    constexpr int W = 1200;
    constexpr int H = 800;
    const StrokeField f = sample_field(prism::aspect_of(W, H));
    for (const float scale : {1.0f, 2.5f}) {
        Frame crop;
        crop.left = 0.1f;
        crop.right = 0.8f;
        RasterStats flat;
        const std::vector<float> field = rasterise(f, fitted_grid(crop, W, H, scale), &flat);
        EXPECT_GT(flat.evaluated, 0u) << scale;
        EXPECT_EQ(flat.turned_positions, 0u) << scale;
        EXPECT_NE(std::count(field.begin(), field.end(), 0.0f), static_cast<std::ptrdiff_t>(field.size()));

        crop.angle = 0.5f;
        RasterStats turn;
        (void)rasterise(f, fitted_grid(crop, W, H, scale), &turn);
        EXPECT_GT(turn.turned_positions, 0u) << scale;
    }
}

// A frame that is not fitted puts some grid pixels off the photo. Each takes the field at the
// nearest photo pixel centre along each axis, which is finite and is what the photo pixel it shows
// (apply_frame repeats the edge pixel) carries. A hard dab of 12 px centred on the left edge,
// level with the middle of the part of the grid that lies more than 40 px left of the photo:
// there, pixels level with the dab get its full weight, though they are over 40 px from it, and
// pixels level with nothing get 0.
TEST(FramedField, OffPhotoPixelsTakeTheFieldAtTheNearestEdge) {
    constexpr int W = 1200;
    constexpr int H = 800;
    const Frame frame = turned(40.0f);  // the whole photo, not fitted: corners off the photo
    const PixelGrid g = PixelGrid::framed(frame, W, H, 1.0f, W, H);
    const FrameMap map(frame, W, H);
    double y_min = H;
    double y_max = 0.0;
    for (int j = 0; j < g.height(); ++j) {
        for (int i = 0; i < g.width(); ++i) {
            const FramePoint q = map.to_photo_pixels({i + 0.5, j + 0.5});
            if (q.x < -40.0) {
                y_min = std::min(y_min, q.y);
                y_max = std::max(y_max, q.y);
            }
        }
    }
    ASSERT_GT(y_max - y_min, 60.0);
    const double y_dab = (y_min + y_max) / 2.0;
    StrokeField f;
    f.aspect = prism::aspect_of(W, H);
    f.strokes.push_back(stroke({{0.0f, static_cast<float>(y_dab / H)}}, 0.01f, 0.0f, 0.75f));
    const std::vector<float> field = rasterise(f, g);
    int level = 0;
    int clear = 0;
    for (int j = 0; j < g.height(); ++j) {
        for (int i = 0; i < g.width(); ++i) {
            const float v = at(field, g, i, j);
            ASSERT_TRUE(std::isfinite(v)) << i << ", " << j;
            const FramePoint q = map.to_photo_pixels({i + 0.5, j + 0.5});
            if (q.x >= -40.0) {
                continue;
            }
            if (std::abs(q.y - y_dab) < 6.0) {
                ASSERT_EQ(v, 0.75f) << i << ", " << j;
                ++level;
            } else if (std::abs(q.y - y_dab) > 20.0) {
                ASSERT_EQ(v, 0.0f) << i << ", " << j;
                ++clear;
            }
        }
    }
    EXPECT_GT(level, 20);
    EXPECT_GT(clear, 20);
}

// Every pixel of turned and reduced grids against the formula in double, with each position from
// FrameMap (moved onto the photo): so no segment's box leaves out a pixel the stroke reaches.
TEST(FramedField, TurnedFieldEqualsTheFormulaAtEveryPixel) {
    constexpr int W = 600;
    constexpr int H = 400;
    const StrokeField f = sample_field(prism::aspect_of(W, H));
    const double sx = std::min(static_cast<double>(f.aspect), 1.0);
    const double sy = std::min(1.0 / f.aspect, 1.0);
    struct Case {
        float angle;
        float scale;
        bool fit;
    };
    for (const Case c : {Case{12.0f, 1.0f, true}, Case{-41.0f, 1.7f, true},
                         Case{25.0f, 1.0f, false}, Case{0.0f, 2.5f, true}}) {
        Frame frame = turned(c.angle);
        frame.left = 0.05f;
        frame.right = 0.9f;
        if (c.fit) {
            frame = prism::fit_frame(frame, W, H);
        }
        const prism::PixelSize size = prism::framed_size(frame, W, H);
        const PixelGrid g = PixelGrid::framed(
            frame, W, H, c.scale, static_cast<int>(std::lround(size.width / c.scale)),
            static_cast<int>(std::lround(size.height / c.scale)));
        const FrameMap map(frame, W, H);
        const std::vector<float> got = rasterise(f, g);
        int nonzero = 0;
        for (int j = 0; j < g.height(); ++j) {
            for (int i = 0; i < g.width(); ++i) {
                const FramePoint q = map.to_photo_pixels({(i + 0.5) * c.scale, (j + 0.5) * c.scale});
                const double X = std::clamp(q.x, 0.5, W - 0.5) / W * sx;
                const double Y = std::clamp(q.y, 0.5, H - 0.5) / H * sy;
                double value = 0.0;
                bool near_edge = false;
                for (const auto& s : f.strokes) {
                    const double r = s->radius;
                    const double r_in = r * (1.0 - s->softness);
                    double best = 0.0;
                    const std::size_t n = s->points.size();
                    for (std::size_t k = 0; k < std::max<std::size_t>(n, 2) - 1; ++k) {
                        const auto& a = s->points[k];
                        const auto& b = s->points[std::min(k + 1, n - 1)];
                        const double ax = a.u * sx, ay = a.v * sy;
                        const double dx = b.u * sx - ax, dy = b.v * sy - ay;
                        const double len2 = dx * dx + dy * dy;
                        const double t =
                            len2 > 0 ? std::clamp(((X - ax) * dx + (Y - ay) * dy) / len2, 0.0, 1.0)
                                     : 0.0;
                        const double d = std::hypot(X - ax - t * dx, Y - ay - t * dy);
                        near_edge = near_edge || std::abs(d - r) < 1e-5 || std::abs(d - r_in) < 1e-5;
                        double cov = 0.0;
                        if (d <= r_in) {
                            cov = 1.0;
                        } else if (d < r) {
                            const double u = (d - r_in) / (r - r_in);
                            cov = 1.0 - u * u * (3.0 - 2.0 * u);
                        }
                        best = std::max(best, cov);
                    }
                    value += s->weight * best;
                }
                if (near_edge) {
                    continue;
                }
                ASSERT_NEAR(at(got, g, i, j), value, 1e-4)
                    << c.angle << " degrees, scale " << c.scale << ", at (" << i << ", " << j << ")";
                nonzero += value != 0.0;
            }
        }
        EXPECT_GT(nonzero, g.width() * g.height() / 4) << c.angle;
    }
}

// The incremental API on a framed grid, as the preview's field cache drives it (each point's
// segment covered on the sub-grid of the stroke's box so far), equals rasterising the strokes from
// fresh, bit for bit: turned, and at 0 degrees with a scale.
TEST(FramedField, IncrementalEqualsFreshOnAFramedGrid) {
    constexpr int W = 1500;
    constexpr int H = 1000;
    struct Case {
        float angle;
        float scale;
    };
    for (const Case c : {Case{-30.0f, 1.0f}, Case{8.0f, 1.5f}, Case{0.0f, 2.5f}}) {
        Frame crop = turned(c.angle);
        crop.left = 0.2f;
        crop.top = 0.1f;
        crop.right = 0.9f;
        const PixelGrid g = fitted_grid(crop, W, H, c.scale);
        StrokeField before = sample_field(prism::aspect_of(W, H));
        const std::vector<float> before_field = rasterise(before, g);
        const FieldStroke last{{{0.35f, 0.4f}, {0.5f, 0.45f}, {0.62f, 0.38f}, {0.7f, 0.55f}},
                               0.04f,
                               0.6f,
                               -0.4567f};
        std::vector<float> field = before_field;
        // Each segment's coverage on the sub-grid of its own box, applied through that box.
        GridRect box_so_far;
        std::vector<float> coverage(field.size(), 0.0f);
        for (std::size_t k = 0; k < last.points.size(); ++k) {
            const GridRect need = prism::segment_bounds(last, k, before.aspect, g);
            if (need.empty()) {
                continue;
            }
            const PixelGrid sub = prism::sub_grid(g, need);
            std::vector<float> part(need.area(), 0.0f);
            (void)prism::add_segment_coverage(last, k, before.aspect, sub, part);
            for (int j = need.j_begin; j < need.j_end; ++j) {
                for (int i = need.i_begin; i < need.i_end; ++i) {
                    float& cov = coverage[static_cast<std::size_t>(j) * g.width() + i];
                    cov = std::max(cov, part[static_cast<std::size_t>(j - need.j_begin) *
                                                 (need.i_end - need.i_begin) +
                                             static_cast<std::size_t>(i - need.i_begin)]);
                }
            }
            box_so_far = box_so_far.empty()
                             ? need
                             : GridRect{std::min(box_so_far.i_begin, need.i_begin),
                                        std::max(box_so_far.i_end, need.i_end),
                                        std::min(box_so_far.j_begin, need.j_begin),
                                        std::max(box_so_far.j_end, need.j_end)};
            prism::apply_stroke_coverage(before_field, last.weight, coverage, g, box_so_far, field);
        }
        before.strokes.push_back(std::make_shared<const FieldStroke>(last));
        const std::vector<float> fresh = rasterise(before, g);
        EXPECT_EQ(first_bit_difference(field, fresh), -1)
            << c.angle << " degrees, scale " << c.scale;
        EXPECT_NE(first_bit_difference(fresh, before_field), -1);
    }
}

// A framed grid needs cols and rows, a finite scale above 0 and a valid frame; rasterising on
// anything else throws, and segment_bounds gives an empty box.
TEST(FramedField, BadFramedGridsAreRefused) {
    constexpr int W = 300;
    constexpr int H = 200;
    const StrokeField f = sample_field(prism::aspect_of(W, H));
    Frame bad_frame;
    bad_frame.left = 0.6f;
    bad_frame.right = 0.4f;
    Frame too_turned;
    too_turned.angle = 50.0f;
    const PixelGrid good = PixelGrid::framed(Frame{}, W, H, 1.0f, W, H);
    ASSERT_FALSE(prism::segment_bounds(*f.strokes[0], 1, f.aspect, good).empty());
    for (const PixelGrid& g :
         {PixelGrid::framed(Frame{}, W, H, 1.0f, 0, H), PixelGrid::framed(Frame{}, W, H, 1.0f, W, 0),
          PixelGrid::framed(Frame{}, W, H, 0.0f, W, H), PixelGrid::framed(Frame{}, W, H, -1.0f, W, H),
          PixelGrid::framed(Frame{}, W, H, std::numeric_limits<float>::quiet_NaN(), W, H),
          PixelGrid::framed(Frame{}, W, H, std::numeric_limits<float>::infinity(), W, H),
          PixelGrid::framed(bad_frame, W, H, 1.0f, W, H),
          PixelGrid::framed(too_turned, W, H, 1.0f, W, H)}) {
        std::vector<float> out(static_cast<std::size_t>(std::max(g.width(), 1)) *
                               static_cast<std::size_t>(std::max(g.height(), 1)));
        EXPECT_THROW(prism::rasterise_field(f, g, 0, g.height(), out), std::invalid_argument)
            << g.framing->scale << " " << g.width() << "x" << g.height();
        EXPECT_TRUE(prism::segment_bounds(*f.strokes[0], 1, f.aspect, g).empty())
            << g.framing->scale << " " << g.width() << "x" << g.height();
    }
}

}  // namespace

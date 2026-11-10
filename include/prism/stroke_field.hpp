#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "prism/fast_math.hpp"

namespace prism {

// Dodge and burn brush strokes, and the "stroke field" they paint: one number per pixel, in stops,
// that a later pipeline stage turns into a gain (stroke_gain). This file only computes the field;
// it does not touch an image.
//
// Coordinates are resolution-free so a stroke drawn on the 2560 px preview lands on the same
// place of the full-size export: a point is (u, v) with u = (x + 0.5) / W and v = (y + 0.5) / H,
// where (x, y) is a pixel of the oriented (rotated as displayed) W x H image, so pixel centres
// map to the middle of their cell and (0, 0)..(1, 1) is the whole image whatever its size.

// One point of a stroke, in the (u, v) coordinates above.
struct FieldPoint {
    float u = 0.0f;
    float v = 0.0f;

    friend bool operator==(const FieldPoint&, const FieldPoint&) = default;
};

// One brush stroke: the brush's centre line, its size, edge and strength.
struct FieldStroke {
    // The centre line, in drawing order. One point is a dab (a disc); more is a polyline whose
    // segments are drawn with round ends ("capsules").
    std::vector<FieldPoint> points;
    // The brush radius as a fraction of the image's long side (0.03 = 3% of the long side).
    float radius = 0.0f;
    // 0 = hard edge, 1 = the whole radius fades. The inner (1 - softness) of the radius is full
    // strength, and the rest falls off along a smoothstep.
    float softness = 0.0f;
    // Strength in stops: positive dodges (brightens), negative burns (darkens).
    float weight = 0.0f;

    friend bool operator==(const FieldStroke&, const FieldStroke&) = default;
};

// All strokes on a photo. Strokes are immutable and shared, so copying a field (an undo step, the
// settings handed to the render thread) copies pointers, not points.
struct StrokeField {
    // W / H of the oriented decoded image, fixed when the first stroke is made (aspect_of). It
    // sets the long-side units the radius and distances are measured in. 0 while there are no
    // strokes.
    float aspect = 0.0f;
    std::vector<std::shared_ptr<const FieldStroke>> strokes;

    // Equal if the aspects match and each stroke is the same object or has the same content.
    friend bool operator==(const StrokeField& a, const StrokeField& b) noexcept;
};

// The aspect to store in StrokeField::aspect for a W x H image.
[[nodiscard]] float aspect_of(int width, int height) noexcept;

// The gain a field value asks for: 2^field. Field 0 means "this pixel is untouched by any stroke"
// (the rasteriser writes exactly 0 there, and fast_exp2(0) is exactly 1). The stage that applies
// the field relies on this: it may skip pixels whose field is exactly 0.
[[nodiscard]] inline float stroke_gain(float field) noexcept {
    return fast_exp2(field);
}

// ---------------------------------------------------------------------------------------------
// The formula
//
// Distances are measured in long-side units: a pixel centre's (u, v) becomes
//   X = u * min(aspect, 1),   Y = v * min(1 / aspect, 1)
// so the long side runs from 0 to 1 and the short side from 0 to its fraction of that; a stroke's
// points are mapped the same way. For each segment of a stroke, d is the distance from (X, Y) to
// the segment. With r = radius and r_in = r * (1 - softness):
//   coverage = 1                                        if d <= r_in
//            = 0                                        if d >= r
//            = 1 - smoothstep((d - r_in) / (r - r_in))  otherwise, smoothstep(s) = s^2 (3 - 2s)
// (softness 0 makes r_in = r: a hard edge, and the middle case never happens, so no division by
// zero). A stroke's coverage is the MAX over its segments, so where it crosses itself or where
// two segments meet at a joint it still counts once. The field is
//   field = sum over strokes, in stroke order, of weight * coverage
// accumulated in float as field = field + (weight * coverage), one stroke at a time; the
// incremental API below repeats exactly these operations so its result is bit-identical.
//
// A stroke with n >= 1 points has n "segments": segment 0 is the first point alone (a disc), and
// segment k >= 1 runs from point k - 1 to point k. The disc lies inside segment 1's capsule, so
// it never changes the coverage mathematically; it is there so that a stroke growing one point at
// a time (the incremental API) and the same stroke drawn at once take the max over exactly the
// same segments, which is what makes the two bit-identical.
//
// stroke_field.cpp is compiled without floating-point contraction (no fused multiply-add) on every
// compiler, and every caller goes through the same functions, so a pixel's value depends only on
// the pixel and the strokes, never on which grid, row range or thread computed it.
// ---------------------------------------------------------------------------------------------

// A grid of sample positions on a full_w x full_h image: grid pixel (i, j) is image pixel
// (x0 + i * stride, y0 + j * stride). The default (x0 = y0 = 0, stride = 1) is the whole image; a
// band of rows is {W, H, 0, y, 1}; a preview downsampled by 4 could be {W, H, 1, 1, 4}. The grid
// extends right and down as far as the image does.
struct PixelGrid {
    int full_w = 0;
    int full_h = 0;
    int x0 = 0;
    int y0 = 0;
    int stride = 1;

    // Grid pixels per row: the number of i >= 0 with x0 + i * stride < full_w.
    [[nodiscard]] int width() const noexcept;
    // Grid rows: the number of j >= 0 with y0 + j * stride < full_h.
    [[nodiscard]] int height() const noexcept;
    // The whole image.
    [[nodiscard]] static PixelGrid whole(int w, int h) noexcept { return {w, h, 0, 0, 1}; }
};

// A rectangle of grid pixels: columns [i_begin, i_end), rows [j_begin, j_end). Empty if either
// range is.
struct GridRect {
    int i_begin = 0;
    int i_end = 0;
    int j_begin = 0;
    int j_end = 0;

    [[nodiscard]] bool empty() const noexcept { return i_begin >= i_end || j_begin >= j_end; }
    [[nodiscard]] std::size_t area() const noexcept {
        return empty() ? 0
                       : static_cast<std::size_t>(i_end - i_begin) *
                             static_cast<std::size_t>(j_end - j_begin);
    }
    friend bool operator==(const GridRect&, const GridRect&) = default;
};

// Counts for tests and benches: how many (pixel, segment) coverage evaluations were made.
struct RasterStats {
    std::size_t evaluated = 0;
};

// The number of segments of `stroke` (= its number of points; see above).
[[nodiscard]] std::size_t segment_count(const FieldStroke& stroke) noexcept;

// The grid pixels that segment `segment` of `stroke` can cover: the image pixels whose centres lie
// in its bounding box (the segment widened by the radius), plus one image pixel on each side for
// rounding, clipped to the grid.
// Every pixel outside it has coverage 0 from that segment. Empty if the segment is off the image.
[[nodiscard]] GridRect segment_bounds(const FieldStroke& stroke, std::size_t segment,
                                      float aspect, const PixelGrid& grid) noexcept;

// Writes the field for grid rows [row_begin, row_end) into `out`, row after row, grid.width()
// floats per row (out.size() must be (row_end - row_begin) * grid.width()). Pixels no stroke
// reaches get exactly 0. Only pixels inside some segment's bounds are evaluated, and the only
// memory used beyond `out` is one row of floats. Safe to call from several threads at once with
// separate `out` buffers. Throws std::invalid_argument on a bad grid, range or buffer size.
void rasterise_field(const StrokeField& field, const PixelGrid& grid, int row_begin, int row_end,
                     std::span<float> out, RasterStats* stats = nullptr);

// A field made ready to rasterise on one grid: every stroke's segments and their pixel boxes,
// worked out once, for a caller that rasterises many row ranges (the dodge & burn stage, one tile
// of rows at a time on each thread). rasterise_field is exactly
// PreparedField(field, grid).rasterise(row_begin, row_end, out, stats), so the two give
// bit-identical fields. It holds its own copy of what it needs (not `field`), never changes after
// construction, and rasterise() may be called from several threads at once.
class PreparedField {
public:
    // Throws std::invalid_argument on a bad grid, or a field with strokes but no valid aspect.
    PreparedField(const StrokeField& field, const PixelGrid& grid);
    ~PreparedField();
    PreparedField(PreparedField&&) noexcept;
    PreparedField& operator=(PreparedField&&) noexcept;
    PreparedField(const PreparedField&) = delete;
    PreparedField& operator=(const PreparedField&) = delete;

    [[nodiscard]] const PixelGrid& grid() const noexcept;
    // True if no segment can reach grid rows [row_begin, row_end): rasterise() would write only 0
    // there, so a caller may skip those rows altogether.
    [[nodiscard]] bool rows_untouched(int row_begin, int row_end) const noexcept;
    // As rasterise_field. Only the segments whose boxes meet [row_begin, row_end) are looked at.
    void rasterise(int row_begin, int row_end, std::span<float> out,
                   RasterStats* stats = nullptr) const;

private:
    struct Data;
    std::unique_ptr<const Data> data_;
};

// ---------------------------------------------------------------------------------------------
// Incremental API, for a stroke being drawn: keep `coverage` (the stroke's coverage so far, over
// the whole grid, starting all 0) and `before` (the field of the strokes before it, from
// rasterise_field). Each time a point is added:
//   const std::size_t k = stroke.points.size() - 1;  // the new point's index = its segment's
//   const GridRect r = add_segment_coverage(stroke, k, aspect, grid, coverage);
//   apply_stroke_coverage(before, stroke.weight, coverage, grid, r, field);
// (the first point adds segment 0, the disc; point k adds the segment from point k - 1 to it).
// Then `field` is bit-identical to rasterise_field of the strokes before plus this stroke, as
// long as this stroke is the last one (field is initialised as a copy of `before`).
// ---------------------------------------------------------------------------------------------

// coverage = max(coverage, coverage of segment `segment`) over that segment's bounds only, which it
// returns. `coverage` holds grid.width() * grid.height() floats. Throws std::invalid_argument on a
// bad grid or buffer size, or a segment index out of range.
GridRect add_segment_coverage(const FieldStroke& stroke, std::size_t segment, float aspect,
                              const PixelGrid& grid, std::span<float> coverage,
                              RasterStats* stats = nullptr);

// field = before + weight * coverage, inside `rect` only. All three buffers hold
// grid.width() * grid.height() floats.
void apply_stroke_coverage(std::span<const float> before, float weight,
                           std::span<const float> coverage, const PixelGrid& grid,
                           const GridRect& rect, std::span<float> field);

// Point spacing while drawing: a new point is worth adding only once the pointer is at least
// max(0.15 * radius, one image pixel) from the last point added, measured in long-side units.
// Closer points would cost time and add nothing visible (each capsule overlaps the last almost
// entirely); the 1 px floor keeps a tiny brush from adding a point per mouse event.
// `long_side_px` is the long side, in pixels, of the image the stroke is drawn on.
[[nodiscard]] float min_point_spacing(float radius, int long_side_px) noexcept;
// True if `next` is at least `spacing` (long-side units) from `last` on an image of `aspect`.
[[nodiscard]] bool far_enough(FieldPoint last, FieldPoint next, float aspect,
                              float spacing) noexcept;

}  // namespace prism

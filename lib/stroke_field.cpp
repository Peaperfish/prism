#include "prism/stroke_field.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

// This file must be compiled without floating-point contraction (lib/CMakeLists.txt sets
// -ffp-contract=off for it on GCC and Clang; MSVC does not contract by default): the bit-identity
// promises in stroke_field.hpp assume every a * b + c is rounded twice, the same way everywhere.

namespace prism {

bool operator==(const StrokeField& a, const StrokeField& b) noexcept {
    if (a.aspect != b.aspect || a.strokes.size() != b.strokes.size()) {
        return false;
    }
    for (std::size_t k = 0; k < a.strokes.size(); ++k) {
        const auto& sa = a.strokes[k];
        const auto& sb = b.strokes[k];
        if (sa == sb) {
            continue;  // the same object (or both null)
        }
        if (!sa || !sb || !(*sa == *sb)) {
            return false;
        }
    }
    return true;
}

float aspect_of(int width, int height) noexcept {
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 0.0f;
}

int PixelGrid::width() const noexcept {
    const int n = (full_w > x0 && x0 >= 0 && stride > 0) ? (full_w - x0 + stride - 1) / stride : 0;
    return cols > 0 ? std::min(n, cols) : n;
}

int PixelGrid::height() const noexcept {
    const int n = (full_h > y0 && y0 >= 0 && stride > 0) ? (full_h - y0 + stride - 1) / stride : 0;
    return rows > 0 ? std::min(n, rows) : n;
}

std::size_t segment_count(const FieldStroke& stroke) noexcept {
    return stroke.points.size();
}

namespace {

// Long-side units per pixel-centre coordinate: X = (x + 0.5) * kx, Y = (y + 0.5) * ky.
struct Mapping {
    float sx = 0.0f;  // min(aspect, 1)
    float sy = 0.0f;  // min(1 / aspect, 1)
    float kx = 0.0f;  // sx / full_w
    float ky = 0.0f;  // sy / full_h
};

bool valid_aspect(float aspect) noexcept {
    return std::isfinite(aspect) && aspect > 0.0f;
}

Mapping mapping_for(float aspect, const PixelGrid& grid) noexcept {
    Mapping m;
    m.sx = std::min(aspect, 1.0f);
    m.sy = std::min(1.0f / aspect, 1.0f);
    m.kx = m.sx / static_cast<float>(grid.full_w);
    m.ky = m.sy / static_cast<float>(grid.full_h);
    return m;
}

float pixel_x(const Mapping& m, const PixelGrid& g, int i) noexcept {
    const int x = g.x0 + i * g.stride;
    return (static_cast<float>(x) + 0.5f) * m.kx;
}

float pixel_y(const Mapping& m, const PixelGrid& g, int j) noexcept {
    const int y = g.y0 + j * g.stride;
    return (static_cast<float>(y) + 0.5f) * m.ky;
}

// A stroke's brush, precomputed.
struct Brush {
    float r = 0.0f;
    float r_in = 0.0f;
    float r2 = 0.0f;
    float rin2 = 0.0f;
    float inv_band = 0.0f;  // 1 / (r - r_in), or 0 for a hard edge (never used then)
    bool draws = false;     // false for a zero, negative or non-finite radius: covers nothing
};

Brush brush_for(const FieldStroke& s) noexcept {
    Brush b;
    if (!std::isfinite(s.radius) || s.radius <= 0.0f || !std::isfinite(s.softness)) {
        return b;
    }
    const float softness = std::clamp(s.softness, 0.0f, 1.0f);
    b.r = s.radius;
    b.r_in = s.radius * (1.0f - softness);
    b.r2 = b.r * b.r;
    b.rin2 = b.r_in * b.r_in;
    b.inv_band = b.r > b.r_in ? 1.0f / (b.r - b.r_in) : 0.0f;
    b.draws = true;
    return b;
}

// One segment in long-side units: from (ax, ay) along (dx, dy).
struct Segment {
    float ax = 0.0f;
    float ay = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float inv_len2 = 0.0f;  // 1 / (dx^2 + dy^2), or 0 for a point
    GridRect box;
};

Segment segment_for(const FieldStroke& s, std::size_t k, const Mapping& m) noexcept {
    const FieldPoint& a = s.points[k == 0 ? 0 : k - 1];
    const FieldPoint& b = s.points[k];
    Segment seg;
    seg.ax = a.u * m.sx;
    seg.ay = a.v * m.sy;
    const float bx = b.u * m.sx;
    const float by = b.v * m.sy;
    seg.dx = bx - seg.ax;
    seg.dy = by - seg.ay;
    const float dx2 = seg.dx * seg.dx;
    const float dy2 = seg.dy * seg.dy;
    const float len2 = dx2 + dy2;
    seg.inv_len2 = len2 > 0.0f ? 1.0f / len2 : 0.0f;
    return seg;
}

// One image pixel of slack on each side of the tight box, for the float rounding of pixel centres
// and segment ends (a few ulps, far below a pixel).
constexpr double box_margin_px = 1.0;

// Image-pixel range [lo, hi] (inclusive) whose centres lie within `r` of [c_min, c_max]
// (long-side units, `k` units per pixel), widened by box_margin_px, then turned into grid indices
// [begin, end) for a grid starting at `first` with `stride` and `count` entries.
void grid_range(double c_min, double c_max, double r, double k, int full, int first, int stride,
                int count, int& begin, int& end) noexcept {
    begin = 0;
    end = 0;
    // Pixel x's centre is at (x + 0.5) * k, so it is at or right of c when x >= c / k - 0.5: the
    // tight range is [ceil(lo edge), floor(hi edge)].
    const double lo_f = std::ceil((c_min - r) / k - 0.5) - box_margin_px;
    const double hi_f = std::floor((c_max + r) / k - 0.5) + box_margin_px;
    if (!(lo_f <= hi_f) || hi_f < 0.0 || lo_f > static_cast<double>(full - 1)) {
        return;  // off the image (or NaN)
    }
    const int lo = static_cast<int>(std::max(lo_f, 0.0));
    const int hi = static_cast<int>(std::min(hi_f, static_cast<double>(full - 1)));
    if (hi < first) {
        return;
    }
    // The first grid index at or after lo, and the last at or before hi.
    const int b = lo <= first ? 0 : (lo - first + stride - 1) / stride;
    const int e = std::min((hi - first) / stride + 1, count);
    if (b < e) {
        begin = b;
        end = e;
    }
}

GridRect bounds_for(const Segment& seg, const Brush& brush, const Mapping& m,
                    const PixelGrid& g) noexcept {
    GridRect rect;
    if (!brush.draws) {
        return rect;
    }
    const double ax = seg.ax;
    const double ay = seg.ay;
    const double bx = ax + static_cast<double>(seg.dx);
    const double by = ay + static_cast<double>(seg.dy);
    grid_range(std::min(ax, bx), std::max(ax, bx), brush.r, m.kx, g.full_w, g.x0, g.stride,
               g.width(), rect.i_begin, rect.i_end);
    grid_range(std::min(ay, by), std::max(ay, by), brush.r, m.ky, g.full_h, g.y0, g.stride,
               g.height(), rect.j_begin, rect.j_end);
    if (rect.empty()) {
        rect = {};
    }
    return rect;
}

// The coverage of the point (X, Y) by one segment (the formula in stroke_field.hpp). Distances are
// compared squared, so the square root is only taken in the soft band.
float coverage_at(const Segment& s, const Brush& b, float X, float Y) noexcept {
    const float px = X - s.ax;
    const float py = Y - s.ay;
    const float dot_x = px * s.dx;
    const float dot_y = py * s.dy;
    const float t = std::clamp((dot_x + dot_y) * s.inv_len2, 0.0f, 1.0f);
    const float tx = t * s.dx;
    const float ty = t * s.dy;
    const float qx = px - tx;
    const float qy = py - ty;
    const float qx2 = qx * qx;
    const float qy2 = qy * qy;
    const float d2 = qx2 + qy2;
    if (d2 <= b.rin2) {
        return 1.0f;
    }
    if (d2 >= b.r2) {
        return 0.0f;
    }
    const float d = std::sqrt(d2);
    const float t_band = std::clamp((d - b.r_in) * b.inv_band, 0.0f, 1.0f);
    const float smooth = t_band * t_band * (3.0f - 2.0f * t_band);
    return 1.0f - smooth;
}

// field = before + weight * coverage, rounded twice (the product, then the sum), in one place so
// the full rasteriser and the incremental API cannot differ.
float accumulate(float before, float weight, float coverage) noexcept {
    const float added = weight * coverage;
    return before + added;
}

void check_grid(const PixelGrid& g, const char* who) {
    if (g.full_w <= 0 || g.full_h <= 0 || g.stride <= 0 || g.x0 < 0 || g.y0 < 0 ||
        g.x0 >= g.full_w || g.y0 >= g.full_h) {
        throw std::invalid_argument(std::string(who) + ": invalid pixel grid");
    }
}

struct PreparedStroke {
    Brush brush;
    float weight = 0.0f;
    std::vector<Segment> segments;  // only those with a non-empty box
    GridRect box;                   // union of the segments' boxes
};

}  // namespace

GridRect segment_bounds(const FieldStroke& stroke, std::size_t segment, float aspect,
                        const PixelGrid& grid) noexcept {
    if (segment >= segment_count(stroke) || !valid_aspect(aspect) || grid.width() <= 0 ||
        grid.height() <= 0) {
        return {};
    }
    const Mapping m = mapping_for(aspect, grid);
    return bounds_for(segment_for(stroke, segment, m), brush_for(stroke), m, grid);
}

void rasterise_field(const StrokeField& field, const PixelGrid& grid, int row_begin, int row_end,
                     std::span<float> out, RasterStats* stats) {
    PreparedField(field, grid).rasterise(row_begin, row_end, out, stats);
}

namespace {

// Grows `box` to cover `add` (or makes it `add`, if `box` is still empty).
void unite(GridRect& box, const GridRect& add) noexcept {
    if (box.empty()) {
        box = add;
        return;
    }
    box.i_begin = std::min(box.i_begin, add.i_begin);
    box.i_end = std::max(box.i_end, add.i_end);
    box.j_begin = std::min(box.j_begin, add.j_begin);
    box.j_end = std::max(box.j_end, add.j_end);
}

}  // namespace

struct PreparedField::Data {
    PixelGrid grid;
    Mapping m;
    std::vector<PreparedStroke> strokes;  // in stroke order; only those with a non-empty box
};

PreparedField::PreparedField(const StrokeField& field, const PixelGrid& grid) {
    check_grid(grid, "rasterise_field");
    auto data = std::make_unique<Data>();
    data->grid = grid;
    if (!field.strokes.empty()) {
        if (!valid_aspect(field.aspect)) {
            throw std::invalid_argument("rasterise_field: field has strokes but no valid aspect");
        }
        // Every stroke's segments and their boxes, dropping those that miss the grid.
        data->m = mapping_for(field.aspect, grid);
        data->strokes.reserve(field.strokes.size());
        for (const auto& sp : field.strokes) {
            if (!sp) {
                continue;
            }
            PreparedStroke ps;
            ps.brush = brush_for(*sp);
            ps.weight = sp->weight;
            if (!ps.brush.draws) {
                continue;
            }
            for (std::size_t k = 0; k < segment_count(*sp); ++k) {
                Segment seg = segment_for(*sp, k, data->m);
                seg.box = bounds_for(seg, ps.brush, data->m, grid);
                if (seg.box.empty()) {
                    continue;
                }
                unite(ps.box, seg.box);
                ps.segments.push_back(seg);
            }
            if (!ps.segments.empty()) {
                data->strokes.push_back(std::move(ps));
            }
        }
    }
    data_ = std::move(data);
}

PreparedField::~PreparedField() = default;
PreparedField::PreparedField(PreparedField&&) noexcept = default;
PreparedField& PreparedField::operator=(PreparedField&&) noexcept = default;

const PixelGrid& PreparedField::grid() const noexcept {
    return data_->grid;
}

bool PreparedField::rows_untouched(int row_begin, int row_end) const noexcept {
    for (const PreparedStroke& ps : data_->strokes) {
        if (ps.box.j_begin < row_end && ps.box.j_end > row_begin) {
            return false;
        }
    }
    return true;
}

void PreparedField::rasterise(int row_begin, int row_end, std::span<float> out,
                              RasterStats* stats) const {
    const PixelGrid& grid = data_->grid;
    const Mapping& m = data_->m;
    const int gw = grid.width();
    const int gh = grid.height();
    if (row_begin < 0 || row_end > gh || row_begin > row_end) {
        throw std::invalid_argument("rasterise_field: row range outside the grid");
    }
    const std::size_t width = static_cast<std::size_t>(gw);
    if (out.size() != static_cast<std::size_t>(row_end - row_begin) * width) {
        throw std::invalid_argument("rasterise_field: output buffer has the wrong size");
    }
    std::fill(out.begin(), out.end(), 0.0f);
    if (row_begin == row_end) {
        return;
    }

    // The segments whose boxes meet these rows, stroke by stroke in stroke order; a segment that
    // misses them has coverage 0 on every one of them, so leaving it out changes nothing.
    struct Run {
        const PreparedStroke* stroke = nullptr;
        std::size_t first = 0;  // into `segments`
        std::size_t count = 0;
        GridRect box;  // union of these segments' boxes
    };
    std::vector<Run> runs;
    std::vector<const Segment*> segments;
    for (const PreparedStroke& ps : data_->strokes) {
        if (ps.box.j_end <= row_begin || ps.box.j_begin >= row_end) {
            continue;
        }
        Run run{&ps, segments.size(), 0, {}};
        for (const Segment& seg : ps.segments) {
            if (seg.box.j_end <= row_begin || seg.box.j_begin >= row_end) {
                continue;
            }
            unite(run.box, seg.box);
            segments.push_back(&seg);
        }
        run.count = segments.size() - run.first;
        if (run.count > 0) {
            runs.push_back(run);
        }
    }
    if (runs.empty()) {
        return;
    }

    // One row of the current stroke's coverage (the max over its segments), kept all zero
    // between uses.
    std::vector<float> scratch(width, 0.0f);
    std::size_t evaluated = 0;
    for (int j = row_begin; j < row_end; ++j) {
        const float Y = pixel_y(m, grid, j);
        float* row = out.data() + static_cast<std::size_t>(j - row_begin) * width;
        for (const Run& run : runs) {
            if (j < run.box.j_begin || j >= run.box.j_end) {
                continue;
            }
            const PreparedStroke& ps = *run.stroke;
            int lo = gw;
            int hi = 0;
            for (std::size_t s = run.first; s < run.first + run.count; ++s) {
                const Segment& seg = *segments[s];
                if (j < seg.box.j_begin || j >= seg.box.j_end) {
                    continue;
                }
                for (int i = seg.box.i_begin; i < seg.box.i_end; ++i) {
                    const float c = coverage_at(seg, ps.brush, pixel_x(m, grid, i), Y);
                    scratch[static_cast<std::size_t>(i)] =
                        std::max(scratch[static_cast<std::size_t>(i)], c);
                }
                evaluated += static_cast<std::size_t>(seg.box.i_end - seg.box.i_begin);
                lo = std::min(lo, seg.box.i_begin);
                hi = std::max(hi, seg.box.i_end);
            }
            // Pixels in [lo, hi) that no segment reached hold 0 and get weight * 0 added, which
            // leaves them unchanged, exactly as a pixel outside every box.
            for (int i = lo; i < hi; ++i) {
                const std::size_t ui = static_cast<std::size_t>(i);
                row[ui] = accumulate(row[ui], ps.weight, scratch[ui]);
                scratch[ui] = 0.0f;
            }
        }
    }
    if (stats) {
        stats->evaluated += evaluated;
    }
}

GridRect add_segment_coverage(const FieldStroke& stroke, std::size_t segment, float aspect,
                              const PixelGrid& grid, std::span<float> coverage,
                              RasterStats* stats) {
    check_grid(grid, "add_segment_coverage");
    const std::size_t width = static_cast<std::size_t>(grid.width());
    if (coverage.size() != width * static_cast<std::size_t>(grid.height())) {
        throw std::invalid_argument("add_segment_coverage: coverage buffer has the wrong size");
    }
    if (segment >= segment_count(stroke)) {
        throw std::invalid_argument("add_segment_coverage: no such segment");
    }
    if (!valid_aspect(aspect)) {
        throw std::invalid_argument("add_segment_coverage: invalid aspect");
    }
    const Mapping m = mapping_for(aspect, grid);
    const Brush brush = brush_for(stroke);
    const Segment seg = segment_for(stroke, segment, m);
    const GridRect box = bounds_for(seg, brush, m, grid);
    for (int j = box.j_begin; j < box.j_end; ++j) {
        const float Y = pixel_y(m, grid, j);
        float* row = coverage.data() + static_cast<std::size_t>(j) * width;
        for (int i = box.i_begin; i < box.i_end; ++i) {
            const float c = coverage_at(seg, brush, pixel_x(m, grid, i), Y);
            row[i] = std::max(row[i], c);
        }
    }
    if (stats) {
        stats->evaluated += box.area();
    }
    return box;
}

void apply_stroke_coverage(std::span<const float> before, float weight,
                           std::span<const float> coverage, const PixelGrid& grid,
                           const GridRect& rect, std::span<float> field) {
    check_grid(grid, "apply_stroke_coverage");
    const std::size_t width = static_cast<std::size_t>(grid.width());
    const std::size_t n = width * static_cast<std::size_t>(grid.height());
    if (before.size() != n || coverage.size() != n || field.size() != n) {
        throw std::invalid_argument("apply_stroke_coverage: buffer has the wrong size");
    }
    if (rect.empty()) {
        return;
    }
    if (rect.i_begin < 0 || rect.j_begin < 0 || rect.i_end > grid.width() ||
        rect.j_end > grid.height()) {
        throw std::invalid_argument("apply_stroke_coverage: rectangle outside the grid");
    }
    for (int j = rect.j_begin; j < rect.j_end; ++j) {
        const std::size_t base = static_cast<std::size_t>(j) * width;
        for (int i = rect.i_begin; i < rect.i_end; ++i) {
            const std::size_t p = base + static_cast<std::size_t>(i);
            field[p] = accumulate(before[p], weight, coverage[p]);
        }
    }
}

void apply_stroke_coverage(std::span<const float> before, float weight,
                           std::span<const float> coverage, const GridRect& coverage_box,
                           const PixelGrid& grid, const GridRect& rect, std::span<float> field) {
    check_grid(grid, "apply_stroke_coverage");
    const std::size_t width = static_cast<std::size_t>(grid.width());
    const std::size_t n = width * static_cast<std::size_t>(grid.height());
    if (before.size() != n || field.size() != n || coverage.size() != coverage_box.area()) {
        throw std::invalid_argument("apply_stroke_coverage: buffer has the wrong size");
    }
    if (rect.empty()) {
        return;
    }
    if (coverage_box.i_begin < 0 || coverage_box.j_begin < 0 ||
        coverage_box.i_end > grid.width() || coverage_box.j_end > grid.height() ||
        rect.i_begin < coverage_box.i_begin || rect.j_begin < coverage_box.j_begin ||
        rect.i_end > coverage_box.i_end || rect.j_end > coverage_box.j_end) {
        throw std::invalid_argument("apply_stroke_coverage: rectangle outside the coverage box");
    }
    const std::size_t box_width = static_cast<std::size_t>(coverage_box.i_end - coverage_box.i_begin);
    for (int j = rect.j_begin; j < rect.j_end; ++j) {
        const std::size_t base = static_cast<std::size_t>(j) * width;
        const float* cov =
            coverage.data() + static_cast<std::size_t>(j - coverage_box.j_begin) * box_width;
        for (int i = rect.i_begin; i < rect.i_end; ++i) {
            const std::size_t p = base + static_cast<std::size_t>(i);
            field[p] = accumulate(before[p], weight,
                                  cov[static_cast<std::size_t>(i - coverage_box.i_begin)]);
        }
    }
}

PixelGrid sub_grid(const PixelGrid& grid, const GridRect& box) noexcept {
    PixelGrid g = grid;
    g.x0 = grid.x0 + box.i_begin * grid.stride;
    g.y0 = grid.y0 + box.j_begin * grid.stride;
    g.cols = box.i_end - box.i_begin;
    g.rows = box.j_end - box.j_begin;
    return g;
}

float min_point_spacing(float radius, int long_side_px) noexcept {
    const float one_px = long_side_px > 0 ? 1.0f / static_cast<float>(long_side_px) : 0.0f;
    return std::max(0.15f * radius, one_px);
}

bool far_enough(FieldPoint last, FieldPoint next, float aspect, float spacing) noexcept {
    if (!valid_aspect(aspect)) {
        return true;
    }
    const float dx = (next.u - last.u) * std::min(aspect, 1.0f);
    const float dy = (next.v - last.v) * std::min(1.0f / aspect, 1.0f);
    const float dx2 = dx * dx;
    const float dy2 = dy * dy;
    const float s2 = spacing * spacing;
    return dx2 + dy2 >= s2;
}

}  // namespace prism

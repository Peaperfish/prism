#include "prism/stroke_field.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
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
    if (framing) {
        return std::max(cols, 0);
    }
    const int n = (full_w > x0 && x0 >= 0 && stride > 0) ? (full_w - x0 + stride - 1) / stride : 0;
    return cols > 0 ? std::min(n, cols) : n;
}

int PixelGrid::height() const noexcept {
    if (framing) {
        return std::max(rows, 0);
    }
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

// How a grid's pixels land on the photo: the Mapping, and for a framed grid its FrameMap, made once
// per grid so sin and cos are worked out once.
struct Placement {
    Mapping m;
    PixelGrid g;
    std::optional<FrameMap> map;  // framed grids only
    double scale = 1.0;
    bool turned = false;  // framed at an angle other than exactly 0
    // Every grid pixel's photo position (before it is moved onto the photo) lies in
    // [reach_x0, reach_x1] x [reach_y0, reach_y1], in photo pixels. Framed grids only.
    double reach_x0 = 0.0;
    double reach_x1 = 0.0;
    double reach_y0 = 0.0;
    double reach_y1 = 0.0;
};

// The frame point of image pixel (x, y)'s centre: ((x + 0.5) * scale, (y + 0.5) * scale).
FramePoint frame_point(const Placement& p, int x, int y) noexcept {
    return {(x + 0.5) * p.scale, (y + 0.5) * p.scale};
}

// A photo position (in photo pixels) in long-side units, moved first onto the photo: to the
// nearest pixel centre's coordinate if it is off the photo. This is the whole of what an
// unframed grid's (x + 0.5) * kx does, for a position that is not a whole number plus a half.
float to_long_side(double photo_px, int full, float k) noexcept {
    const double on_photo = std::clamp(photo_px, 0.5, static_cast<double>(full) - 0.5);
    return static_cast<float>(on_photo) * k;
}

// Grid pixel (i, j)'s position in long-side units on a turned grid, worked out from (i, j) alone.
void turned_position(const Placement& p, int i, int j, float& X, float& Y) noexcept {
    const PixelGrid& g = p.g;
    const FramePoint q = p.map->to_photo_pixels(frame_point(p, g.x0 + i * g.stride, g.y0 + j * g.stride));
    X = to_long_side(q.x, g.full_w, p.m.kx);
    Y = to_long_side(q.y, g.full_h, p.m.ky);
}

// Column i's and row j's positions on a framed grid at an angle of exactly 0. FrameMap's
// to_photo_pixels gives x = W / 2 + (cos * dx + sin * dy) with cos exactly 1 and sin exactly 0
// there, so sin * dy is a zero and x is W / 2 + dx whatever dy is (and the same for y): these are,
// bit for bit, the per-pixel positions turned_position would give at 0 degrees.
float framed_column_x(const Placement& p, int i) noexcept {
    const PixelGrid& g = p.g;
    return to_long_side(p.map->to_photo_pixels(frame_point(p, g.x0 + i * g.stride, 0)).x, g.full_w,
                        p.m.kx);
}

float framed_row_y(const Placement& p, int j) noexcept {
    const PixelGrid& g = p.g;
    return to_long_side(p.map->to_photo_pixels(frame_point(p, 0, g.y0 + j * g.stride)).y, g.full_h,
                        p.m.ky);
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
        (!g.framing && (g.x0 >= g.full_w || g.y0 >= g.full_h)) ||
        (g.framing && (g.cols <= 0 || g.rows <= 0))) {
        throw std::invalid_argument(std::string(who) + ": invalid pixel grid");
    }
    if (g.framing) {
        if (!(std::isfinite(g.framing->scale) && g.framing->scale > 0.0f)) {
            throw std::invalid_argument(std::string(who) +
                                        ": a framed grid's scale must be finite and above 0");
        }
        const std::vector<std::string> problems = validate_frame(g.framing->frame());
        if (!problems.empty()) {
            std::string message = std::string(who) + ": invalid frame:";
            for (const std::string& problem : problems) {
                message += " " + problem + ".";
            }
            throw std::invalid_argument(message);
        }
    }
}

// The Placement of a grid that passed check_grid, for a field of a valid `aspect`.
Placement placement_for(float aspect, const PixelGrid& g) {
    Placement p;
    p.m = mapping_for(aspect, g);
    p.g = g;
    if (!g.framing) {
        return p;
    }
    p.map.emplace(g.framing->frame(), g.full_w, g.full_h);
    p.scale = static_cast<double>(g.framing->scale);
    p.turned = g.framing->angle != 0.0f;
    // The map is affine, so the grid's positions lie inside the box of its four corner pixels'.
    const int last_x = g.x0 + (g.width() - 1) * g.stride;
    const int last_y = g.y0 + (g.height() - 1) * g.stride;
    bool first = true;
    for (const int x : {g.x0, last_x}) {
        for (const int y : {g.y0, last_y}) {
            const FramePoint q = p.map->to_photo_pixels(frame_point(p, x, y));
            p.reach_x0 = first ? q.x : std::min(p.reach_x0, q.x);
            p.reach_x1 = first ? q.x : std::max(p.reach_x1, q.x);
            p.reach_y0 = first ? q.y : std::min(p.reach_y0, q.y);
            p.reach_y1 = first ? q.y : std::max(p.reach_y1, q.y);
            first = false;
        }
    }
    return p;
}

// One axis of a segment's box on the photo for a framed grid: photo pixels [lo, hi] (positions,
// continuous) whose long-side coordinate lies within `r` of [c_min, c_max], widened by
// box_margin_px. Positions are moved onto [0.5, full - 0.5] before use, so if that range does not
// meet the box nothing on the grid can be covered (false), and if the box takes in an end of it,
// every position past that end is in too: the box then runs out to the grid's reach.
bool framed_axis_box(double c_min, double c_max, double r, double k, int full, double reach_lo,
                     double reach_hi, double& lo, double& hi) noexcept {
    lo = (c_min - r) / k - box_margin_px;
    hi = (c_max + r) / k + box_margin_px;
    const double first = 0.5;
    const double last = static_cast<double>(full) - 0.5;
    if (!(lo <= hi) || hi < first || lo > last) {
        return false;  // off the photo (or NaN)
    }
    if (lo <= first) {
        lo = std::min(reach_lo, first) - box_margin_px;
    }
    if (hi >= last) {
        hi = std::max(reach_hi, last) + box_margin_px;
    }
    return true;
}

// bounds_for on a framed grid: the photo box's corners mapped back onto the grid (the map is
// affine, so the box's image is inside their bounding box), widened by one grid pixel.
GridRect framed_bounds_for(const Segment& seg, const Brush& brush, const Placement& p) noexcept {
    GridRect rect;
    if (!brush.draws) {
        return rect;
    }
    const PixelGrid& g = p.g;
    const double ax = seg.ax;
    const double ay = seg.ay;
    const double bx = ax + static_cast<double>(seg.dx);
    const double by = ay + static_cast<double>(seg.dy);
    double x_lo = 0.0;
    double x_hi = 0.0;
    double y_lo = 0.0;
    double y_hi = 0.0;
    if (!framed_axis_box(std::min(ax, bx), std::max(ax, bx), brush.r, p.m.kx, g.full_w,
                         p.reach_x0, p.reach_x1, x_lo, x_hi) ||
        !framed_axis_box(std::min(ay, by), std::max(ay, by), brush.r, p.m.ky, g.full_h,
                         p.reach_y0, p.reach_y1, y_lo, y_hi)) {
        return rect;
    }
    const double w = g.full_w;
    const double h = g.full_h;
    double i_min = std::numeric_limits<double>::infinity();
    double i_max = -i_min;
    double j_min = i_min;
    double j_max = -i_min;
    for (const double px : {x_lo, x_hi}) {
        for (const double py : {y_lo, y_hi}) {
            const FramePoint f = p.map->to_frame({px / w, py / h});
            // Frame point -> image pixel (centre at x + 0.5) -> grid index.
            const double i = (f.x / p.scale - 0.5 - g.x0) / g.stride;
            const double j = (f.y / p.scale - 0.5 - g.y0) / g.stride;
            i_min = std::min(i_min, i);
            i_max = std::max(i_max, i);
            j_min = std::min(j_min, j);
            j_max = std::max(j_max, j);
        }
    }
    const auto range = [](double lo, double hi, int count, int& begin, int& end) {
        begin = 0;
        end = 0;
        lo = std::ceil(lo) - 1.0;
        hi = std::floor(hi) + 1.0;
        if (!(lo <= hi) || hi < 0.0 || lo > static_cast<double>(count - 1)) {
            return;
        }
        begin = static_cast<int>(std::max(lo, 0.0));
        end = static_cast<int>(std::min(hi, static_cast<double>(count - 1))) + 1;
    };
    range(i_min, i_max, g.width(), rect.i_begin, rect.i_end);
    range(j_min, j_max, g.height(), rect.j_begin, rect.j_end);
    if (rect.empty()) {
        rect = {};
    }
    return rect;
}

GridRect bounds_on(const Segment& seg, const Brush& brush, const Placement& p) noexcept {
    return p.map ? framed_bounds_for(seg, brush, p) : bounds_for(seg, brush, p.m, p.g);
}

// Where the rasterising loops get each pixel's position from, one kind per grid. row(j, lo, hi)
// comes first on each row, with [lo, hi) holding every column that row will ask about; then x(i)
// and y(i) give pixel (i, j)'s position in long-side units.

// A grid without framing: as it always was.
struct AxisPositions {
    static constexpr bool per_pixel = false;
    const Mapping& m;
    const PixelGrid& g;
    float Y = 0.0f;
    void row(int j, int /*lo*/, int /*hi*/) noexcept { Y = pixel_y(m, g, j); }
    [[nodiscard]] float x(int i) const noexcept { return pixel_x(m, g, i); }
    [[nodiscard]] float y(int /*i*/) const noexcept { return Y; }
};

// A framed grid at exactly 0 degrees: a column's position from a table, a row's once per row.
struct FramedAxisPositions {
    static constexpr bool per_pixel = false;
    const Placement& p;
    const float* columns;  // columns[i - first] is column i's position
    int first;
    float Y = 0.0f;
    void row(int j, int /*lo*/, int /*hi*/) noexcept { Y = framed_row_y(p, j); }
    [[nodiscard]] float x(int i) const noexcept { return columns[i - first]; }
    [[nodiscard]] float y(int /*i*/) const noexcept { return Y; }
};

// A turned grid: each pixel of [lo, hi) on the row worked out on its own (turned_position), into
// two rows of floats.
struct TurnedPositions {
    static constexpr bool per_pixel = true;
    const Placement& p;
    std::vector<float> xs;
    std::vector<float> ys;
    std::size_t mapped = 0;
    explicit TurnedPositions(const Placement& place)
        : p(place),
          xs(static_cast<std::size_t>(place.g.width())),
          ys(static_cast<std::size_t>(place.g.width())) {}
    void row(int j, int lo, int hi) noexcept {
        for (int i = lo; i < hi; ++i) {
            turned_position(p, i, j, xs[static_cast<std::size_t>(i)], ys[static_cast<std::size_t>(i)]);
        }
        mapped += static_cast<std::size_t>(std::max(hi - lo, 0));
    }
    [[nodiscard]] float x(int i) const noexcept { return xs[static_cast<std::size_t>(i)]; }
    [[nodiscard]] float y(int i) const noexcept { return ys[static_cast<std::size_t>(i)]; }
};

// Column positions [begin, end) of a framed grid at 0 degrees.
std::vector<float> framed_columns(const Placement& p, int begin, int end) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(std::max(end - begin, 0)));
    for (int i = begin; i < end; ++i) {
        out.push_back(framed_column_x(p, i));
    }
    return out;
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
    if (!grid.framing) {
        const Mapping m = mapping_for(aspect, grid);
        return bounds_for(segment_for(stroke, segment, m), brush_for(stroke), m, grid);
    }
    try {
        check_grid(grid, "segment_bounds");
        const Placement p = placement_for(aspect, grid);
        return bounds_on(segment_for(stroke, segment, p.m), brush_for(stroke), p);
    } catch (...) {
        return {};  // an invalid framed grid (or no memory for the message saying so)
    }
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
    Placement place;
    std::vector<float> columns;           // a framed grid at 0 degrees: every column's position
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
        data->place = placement_for(field.aspect, grid);
        const Mapping& m = data->place.m;
        if (data->place.map && !data->place.turned) {
            data->columns = framed_columns(data->place, 0, grid.width());
        }
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
                Segment seg = segment_for(*sp, k, m);
                seg.box = bounds_on(seg, ps.brush, data->place);
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

namespace {

// One run: the segments of one stroke whose boxes meet the rows being rasterised.
struct Run {
    const PreparedStroke* stroke = nullptr;
    std::size_t first = 0;  // into `segments`
    std::size_t count = 0;
    GridRect box;  // union of these segments' boxes
};

// The rows [row_begin, row_end) of the field, with positions from `pos`; see rasterise().
template <class Positions>
std::size_t rasterise_rows(Positions& pos, int gw, int row_begin, int row_end,
                           const std::vector<Run>& runs, const std::vector<const Segment*>& segments,
                           std::span<float> out) {
    const std::size_t width = static_cast<std::size_t>(gw);
    // One row of the current stroke's coverage (the max over its segments), kept all zero
    // between uses.
    std::vector<float> scratch(width, 0.0f);
    std::size_t evaluated = 0;
    for (int j = row_begin; j < row_end; ++j) {
        int row_lo = 0;
        int row_hi = 0;
        if constexpr (Positions::per_pixel) {
            // Every column a segment on this row asks about: only those are mapped.
            row_lo = gw;
            for (const Run& run : runs) {
                if (j >= run.box.j_begin && j < run.box.j_end) {
                    row_lo = std::min(row_lo, run.box.i_begin);
                    row_hi = std::max(row_hi, run.box.i_end);
                }
            }
            if (row_lo >= row_hi) {
                continue;
            }
        }
        pos.row(j, row_lo, row_hi);
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
                    const float c = coverage_at(seg, ps.brush, pos.x(i), pos.y(i));
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
    return evaluated;
}

}  // namespace

void PreparedField::rasterise(int row_begin, int row_end, std::span<float> out,
                              RasterStats* stats) const {
    const PixelGrid& grid = data_->grid;
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

    const Placement& place = data_->place;
    std::size_t evaluated = 0;
    std::size_t mapped = 0;
    if (!place.map) {
        AxisPositions pos{place.m, grid};
        evaluated = rasterise_rows(pos, gw, row_begin, row_end, runs, segments, out);
    } else if (!place.turned) {
        FramedAxisPositions pos{place, data_->columns.data(), 0};
        evaluated = rasterise_rows(pos, gw, row_begin, row_end, runs, segments, out);
    } else {
        TurnedPositions pos(place);
        evaluated = rasterise_rows(pos, gw, row_begin, row_end, runs, segments, out);
        mapped = pos.mapped;
    }
    if (stats) {
        stats->evaluated += evaluated;
        stats->turned_positions += mapped;
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
    const Placement place = placement_for(aspect, grid);
    const Brush brush = brush_for(stroke);
    const Segment seg = segment_for(stroke, segment, place.m);
    const GridRect box = bounds_on(seg, brush, place);
    const auto cover = [&](auto& pos) {
        for (int j = box.j_begin; j < box.j_end; ++j) {
            pos.row(j, box.i_begin, box.i_end);
            float* row = coverage.data() + static_cast<std::size_t>(j) * width;
            for (int i = box.i_begin; i < box.i_end; ++i) {
                const float c = coverage_at(seg, brush, pos.x(i), pos.y(i));
                row[i] = std::max(row[i], c);
            }
        }
    };
    std::size_t mapped = 0;
    if (!place.map) {
        AxisPositions pos{place.m, grid};
        cover(pos);
    } else if (!place.turned) {
        const std::vector<float> columns = framed_columns(place, box.i_begin, box.i_end);
        FramedAxisPositions pos{place, columns.data(), box.i_begin};
        cover(pos);
    } else {
        TurnedPositions pos(place);
        cover(pos);
        mapped = pos.mapped;
    }
    if (stats) {
        stats->evaluated += box.area();
        stats->turned_positions += mapped;
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

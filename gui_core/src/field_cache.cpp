#include "prism/gui/field_cache.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <span>

#include "prism/dodge_burn.hpp"
#include "prism/parallel.hpp"

namespace prism::gui {

namespace {

bool same_grid(const PixelGrid& a, const PixelGrid& b) noexcept {
    return a.full_w == b.full_w && a.full_h == b.full_h && a.x0 == b.x0 && a.y0 == b.y0 &&
           a.stride == b.stride && a.width() == b.width() && a.height() == b.height();
}

// The first `n` strokes of both are the same objects.
bool same_strokes(const StrokeField& a, const StrokeField& b, std::size_t n) noexcept {
    return a.strokes.size() >= n && b.strokes.size() >= n &&
           std::equal(a.strokes.begin(), a.strokes.begin() + static_cast<std::ptrdiff_t>(n),
                      b.strokes.begin());
}

bool same_bits(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// `now` is `was` with points added at the end, and the same brush.
bool grown_from(const FieldStroke& was, const FieldStroke& now) noexcept {
    return same_bits(was.radius, now.radius) && same_bits(was.softness, now.softness) &&
           same_bits(was.weight, now.weight) && now.points.size() >= was.points.size() &&
           std::equal(was.points.begin(), was.points.end(), now.points.begin(),
                      [](const FieldPoint& p, const FieldPoint& q) {
                          return same_bits(p.u, q.u) && same_bits(p.v, q.v);
                      });
}

GridRect unite(const GridRect& a, const GridRect& b) noexcept {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    return {std::min(a.i_begin, b.i_begin), std::max(a.i_end, b.i_end),
            std::min(a.j_begin, b.j_begin), std::max(a.j_end, b.j_end)};
}

bool holds(const GridRect& outer, const GridRect& inner) noexcept {
    return inner.empty() ||
           (!outer.empty() && inner.i_begin >= outer.i_begin && inner.i_end <= outer.i_end &&
            inner.j_begin >= outer.j_begin && inner.j_end <= outer.j_end);
}

std::size_t pixels_of(const PixelGrid& g) noexcept {
    return static_cast<std::size_t>(g.width()) * static_cast<std::size_t>(g.height());
}

}  // namespace

std::string_view field_path_name(FieldPath path) noexcept {
    switch (path) {
        case FieldPath::none: return "none";
        case FieldPath::fresh: return "fresh";
        case FieldPath::reuse: return "reuse";
        case FieldPath::grow: return "grow";
        case FieldPath::append: return "append";
        case FieldPath::undo: return "undo";
    }
    return "?";
}

std::size_t FieldCache::bytes() const noexcept {
    return (field_.capacity() + before_.capacity() + coverage_.capacity()) * sizeof(float);
}

void FieldCache::clear() noexcept {
    grid_ = {};
    held_.reset();
    std::vector<float>().swap(field_);  // `= {}` would keep the memory
    std::vector<float>().swap(before_);
    std::vector<float>().swap(coverage_);
    coverage_box_ = {};
    have_before_ = false;
    last_path_ = FieldPath::none;
}

FieldPath FieldCache::update(const Strokes& strokes, const PixelGrid& grid, int threads) {
    try {
        if (strokes.empty()) {
            clear();
            last_path_ = FieldPath::fresh;
            return last_path_;
        }
        const std::shared_ptr<const StrokeField>& now = strokes.field();
        if (held_ && same_grid(grid, grid_) && same_bits(now->aspect, held_->aspect)) {
            const StrokeField& was = *held_;
            const std::size_t n = now->strokes.size();
            const std::size_t had = was.strokes.size();
            if (now == held_ || (n == had && same_strokes(*now, was, n))) {
                held_ = now;
                last_path_ = FieldPath::reuse;
                return last_path_;
            }
            if (have_before_ && n == had && same_strokes(*now, was, n - 1) &&
                grown_from(*was.strokes.back(), *now->strokes.back())) {
                const std::size_t from = segment_count(*was.strokes.back());
                held_ = now;
                add_segments(from);
                last_path_ = FieldPath::grow;
                return last_path_;
            }
            if (n == had + 1 && same_strokes(*now, was, had)) {
                before_ = field_;
                coverage_box_ = {};
                coverage_.clear();
                have_before_ = true;
                held_ = now;
                add_segments(0);
                last_path_ = FieldPath::append;
                return last_path_;
            }
            if (have_before_ && n + 1 == had && same_strokes(*now, was, n)) {
                field_.swap(before_);
                before_ = {};
                coverage_ = {};
                coverage_box_ = {};
                have_before_ = false;
                held_ = now;
                last_path_ = FieldPath::undo;
                return last_path_;
            }
        }
        grid_ = grid;
        held_ = now;
        fresh(threads);
        last_path_ = FieldPath::fresh;
        return last_path_;
    } catch (...) {
        clear();
        throw;
    }
}

void FieldCache::fresh(int threads) {
    const std::size_t pixels = pixels_of(grid_);
    const std::size_t width = static_cast<std::size_t>(grid_.width());
    // F_before: every stroke but the last, on up to `threads` threads, a tile of rows at a time
    // (the stage's own way of rasterising), straight into the buffer.
    before_.assign(pixels, 0.0f);
    StrokeField earlier;
    earlier.aspect = held_->aspect;
    earlier.strokes.assign(held_->strokes.begin(), held_->strokes.end() - 1);
    if (!earlier.strokes.empty() && pixels > 0) {
        const PreparedField prepared(earlier, grid_);
        const int n = threads_for_pixels(pixels, threads);
        std::exception_ptr error;
        std::mutex error_mutex;
        parallel_for_rows(grid_.height(), n, [&](int begin, int end) {
            try {
                for (int t = begin; t < end; t += dodge_burn_tile_rows) {
                    const int t_end = std::min(t + dodge_burn_tile_rows, end);
                    if (prepared.rows_untouched(t, t_end)) {
                        continue;
                    }
                    prepared.rasterise(
                        t, t_end,
                        std::span<float>(before_.data() + static_cast<std::size_t>(t) * width,
                                         static_cast<std::size_t>(t_end - t) * width));
                }
            } catch (...) {
                const std::lock_guard lock(error_mutex);
                if (!error) {
                    error = std::current_exception();
                }
            }
        });
        if (error) {
            std::rethrow_exception(error);
        }
    }
    // Then the last stroke, as if it had just been added.
    field_ = before_;
    coverage_ = {};
    coverage_box_ = {};
    have_before_ = true;
    add_segments(0);
}

void FieldCache::hold_box(const GridRect& need) {
    if (holds(coverage_box_, need)) {
        return;
    }
    // Room to grow on every side, so that a stroke being drawn does not move S at every point.
    const int pad_i = std::max(16, grid_.width() / 16);
    const int pad_j = std::max(16, grid_.height() / 16);
    const GridRect u = unite(coverage_box_, need);
    const GridRect box{std::max(0, u.i_begin - pad_i), std::min(grid_.width(), u.i_end + pad_i),
                       std::max(0, u.j_begin - pad_j), std::min(grid_.height(), u.j_end + pad_j)};
    std::vector<float> moved(box.area(), 0.0f);
    if (!coverage_box_.empty()) {
        const std::size_t old_w = static_cast<std::size_t>(coverage_box_.i_end - coverage_box_.i_begin);
        const std::size_t new_w = static_cast<std::size_t>(box.i_end - box.i_begin);
        for (int j = coverage_box_.j_begin; j < coverage_box_.j_end; ++j) {
            std::memcpy(moved.data() + static_cast<std::size_t>(j - box.j_begin) * new_w +
                            static_cast<std::size_t>(coverage_box_.i_begin - box.i_begin),
                        coverage_.data() + static_cast<std::size_t>(j - coverage_box_.j_begin) * old_w,
                        old_w * sizeof(float));
        }
    }
    coverage_.swap(moved);
    coverage_box_ = box;
}

void FieldCache::add_segments(std::size_t from) {
    const FieldStroke& stroke = *held_->strokes.back();
    const std::size_t to = segment_count(stroke);
    if (from >= to) {
        return;
    }
    const float aspect = held_->aspect;
    GridRect need;
    for (std::size_t k = from; k < to; ++k) {
        need = unite(need, segment_bounds(stroke, k, aspect, grid_));
    }
    if (need.empty()) {
        return;  // every new segment is off the grid: S and F stay as they are
    }
    hold_box(need);
    const PixelGrid sub = sub_grid(grid_, coverage_box_);
    GridRect changed;
    for (std::size_t k = from; k < to; ++k) {
        const GridRect r = add_segment_coverage(stroke, k, aspect, sub, coverage_);
        if (!r.empty()) {
            changed = unite(changed, GridRect{r.i_begin + coverage_box_.i_begin,
                                              r.i_end + coverage_box_.i_begin,
                                              r.j_begin + coverage_box_.j_begin,
                                              r.j_end + coverage_box_.j_begin});
        }
    }
    apply_stroke_coverage(before_, stroke.weight, coverage_, coverage_box_, grid_, changed, field_);
}

bool field_at(std::span<const float> field, const PixelGrid& grid, const PixelGrid& part,
              std::vector<float>& out) {
    out.clear();
    const int w = part.width();
    const int h = part.height();
    if (grid.stride <= 0 || part.stride <= 0 || part.full_w != grid.full_w ||
        part.full_h != grid.full_h || field.size() != pixels_of(grid) ||
        part.stride % grid.stride != 0 || part.x0 < grid.x0 || part.y0 < grid.y0 ||
        (part.x0 - grid.x0) % grid.stride != 0 || (part.y0 - grid.y0) % grid.stride != 0) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        return true;
    }
    const int step = part.stride / grid.stride;
    const int i0 = (part.x0 - grid.x0) / grid.stride;
    const int j0 = (part.y0 - grid.y0) / grid.stride;
    if (i0 + (w - 1) * step >= grid.width() || j0 + (h - 1) * step >= grid.height()) {
        return false;
    }
    const std::size_t width = static_cast<std::size_t>(grid.width());
    out.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    float* to = out.data();
    for (int j = 0; j < h; ++j) {
        const float* row = field.data() + static_cast<std::size_t>(j0 + j * step) * width;
        if (step == 1) {
            to = std::copy_n(row + i0, w, to);
        } else {
            for (int i = 0; i < w; ++i) {
                *to++ = row[i0 + i * step];
            }
        }
    }
    return true;
}

std::uint8_t stroke_mask_byte(float field) noexcept {
    if (std::isnan(field)) {
        return 128;
    }
    const float scaled = std::clamp(field / 2.0f * 127.0f, -127.0f, 127.0f);
    return static_cast<std::uint8_t>(128 + static_cast<int>(std::round(scaled)));
}

}  // namespace prism::gui

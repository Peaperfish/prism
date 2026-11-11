#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "prism/gui/settings.hpp"
#include "prism/stroke_field.hpp"

namespace prism::gui {

// How FieldCache::update got to the field of the strokes it was given.
enum class FieldPath {
    none,    // update() was never called (or the cache was cleared)
    fresh,   // rasterised from scratch (or no strokes: nothing to keep)
    reuse,   // the same strokes as last time: nothing to do
    grow,    // the last stroke has more points: only its new segments were rasterised
    append,  // one stroke more: only that stroke was rasterised
    undo,    // the last stroke is gone: the field from before it was kept, and is taken back
};

// "fresh", "reuse", ... for messages.
[[nodiscard]] std::string_view field_path_name(FieldPath path) noexcept;

// The stroke field of the preview, kept from one render to the next so that a render does not
// rasterise every stroke again: while a stroke is drawn, only its newest segments are.
//
// It holds, for one grid:
//   field    F: the field of all its strokes (what the Dodge & burn stage reads);
//   before   F_before: the field of all strokes but the last;
//   coverage S: the last stroke's coverage (the max over its segments), only over a box of the
//            grid that holds every segment of it;
// with F = F_before + weight * S inside that box and F = F_before outside it. Those are exactly
// the operations rasterise_field does, stroke after stroke (stroke_field.hpp, the incremental
// API), so whichever path update() takes, F is bit-identical to rasterising the strokes afresh.
//
// Not thread-safe, and not meant to be: the preview worker thread owns one and is the only thread
// that calls update(). The field() it hands out may be read by several threads at once (the test
// strip's bands) until the next update().
class FieldCache {
public:
    // Brings the cache to `strokes` on `grid` and returns the path it took:
    //   the same handle (or the same strokes, by pointer)                    -> reuse
    //   the same strokes but the last, which has the same brush and the old
    //   points followed by new ones                                         -> grow
    //   the same strokes and one more                                       -> append
    //   the strokes before the last one, while F_before is still that field  -> undo
    //   anything else: another grid, an aspect change, an earlier stroke
    //   changed or removed, a brush change, no strokes                       -> fresh
    // "Same" is by pointer: strokes are never changed once made (Strokes). The fresh path
    // rasterises on up to `threads` threads (0 = one per hardware thread). Throws what
    // rasterise_field throws (an invalid aspect); the cache is then empty.
    FieldPath update(const Strokes& strokes, const PixelGrid& grid, int threads);

    // The field of the strokes of the last update(), on its grid (grid().width() x
    // grid().height() floats, row after row). Empty if those had no strokes.
    [[nodiscard]] std::span<const float> field() const noexcept { return field_; }
    [[nodiscard]] const PixelGrid& grid() const noexcept { return grid_; }
    [[nodiscard]] FieldPath last_path() const noexcept { return last_path_; }
    // How many times a render has handed this cache's field to a Dodge & burn stage (the plain
    // picture, each test strip band, the clip report's sample grid) since the cache was made. For
    // tests: a render that stops reading the cache rasterises every stroke again, with the same
    // picture. Counted from several threads (the bands), so only the total means anything.
    [[nodiscard]] std::size_t field_reads() const noexcept {
        return field_reads_.load(std::memory_order_relaxed);
    }
    void note_field_read() const noexcept { field_reads_.fetch_add(1, std::memory_order_relaxed); }
    // The memory the cache holds, in bytes (F, F_before and S).
    [[nodiscard]] std::size_t bytes() const noexcept;
    // Lets go of everything.
    void clear() noexcept;

private:
    void fresh(int threads);
    // Adds segments [from, end) of the last stroke to S, and updates F where S changed.
    void add_segments(std::size_t from);
    // Makes S's box hold `need` (in grid pixels), keeping what S already has.
    void hold_box(const GridRect& need);

    PixelGrid grid_{};
    std::shared_ptr<const StrokeField> held_;  // the strokes F is the field of; null: nothing held
    std::vector<float> field_;                 // F
    bool have_before_ = false;                 // before_ and coverage_ describe held_'s last stroke
    std::vector<float> before_;                // F_before
    std::vector<float> coverage_;              // S, over coverage_box_ only
    GridRect coverage_box_;
    FieldPath last_path_ = FieldPath::none;
    mutable std::atomic<std::size_t> field_reads_{0};
};

// The values of `field` (on `grid`) at the pixels of `part`, row after row, into `out`: for a
// render on a part of the picture (a test strip band, the clip report's sample grid) from the
// field of the whole. False, and `out` cleared, if some pixel of `part` is not a pixel of `grid`.
// The values are copied, so they are what rasterising on `part` gives (a pixel's field depends only
// on the pixel and the strokes).
bool field_at(std::span<const float> field, const PixelGrid& grid, const PixelGrid& part,
              std::vector<float>& out);

// One byte of the stroke mask (PreviewFrame::stroke_mask) for a field value in stops:
// 128 + clamp(round(field / 2 * 127), -127, 127). 128 is untouched (and a NaN), above it dodged,
// below it burned; 2 stops or more either way is the end of the range (255 or 1).
[[nodiscard]] std::uint8_t stroke_mask_byte(float field) noexcept;

}  // namespace prism::gui

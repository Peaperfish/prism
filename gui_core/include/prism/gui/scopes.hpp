#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace prism::gui {

// Colour scopes of the picture on screen: where the tones sit across the frame (RGB parade and
// luma waveform) and which hues and strengths it holds (vectorscope). They are measured on the
// display scale, from the 8-bit sRGB bytes the preview shows, so a scope always matches the
// picture it arrives with (and, like the histogram, is of the stage that picture was rendered for).
//
// Every grid is `size` x `size` counts of sampled pixels. Parade and waveform: row = level
// (0 = black, 255 = white), column = the sample's image x binned across the width into 256
// columns, index = level * size + column. Vectorscope: Rec. 709 Cb (column) and Cr (row) of the
// encoded values, with the neutral axis (greys) in cell 128, index = cr * size + cb.
//
// Only a strided grid of at most `max_samples` pixels is counted: scopes show a distribution, so
// they don't need every pixel, and the cost stays small however big the preview is.
//
// Note for a gamut warning (#14): these scopes are of clamped 8-bit values, which are always inside
// sRGB, so no vectorscope cell means "out of gamut". That needs the stage's float values before
// clamping (out of gamut = any channel < 0 or > 1 in the output space). The vectorscope could show
// those pixels if a future version binned the unclamped floats.
struct Scopes {
    static constexpr int size = 256;
    static constexpr std::uint64_t max_samples = 250'000;
    using Grid = std::vector<std::uint32_t>;  // size * size counts, see above

    Grid parade_red;
    Grid parade_green;
    Grid parade_blue;
    Grid waveform;     // Rec. 709 luma of the encoded values
    Grid vectorscope;  // Rec. 709 Cb / Cr of the encoded values

    std::uint64_t sample_count = 0;  // pixels counted (each is in every grid once)
    int stride = 1;                  // distance in x and y between counted pixels

    bool operator==(const Scopes&) const = default;
};

// A vectorscope cell: column = Cb, row = Cr, each 0..255 with 128 the neutral axis.
struct VectorscopeCell {
    int cb = 128;
    int cr = 128;

    bool operator==(const VectorscopeCell&) const = default;
};

// The vectorscope cell of an 8-bit sRGB colour (what a sample of the picture contributes).
[[nodiscard]] VectorscopeCell vectorscope_cell(std::uint8_t r, std::uint8_t g,
                                               std::uint8_t b) noexcept;

// The vectorscope plot is round, and its outer ring is 128 cells from the neutral axis (the plot's
// half side). Cb/Cr are linear in R, G and B, so the farthest an 8-bit sRGB colour gets from the axis
// is at a corner of the cube: one of the six full-strength primaries and secondaries (the hexagon).
// That is green and magenta, about 152 unscaled cells out, past the ring that is inscribed in the Cb/Cr
// square. So the plot is scaled: every colour's offset is multiplied by vectorscope_scale(), which
// puts the farthest corner on the ring and everything else inside it. The 75% targets, the skin-tone
// line and the cells all use the same mapping, so nothing needs clipping.
inline constexpr int vectorscope_ring_cells = 128;

// A colour's place on the plot: cells right of (cb) and above (cr) the neutral axis, after scaling.
struct VectorscopePoint {
    float cb = 0.0f;
    float cr = 0.0f;
};

// The distance in unscaled cells of the farthest hexagon corner, computed from the six corners.
[[nodiscard]] float vectorscope_farthest_cells() noexcept;
// vectorscope_ring_cells / vectorscope_farthest_cells().
[[nodiscard]] float vectorscope_scale() noexcept;
// The exact (unrounded) scaled position of an 8-bit sRGB colour; vectorscope_cell rounds it.
[[nodiscard]] VectorscopePoint vectorscope_point(std::uint8_t r, std::uint8_t g,
                                                 std::uint8_t b) noexcept;

// Rec. 709 luma of 8-bit encoded values, rounded to 0..255 (greys keep their value).
[[nodiscard]] int luma8(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept;

// The hue a vectorscope cell is drawn in: what a colour at that angle looks like, more saturated the
// further the cell is from the neutral axis (which is a light grey).
struct ScopeRgb {
    std::uint8_t r = 0, g = 0, b = 0;

    bool operator==(const ScopeRgb&) const = default;
};
[[nodiscard]] ScopeRgb vectorscope_cell_colour(int cb, int cr) noexcept;

// A line across the parade and the waveform. They are on the display scale, so the lines go where
// these shares of white's light land on it: `level` is the 8-bit code (0..255), as in the histogram.
struct ScopeGuide {
    int level = 0;
    std::string_view label;
};
[[nodiscard]] std::span<const ScopeGuide> scope_guides();

// The scopes of a width x height picture of 8-bit sRGB `rgb8` (3 bytes per pixel, row-major).
// An empty picture, or one whose buffer is not width * height * 3 bytes, gives all-zero grids and
// sample_count 0; this never throws or reads out of range.
[[nodiscard]] Scopes compute_scopes(const std::vector<std::uint8_t>& rgb8, int width, int height);

}  // namespace prism::gui

#pragma once

#include <cstdint>
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

// Rec. 709 luma of 8-bit encoded values, rounded to 0..255 (greys keep their value).
[[nodiscard]] int luma8(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept;

// The scopes of a width x height picture of 8-bit sRGB `rgb8` (3 bytes per pixel, row-major).
// An empty picture, or one whose buffer is not width * height * 3 bytes, gives all-zero grids and
// sample_count 0; this never throws or reads out of range.
[[nodiscard]] Scopes compute_scopes(const std::vector<std::uint8_t>& rgb8, int width, int height);

}  // namespace prism::gui

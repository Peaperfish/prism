#pragma once

#include <array>
#include <cstdint>

#include "prism/image.hpp"

namespace prism {

// Per-channel histograms of the final 8-bit sRGB values, i.e. the bytes save_image writes.
// This matches the histogram a camera or Lightroom shows, rather than one of linear light.
struct Histogram {
    static constexpr int bins = 256;
    using Counts = std::array<std::uint64_t, bins>;

    Counts red{};
    Counts green{};
    Counts blue{};
    // Rec. 709 weighted sum of the encoded R, G and B (strictly "luma", Y'), as cameras use.
    Counts luma{};
    std::uint64_t pixel_count = 0;
};

[[nodiscard]] Histogram compute_histogram(const Image& img);

// Percentage (0-100) of pixels that fall in `bin` of `counts`, which should be one of h's
// members. For example bin_percent(h, h.red, 255) is the share of pixels with red clipped.
// Returns 0 for an empty image.
[[nodiscard]] double bin_percent(const Histogram& h, const Histogram::Counts& counts, int bin);

// Draws the histogram as a width x height image: luma as a grey fill at the back, with
// R, G and B added on top (red + green overlap shows yellow, all three show white).
// All curves share one vertical scale, set by the tallest bin other than 0 and 255 (clipped
// spikes are cut off at the top). Throws std::invalid_argument if width or height is not positive.
[[nodiscard]] Image render_histogram(const Histogram& h, int width = 512, int height = 200);

}  // namespace prism

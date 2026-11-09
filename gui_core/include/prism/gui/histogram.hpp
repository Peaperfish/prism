#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "prism/image.hpp"

namespace prism::gui {

// Out-of-range sample counts for one channel, measured on the stage's float values before the
// 8-bit sRGB encode clamps [0, 1] to 0..255. Kept separate from the 256 in-range bins so clipped
// pixels are distinguished from in-range black (0) and white (1) and do not spike the end bins.
// +inf and -inf count as over-white and under-black; NaN counts in bin 0, as the encode shows it.
struct ChannelClips {
    std::uint64_t under_black = 0;  // values < 0
    std::uint64_t over_white = 0;   // values > 1
    // Values in [1 - file_ceiling_epsilon, 1], counted only for a histogram of Decode (see
    // compute_stage_histogram's `file_ceiling`): already clipped in the file. They also stay in
    // the bins, so the graph is the same; the figures under the scope add them to over_white.
    std::uint64_t at_ceiling = 0;

    // What the readout's R, G and B figures count above white: the same as any_over_white counts.
    [[nodiscard]] std::uint64_t high() const noexcept { return over_white + at_ceiling; }

    bool operator==(const ChannelClips&) const = default;
};

// Per-channel and Rec. 709 luminance histograms of a pipeline stage's float image before the
// 8-bit encode, with separate under-black / over-white clip counts. The 256 bins are on the display
// scale: a value falls in the bin of its 8-bit sRGB code (linear 0.18 is in bin 118), so the graph
// matches what is on screen. Luminance is computed in linear light, then encoded the same way.
struct StageHistogram {
    static constexpr int bins = 256;
    using Counts = std::array<std::uint64_t, bins>;

    Counts red{};
    Counts green{};
    Counts blue{};
    Counts luminance{};  // Rec. 709: 0.2126 R + 0.7152 G + 0.0722 B

    ChannelClips red_clips{};
    ChannelClips green_clips{};
    ChannelClips blue_clips{};
    ChannelClips luminance_clips{};

    // Pixels with ANY of R, G, B out of range, each pixel counted once however many channels are: the
    // numbers under the scope, and what lights its clip triangles. Linear limits on the stage's
    // floats, by pixel_below_black and pixel_above_white (clipping.hpp), the very test the
    // clipping overlay uses for the same picture, not the 8-bit codes (a value of 0.999 encodes
    // to 255 and has lost nothing). So a tiny negative on a pixel that still has light is not
    // below black here, as it is not in the overlay. any_over_white also counts a channel at the
    // file's ceiling when the histogram is of Decode (see `file_ceiling` below), as the overlay
    // does. The per-channel counts above are plainer: any channel < 0, and over_white / at_ceiling.
    std::uint64_t any_under_black = 0;  // pixel_below_black
    std::uint64_t any_over_white = 0;   // pixel_above_white

    std::uint64_t pixel_count = 0;

    bool operator==(const StageHistogram&) const = default;
};

// Computes the stage histogram of `img` (before the 8-bit encode). `threads`: 0 = one per
// hardware thread, 1 = single-threaded; the result is identical for any thread count.
// `file_ceiling`: `img` is Decode's output, where a channel in [1 - file_ceiling_epsilon, 1] was
// already clipped in the file, so it counts in any_over_white (compute_clip_mask's flag of the same
// name).
// Throws std::invalid_argument if img.pixels.size() does not match width * height * 3.
[[nodiscard]] StageHistogram compute_stage_histogram(const Image& img, int threads = 0,
                                                     bool file_ceiling = false);

// Encodes `img` to 8-bit sRGB (bit-identical to encode_srgb8) and computes its stage histogram in
// one parallel pass over the image's rows, so preview rendering reads the float buffer once.
void encode_srgb8_with_histogram(const Image& img, int threads, std::vector<std::uint8_t>& rgb8,
                                 StageHistogram& histogram, bool file_ceiling = false);

// Percentage (0 to 100) of `pixel_count` represented by `count` (a clip count or bin count).
// Returns 0 for an empty image.
[[nodiscard]] double pixel_percent(std::uint64_t count, std::uint64_t pixel_count) noexcept;

// The line under the scope, its two ends: "0.1% below black" and "0.0% above white". One decimal,
// worked out in integers, so a comma locale can't change it. "0.0%" means no pixel at all: a share
// that would round to it reads "<0.1%", and one that would round to everything reads ">99.9%".
struct ScopeReadout {
    std::string below_black;
    std::string above_white;

    bool operator==(const ScopeReadout&) const = default;
};
[[nodiscard]] ScopeReadout scope_readout(const StageHistogram& h);

}  // namespace prism::gui

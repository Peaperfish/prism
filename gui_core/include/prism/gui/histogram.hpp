#pragma once

#include <array>
#include <cstdint>
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

    bool operator==(const ChannelClips&) const = default;
};

// Per-channel and Rec. 709 luminance histograms of a pipeline stage's float image before the
// 8-bit encode, with 256 bins over [0, 1] and separate under-black / over-white clip counts.
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

    std::uint64_t pixel_count = 0;

    bool operator==(const StageHistogram&) const = default;
};

// Computes the stage histogram of `img` (before the 8-bit encode). `threads`: 0 = one per
// hardware thread, 1 = single-threaded; the result is identical for any thread count.
// Throws std::invalid_argument if img.pixels.size() does not match width * height * 3.
[[nodiscard]] StageHistogram compute_stage_histogram(const Image& img, int threads = 0);

// Encodes `img` to 8-bit sRGB (bit-identical to encode_srgb8) and computes its stage histogram in
// one parallel pass over the image's rows, so preview rendering reads the float buffer once.
void encode_srgb8_with_histogram(const Image& img, int threads, std::vector<std::uint8_t>& rgb8,
                                 StageHistogram& histogram);

// Percentage (0 to 100) of `pixel_count` represented by `count` (a clip count or bin count).
// Returns 0 for an empty image.
[[nodiscard]] double pixel_percent(std::uint64_t count, std::uint64_t pixel_count) noexcept;

// Default threshold (in percent of pixels) above which the UI's shadow or highlight clip
// indicator lights up.
inline constexpr double default_clip_threshold_percent = 0.01;

// True if any of R, G or B has under-black / over-white clipping above `threshold_percent` of
// the image's pixels.
[[nodiscard]] bool has_shadow_clipping(
    const StageHistogram& h, double threshold_percent = default_clip_threshold_percent) noexcept;
[[nodiscard]] bool has_highlight_clipping(
    const StageHistogram& h, double threshold_percent = default_clip_threshold_percent) noexcept;

}  // namespace prism::gui

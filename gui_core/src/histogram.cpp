#include "prism/gui/histogram.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "prism/color.hpp"
#include "prism/parallel.hpp"

namespace prism::gui {

namespace {

std::size_t require_consistent(const Image& img, const char* caller) {
    if (img.width < 0 || img.height < 0) {
        throw std::invalid_argument(std::string(caller) + ": negative image dimensions");
    }
    const std::size_t expected = static_cast<std::size_t>(img.width) *
                                 static_cast<std::size_t>(img.height) * Image::channels;
    if (img.pixels.size() != expected) {
        throw std::invalid_argument(std::string(caller) +
                                    ": pixel buffer size does not match width * height * 3");
    }
    return expected;
}

void record_sample(float v, StageHistogram::Counts& bins, ChannelClips& clips) noexcept {
    if (v >= 0.0f && v <= 1.0f) {
        ++bins[static_cast<std::size_t>(v * 255.0f + 0.5f)];
    } else if (v < 0.0f) {
        ++clips.under_black;
    } else if (v > 1.0f) {
        ++clips.over_white;
    } else {
        // NaN: linear_to_srgb8 shows it as black, so count it as black rather than losing it
        // (every pixel is counted once per channel, in a bin or as a clip).
        ++bins[0];
    }
}

void count_range(const float* pixels, std::size_t first, std::size_t last,
                 StageHistogram& local) noexcept {
    for (std::size_t i = first; i < last; i += Image::channels) {
        const float r = pixels[i];
        const float g = pixels[i + 1];
        const float b = pixels[i + 2];
        const float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        record_sample(r, local.red, local.red_clips);
        record_sample(g, local.green, local.green_clips);
        record_sample(b, local.blue, local.blue_clips);
        record_sample(y, local.luminance, local.luminance_clips);
    }
    local.pixel_count += (last - first) / Image::channels;
}

void add_counts(StageHistogram::Counts& into, const StageHistogram::Counts& from) noexcept {
    for (int i = 0; i < StageHistogram::bins; ++i) {
        into[static_cast<std::size_t>(i)] += from[static_cast<std::size_t>(i)];
    }
}

void add_clips(ChannelClips& into, const ChannelClips& from) noexcept {
    into.under_black += from.under_black;
    into.over_white += from.over_white;
}

StageHistogram merge_partials(const std::vector<StageHistogram>& partial) noexcept {
    StageHistogram total;
    for (const StageHistogram& p : partial) {
        add_counts(total.red, p.red);
        add_counts(total.green, p.green);
        add_counts(total.blue, p.blue);
        add_counts(total.luminance, p.luminance);
        add_clips(total.red_clips, p.red_clips);
        add_clips(total.green_clips, p.green_clips);
        add_clips(total.blue_clips, p.blue_clips);
        add_clips(total.luminance_clips, p.luminance_clips);
        total.pixel_count += p.pixel_count;
    }
    return total;
}

}  // namespace

StageHistogram compute_stage_histogram(const Image& img, int threads) {
    (void)require_consistent(img, "compute_stage_histogram");
    if (img.width == 0 || img.height == 0) {
        return {};
    }

    const int n = threads_for_pixels(static_cast<std::size_t>(img.width) * img.height, threads);
    const int chunks = std::clamp(n, 1, std::max(img.height, 1));
    std::vector<StageHistogram> partial(static_cast<std::size_t>(chunks));
    const std::size_t row_samples = static_cast<std::size_t>(img.width) * Image::channels;

    parallel_for(chunks, [&](int chunk) {
        const RowRange rows = chunk_rows(img.height, chunks, chunk);
        StageHistogram local;
        count_range(img.pixels.data(), rows.begin * row_samples, rows.end * row_samples, local);
        partial[static_cast<std::size_t>(chunk)] = local;
    });

    return merge_partials(partial);
}

void encode_srgb8_with_histogram(const Image& img, int threads, std::vector<std::uint8_t>& rgb8,
                                 StageHistogram& histogram) {
    const std::size_t total_samples = require_consistent(img, "encode_srgb8_with_histogram");
    rgb8.resize(total_samples);
    if (img.width == 0 || img.height == 0) {
        histogram = {};
        return;
    }

    const int n = threads_for_pixels(static_cast<std::size_t>(img.width) * img.height, threads);
    const int chunks = std::clamp(n, 1, std::max(img.height, 1));
    std::vector<StageHistogram> partial(static_cast<std::size_t>(chunks));
    const std::size_t row_samples = static_cast<std::size_t>(img.width) * Image::channels;

    parallel_for(chunks, [&](int chunk) {
        const RowRange rows = chunk_rows(img.height, chunks, chunk);
        const std::size_t first = rows.begin * row_samples;
        const std::size_t last = rows.end * row_samples;
        StageHistogram local;
        // Work in L1-sized slices (1024 pixels = 12 KB of float) so counting reads cache lines
        // that linear_to_srgb8 just loaded.
        constexpr std::size_t block = 3 * 1024;
        for (std::size_t i = first; i < last; i += block) {
            const std::size_t count = std::min(block, last - i);
            linear_to_srgb8(img.pixels.data() + i, rgb8.data() + i, count);
            count_range(img.pixels.data(), i, i + count, local);
        }
        partial[static_cast<std::size_t>(chunk)] = local;
    });

    histogram = merge_partials(partial);
}

double pixel_percent(std::uint64_t count, std::uint64_t pixel_count) noexcept {
    if (pixel_count == 0) {
        return 0.0;
    }
    return 100.0 * static_cast<double>(count) / static_cast<double>(pixel_count);
}

bool has_shadow_clipping(const StageHistogram& h, double threshold_percent) noexcept {
    if (h.pixel_count == 0) {
        return false;
    }
    const std::uint64_t worst =
        std::max({h.red_clips.under_black, h.green_clips.under_black, h.blue_clips.under_black});
    return worst > 0 && pixel_percent(worst, h.pixel_count) >= threshold_percent;
}

bool has_highlight_clipping(const StageHistogram& h, double threshold_percent) noexcept {
    if (h.pixel_count == 0) {
        return false;
    }
    const std::uint64_t worst =
        std::max({h.red_clips.over_white, h.green_clips.over_white, h.blue_clips.over_white});
    return worst > 0 && pixel_percent(worst, h.pixel_count) >= threshold_percent;
}

}  // namespace prism::gui

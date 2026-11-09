#include "prism/gui/histogram.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "prism/color.hpp"
#include "prism/gui/clipping.hpp"
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

// Counts one sample: below 0 and above 1 are clips, measured on the float. Anything else (NaN
// included, which is in neither range) is binned by `code`, its 8-bit sRGB value.
void record_sample(float v, std::uint8_t code, StageHistogram::Counts& bins,
                   ChannelClips& clips, bool file_ceiling = false) noexcept {
    if (file_ceiling && v >= 1.0f - file_ceiling_epsilon && v <= 1.0f) {
        ++clips.at_ceiling;  // also binned below, unlike the other two
    }
    if (v < 0.0f) {
        ++clips.under_black;
    } else if (v > 1.0f) {
        ++clips.over_white;
    } else {
        ++bins[code];
    }
}

// Counts pixels in sample range [first, last) of `pixels`. Bins are on the display scale: the bin
// of a value is the sRGB code linear_to_srgb8 gives it (NaN gives 0, black, as on screen). R, G and
// B read that code from `encoded` (the bytes the encode already produced for the same samples)
// when it is given, and look it up otherwise; luminance has no output byte, so it is encoded here.
// The any-channel counts ask pixel_below_black and pixel_above_white, as the clip mask does.
void count_range(const float* pixels, const std::uint8_t* encoded, std::size_t first,
                 std::size_t last, bool file_ceiling, StageHistogram& local) noexcept {
    for (std::size_t i = first; i < last; i += Image::channels) {
        const float r = pixels[i];
        const float g = pixels[i + 1];
        const float b = pixels[i + 2];
        const float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        record_sample(r, encoded ? encoded[i] : linear_to_srgb8(r), local.red, local.red_clips,
                      file_ceiling);
        record_sample(g, encoded ? encoded[i + 1] : linear_to_srgb8(g), local.green,
                      local.green_clips, file_ceiling);
        record_sample(b, encoded ? encoded[i + 2] : linear_to_srgb8(b), local.blue,
                      local.blue_clips, file_ceiling);
        record_sample(y, linear_to_srgb8(y), local.luminance, local.luminance_clips);
        const PixelRange range = pixel_range(pixels + i);
        local.any_under_black += pixel_below_black(range);
        local.any_over_white += pixel_above_white(range, file_ceiling);
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
    into.at_ceiling += from.at_ceiling;
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
        total.any_under_black += p.any_under_black;
        total.any_over_white += p.any_over_white;
        total.pixel_count += p.pixel_count;
    }
    return total;
}

}  // namespace

StageHistogram compute_stage_histogram(const Image& img, int threads, bool file_ceiling) {
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
        count_range(img.pixels.data(), nullptr, rows.begin * row_samples,
                    rows.end * row_samples, file_ceiling, local);
        partial[static_cast<std::size_t>(chunk)] = local;
    });

    return merge_partials(partial);
}

void encode_srgb8_with_histogram(const Image& img, int threads, std::vector<std::uint8_t>& rgb8,
                                 StageHistogram& histogram, bool file_ceiling) {
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
        // that linear_to_srgb8 just loaded, and bins R, G and B from the bytes it just wrote.
        constexpr std::size_t block = 3 * 1024;
        for (std::size_t i = first; i < last; i += block) {
            const std::size_t count = std::min(block, last - i);
            linear_to_srgb8(img.pixels.data() + i, rgb8.data() + i, count);
            count_range(img.pixels.data(), rgb8.data(), i, i + count, file_ceiling, local);
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

namespace {

std::string readout_percent(std::uint64_t count, std::uint64_t total) {
    if (count == 0 || total == 0) {
        return "0.0%";
    }
    if (count >= total) {
        return "100.0%";
    }
    const std::uint64_t tenths = (count * 2000 + total) / (2 * total);  // rounded to the nearest
    if (tenths == 0) {
        return "<0.1%";
    }
    if (tenths >= 1000) {
        return ">99.9%";
    }
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "%";
}

}  // namespace

ScopeReadout scope_readout(const StageHistogram& h) {
    return {readout_percent(h.any_under_black, h.pixel_count) + " below black",
            readout_percent(h.any_over_white, h.pixel_count) + " above white"};
}

}  // namespace prism::gui

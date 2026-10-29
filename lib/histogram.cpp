#include "prism/histogram.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include "prism/color.hpp"

namespace prism {

namespace {

// Rec. 709 luma in integer arithmetic: weights scaled by 10000 (they sum to exactly 10000),
// and +5000 rounds to nearest. Integers make white land on exactly 255, with no float error.
int luma709(int r, int g, int b) {
    return (2126 * r + 7152 * g + 722 * b + 5000) / 10000;
}

// Bar height in pixels for `count`, scaled so that `max_count` fills the full height.
int bar_height(std::uint64_t count, std::uint64_t max_count, int height) {
    if (max_count == 0) {
        return 0;
    }
    const double scaled = static_cast<double>(count) / static_cast<double>(max_count) * height;
    return static_cast<int>(std::min(scaled + 0.5, static_cast<double>(height)));
}

}  // namespace

Histogram compute_histogram(const Image& img) {
    Histogram h;
    for (std::size_t i = 0; i + 2 < img.pixels.size(); i += Image::channels) {
        // The same conversion save_image uses, so we count exactly the bytes in the output file.
        const int r = linear_to_srgb8(img.pixels[i]);
        const int g = linear_to_srgb8(img.pixels[i + 1]);
        const int b = linear_to_srgb8(img.pixels[i + 2]);
        ++h.red[r];
        ++h.green[g];
        ++h.blue[b];
        ++h.luma[luma709(r, g, b)];
        ++h.pixel_count;
    }
    return h;
}

double bin_percent(const Histogram& h, const Histogram::Counts& counts, int bin) {
    if (h.pixel_count == 0 || bin < 0 || bin >= Histogram::bins) {
        return 0.0;
    }
    return 100.0 * static_cast<double>(counts[bin]) / static_cast<double>(h.pixel_count);
}

Image render_histogram(const Histogram& h, int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("histogram size must be positive");
    }

    // Colours are picked as sRGB values (how they look) and converted to linear, because
    // Image stores linear light and adding light is only physically right in linear.
    const float background = srgb_to_linear(0.10f);
    const float luma_grey = srgb_to_linear(0.35f);
    const float channel_add = 0.6f;  // linear amount each R/G/B curve adds to its own channel

    // Scale to the tallest bin, ignoring 0 and 255: heavy clipping makes those spike and would
    // flatten everything else. Clipped bars just run off the top. If only 0 and 255 have any
    // counts (e.g. a pure black-and-white image), fall back to including them.
    std::uint64_t max_count = 0;
    std::uint64_t max_with_ends = 0;
    for (const Histogram::Counts* counts : {&h.red, &h.green, &h.blue, &h.luma}) {
        max_count = std::max(max_count, *std::max_element(counts->begin() + 1, counts->end() - 1));
        max_with_ends = std::max(max_with_ends, *std::max_element(counts->begin(), counts->end()));
    }
    if (max_count == 0) {
        max_count = max_with_ends;
    }

    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<std::size_t>(width) * height * Image::channels);

    for (int x = 0; x < width; ++x) {
        // Spread the 256 bins evenly across the width (2 pixels per bin at width 512).
        const int bin = x * Histogram::bins / width;
        const int luma_h = bar_height(h.luma[bin], max_count, height);
        const int rgb_h[3] = {bar_height(h.red[bin], max_count, height),
                              bar_height(h.green[bin], max_count, height),
                              bar_height(h.blue[bin], max_count, height)};

        for (int y = 0; y < height; ++y) {
            // Image rows run top to bottom, but bars grow up from the bottom edge.
            const int from_bottom = height - 1 - y;
            float* p = &img.pixels[(static_cast<std::size_t>(y) * width + x) * Image::channels];
            const float base = from_bottom < luma_h ? luma_grey : background;
            for (int c = 0; c < Image::channels; ++c) {
                // Values above 1 are fine here: save_image clamps them to 255.
                p[c] = base + (from_bottom < rgb_h[c] ? channel_add : 0.0f);
            }
        }
    }
    return img;
}

}  // namespace prism

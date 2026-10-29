#pragma once

// Internal helpers shared by the per-pixel stages; not part of the public API.

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>

#include "prism/image.hpp"
#include "prism/parallel.hpp"

namespace prism::detail {

inline std::size_t sample_count(int width, int height) {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * Image::channels;
}

// Threads index the pixel buffer by row, so a buffer that disagrees with width/height would mean
// reading or writing out of bounds. Check once up front instead.
inline void require_consistent(const Image& img, const char* what) {
    if (img.width < 0 || img.height < 0 ||
        img.pixels.size() != sample_count(img.width, img.height)) {
        throw std::invalid_argument(std::string(what) +
                                    ": pixel buffer size does not match width * height * 3");
    }
}

using SampleRangeFn = std::function<void(std::size_t first, std::size_t last)>;

// Splits a width x height image into row chunks (see parallel_for_rows) and calls
// fn(first, last) with the range of sample (float) indices [first, last) that each chunk covers.
// Rows are contiguous in memory, so each chunk is one unbroken slice of the buffer.
inline void parallel_for_samples(int width, int height, int threads, const SampleRangeFn& fn) {
    const std::size_t row_samples = sample_count(width, 1);
    const int n = threads_for_pixels(static_cast<std::size_t>(width) * height, threads);
    parallel_for_rows(height, n, [&](int begin, int end) {
        fn(static_cast<std::size_t>(begin) * row_samples,
           static_cast<std::size_t>(end) * row_samples);
    });
}

}  // namespace prism::detail

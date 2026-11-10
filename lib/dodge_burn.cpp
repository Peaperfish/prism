#include "prism/dodge_burn.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "prism/parallel.hpp"
#include "sample_ranges.hpp"

// Compiled without floating-point contraction, like stroke_field.cpp (lib/CMakeLists.txt): the
// gain is fast_exp2, a polynomial, and a fused multiply-add in it could round differently in the
// vectorised body of a row's loop than in its scalar tail.

namespace prism {

void dodge_burn(Image& img, const LocalExposure& local, int threads) {
    detail::require_consistent(img, "dodge_burn");
    if (local.field.strokes.empty()) {
        return;
    }
    if (local.grid.full_w <= 0 || local.grid.full_h <= 0 || local.grid.stride <= 0 ||
        local.grid.width() != img.width || local.grid.height() != img.height) {
        throw std::invalid_argument(
            "dodge_burn: the stroke grid is " + std::to_string(local.grid.width()) + " x " +
            std::to_string(local.grid.height()) + " pixels but the image is " +
            std::to_string(img.width) + " x " + std::to_string(img.height));
    }
    const std::size_t width = static_cast<std::size_t>(img.width);
    const std::size_t pixels = width * static_cast<std::size_t>(img.height);
    if (!local.precomputed.empty() && local.precomputed.size() != pixels) {
        throw std::invalid_argument("dodge_burn: the precomputed field has " +
                                    std::to_string(local.precomputed.size()) +
                                    " values but the image has " + std::to_string(pixels) +
                                    " pixels");
    }
    if (pixels == 0) {
        return;
    }
    const int n = threads_for_pixels(pixels, threads);

    // The gain of one pixel, where its field is not exactly 0: the same few operations whether the
    // field was rasterised here or handed in.
    const auto apply = [](const float* field, std::size_t count, float* px) {
        for (std::size_t p = 0; p < count; ++p, px += 3) {
            const float f = field[p];
            if (f != 0.0f) {
                const float gain = stroke_gain(f);
                px[0] *= gain;
                px[1] *= gain;
                px[2] *= gain;
            }
        }
    };

    if (!local.precomputed.empty()) {
        parallel_for_rows(img.height, n, [&](int begin, int end) {
            const std::size_t first = static_cast<std::size_t>(begin) * width;
            apply(local.precomputed.data() + first,
                  static_cast<std::size_t>(end - begin) * width, img.pixels.data() + first * 3);
        });
        return;
    }

    // The segments and their boxes, once for the whole call; each tile only picks out the ones
    // that reach its rows.
    const PreparedField prepared(local.field, local.grid);

    // A worker must not throw (parallel_for); the first error is kept and rethrown here.
    std::exception_ptr error;
    std::mutex error_mutex;
    parallel_for_rows(img.height, n, [&](int begin, int end) {
        try {
            std::vector<float> tile;  // this thread's own; allocated on the first tile a stroke reaches
            for (int t = begin; t < end; t += dodge_burn_tile_rows) {
                const int t_end = std::min(t + dodge_burn_tile_rows, end);
                if (prepared.rows_untouched(t, t_end)) {
                    continue;
                }
                tile.resize(static_cast<std::size_t>(t_end - t) * width);
                prepared.rasterise(t, t_end, tile);
                apply(tile.data(), tile.size(),
                      img.pixels.data() + static_cast<std::size_t>(t) * width * 3);
            }
        } catch (...) {
            const std::lock_guard lock(error_mutex);
            if (!error) {
                error = std::current_exception();
            }
        }
    });
    if (error) {
        std::rethrow_exception(error);
    }
}

}  // namespace prism

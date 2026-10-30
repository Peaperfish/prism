#include "prism/resize.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "prism/parallel.hpp"
#include "sample_ranges.hpp"

namespace prism {

namespace {

// Which source pixels each output pixel along one axis averages, and with what weights.
//
// Shrinking `src` pixels to `dst`, measure positions in units of 1 / (src * dst) of the image's
// length: then source pixel i covers [i * dst, (i + 1) * dst) and output pixel o covers
// [o * src, (o + 1) * src), all whole numbers. A source pixel's weight is the length of its
// overlap with the output pixel divided by the output pixel's length, src. Working in whole
// numbers makes every weight exact up to that one division, and the weights of an output pixel
// add up to 1 (to float rounding).
class Taps {
public:
    Taps(int src, int dst) : first_(dst), count_(dst) {
        // An output pixel spans src / dst source pixels, which can touch one more because it need
        // not start on a source pixel's edge.
        stride_ = (src + dst - 1) / dst + 1;
        weights_.assign(static_cast<std::size_t>(dst) * stride_, 0.0f);
        for (int o = 0; o < dst; ++o) {
            const long long begin = static_cast<long long>(o) * src;
            const long long end = begin + src;
            const int first = static_cast<int>(begin / dst);
            const int last = static_cast<int>((end - 1) / dst);  // inclusive
            first_[o] = first;
            count_[o] = last - first + 1;
            for (int i = first; i <= last; ++i) {
                const long long overlap = std::min(end, static_cast<long long>(i + 1) * dst) -
                                          std::max(begin, static_cast<long long>(i) * dst);
                weights_[static_cast<std::size_t>(o) * stride_ + (i - first)] =
                    static_cast<float>(static_cast<double>(overlap) / static_cast<double>(src));
            }
        }
    }

    [[nodiscard]] int first(int o) const { return first_[o]; }
    [[nodiscard]] int count(int o) const { return count_[o]; }
    [[nodiscard]] const float* weights(int o) const {
        return &weights_[static_cast<std::size_t>(o) * stride_];
    }

private:
    std::vector<int> first_;
    std::vector<int> count_;
    std::vector<float> weights_;  // `stride_` per output pixel; unused ones stay 0
    int stride_ = 0;
};

// Rounds a * b / c to the nearest whole number (all positive), without overflowing int.
int scale_rounded(int a, int b, int c) {
    return static_cast<int>((2LL * a * b + c) / (2LL * c));
}

}  // namespace

Image downscale(const Image& img, int max_long_side, int threads) {
    detail::require_consistent(img, "downscale");
    if (max_long_side < 1) {
        throw std::invalid_argument("downscale: max_long_side must be at least 1");
    }
    if (std::max(img.width, img.height) <= max_long_side) {
        return img;
    }

    Image out;
    if (img.width >= img.height) {
        out.width = max_long_side;
        out.height = std::max(1, scale_rounded(img.height, max_long_side, img.width));
    } else {
        out.height = max_long_side;
        out.width = std::max(1, scale_rounded(img.width, max_long_side, img.height));
    }
    out.pixels.resize(detail::sample_count(out.width, out.height));

    const Taps across(img.width, out.width);
    const Taps down(img.height, out.height);
    const std::size_t src_row = detail::sample_count(img.width, 1);
    const std::size_t dst_row = detail::sample_count(out.width, 1);

    // The work grows with the source size, so that decides whether to use several threads. Each
    // thread writes only its own output rows (and only reads the source), so no locks are needed,
    // and each output row is always computed in the same order, whatever the thread count.
    const int n = threads_for_pixels(static_cast<std::size_t>(img.width) * img.height, threads);
    parallel_for_rows(out.height, n, [&](int begin, int end) {
        for (int oy = begin; oy < end; ++oy) {
            float* dst = &out.pixels[static_cast<std::size_t>(oy) * dst_row];
            std::fill(dst, dst + dst_row, 0.0f);
            const float* wy = down.weights(oy);
            // Average across each source row this output row covers, and add that row's share.
            for (int ky = 0; ky < down.count(oy); ++ky) {
                const float* src =
                    &img.pixels[static_cast<std::size_t>(down.first(oy) + ky) * src_row];
                for (int ox = 0; ox < out.width; ++ox) {
                    const float* wx = across.weights(ox);
                    const float* p = src + static_cast<std::size_t>(across.first(ox)) * 3;
                    float r = 0.0f;
                    float g = 0.0f;
                    float b = 0.0f;
                    for (int kx = 0; kx < across.count(ox); ++kx, p += 3) {
                        r += wx[kx] * p[0];
                        g += wx[kx] * p[1];
                        b += wx[kx] * p[2];
                    }
                    float* q = dst + static_cast<std::size_t>(ox) * 3;
                    q[0] += wy[ky] * r;
                    q[1] += wy[ky] * g;
                    q[2] += wy[ky] * b;
                }
            }
        }
    });
    return out;
}

}  // namespace prism

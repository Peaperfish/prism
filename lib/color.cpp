#include "prism/color.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>

namespace prism {

float srgb_to_linear(float srgb) noexcept {
    if (srgb <= 0.04045f) {
        return srgb / 12.92f;
    }
    return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float linear) noexcept {
    if (linear <= 0.0031308f) {
        return linear * 12.92f;
    }
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

float srgb8_to_linear(std::uint8_t srgb) noexcept {
    // Only 256 possible inputs, so compute them all once instead of calling pow per pixel.
    // A function-local static is initialised on first use, and C++ guarantees that is thread-safe.
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            t[i] = srgb_to_linear(static_cast<float>(i) / 255.0f);
        }
        return t;
    }();
    return table[srgb];
}

std::uint8_t linear_to_srgb8_reference(float linear) noexcept {
    // Written as !(x > 0) rather than x <= 0 so NaN also takes this branch.
    if (!(linear > 0.0f)) {
        return 0;
    }
    if (linear >= 1.0f) {
        return 255;
    }
    return static_cast<std::uint8_t>(std::lround(linear_to_srgb(linear) * 255.0f));
}

namespace {

// Fast, exact linear -> 8-bit sRGB without calling pow per value.
//
// The output only changes at 255 points: threshold[k] is the smallest float whose reference
// result is >= k + 1. So the answer for x is "how many thresholds are <= x". We find the thresholds
// by binary-searching the reference function itself (not by inverting the formula), which makes
// the table exact by construction: float rounding in pow or in the *255 cannot put a boundary
// one ULP off. This relies on the reference being non-decreasing, which the prism_bench
// exhaustive check proves over every float.
//
// Counting thresholds by binary search takes 8 dependent loads per value. Instead we jump close
// with a second table indexed by the top bits of the float (its exponent and first 7 mantissa
// bits). Each such bucket spans a float range containing at most one threshold, so one
// comparison finishes the job and there are no branches in the hot loop. A plain binary search
// is also exact, but measured ~4x slower than this on an M2.
class SrgbEncoder {
public:
    SrgbEncoder() {
        for (int k = 1; k <= 255; ++k) {
            // Invariant: reference(lo) < k <= reference(hi). Positive floats order the same as
            // their bit patterns viewed as integers, so we can bisect on those.
            std::uint32_t lo = 0;
            std::uint32_t hi = std::bit_cast<std::uint32_t>(1.0f);
            while (hi - lo > 1) {
                const std::uint32_t mid = lo + (hi - lo) / 2;
                if (linear_to_srgb8_reference(std::bit_cast<float>(mid)) >= k) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            threshold_[k - 1] = hi;
        }
        threshold_[255] = 0xFFFFFFFFu;  // sentinel: larger than any input lookup() compares

        // bucket_[b] = number of thresholds <= the smallest float in bucket b.
        for (std::uint32_t b = 0; b < buckets; ++b) {
            const std::uint32_t start = first_bits + (b << shift);
            int n = 0;
            while (threshold_[n] <= start) {
                ++n;
            }
            bucket_[b] = static_cast<std::uint8_t>(n);
        }
        // Check "at most one threshold per bucket", which lookup() relies on. It is a fixed
        // property of the constants above, so this fails on every run if ever broken.
        for (std::uint32_t b = 0; b + 1 < buckets; ++b) {
            if (bucket_[b + 1] - bucket_[b] > 1) {
                std::abort();
            }
        }
        if (255 - bucket_[buckets - 1] > 1) {
            std::abort();
        }
    }

    // Handles any input: negatives, NaN, 0, 1 and above.
    //
    // Works on the float's bits as an integer rather than comparing floats: for non-negative
    // floats the two orders agree, and integer compares have no NaN special cases. (Float
    // clamps like `x > 0 ? x : 0` get compiled to min/max instructions that treat signalling
    // NaNs differently from quiet ones, which made some NaN bit patterns encode as 255.)
    std::uint8_t lookup(float x) const noexcept {
        std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
        // Negative numbers (sign bit set) and NaN (above the bits of +infinity) become 0.0f.
        bits = bits <= infinity_bits ? bits : 0;
        bits = bits < below_one_bits ? bits : below_one_bits;  // 1 and above: top bin, result 255
        // Below the first bucket means below every threshold: use bucket 0, whose answer is 0.
        const std::uint32_t offset = bits > first_bits ? bits - first_bits : 0;
        unsigned n = bucket_[offset >> shift];
        n += threshold_[n] <= bits ? 1u : 0u;
        return static_cast<std::uint8_t>(n);
    }

private:
    // Buckets cover [2^-16, 1): 16 exponents x 2^7 mantissa steps. The first threshold is
    // ~1.5e-4, comfortably above 2^-16 (1.5e-5), so everything below the table is 0.
    static constexpr std::uint32_t shift = 16;
    static constexpr std::uint32_t buckets = 16u << (23 - shift);
    static constexpr std::uint32_t first_bits = 0x3F800000u - (16u << 23);  // bits of 2^-16
    static constexpr std::uint32_t below_one_bits = 0x3F800000u - 1;        // largest float < 1
    static constexpr std::uint32_t infinity_bits = 0x7F800000u;

    std::array<std::uint32_t, 256> threshold_{};  // as float bit patterns; see lookup()
    std::array<std::uint8_t, buckets> bucket_{};
};

// Built on first use; C++ guarantees that initialisation of a function-local static is
// thread-safe. The batch function fetches it once, outside its loop.
const SrgbEncoder& encoder() {
    static const SrgbEncoder e;
    return e;
}

}  // namespace

std::uint8_t linear_to_srgb8(float linear) noexcept {
    return encoder().lookup(linear);
}

void linear_to_srgb8(const float* in, std::uint8_t* out, std::size_t count) noexcept {
    const SrgbEncoder& e = encoder();
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = e.lookup(in[i]);
    }
}

}  // namespace prism

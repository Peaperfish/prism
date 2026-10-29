#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

namespace prism {

// Fast log2 and exp2, used by the gamma tone curve.
//
// std::log2, std::exp2 and std::pow are calls into the system maths library: the compiler can't
// see inside them, so a loop calling one per value runs one value at a time. These are written
// with only float arithmetic, integer operations on the float's bits and selects (no branches,
// no calls), so the compiler can inline them and vectorise the loop (4 floats per instruction
// on NEON or SSE).
//
// They are accurate to about 2e-7 (relative, for exp2; absolute in stops, for log2), close to
// float's own precision, but not correctly rounded like the library versions, so results can
// differ from std::log2 / std::exp2 in the last bit or two. The tests check what matters: gamma's
// output, through the sRGB encode (and the filmic curve), stays within 1/255 of std::pow's
// (pipeline_test.cpp, and prism_bench --exhaustive over every float from 0 to 256).

// log2(x) for x > 0, including denormals and +infinity. Any other input (0, negative, NaN) gives a
// meaningless but harmless value: callers handle those themselves.
[[nodiscard]] inline float fast_log2(float x) noexcept {
    const float input = x;
    // Denormals have no implicit leading 1 bit, so the exponent trick below would misread them.
    // Scaling by 2^23 makes them normal; the 23 is subtracted again at the end.
    const bool denormal = x < std::numeric_limits<float>::min();
    x = denormal ? x * 8388608.0f : x;
    const float exponent_bias = denormal ? -23.0f : 0.0f;

    // A positive float is 2^e * m: its bits hold e and m directly, so log2(x) = e + log2(m)
    // with no maths. Splitting at sqrt(1/2) instead of 1 puts m in [sqrt(1/2), sqrt(2)), where
    // log2(m) is small (|log2 m| <= 0.5) and the series below converges fastest.
    // (Unsigned arithmetic: wrapping is defined there; converting back to signed is too, in C++20.)
    constexpr std::uint32_t sqrt_half_bits = 0x3F3504F3u;  // bits of sqrt(1/2)
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
    const std::int32_t e = static_cast<std::int32_t>(bits - sqrt_half_bits) >> 23;
    const float m = std::bit_cast<float>(bits - (static_cast<std::uint32_t>(e) << 23));

    // log2(m) = 2/ln(2) * atanh(t) with t = (m - 1) / (m + 1)
    //         = 2/ln(2) * (t + t^3/3 + t^5/5 + t^7/7 + ...)
    // |t| <= 0.1716 here, so the first left-out term (t^9/9) is below 5e-8.
    constexpr float k = 2.8853900817779268f;  // 2 / ln(2)
    const float t = (m - 1.0f) / (m + 1.0f);
    const float t2 = t * t;
    const float log2_m = t * (k + t2 * (k / 3.0f + t2 * (k / 5.0f + t2 * (k / 7.0f))));
    const float result = static_cast<float>(e) + exponent_bias + log2_m;
    // The bits of +infinity read as 2^128 * 1, i.e. 128: correct that.
    return input == std::numeric_limits<float>::infinity() ? input : result;
}

// 2^y for any y except NaN. Results below 2^-125 (~2e-38) become 0; y >= 128 gives +infinity.
[[nodiscard]] inline float fast_exp2(float y) noexcept {
    const float yc = std::min(std::max(y, -125.0f), 128.0f);
    // Split y = n + f with n a whole number and f in [-0.5, 0.5]. Adding and subtracting
    // 1.5 * 2^23 rounds to the nearest integer: at that size a float has no bits left for a
    // fraction. (std::nearbyint does the same but doesn't vectorise on every target.)
    constexpr float round_magic = 12582912.0f;  // 1.5 * 2^23
    const float n = (yc + round_magic) - round_magic;
    const float f = yc - n;

    // 2^f on [-0.5, 0.5]: minimax polynomial from Cephes' exp2f, relative error ~2e-7.
    float p = 1.535336188319500e-4f;
    p = p * f + 1.339887440266574e-3f;
    p = p * f + 9.618437357674640e-3f;
    p = p * f + 5.550332471162809e-2f;
    p = p * f + 2.402264791363012e-1f;
    p = p * f + 6.931472028550421e-1f;
    p = p * f + 1.0f;

    // Multiply by 2^n by adding n to the float's exponent field. p is in [0.70, 1.42], so the
    // new exponent stays valid for n in [-125, 128]; n = 128 with f = 0 (p = 1 exactly) gives
    // the bits of +infinity.
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(p) +
                               (static_cast<std::uint32_t>(static_cast<std::int32_t>(n)) << 23);
    return y < -125.0f ? 0.0f : std::bit_cast<float>(bits);
}

}  // namespace prism

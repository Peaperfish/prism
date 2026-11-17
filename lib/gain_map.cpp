#include "prism/gain_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

#include "prism/hdr.hpp"
#include "prism/parallel.hpp"
#include "sample_ranges.hpp"

namespace prism {

namespace {

void require_base(std::span<const std::uint8_t> base, int width, int height, const char* what) {
    if (width < 0 || height < 0 || base.size() != detail::sample_count(width, height)) {
        throw std::invalid_argument(std::string(what) +
                                    ": base size does not match width * height * 3");
    }
}

// Runs fn(begin_row, end_row) over the rows of a width x height image, split as every stage is.
template <typename Fn>
void for_rows(int width, int height, int threads, const Fn& fn) {
    const int n = threads_for_pixels(static_cast<std::size_t>(width) * height, threads);
    parallel_for_rows(height, n, [&](int begin, int end) { fn(begin, end); });
}

}  // namespace

GainMap make_gain_map(std::span<const std::uint8_t> base_srgb8, const Image& hdr,
                      float headroom_stops, const GainMapOptions& options, int threads) {
    detail::require_consistent(hdr, "make_gain_map");
    require_base(base_srgb8, hdr.width, hdr.height, "make_gain_map");
    if (!(headroom_stops > headroom_min_stops && headroom_stops <= headroom_max_stops)) {
        throw std::invalid_argument("make_gain_map: headroom must be above 0 and at most 4 stops");
    }
    const int width = hdr.width;
    const int height = hdr.height;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    // The base's 256 levels as linear light, exactly as decode_srgb8 gives them, so a lookup
    // replaces a whole decoded copy of the picture (12 bytes a pixel).
    std::array<float, 256> linear{};
    {
        std::vector<std::uint8_t> levels(256 * 3);
        for (std::size_t i = 0; i < levels.size(); ++i) {
            levels[i] = static_cast<std::uint8_t>(i / 3);
        }
        const Image decoded = decode_srgb8(levels, 256, 1, 1);
        for (std::size_t c = 0; c < 256; ++c) {
            linear[c] = decoded.pixels[c * 3];
        }
    }
    const float o = gain_map_offset;
    const float peak = std::exp2(headroom_stops);
    const bool limited = options.keep_channels_within_headroom;

    // A pixel's log2 gain before quantising, and the most its strongest channel allows.
    const auto base_at = [&](std::size_t i, int c) { return linear[base_srgb8[i * 3 + c]]; };
    const auto limit_at = [&](std::size_t i) {
        // At full weight the strongest channel becomes (c + o) * 2^g - o: keep it <= H.
        const float strongest = std::max({base_at(i, 0), base_at(i, 1), base_at(i, 2)});
        return std::log2((peak + o) / (strongest + o));
    };
    const auto gain_at = [&](std::size_t i) {
        const float* h = &hdr.pixels[i * 3];
        const float y_sdr = luminance(base_at(i, 0), base_at(i, 1), base_at(i, 2));
        // No light (negative) and NaN count as 0; +infinity as the largest float, so every gain
        // is finite.
        const float y = luminance(h[0], h[1], h[2]);
        const float y_hdr = y > 0.0f ? std::min(y, std::numeric_limits<float>::max()) : 0.0f;
        // In double: the largest float over a dark base would overflow a float ratio.
        const auto g = static_cast<float>(
            std::log2((static_cast<double>(y_hdr) + o) / (static_cast<double>(y_sdr) + o)));
        return limited ? std::min(g, limit_at(i)) : g;
    };

    // Pass 1: each pixel's log2 gain, and each row's smallest and largest. (Min and max are exact
    // in any order, so splitting the rows another way cannot change them.)
    std::vector<float> log_gain(pixels);
    std::vector<float> row_min(static_cast<std::size_t>(height));
    std::vector<float> row_max(static_cast<std::size_t>(height));
    for_rows(width, height, threads, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            float lo = std::numeric_limits<float>::infinity();
            float hi = -std::numeric_limits<float>::infinity();
            for (int x = 0; x < width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * width + x;
                const float g = gain_at(i);
                log_gain[i] = g;
                lo = std::min(lo, g);
                hi = std::max(hi, g);
            }
            row_min[static_cast<std::size_t>(y)] = lo;
            row_max[static_cast<std::size_t>(y)] = hi;
        }
    });

    GainMap map;
    map.width = width;
    map.height = height;
    map.codes.assign(pixels, 0);
    map.metadata.hdr_capacity_max = headroom_stops;
    if (pixels == 0) {
        return map;
    }
    map.metadata.min_log2 = *std::min_element(row_min.begin(), row_min.end());
    map.metadata.max_log2 = *std::max_element(row_max.begin(), row_max.end());
    const GainMapMetadata& m = map.metadata;
    const float range = m.max_log2 - m.min_log2;
    if (!(range > 0.0f)) {
        return map;  // one gain everywhere: code 0 stands for it
    }

    // Pass 2: the codes.
    for_rows(width, height, threads, [&](int begin, int end) {
        for (std::size_t i = static_cast<std::size_t>(begin) * width;
             i < static_cast<std::size_t>(end) * width; ++i) {
            const float t = (log_gain[i] - m.min_log2) / range;
            int code = static_cast<int>(std::floor(t * 255.0f + 0.5f));
            code = std::clamp(code, 0, 255);
            if (limited && code > 0 &&
                gain_map_log2(static_cast<std::uint8_t>(code), m) > limit_at(i)) {
                --code;  // rounding went up past the limit: half a step down is within it
            }
            map.codes[i] = static_cast<std::uint8_t>(code);
        }
    });
    return map;
}

float gain_map_log2(std::uint8_t code, const GainMapMetadata& m) noexcept {
    const float t = static_cast<float>(code) / 255.0f;
    const float recovery = m.gamma == 1.0f ? t : std::pow(t, 1.0f / m.gamma);
    // As the spec and libultrahdr write it: exactly min at code 0 and exactly max at 255.
    return m.min_log2 * (1.0f - recovery) + m.max_log2 * recovery;
}

float gain_map_weight(float display_headroom, const GainMapMetadata& m) noexcept {
    if (!(display_headroom > 0.0f)) {
        return 0.0f;
    }
    const float stops = std::log2(display_headroom);
    const float span = m.hdr_capacity_max - m.hdr_capacity_min;
    if (!(span > 0.0f)) {
        return stops >= m.hdr_capacity_max ? 1.0f : 0.0f;
    }
    return std::clamp((stops - m.hdr_capacity_min) / span, 0.0f, 1.0f);
}

Image apply_gain_map(std::span<const std::uint8_t> base_srgb8, const GainMap& map, float weight,
                     int threads) {
    require_base(base_srgb8, map.width, map.height, "apply_gain_map");
    if (map.codes.size() != static_cast<std::size_t>(map.width) * map.height) {
        throw std::invalid_argument("apply_gain_map: gain map size does not match its dimensions");
    }
    Image out = decode_srgb8(base_srgb8, map.width, map.height, threads);
    weight = weight > 0.0f ? std::min(weight, 1.0f) : 0.0f;  // NaN too
    if (weight == 0.0f) {
        return out;  // the base exactly ((c + o) - o is not always c in float)
    }
    // One factor per code, so each pixel costs a lookup instead of an exp2.
    float factor[256];
    for (int c = 0; c < 256; ++c) {
        factor[c] = std::exp2(gain_map_log2(static_cast<std::uint8_t>(c), map.metadata) * weight);
    }
    const float o_sdr = map.metadata.offset_sdr;
    const float o_hdr = map.metadata.offset_hdr;
    for_rows(map.width, map.height, threads, [&](int begin, int end) {
        for (std::size_t i = static_cast<std::size_t>(begin) * map.width;
             i < static_cast<std::size_t>(end) * map.width; ++i) {
            const float f = factor[map.codes[i]];
            float* p = &out.pixels[i * 3];
            for (int c = 0; c < 3; ++c) {
                p[c] = (p[c] + o_sdr) * f - o_hdr;
            }
        }
    });
    return out;
}

// --- ISO 21496-1 fractions -----------------------------------------------------------------------
//
// float_to_unsigned_fraction below is a port of floatToUnsignedFractionImpl from libultrahdr
// v2.0.2, lib/src/gainmapmath.cpp (lines 1615-1664), used under the MIT licence (libultrahdr is
// offered under MIT or Apache-2.0, at the user's option):
//
//   Copyright 2022 The Android Open Source Project
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
//   and associated documentation files (the "Software"), to deal in the Software without
//   restriction, including without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
//   Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all copies or
//   substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
//   BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
//   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// The arithmetic is kept step for step (double intermediates, the same rounding and limits), so
// the fractions are the ones libultrahdr writes.

namespace {

bool float_to_unsigned_fraction(float v, std::uint32_t max_numerator, std::uint32_t* numerator,
                                std::uint32_t* denominator) {
    if (std::isnan(v) || v < 0 || v > max_numerator) {
        return false;
    }
    // The largest denominator that keeps the numerator <= max_numerator and the denominator
    // <= UINT32_MAX.
    const std::uint64_t max_d = (v <= 1)
                                    ? std::numeric_limits<std::uint32_t>::max()
                                    : static_cast<std::uint64_t>(std::floor(max_numerator / v));
    // The best approximation of v as a fraction, by continued fractions.
    *denominator = 1;
    std::uint32_t previous_d = 0;
    double current_v = static_cast<double>(v) - std::floor(v);
    // The golden ratio is the worst case, at 39 iterations.
    constexpr int max_iter = 39;
    for (int iter = 0; iter < max_iter; ++iter) {
        const double numerator_double = static_cast<double>(*denominator) * v;
        if (numerator_double > max_numerator) {
            return false;
        }
        *numerator = static_cast<std::uint32_t>(std::round(numerator_double));
        if (std::fabs(numerator_double - *numerator) == 0.0) {
            return true;
        }
        current_v = 1.0 / current_v;
        const double new_d = previous_d + std::floor(current_v) * (*denominator);
        if (new_d > static_cast<double>(max_d)) {
            return true;  // the best there is with a denominator <= max_d
        }
        previous_d = *denominator;
        if (new_d > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        *denominator = static_cast<std::uint32_t>(new_d);
        current_v -= std::floor(current_v);
    }
    *numerator = static_cast<std::uint32_t>(std::round(static_cast<double>(*denominator) * v));
    return true;
}

}  // namespace

std::optional<SignedFraction> signed_fraction(float v) noexcept {
    std::uint32_t n = 0;
    std::uint32_t d = 1;
    if (!float_to_unsigned_fraction(std::fabs(v), std::numeric_limits<std::int32_t>::max(), &n,
                                    &d)) {
        return std::nullopt;
    }
    const auto magnitude = static_cast<std::int32_t>(n);
    return SignedFraction{v < 0 ? -magnitude : magnitude, d};
}

std::optional<UnsignedFraction> unsigned_fraction(float v) noexcept {
    std::uint32_t n = 0;
    std::uint32_t d = 1;
    if (!float_to_unsigned_fraction(v, std::numeric_limits<std::uint32_t>::max(), &n, &d)) {
        return std::nullopt;
    }
    return UnsignedFraction{n, d};
}

std::optional<IsoGainMapFractions> iso_fractions(const GainMapMetadata& m) noexcept {
    const auto min = signed_fraction(m.min_log2);
    const auto max = signed_fraction(m.max_log2);
    const auto gamma = unsigned_fraction(m.gamma);
    const auto base_offset = signed_fraction(m.offset_sdr);
    const auto alternate_offset = signed_fraction(m.offset_hdr);
    const auto base_headroom = unsigned_fraction(m.hdr_capacity_min);
    const auto alternate_headroom = unsigned_fraction(m.hdr_capacity_max);
    if (!min || !max || !gamma || !base_offset || !alternate_offset || !base_headroom ||
        !alternate_headroom) {
        return std::nullopt;
    }
    return IsoGainMapFractions{
        *min, *max, *gamma, *base_offset, *alternate_offset, *base_headroom, *alternate_headroom};
}

}  // namespace prism

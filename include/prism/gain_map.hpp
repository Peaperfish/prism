#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "prism/image.hpp"

namespace prism {

// --- Gain map maths (Ultra HDR / ISO 21496-1) ----------------------------------------------------
//
// An HDR file made of the SDR picture (the "base", what every viewer can show) plus a gain map: one
// 8-bit code per pixel saying how much brighter that pixel may get on an HDR screen. A viewer on a
// screen with headroom D (D x white) shows
//   out = (sdr + offset_sdr) * 2^(log_gain * weight) - offset_hdr,   weight from D (see below)
// per channel, where log_gain comes from the pixel's code and the metadata. Prism's map is single
// channel (one gain for R, G and B) at full resolution. These are pure functions: writing the JPEG
// around them is a separate step.
//
// The base is measured as it is decoded, 8-bit sRGB, because that is what a viewer starts from.

// The offsets added to SDR and HDR light before the ratio, so black and near-black pixels have a
// finite gain: 1/64, as Ultra HDR recommends.
inline constexpr float gain_map_offset = 1.0f / 64.0f;

// Rec. 709 / sRGB luminance of linear light, the same weights saturation() uses.
[[nodiscard]] inline float luminance(float r, float g, float b) noexcept {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// What a viewer needs to read the map, in stops (log2) where the format stores stops.
struct GainMapMetadata {
    float min_log2 = 0.0f;  // GainMapMin: the log2 gain that code 0 stands for
    float max_log2 = 0.0f;  // GainMapMax: the log2 gain that code 255 stands for
    float gamma = 1.0f;     // codes are spread evenly over [min, max] in stops
    float offset_sdr = gain_map_offset;
    float offset_hdr = gain_map_offset;
    float hdr_capacity_min = 0.0f;  // stops: below this display headroom, show the SDR picture
    float hdr_capacity_max = 0.0f;  // stops: from this headroom up, the whole gain (= N)
    bool operator==(const GainMapMetadata&) const = default;
};

struct GainMapOptions {
    // Limits each pixel's gain so that, at full weight, no channel of the result passes the
    // headroom H = 2^N. A single (luminance) gain applied to all three channels otherwise lifts the
    // strongest channel of a saturated colour past H. On by default.
    bool keep_channels_within_headroom = true;
};

struct GainMap {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> codes;  // width * height, row-major
    GainMapMetadata metadata;
};

// The gain map for an SDR base and its HDR picture, both width x height:
// - `base_srgb8`: the base as decoded, interleaved 8-bit sRGB RGB (width * height * 3 bytes);
// - `hdr`: the HDR picture (process_hdr's `hdr`), linear float;
// - `headroom_stops`: N, written as HDRCapacityMax; HDRCapacityMin is 0.
// Each pixel's gain is (Y_hdr + 1/64) / (Y_sdr + 1/64) with Y the luminance (negative light
// counts as 0, and so does NaN; +infinity counts as the largest float), limited per pixel as
// `options` says. GainMapMin and GainMapMax are the smallest and largest log2 gains in this
// picture (so GainMapMin can sit a little below 0 from the base's 8-bit rounding, or above 0 when
// every pixel is lifted), and each code is round(255 * (log2 gain - min) / (max - min)), lowered
// by one where rounding up would let the limit be passed. When every pixel has the same gain,
// min = max and every code is 0. Bit-identical for any thread count. Throws
// std::invalid_argument if the sizes disagree or N is not in (0, 4].
[[nodiscard]] GainMap make_gain_map(std::span<const std::uint8_t> base_srgb8, const Image& hdr,
                                    float headroom_stops, const GainMapOptions& options = {},
                                    int threads = 0);

// The log2 gain a code stands for: min * (1 - r) + max * r with r = (code / 255)^(1 / gamma), as
// the Ultra HDR spec decodes it (exactly min at code 0 and max at 255).
[[nodiscard]] float gain_map_log2(std::uint8_t code, const GainMapMetadata& m) noexcept;

// The weight a viewer gives the map on a screen whose headroom is `display_headroom` times white
// (1 = an SDR screen, 4 = two stops above white): (log2 D - HDRCapacityMin) / (HDRCapacityMax -
// HDRCapacityMin), clamped to 0..1. With N = 2: D = 1, 2, 4, 16 give 0, 0.5, 1, 1. A display
// headroom that is not above 0 (or NaN) gives 0.
//
// Prism writes HDRCapacityMax = N, the headroom chosen (#96), where the Ultra HDR spec's tip is
// GainMapMax: a screen with N stops shows the whole HDR picture, and one with less shows the same
// share of every pixel's gain, whatever the photo's own largest gain is.
[[nodiscard]] float gain_map_weight(float display_headroom, const GainMapMetadata& m) noexcept;

// What a viewer shows: `base_srgb8` decoded to linear, then each channel brightened by its
// pixel's gain at `weight` (gain_map_weight), clamped to 0..1 (NaN counts as 0). Weight 0 gives
// the decoded base exactly. Linear float, width x height, not clamped: as in the spec, a gain
// below 1 takes black slightly below 0, and a gain above 1 lifts a saturated colour's weak
// channels too. Bit-identical for any thread count. Throws std::invalid_argument if the sizes
// disagree.
[[nodiscard]] Image apply_gain_map(std::span<const std::uint8_t> base_srgb8, const GainMap& map,
                                   float weight, int threads = 0);

// --- ISO 21496-1 fractions --------------------------------------------------------------------
//
// ISO 21496-1 stores each value as a fraction. These are libultrahdr's continued-fraction routine
// (floatToSignedFraction / floatToUnsignedFraction), ported so the numbers match its files.

struct SignedFraction {
    std::int32_t numerator = 0;
    std::uint32_t denominator = 1;
    bool operator==(const SignedFraction&) const = default;
};

struct UnsignedFraction {
    std::uint32_t numerator = 0;
    std::uint32_t denominator = 1;
    bool operator==(const UnsignedFraction&) const = default;
};

// The best fraction for `v` (a float, so the search is in double), or nothing for NaN or a value
// too large for the numerator (and, unsigned, for a negative value).
[[nodiscard]] std::optional<SignedFraction> signed_fraction(float v) noexcept;
[[nodiscard]] std::optional<UnsignedFraction> unsigned_fraction(float v) noexcept;

// The metadata as ISO 21496-1 fractions, one channel ("base" is the SDR picture, "alternate" the
// HDR one). Nothing if a value cannot be written as a fraction.
struct IsoGainMapFractions {
    SignedFraction gain_map_min;
    SignedFraction gain_map_max;
    UnsignedFraction gamma;
    SignedFraction base_offset;
    SignedFraction alternate_offset;
    UnsignedFraction base_hdr_headroom;       // HDRCapacityMin
    UnsignedFraction alternate_hdr_headroom;  // HDRCapacityMax
    bool operator==(const IsoGainMapFractions&) const = default;
};
[[nodiscard]] std::optional<IsoGainMapFractions> iso_fractions(const GainMapMetadata& m) noexcept;

}  // namespace prism

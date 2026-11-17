#pragma once

#include <optional>

#include "prism/image.hpp"
#include "prism/pipeline.hpp"

namespace prism {

struct ProcessSettings;

// --- HDR: the second way out after Contrast ------------------------------------------------------
//
// With a headroom of N stops above 0, the light branches after Contrast. The SDR branch is
// unchanged (Tone curve, then Output encode). The HDR branch applies the HDR curve below, built
// from the same tone curve, and is linear float light cut at H = 2^N: white (1.0) is the SDR
// file's white, and H is the brightest light the HDR picture holds.

// Headroom in stops above white: 0 is SDR only, as before 0.8.0. A headroom chosen when HDR is
// turned on starts at the default.
inline constexpr float headroom_min_stops = 0.0f;
inline constexpr float headroom_max_stops = 4.0f;
inline constexpr float headroom_default_stops = 2.0f;

// The HDR curve's knee: middle grey. At and below it the HDR curve is the tone curve itself.
inline constexpr float hdr_knee = 0.18f;

// The HDR curve ("design B"). c is the SDR tone curve (the identity when there is none), x the
// value reaching it, S the file's white at that point and H = 2^N. With k = c(0.18),
// u = log2(x / 0.18), U = log2(S / 0.18), V = log2(H / k) and g0 = c'(0.18) * 0.18 / k (the
// curve's slope in stops per stop at the knee):
//   x <= 0.18:      c(x)
//   0.18 < x < S:   k * 2^(g0 * u + a * u^2),   a = (V - g0 * U) / U^2
//   x >= S:         H
// so the HDR curve leaves middle grey exactly where the SDR curve puts it, with the same slope,
// and the file's white lands exactly on H. Guard: when S <= 0.18 or c(S) <= c(0.18) there is no
// room to lift (U would be 0 or negative), so the curve is c(x). Every result is then cut at H.
// Only the top is cut: with no tone curve, c(x) = x keeps a negative value (possible after a
// matrix) negative, as the SDR branch does until Output encode.
//
// Where the parabola's peak falls before S (only with a small N and a bright S, when a < 0 and
// 2V/U < g0), the curve passes H before S; the cut at H holds it there, so the result still never
// decreases as x grows.
struct HdrCurve {
    std::optional<ToneCurve> sdr;  // empty: no tone curve, c(x) = x
    float white = 1.0f;            // S
    float headroom_stops = 0.0f;   // N
    float peak = 1.0f;             // H = 2^N
    bool lift = false;             // false when the guard applies
    float knee_value = hdr_knee;   // k = c(0.18)
    float log_slope = 1.0f;        // g0
    float bend = 0.0f;             // a
    float log2_knee = 0.0f;        // log2(0.18)

    // One value through the curve. Safe from several threads: it only reads.
    [[nodiscard]] float map(float x) const noexcept;
};

// Builds the HDR curve for SDR curve `sdr` (empty: none), file white `white` (S) and headroom
// `headroom_stops` (N). Throws std::invalid_argument if N is not in (0, headroom_max_stops] or S
// is not finite.
[[nodiscard]] HdrCurve make_hdr_curve(const std::optional<ToneCurve>& sdr, float white,
                                      float headroom_stops);

// The tone curve's slope in stops per stop, c'(x) * x / c(x), from its formula (not by
// differencing). x must be > 0. No curve (empty) gives 1.
[[nodiscard]] double tone_curve_log_slope(const std::optional<ToneCurve>& curve, double x);

// S: the file's white where the light branches. The pixel (1, 1, 1) (white in every file Prism
// reads: a RAW's sensor white, an 8-bit file's 255) is sent through every stage before the branch
// that `s` asks for (exposure, white balance, colour matrix, saturation, contrast; dodge and burn
// is local, so it is left out), with the same functions process() uses, and the brightest
// channel is returned. A plain Natural look (+1 stop) gives 2.0. Light that the stages push
// past S in other pixels (a saturated colour through a matrix with negative weights, a dodge
// stroke) reaches the HDR curve's "x >= S" part and is held at H.
[[nodiscard]] float hdr_file_white(const ProcessSettings& s);

// Applies the HDR curve to every value of `img` in place. `threads` as for every stage; the
// result is bit-identical for any thread count.
void hdr_curve(Image& img, const HdrCurve& curve, int threads = 0);

}  // namespace prism

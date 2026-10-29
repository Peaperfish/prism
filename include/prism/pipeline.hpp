#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "prism/image.hpp"

namespace prism {

// Each step edits the image in place: photos are tens of megabytes as float, so returning
// a modified copy from every step would allocate and copy the whole buffer each time.
//
// `threads`: 0 (the default) = one per hardware thread, 1 = single-threaded, n = at most n.
// The result is bit-identical for any value; see prism/parallel.hpp. Each step throws
// std::invalid_argument if img.pixels.size() does not match width * height * 3.

// Multiplies every channel by 2^stops (+1 stop doubles the light, -1 halves it).
void exposure(Image& img, float stops, int threads = 0);

// A 3x3 colour matrix in row-major order: row i gives output channel i (R, G, B) as a
// weighted sum of the input R, G and B. For example, output R = m[0]*R + m[1]*G + m[2]*B.
using Matrix3 = std::array<float, 9>;

inline constexpr Matrix3 identity_matrix = {1, 0, 0,  //
                                            0, 1, 0,  //
                                            0, 0, 1};

void apply_matrix(Image& img, const Matrix3& m, int threads = 0);

// Reads exactly 9 whitespace-separated numbers (row-major) from a text file.
// Text after '#' on a line is a comment. Throws std::runtime_error on any problem.
[[nodiscard]] Matrix3 load_matrix(const std::filesystem::path& path);

// Scales each pixel's distance from its own luminance: out = Y + s * (in - Y), where
// Y = 0.2126 R + 0.7152 G + 0.0722 B (Rec. 709 / sRGB luminance weights, in linear light).
// s = 1 changes nothing, 0 gives greyscale, > 1 is more colourful. Greys (R = G = B) stay grey.
// Y is kept exactly, so brightness does not change, only colourfulness.
//
// Pushing a colour away from grey can drive its weakest channel below 0, which has no meaning as
// light and would be clipped later, shifting the hue. Instead, where that would happen, the pixel
// gets the largest s (up to the requested one) that brings its weakest channel exactly to 0: the
// most saturated colour of that hue and luminance that sRGB can show. A pixel with Y <= 0 (no
// light) becomes black. With s = 1 the image is left exactly as it is, negative values included.
void saturation(Image& img, float s, int threads = 0);

// Contrast: an S-curve in log2 space (stops) around middle grey, applied to each channel:
//   e = log2(x / 0.18)                         stops from middle grey
//   u = e / w                                  w = contrast_range_stops
//   e' = e + (c - 1) * w * u / sqrt(1 + u^2)
//   x' = 0.18 * 2^e'
// u / sqrt(1 + u^2) is an S-shaped function like tanh (slope 1 at 0, levelling off at -1 and
// +1) but about twice as fast to compute. Near middle grey the slope (in stops per stop) is c, so c = 1.15 makes every stop around 0.18
// 15% wider. Further than about w stops away the slope eases back to 1, so deep shadows and bright
// highlights are only shifted by at most (c - 1) * w stops instead of being stretched without
// limit. Working in stops means it acts on ratios of light, like the eye, rather than on
// absolute values. 0.18 is unchanged for any c, c = 1 changes nothing, and the curve is strictly
// increasing for every c > 0. Values <= 0 are scaled by the curve's limit at 0, 2^(-(c-1)*w),
// which keeps it continuous and increasing there too.
inline constexpr float contrast_pivot = 0.18f;
inline constexpr float contrast_range_stops = 2.0f;
//
// contrast() looks the curve up in a table built once per call (interpolating between 256 points
// per stop) instead of calling log2 and exp2 per value, so its results are very slightly
// different from the formula above: after the filmic curve and sRGB encode, at most ~0.1 of an
// 8-bit level (at c = 10). 0.18 stays exactly 0.18, and +infinity stays +infinity.
void contrast(Image& img, float c, int threads = 0);

// One value through contrast(), computed directly from the formula with std::log2 and std::exp2.
// Kept as the reference that the fast version is tested against.
[[nodiscard]] float contrast_reference(float x, float c) noexcept;

// --- Tone curves ---------------------------------------------------------------------------------
//
// A tone curve maps one linear value to another and is applied to each channel separately. Each
// kind of curve is a small struct holding its parameters, with an inline operator() so the
// compiler can see the maths wherever the struct is used. Negative inputs (possible after a
// matrix) are clamped to 0 first: pow() of a negative number with a fractional exponent is NaN.

// Brightens midtones for gamma > 1 and darkens them for gamma < 1; gamma 1 changes nothing.
// 0 and 1 stay fixed.
struct GammaCurve {
    float inv_gamma = 1.0f;
    [[nodiscard]] float operator()(float x) const noexcept {
        return std::pow(std::max(x, 0.0f), inv_gamma);
    }
};

// Extended Reinhard: x * (1 + x / w^2) / (1 + x). Compresses highlights smoothly so that
// linear value w (the "white point") maps to exactly 1.
struct ReinhardCurve {
    float inv_white_sq = 1.0f / 16.0f;
    [[nodiscard]] float operator()(float x) const noexcept {
        x = std::max(x, 0.0f);
        return x * (1.0f + x * inv_white_sq) / (1.0f + x);
    }
};

// John Hable's "Uncharted 2" filmic curve: a gentle toe in the shadows and a soft shoulder in the
// highlights. Constants and the 2x exposure bias are from his original write-up; `scale` maps
// linear white 11.2 to 1.
struct FilmicCurve {
    float scale = 1.0f;
    [[nodiscard]] static float hable(float x) noexcept {
        constexpr float a = 0.15f;  // shoulder strength
        constexpr float b = 0.50f;  // linear strength
        constexpr float c = 0.10f;  // linear angle
        constexpr float d = 0.20f;  // toe strength
        constexpr float e = 0.02f;  // toe numerator
        constexpr float f = 0.30f;  // toe denominator
        return (x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f) - e / f;
    }
    [[nodiscard]] float operator()(float x) const noexcept {
        constexpr float exposure_bias = 2.0f;
        return hable(exposure_bias * std::max(x, 0.0f)) * scale;
    }
};

// One of the curves above. A std::variant stores whichever one it is inline (no heap allocation)
// together with a tag saying which, so code can switch on the actual type with std::visit.
struct ToneCurve {
    std::string name;
    std::variant<GammaCurve, ReinhardCurve, FilmicCurve> shape;

    // One value through the curve. Safe to call from several threads: it only reads `shape`.
    [[nodiscard]] float map(float x) const noexcept {
        return std::visit([x](const auto& curve) { return curve(x); }, shape);
    }
};

// Parses a curve spec such as "gamma:2.2", "reinhard", "reinhard:4" or "filmic".
// Throws std::runtime_error for unknown names or bad parameters.
[[nodiscard]] ToneCurve parse_curve(std::string_view spec);

// Spec formats for every supported curve (e.g. "gamma:<value>"), for help messages.
[[nodiscard]] std::vector<std::string_view> curve_specs();

void tone_curve(Image& img, const ToneCurve& curve, int threads = 0);

}  // namespace prism

#pragma once

#include <array>

#include "prism/camera_profile.hpp"
#include "prism/pipeline.hpp"

namespace prism {

// White balance as photographers set it: a colour temperature in kelvin and a green-magenta tint.
// Everything here is double-precision colour maths; only white_balance_matrix rounds to float.
//
// (kelvin, tint) names a light: the light the scene was lit by. It follows Lightroom's
// convention, so a photographer is not surprised: a higher kelvin makes the image warmer, and a
// positive tint (a greener light) makes the image more magenta.

struct WhiteBalance {
    double kelvin = 6500;
    double tint = 0;
    bool operator==(const WhiteBalance&) const = default;
};

// Tint is an offset Duv from the Planckian locus in CIE 1960 uv, measured along the locus's
// normal toward larger v (the green side): Duv = tint * tint_duv_per_unit, so the range -100..100
// is Duv -0.02..+0.02.
inline constexpr double tint_duv_per_unit = 0.0002;

// The range the Planckian formula is valid for.
inline constexpr double planckian_min_kelvin = 1000;
inline constexpr double planckian_max_kelvin = 15000;

// A point in the CIE 1960 uv chromaticity plane.
struct Uv {
    double u = 0;
    double v = 0;
};

// The colour of an ideal glowing body at `kelvin` (Krystek's rational approximation, valid from
// 1000 K to 15000 K), and its derivative with respect to kelvin.
[[nodiscard]] Uv planckian_uv(double kelvin) noexcept;
[[nodiscard]] Uv planckian_uv_slope(double kelvin) noexcept;

// The unit normal to the locus at `kelvin`, pointing toward larger v (the green side).
[[nodiscard]] Uv locus_normal(double kelvin) noexcept;

[[nodiscard]] Uv uv_of(Chromaticity xy) noexcept;
[[nodiscard]] Chromaticity xy_of(Uv uv) noexcept;

// The chromaticity of the light named by (kelvin, tint).
[[nodiscard]] Chromaticity white_of(const WhiteBalance& wb) noexcept;

// The inverse: the nearest point of the locus (searched from 1000 K to 15000 K) and the tint that
// reaches `xy` from it. Not clamped to the editing ranges; callers clamp.
[[nodiscard]] WhiteBalance white_balance_of(Chromaticity xy) noexcept;

// The light a photo without a known as-shot white is taken to have been lit by: exactly
// (6500 K, tint 0). For such photos 6500 K / tint 0 is "no change".
inline constexpr WhiteBalance default_as_shot{6500, 0};

// The linear sRGB matrix that re-lights a decoded photo as if it had been lit by `chosen`
// instead of by the light whose chromaticity is `as_shot`. The decoded image was already adapted
// from the as-shot light to D65; undoing that and adapting from the chosen light combines, in
// Bradford's cone space, into one step: xyz_to_srgb * B * srgb_to_xyz, where B =
// bradford(white of chosen, as_shot) maps the chosen light's white to the as-shot white. The
// whites being equal gives exactly identity_matrix.
[[nodiscard]] Matrix3 white_balance_matrix(const WhiteBalance& chosen, Chromaticity as_shot);

}  // namespace prism

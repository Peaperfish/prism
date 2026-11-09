#include "prism/white_balance.hpp"

#include <cmath>
#include <stdexcept>

namespace prism {

namespace {

// Krystek, M. 1985, "An algorithm to calculate correlated colour temperature", Color Research &
// Application 10(1), 38-40: the Planckian locus in CIE 1960 uv as a ratio of two quadratics in T,
// valid from 1000 K to 15000 K.
struct Quadratic {
    double c0, c1, c2;
    double at(double t) const noexcept { return c0 + t * (c1 + t * c2); }
    double slope(double t) const noexcept { return c1 + 2.0 * c2 * t; }
};

constexpr Quadratic u_numerator{0.860117757, 1.54118254e-4, 1.28641212e-7};
constexpr Quadratic u_denominator{1.0, 8.42420235e-4, 7.08145163e-7};
constexpr Quadratic v_numerator{0.317398726, 4.22806245e-5, 4.20481691e-8};
constexpr Quadratic v_denominator{1.0, -2.89741816e-5, 1.61456053e-7};

// d/dT of n(T) / d(T), by the quotient rule.
double ratio_slope(const Quadratic& n, const Quadratic& d, double t) noexcept {
    const double dv = d.at(t);
    return (n.slope(t) * dv - n.at(t) * d.slope(t)) / (dv * dv);
}

}  // namespace

Uv planckian_uv(double kelvin) noexcept {
    return {u_numerator.at(kelvin) / u_denominator.at(kelvin),
            v_numerator.at(kelvin) / v_denominator.at(kelvin)};
}

Uv planckian_uv_slope(double kelvin) noexcept {
    return {ratio_slope(u_numerator, u_denominator, kelvin),
            ratio_slope(v_numerator, v_denominator, kelvin)};
}

Uv locus_normal(double kelvin) noexcept {
    const Uv tangent = planckian_uv_slope(kelvin);
    const double length = std::hypot(tangent.u, tangent.v);
    Uv n{-tangent.v / length, tangent.u / length};
    if (n.v < 0) {
        n = {-n.u, -n.v};
    }
    return n;
}

Uv uv_of(Chromaticity xy) noexcept {
    const double d = -2.0 * xy.x + 12.0 * xy.y + 3.0;
    return {4.0 * xy.x / d, 6.0 * xy.y / d};
}

Chromaticity xy_of(Uv uv) noexcept {
    const double d = 2.0 * uv.u - 8.0 * uv.v + 4.0;
    return {3.0 * uv.u / d, 2.0 * uv.v / d};
}

Chromaticity white_of(const WhiteBalance& wb) noexcept {
    const Uv locus = planckian_uv(wb.kelvin);
    const Uv n = locus_normal(wb.kelvin);
    const double duv = wb.tint * tint_duv_per_unit;
    return xy_of({locus.u + duv * n.u, locus.v + duv * n.v});
}

WhiteBalance white_balance_of(Chromaticity xy) noexcept {
    const Uv target = uv_of(xy);
    const auto distance2 = [&](double mired) {
        const Uv p = planckian_uv(1e6 / mired);
        return (p.u - target.u) * (p.u - target.u) + (p.v - target.v) * (p.v - target.v);
    };
    // Golden-section search in mired (1e6 / T), where the locus is far more evenly spaced than
    // in kelvin, to 1e-9 mired.
    constexpr double inv_phi = 0.6180339887498949;
    double lo = 1e6 / planckian_max_kelvin;
    double hi = 1e6 / planckian_min_kelvin;
    double a = hi - inv_phi * (hi - lo);
    double b = lo + inv_phi * (hi - lo);
    double fa = distance2(a);
    double fb = distance2(b);
    while (hi - lo > 1e-9) {
        if (fa < fb) {
            hi = b;
            b = a;
            fb = fa;
            a = hi - inv_phi * (hi - lo);
            fa = distance2(a);
        } else {
            lo = a;
            a = b;
            fa = fb;
            b = lo + inv_phi * (hi - lo);
            fb = distance2(b);
        }
    }
    const double kelvin = 1e6 / (0.5 * (lo + hi));
    const Uv locus = planckian_uv(kelvin);
    const Uv n = locus_normal(kelvin);
    const double duv = (target.u - locus.u) * n.u + (target.v - locus.v) * n.v;
    return {kelvin, duv / tint_duv_per_unit};
}

std::optional<Chromaticity> as_shot_white(const std::array<double, 3>& cam_mul,
                                          const Mat3& cam_xyz) {
    Vec3 neutral{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(cam_mul[i] > 0.0) || !std::isfinite(cam_mul[i])) {
            return std::nullopt;
        }
        neutral[i] = 1.0 / cam_mul[i];
    }
    Vec3 xyz;
    try {
        xyz = multiply(invert(cam_xyz), neutral);
    } catch (const std::domain_error&) {
        return std::nullopt;  // all zero, or singular
    }
    const double sum = xyz[0] + xyz[1] + xyz[2];
    const Chromaticity xy{xyz[0] / sum, xyz[1] / sum};
    if (!std::isfinite(xy.x) || !std::isfinite(xy.y) || xy.y <= 0.0) {
        return std::nullopt;
    }
    return xy;
}

Matrix3 white_balance_matrix(const WhiteBalance& chosen, Chromaticity as_shot) {
    const Chromaticity light = white_of(chosen);
    if (light.x == as_shot.x && light.y == as_shot.y) {
        return identity_matrix;  // rounding must not turn "no change" into a nearly-identity
    }
    const Mat3 adapt = bradford(light, as_shot);
    return to_matrix3(multiply(xyz_to_srgb(), multiply(adapt, srgb_to_xyz())));
}

}  // namespace prism

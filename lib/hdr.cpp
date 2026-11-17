#include "prism/hdr.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>

#include "c_number.hpp"
#include "prism/number_text.hpp"
#include "prism/process.hpp"
#include "prism/ranges.hpp"
#include "sample_ranges.hpp"

namespace prism {

namespace {

// The HDR curve given the SDR curve `c` as a callable: map() and hdr_curve() share it, with c
// known at compile time in hdr_curve's loop.
template <typename Curve>
float hdr_value(const HdrCurve& h, const Curve& c, float x) noexcept {
    // !(x > knee) is also true for NaN, which then stays NaN through std::min.
    if (!h.lift || !(x > hdr_knee)) {
        return std::min(c(x), h.peak);
    }
    if (x >= h.white) {
        return h.peak;
    }
    const float u = std::log2(x) - h.log2_knee;
    return std::min(h.knee_value * std::exp2(u * (h.log_slope + h.bend * u)), h.peak);
}

struct Identity {
    float operator()(float x) const noexcept { return x; }
};

// Each curve's log-slope x * c'(x) / c(x), from its formula. The scale factors (filmic's `scale`)
// cancel out.
double log_slope(const GammaCurve& g, double) { return g.inv_gamma; }

double log_slope(const ReinhardCurve& r, double x) {
    // c = x (1 + a x) / (1 + x): ln c = ln x + ln(1 + a x) - ln(1 + x).
    const double a = r.inv_white_sq;
    return 1.0 + a * x / (1.0 + a * x) - x / (1.0 + x);
}

double log_slope(const FilmicCurve&, double x) {
    // c = scale * h(2x), so x c'/c = y h'(y) / h(y) with y = 2x, and h = n / den - e / f.
    constexpr double a = 0.15, b = 0.50, c = 0.10, d = 0.20, e = 0.02, f = 0.30;
    const double y = 2.0 * x;
    const double n = y * (a * y + c * b) + d * e;
    const double dn = 2.0 * a * y + c * b;
    const double den = y * (a * y + b) + d * f;
    const double dden = 2.0 * a * y + b;
    const double h = n / den - e / f;
    const double dh = (dn * den - n * dden) / (den * den);
    return y * dh / h;
}

}  // namespace

double tone_curve_log_slope(const std::optional<ToneCurve>& curve, double x) {
    if (!curve) {
        return 1.0;
    }
    return std::visit([x](const auto& shape) { return log_slope(shape, x); }, curve->shape);
}

HdrCurve make_hdr_curve(const std::optional<ToneCurve>& sdr, float white, float headroom_stops) {
    if (!(headroom_stops > headroom_min_stops && headroom_stops <= headroom_max_stops)) {
        throw std::invalid_argument("HDR curve: headroom must be above 0 and at most 4 stops");
    }
    if (!std::isfinite(white)) {
        throw std::invalid_argument("HDR curve: the file's white must be a finite number");
    }
    HdrCurve h;
    h.sdr = sdr;
    h.white = white;
    h.headroom_stops = headroom_stops;
    h.peak = std::exp2(headroom_stops);
    const auto c = [&](float x) { return sdr ? sdr->map(x) : x; };
    h.knee_value = c(hdr_knee);
    h.log2_knee = std::log2(hdr_knee);
    // The guard: no room between the knee and the file's white.
    h.lift = white > hdr_knee && c(white) > h.knee_value;
    if (h.lift) {
        const double g0 = tone_curve_log_slope(sdr, hdr_knee);
        const double U = std::log2(static_cast<double>(white) / hdr_knee);
        const double V = std::log2(static_cast<double>(h.peak) / h.knee_value);
        h.log_slope = static_cast<float>(g0);
        h.bend = static_cast<float>((V - g0 * U) / (U * U));
    }
    return h;
}

float HdrCurve::map(float x) const noexcept {
    if (!sdr) {
        return hdr_value(*this, Identity{}, x);
    }
    return std::visit([&](const auto& shape) { return hdr_value(*this, shape, x); }, sdr->shape);
}

float hdr_file_white(const ProcessSettings& s) {
    Image white{1, 1, {1.0f, 1.0f, 1.0f}};
    ProcessSettings before_branch = s;
    before_branch.dodge_burn = {};
    before_branch.curve.reset();
    before_branch.headroom_stops.reset();
    (void)process(white, before_branch, 1);
    return std::max({white.pixels[0], white.pixels[1], white.pixels[2]});
}

static_assert(headroom_range.min == headroom_min_stops && headroom_range.max == headroom_max_stops);

std::optional<float> parse_headroom(std::string_view text) {
    const std::string s = minus_as_hyphen(text);  // null-terminated, as c_strtod needs
    // c_strtod skips leading white space; the whole text must be the number.
    if (s.empty() || std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        return std::nullopt;
    }
    char* end = nullptr;
    errno = 0;
    const double value = c_strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || errno == ERANGE || !std::isfinite(value) ||
        value < headroom_range.min || value > headroom_range.max) {
        return std::nullopt;
    }
    return static_cast<float>(value);
}

void hdr_curve(Image& img, const HdrCurve& curve, int threads) {
    detail::require_consistent(img, "hdr_curve");
    const auto run = [&](const auto& shape) {
        detail::parallel_for_samples(img.width, img.height, threads,
                                     [&](std::size_t first, std::size_t last) {
                                         float* p = img.pixels.data();
                                         for (std::size_t i = first; i < last; ++i) {
                                             p[i] = hdr_value(curve, shape, p[i]);
                                         }
                                     });
    };
    if (!curve.sdr) {
        run(Identity{});
    } else {
        std::visit(run, curve.sdr->shape);
    }
}

}  // namespace prism

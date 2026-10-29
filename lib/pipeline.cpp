#include "prism/pipeline.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "sample_ranges.hpp"

namespace prism {

namespace {

// Parses the whole of `text` as a finite number, or returns nothing.
// std::strtod reports where it stopped, which lets us reject trailing junk like "2.2x".
std::optional<float> parse_float(std::string_view text) {
    const std::string s(text);  // strtod needs a null-terminated string
    if (s.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || errno == ERANGE || !std::isfinite(value)) {
        return std::nullopt;
    }
    return static_cast<float>(value);
}

// --- Tone curves -----------------------------------------------------------------------------
//
// To add a curve: write a make_* function and add one row to `curves` below.
// Each factory receives the text after ':' (empty if there was none) and either returns the
// curve or throws std::runtime_error explaining what was wrong with the parameter.

float require_positive_param(std::string_view curve, std::string_view param) {
    const std::optional<float> value = parse_float(param);
    if (!value || *value <= 0.0f) {
        throw std::runtime_error("curve '" + std::string(curve) +
                                 "' needs a positive number, got '" + std::string(param) + "'");
    }
    return *value;
}

// Brightens midtones for gamma > 1 and darkens them for gamma < 1; gamma:1 changes nothing.
// 0 and 1 stay fixed. Negative inputs (possible after a matrix) are clamped to 0 first,
// because pow() of a negative number with a fractional exponent is NaN.
ToneCurve make_gamma(std::string_view param) {
    if (param.empty()) {
        throw std::runtime_error("curve 'gamma' needs a value, e.g. gamma:2.2");
    }
    const float inv = 1.0f / require_positive_param("gamma", param);
    return {"gamma", [inv](float x) { return std::pow(std::max(x, 0.0f), inv); }};
}

// Extended Reinhard: x * (1 + x / w^2) / (1 + x). Compresses highlights smoothly so that
// linear value `w` (the "white point") maps to exactly 1. Default w = 4, i.e. 2 stops of
// highlight headroom above 1.0.
ToneCurve make_reinhard(std::string_view param) {
    const float white = param.empty() ? 4.0f : require_positive_param("reinhard", param);
    const float inv_white_sq = 1.0f / (white * white);
    return {"reinhard", [inv_white_sq](float x) {
                x = std::max(x, 0.0f);
                return x * (1.0f + x * inv_white_sq) / (1.0f + x);
            }};
}

// John Hable's "Uncharted 2" filmic curve: a gentle toe in the shadows and a soft shoulder
// in the highlights. Constants and the 2x exposure bias are from his original write-up.
float hable(float x) {
    constexpr float a = 0.15f;  // shoulder strength
    constexpr float b = 0.50f;  // linear strength
    constexpr float c = 0.10f;  // linear angle
    constexpr float d = 0.20f;  // toe strength
    constexpr float e = 0.02f;  // toe numerator
    constexpr float f = 0.30f;  // toe denominator
    return (x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f) - e / f;
}

ToneCurve make_filmic(std::string_view param) {
    if (!param.empty()) {
        throw std::runtime_error("curve 'filmic' takes no parameter");
    }
    constexpr float exposure_bias = 2.0f;
    constexpr float linear_white = 11.2f;
    const float scale = 1.0f / hable(linear_white);
    return {"filmic",
            [scale](float x) { return hable(exposure_bias * std::max(x, 0.0f)) * scale; }};
}

struct CurveEntry {
    std::string_view name;
    std::string_view spec;  // shown in help and error messages
    ToneCurve (*make)(std::string_view param);
};

constexpr CurveEntry curves[] = {
    {"gamma", "gamma:<value>", make_gamma},
    {"reinhard", "reinhard[:<white>]", make_reinhard},
    {"filmic", "filmic", make_filmic},
};

}  // namespace

// All three stages below edit pixels in place from several threads at once. That is safe without
// locks because a data race needs two threads touching the *same* memory location with at least
// one writing, and here each thread only reads and writes the samples of its own rows.

void exposure(Image& img, float stops, int threads) {
    detail::require_consistent(img, "exposure");
    const float gain = std::exp2(stops);
    detail::parallel_for_samples(img.width, img.height, threads,
                                 [&](std::size_t first, std::size_t last) {
                                     for (std::size_t i = first; i < last; ++i) {
                                         img.pixels[i] *= gain;
                                     }
                                 });
}

void apply_matrix(Image& img, const Matrix3& m, int threads) {
    detail::require_consistent(img, "apply_matrix");
    detail::parallel_for_samples(
        img.width, img.height, threads, [&](std::size_t first, std::size_t last) {
            // Chunks start on a row boundary, so `first` is always the start of a pixel.
            for (std::size_t i = first; i < last; i += Image::channels) {
                // Copy the input first: writing p[0] before computing p[1] would use the new red.
                float* p = &img.pixels[i];
                const float r = p[0];
                const float g = p[1];
                const float b = p[2];
                p[0] = m[0] * r + m[1] * g + m[2] * b;
                p[1] = m[3] * r + m[4] * g + m[5] * b;
                p[2] = m[6] * r + m[7] * g + m[8] * b;
            }
        });
}

void saturation(Image& img, float s, int threads) {
    detail::require_consistent(img, "saturation");
    if (s == 1.0f) {
        return;  // exactly a no-op, as documented (the gamut limit below would otherwise apply)
    }
    detail::parallel_for_samples(
        img.width, img.height, threads, [&](std::size_t first, std::size_t last) {
            for (std::size_t i = first; i < last; i += Image::channels) {
                float* p = &img.pixels[i];
                const float y = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
                if (!(y > 0.0f)) {
                    p[0] = p[1] = p[2] = 0.0f;
                    continue;
                }
                // The weakest channel ends up at y + k * (lo - y). Solving that for 0 gives the
                // largest k that keeps it non-negative: y / (y - lo). (lo < y here, else the
                // pixel would be grey and k would not matter.)
                const float lo = std::min({p[0], p[1], p[2]});
                float k = s;
                if (lo < y && y + k * (lo - y) < 0.0f) {
                    k = y / (y - lo);
                }
                for (int c = 0; c < 3; ++c) {
                    p[c] = std::max(y + k * (p[c] - y), 0.0f);  // max only absorbs rounding
                }
            }
        });
}

void contrast(Image& img, float c, int threads) {
    detail::require_consistent(img, "contrast");
    if (c == 1.0f) {
        return;
    }
    const float strength = (c - 1.0f) * contrast_range_stops;
    const float inv_range = 1.0f / contrast_range_stops;
    // The curve's slope at 0, used for x <= 0, where log2 is undefined: as x -> 0, e -> -infinity
    // and the sigmoid -> -1, so x' -> x * 2^(-strength).
    const float zero_slope = std::exp2(-strength);
    detail::parallel_for_samples(
        img.width, img.height, threads, [&](std::size_t first, std::size_t last) {
            for (std::size_t i = first; i < last; ++i) {
                const float x = img.pixels[i];
                if (!(x > 0.0f)) {
                    img.pixels[i] = x * zero_slope;
                    continue;
                }
                // x' = 0.18 * 2^(e + strength * sigmoid(u)) = x * 2^(strength * sigmoid(u)).
                // The sigmoid u / sqrt(1 + u^2) costs a sqrt, where std::tanh costs about as much
                // as log2 and exp2 together.
                const float u = std::log2(x / contrast_pivot) * inv_range;
                img.pixels[i] = x * std::exp2(strength * u / std::sqrt(1.0f + u * u));
            }
        });
}

Matrix3 load_matrix(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open matrix file '" + path.string() + "'");
    }

    Matrix3 m{};
    std::size_t count = 0;
    std::string line;
    for (int line_number = 1; std::getline(file, line); ++line_number) {
        line = line.substr(0, line.find('#'));  // drop comments
        std::istringstream words(line);
        std::string word;
        while (words >> word) {
            const std::optional<float> value = parse_float(word);
            if (!value) {
                throw std::runtime_error("matrix file '" + path.string() + "' line " +
                                         std::to_string(line_number) + ": '" + word +
                                         "' is not a number");
            }
            if (count < m.size()) {
                m[count] = *value;
            }
            ++count;
        }
    }
    if (count != m.size()) {
        throw std::runtime_error("matrix file '" + path.string() + "' must contain 9 numbers, found " +
                                 std::to_string(count));
    }
    return m;
}

ToneCurve parse_curve(std::string_view spec) {
    const std::size_t colon = spec.find(':');
    const std::string_view name = spec.substr(0, colon);
    const std::string_view param =
        colon == std::string_view::npos ? std::string_view{} : spec.substr(colon + 1);

    for (const CurveEntry& entry : curves) {
        if (entry.name == name) {
            return entry.make(param);
        }
    }

    std::string message = "unknown curve '" + std::string(spec) + "' (available:";
    for (const CurveEntry& entry : curves) {
        message += " " + std::string(entry.spec);
    }
    throw std::runtime_error(message + ")");
}

std::vector<std::string_view> curve_specs() {
    std::vector<std::string_view> specs;
    for (const CurveEntry& entry : curves) {
        specs.push_back(entry.spec);
    }
    return specs;
}

void tone_curve(Image& img, const ToneCurve& curve, int threads) {
    detail::require_consistent(img, "tone_curve");
    // Every thread calls the same curve.map at once. That is fine because calling it only reads
    // the curve's captured parameters; the curves keep no state that a call would modify.
    detail::parallel_for_samples(img.width, img.height, threads,
                                 [&](std::size_t first, std::size_t last) {
                                     for (std::size_t i = first; i < last; ++i) {
                                         img.pixels[i] = curve.map(img.pixels[i]);
                                     }
                                 });
}

}  // namespace prism

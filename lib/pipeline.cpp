#include "prism/pipeline.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
// To add a curve: add a struct for it to ToneCurve::shape (pipeline.hpp), write a make_* function
// and add one row to `curves` below.
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

ToneCurve make_gamma(std::string_view param) {
    if (param.empty()) {
        throw std::runtime_error("curve 'gamma' needs a value, e.g. gamma:2.2");
    }
    return {"gamma", GammaCurve{1.0f / require_positive_param("gamma", param)}};
}

// Default white 4, i.e. 2 stops of highlight headroom above 1.0.
ToneCurve make_reinhard(std::string_view param) {
    const float white = param.empty() ? 4.0f : require_positive_param("reinhard", param);
    return {"reinhard", ReinhardCurve{1.0f / (white * white)}};
}

ToneCurve make_filmic(std::string_view param) {
    if (!param.empty()) {
        throw std::runtime_error("curve 'filmic' takes no parameter");
    }
    constexpr float linear_white = 11.2f;
    return {"filmic", FilmicCurve{1.0f / FilmicCurve::hable(linear_white)}};
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
    detail::parallel_for_samples(img.width, img.height, threads,
                                 [&](std::size_t first, std::size_t last) {
                                     // Chunks start on a row boundary, so `first` is always the
                                     // start of a pixel.
                                     for (std::size_t i = first; i < last; i += Image::channels) {
                                         // Copy the input first: writing p[0] before computing p[1]
                                         // would use the new red.
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

namespace {

// The factor contrast multiplies x > 0 by: x' = 0.18 * 2^(e + strength * sigmoid(u)) =
// x * 2^(strength * sigmoid(u)). A separate function so the table can store it directly:
// computing it as x' / x would overflow to infinity / infinity for the largest floats.
float contrast_gain(float x, float strength) noexcept {
    // log2(x) - log2(0.18) rather than log2(x / 0.18), which overflows for x above ~6e37.
    const float u = (std::log2(x) - std::log2(contrast_pivot)) / contrast_range_stops;
    // The sigmoid u / sqrt(1 + u^2) costs a sqrt, where std::tanh costs about as much as log2
    // and exp2 together.
    return std::exp2(strength * u / std::sqrt(1.0f + u * u));
}

}  // namespace

float contrast_reference(float x, float c) noexcept {
    const float strength = (c - 1.0f) * contrast_range_stops;
    // The curve's slope at 0, used for x <= 0, where log2 is undefined: as x -> 0, e -> -infinity
    // and the sigmoid -> -1, so x' -> x * 2^(-strength).
    if (!(x > 0.0f)) {
        return x * std::exp2(-strength);
    }
    return x * contrast_gain(x, strength);
}

namespace {

// contrast() as a lookup table: the gain g(x) = x' / x at evenly spaced float bit patterns, with
// linear interpolation in between. Calling std::log2 and std::exp2 for every value costs ~5 ns,
// a table lookup ~1.2 ns. (Inline polynomial log2 / exp2, which the compiler can vectorise, were
// tried too: ~1.4 ns, since they still need a division and a square root per value.)
//
// The index is the float's bit pattern shifted right by 15: its 8 exponent bits and top 8
// mantissa bits. Positive floats sort the same as their bits, and within one power of two the
// bits grow linearly with the value, so a point every 2^15 bit patterns is 256 points per stop,
// spaced at most 0.4% apart, over the whole float range. The low 15 bits say how far x is
// between two points. Between those points the gain is smooth (it changes with x in stops), so
// straight-line interpolation is very close. pipeline_test.cpp checks the result, after the
// filmic curve and sRGB encode, against contrast_reference: the worst case is ~0.1 of an 8-bit
// level, at c = 10.
//
// The points are shifted by the low 15 bits of 0.18f, so that 0.18 lands exactly on a point,
// where the gain is exactly 1: middle grey stays exactly where it is, as documented.
//
// The index keeps the sign bit, so the table covers all 2^32 bit patterns: every input gives a
// valid index, and one formula, x * gain, is right for all of them. The negative half holds the
// curve's slope at 0, which is what contrast_reference does for x <= 0. Only the parts of the
// table that real pixel values touch are ever loaded into the cache.
class ContrastTable {
public:
    explicit ContrastTable(float c) {
        const float strength = (c - 1.0f) * contrast_range_stops;
        const float zero_slope = std::exp2(-strength);
        for (std::uint32_t k = 0; k < half; ++k) {
            // Points past the largest float are only used by NaN, which stays NaN times any gain.
            const std::uint32_t bits = offset + (k << shift);
            gain_[k] =
                contrast_gain(std::bit_cast<float>(std::min(bits, max_float_bits)), strength);
        }
        std::fill(gain_.begin() + half, gain_.end(), zero_slope);
        // Subtracting `offset` wraps the tiniest positive denormals (bits below `offset`, under
        // 3e-41) and +0 around to the last interval, which therefore runs from the slope at 0 to
        // the gain at the first point. -0 and the tiniest negative denormals land in interval
        // half - 1, which must give exactly the slope at 0, like every other negative number.
        gain_[half - 1] = zero_slope;
        gain_[half] = zero_slope;
        gain_[2 * half] = gain_[0];
    }

    // Any input. x <= 0 gets the curve's slope at 0, as in contrast_reference, and NaN stays NaN.
    // +infinity stays +infinity (contrast_reference gives NaN there: infinity / infinity).
    [[nodiscard]] float operator()(float x) const noexcept {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(x) - offset;
        const std::uint32_t k = bits >> shift;  // 0 .. 2 * half - 1
        const float t = static_cast<float>(bits & ((1u << shift) - 1)) * (1.0f / (1u << shift));
        return x * (gain_[k] + (gain_[k + 1] - gain_[k]) * t);
    }

private:
    static constexpr std::uint32_t shift = 15;
    static constexpr std::uint32_t half = 1u << (32 - 1 - shift);  // 2^16 points per sign
    static constexpr std::uint32_t offset =
        std::bit_cast<std::uint32_t>(contrast_pivot) & ((1u << shift) - 1);
    static constexpr std::uint32_t max_float_bits = 0x7F7FFFFFu;

    // One extra entry: gain[k + 1] for the last k. 512 KB, so on the heap rather than the stack.
    std::vector<float> gain_ = std::vector<float>(2 * half + 1);
};

}  // namespace

void contrast(Image& img, float c, int threads) {
    detail::require_consistent(img, "contrast");
    if (c == 1.0f) {
        return;
    }
    // ~65,000 calls to contrast_gain: about 0.4 ms, once per image.
    const ContrastTable table(c);
    detail::parallel_for_samples(img.width, img.height, threads,
                                 [&](std::size_t first, std::size_t last) {
                                     float* p = img.pixels.data();
                                     for (std::size_t i = first; i < last; ++i) {
                                         p[i] = table(p[i]);
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
        throw std::runtime_error("matrix file '" + path.string() +
                                 "' must contain 9 numbers, found " + std::to_string(count));
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
    // std::visit runs the lambda below with `shape` as its actual type (GammaCurve, ReinhardCurve
    // or FilmicCurve): the lambda is a template, compiled once per curve type. So inside the loop
    // the compiler knows exactly which operator() it calls and can inline it, and then vectorise
    // the loop (process 4 floats per instruction) where the maths allows. The type is checked
    // once per image here, instead of once per value as calling through a std::function would.
    //
    // Every thread uses the same curve at once. That is fine because calling it only reads the
    // curve's parameters.
    std::visit(
        [&](const auto& shape) {
            detail::parallel_for_samples(img.width, img.height, threads,
                                         [&](std::size_t first, std::size_t last) {
                                             float* p = img.pixels.data();
                                             for (std::size_t i = first; i < last; ++i) {
                                                 p[i] = shape(p[i]);
                                             }
                                         });
        },
        curve.shape);
}

}  // namespace prism

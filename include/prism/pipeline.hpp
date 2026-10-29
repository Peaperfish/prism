#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "prism/image.hpp"

namespace prism {

// Each step edits the image in place: photos are tens of megabytes as float, so returning
// a modified copy from every step would allocate and copy the whole buffer each time.

// Multiplies every channel by 2^stops (+1 stop doubles the light, -1 halves it).
void exposure(Image& img, float stops);

// A 3x3 colour matrix in row-major order: row i gives output channel i (R, G, B) as a
// weighted sum of the input R, G and B. For example, output R = m[0]*R + m[1]*G + m[2]*B.
using Matrix3 = std::array<float, 9>;

inline constexpr Matrix3 identity_matrix = {1, 0, 0,  //
                                            0, 1, 0,  //
                                            0, 0, 1};

void apply_matrix(Image& img, const Matrix3& m);

// Reads exactly 9 whitespace-separated numbers (row-major) from a text file.
// Text after '#' on a line is a comment. Throws std::runtime_error on any problem.
[[nodiscard]] Matrix3 load_matrix(const std::filesystem::path& path);

// A tone curve maps one linear value to another and is applied to each channel separately.
struct ToneCurve {
    std::string name;
    std::function<float(float)> map;
};

// Parses a curve spec such as "gamma:2.2", "reinhard", "reinhard:4" or "filmic".
// Throws std::runtime_error for unknown names or bad parameters.
[[nodiscard]] ToneCurve parse_curve(std::string_view spec);

// Spec formats for every supported curve (e.g. "gamma:<value>"), for help messages.
[[nodiscard]] std::vector<std::string_view> curve_specs();

void tone_curve(Image& img, const ToneCurve& curve);

}  // namespace prism

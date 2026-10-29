#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "prism/pipeline.hpp"

namespace prism {

// Colour maths for building a camera RGB -> linear sRGB matrix (what --raw-camera-matrix takes)
// from a camera profile. Everything here is plain 3x3 linear algebra in double precision; the
// result is rounded to float (Matrix3) only at the end, when it becomes a pipeline matrix.
//
// A matrix M maps a column vector v to M * v. Chaining two conversions "first A, then B" is
// B * A: the matrix nearest the vector is applied first, so chains read right to left.

using Mat3 = std::array<std::array<double, 3>, 3>;  // m[row][column]
using Vec3 = std::array<double, 3>;

inline constexpr Mat3 identity3 = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

[[nodiscard]] Mat3 multiply(const Mat3& a, const Mat3& b) noexcept;
[[nodiscard]] Vec3 multiply(const Mat3& m, const Vec3& v) noexcept;

// The inverse of m, via the adjugate (cofactor) formula. Throws std::domain_error if m is singular
// or so close to singular that its inverse would be dominated by rounding error: |det| below
// 1e-12 times the size of m's entries cubed.
[[nodiscard]] Mat3 invert(const Mat3& m);

// m with each row divided by its sum, so m * (1, 1, 1) = (1, 1, 1): equal camera channels (white
// after white balance) give equal output channels (white). Throws std::domain_error if a row sums
// to (nearly) zero, where no scaling can make it sum to 1.
[[nodiscard]] Mat3 normalize_rows(const Mat3& m);

// Matrix3 (float, row-major) as used by apply_matrix and load_matrix.
[[nodiscard]] Matrix3 to_matrix3(const Mat3& m) noexcept;

// --- Colour spaces -------------------------------------------------------------------------------

// A CIE 1931 xy chromaticity: the colour of a light or primary, ignoring its brightness.
struct Chromaticity {
    double x = 0;
    double y = 0;
};

// Standard white points, as the colour space specifications give them.
inline constexpr Chromaticity white_d65 = {0.3127, 0.3290};  // daylight; sRGB, Rec. 2020
inline constexpr Chromaticity white_d50 = {0.3457, 0.3585};  // DNG's and ICC's XYZ connection space

// XYZ of a chromaticity at luminance Y = 1.
[[nodiscard]] Vec3 xyz_of(Chromaticity c) noexcept;

// The linear RGB -> XYZ matrix of an RGB space with these primaries and white point. Its columns
// are the XYZ of pure red, green and blue, scaled so that RGB (1, 1, 1) gives the white at Y = 1.
[[nodiscard]] Mat3 rgb_to_xyz(Chromaticity red, Chromaticity green, Chromaticity blue,
                              Chromaticity white);

[[nodiscard]] Mat3 srgb_to_xyz();     // Rec. 709 primaries, D65
[[nodiscard]] Mat3 xyz_to_srgb();     // its inverse
[[nodiscard]] Mat3 rec2020_to_xyz();  // Rec. 2020 primaries, D65

// Bradford chromatic adaptation: maps XYZ seen under white `from` to the XYZ that looks the same
// under white `to` (e.g. D50 -> D65), by scaling in the Bradford "cone response" space.
[[nodiscard]] Mat3 bradford(Chromaticity from, Chromaticity to);

// --- Camera -> linear sRGB -----------------------------------------------------------------------
// Each returns a matrix from white-balanced camera RGB to linear sRGB (D65), rows summing to 1.

// From a DNG ForwardMatrix (white-balanced camera RGB -> XYZ D50):
//   normalize_rows(xyz_to_srgb * bradford(D50, D65) * forward)
[[nodiscard]] Mat3 camera_to_srgb_from_forward_matrix(const Mat3& forward);

// From a DNG ColorMatrix (XYZ -> camera RGB, *before* white balance) for a D65-ish light:
//   normalize_rows(xyz_to_srgb * invert(color) * diag(color * D65 white))
// The diag() term is white balance: it scales each camera channel by the camera's response to
// white, so camera white becomes (1, 1, 1). See camera_profile.cpp for why it multiplies columns.
[[nodiscard]] Mat3 camera_to_srgb_from_color_matrix(const Mat3& color);

// From white-balanced camera RGB -> linear Rec. 2020 (e.g. a LightCraft profile's "matrix").
// Both spaces use D65, so only the primaries change:
//   normalize_rows(xyz_to_srgb * rec2020_to_xyz * m)
[[nodiscard]] Mat3 camera_to_srgb_from_rec2020(const Mat3& m);

// --- DNG profile data ----------------------------------------------------------------------------

// The colour tags of a DNG or DCP profile: up to two calibrations, each for one illuminant
// (CalibrationIlluminant1/2, an EXIF LightSource code such as 17 = Standard light A, 21 = D65).
struct DngCalibration {
    int illuminant = 0;
    std::optional<Mat3> color_matrix;    // XYZ -> camera
    std::optional<Mat3> forward_matrix;  // white-balanced camera -> XYZ D50
};

// Correlated colour temperature in kelvin of an EXIF LightSource code, or nothing for codes
// without a defined temperature (0 = unknown, 255 = other).
[[nodiscard]] std::optional<double> illuminant_kelvin(int exif_light_source) noexcept;

// The DNG-style camera -> sRGB matrix from up to two calibrations. Picks the calibration whose
// illuminant is closest to D65 (in mireds, 1e6 / kelvin, which is roughly perceptually even), then
// uses its ForwardMatrix if present, else its ColorMatrix. Throws std::invalid_argument if no
// calibration has a matrix.
[[nodiscard]] Mat3 camera_to_srgb_from_dng(const std::array<DngCalibration, 2>& calibrations);

// --- LightCraft camera profiles ------------------------------------------------------------------

// A LightCraft camera profile (assets/camera-profiles/<model>.json in LightCraft), version 1.
// `matrix` maps white-balanced camera RGB to linear Rec. 2020. It was fitted to the camera's own
// JPEGs, so it carries the camera's rendering (e.g. Sony "Standard"), not a colorimetric
// calibration. The hue/saturation/value table has no equivalent in a 3x3 matrix: only its size is
// read, so tools can report that it was skipped.
struct LightCraftProfile {
    int version = 0;
    std::string model;
    long long files = 0;    // photos the profile was fitted on
    long long samples = 0;  // colour pairs
    Mat3 matrix{};
    bool has_hue_sat = false;
    int hue_divisions = 0;
    int sat_divisions = 0;
    int val_divisions = 0;
};

// Parses a LightCraft profile's JSON text. Throws std::runtime_error if it is not valid JSON, is
// not version 1, or lacks a model or a 3x3 matrix of finite numbers.
[[nodiscard]] LightCraftProfile parse_lightcraft_profile(std::string_view json);

// Reads and parses a profile file. Throws std::runtime_error on any problem.
[[nodiscard]] LightCraftProfile load_lightcraft_profile(const std::string& path);

}  // namespace prism

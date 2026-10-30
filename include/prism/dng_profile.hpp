#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "prism/camera_profile.hpp"

namespace prism {

// The colour data of a DNG file or a DCP camera profile (Adobe DNG Specification 1.7.1.0,
// chapter 4 "DNG Tags" and chapter 6 "Mapping Camera Color Space to CIE XYZ Space").
//
// A DCP is a TIFF-like file holding one camera profile; a DNG carries one in its IFD 0, plus
// per-file tags such as AsShotNeutral. Only the matrices and their illuminants are read, and the
// tags that will be needed to apply them; the profile's tables (hue/saturation map, look table,
// tone curve) have no 3x3 equivalent, so only their sizes are read, to report what was skipped.
// Nothing here is applied yet: CameraCalibration and AnalogBalance are carried, not used.
struct DngProfile {
    enum class Source { dng, dcp };  // TIFF magic number 42, or 0x4352 ("CR")
    Source source = Source::dng;

    // CalibrationIlluminant1/2 with ColorMatrix1/2 and ForwardMatrix1/2 (calibration k has no
    // matrices if neither tag is present). Ready for camera_to_srgb_from_dng.
    std::array<DngCalibration, 2> calibrations{};
    // DNG 1.6 added a third calibration (CalibrationIlluminant3, ColorMatrix3, ...). Readers may
    // ignore it and use the first two, which is what this does; true if the file has one.
    bool ignored_third_calibration = false;

    // Reference camera -> this camera's native space, per calibration (DNG, IFD 0 only). The
    // spec says to use them only if camera_calibration_signature == profile_calibration_signature.
    std::array<std::optional<Mat3>, 2> camera_calibration{};
    std::optional<Vec3> analog_balance;   // gain already applied to the raw values
    std::optional<Vec3> as_shot_neutral;  // white balance at capture, in camera values
    std::optional<std::string> camera_calibration_signature;
    std::optional<std::string> profile_calibration_signature;

    // Text exactly as stored, up to its first NUL: it may contain any other byte, including
    // control characters and newlines, so sanitise it before writing it anywhere.
    std::optional<std::string> unique_camera_model;
    std::optional<std::string> profile_name;
    std::optional<std::string> profile_copyright;
    // The profile's usage rules (0 = allow copying, 1 = embed if used, 2 = embed never,
    // 3 = no restrictions; DNG spec, ProfileEmbedPolicy). Only read, never acted on here.
    std::optional<std::uint32_t> embed_policy;

    // Sizes of the skipped tables.
    struct TableSize {
        std::uint32_t hue_divisions = 0;
        std::uint32_t sat_divisions = 0;
        std::uint32_t val_divisions = 0;
    };
    std::optional<TableSize> hue_sat_map;  // ProfileHueSatMapDims
    int hue_sat_map_tables = 0;            // how many of ProfileHueSatMapData1/2/3 are present
    std::optional<TableSize> look_table;   // ProfileLookTableDims
    std::optional<std::uint32_t> tone_curve_points;  // ProfileToneCurve, (x, y) pairs
};

// Files larger than this are refused before reading: DNGs are tens of MB, DCPs a few hundred KB.
inline constexpr std::size_t max_dng_profile_bytes = std::size_t{512} * 1024 * 1024;

// Parses a whole DNG or DCP file held in memory. Throws std::runtime_error if it is not a valid
// TIFF (see lib/tiff_reader.hpp), or if its colour tags are unusable: not 3 colour planes (4-colour
// cameras are not supported), a matrix or vector with the wrong number of values, a zero
// denominator or non-finite value, a matrix without its CalibrationIlluminant, or no ColorMatrix
// or ForwardMatrix at all. A tag stored with a type the spec doesn't allow for it is ignored, as if
// absent (TIFF readers skip what they don't expect); missing optional tags are fine.
[[nodiscard]] DngProfile parse_dng_profile(std::span<const std::byte> file);

// Reads a file (at most max_dng_profile_bytes) and parses it. Throws std::runtime_error on any
// problem, with the path in the message.
[[nodiscard]] DngProfile load_dng_profile(const std::filesystem::path& path);

}  // namespace prism

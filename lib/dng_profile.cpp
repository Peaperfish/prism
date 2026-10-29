#include "prism/dng_profile.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "tiff_reader.hpp"

namespace prism {

namespace {

using detail::TiffEntry;
using detail::TiffIfd;
using detail::TiffType;

// Tag numbers, from the DNG specification 1.7.1.0, chapter 4.
namespace tag {
constexpr std::uint16_t unique_camera_model = 50708;
constexpr std::uint16_t color_matrix[2] = {50721, 50722};
constexpr std::uint16_t camera_calibration[2] = {50723, 50724};
constexpr std::uint16_t analog_balance = 50727;
constexpr std::uint16_t as_shot_neutral = 50728;
constexpr std::uint16_t calibration_illuminant[2] = {50778, 50779};
constexpr std::uint16_t camera_calibration_signature = 50931;
constexpr std::uint16_t profile_calibration_signature = 50932;
constexpr std::uint16_t profile_name = 50936;
constexpr std::uint16_t hue_sat_map_dims = 50937;
constexpr std::uint16_t hue_sat_map_data[3] = {50938, 50939, 52537};
constexpr std::uint16_t tone_curve = 50940;
constexpr std::uint16_t embed_policy = 50941;
constexpr std::uint16_t profile_copyright = 50942;
constexpr std::uint16_t forward_matrix[2] = {50964, 50965};
constexpr std::uint16_t look_table_dims = 50981;
// DNG 1.6's third calibration: CalibrationIlluminant3, CameraCalibration3, ColorMatrix3,
// ForwardMatrix3. (ProfileHueSatMapData3 only counts as a skipped table.)
constexpr std::uint16_t third_calibration[4] = {52529, 52530, 52531, 52532};
}  // namespace tag

[[noreturn]] void fail(const std::string& why) {
    throw std::runtime_error("DNG profile: " + why);
}

bool is_integer(TiffType t) {
    return t == TiffType::byte || t == TiffType::uint16 || t == TiffType::uint32;
}

// The entry for a tag if it is stored with a type the spec allows for it; nullptr if it is absent
// or has another type, which is skipped like the unknown types the TIFF reader skips.
template <typename Allowed>
const TiffEntry* find(const TiffIfd& ifd, std::uint16_t t, Allowed allowed) {
    const TiffEntry* e = ifd.find(t);
    return (e != nullptr && allowed(e->type)) ? e : nullptr;
}

const TiffEntry* find_type(const TiffIfd& ifd, std::uint16_t t, TiffType type) {
    return find(ifd, t, [type](TiffType x) { return x == type; });
}

// Value i as a finite double. The TIFF reader's error (e.g. a zero denominator) gets the tag's
// name added, so the message says which tag was damaged.
double value(const TiffIfd& ifd, const TiffEntry& e, std::uint32_t i, const std::string& name) {
    double v = 0;
    try {
        v = ifd.number(e, i);
    } catch (const std::runtime_error& err) {
        fail(name + ": " + err.what());
    }
    if (!std::isfinite(v)) {
        fail(name + " value " + std::to_string(i) + " is not a finite number");
    }
    return v;
}

// The number of colour planes a value count implies, or 0 if it implies none. Matrices store
// ColorPlanes * 3 values (ColorMatrix, ForwardMatrix) or ColorPlanes^2 (CameraCalibration);
// vectors (AnalogBalance, AsShotNeutral) one per plane.
enum class Shape { times3, square, vector };

std::uint32_t planes_for(std::uint32_t count, Shape shape) {
    switch (shape) {
        case Shape::times3: return count % 3 == 0 ? count / 3 : 0;
        case Shape::square: {
            const auto r =
                static_cast<std::uint32_t>(std::lround(std::sqrt(static_cast<double>(count))));
            return r * r == count ? r : 0;
        }
        case Shape::vector: return count;
    }
    return 0;
}

void require_count(const TiffEntry& e, std::uint32_t expected, Shape shape,
                   const std::string& name) {
    if (e.count == expected) {
        return;
    }
    std::string why = name + " has " + std::to_string(e.count) + " values, expected " +
                      std::to_string(expected) + " (for 3 colour planes)";
    if (const std::uint32_t planes = planes_for(e.count, shape); planes > 3) {
        why += "; that is a " + std::to_string(planes) +
               "-colour camera, and only 3-colour (RGB) cameras are supported";
    }
    fail(why);
}

std::optional<Mat3> read_matrix(const TiffIfd& ifd, std::uint16_t t, Shape shape,
                                const std::string& name) {
    const TiffEntry* e = find_type(ifd, t, TiffType::srational);
    if (e == nullptr) {
        return std::nullopt;
    }
    require_count(*e, 9, shape, name);
    Mat3 m{};
    for (std::uint32_t i = 0; i < 9; ++i) {
        m[i / 3][i % 3] = value(ifd, *e, i, name);  // stored in row scan order
    }
    return m;
}

template <typename Allowed>
std::optional<Vec3> read_vector(const TiffIfd& ifd, std::uint16_t t, Allowed allowed,
                                const std::string& name) {
    const TiffEntry* e = find(ifd, t, allowed);
    if (e == nullptr) {
        return std::nullopt;
    }
    require_count(*e, 3, Shape::vector, name);
    return Vec3{value(ifd, *e, 0, name), value(ifd, *e, 1, name), value(ifd, *e, 2, name)};
}

// An integer tag with `count` values (BYTE, SHORT or LONG: TIFF readers accept any of them).
const TiffEntry* find_integers(const TiffIfd& ifd, std::uint16_t t, std::uint32_t count,
                               const std::string& name) {
    const TiffEntry* e = find(ifd, t, is_integer);
    if (e != nullptr && e->count != count) {
        fail(name + " has " + std::to_string(e->count) + " values, expected " +
             std::to_string(count));
    }
    return e;
}

std::optional<DngProfile::TableSize> read_dims(const TiffIfd& ifd, std::uint16_t t,
                                               const std::string& name) {
    const TiffEntry* e = find_integers(ifd, t, 3, name);
    if (e == nullptr) {
        return std::nullopt;
    }
    return DngProfile::TableSize{ifd.integer(*e, 0), ifd.integer(*e, 1), ifd.integer(*e, 2)};
}

std::optional<std::string> read_text(const TiffIfd& ifd, std::uint16_t t) {
    const TiffEntry* e =
        find(ifd, t, [](TiffType x) { return x == TiffType::ascii || x == TiffType::byte; });
    return e != nullptr ? std::optional(ifd.string(*e)) : std::nullopt;
}

}  // namespace

DngProfile parse_dng_profile(std::span<const std::byte> file) {
    const TiffIfd ifd = detail::parse_tiff(file);
    DngProfile p;
    p.source = ifd.magic == detail::TiffMagic::camera_profile ? DngProfile::Source::dcp
                                                              : DngProfile::Source::dng;

    bool any_matrix = false;
    for (int k = 0; k < 2; ++k) {
        const std::string n = std::to_string(k + 1);
        DngCalibration& c = p.calibrations[static_cast<std::size_t>(k)];
        c.color_matrix = read_matrix(ifd, tag::color_matrix[k], Shape::times3, "ColorMatrix" + n);
        c.forward_matrix =
            read_matrix(ifd, tag::forward_matrix[k], Shape::times3, "ForwardMatrix" + n);
        p.camera_calibration[static_cast<std::size_t>(k)] =
            read_matrix(ifd, tag::camera_calibration[k], Shape::square, "CameraCalibration" + n);

        const TiffEntry* illuminant =
            find_integers(ifd, tag::calibration_illuminant[k], 1, "CalibrationIlluminant" + n);
        if (illuminant != nullptr) {
            c.illuminant = static_cast<int>(ifd.integer(*illuminant, 0));
        }
        const bool has_matrix = c.color_matrix || c.forward_matrix;
        // The spec lets CalibrationIlluminant1 default to 0 (unknown), but a matrix whose light
        // is unknown can't be chosen or interpolated by temperature, so require it.
        if (has_matrix && illuminant == nullptr) {
            fail((c.color_matrix ? "ColorMatrix" : "ForwardMatrix") + n +
                 " has no CalibrationIlluminant" + n + " saying which light it is for");
        }
        any_matrix = any_matrix || has_matrix;
    }
    if (!any_matrix) {
        fail(
            "no ColorMatrix or ForwardMatrix, so no colour calibration (a monochrome DNG has "
            "none)");
    }
    for (const std::uint16_t t : tag::third_calibration) {
        p.ignored_third_calibration = p.ignored_third_calibration || ifd.find(t) != nullptr;
    }

    p.analog_balance = read_vector(
        ifd, tag::analog_balance, [](TiffType x) { return x == TiffType::rational; },
        "AnalogBalance");
    p.as_shot_neutral = read_vector(
        ifd, tag::as_shot_neutral,
        [](TiffType x) { return x == TiffType::uint16 || x == TiffType::rational; },
        "AsShotNeutral");
    p.camera_calibration_signature = read_text(ifd, tag::camera_calibration_signature);
    p.profile_calibration_signature = read_text(ifd, tag::profile_calibration_signature);
    p.unique_camera_model = read_text(ifd, tag::unique_camera_model);
    p.profile_name = read_text(ifd, tag::profile_name);
    p.profile_copyright = read_text(ifd, tag::profile_copyright);
    if (const TiffEntry* e = find_integers(ifd, tag::embed_policy, 1, "ProfileEmbedPolicy")) {
        p.embed_policy = ifd.integer(*e, 0);
    }

    p.hue_sat_map = read_dims(ifd, tag::hue_sat_map_dims, "ProfileHueSatMapDims");
    for (const std::uint16_t t : tag::hue_sat_map_data) {
        p.hue_sat_map_tables += find_type(ifd, t, TiffType::float32) != nullptr ? 1 : 0;
    }
    p.look_table = read_dims(ifd, tag::look_table_dims, "ProfileLookTableDims");
    if (const TiffEntry* e = find_type(ifd, tag::tone_curve, TiffType::float32)) {
        if (e->count % 2 != 0) {
            fail("ProfileToneCurve has " + std::to_string(e->count) +
                 " values, but it is a list of (x, y) pairs");
        }
        p.tone_curve_points = e->count / 2;
    }
    return p;
}

DngProfile load_dng_profile(const std::string& path) {
    // Check the size before reading, so a huge file (or a mistaken path to a disk image) is
    // refused without allocating memory for it.
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw std::runtime_error("cannot open DNG profile '" + path + "': " + ec.message());
    }
    if (size > max_dng_profile_bytes) {
        throw std::runtime_error(path + ": file is " + std::to_string(size) +
                                 " bytes, more than the " + std::to_string(max_dng_profile_bytes) +
                                 "-byte limit for a DNG or DCP file");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open DNG profile '" + path + "'");
    }
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    // Reading through char* is allowed for any object: char may alias anything.
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (file.gcount() != static_cast<std::streamsize>(data.size())) {
        throw std::runtime_error("cannot read DNG profile '" + path + "'");
    }
    try {
        return parse_dng_profile(data);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

}  // namespace prism

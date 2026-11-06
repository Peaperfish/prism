#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <string>

#include "prism/photo_info.hpp"
#include "tiff_reader.hpp"

namespace prism {

namespace {

using detail::IfdMode;
using detail::TiffEntry;
using detail::TiffIfd;
using detail::TiffType;

constexpr std::uint16_t tag_make = 0x010F;
constexpr std::uint16_t tag_model = 0x0110;
constexpr std::uint16_t tag_orientation = 0x0112;
constexpr std::uint16_t tag_exif_ifd = 0x8769;
constexpr std::uint16_t tag_exposure_time = 0x829A;
constexpr std::uint16_t tag_f_number = 0x829D;
constexpr std::uint16_t tag_iso = 0x8827;
constexpr std::uint16_t tag_date_time_original = 0x9003;
constexpr std::uint16_t tag_focal_length = 0x920A;
constexpr std::uint16_t tag_focal_length_35mm = 0xA405;
constexpr std::uint16_t tag_lens_model = 0xA434;

// Each reader returns nullopt for a missing tag, a tag of the wrong type, or a bad value; one bad
// field never costs the others.

std::optional<std::string> text(const TiffIfd& ifd, std::uint16_t tag) {
    const TiffEntry* e = ifd.find(tag);
    if (e == nullptr || e->type != TiffType::ascii) {
        return std::nullopt;
    }
    try {
        std::string s = ifd.string(*e);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) {
            s.pop_back();
        }
        if (s.empty()) {
            return std::nullopt;
        }
        return s;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> integer(const TiffIfd& ifd, std::uint16_t tag) {
    const TiffEntry* e = ifd.find(tag);
    if (e == nullptr || e->count == 0 ||
        (e->type != TiffType::uint16 && e->type != TiffType::uint32 && e->type != TiffType::byte)) {
        return std::nullopt;
    }
    try {
        return ifd.integer(*e, 0);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A positive count (ISO, 35 mm focal length); 0 means "unknown" in EXIF, so it is absent here.
std::optional<int> positive_int(const TiffIfd& ifd, std::uint16_t tag) {
    const std::optional<std::uint32_t> v = integer(ifd, tag);
    if (!v || *v == 0 || *v > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(*v);
}

std::optional<double> positive_number(const TiffIfd& ifd, std::uint16_t tag) {
    const TiffEntry* e = ifd.find(tag);
    if (e == nullptr || e->count == 0 ||
        (e->type != TiffType::rational && e->type != TiffType::srational)) {
        return std::nullopt;
    }
    try {
        const double v = ifd.number(*e, 0);
        if (!std::isfinite(v) || v <= 0.0) {
            return std::nullopt;
        }
        return v;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::uint32_t read32(std::span<const std::byte> b, std::size_t at, bool big) {
    std::uint32_t v = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        const std::size_t i = big ? at + k : at + 3 - k;
        v = (v << 8) | std::to_integer<std::uint32_t>(b[i]);
    }
    return v;
}

PhotoInfo read(std::span<const std::byte> file) {
    PhotoInfo info;
    if (file.size() < 8) {
        return info;
    }
    bool big = false;
    if (file[0] == std::byte{'M'} && file[1] == std::byte{'M'}) {
        big = true;
    } else if (!(file[0] == std::byte{'I'} && file[1] == std::byte{'I'})) {
        return info;
    }
    // The magic number is 42 in either order; a block that says otherwise isn't EXIF.
    const std::uint32_t magic = big ? (std::to_integer<std::uint32_t>(file[2]) << 8) |
                                          std::to_integer<std::uint32_t>(file[3])
                                    : (std::to_integer<std::uint32_t>(file[3]) << 8) |
                                          std::to_integer<std::uint32_t>(file[2]);
    if (magic != 42) {
        return info;
    }
    const std::uint64_t ifd0_offset = read32(file, 4, big);

    const TiffIfd ifd0 = detail::parse_ifd(file, big, ifd0_offset, IfdMode::lenient, "IFD 0");
    info.make = text(ifd0, tag_make);
    info.model = text(ifd0, tag_model);
    if (const std::optional<std::uint32_t> o = integer(ifd0, tag_orientation);
        o && *o >= 1 && *o <= 8) {
        info.orientation = static_cast<int>(*o);
    }

    // IFD 0 -> Exif IFD only. A pointer back to IFD 0 would read it twice; stop instead.
    const std::optional<std::uint32_t> exif_offset = integer(ifd0, tag_exif_ifd);
    if (!exif_offset || *exif_offset == ifd0_offset) {
        return info;
    }
    // A bad pointer must not cost what IFD 0 already gave.
    TiffIfd exif;
    try {
        exif = detail::parse_ifd(file, big, *exif_offset, IfdMode::lenient, "Exif IFD");
    } catch (const std::exception&) {
        return info;
    }
    info.exposure_seconds = positive_number(exif, tag_exposure_time);
    info.f_number = positive_number(exif, tag_f_number);
    info.iso = positive_int(exif, tag_iso);
    info.date_taken = text(exif, tag_date_time_original);
    info.focal_length_mm = positive_number(exif, tag_focal_length);
    info.focal_length_35mm = positive_int(exif, tag_focal_length_35mm);
    info.lens = text(exif, tag_lens_model);
    return info;
}

}  // namespace

PhotoInfo read_exif(std::span<const std::uint8_t> tiff_bytes) noexcept {
    try {
        return read(std::as_bytes(tiff_bytes));
    } catch (...) {
        return {};
    }
}

}  // namespace prism

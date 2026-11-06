#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace prism {

// Basic facts about how a photo was taken, from its EXIF block (JPEG) or from the camera RAW
// decoder. Every field is optional: unknown is std::nullopt, never 0 or an empty string.
struct PhotoInfo {
    std::optional<std::string> make;        // "SONY"
    std::optional<std::string> model;       // "ILCE-7RM5"
    std::optional<std::string> lens;        // lens model, as the camera names it
    std::optional<std::string> date_taken;  // "YYYY:MM:DD HH:MM:SS", as EXIF writes it
    std::optional<int> iso;
    // The file's EXIF Orientation (1-8), already applied to the pixels; absent for RAW, where
    // LibRaw applies it.
    std::optional<int> orientation;
    std::optional<int> focal_length_35mm;
    std::optional<double> exposure_seconds;
    std::optional<double> f_number;
    std::optional<double> focal_length_mm;

    // True if no field is set.
    [[nodiscard]] bool empty() const noexcept {
        return !make && !model && !lens && !date_taken && !iso && !orientation &&
               !focal_length_35mm && !exposure_seconds && !f_number && !focal_length_mm;
    }
};

// Reads PhotoInfo from the TIFF structure of an EXIF block: the bytes that start with "II" or
// "MM", i.e. an APP1 payload without its "Exif\0\0" prefix. Reads IFD 0 and the Exif IFD it points
// to; nothing else (no thumbnail IFD, no MakerNote). Never throws: a damaged or truncated block
// gives whatever could be read, in the worst case an empty PhotoInfo. Text has trailing NULs and
// spaces removed; an Orientation outside 1-8 counts as absent.
[[nodiscard]] PhotoInfo read_exif(std::span<const std::uint8_t> tiff_bytes) noexcept;

}  // namespace prism

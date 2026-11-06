#pragma once

#include <filesystem>

#include "prism/image.hpp"

namespace prism {

// iPhone photos (.heic, .heif) are decoded by Apple's ImageIO, so they open on macOS only; on
// other systems load_heic_photo throws an error that says so. Only the standard (SDR) base image
// is read: HDR gain maps are ignored.

// True if the extension is .heic or .heif, case-insensitive. Only looks at the name; it says
// nothing about whether this build can read HEIC files.
[[nodiscard]] bool is_heic_file(const std::filesystem::path& path);

// True if this build can decode HEIC (macOS). If false, load_heic_photo always throws.
[[nodiscard]] bool heic_support_available() noexcept;

// Decodes the primary image of a HEIC/HEIF file (the one ImageIO names as primary), colour
// managed into linear sRGB with extended range (colours outside sRGB, e.g. from Display P3, stay
// as negative or above-1 values), turned upright exactly once, with PhotoInfo filled in from the
// file's metadata (never its GPS data).
// `threads` is accepted for symmetry with the other loaders; ImageIO does the decoding itself.
// Throws std::runtime_error naming the file if it can't be opened or decoded (a truncated or
// damaged file throws; it never comes back as a black picture), and on systems
// other than macOS: "HEIC photos can only be opened on macOS in this version of Prism."
[[nodiscard]] Photo load_heic_photo(const std::filesystem::path& path, int threads = 0);

}  // namespace prism

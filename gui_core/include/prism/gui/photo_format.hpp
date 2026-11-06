#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "prism/photo_info.hpp"

namespace prism::gui {

// How the status bar words what a photo says about how it was taken. All of these are pure and
// locale-independent; a missing or meaningless value gives an empty string.

// "70 mm"; one decimal under 10 mm ("6.8 mm"). When the 35 mm equivalent is known and differs from
// the focal length by more than 10%, " (24 mm equiv.)" follows.
[[nodiscard]] std::string format_focal(double mm, std::optional<int> mm35 = std::nullopt);

// "f/1.8", "f/2.8", "f/10": at most one decimal, never a trailing ".0".
[[nodiscard]] std::string format_aperture(double f_number);

// "1/250 s" when under a second and 1/t is within 3% of a whole number; otherwise seconds with at
// most one decimal and no trailing ".0" ("0.6 s", "1 s", "2.5 s", "30 s").
[[nodiscard]] std::string format_shutter(double seconds);

// "ISO 100".
[[nodiscard]] std::string format_iso(int iso);

// The camera model, focal length, aperture, shutter and ISO joined by " · ", skipping what is
// missing; empty when nothing is known. The make is left to camera_tooltip.
[[nodiscard]] std::string camera_line(const PhotoInfo& info);

// One line each for the make, model, lens and date taken ("Taken 2024:05:06 07:08:09"), skipping
// what is missing, joined by newlines.
[[nodiscard]] std::string camera_tooltip(const PhotoInfo& info);

// "RAW", "JPEG", "PNG" or "HEIC", from the file's extension.
[[nodiscard]] std::string file_kind_label(const std::filesystem::path& path);

}  // namespace prism::gui

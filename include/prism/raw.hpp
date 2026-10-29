#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

#include "prism/image.hpp"
#include "prism/pipeline.hpp"

namespace prism {

// Camera RAW files are decoded with LibRaw, which is optional at build time (PRISM_WITH_LIBRAW).
// The result is an ordinary linear-light Image, so the rest of the pipeline treats it like any
// other input. Unlike a PNG or JPEG it has no tone curve baked in, so it looks darker and flatter
// than the camera's own JPEG of the same shot; that is expected, and what --curve is for.

struct RawOptions {
    // LibRaw's half-size mode: each 2x2 block of sensor pixels becomes one RGB pixel, so no
    // demosaicing is needed. About half the width and height and several times faster; good for
    // previews and quick checks.
    bool half_size = false;

    // Unset (the default): LibRaw converts the camera's colours to linear sRGB primaries with its
    // own matrix for the camera model.
    // Set: LibRaw outputs the camera's native RGB (white-balanced, no colour conversion) and this
    // matrix converts it to linear sRGB instead, e.g. one from your own calibration. Like
    // --matrix it is row-major and applied with apply_matrix; see README for what it must map.
    std::optional<Matrix3> camera_matrix;
};

// True if the extension is one of the RAW formats Prism hands to LibRaw: .arw (Sony), .dng,
// .cr2 / .cr3 (Canon), .nef (Nikon) or .raf (Fujifilm). Case-insensitive. This only looks at the
// name; it says nothing about whether this build can actually read RAW files.
[[nodiscard]] bool is_raw_file(const std::filesystem::path& path);

// True if Prism was built with LibRaw. If false, load_raw always throws.
[[nodiscard]] bool raw_support_available() noexcept;

// One 16-bit sample (0-65535) to float in [0, 1]. The value is already linear; this only rescales.
[[nodiscard]] constexpr float raw16_to_float(std::uint16_t value) noexcept {
    return static_cast<float>(value) / 65535.0f;
}

// Converts interleaved linear 16-bit RGB samples (width * height * 3 of them) to an Image with
// raw16_to_float. Throws std::invalid_argument if samples.size() does not match the dimensions.
[[nodiscard]] Image decode_rgb16(std::span<const std::uint16_t> samples, int width, int height,
                                 int threads = 0);

// Decodes a camera RAW file to a linear Image (in sRGB primaries, or via options.camera_matrix),
// with the camera's as-shot white balance and the orientation stored in the file applied.
// Throws std::runtime_error if the file cannot be read, or if Prism was built without LibRaw.
// LibRaw's own decoding is not split across `threads`; only the 16-bit -> float conversion and
// the optional camera matrix are.
[[nodiscard]] Image load_raw(const std::filesystem::path& path, const RawOptions& options = {},
                             int threads = 0);

}  // namespace prism

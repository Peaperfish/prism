#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace prism {

// An RGB image in linear light, 32-bit float per channel.
// Pixels are interleaved and row-major: pixel (x, y) starts at index (y * width + x) * channels,
// followed by its R, G and B values. pixels.size() must equal width * height * channels.
struct Image {
    static constexpr int channels = 3;

    int width = 0;
    int height = 0;
    std::vector<float> pixels;
};

// Every function taking `int threads` splits the work across that many threads; 0 (the default)
// means one per hardware thread, 1 means single-threaded. See prism/parallel.hpp.

// Converts interleaved 8-bit sRGB RGB bytes (width * height * 3 of them) to a linear Image.
// Throws std::invalid_argument if bytes.size() does not match the dimensions.
[[nodiscard]] Image decode_srgb8(std::span<const std::uint8_t> bytes, int width, int height,
                                 int threads = 0);

// Converts a linear Image to 8-bit sRGB bytes (clamped, rounded): exactly what save_image writes.
// Throws std::invalid_argument if pixels.size() does not match width * height * 3.
[[nodiscard]] std::vector<std::uint8_t> encode_srgb8(const Image& img, int threads = 0);

// Loads an 8-bit PNG or JPEG (anything stb_image reads) and converts sRGB to linear float.
// Grey or alpha images are expanded/flattened to RGB. Throws std::runtime_error on failure.
// Decoding the file itself (stb_image) is single-threaded; only the sRGB conversion uses `threads`.
[[nodiscard]] Image load_image(const std::filesystem::path& path, int threads = 0);

// Writes already-encoded 8-bit sRGB bytes (e.g. from encode_srgb8) as PNG or JPEG, chosen by the
// file extension. Lets a caller that also needs the bytes for something else (like a histogram)
// encode only once. Throws std::runtime_error on failure, as save_image does.
void save_srgb8(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, int width,
                int height);

// Converts linear to 8-bit sRGB (clamped, rounded) and writes PNG or JPEG, chosen by the
// file extension (.png, .jpg, .jpeg; case-insensitive). Throws std::runtime_error on failure.
// As with loading, only the sRGB conversion uses `threads`; PNG/JPEG compression stays
// single-threaded.
void save_image(const std::filesystem::path& path, const Image& img, int threads = 0);

}  // namespace prism

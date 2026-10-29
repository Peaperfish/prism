#pragma once

#include <filesystem>
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

// Loads an 8-bit PNG or JPEG (anything stb_image reads) and converts sRGB to linear float.
// Grey or alpha images are expanded/flattened to RGB. Throws std::runtime_error on failure.
[[nodiscard]] Image load_image(const std::filesystem::path& path);

// Converts linear to 8-bit sRGB (clamped, rounded) and writes PNG or JPEG, chosen by the
// file extension (.png, .jpg, .jpeg; case-insensitive). Throws std::runtime_error on failure.
void save_image(const std::filesystem::path& path, const Image& img);

}  // namespace prism

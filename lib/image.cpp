#include "prism/image.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include <stb_image.h>
#include <stb_image_write.h>

#include "prism/color.hpp"

namespace prism {

namespace {

// Quality 95 keeps JPEG artefacts hard to see while still compressing well.
constexpr int jpeg_quality = 95;

// stbi_load returns a malloc'd buffer that must be released with stbi_image_free.
// Holding it in a unique_ptr frees it automatically, even if we throw part-way through.
struct StbiDeleter {
    void operator()(unsigned char* p) const noexcept { stbi_image_free(p); }
};
using StbiPixels = std::unique_ptr<unsigned char, StbiDeleter>;

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::size_t sample_count(int width, int height) {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * Image::channels;
}

}  // namespace

Image load_image(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    // Passing Image::channels asks stb to convert whatever is in the file (grey, RGBA, ...) to RGB.
    StbiPixels data(stbi_load(path.string().c_str(), &width, &height, &channels_in_file,
                              Image::channels));
    if (!data) {
        throw std::runtime_error("cannot load image '" + path.string() +
                                 "': " + stbi_failure_reason());
    }

    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(sample_count(width, height));
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        img.pixels[i] = srgb8_to_linear(data.get()[i]);
    }
    return img;
}

void save_image(const std::filesystem::path& path, const Image& img) {
    if (img.width <= 0 || img.height <= 0) {
        throw std::runtime_error("cannot save image '" + path.string() + "': image is empty");
    }
    if (img.pixels.size() != sample_count(img.width, img.height)) {
        throw std::runtime_error("cannot save image '" + path.string() +
                                 "': pixel buffer size does not match width * height * 3");
    }

    // Decide the format before doing any work, so a typo fails fast.
    const std::string ext = lowercase_extension(path);
    const bool is_png = ext == ".png";
    const bool is_jpeg = ext == ".jpg" || ext == ".jpeg";
    if (!is_png && !is_jpeg) {
        throw std::runtime_error("cannot save image '" + path.string() +
                                 "': unsupported extension (use .png, .jpg or .jpeg)");
    }

    std::vector<std::uint8_t> bytes(img.pixels.size());
    std::transform(img.pixels.begin(), img.pixels.end(), bytes.begin(), linear_to_srgb8);

    const std::string filename = path.string();
    const int stride = img.width * Image::channels;  // bytes from one row to the next
    // stb_image_write returns 0 on failure and does not say why (usually a bad path or permissions).
    const int ok = is_png ? stbi_write_png(filename.c_str(), img.width, img.height,
                                           Image::channels, bytes.data(), stride)
                          : stbi_write_jpg(filename.c_str(), img.width, img.height,
                                           Image::channels, bytes.data(), jpeg_quality);
    if (ok == 0) {
        throw std::runtime_error("cannot save image '" + filename +
                                 "': write failed (check the directory exists and is writable)");
    }
}

}  // namespace prism

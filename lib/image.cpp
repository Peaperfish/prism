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
#include "sample_ranges.hpp"

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

using detail::sample_count;

}  // namespace

Image decode_srgb8(std::span<const std::uint8_t> bytes, int width, int height, int threads) {
    if (width < 0 || height < 0 || bytes.size() != sample_count(width, height)) {
        throw std::invalid_argument("decode_srgb8: byte count does not match width * height * 3");
    }
    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(bytes.size());
    // Each thread reads and writes only its own slice [first, last), so threads never touch the
    // same element and no locking is needed.
    detail::parallel_for_samples(width, height, threads, [&](std::size_t first, std::size_t last) {
        for (std::size_t i = first; i < last; ++i) {
            img.pixels[i] = srgb8_to_linear(bytes[i]);
        }
    });
    return img;
}

std::vector<std::uint8_t> encode_srgb8(const Image& img, int threads) {
    detail::require_consistent(img, "encode_srgb8");
    std::vector<std::uint8_t> bytes(img.pixels.size());
    detail::parallel_for_samples(img.width, img.height, threads,
                                 [&](std::size_t first, std::size_t last) {
                                     std::transform(img.pixels.begin() + first,
                                                    img.pixels.begin() + last,
                                                    bytes.begin() + first, linear_to_srgb8);
                                 });
    return bytes;
}

Image load_image(const std::filesystem::path& path, int threads) {
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

    return decode_srgb8({data.get(), sample_count(width, height)}, width, height, threads);
}

void save_image(const std::filesystem::path& path, const Image& img, int threads) {
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

    const std::vector<std::uint8_t> bytes = encode_srgb8(img, threads);

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

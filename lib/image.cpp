#include "prism/image.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

#include "file_ext.hpp"
#include "prism/color.hpp"
#include "prism/raw.hpp"
#include "prism/utf8_path.hpp"
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

using detail::lowercase_extension;
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
    detail::parallel_for_samples(
        img.width, img.height, threads, [&](std::size_t first, std::size_t last) {
            linear_to_srgb8(img.pixels.data() + first, bytes.data() + first, last - first);
        });
    return bytes;
}

Image load_image(const std::filesystem::path& path, int threads) {
    return load_photo(path, RawOptions{}, threads).image;
}

Image load_image(const std::filesystem::path& path, const RawOptions& raw, int threads) {
    return load_photo(path, raw, threads).image;
}

Photo load_photo(const std::filesystem::path& path, int threads) {
    return load_photo(path, RawOptions{}, threads);
}

Photo load_photo(const std::filesystem::path& path, const RawOptions& raw, int threads) {
    if (is_raw_file(path)) {
        return load_raw_photo(path, raw, threads);
    }

    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    // Passing Image::channels asks stb to convert whatever is in the file (grey, RGBA, ...) to RGB.
    // stb takes the file name as UTF-8 (see STBI_WINDOWS_UTF8 in stb_impl.cpp).
    StbiPixels data(
        stbi_load(utf8(path).c_str(), &width, &height, &channels_in_file, Image::channels));
    if (!data) {
        throw std::runtime_error("cannot load image '" + utf8(path) +
                                 "': " + stbi_failure_reason());
    }

    Photo photo;
    photo.image = decode_srgb8({data.get(), sample_count(width, height)}, width, height, threads);

    // Only JPEGs carry EXIF (by extension, like the rest of the file-type decisions). Reading it
    // and applying it cannot fail the load: the readers never throw, and without an Orientation
    // the image is left as stored.
    const std::string ext = lowercase_extension(path);
    if (ext == ".jpg" || ext == ".jpeg") {
        if (const auto exif = read_jpeg_exif(path)) {
            photo.info = read_exif(*exif);
        }
        if (photo.info.orientation) {
            apply_orientation(photo.image, *photo.info.orientation);
        }
    }
    return photo;
}

void apply_orientation(Image& img, int orientation) {
    detail::require_consistent(img, "apply_orientation");
    if (orientation < 2 || orientation > 8 || img.width == 0 || img.height == 0) {
        return;
    }
    const int w = img.width;
    const int h = img.height;
    constexpr std::size_t ch = Image::channels;
    const bool turned = orientation >= 5;
    const int out_w = turned ? h : w;
    const int out_h = turned ? w : h;

    // `source(x, y)` is where output pixel (x, y) comes from in the input: the table in the header.
    const auto fill = [&](auto source) {
        std::vector<float> out(img.pixels.size());
        const float* in = img.pixels.data();
        float* dst = out.data();
        for (int y = 0; y < out_h; ++y) {
            for (int x = 0; x < out_w; ++x) {
                const auto [sx, sy] = source(x, y);
                const float* px = in + (static_cast<std::size_t>(sy) * w + sx) * ch;
                dst[0] = px[0];
                dst[1] = px[1];
                dst[2] = px[2];
                dst += ch;
            }
        }
        img.pixels = std::move(out);
    };
    using Pos = std::pair<int, int>;
    switch (orientation) {
        case 2: fill([&](int x, int y) { return Pos{w - 1 - x, y}; }); break;
        case 3: fill([&](int x, int y) { return Pos{w - 1 - x, h - 1 - y}; }); break;
        case 4: fill([&](int x, int y) { return Pos{x, h - 1 - y}; }); break;
        case 5: fill([&](int x, int y) { return Pos{y, x}; }); break;
        case 6: fill([&](int x, int y) { return Pos{y, h - 1 - x}; }); break;
        case 7: fill([&](int x, int y) { return Pos{w - 1 - y, h - 1 - x}; }); break;
        default: fill([&](int x, int y) { return Pos{w - 1 - y, x}; }); break;  // 8
    }
    img.width = out_w;
    img.height = out_h;
}

void save_srgb8(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, int width,
                int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("cannot save image '" + utf8(path) + "': image is empty");
    }
    if (bytes.size() != sample_count(width, height)) {
        throw std::runtime_error("cannot save image '" + utf8(path) +
                                 "': byte count does not match width * height * 3");
    }

    const std::string ext = lowercase_extension(path);
    const bool is_png = ext == ".png";
    const bool is_jpeg = ext == ".jpg" || ext == ".jpeg";
    if (!is_png && !is_jpeg) {
        throw std::runtime_error("cannot save image '" + utf8(path) +
                                 "': unsupported extension (use .png, .jpg or .jpeg)");
    }

    const std::string filename = utf8(path);     // stb takes UTF-8, as in load_image
    const int stride = width * Image::channels;  // bytes from one row to the next
    // stb_image_write returns 0 on failure and does not say why (usually a bad path or
    // permissions).
    const int ok = is_png ? stbi_write_png(filename.c_str(), width, height, Image::channels,
                                           bytes.data(), stride)
                          : stbi_write_jpg(filename.c_str(), width, height, Image::channels,
                                           bytes.data(), jpeg_quality);
    if (ok == 0) {
        throw std::runtime_error("cannot save image '" + filename +
                                 "': write failed (check the directory exists and is writable)");
    }
}

void save_image(const std::filesystem::path& path, const Image& img, int threads) {
    // Check the file name and image shape before the (slow) encode, so mistakes fail fast.
    if (img.width <= 0 || img.height <= 0) {
        throw std::runtime_error("cannot save image '" + utf8(path) + "': image is empty");
    }
    if (img.pixels.size() != sample_count(img.width, img.height)) {
        throw std::runtime_error("cannot save image '" + utf8(path) +
                                 "': pixel buffer size does not match width * height * 3");
    }
    const std::string ext = lowercase_extension(path);
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") {
        throw std::runtime_error("cannot save image '" + utf8(path) +
                                 "': unsupported extension (use .png, .jpg or .jpeg)");
    }
    save_srgb8(path, encode_srgb8(img, threads), img.width, img.height);
}

}  // namespace prism

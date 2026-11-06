#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "prism/photo_info.hpp"

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

// A decoded photo and what is known about how it was taken.
struct Photo {
    Image image;
    PhotoInfo info;
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
// A JPEG comes out upright: its EXIF Orientation is applied (apply_orientation), so a photo taken
// with the camera on its side is not sideways, and 5-8 swap its width and height. Metadata
// problems never fail a load: the photo then loads as stored.
// Grey or alpha images are expanded/flattened to RGB. Throws std::runtime_error on failure.
// Decoding the file itself (stb_image) is single-threaded; only the sRGB conversion uses `threads`.
// Camera RAW files (see is_raw_file in prism/raw.hpp) go to load_raw with default RawOptions, and
// .heic / .heif files to load_heic_photo (prism/heic.hpp), which only works on macOS.
[[nodiscard]] Image load_image(const std::filesystem::path& path, int threads = 0);

// As above, passing `raw` on to load_raw for RAW files; ignored for PNG and JPEG.
// (Declared here, defined in prism/raw.hpp: include that to create a RawOptions.)
struct RawOptions;
[[nodiscard]] Image load_image(const std::filesystem::path& path, const RawOptions& raw,
                               int threads = 0);

// As load_image, and also returns what the file says about the shot (see PhotoInfo): from the
// EXIF block of a JPEG, or from the RAW decoder for a RAW file (whose orientation is applied by
// LibRaw, so `info.orientation` is not set). A PNG, or a file with no usable metadata, gives an
// empty PhotoInfo. Throws std::runtime_error exactly when load_image does.
[[nodiscard]] Photo load_photo(const std::filesystem::path& path, const RawOptions& raw,
                               int threads = 0);
[[nodiscard]] Photo load_photo(const std::filesystem::path& path, int threads = 0);

// Turns `img` to display upright, given an EXIF Orientation value (1-8), in place. With W x H the
// size of `img` and in(x, y) its pixels, the new pixel (x, y) is:
//   1: in(x, y)          2: in(W-1-x, y)      3: in(W-1-x, H-1-y)   4: in(x, H-1-y)
//   5: in(y, x)          6: in(y, H-1-x)      7: in(W-1-y, H-1-x)   8: in(W-1-y, x)
// i.e. 2 mirrors left-right, 3 turns 180 degrees, 4 mirrors top-bottom, 5 transposes, 6 turns 90
// degrees clockwise, 7 transverses and 8 turns 90 degrees counter-clockwise. For 5-8 the result is
// H wide and W tall. 1, and any value outside 1-8, leave the image as it is.
// Throws std::invalid_argument if pixels.size() does not match width * height * 3.
void apply_orientation(Image& img, int orientation);

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

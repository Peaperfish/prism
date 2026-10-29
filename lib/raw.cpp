#include "prism/raw.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef PRISM_HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

#include "file_ext.hpp"
#include "sample_ranges.hpp"

namespace prism {

bool is_raw_file(const std::filesystem::path& path) {
    constexpr std::array<std::string_view, 6> raw_extensions = {".arw", ".dng", ".cr2",
                                                                ".cr3", ".nef", ".raf"};
    const std::string ext = detail::lowercase_extension(path);
    return std::find(raw_extensions.begin(), raw_extensions.end(), ext) != raw_extensions.end();
}

bool raw_support_available() noexcept {
#ifdef PRISM_HAVE_LIBRAW
    return true;
#else
    return false;
#endif
}

Image decode_rgb16(std::span<const std::uint16_t> samples, int width, int height, int threads) {
    if (width < 0 || height < 0 || samples.size() != detail::sample_count(width, height)) {
        throw std::invalid_argument("decode_rgb16: sample count does not match width * height * 3");
    }
    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(samples.size());
    detail::parallel_for_samples(width, height, threads, [&](std::size_t first, std::size_t last) {
        for (std::size_t i = first; i < last; ++i) {
            img.pixels[i] = raw16_to_float(samples[i]);
        }
    });
    return img;
}

#ifdef PRISM_HAVE_LIBRAW

namespace {

[[noreturn]] void raw_error(const std::filesystem::path& path, const std::string& why) {
    throw std::runtime_error("cannot load RAW file '" + path.string() + "': " + why);
}

// LibRaw calls return 0 (LIBRAW_SUCCESS) or an error code that libraw_strerror describes.
void check(int code, const std::filesystem::path& path, const char* step) {
    if (code != LIBRAW_SUCCESS) {
        raw_error(path, std::string(step) + " failed: " + libraw_strerror(code));
    }
}

// dcraw_make_mem_image returns a buffer LibRaw allocated; it must go back via dcraw_clear_mem.
struct MemImageDeleter {
    void operator()(libraw_processed_image_t* p) const noexcept { LibRaw::dcraw_clear_mem(p); }
};
using MemImage = std::unique_ptr<libraw_processed_image_t, MemImageDeleter>;

}  // namespace

Image load_raw(const std::filesystem::path& path, const RawOptions& options, int threads) {
    // A RAW file holds one value per photosite, each behind a red, green or blue filter (the Bayer
    // pattern), proportional to the light that hit it. LibRaw's processing turns that into RGB:
    //   unpack        decompress the sensor values
    //   dcraw_process subtract the black level, scale to white, apply white balance, demosaic
    //                 (estimate the two missing colours at each photosite), convert colour space
    //   make_mem_image  rotate to the stored orientation and hand us interleaved RGB
    // The settings below make every one of those steps keep the values linear.
    //
    // The LibRaw object holds all of the decoder's state, including large fixed arrays, so it
    // lives on the heap rather than the stack.
    auto raw = std::make_unique<LibRaw>();
    libraw_output_params_t& p = raw->imgdata.params;

    // Output transfer curve: gamm[0] is the inverse gamma (power) and gamm[1] the slope of the
    // linear toe. LibRaw's default is BT.709's curve (0.45, 4.5), which brightens the image like
    // a PNG/JPEG. 1/1 means "no curve": output stays proportional to light, which exposure and
    // matrices need. encode_srgb8 applies the sRGB curve at the very end instead.
    p.gamm[0] = 1.0;
    p.gamm[1] = 1.0;

    // By default LibRaw brightens each image so its brightest 1% of pixels reach white. That
    // changes from shot to shot and would make --exposure meaningless; with it off, sensor
    // saturation (the white level) maps to exactly 65535, i.e. 1.0.
    p.no_auto_bright = 1;

    // 16 bits per sample instead of 8. Linear data puts most of the picture in small values
    // (middle grey is ~0.18), so 8 bits would leave visible banding in the shadows.
    p.output_bps = 16;

    // White balance as shot: the multipliers the camera stored for the light it measured or was
    // set to. Auto WB (LibRaw guessing from the image) stays off.
    p.use_camera_wb = 1;
    p.use_auto_wb = 0;

    // Colour: output_color 1 = sRGB primaries (Rec. 709, D65 white). With gamma 1/1 this is
    // exactly "linear sRGB", what Image holds. LibRaw gets there with a camera -> sRGB matrix for
    // this model: for a DNG, the matrix embedded in the file (use_camera_matrix = 1 uses it when
    // the camera's white balance is used, as here); for ARW, CR3, NEF and the rest, the files
    // don't carry one, so LibRaw uses its built-in table of matrices per camera model, derived
    // from Adobe's colour data (the α7R V is "ILCE-7RM5").
    //
    // With a camera_matrix of our own we ask for output_color 0 instead: "raw colour", the
    // camera's native RGB after white balance and demosaicing, with no conversion. Our matrix
    // then does the conversion, in float and without the clipping to [0, 65535] that LibRaw's own
    // conversion step applies.
    p.use_camera_matrix = 1;
    p.output_color = options.camera_matrix ? 0 : 1;

    // Half-size: skip demosaicing by treating each 2x2 Bayer block (R, G, G, B) as one RGB pixel.
    p.half_size = options.half_size ? 1 : 0;

    // -1 = use the orientation stored in the file (the camera's tilt sensor), so portrait shots
    // come out upright. dcraw_make_mem_image applies it, swapping width and height when needed.
    p.user_flip = -1;

    // Highlights: 0 = clip any channel at the white level. Clipped areas stay neutral white
    // instead of the magenta casts unclipped highlights can produce after white balance.
    p.highlight = 0;

    // LibRaw has a wchar_t overload for Windows paths; char works on macOS and Linux.
    check(raw->open_file(path.string().c_str()), path, "open");
    check(raw->unpack(), path, "unpack");
    check(raw->dcraw_process(), path, "processing");

    int error = LIBRAW_SUCCESS;
    const MemImage mem(raw->dcraw_make_mem_image(&error));
    if (!mem) {
        check(error != LIBRAW_SUCCESS ? error : LIBRAW_UNSPECIFIED_ERROR, path, "output");
    }
    if (mem->type != LIBRAW_IMAGE_BITMAP || mem->colors != Image::channels || mem->bits != 16) {
        raw_error(path, "unexpected output from LibRaw (" + std::to_string(mem->colors) +
                            " colours, " + std::to_string(mem->bits) + " bits)");
    }
    const int width = mem->width;
    const int height = mem->height;
    const std::size_t samples = detail::sample_count(width, height);
    if (mem->data_size != samples * sizeof(std::uint16_t)) {
        raw_error(path, "LibRaw output size does not match its dimensions");
    }

    // The 16-bit RGB buffer is no longer needed inside LibRaw: free its working copy (several
    // hundred MB for a 61 MP file) before allocating the float image. `mem` is a separate buffer.
    raw->recycle();

    // LibRaw wrote `data` as unsigned shorts in native byte order (only its PPM/TIFF writers
    // byte-swap), so reading it back as uint16_t is what it intends.
    const auto* rgb16 = reinterpret_cast<const std::uint16_t*>(mem->data);
    Image img = decode_rgb16({rgb16, samples}, width, height, threads);
    if (options.camera_matrix) {
        apply_matrix(img, *options.camera_matrix, threads);
    }
    return img;
}

#else  // !PRISM_HAVE_LIBRAW

Image load_raw(const std::filesystem::path& path, const RawOptions&, int) {
    throw std::runtime_error("cannot load RAW file '" + path.string() +
                             "': Prism was built without LibRaw (install it, e.g. `brew install "
                             "libraw`, then reconfigure with -DPRISM_WITH_LIBRAW=ON)");
}

#endif

}  // namespace prism

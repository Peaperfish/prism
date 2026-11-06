#include "prism/heic.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "file_ext.hpp"
#include "prism/image.hpp"
#include "prism/utf8_path.hpp"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cmath>
#include <limits>
#include <optional>
#endif

namespace prism {

bool is_heic_file(const std::filesystem::path& path) {
    const std::string ext = detail::lowercase_extension(path);
    return ext == ".heic" || ext == ".heif";
}

#ifdef __APPLE__

namespace {

// Owns one CoreFoundation object and releases it.
template <typename T>
class CfRef {
public:
    CfRef() = default;
    explicit CfRef(T ref) noexcept : ref_(ref) {}
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
    CfRef(CfRef&& o) noexcept : ref_(o.ref_) { o.ref_ = nullptr; }
    CfRef& operator=(CfRef&& o) noexcept {
        if (this != &o) {
            reset();
            ref_ = o.ref_;
            o.ref_ = nullptr;
        }
        return *this;
    }
    ~CfRef() { reset(); }
    [[nodiscard]] T get() const noexcept { return ref_; }
    explicit operator bool() const noexcept { return ref_ != nullptr; }
    // Releases the object now instead of at the end of the scope.
    void reset() noexcept {
        if (ref_ != nullptr) {
            CFRelease(ref_);
        }
        ref_ = nullptr;
    }

private:
    T ref_ = nullptr;
};

// --- Property readers. Each returns nullopt for a missing key, a value of the wrong type, or a
// --- zero/negative number, like read_exif. Whole numbers are rounded, and one that rounds to 0
// --- counts as missing too.

CFDictionaryRef dictionary_in(CFDictionaryRef parent, CFStringRef key) {
    if (parent == nullptr) {
        return nullptr;
    }
    const void* v = CFDictionaryGetValue(parent, key);
    return (v != nullptr && CFGetTypeID(v) == CFDictionaryGetTypeID())
               ? static_cast<CFDictionaryRef>(v)
               : nullptr;
}

std::optional<std::string> text_in(CFDictionaryRef dict, CFStringRef key) {
    if (dict == nullptr) {
        return std::nullopt;
    }
    const void* v = CFDictionaryGetValue(dict, key);
    if (v == nullptr || CFGetTypeID(v) != CFStringGetTypeID()) {
        return std::nullopt;
    }
    const auto s = static_cast<CFStringRef>(v);
    const CFIndex max =
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
    std::vector<char> buffer(static_cast<std::size_t>(max));
    if (!CFStringGetCString(s, buffer.data(), max, kCFStringEncodingUTF8)) {
        return std::nullopt;
    }
    // Leading and trailing spaces are removed; text that is then empty counts as missing.
    std::string text(buffer.data());
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    std::size_t first = 0;
    while (first < text.size() && text[first] == ' ') {
        ++first;
    }
    text.erase(0, first);
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

std::optional<double> number_of(const void* v) {
    if (v == nullptr || CFGetTypeID(v) != CFNumberGetTypeID()) {
        return std::nullopt;
    }
    double d = 0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(v), kCFNumberDoubleType, &d)) {
        return std::nullopt;
    }
    return d;
}

std::optional<double> positive_number_in(CFDictionaryRef dict, CFStringRef key) {
    if (dict == nullptr) {
        return std::nullopt;
    }
    const std::optional<double> d = number_of(CFDictionaryGetValue(dict, key));
    if (!d || !(*d > 0.0) || *d > 1e12) {
        return std::nullopt;
    }
    return d;
}

// `d` rounded to a whole number, if that is at least 1 and fits an int.
std::optional<int> rounded_positive_int(double d) {
    const double rounded = std::floor(d + 0.5);
    if (!(rounded >= 1.0) || rounded > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(rounded);
}

std::optional<int> positive_int_in(CFDictionaryRef dict, CFStringRef key) {
    const std::optional<double> d = positive_number_in(dict, key);
    return d ? rounded_positive_int(*d) : std::nullopt;
}

// ISOSpeedRatings is an array of numbers: the first element.
std::optional<int> iso_in(CFDictionaryRef exif) {
    if (exif == nullptr) {
        return std::nullopt;
    }
    const void* v = CFDictionaryGetValue(exif, kCGImagePropertyExifISOSpeedRatings);
    if (v == nullptr || CFGetTypeID(v) != CFArrayGetTypeID()) {
        return std::nullopt;
    }
    const auto array = static_cast<CFArrayRef>(v);
    if (CFArrayGetCount(array) < 1) {
        return std::nullopt;
    }
    const std::optional<double> d = number_of(CFArrayGetValueAtIndex(array, 0));
    return d ? rounded_positive_int(*d) : std::nullopt;
}

std::optional<int> orientation_in(CFDictionaryRef dict, CFStringRef key) {
    const std::optional<int> o = positive_int_in(dict, key);
    if (!o || *o > 8) {
        return std::nullopt;
    }
    return o;
}

// Make, model and the shot's settings; deliberately nothing else (no GPS dictionary is touched).
PhotoInfo info_from(CFDictionaryRef properties) {
    PhotoInfo info;
    const CFDictionaryRef tiff = dictionary_in(properties, kCGImagePropertyTIFFDictionary);
    const CFDictionaryRef exif = dictionary_in(properties, kCGImagePropertyExifDictionary);
    info.make = text_in(tiff, kCGImagePropertyTIFFMake);
    info.model = text_in(tiff, kCGImagePropertyTIFFModel);
    info.lens = text_in(exif, kCGImagePropertyExifLensModel);
    info.date_taken = text_in(exif, kCGImagePropertyExifDateTimeOriginal);
    info.iso = iso_in(exif);
    info.focal_length_35mm = positive_int_in(exif, kCGImagePropertyExifFocalLenIn35mmFilm);
    info.exposure_seconds = positive_number_in(exif, kCGImagePropertyExifExposureTime);
    info.f_number = positive_number_in(exif, kCGImagePropertyExifFNumber);
    info.focal_length_mm = positive_number_in(exif, kCGImagePropertyExifFocalLength);
    return info;
}

}  // namespace

bool heic_support_available() noexcept {
    return true;
}

Photo load_heic_photo(const std::filesystem::path& path, int /*threads*/) {
    const std::string name = utf8(path);
    const auto fail = [&](const std::string& why) {
        return std::runtime_error("cannot load HEIC file '" + name + "': " + why);
    };

    const std::string native = path.string();  // the file system's own bytes (UTF-8 on macOS)
    CfRef<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(native.c_str()),
        static_cast<CFIndex>(native.size()), false));
    if (!url) {
        throw fail("bad file name");
    }
    CfRef<CGImageSourceRef> source(CGImageSourceCreateWithURL(url.get(), nullptr));
    if (!source) {
        throw fail("cannot open the file (missing, unreadable or not an image)");
    }
    if (CGImageSourceGetCount(source.get()) < 1) {
        throw fail("the file contains no image");
    }
    // The image to show is the primary one (not necessarily the first). ShouldCacheImmediately
    // makes ImageIO decode it now, so a damaged file fails here and not half way through the draw
    // below. No kCGImageSourceDecodeToHDR: only the SDR base image is read.
    const std::size_t index = CGImageSourceGetPrimaryImageIndex(source.get());
    const void* option_keys[] = {kCGImageSourceShouldCacheImmediately};
    const void* option_values[] = {kCFBooleanTrue};
    CfRef<CFDictionaryRef> options(
        CFDictionaryCreate(kCFAllocatorDefault, option_keys, option_values, 1,
                           &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    CfRef<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.get(), index, options.get()));
    if (!image || CGImageSourceGetStatusAtIndex(source.get(), index) != kCGImageStatusComplete) {
        throw fail("cannot decode the image");
    }
    const CGImageAlphaInfo alpha_info = CGImageGetAlphaInfo(image.get());
    const bool has_alpha =
        !(alpha_info == kCGImageAlphaNone || alpha_info == kCGImageAlphaNoneSkipFirst ||
          alpha_info == kCGImageAlphaNoneSkipLast);

    Photo photo;
    CfRef<CFDictionaryRef> properties(
        CGImageSourceCopyPropertiesAtIndex(source.get(), index, nullptr));
    photo.info = info_from(properties.get());

    const std::size_t width = CGImageGetWidth(image.get());
    const std::size_t height = CGImageGetHeight(image.get());
    if (width == 0 || height == 0 || width > 1u << 28 || height > 1u << 28 ||
        width * height > (std::size_t{1} << 31) / 16) {
        throw fail("the image has an unsupported size");
    }

    // Render into an RGBA float bitmap in extended linear sRGB: CoreGraphics converts from the
    // file's colour space (Display P3 on an iPhone), and the extended range keeps colours outside
    // sRGB as negative or above-1 values. Row 0 of the bitmap is the top of the picture.
    CfRef<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB));
    if (!space) {
        throw fail("no linear sRGB colour space");
    }
    const std::size_t pixel_count = width * height;
    std::vector<float> pixels(pixel_count * 4);
    {
        CfRef<CGContextRef> context(CGBitmapContextCreate(
            pixels.data(), width, height, 32, width * 4 * sizeof(float), space.get(),
            static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapFloatComponents |
                kCGBitmapByteOrder32Host));
        if (!context) {
            throw fail("cannot create a bitmap to decode into");
        }
        CGContextSetInterpolationQuality(context.get(), kCGInterpolationNone);
        CGContextSetBlendMode(context.get(), kCGBlendModeCopy);
        CGContextDrawImage(context.get(), CGRectMake(0, 0, static_cast<CGFloat>(width),
                                                     static_cast<CGFloat>(height)),
                           image.get());
    }
    // The picture is in the bitmap: the decoded image and the file are not needed any more.
    image.reset();
    source.reset();

    // RGBA to RGB in the same vector (pixel i's RGB lands at or before its RGBA, so nothing that
    // is still to be read is overwritten). The bitmap started out all zero, so a pixel with alpha 0
    // is one that was never drawn: a damaged file whose decode stopped early. A picture without an
    // alpha channel is drawn fully opaque everywhere, so one such pixel is enough to reject it; one
    // with alpha is rejected only if it is transparent everywhere.
    std::size_t transparent = 0;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const float a = pixels[i * 4 + 3];
        if (a == 0.0f) {
            ++transparent;
        }
        // An opaque photo has alpha 1; undo the premultiplication anyway if it isn't.
        const float scale = (a > 0.0f && a < 1.0f) ? 1.0f / a : 1.0f;
        for (std::size_t c = 0; c < 3; ++c) {
            pixels[i * 3 + c] = pixels[i * 4 + c] * scale;
        }
    }
    if (has_alpha ? transparent == pixel_count : transparent > 0) {
        throw fail("cannot decode the image");
    }
    // Moved, not shrunk: shrink_to_fit would allocate the smaller buffer next to this one, which is
    // the RGBA-plus-RGB pair this conversion in place avoids.
    pixels.resize(pixel_count * 3);
    photo.image.width = static_cast<int>(width);
    photo.image.height = static_cast<int>(height);
    photo.image.pixels = std::move(pixels);

    // CreateImageAtIndex returns the pixels as stored (an iPhone portrait comes out landscape),
    // so the turn is applied here, once, from ImageIO's top-level Orientation. Observed on
    // macOS 27, not documented by Apple: that value follows the HEIF irot/imir boxes and ignores
    // the EXIF Orientation tag, so a HEIC whose rotation is only in EXIF opens unrotated.
    if (const std::optional<int> o =
            orientation_in(properties.get(), kCGImagePropertyOrientation)) {
        apply_orientation(photo.image, *o);
        photo.info.orientation = *o;
    }
    return photo;
}

#else  // !__APPLE__

bool heic_support_available() noexcept {
    return false;
}

Photo load_heic_photo(const std::filesystem::path& path, int) {
    throw std::runtime_error("cannot load HEIC file '" + utf8(path) +
                             "': HEIC photos can only be opened on macOS in this version of "
                             "Prism.");
}

#endif

}  // namespace prism

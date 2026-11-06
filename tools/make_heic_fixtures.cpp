// Writes the HEIC test fixtures: the four-quadrant picture of tests/synthetic_exif.hpp (64 x 32,
// top-left red, top-right green, bottom-left blue, bottom-right yellow, 32 x 16 blocks) stored
// with Orientation 1 to 8, and one more with camera metadata. macOS only, and not part of the
// normal build. Run from the repository root:
//
//   clang++ -std=c++20 tools/make_heic_fixtures.cpp -o /tmp/make_heic_fixtures \
//       -framework ImageIO -framework CoreGraphics -framework CoreFoundation
//   /tmp/make_heic_fixtures tests/data/heic
//
// writes quadrants_o1.heic to quadrants_o8.heic and quadrants_meta.heic into the folder. The pixels
// are stored as drawn whatever the orientation. Given an Orientation, ImageIO records the turn
// twice: as HEIF transform boxes (irot for a turn, imir for a mirror) and as the EXIF Orientation
// tag. The files differ in those and, for _meta, in the TIFF/Exif metadata: _meta has Orientation 1
// and a fixed set of camera fields (see tests/heic_test.cpp), and no GPS. A reader that applies
// the orientation ends up with the pictures listed in tests/heic_test.cpp.

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

constexpr int width = 64;
constexpr int height = 32;
// Same colours as prism_test::quadrant_rgb.
constexpr std::uint8_t colours[4][3] = {
    {230, 30, 30}, {30, 230, 30}, {30, 30, 230}, {230, 230, 30}};

// A CFString from UTF-8 text.
CFStringRef utf8_string(const char* text) {
    return CFStringCreateWithCString(nullptr, text, kCFStringEncodingUTF8);
}

CFNumberRef int_number(int value) {
    return CFNumberCreate(nullptr, kCFNumberIntType, &value);
}

CFNumberRef double_number(double value) {
    return CFNumberCreate(nullptr, kCFNumberDoubleType, &value);
}

CFDictionaryRef dictionary(const void** keys, const void** values, CFIndex count) {
    return CFDictionaryCreate(nullptr, keys, values, count, &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);
}

// The TIFF and Exif dictionaries of quadrants_meta.heic; no GPS dictionary at all. Added to
// `keys`/`values` (the destination's properties) and appended to `owned` for the caller to release.
void add_camera_metadata(std::vector<const void*>& keys, std::vector<const void*>& values,
                         std::vector<CFTypeRef>& owned) {
    CFStringRef make = utf8_string("Pentax \xC3\x84");
    CFStringRef model = utf8_string("K-1");
    const void* tiff_keys[] = {kCGImagePropertyTIFFMake, kCGImagePropertyTIFFModel};
    const void* tiff_values[] = {make, model};
    CFDictionaryRef tiff = dictionary(tiff_keys, tiff_values, 2);

    const int iso_value = 400;
    CFNumberRef iso = int_number(iso_value);
    const void* iso_items[] = {iso};
    CFArrayRef iso_array = CFArrayCreate(nullptr, iso_items, 1, &kCFTypeArrayCallBacks);
    CFNumberRef exposure = double_number(0.004);
    CFNumberRef f_number = double_number(2.8);
    CFNumberRef focal = double_number(35.0);
    CFNumberRef focal_35 = int_number(52);
    CFStringRef lens = utf8_string("Objektiv \xE5\x86\x99\xE7\x9C\x9F");
    CFStringRef date = utf8_string("2024:05:06 07:08:09");
    const void* exif_keys[] = {kCGImagePropertyExifISOSpeedRatings,
                               kCGImagePropertyExifExposureTime,
                               kCGImagePropertyExifFNumber,
                               kCGImagePropertyExifFocalLength,
                               kCGImagePropertyExifFocalLenIn35mmFilm,
                               kCGImagePropertyExifLensModel,
                               kCGImagePropertyExifDateTimeOriginal};
    const void* exif_values[] = {iso_array, exposure, f_number, focal, focal_35, lens, date};
    CFDictionaryRef exif = dictionary(exif_keys, exif_values, 7);

    keys.push_back(kCGImagePropertyTIFFDictionary);
    values.push_back(tiff);
    keys.push_back(kCGImagePropertyExifDictionary);
    values.push_back(exif);
    for (const CFTypeRef ref :
         std::initializer_list<CFTypeRef>{make, model, tiff, iso, iso_array, exposure, f_number,
                                          focal, focal_35, lens, date, exif}) {
        owned.push_back(ref);
    }
}

bool write(const std::string& path, int orientation, bool camera_metadata) {
    std::vector<std::uint8_t> rgba(width * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int q = (y >= height / 2 ? 2 : 0) + (x >= width / 2 ? 1 : 0);
            std::uint8_t* px = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            px[0] = colours[q][0];
            px[1] = colours[q][1];
            px[2] = colours[q][2];
            px[3] = 255;
        }
    }
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(rgba.data(), width, height, 8, width * 4, space,
                                                 kCGImageAlphaPremultipliedLast);
    CGImageRef image = CGBitmapContextCreateImage(context);

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()),
        false);
    CGImageDestinationRef dest =
        CGImageDestinationCreateWithURL(url, CFSTR("public.heic"), 1, nullptr);
    bool ok = false;
    if (dest != nullptr) {
        CFNumberRef o = int_number(orientation);
        CFNumberRef q = double_number(0.9);
        std::vector<const void*> keys = {kCGImagePropertyOrientation,
                                         kCGImageDestinationLossyCompressionQuality};
        std::vector<const void*> values = {o, q};
        std::vector<CFTypeRef> owned = {o, q};
        if (camera_metadata) {
            add_camera_metadata(keys, values, owned);
        }
        CFDictionaryRef props =
            dictionary(keys.data(), values.data(), static_cast<CFIndex>(keys.size()));
        CGImageDestinationAddImage(dest, image, props);
        ok = CGImageDestinationFinalize(dest);
        CFRelease(props);
        for (const CFTypeRef ref : owned) {
            CFRelease(ref);
        }
        CFRelease(dest);
    }
    CFRelease(url);
    CGImageRelease(image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output folder>\n", argv[0]);
        return 2;
    }
    const std::string folder = argv[1];
    for (int orientation = 1; orientation <= 8; ++orientation) {
        const std::string path = folder + "/quadrants_o" + std::to_string(orientation) + ".heic";
        if (!write(path, orientation, false)) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            return 1;
        }
        std::printf("wrote %s\n", path.c_str());
    }
    const std::string meta = folder + "/quadrants_meta.heic";
    if (!write(meta, 1, true)) {
        std::fprintf(stderr, "cannot write %s\n", meta.c_str());
        return 1;
    }
    std::printf("wrote %s\n", meta.c_str());
    return 0;
}

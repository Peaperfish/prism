#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <unistd.h>
#endif

#include "env_var.hpp"
#include "heic_damage.hpp"
#include "prism/heic.hpp"
#include "prism/image.hpp"
#include "prism/utf8_path.hpp"
#include "synthetic_exif.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fixture_named(const std::string& name) {
    return fs::path(PRISM_TEST_DATA) / "heic" / name;
}

fs::path fixture(int orientation) {
    return fixture_named("quadrants_o" + std::to_string(orientation) + ".heic");
}

#ifdef __APPLE__

// The colour (index into quadrant_rgb) expected in the top-left, top-right, bottom-left and
// bottom-right of the loaded picture for each Orientation; the same table as exif_test.cpp.
// The stored picture is   red   green
//                         blue  yellow
constexpr int expected_quadrants[9][4] = {
    {0, 0, 0, 0},  // (unused)
    {0, 1, 2, 3},  // 1 as stored
    {1, 0, 3, 2},  // 2
    {3, 2, 1, 0},  // 3 turned 180 degrees
    {2, 3, 0, 1},  // 4
    {0, 2, 1, 3},  // 5
    {2, 0, 3, 1},  // 6 turned 90 degrees clockwise
    {3, 1, 2, 0},  // 7
    {1, 3, 0, 2},  // 8 turned 90 degrees counter-clockwise
};

// The 8-bit sRGB colour at the middle of quadrant q (0..3 = TL, TR, BL, BR).
std::array<int, 3> quadrant_centre(const std::vector<std::uint8_t>& rgb, int width, int height,
                                   int q) {
    const int x = (q % 2 == 0 ? 1 : 3) * width / 4;
    const int y = (q < 2 ? 1 : 3) * height / 4;
    const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
    return {rgb[i], rgb[i + 1], rgb[i + 2]};
}

// Means of an 8 x 8 grid of blocks, per channel, of 8-bit RGB: 192 numbers.
std::vector<double> block_means(const std::vector<std::uint8_t>& rgb, int width, int height) {
    constexpr int n = 8;
    std::vector<double> sum(n * n * 3, 0.0);
    std::vector<double> count(n * n, 0.0);
    for (int y = 0; y < height; ++y) {
        const int by = std::min(n - 1, y * n / height);
        for (int x = 0; x < width; ++x) {
            const int b = by * n + std::min(n - 1, x * n / width);
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
            for (int c = 0; c < 3; ++c) {
                sum[b * 3 + c] += rgb[i + c];
            }
            count[b] += 1.0;
        }
    }
    for (int b = 0; b < n * n; ++b) {
        for (int c = 0; c < 3; ++c) {
            sum[b * 3 + c] /= std::max(count[b], 1.0);
        }
    }
    return sum;
}

// A folder of this test's own under the system temp folder, removed when the test ends.
class ScratchDir {
public:
    ScratchDir() {
        const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
        path_ = fs::path(testing::TempDir()) / ("prism_heic_" + std::string(test->name()) + "_" +
                                                std::to_string(static_cast<int>(getpid())));
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

// ImageIO's own upright rendering of the file (a thumbnail with the transform applied, at most
// `max_pixels` on the long side), as 8-bit sRGB: the oracle our decoder is compared with.
struct Rendering {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
};

Rendering imageio_upright(const fs::path& path, int max_pixels) {
    Rendering out;
    const std::string native = path.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(native.c_str()),
        static_cast<CFIndex>(native.size()), false);
    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (source == nullptr) {
        return out;
    }
    const void* keys[] = {kCGImageSourceCreateThumbnailFromImageAlways,
                          kCGImageSourceCreateThumbnailWithTransform,
                          kCGImageSourceThumbnailMaxPixelSize};
    CFNumberRef max = CFNumberCreate(nullptr, kCFNumberIntType, &max_pixels);
    const void* values[] = {kCFBooleanTrue, kCFBooleanTrue, max};
    CFDictionaryRef options = CFDictionaryCreate(nullptr, keys, values, 3,
                                                 &kCFTypeDictionaryKeyCallBacks,
                                                 &kCFTypeDictionaryValueCallBacks);
    CGImageRef image = CGImageSourceCreateThumbnailAtIndex(
        source, CGImageSourceGetPrimaryImageIndex(source), options);
    CFRelease(options);
    CFRelease(max);
    CFRelease(source);
    if (image == nullptr) {
        return out;
    }
    out.width = static_cast<int>(CGImageGetWidth(image));
    out.height = static_cast<int>(CGImageGetHeight(image));
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(out.width) * out.height * 4);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(rgba.data(), out.width, out.height, 8,
                                                 static_cast<std::size_t>(out.width) * 4, space,
                                                 kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(context, CGRectMake(0, 0, out.width, out.height), image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    CGImageRelease(image);
    out.rgb.resize(static_cast<std::size_t>(out.width) * out.height * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(out.width) * out.height; ++i) {
        for (int c = 0; c < 3; ++c) {
            out.rgb[i * 3 + c] = rgba[i * 4 + c];
        }
    }
    return out;
}

// Our decode agrees with ImageIO's upright rendering: same aspect (to within the thumbnail's
// rounding) and close 8 x 8 block means: no block further off than `tolerance`, and the average
// over all blocks within `mean_tolerance`. A tiny lossy fixture needs a loose `tolerance`: ImageIO
// turns the picture at a different stage than we do, which can put the chroma of a colour edge one
// pixel to the side, so the blocks on an edge differ by up to a pixel's worth of colour.
void expect_matches_imageio(const fs::path& path, const prism::Image& img, int max_pixels,
                            double tolerance, double mean_tolerance) {
    const Rendering oracle = imageio_upright(path, max_pixels);
    ASSERT_GT(oracle.width, 0) << "ImageIO could not render " << prism::utf8(path.filename());
    EXPECT_EQ(img.width > img.height, oracle.width > oracle.height);
    EXPECT_NEAR(static_cast<double>(img.width) / img.height,
                static_cast<double>(oracle.width) / oracle.height, 0.01);
    const std::vector<double> want = block_means(oracle.rgb, oracle.width, oracle.height);
    const std::vector<double> got = block_means(prism::encode_srgb8(img), img.width, img.height);
    ASSERT_EQ(want.size(), got.size());
    double worst = 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        worst = std::max(worst, std::abs(want[i] - got[i]));
        total += std::abs(want[i] - got[i]);
    }
    EXPECT_LE(worst, tolerance) << prism::utf8(path.filename());
    EXPECT_LE(total / static_cast<double>(want.size()), mean_tolerance)
        << prism::utf8(path.filename());
}

#endif  // __APPLE__

}  // namespace

TEST(Heic, DetectsHeicExtensionsCaseInsensitively) {
    for (const char* name : {"a.heic", "A.HEIC", "dir/IMG_1.Heic", "x.heif", "x.HEIF"}) {
        EXPECT_TRUE(prism::is_heic_file(name)) << name;
    }
    for (const char* name : {"a.jpg", "a.png", "heic", ".heic.png", "a.heic.jpg", "a.hei", ""}) {
        EXPECT_FALSE(prism::is_heic_file(name)) << name;
    }
}

// No skip anywhere: with ImageIO the fixture loads, without it the error says why.
TEST(Heic, LoadsTheFixtureOrSaysItNeedsMacOS) {
    if (prism::heic_support_available()) {
        const prism::Photo photo = prism::load_heic_photo(fixture(1));
        EXPECT_EQ(photo.image.width, 64);
        EXPECT_EQ(photo.image.height, 32);
    } else {
        try {
            (void)prism::load_heic_photo(fixture(1));
            FAIL() << "expected an exception";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find("macOS"), std::string::npos) << e.what();
        }
    }
}

// load_photo and load_image send .heic to the same decoder on every system; nothing skipped.
TEST(Heic, LoadPhotoDispatchesHeicFilesOrSaysItNeedsMacOS) {
    if (prism::heic_support_available()) {
        const prism::Photo photo = prism::load_photo(fixture(6));
        EXPECT_EQ(photo.image.width, prism_test::quadrant_height);
        EXPECT_EQ(photo.image.height, prism_test::quadrant_width);
        EXPECT_EQ(photo.info.orientation, 6);
        EXPECT_EQ(prism::load_image(fixture(6)).pixels, photo.image.pixels);
    } else {
        using Load = void (*)(const fs::path&);
        for (const Load load :
             std::initializer_list<Load>{[](const fs::path& p) { (void)prism::load_photo(p); },
                                         [](const fs::path& p) { (void)prism::load_image(p); }}) {
            try {
                load(fixture(1));
                FAIL() << "expected an exception";
            } catch (const std::runtime_error& e) {
                const std::string message = e.what();
                EXPECT_NE(message.find("macOS"), std::string::npos) << message;
                EXPECT_NE(message.find("HEIC photos can only be opened on macOS in this version "
                                       "of Prism."), std::string::npos) << message;
            }
        }
    }
}

#ifdef __APPLE__

TEST(Heic, SupportIsAvailableOnMacOS) {
    EXPECT_TRUE(prism::heic_support_available());
}

TEST(Heic, EveryFixtureOrientationLandsTheQuadrantsWhereTheyBelong) {
    for (int o = 1; o <= 8; ++o) {
        SCOPED_TRACE("orientation " + std::to_string(o));
        const prism::Photo photo = prism::load_heic_photo(fixture(o));
        const bool turned = o >= 5;
        EXPECT_EQ(photo.image.width,
                  turned ? prism_test::quadrant_height : prism_test::quadrant_width);
        EXPECT_EQ(photo.image.height,
                  turned ? prism_test::quadrant_width : prism_test::quadrant_height);
        EXPECT_EQ(photo.info.orientation, o);
        ASSERT_EQ(photo.image.pixels.size(),
                  static_cast<std::size_t>(photo.image.width) * photo.image.height * 3);

        const std::vector<std::uint8_t> bytes = prism::encode_srgb8(photo.image);
        for (int q = 0; q < 4; ++q) {
            const std::array<int, 3> got =
                quadrant_centre(bytes, photo.image.width, photo.image.height, q);
            for (int c = 0; c < 3; ++c) {
                // HEVC is lossy: a solid block's middle is within a few levels of its colour.
                EXPECT_NEAR(got[c], prism_test::quadrant_rgb[expected_quadrants[o][q]][c], 14)
                    << "quadrant " << q << " channel " << c;
            }
        }
        expect_matches_imageio(fixture(o), photo.image, 4096, 40.0, 8.0);
    }
}

TEST(Heic, ALoadedFixtureHasOnlyTheOrientationInItsInfo) {
    const prism::Photo photo = prism::load_heic_photo(fixture(6));
    EXPECT_FALSE(photo.info.make);
    EXPECT_FALSE(photo.info.iso);
    EXPECT_FALSE(photo.info.exposure_seconds);
}

// Every PhotoInfo field of quadrants_meta.heic (see tools/make_heic_fixtures.cpp), non-ASCII text
// included; the fixture has no GPS dictionary.
TEST(Heic, ReadsEveryPhotoInfoFieldFromTheMetaFixture) {
    const prism::Photo photo = prism::load_heic_photo(fixture_named("quadrants_meta.heic"));
    EXPECT_EQ(photo.info.make, std::optional<std::string>("Pentax \xC3\x84"));
    EXPECT_EQ(photo.info.model, std::optional<std::string>("K-1"));
    EXPECT_EQ(photo.info.lens, std::optional<std::string>("Objektiv \xE5\x86\x99\xE7\x9C\x9F"));
    EXPECT_EQ(photo.info.date_taken, std::optional<std::string>("2024:05:06 07:08:09"));
    EXPECT_EQ(photo.info.iso, std::optional<int>(400));
    EXPECT_EQ(photo.info.orientation, std::optional<int>(1));
    EXPECT_EQ(photo.info.focal_length_35mm, std::optional<int>(52));
    EXPECT_EQ(photo.info.exposure_seconds, std::optional<double>(0.004));
    EXPECT_EQ(photo.info.f_number, std::optional<double>(2.8));
    EXPECT_EQ(photo.info.focal_length_mm, std::optional<double>(35.0));
}

// A damaged file must throw, not decode to a black (or partly black) picture.
TEST(Heic, DamagedCopiesOfTheFixtureThrowNamingTheFile) {
    const std::vector<std::uint8_t> whole = prism_test::read_all_bytes(fixture(1));
    ASSERT_LT(prism_test::mdat_payload_offset(whole), whole.size()) << "no mdat box in the fixture";
    const ScratchDir dir;
    const std::vector<fs::path> damaged =
        prism_test::write_damaged_heic_copies(fixture(1), dir.path());
    ASSERT_EQ(damaged.size(), 4u);
    for (const fs::path& path : damaged) {
        const std::string name = prism::utf8(path.filename());
        SCOPED_TRACE(name);
        try {
            const prism::Photo photo = prism::load_heic_photo(path);
            ADD_FAILURE() << "decoded to " << photo.image.width << " x " << photo.image.height;
        } catch (const std::runtime_error& e) {
            const std::string message = e.what();
            EXPECT_NE(message.find("cannot decode"), std::string::npos) << message;
            EXPECT_NE(message.find(name), std::string::npos) << message;
        }
    }
}

// load_photo and load_image reach the same decoder: a damaged file throws there too.
TEST(Heic, LoadPhotoAndLoadImageThrowOnDamagedCopiesOfTheFixture) {
    const ScratchDir dir;
    for (const fs::path& path : prism_test::write_damaged_heic_copies(fixture(1), dir.path())) {
        SCOPED_TRACE(prism::utf8(path.filename()));
        EXPECT_THROW((void)prism::load_photo(path), std::runtime_error);
        EXPECT_THROW((void)prism::load_image(path), std::runtime_error);
    }
}

// Like raw_test.cpp's non-ASCII path test: the name goes through UTF-8 and back.
TEST(Heic, OpensAFileWithANonAsciiNameAndNamesAMissingOne) {
    const ScratchDir dir;
    const fs::path path = dir.path() / fs::path(u8"f\u00f6to \u5199\u771f.heic");
    fs::copy_file(fixture(6), path);
    const prism::Photo photo = prism::load_heic_photo(path);
    EXPECT_EQ(photo.image.width, prism_test::quadrant_height);
    EXPECT_EQ(photo.image.height, prism_test::quadrant_width);
    EXPECT_EQ(photo.info.orientation, 6);

    const fs::path missing = dir.path() / fs::path(u8"no_such_\u6587\u4ef6.heic");
    try {
        (void)prism::load_heic_photo(missing);
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find(prism::utf8(missing)), std::string::npos) << message;
        EXPECT_NE(message.find("no_such_\xE6\x96\x87\xE4\xBB\xB6.heic"), std::string::npos)
            << message;
    }
}

TEST(Heic, MissingAndNonHeicFilesThrowNamingTheFile) {
    for (const fs::path& path :
         {fs::path("does_not_exist.heic"), fs::path(PRISM_TEST_DATA) / "recipe_complete.prism"}) {
        try {
            (void)prism::load_heic_photo(path);
            FAIL() << "expected an exception for " << path;
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find(prism::utf8(path)), std::string::npos) << e.what();
        }
    }
}

// --- Real iPhone photos -------------------------------------------------------------------------
//
// They contain GPS and are not checked in. Point PRISM_TEST_HEIC at a folder of them to run these:
//   PRISM_TEST_HEIC=~/Photos/iphone ctest --test-dir build -R HeicFile

TEST(HeicFile, EveryPhotoInTheFolderMatchesImageIO) {
    const std::string dir = prism_test::env_var("PRISM_TEST_HEIC");
    if (dir.empty()) {
        GTEST_SKIP() << "set PRISM_TEST_HEIC to a folder of .heic photos to run this test";
    }
    int count = 0;
    const ScratchDir scratch;  // the truncated copies (they contain GPS) live and die here
    for (const fs::directory_entry& entry : fs::directory_iterator(fs::path(dir))) {
        if (!entry.is_regular_file() || !prism::is_heic_file(entry.path())) {
            continue;
        }
        ++count;
        const std::string name = prism::utf8(entry.path().filename());
        SCOPED_TRACE(name);
        const prism::Photo photo = prism::load_heic_photo(entry.path());
        ASSERT_GT(photo.image.width, 0);
        ASSERT_EQ(photo.image.pixels.size(),
                  static_cast<std::size_t>(photo.image.width) * photo.image.height * 3);
        // Looks in the same direction as ImageIO's upright rendering, with the same content.
        expect_matches_imageio(entry.path(), photo.image, 1024, 10.0, 3.0);

        // The same photo cut off at 70% of its size must throw, not come back part black.
        std::vector<std::uint8_t> bytes = prism_test::read_all_bytes(entry.path());
        bytes.resize(bytes.size() * 7 / 10);
        const fs::path cut = scratch.path() / entry.path().filename();
        prism_test::write_all_bytes(cut, bytes);
        EXPECT_THROW((void)prism::load_heic_photo(cut), std::runtime_error) << name;
        fs::remove(cut);

        EXPECT_TRUE(photo.info.make) << name;
        EXPECT_TRUE(photo.info.model) << name;
        EXPECT_TRUE(photo.info.iso) << name;
        EXPECT_TRUE(photo.info.exposure_seconds) << name;
        EXPECT_TRUE(photo.info.f_number) << name;
        EXPECT_TRUE(photo.info.focal_length_mm) << name;
        EXPECT_TRUE(photo.info.date_taken) << name;
        EXPECT_TRUE(photo.info.orientation) << name;
        // The photo is upright: portrait shots (Orientation 5-8) are taller than wide.
        if (photo.info.orientation && *photo.info.orientation >= 5) {
            EXPECT_GT(photo.image.height, photo.image.width) << name;
        }
    }
    EXPECT_GT(count, 0) << "no .heic files in " << dir;
}

#endif  // __APPLE__

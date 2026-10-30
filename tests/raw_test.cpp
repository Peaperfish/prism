#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "env_var.hpp"
#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/raw.hpp"
#include "synthetic_dng.hpp"

namespace fs = std::filesystem;

// --- Unit tests: no RAW file or LibRaw needed ----------------------------------------------------

TEST(Raw, DetectsRawExtensionsCaseInsensitively) {
    for (const char* name :
         {"a.arw", "A.ARW", "dir/shot.Arw", "x.dng", "c.cr2", "c.CR3", "n.nef", "f.RAF"}) {
        EXPECT_TRUE(prism::is_raw_file(name)) << name;
    }
    for (const char* name : {"a.jpg", "a.png", "arw", ".arw.png", "a.arw.jpg", "a.tif", "a.raw",
                             "dir.arw/a.png", ""}) {
        EXPECT_FALSE(prism::is_raw_file(name)) << name;
    }
}

TEST(Raw, SixteenBitToFloatCoversZeroToOne) {
    static_assert(prism::raw16_to_float(0) == 0.0f);
    static_assert(prism::raw16_to_float(65535) == 1.0f);
    EXPECT_FLOAT_EQ(prism::raw16_to_float(32768), 32768.0f / 65535.0f);

    // Every value lands in [0, 1], and each is strictly larger than the one before: dividing by
    // 65535 in float keeps all 65536 levels distinct, so no shadow detail is merged.
    float previous = -1.0f;
    for (int v = 0; v <= 65535; ++v) {
        const float f = prism::raw16_to_float(static_cast<std::uint16_t>(v));
        ASSERT_GE(f, 0.0f) << v;
        ASSERT_LE(f, 1.0f) << v;
        ASSERT_GT(f, previous) << v;
        previous = f;
    }
}

TEST(Raw, DecodeRgb16KeepsSampleOrder) {
    const std::vector<std::uint16_t> samples = {0, 65535, 1000,  2000, 3000, 4000,
                                                5, 6,     65534, 7,    8,    9};
    const prism::Image img = prism::decode_rgb16(samples, 2, 2);
    ASSERT_EQ(img.width, 2);
    ASSERT_EQ(img.height, 2);
    ASSERT_EQ(img.pixels.size(), samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        EXPECT_EQ(img.pixels[i], prism::raw16_to_float(samples[i])) << "sample " << i;
    }
}

TEST(Raw, DecodeRgb16RejectsWrongSize) {
    const std::vector<std::uint16_t> samples(11);
    EXPECT_THROW((void)prism::decode_rgb16(samples, 2, 2), std::invalid_argument);
}

// Without LibRaw a RAW file must give an error that says why, not stb's "unknown image type".
// With LibRaw, a missing file must still be reported as a RAW load error from LibRaw.
TEST(Raw, MissingRawFileGivesClearError) {
    const fs::path path = fs::path(testing::TempDir()) / "prism_does_not_exist.ARW";
    try {
        (void)prism::load_image(path);
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("cannot load RAW file"), std::string::npos) << message;
        const bool says_no_libraw = message.find("built without LibRaw") != std::string::npos;
        EXPECT_EQ(says_no_libraw, !prism::raw_support_available()) << message;
    }
}

// Without LibRaw the tests below check the "built without LibRaw" error and pass, so a CI job
// that should have RAW support sets PRISM_REQUIRE_RAW=1: then a build that lost LibRaw fails here
// instead of passing quietly.
TEST(Raw, SupportIsAvailableWhenRequired) {
    if (prism_test::env_var("PRISM_REQUIRE_RAW") != "1") {
        GTEST_SKIP() << "PRISM_REQUIRE_RAW is not set";
    }
    EXPECT_TRUE(prism::raw_support_available());
}

// --- Synthetic DNG: a real decode on every build with LibRaw
// --------------------------------------
//
// synthetic_dng.hpp writes a 32 x 32 uniform grey Bayer DNG, so LibRaw's whole path (open, unpack,
// demosaic, colour conversion, output) runs in CI without a camera file. Without LibRaw the same
// tests check that loading it fails with the "built without LibRaw" error.

namespace {

constexpr std::uint16_t grey_level = 16384;  // 0.25 of white
constexpr float grey = grey_level / 65535.0f;

// The test's name and the process id keep `ctest -j` runs apart, as in cli_test.cpp. `suffix` is
// UTF-8 text before ".dng".
fs::path write_synthetic_dng(const std::u8string& suffix) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    fs::path path = fs::path(testing::TempDir()) /
                    ("prism_raw_" + std::string(test->name()) + "_" + std::to_string(pid));
    path += suffix;
    path += ".dng";
    const std::vector<std::byte> bytes = prism_test::synthetic_dng(grey_level);
    // ofstream takes the fs::path itself, so it opens the UTF-16 name on Windows.
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    EXPECT_TRUE(out.good()) << "cannot write the test DNG";
    return path;
}

// Loads `path`; with LibRaw, checks that the result is a uniform grey of `width` x `height` at
// `grey` in every channel. Without LibRaw, checks the error instead.
void expect_grey_dng(const fs::path& path, const prism::RawOptions& options, int width,
                     int height) {
    if (!prism::raw_support_available()) {
        try {
            (void)prism::load_image(path, options);
            FAIL() << "expected an exception";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find("built without LibRaw"), std::string::npos)
                << e.what();
        }
        return;
    }
    const prism::Image img = prism::load_image(path, options);
    ASSERT_EQ(img.width, width);
    ASSERT_EQ(img.height, height);
    ASSERT_EQ(img.pixels.size(), static_cast<std::size_t>(width) * height * prism::Image::channels);
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        // LibRaw works in integers and its sRGB matrix is a rounded inverse of ours, so allow 1%.
        ASSERT_NEAR(img.pixels[i], grey, 0.01f * grey) << "sample " << i;
    }
}

constexpr int dng_size = prism_test::synthetic_dng_size;

}  // namespace

TEST(RawSyntheticDng, FullSizeDecodesToUniformGrey) {
    const fs::path path = write_synthetic_dng(u8"");
    expect_grey_dng(path, {}, dng_size, dng_size);
    fs::remove(path);
}

TEST(RawSyntheticDng, HalfSizeHalvesTheDimensions) {
    const fs::path path = write_synthetic_dng(u8"");
    prism::RawOptions options;
    options.half_size = true;
    expect_grey_dng(path, options, dng_size / 2, dng_size / 2);
    fs::remove(path);
}

// With an identity matrix LibRaw outputs camera RGB, which for this file is already linear sRGB.
TEST(RawSyntheticDng, IdentityCameraMatrixGivesTheSameGrey) {
    const fs::path path = write_synthetic_dng(u8"");
    prism::RawOptions options;
    options.camera_matrix = prism::identity_matrix;
    expect_grey_dng(path, options, dng_size, dng_size);
    fs::remove(path);
}

// "ü_写真_📷", as u-escapes (see unicode_path_test.cpp). On Windows this goes through LibRaw's
// open_file(const wchar_t*): a name outside the ANSI code page must open.
TEST(RawSyntheticDng, LoadsFromANonAsciiPath) {
    const fs::path path = write_synthetic_dng(u8"_\u00fc_\u5199\u771f_\U0001F4F7");
    expect_grey_dng(path, {}, dng_size, dng_size);
    fs::remove(path);
}

// A file LibRaw can't parse must give LibRaw's own error, with the (non-ASCII) path in it.
TEST(RawSyntheticDng, TruncatedFileGivesClearError) {
    const fs::path path = write_synthetic_dng(u8"_\u5199\u771f");
    fs::resize_file(path, 64);
    try {
        (void)prism::load_image(path);
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("cannot load RAW file"), std::string::npos) << message;
        const std::u8string name = path.filename().u8string();
        EXPECT_NE(message.find(std::string(name.begin(), name.end())), std::string::npos)
            << message;
        if (prism::raw_support_available()) {
            EXPECT_EQ(message.find("built without LibRaw"), std::string::npos) << message;
        }
    }
    fs::remove(path);
}

// --- Integration tests: only with a real RAW file ------------------------------------------------
//
// RAW files are tens of MB, so none is checked in. Point PRISM_TEST_RAW at one to run these:
//   PRISM_TEST_RAW=~/Photos/DSC01234.ARW ctest --test-dir build -R RawFile
// Optionally set PRISM_TEST_RAW_SIZE=<width>x<height> (after rotation) to check the exact size.

namespace {

std::optional<fs::path> test_raw_path() {
    const std::string path = prism_test::env_var("PRISM_TEST_RAW");
    if (path.empty()) {
        return std::nullopt;
    }
    return fs::path(path);
}

// Checks the shape, and that every value is finite and in [0, 1] with some actual signal.
void expect_valid_linear_image(const prism::Image& img) {
    ASSERT_GT(img.width, 0);
    ASSERT_GT(img.height, 0);
    ASSERT_EQ(img.pixels.size(),
              static_cast<std::size_t>(img.width) * img.height * prism::Image::channels);
    std::size_t out_of_range = 0;
    float max = 0.0f;
    for (const float v : img.pixels) {
        if (!std::isfinite(v) || v < 0.0f || v > 1.0f) {
            ++out_of_range;
        }
        max = std::max(max, v);
    }
    EXPECT_EQ(out_of_range, 0u);
    EXPECT_GT(max, 0.0f) << "image is entirely black";
}

class RawFile : public testing::Test {
protected:
    void SetUp() override {
        const std::optional<fs::path> path = test_raw_path();
        if (!path) {
            GTEST_SKIP() << "set PRISM_TEST_RAW to a RAW file to run this test";
        }
        if (!prism::raw_support_available()) {
            GTEST_SKIP() << "Prism was built without LibRaw";
        }
        path_ = *path;
    }

    fs::path path_;
};

}  // namespace

TEST_F(RawFile, FullSizeLoadsLinearValuesInRange) {
    const prism::Image img = prism::load_image(path_);
    expect_valid_linear_image(img);

    if (const std::string size = prism_test::env_var("PRISM_TEST_RAW_SIZE"); !size.empty()) {
        EXPECT_EQ(std::to_string(img.width) + "x" + std::to_string(img.height), size);
    }
}

TEST_F(RawFile, HalfSizeHalvesTheDimensions) {
    const prism::Image full = prism::load_image(path_);
    prism::RawOptions options;
    options.half_size = true;
    const prism::Image half = prism::load_image(path_, options);
    expect_valid_linear_image(half);

    // Half size rounds up an odd dimension, so allow one pixel either way.
    EXPECT_NEAR(half.width * 2, full.width, 1) << half.width << " vs " << full.width;
    EXPECT_NEAR(half.height * 2, full.height, 1) << half.height << " vs " << full.height;
}

TEST_F(RawFile, IdentityCameraMatrixGivesCameraRgb) {
    prism::RawOptions options;
    options.camera_matrix = prism::identity_matrix;
    const prism::Image img = prism::load_image(path_, options);
    // Native camera RGB after white balance is still in [0, 1]: nothing has mixed the channels yet.
    expect_valid_linear_image(img);

    // Same geometry as the default colour path; only the colours differ.
    const prism::Image srgb = prism::load_image(path_);
    EXPECT_EQ(img.width, srgb.width);
    EXPECT_EQ(img.height, srgb.height);
}

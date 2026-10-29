#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/raw.hpp"

namespace fs = std::filesystem;

// --- Unit tests: no RAW file or LibRaw needed ----------------------------------------------------

TEST(Raw, DetectsRawExtensionsCaseInsensitively) {
    for (const char* name : {"a.arw", "A.ARW", "dir/shot.Arw", "x.dng", "c.cr2", "c.CR3", "n.nef",
                             "f.RAF"}) {
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

// --- Integration tests: only with a real RAW file ------------------------------------------------
//
// RAW files are tens of MB, so none is checked in. Point PRISM_TEST_RAW at one to run these:
//   PRISM_TEST_RAW=~/Photos/DSC01234.ARW ctest --test-dir build -R RawFile
// Optionally set PRISM_TEST_RAW_SIZE=<width>x<height> (after rotation) to check the exact size.

namespace {

std::optional<fs::path> test_raw_path() {
    const char* path = std::getenv("PRISM_TEST_RAW");
    if (path == nullptr || *path == '\0') {
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

    if (const char* size = std::getenv("PRISM_TEST_RAW_SIZE")) {
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

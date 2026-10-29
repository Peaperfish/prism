#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

#include "prism/color.hpp"
#include "prism/image.hpp"

namespace fs = std::filesystem;

namespace {

// A small image where every channel holds a different sRGB 8-bit value, so a PNG
// round-trip checks pixel order as well as the colour conversion.
prism::Image make_test_image() {
    prism::Image img;
    img.width = 4;
    img.height = 3;
    img.pixels.resize(static_cast<std::size_t>(img.width * img.height * prism::Image::channels));
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        img.pixels[i] = prism::srgb8_to_linear(static_cast<std::uint8_t>(i * 7));
    }
    return img;
}

fs::path temp_path(const char* name) {
    return fs::path(testing::TempDir()) / name;
}

}  // namespace

TEST(Image, PngRoundTripIsExact) {
    const prism::Image original = make_test_image();
    const fs::path path = temp_path("prism_roundtrip.png");
    prism::save_image(path, original);

    const prism::Image loaded = prism::load_image(path);
    fs::remove(path);

    ASSERT_EQ(loaded.width, original.width);
    ASSERT_EQ(loaded.height, original.height);
    ASSERT_EQ(loaded.pixels.size(), original.pixels.size());
    for (std::size_t i = 0; i < original.pixels.size(); ++i) {
        EXPECT_EQ(loaded.pixels[i], original.pixels[i]) << "sample " << i;
    }
}

TEST(Image, JpegRoundTripKeepsSize) {
    const prism::Image original = make_test_image();
    const fs::path path = temp_path("prism_roundtrip.JPG");  // also checks case-insensitivity
    prism::save_image(path, original);

    const prism::Image loaded = prism::load_image(path);
    fs::remove(path);

    ASSERT_EQ(loaded.width, original.width);
    ASSERT_EQ(loaded.height, original.height);
}

TEST(Image, LoadMissingFileThrows) {
    EXPECT_THROW((void)prism::load_image(temp_path("does_not_exist.png")), std::runtime_error);
}

TEST(Image, SaveUnsupportedExtensionThrows) {
    EXPECT_THROW(prism::save_image(temp_path("out.bmp"), make_test_image()), std::runtime_error);
}

TEST(Image, SaveMismatchedBufferThrows) {
    prism::Image img = make_test_image();
    img.pixels.pop_back();
    EXPECT_THROW(prism::save_image(temp_path("out.png"), img), std::runtime_error);
}

TEST(Image, SaveToMissingDirectoryThrows) {
    EXPECT_THROW(prism::save_image(temp_path("no_such_dir/out.png"), make_test_image()),
                 std::runtime_error);
}

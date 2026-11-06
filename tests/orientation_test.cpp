#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "prism/image.hpp"

namespace {

// A width x height image whose pixel number i = y * width + x holds (i, i + 1000, i + 2000), so
// every pixel and every channel is distinct.
prism::Image numbered(int width, int height) {
    prism::Image img;
    img.width = width;
    img.height = height;
    for (int i = 0; i < width * height; ++i) {
        img.pixels.push_back(static_cast<float>(i));
        img.pixels.push_back(static_cast<float>(i + 1000));
        img.pixels.push_back(static_cast<float>(i + 2000));
    }
    return img;
}

// Checks that `img` is `width` x `height` and that its pixels carry the numbers in `ids`, row by
// row (in the first channel; the others are checked to match).
void expect_ids(const prism::Image& img, int width, int height, const std::vector<int>& ids) {
    ASSERT_EQ(img.width, width);
    ASSERT_EQ(img.height, height);
    ASSERT_EQ(img.pixels.size(), static_cast<std::size_t>(width) * height * 3);
    ASSERT_EQ(ids.size(), static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        EXPECT_EQ(img.pixels[3 * i], static_cast<float>(ids[i])) << "pixel " << i;
        EXPECT_EQ(img.pixels[3 * i + 1], static_cast<float>(ids[i] + 1000)) << "pixel " << i;
        EXPECT_EQ(img.pixels[3 * i + 2], static_cast<float>(ids[i] + 2000)) << "pixel " << i;
    }
}

}  // namespace

// The 3 x 2 input is   0 1 2
//                      3 4 5
// and each expected grid below is worked out by hand from the table in image.hpp.
TEST(ApplyOrientation, ThreeByTwoAllEightValues) {
    const struct {
        int orientation;
        int width;
        int height;
        std::vector<int> ids;
    } cases[] = {
        {1, 3, 2, {0, 1, 2, 3, 4, 5}},  // as is
        {2, 3, 2, {2, 1, 0, 5, 4, 3}},  // mirror left-right
        {3, 3, 2, {5, 4, 3, 2, 1, 0}},  // 180 degrees
        {4, 3, 2, {3, 4, 5, 0, 1, 2}},  // mirror top-bottom
        {5, 2, 3, {0, 3, 1, 4, 2, 5}},  // transpose
        {6, 2, 3, {3, 0, 4, 1, 5, 2}},  // 90 degrees clockwise
        {7, 2, 3, {5, 2, 4, 1, 3, 0}},  // transverse
        {8, 2, 3, {2, 5, 1, 4, 0, 3}},  // 90 degrees counter-clockwise
    };
    for (const auto& c : cases) {
        SCOPED_TRACE("orientation " + std::to_string(c.orientation));
        prism::Image img = numbered(3, 2);
        prism::apply_orientation(img, c.orientation);
        expect_ids(img, c.width, c.height, c.ids);
    }
}

TEST(ApplyOrientation, WideRowRotatedClockwise) {
    constexpr int w = 37;
    constexpr int h = 5;
    prism::Image img = numbered(w, h);
    prism::apply_orientation(img, 6);
    ASSERT_EQ(img.width, h);
    ASSERT_EQ(img.height, w);
    // out(x, y) = in(y, H-1-x), with in(x, y) numbered y * W + x.
    std::vector<int> ids;
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < h; ++x) {
            ids.push_back((h - 1 - x) * w + y);
        }
    }
    expect_ids(img, h, w, ids);
}

TEST(ApplyOrientation, TurningBackRestoresTheImage) {
    // 6 then 8 (clockwise then counter-clockwise), and 5 and 2 / 4 twice, are identities.
    const prism::Image original = numbered(7, 4);
    prism::Image img = original;
    prism::apply_orientation(img, 6);
    prism::apply_orientation(img, 8);
    EXPECT_EQ(img.pixels, original.pixels);
    prism::apply_orientation(img, 5);
    prism::apply_orientation(img, 5);
    EXPECT_EQ(img.width, original.width);
    EXPECT_EQ(img.pixels, original.pixels);
}

TEST(ApplyOrientation, OtherValuesAndEmptyImagesAreLeftAlone) {
    for (const int o : {-1, 0, 9, 1000}) {
        prism::Image img = numbered(3, 2);
        prism::apply_orientation(img, o);
        expect_ids(img, 3, 2, {0, 1, 2, 3, 4, 5});
    }
    prism::Image empty;
    prism::apply_orientation(empty, 6);
    EXPECT_EQ(empty.width, 0);
    EXPECT_EQ(empty.height, 0);
}

TEST(ApplyOrientation, RejectsAnInconsistentImage) {
    prism::Image img = numbered(3, 2);
    img.pixels.pop_back();
    EXPECT_THROW(prism::apply_orientation(img, 6), std::invalid_argument);
}

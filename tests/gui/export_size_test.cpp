#include <gtest/gtest.h>

#include <optional>

#include "prism/gui/export_size.hpp"

using prism::gui::export_long_side;
using prism::gui::ExportSize;

namespace {

ExportSize long_side(int pixels) {
    return {ExportSize::Mode::long_side, pixels, 50};
}

ExportSize percent(int p) {
    return {ExportSize::Mode::percent, 2048, p};
}

}  // namespace

TEST(ExportSize, OriginalNeverReduces) {
    EXPECT_EQ(export_long_side(ExportSize{}, 6000, 4000), std::nullopt);
    EXPECT_EQ(export_long_side({ExportSize::Mode::original, 100, 10}, 6000, 4000), std::nullopt)
        << "the stored numbers do not matter";
}

TEST(ExportSize, LongSideSmallerThanTheFileIsUsed) {
    EXPECT_EQ(export_long_side(long_side(2048), 6000, 4000), 2048);
    EXPECT_EQ(export_long_side(long_side(2048), 4000, 6000), 2048) << "portrait";
    EXPECT_EQ(export_long_side(long_side(1), 6000, 4000), 1);
    EXPECT_EQ(export_long_side(long_side(5999), 6000, 4000), 5999);
    EXPECT_EQ(export_long_side(long_side(4000), 4000, 6000), 4000)
        << "the short side does not count: 6000 is reduced to 4000";
}

TEST(ExportSize, LongSideEqualToOrLargerThanTheFileGivesNothing) {
    EXPECT_EQ(export_long_side(long_side(6000), 6000, 4000), std::nullopt);
    EXPECT_EQ(export_long_side(long_side(6001), 6000, 4000), std::nullopt);
    EXPECT_EQ(export_long_side(long_side(6000), 4000, 6000), std::nullopt);
    EXPECT_EQ(export_long_side(long_side(100000), 6000, 4000), std::nullopt);
}

TEST(ExportSize, PercentIsOfTheFilesLongerSide) {
    EXPECT_EQ(export_long_side(percent(50), 6000, 4000), 3000);
    EXPECT_EQ(export_long_side(percent(50), 4000, 6000), 3000) << "portrait";
    EXPECT_EQ(export_long_side(percent(33), 3001, 2001), 990);
    EXPECT_EQ(export_long_side(percent(33), 2001, 3001), 990);
    EXPECT_EQ(export_long_side(percent(50), 3001, 2001), 1501) << "halves round up";
    EXPECT_EQ(export_long_side(percent(1), 6000, 4000), 60);
}

TEST(ExportSize, PercentThatRoundsToTheFileGivesNothing) {
    EXPECT_EQ(export_long_side(percent(100), 6000, 4000), std::nullopt);
    EXPECT_EQ(export_long_side(percent(99), 20, 10), std::nullopt) << "19.8 rounds to 20";
    EXPECT_EQ(export_long_side(percent(95), 10, 10), std::nullopt) << "9.5 rounds up to 10";
    EXPECT_EQ(export_long_side(percent(94), 10, 10), 9);
}

TEST(ExportSize, PercentOfATinyFileIsAtLeastOnePixelOrNothing) {
    EXPECT_EQ(export_long_side(percent(1), 10, 10), 1);
    EXPECT_EQ(export_long_side(percent(50), 1, 1), std::nullopt) << "already 1 pixel";
}

TEST(ExportSize, MalformedOrUnknownSizesGiveNothing) {
    EXPECT_EQ(export_long_side(long_side(0), 6000, 4000), std::nullopt);
    EXPECT_EQ(export_long_side(long_side(-5), 6000, 4000), std::nullopt);
    EXPECT_EQ(export_long_side(percent(0), 6000, 4000), std::nullopt);
    EXPECT_EQ(export_long_side(percent(101), 6000, 4000), std::nullopt);
    EXPECT_EQ(export_long_side(long_side(100), 0, 0), std::nullopt) << "no file open";
}

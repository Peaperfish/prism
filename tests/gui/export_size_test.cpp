#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>

#include "prism/gui/export_size.hpp"
#include "prism/gui/file_types.hpp"

using prism::gui::export_long_side;
using prism::gui::ExportSize;
using prism::gui::export_original_text;
using prism::gui::export_result_text;
using prism::gui::plan_export;
using prism::PixelSize;

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

namespace {

std::string result(const ExportSize& size, PixelSize file, PixelSize start) {
    return export_result_text(size, plan_export(size, file, start));
}

// The name the save dialog starts with for `source`, as app.cpp asks for it.
std::string suggested_name(const ExportSize& size, PixelSize file, PixelSize start,
                           const char* source = "DSC0001.jpg") {
    const prism::gui::ExportPlan plan = plan_export(size, file, start);
    return prism::gui::suggested_export_path(source, prism::gui::ExportFormat::jpeg, plan.long_side)
        .filename()
        .string();
}

}  // namespace

TEST(ExportPlan, ReducedSaysTheNewSize) {
    EXPECT_EQ(result(long_side(2048), {6000, 4000}, {6000, 4000}), "Exports at 2048 \xC3\x97 1365");
    EXPECT_EQ(result(percent(50), {6000, 4000}, {6000, 4000}), "Exports at 3000 \xC3\x97 2000");
}

TEST(ExportPlan, PortraitReducesTheLongerSide) {
    EXPECT_EQ(result(long_side(2048), {4000, 6000}, {4000, 6000}), "Exports at 1365 \xC3\x97 2048");
    EXPECT_EQ(result(percent(25), {4000, 6000}, {4000, 6000}), "Exports at 1000 \xC3\x97 1500");
}

TEST(ExportPlan, ASizeThatIsNotSmallerSaysSo) {
    EXPECT_EQ(result(long_side(6000), {6000, 4000}, {6000, 4000}),
              "Not smaller than the photo: exports at full size, 6000 \xC3\x97 4000");
    EXPECT_EQ(result(long_side(9000), {1200, 800}, {1200, 800}),
              "Not smaller than the photo: exports at full size, 1200 \xC3\x97 800");
    EXPECT_EQ(result(percent(99), {10, 6}, {10, 6}),
              "Not smaller than the photo: exports at full size, 10 \xC3\x97 6")
        << "99% of 10 rounds back up to 10";
}

TEST(ExportPlan, OriginalIsFullSize) {
    EXPECT_EQ(result(ExportSize{}, {6000, 4000}, {6000, 4000}),
              "Exports at full size, 6000 \xC3\x97 4000");
    EXPECT_EQ(export_original_text({6000, 4000}), "Full size, 6000 \xC3\x97 4000");
}

TEST(ExportPlan, ARawFileWithHalfSizeWritesTheHalfSizeDecode) {
    // A 6000 x 4000 sensor decoded at 3000 x 2000.
    const PixelSize file{6000, 4000};
    const PixelSize half{3000, 2000};
    EXPECT_EQ(result(ExportSize{}, file, half), "Exports at full size, 3000 \xC3\x97 2000");
    EXPECT_EQ(result(long_side(2048), file, half), "Exports at 2048 \xC3\x97 1365");
    EXPECT_EQ(result(percent(25), file, half), "Exports at 1500 \xC3\x97 1000");
    // 50% of the SENSOR is the half-size decode itself: nothing to reduce.
    EXPECT_EQ(result(percent(50), file, half),
              "Not smaller than the photo: exports at full size, 3000 \xC3\x97 2000");
    EXPECT_EQ(result(long_side(3000), file, half),
              "Not smaller than the photo: exports at full size, 3000 \xC3\x97 2000");
    // The same sizes without half-size decoding start from the sensor.
    EXPECT_EQ(result(percent(50), file, file), "Exports at 3000 \xC3\x97 2000");
}

TEST(ExportPlan, TheFileNameHasTheSideOnlyWhenTheExportIsReduced) {
    const PixelSize photo{6000, 4000};
    EXPECT_EQ(suggested_name(long_side(2048), photo, photo), "DSC0001_prism_2048.jpg");
    EXPECT_EQ(suggested_name(ExportSize{}, photo, photo), "DSC0001_prism.jpg");
    EXPECT_EQ(suggested_name(long_side(6000), photo, photo), "DSC0001_prism.jpg");
    EXPECT_EQ(suggested_name(long_side(9000), photo, photo), "DSC0001_prism.jpg");
    EXPECT_EQ(suggested_name(percent(50), photo, photo), "DSC0001_prism_3000.jpg");
    // RAW at half size: 3000 is what 50% of the sensor asks, and the decode already is that.
    EXPECT_EQ(suggested_name(percent(50), photo, {3000, 2000}), "DSC0001_prism.jpg");
    EXPECT_EQ(suggested_name(percent(25), photo, {3000, 2000}), "DSC0001_prism_1500.jpg");
}

TEST(ExportPlan, NothingOpenPlansNothing) {
    const prism::gui::ExportPlan plan = plan_export(long_side(100), {0, 0}, {0, 0});
    EXPECT_FALSE(plan.reduced());
}

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>

#include "prism/gui/export_size.hpp"
#include "prism/frame.hpp"
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

// --- An estimated RAW size is not shown as exact (#55) ---

TEST(HeadlessEstimatedExportSize, BothLinesSayAboutWhenTheFileSizeIsAnEstimate) {
    const PixelSize file{6000, 4000};
    EXPECT_EQ(export_original_text(file, false), "Full size, about 6000 \xC3\x97 4000");
    const auto text = [&](const ExportSize& size) {
        return export_result_text(size, plan_export(size, file, file), false);
    };
    EXPECT_EQ(text(ExportSize{}), "Exports at full size, about 6000 \xC3\x97 4000");
    EXPECT_EQ(text(long_side(2048)), "Exports at about 2048 \xC3\x97 1365");
    EXPECT_EQ(text(long_side(9000)),
              "Not smaller than the photo: exports at full size, about 6000 \xC3\x97 4000");
}

TEST(HeadlessEstimatedExportSize, AnExactSizeReadsAsBefore) {
    const PixelSize file{6000, 4000};
    EXPECT_EQ(export_original_text(file, true), "Full size, 6000 \xC3\x97 4000");
    EXPECT_EQ(export_original_text(file), export_original_text(file, true));
    const ExportSize size = long_side(2048);
    const prism::gui::ExportPlan plan = plan_export(size, file, file);
    EXPECT_EQ(export_result_text(size, plan, true), "Exports at 2048 \xC3\x97 1365");
    EXPECT_EQ(export_result_text(size, plan), export_result_text(size, plan, true));
}

TEST(HeadlessEstimatedExportSize, TheMarkerIsTheStatusBarsOwn) {
    EXPECT_EQ(prism::gui::size_estimate_prefix(false), "about ");
    EXPECT_EQ(prism::gui::size_estimate_prefix(true), "");
}

// A RAW file whose size is only an estimate (made up here: 6000 x 4000, its half-size decode
// 3000 x 2000), with a turned, cropped frame: the sheet's lines measure the frame on the
// estimate, and still say "about".
TEST(FramedEstimatedExportSize, TheSheetMeasuresTheFrameOnTheEstimate) {
    prism::Frame frame;
    frame.angle = 2.0f;
    frame.left = 0.1f;
    frame.top = 0.05f;
    frame.right = 0.85f;
    frame.bottom = 0.9f;
    frame.aspect = prism::Aspect::free;
    const PixelSize file{6000, 4000};
    const PixelSize framed = prism::gui::framed_export_size(frame, file);
    EXPECT_EQ(framed, prism::framed_size(prism::fit_frame(frame, 6000, 4000), 6000, 4000));
    ASSERT_LT(framed.width, 6000);
    const std::string times =
        std::to_string(framed.width) + " \xC3\x97 " + std::to_string(framed.height);
    EXPECT_EQ(export_original_text(framed, false), "Full size, about " + times);

    ExportSize half;
    half.mode = ExportSize::Mode::percent;
    half.percent = 50;
    const prism::gui::ExportPlan plan = plan_export(half, framed, framed);
    ASSERT_TRUE(plan.reduced());
    EXPECT_EQ(*plan.long_side,
              prism::long_side_for_percent(framed.width, framed.height, 50));
    const PixelSize reduced =
        prism::downscaled_size(framed.width, framed.height, *plan.long_side);
    EXPECT_EQ(export_result_text(half, plan, false),
              "Exports at about " + std::to_string(reduced.width) + " \xC3\x97 " +
                  std::to_string(reduced.height));

    // With half-size decoding the export starts from the frame on the half-size decode.
    const PixelSize start = prism::gui::framed_export_size(frame, {3000, 2000});
    EXPECT_EQ(start, prism::framed_size(prism::fit_frame(frame, 3000, 2000), 3000, 2000));
    EXPECT_EQ(plan_export(ExportSize{}, framed, start).size, start);
    // No frame, or the whole photo: the size itself.
    EXPECT_EQ(prism::gui::framed_export_size(std::nullopt, file), file);
    EXPECT_EQ(prism::gui::framed_export_size(prism::Frame{}, file), file);
}

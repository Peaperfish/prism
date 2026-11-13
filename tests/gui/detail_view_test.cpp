#include <gtest/gtest.h>

#include "prism/frame.hpp"
#include "prism/gui/detail_view.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/resize.hpp"

// Real-pixel zoom's pure maths (#37): zoom as a percent of the file, whether real pixels are
// wanted, the region they come from and its budget, the size it is drawn at, the "· preview"
// chip, and the words.

namespace {

using prism::Frame;
using prism::PixelSize;
using prism::plan_export_frame;
using namespace prism::gui;

// A 60 MP landscape file and the 2560 px preview Session keeps of it.
constexpr PixelSize file_60mp{9504, 6336};

PixelSize preview_of(PixelSize file) {
    return prism::downscaled_size(file.width, file.height, 2560);
}

// The framed sizes of `frame` on the file and on its preview, as Session works them out: each
// fitted on its own decode.
PixelSize framed(const Frame& frame, PixelSize size) {
    return plan_export_frame(frame, size.width, size.height).size;
}

TEST(DetailView, FileScaleIsPerAxis) {
    const FileScale k = file_scale({9564, 6376}, {2560, 1707});
    EXPECT_DOUBLE_EQ(k.x, 9564.0 / 2560.0);
    EXPECT_DOUBLE_EQ(k.y, 6376.0 / 1707.0);
    EXPECT_DOUBLE_EQ(k.shown, k.x);  // landscape: read along x
    const FileScale tall = file_scale({6376, 9564}, {1707, 2560});
    EXPECT_DOUBLE_EQ(tall.shown, tall.y);  // portrait: read along y
    const FileScale empty = file_scale({9564, 6376}, {0, 1707});
    EXPECT_DOUBLE_EQ(empty.x, 1.0);
    EXPECT_DOUBLE_EQ(empty.y, 1.0);
    EXPECT_DOUBLE_EQ(empty.shown, 1.0);
}

TEST(DetailView, HundredPercentIsOneFilePixelOnOneDevicePixel) {
    const FileScale k = file_scale(file_60mp, preview_of(file_60mp));
    // 1x screen: a preview pixel is k.shown points across at 100%.
    EXPECT_NEAR(file_zoom_percent(k.shown, 1.0, k), 100.0, 1e-9);
    EXPECT_NEAR(view_scale_for_file_percent(100.0, 1.0, k), k.shown, 1e-12);
    // 2x screen: half that, so a file pixel is half a point.
    EXPECT_NEAR(file_zoom_percent(k.shown / 2.0, 2.0, k), 100.0, 1e-9);
    EXPECT_NEAR(view_scale_for_file_percent(100.0, 2.0, k), k.shown / 2.0, 1e-12);
    // And the preview at one device pixel per preview pixel is the preview's own share of the file.
    EXPECT_NEAR(file_zoom_percent(0.5, 2.0, k), 100.0 * 2560.0 / 9504.0, 1e-9);
    // Round trip at a step.
    EXPECT_NEAR(file_zoom_percent(view_scale_for_file_percent(33.0, 2.0, k), 2.0, k), 33.0, 1e-9);
}

TEST(DetailView, ATurnedFrameReadsEachAxisOnItsOwnScale) {
    const Frame turned{-12.0f, 0.1f, 0.15f, 0.85f, 0.9f};  // 7167 x 4778 over 1918 x 1279
    const PixelSize file{9564, 6376};
    const PixelSize full = framed(turned, file);
    const PixelSize preview = framed(turned, preview_of(file));
    const FileScale k = file_scale(full, preview);
    // The sizes round on their own, so the two axes' scales differ.
    ASSERT_NE(k.x, k.y);
    EXPECT_DOUBLE_EQ(k.x, static_cast<double>(full.width) / static_cast<double>(preview.width));
    EXPECT_DOUBLE_EQ(k.y, static_cast<double>(full.height) / static_cast<double>(preview.height));
    ASSERT_GT(preview.width, preview.height);
    // The percent is along x, the frame's longer side: at 100% x has exactly one device pixel per
    // file pixel, and y is off by the ratio of the two scales.
    const double s = view_scale_for_file_percent(100.0, 2.0, k);
    EXPECT_NEAR(s * 2.0 / k.x, 1.0, 1e-12);
    EXPECT_NE(s * 2.0 / k.y, 1.0);
    EXPECT_NEAR(file_zoom_percent(s, 2.0, k), 100.0, 1e-9);
}

TEST(DetailView, ZoomStopsAtEightHundredPercentOfTheFile) {
    const FileScale k = file_scale(file_60mp, preview_of(file_60mp));
    const double fit = 0.3;  // points per preview pixel
    EXPECT_NEAR(file_zoom_percent(max_view_scale(fit, 2.0, k), 2.0, k), 800.0, 1e-9);
    EXPECT_NEAR(file_zoom_percent(max_view_scale(fit, 1.0, k), 1.0, k), 800.0, 1e-9);
    // A tiny photo whose fit is beyond 800% stays at its fit.
    const FileScale same = file_scale({40, 30}, {40, 30});
    EXPECT_DOUBLE_EQ(max_view_scale(20.0, 1.0, same), 20.0);
    // The steps run up to 800% and stop there.
    EXPECT_FLOAT_EQ(view_step_zoom(6.0f, 0.3f, 1), 8.0f);
    EXPECT_FLOAT_EQ(view_step_zoom(8.0f, 0.3f, 1), 8.0f);
}

TEST(DetailView, LabelRoundsToWholePercent) {
    EXPECT_EQ(file_zoom_label(100.0), "100%");
    EXPECT_EQ(file_zoom_label(35.49), "35%");
    EXPECT_EQ(file_zoom_label(35.5), "36%");
    EXPECT_EQ(file_zoom_label(99.6), "100%");
    EXPECT_EQ(file_zoom_label(800.0), "800%");
    // The same rounding as view_zoom_label, without " of preview".
    EXPECT_EQ(file_zoom_label(12.5), view_zoom_label(0.125f, 100, 100));
    EXPECT_EQ(file_zoom_label(37.5), view_zoom_label(0.375f, 100, 100));
}

TEST(DetailView, FitOnASixtyMegapixelPhotoNeedsNoRealPixels) {
    // A 1280 x 800 pt canvas on a 2x screen: Fit puts fewer than one device pixel on each preview
    // pixel, so nothing changes.
    const PixelSize preview = preview_of(file_60mp);
    const ViewGeometry g{0.0f, 0.0f, 1280.0f, 800.0f, 2.0f, preview.width, preview.height};
    const double fit_points = static_cast<double>(view_fit_zoom(g)) / g.density;
    EXPECT_LT(fit_points * g.density, 1.0);
    EXPECT_FALSE(real_pixels_needed(fit_points, g.density));
}

TEST(DetailView, RealPixelsAreNeededOnlyPastTheTolerance) {
    // Device pixels per preview pixel, as points per preview pixel on a 2x screen.
    const auto needed = [](double device_per_preview) {
        return real_pixels_needed(device_per_preview / 2.0, 2.0);
    };
    EXPECT_FALSE(needed(1.0));
    EXPECT_FALSE(needed(1.0 + 0.5 * real_pixels_tolerance));
    EXPECT_TRUE(needed(1.0 + 2.0 * real_pixels_tolerance));
    EXPECT_FALSE(needed(0.5));
    EXPECT_TRUE(needed(4.0));
    EXPECT_FALSE(real_pixels_needed(1.0, 1.0));
    EXPECT_TRUE(real_pixels_needed(1.001, 1.0));
}

}  // namespace

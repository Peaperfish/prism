#include <gtest/gtest.h>

#include <optional>

#include "prism/frame.hpp"
#include "prism/gui/detail_view.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/resize.hpp"

// Real-pixel zoom's pure maths (#37): zoom as a percent of the file, whether real pixels are
// wanted, the region they come from and its budget, the size it is drawn at, the "· preview"
// chip, and the words.

namespace {

using prism::Frame;
using prism::FrameBox;
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

// A 100 x 100 preview of a 250 x 250 framed picture: 2.5 file pixels per preview pixel, so whole
// preview pixels land in the middle of file pixels.
constexpr PixelSize small_preview{100, 100};
constexpr PixelSize small_full{250, 250};

TEST(DetailView, RegionRoundsOutward) {
    // Preview [25, 75) is file [62.5, 187.5): both half pixels are taken in.
    EXPECT_EQ(file_region({25.0, 25.0, 75.0, 75.0}, small_preview, small_full),
              (FrameBox{62, 62, 126, 126}));
    // Just past a pixel's edge takes that pixel; on the edge doesn't.
    EXPECT_EQ(file_region({40.0, 40.4, 60.0, 60.4}, small_preview, small_full),
              (FrameBox{100, 101, 50, 50}));
    EXPECT_EQ(file_region({40.0, 40.0, 60.0, 60.0}, small_preview, small_full),
              (FrameBox{100, 100, 50, 50}));
}

TEST(DetailView, RegionUsesEachAxisScale) {
    // 7167 x 4778 over 1918 x 1279 (the -12 degree frame above): y's scale is not x's.
    const PixelSize preview{1918, 1279};
    const PixelSize full{7167, 4778};
    const std::optional<FrameBox> r = file_region({1000.0, 1000.0, 1001.0, 1001.0}, preview, full);
    ASSERT_TRUE(r);
    // x: 1000 * 7167 / 1918 = 3736.70..., 1001 * ... = 3740.44...; y: 1000 * 4778 / 1279 =
    // 3735.73..., 1001 * ... = 3739.47...
    EXPECT_EQ(*r, (FrameBox{3736, 3735, 5, 5}));
    // The bottom right corner of the preview is the framed picture's on both axes.
    EXPECT_EQ(file_region({1917.5, 1278.5, 1918.0, 1279.0}, preview, full),
              (FrameBox{7165, 4776, 2, 2}));
}

TEST(DetailView, RegionIsClampedAtEveryEdge) {
    const auto region = [](double x0, double y0, double x1, double y1) {
        return file_region({x0, y0, x1, y1}, small_preview, small_full);
    };
    EXPECT_EQ(region(-10.0, 40.0, 60.0, 60.0), (FrameBox{0, 100, 150, 50}));    // left
    EXPECT_EQ(region(40.0, -10.0, 60.0, 60.0), (FrameBox{100, 0, 50, 150}));    // top
    EXPECT_EQ(region(40.0, 40.0, 110.0, 60.0), (FrameBox{100, 100, 150, 50}));  // right
    EXPECT_EQ(region(40.0, 40.0, 60.0, 110.0), (FrameBox{100, 100, 50, 150}));  // bottom
    EXPECT_EQ(region(-1.0, -1.0, 101.0, 101.0), (FrameBox{0, 0, 250, 250}));    // all
    // Wholly off the picture, or empty: nothing.
    EXPECT_FALSE(region(-20.0, 40.0, -10.0, 60.0));
    EXPECT_FALSE(region(40.0, 110.0, 60.0, 120.0));
    EXPECT_FALSE(region(50.0, 50.0, 50.0, 60.0));
    EXPECT_FALSE(file_region({0.0, 0.0, 10.0, 10.0}, {0, 100}, small_full));
}

TEST(DetailView, VisibleRegionFollowsTheView) {
    // A 50 x 50 pt canvas at 1x, the 100 x 100 preview at one device pixel per preview pixel,
    // centred on (50, 50): preview [25, 75) is on the canvas.
    const ViewGeometry g{10.0f, 20.0f, 50.0f, 50.0f, 1.0f, 100, 100};
    ViewState s{false, 1.0f, 50.0f, 50.0f};
    EXPECT_EQ(visible_file_region(s, g, small_full), (FrameBox{62, 62, 126, 126}));
    // Panned to the top left corner: the region starts at the picture's corner.
    s.centre_x = 25.0f;
    s.centre_y = 25.0f;
    EXPECT_EQ(visible_file_region(s, g, small_full), (FrameBox{0, 0, 125, 125}));
    // Fit: the whole picture.
    EXPECT_EQ(visible_file_region(ViewState{}, g, small_full), (FrameBox{0, 0, 250, 250}));
    // No canvas: nothing.
    EXPECT_FALSE(
        visible_file_region(s, ViewGeometry{0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 100, 100}, small_full));
}

TEST(DetailView, FitOnALargeRetinaCanvasKeepsThePreview) {
    // 1700 x 1100 pt at 2x: Fit enlarges the preview's pixels, but the visible region is the whole
    // 60 MP picture, over the 30 MP cap, so nothing is rendered and the preview stays.
    const PixelSize preview = preview_of(file_60mp);
    const ViewGeometry g{0.0f, 0.0f, 1700.0f, 1100.0f, 2.0f, preview.width, preview.height};
    EXPECT_TRUE(real_pixels_needed(static_cast<double>(view_fit_zoom(g)) / g.density, g.density));
    const std::optional<FrameBox> r = visible_file_region(ViewState{}, g, file_60mp);
    ASSERT_TRUE(r);
    EXPECT_EQ(*r, (FrameBox{0, 0, file_60mp.width, file_60mp.height}));
    EXPECT_EQ(region_budget(*r), RegionBudget::never);
    EXPECT_FALSE(region_renders_now(region_budget(*r), false));
}

TEST(DetailView, DetailRegionSnapsToCellsWithAMarginWhenIdle) {
    const PixelSize full{2000, 1000};
    // [300, 700) x [200, 400) snaps out to [256, 768) x [128, 512).
    const FrameBox visible{300, 200, 400, 200};
    EXPECT_EQ(detail_region(visible, full, false), (FrameBox{256, 128, 512, 384}));
    // Idle: one more cell on each side.
    EXPECT_EQ(detail_region(visible, full, true), (FrameBox{128, 0, 768, 640}));
    // Edges already on a cell stay there.
    EXPECT_EQ(detail_region({256, 128, 256, 128}, full, false), (FrameBox{256, 128, 256, 128}));
    // Clamped at every edge: the top left and the bottom right, idle and not.
    EXPECT_EQ(detail_region({10, 10, 20, 20}, full, true), (FrameBox{0, 0, 256, 256}));
    EXPECT_EQ(detail_region({1990, 990, 10, 10}, full, false), (FrameBox{1920, 896, 80, 104}));
    EXPECT_EQ(detail_region({1990, 990, 10, 10}, full, true), (FrameBox{1792, 768, 208, 232}));
}

TEST(DetailView, BudgetIsLiveUpToEightMegapixels) {
    EXPECT_EQ(region_budget({0, 0, 4000, 2000}), RegionBudget::live);  // exactly 8 MP
    EXPECT_EQ(region_budget({0, 0, 8'000'001, 1}), RegionBudget::on_release);
    EXPECT_EQ(region_budget({0, 0, 1, 1}), RegionBudget::live);
    EXPECT_EQ(region_budget({0, 0, 0, 0}), RegionBudget::live);
    EXPECT_TRUE(region_renders_now(RegionBudget::live, true));
    EXPECT_TRUE(region_renders_now(RegionBudget::live, false));
}

TEST(DetailView, BudgetRendersOnReleaseUpToThirtyMegapixels) {
    EXPECT_EQ(region_budget({0, 0, 6000, 5000}), RegionBudget::on_release);  // exactly 30 MP
    EXPECT_EQ(region_budget({0, 0, 30'000'001, 1}), RegionBudget::never);
    // 2^31 - 1 squared doesn't overflow.
    EXPECT_EQ(region_budget({0, 0, 2147483647, 2147483647}), RegionBudget::never);
    EXPECT_FALSE(region_renders_now(RegionBudget::on_release, true));
    EXPECT_TRUE(region_renders_now(RegionBudget::on_release, false));
    EXPECT_FALSE(region_renders_now(RegionBudget::never, true));
    EXPECT_FALSE(region_renders_now(RegionBudget::never, false));
}

}  // namespace

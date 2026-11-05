#include "prism/gui/view_transform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "prism/gui/layout.hpp"
#include "prism/gui/test_strip.hpp"

namespace prism::gui {
namespace {

// The default window at 2x: a canvas of 950 x 750 points with its corner off the origin, and a 3:2
// preview. Fit is 1900 / 2560 = 0.742.
ViewGeometry geometry(float density = 2.0f) {
    ViewGeometry g;
    g.x = 10.0f;
    g.y = 30.0f;
    g.width = 950.0f;
    g.height = 750.0f;
    g.density = density;
    g.image_width = 2560;
    g.image_height = 1707;
    return g;
}

ViewState zoomed(float zoom, float centre_x, float centre_y) {
    ViewState s;
    s.fit = false;
    s.zoom = zoom;
    s.centre_x = centre_x;
    s.centre_y = centre_y;
    return s;
}

float device_fraction(float screen, float density) {
    const float device = screen * density;
    return std::fabs(device - std::round(device));
}

TEST(ViewTransform, FitIsTheSmallerOfTheTwoRatios) {
    const ViewGeometry g = geometry();
    EXPECT_FLOAT_EQ(view_fit_zoom(g), 1900.0f / 2560.0f);
    ViewGeometry tall = g;
    tall.image_width = 1707;
    tall.image_height = 2560;
    EXPECT_FLOAT_EQ(view_fit_zoom(tall), 1500.0f / 2560.0f);
}

// What the canvas drew before the view existed: fit, centred, with no snapping. The picture must
// stay there, up to the snapping, for every size and density.
TEST(ViewTransform, FitIsWhereTheCanvasAlwaysPutThePicture) {
    for (const float density : {1.0f, 1.25f, 1.5f, 2.0f, 3.0f}) {
        for (const float width : {1.0f, 333.0f, 640.5f, 950.0f, 1711.3f, 3840.0f}) {
            for (const float height : {1.0f, 217.0f, 480.25f, 750.0f, 2160.0f}) {
                for (const auto& [image_w, image_h] :
                     {std::pair{2560, 1707}, std::pair{1707, 2560}, std::pair{800, 800},
                      std::pair{1, 1}, std::pair{2560, 12}}) {
                    ViewGeometry g = geometry(density);
                    g.x = 13.25f;
                    g.y = 41.75f;
                    g.width = width;
                    g.height = height;
                    g.image_width = image_w;
                    g.image_height = image_h;
                    const float scale = std::min(width / static_cast<float>(image_w),
                                                 height / static_cast<float>(image_h));
                    const float shown_x = static_cast<float>(image_w) * scale;
                    const float shown_y = static_cast<float>(image_h) * scale;
                    const float at_x = g.x + (width - shown_x) / 2;
                    const float at_y = g.y + (height - shown_y) / 2;
                    const ViewPlacement p = view_placement(ViewState{}, g);
                    const float one_device_pixel = 1.0f / density;
                    EXPECT_LE(std::fabs(p.at.x - at_x), one_device_pixel);
                    EXPECT_LE(std::fabs(p.at.y - at_y), one_device_pixel);
                    EXPECT_NEAR(p.shown.x, shown_x, shown_x * 1e-5f);
                    EXPECT_NEAR(p.shown.y, shown_y, shown_y * 1e-5f);
                }
            }
        }
    }
}

TEST(ViewTransform, TheCornerIsOnAWholeDevicePixel) {
    for (const float density : {1.0f, 1.25f, 2.0f}) {
        const ViewGeometry g = geometry(density);
        for (const ViewState& s : {ViewState{}, zoomed(1.0f, 1000.3f, 700.7f),
                                   zoomed(2.0f, 901.1f, 650.9f), zoomed(3.7f, 1500.2f, 900.4f)}) {
            const ViewPlacement p = view_placement(s, g);
            EXPECT_LT(device_fraction(p.at.x, density), 2e-3f) << density;
            EXPECT_LT(device_fraction(p.at.y, density), 2e-3f) << density;
        }
    }
}

TEST(ViewTransform, ZoomOneIsOneImagePixelPerDevicePixel) {
    const ViewGeometry g = geometry(2.0f);
    const ViewPlacement p = view_placement(zoomed(1.0f, 1280.0f, 853.5f), g);
    EXPECT_FLOAT_EQ(p.shown.x * g.density, 2560.0f);
    EXPECT_FLOAT_EQ(p.shown.y * g.density, 1707.0f);
    const ViewGeometry single = geometry(1.0f);
    EXPECT_FLOAT_EQ(view_placement(zoomed(1.0f, 1280.0f, 853.5f), single).shown.x, 2560.0f);
}

TEST(ViewTransform, ScreenAndImageRoundTrip) {
    const ViewGeometry g = geometry();
    for (const ViewState& s : {ViewState{}, zoomed(1.0f, 1000.0f, 800.0f),
                               zoomed(4.0f, 300.0f, 1500.0f), zoomed(8.0f, 1280.0f, 853.0f)}) {
        for (const ViewPoint screen : {ViewPoint{10.0f, 30.0f}, ViewPoint{480.0f, 400.0f},
                                       ViewPoint{959.0f, 779.0f}}) {
            const ViewPoint image = view_screen_to_image(s, g, screen);
            const ViewPoint back = view_image_to_screen(s, g, image);
            EXPECT_NEAR(back.x, screen.x, 1e-2f);
            EXPECT_NEAR(back.y, screen.y, 1e-2f);
        }
    }
}

TEST(ViewTransform, TheCentreIsAtTheMiddleOfTheCanvas) {
    const ViewGeometry g = geometry();
    const ViewPoint middle = view_image_to_screen(zoomed(2.0f, 1000.0f, 800.0f), g,
                                                  {1000.0f, 800.0f});
    EXPECT_NEAR(middle.x, g.x + g.width / 2, 0.5f);
    EXPECT_NEAR(middle.y, g.y + g.height / 2, 0.5f);
}

TEST(ViewTransform, ThePixelAtAPointIsWhole) {
    const ViewGeometry g = geometry();
    const ViewState s = zoomed(4.0f, 1000.0f, 800.0f);
    const ViewPoint on_pixel = view_image_to_screen(s, g, {1000.5f, 800.5f});
    const std::optional<ViewPixel> pixel = view_pixel_at(s, g, on_pixel);
    ASSERT_TRUE(pixel.has_value());
    EXPECT_EQ(*pixel, (ViewPixel{1000, 800}));
}

TEST(ViewTransform, NoPixelOffThePictureOrOffTheCanvas) {
    const ViewGeometry g = geometry();
    // At fit, above and below a 3:2 picture on this canvas is empty.
    EXPECT_FALSE(view_pixel_at(ViewState{}, g, {480.0f, 31.0f}).has_value());
    EXPECT_TRUE(view_pixel_at(ViewState{}, g, {480.0f, 400.0f}).has_value());
    // Zoomed in, the picture runs past the canvas, but there is no pixel under the cursor there.
    const ViewState s = zoomed(4.0f, 1280.0f, 853.0f);
    EXPECT_FALSE(view_pixel_at(s, g, {g.x - 5.0f, 400.0f}).has_value());
    EXPECT_FALSE(view_pixel_at(s, g, {g.x + g.width, 400.0f}).has_value());
}

TEST(ViewTransform, TheFirstAndLastPixelAreReachable) {
    const ViewGeometry g = geometry();
    const ViewState s = zoomed(4.0f, 0.0f, 0.0f);  // settles with the top left corner in view
    const std::optional<ViewPixel> first = view_pixel_at(s, g, {g.x, g.y});
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, (ViewPixel{0, 0}));
    const ViewState end = zoomed(4.0f, 99999.0f, 99999.0f);
    const std::optional<ViewPixel> last =
        view_pixel_at(end, g, {g.x + g.width - 0.25f, g.y + g.height - 0.25f});
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(*last, (ViewPixel{2559, 1706}));
}

TEST(ViewTransform, ZoomingKeepsThePointUnderTheAnchor) {
    const ViewGeometry g = geometry();
    const ViewState from = zoomed(2.0f, 1280.0f, 853.0f);
    const ViewPoint anchor{300.0f, 500.0f};
    const ViewPoint before = view_screen_to_image(from, g, anchor);
    const ViewState to = view_zoomed_to(from, g, 4.0f, anchor);
    EXPECT_FLOAT_EQ(to.zoom, 4.0f);
    const ViewPoint after = view_screen_to_image(to, g, anchor);
    // Half a device pixel of snapping at each end, at 4 and 2 device pixels per image pixel.
    EXPECT_NEAR(after.x, before.x, 0.5f);
    EXPECT_NEAR(after.y, before.y, 0.5f);
}

TEST(ViewTransform, ZoomingInFromFitKeepsThePointUnderTheAnchor) {
    const ViewGeometry g = geometry();
    const ViewPoint anchor{480.0f, 400.0f};
    const ViewPoint before = view_screen_to_image(ViewState{}, g, anchor);
    const ViewState to = view_zoomed_to(ViewState{}, g, 3.0f, anchor);
    EXPECT_FALSE(to.fit);
    const ViewPoint after = view_screen_to_image(to, g, anchor);
    EXPECT_NEAR(after.x, before.x, 1.0f);
    EXPECT_NEAR(after.y, before.y, 1.0f);
}

TEST(ViewTransform, RepeatedZoomingDoesNotDrift) {
    const ViewGeometry g = geometry();
    const ViewPoint anchor{300.0f, 500.0f};
    ViewState s = zoomed(2.0f, 1280.0f, 853.0f);
    const ViewPoint before = view_screen_to_image(s, g, anchor);
    for (int i = 0; i < 40; ++i) {
        s = view_zoomed_to(s, g, 4.0f, anchor);
        s = view_zoomed_to(s, g, 2.0f, anchor);
    }
    const ViewPoint after = view_screen_to_image(s, g, anchor);
    EXPECT_NEAR(after.x, before.x, 1.0f);
    EXPECT_NEAR(after.y, before.y, 1.0f);
}

TEST(ViewTransform, ZoomingWithoutAnAnchorKeepsTheMiddle) {
    const ViewGeometry g = geometry();
    const ViewState to = view_zoomed_to(zoomed(2.0f, 1000.0f, 800.0f), g, 4.0f);
    EXPECT_FLOAT_EQ(to.centre_x, 1000.0f);
    EXPECT_FLOAT_EQ(to.centre_y, 800.0f);
}

TEST(ViewTransform, ZoomingToOrBelowFitIsFit) {
    const ViewGeometry g = geometry();
    const ViewState from = zoomed(2.0f, 1000.0f, 800.0f);
    EXPECT_TRUE(view_zoomed_to(from, g, view_fit_zoom(g)).fit);
    EXPECT_TRUE(view_zoomed_to(from, g, 0.1f).fit);
    EXPECT_TRUE(view_zoomed_to(from, g, 0.0f).fit);
    EXPECT_TRUE(view_zoomed_to(from, g, -1.0f).fit);
}

TEST(ViewTransform, ZoomStopsAtTheMaximum) {
    const ViewGeometry g = geometry();
    EXPECT_FLOAT_EQ(view_zoomed_to(ViewState{}, g, 100.0f).zoom, view_zoom_max);
}

TEST(ViewTransform, ATinyPhotoStaysAtItsFit) {
    ViewGeometry g = geometry();
    g.image_width = 16;
    g.image_height = 16;
    const float fit = view_fit_zoom(g);
    ASSERT_GT(fit, view_zoom_max);
    EXPECT_TRUE(view_zoomed_to(ViewState{}, g, view_zoom_max).fit);
    EXPECT_TRUE(view_zoomed_to(ViewState{}, g, 1.0f).fit);
}

TEST(ViewTransform, TheCanvasStaysCovered) {
    const ViewGeometry g = geometry();
    const ViewRect canvas = view_canvas_rect(g);
    for (const ViewState& s : {zoomed(4.0f, -5000.0f, -5000.0f), zoomed(4.0f, 9000.0f, 9000.0f)}) {
        const ViewPlacement p = view_placement(s, g);
        EXPECT_LE(p.at.x, canvas.min.x + 0.5f);
        EXPECT_LE(p.at.y, canvas.min.y + 0.5f);
        EXPECT_GE(p.at.x + p.shown.x, canvas.max.x - 0.5f);
        EXPECT_GE(p.at.y + p.shown.y, canvas.max.y - 0.5f);
    }
}

TEST(ViewTransform, AnAxisSmallerThanTheCanvasIsCentred) {
    ViewGeometry g = geometry();
    g.height = 1200.0f;  // tall canvas: a 3:2 picture at 100% is narrower than this
    const ViewState settled = view_settled(zoomed(1.0f, 5.0f, 5.0f), g);
    EXPECT_FLOAT_EQ(settled.centre_y, 853.5f);  // centred
    EXPECT_GT(settled.centre_x, 5.0f);          // moved to keep the canvas covered
}

TEST(ViewTransform, PanningMovesThePictureWithTheHand) {
    const ViewGeometry g = geometry();
    const ViewState from = zoomed(2.0f, 1280.0f, 853.0f);
    const ViewState to = view_panned_by(from, g, 20.0f, -10.0f);
    const ViewPlacement a = view_placement(from, g);
    const ViewPlacement b = view_placement(to, g);
    EXPECT_NEAR(b.at.x - a.at.x, 20.0f, 0.5f);
    EXPECT_NEAR(b.at.y - a.at.y, -10.0f, 0.5f);
}

TEST(ViewTransform, PanningStopsAtTheEdge) {
    const ViewGeometry g = geometry();
    const ViewState far = view_panned_by(zoomed(2.0f, 1280.0f, 853.0f), g, 1e6f, 1e6f);
    const ViewPlacement p = view_placement(far, g);
    EXPECT_NEAR(p.at.x, g.x, 0.5f);
    EXPECT_NEAR(p.at.y, g.y, 0.5f);
}

TEST(ViewTransform, PanningAtFitDoesNothing) {
    const ViewGeometry g = geometry();
    const ViewState s = view_panned_by(ViewState{}, g, 50.0f, 50.0f);
    EXPECT_TRUE(s.fit);
}

TEST(ViewTransform, AWindowResizeKeepsTheZoomAndCentre) {
    ViewGeometry g = geometry();
    const ViewState s = zoomed(2.0f, 1280.0f, 853.0f);
    g.width = 700.0f;
    g.height = 500.0f;
    const ViewState settled = view_settled(s, g);
    EXPECT_FALSE(settled.fit);
    EXPECT_FLOAT_EQ(settled.zoom, 2.0f);
    EXPECT_FLOAT_EQ(settled.centre_x, 1280.0f);
    EXPECT_FLOAT_EQ(settled.centre_y, 853.0f);
}

TEST(ViewTransform, ACanvasThatGrowsPastTheZoomGoesBackToFit) {
    ViewGeometry g = geometry();
    const ViewState s = zoomed(1.0f, 1280.0f, 853.0f);
    EXPECT_FALSE(view_settled(s, g).fit);
    g.width = 2000.0f;
    g.height = 1500.0f;  // 1:1 now fits with room to spare
    EXPECT_TRUE(view_settled(s, g).fit);
}

TEST(ViewTransform, FitFollowsTheCanvas) {
    ViewGeometry g = geometry();
    const ViewPlacement before = view_placement(ViewState{}, g);
    g.width = 500.0f;
    const ViewPlacement after = view_placement(ViewState{}, g);
    EXPECT_LT(after.shown.x, before.shown.x);
    EXPECT_NEAR(after.at.x + after.shown.x / 2, g.x + g.width / 2, 0.5f);
}

TEST(ViewTransform, ZoomIsInDevicePixelsSoADensityChangeKeepsIt) {
    ViewGeometry g = geometry(2.0f);
    const ViewState s = zoomed(1.0f, 1280.0f, 853.0f);
    const ViewPlacement retina = view_placement(s, g);
    g.density = 1.0f;
    const ViewPlacement plain = view_placement(s, g);
    EXPECT_FLOAT_EQ(retina.shown.x * 2.0f, plain.shown.x);
    EXPECT_FALSE(view_settled(s, g).fit);
    EXPECT_FLOAT_EQ(view_settled(s, g).zoom, 1.0f);
}

TEST(ViewTransform, VisibleRectIsThePictureAtFit) {
    const ViewGeometry g = geometry();
    const ViewPlacement p = view_placement(ViewState{}, g);
    const ViewRect visible = view_visible_rect(ViewState{}, g);
    EXPECT_FLOAT_EQ(visible.min.x, std::max(p.at.x, g.x));
    EXPECT_FLOAT_EQ(visible.min.y, p.at.y);
    EXPECT_FLOAT_EQ(visible.height(), p.shown.y);
}

TEST(ViewTransform, VisibleRectIsTheCanvasWhenZoomedIn) {
    const ViewGeometry g = geometry();
    const ViewRect visible = view_visible_rect(zoomed(4.0f, 1280.0f, 853.0f), g);
    const ViewRect canvas = view_canvas_rect(g);
    EXPECT_FLOAT_EQ(visible.min.x, canvas.min.x);
    EXPECT_FLOAT_EQ(visible.max.x, canvas.max.x);
    EXPECT_FLOAT_EQ(visible.min.y, canvas.min.y);
    EXPECT_FLOAT_EQ(visible.max.y, canvas.max.y);
}

TEST(ViewTransform, IntersectingRectsThatDontMeetIsEmpty) {
    const ViewRect a{{0.0f, 0.0f}, {10.0f, 10.0f}};
    EXPECT_TRUE(view_intersect(a, {{20.0f, 0.0f}, {30.0f, 10.0f}}).empty());
    EXPECT_FALSE(view_intersect(a, {{5.0f, 5.0f}, {30.0f, 30.0f}}).empty());
}

TEST(ViewTransform, EmptyCanvasOrImageIsHarmless) {
    for (ViewGeometry g : {ViewGeometry{}, geometry()}) {
        g.width = 0.0f;
        EXPECT_FLOAT_EQ(view_fit_zoom(g), 1.0f);
        EXPECT_FLOAT_EQ(view_placement(ViewState{}, g).shown.x, 0.0f);
        EXPECT_FALSE(view_pixel_at(ViewState{}, g, {1.0f, 1.0f}).has_value());
        EXPECT_FALSE(std::isnan(view_screen_to_image(ViewState{}, g, {1.0f, 1.0f}).x));
        EXPECT_TRUE(view_zoomed_to(ViewState{}, g, 2.0f).fit);
        EXPECT_TRUE(view_panned_by(ViewState{}, g, 1.0f, 1.0f).fit);
    }
}

TEST(ViewTransform, StepsGoThroughTheStops) {
    const float fit = 0.2f;
    EXPECT_FLOAT_EQ(view_step_zoom(0.25f, fit, 1), 0.33f);
    EXPECT_FLOAT_EQ(view_step_zoom(1.0f, fit, 1), 1.5f);
    EXPECT_FLOAT_EQ(view_step_zoom(1.0f, fit, -1), 0.67f);
    EXPECT_FLOAT_EQ(view_step_zoom(6.0f, fit, 1), 8.0f);
}

TEST(ViewTransform, StepsStartFromAFitBetweenTwoStops) {
    EXPECT_FLOAT_EQ(view_step_zoom(0.742f, 0.742f, 1), 1.0f);
    EXPECT_FLOAT_EQ(view_step_zoom(0.742f, 0.742f, -1), 0.742f);  // nothing beyond fit
}

TEST(ViewTransform, SteppingOutStopsAtFit) {
    EXPECT_FLOAT_EQ(view_step_zoom(1.0f, 0.742f, -1), 0.742f);
    EXPECT_FLOAT_EQ(view_step_zoom(0.3f, 0.28f, -1), 0.28f);
}

TEST(ViewTransform, SteppingInStopsAtTheMaximum) {
    EXPECT_FLOAT_EQ(view_step_zoom(8.0f, 0.5f, 1), 8.0f);
    EXPECT_FLOAT_EQ(view_step_zoom(20.0f, 20.0f, 1), 20.0f);  // a tiny photo's fit is above it
}

TEST(ViewTransform, AZoomJustOffAStopStillMovesOn) {
    EXPECT_FLOAT_EQ(view_step_zoom(1.00001f, 0.5f, 1), 1.5f);
    EXPECT_FLOAT_EQ(view_step_zoom(0.99999f, 0.5f, -1), 0.67f);
}

TEST(ViewZoomLabel, SaysOfPreviewWhileThePreviewIsSmallerThanTheFile) {
    EXPECT_EQ(view_zoom_label(1.0f, 2560, 9566), "100% of preview");
    EXPECT_EQ(view_zoom_label(0.742f, 2560, 6000), "74% of preview");
    EXPECT_EQ(view_zoom_label(4.0f, 2560, 2561), "400% of preview");
}

TEST(ViewZoomLabel, IsPlainWhenThePreviewIsTheWholeFile) {
    EXPECT_EQ(view_zoom_label(1.0f, 2560, 2560), "100%");
    EXPECT_EQ(view_zoom_label(1.6f, 800, 800), "160%");
}

const ViewRect chip_canvas{{100.0f, 50.0f}, {900.0f, 650.0f}};

TEST(ViewChip, GoesInTheBottomRightCorner) {
    const std::optional<ViewPoint> at = view_chip_position(chip_canvas, 200.0f, 30.0f, 12.0f, {});
    ASSERT_TRUE(at.has_value());
    EXPECT_FLOAT_EQ(at->x, 900.0f - 12.0f - 200.0f);
    EXPECT_FLOAT_EQ(at->y, 650.0f - 12.0f - 30.0f);
}

TEST(ViewChip, StaysPutForWhatIsNotInItsWay) {
    // "Rendering..." in the bottom left corner, on the same line.
    const std::vector<ViewRect> taken{{{112.0f, 610.0f}, {300.0f, 638.0f}}};
    const std::optional<ViewPoint> at = view_chip_position(chip_canvas, 200.0f, 30.0f, 12.0f, taken);
    ASSERT_TRUE(at.has_value());
    EXPECT_FLOAT_EQ(at->y, 608.0f);
}

TEST(ViewChip, MovesUpPastWhatIsInItsWay) {
    const std::vector<ViewRect> taken{{{112.0f, 610.0f}, {800.0f, 638.0f}}};
    const std::optional<ViewPoint> at = view_chip_position(chip_canvas, 200.0f, 30.0f, 12.0f, taken);
    ASSERT_TRUE(at.has_value());
    EXPECT_FLOAT_EQ(at->y, 610.0f - 12.0f - 30.0f);
}

TEST(ViewChip, MovesPastSeveralInAnyOrder) {
    const std::vector<ViewRect> taken{{{700.0f, 500.0f}, {800.0f, 560.0f}},
                                      {{700.0f, 600.0f}, {800.0f, 630.0f}}};
    const std::optional<ViewPoint> at = view_chip_position(chip_canvas, 200.0f, 30.0f, 12.0f, taken);
    ASSERT_TRUE(at.has_value());
    EXPECT_FLOAT_EQ(at->y, 500.0f - 12.0f - 30.0f);
}

TEST(ViewChip, NeverOverlapsWhatItWasGiven) {
    // The five stacked labels of a portrait photo's strip, the chip about as wide at 200%.
    std::vector<ViewRect> taken;
    for (int i = 0; i < 5; ++i) {
        const float top = 60.0f + static_cast<float>(i) * 118.0f;
        taken.push_back({{112.0f, top}, {560.0f, top + 56.0f}});
    }
    taken.push_back({{112.0f, 590.0f}, {380.0f, 638.0f}});  // Rendering...
    const float width = 520.0f;
    const float height = 36.0f;
    const std::optional<ViewPoint> at = view_chip_position(chip_canvas, width, height, 12.0f, taken);
    ASSERT_TRUE(at.has_value());
    for (const ViewRect& r : taken) {
        const ViewRect chip{*at, {at->x + width, at->y + height}};
        EXPECT_TRUE(view_intersect(chip, r).empty());
    }
}

TEST(ViewChip, NothingIfThereIsNoRoom) {
    EXPECT_FALSE(view_chip_position(chip_canvas, 900.0f, 30.0f, 12.0f, {}).has_value());
    EXPECT_FALSE(view_chip_position(chip_canvas, 200.0f, 700.0f, 12.0f, {}).has_value());
    const std::vector<ViewRect> whole{chip_canvas};
    EXPECT_FALSE(view_chip_position(chip_canvas, 200.0f, 30.0f, 12.0f, whole).has_value());
}

TEST(ViewChip, IgnoresEmptyRects) {
    const std::vector<ViewRect> taken{{{700.0f, 610.0f}, {700.0f, 610.0f}}};
    const std::optional<ViewPoint> at = view_chip_position(chip_canvas, 200.0f, 30.0f, 12.0f, taken);
    ASSERT_TRUE(at.has_value());
    EXPECT_FLOAT_EQ(at->y, 608.0f);
}

// Compare: the divider divides the part of the picture on the canvas, wherever the picture is.
TEST(ViewWithCompare, TheDividerStaysOnTheCanvasWhenZoomedIn) {
    const ViewGeometry g = geometry();
    const ViewRect canvas = view_canvas_rect(g);
    for (const ViewState& s : {zoomed(4.0f, 300.0f, 300.0f), zoomed(4.0f, 2400.0f, 1600.0f)}) {
        const ViewRect visible = view_visible_rect(s, g);
        for (const float fraction : {0.05f, 0.5f, 0.95f}) {
            const SplitLayout split = split_layout(visible.min.x, visible.width(), fraction);
            EXPECT_GT(split.divider_x, canvas.min.x);
            EXPECT_LT(split.divider_x, canvas.max.x);
        }
    }
}

TEST(ViewWithCompare, BothHalvesSeeTheSameImagePointAtTheDivider) {
    const ViewGeometry g = geometry();
    const ViewState s = zoomed(4.0f, 300.0f, 300.0f);
    const ViewPlacement p = view_placement(s, g);
    const ViewRect visible = view_visible_rect(s, g);
    const SplitLayout split = split_layout(visible.min.x, visible.width(), 0.5f);
    const float u_divider = (split.divider_x - p.at.x) / p.shown.x;
    EXPECT_NEAR(u_divider * 2560.0f, view_screen_to_image(s, g, {split.divider_x, 400.0f}).x, 0.01f);
}

TEST(ViewWithCompare, AtFitTheDividerIsWhereItWas) {
    const ViewGeometry g = geometry();
    const ViewPlacement p = view_placement(ViewState{}, g);
    const ViewRect visible = view_visible_rect(ViewState{}, g);
    const SplitLayout before = split_layout(p.at.x, p.shown.x, 0.5f);
    const SplitLayout now = split_layout(visible.min.x, visible.width(), 0.5f);
    EXPECT_NEAR(now.divider_x, before.divider_x, 1.0f);
}

// The strip: the band under the cursor is found through the view, and a band that is partly off the
// canvas has a corner on it for its label.
TEST(ViewWithStrip, TheBandUnderTheCursorIsTheOneDrawnThere) {
    const ViewGeometry g = geometry();
    const ViewState s = zoomed(4.0f, 1900.0f, 800.0f);
    const std::vector<StripRect> bands = strip_bands(g.image_width, g.image_height, 5);
    for (int i = 0; i < 5; ++i) {
        const StripRect& r = bands[static_cast<std::size_t>(i)];
        const ViewPoint middle{static_cast<float>(r.x) + static_cast<float>(r.width) / 2,
                               static_cast<float>(r.y) + static_cast<float>(r.height) / 2};
        const ViewPoint screen = view_image_to_screen(s, g, middle);
        const ViewPoint back = view_screen_to_image(s, g, screen);
        EXPECT_EQ(strip_band_at(g.image_width, g.image_height, 5, back.x, back.y), i);
    }
}

TEST(ViewWithStrip, AVisibleBandHasItsCornerOnTheCanvas) {
    const ViewGeometry g = geometry();
    const ViewState s = zoomed(4.0f, 1280.0f, 853.0f);  // the middle band only, or nearly
    const ViewRect visible = view_visible_rect(s, g);
    int on_canvas = 0;
    for (const StripRect& r : strip_bands(g.image_width, g.image_height, 5)) {
        const ViewPoint a = view_image_to_screen(
            s, g, {static_cast<float>(r.x), static_cast<float>(r.y)});
        const ViewPoint b = view_image_to_screen(s, g,
                                                 {static_cast<float>(r.x + r.width),
                                                  static_cast<float>(r.y + r.height)});
        const ViewRect cut = view_intersect({a, b}, visible);
        if (cut.empty()) {
            continue;
        }
        ++on_canvas;
        EXPECT_GE(cut.min.x, visible.min.x);
        EXPECT_GE(cut.min.y, visible.min.y);
        EXPECT_LE(cut.max.x, visible.max.x);
        EXPECT_LE(cut.max.y, visible.max.y);
    }
    EXPECT_GE(on_canvas, 1);
    EXPECT_LT(on_canvas, 5);
}

}  // namespace
}  // namespace prism::gui

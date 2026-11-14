#include "prism/gui/detail_canvas.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "prism/frame.hpp"
#include "prism/gui/detail_view.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/resize.hpp"

// The canvas's real-pixel rule (#37), headless: what plan_detail_frame asks the renderer for, what
// it draws, where, and what the chip says.

namespace {

using prism::FrameBox;
using prism::PixelSize;
using namespace prism::gui;

// The 60 MP RAW file (9564 x 6376) and its 2560 px preview, on a 1500 x 1000 point canvas at 2x.
constexpr PixelSize full_60mp{9564, 6376};
constexpr PixelSize preview_60mp{2560, 1707};

ViewGeometry canvas(PixelSize preview = preview_60mp, PixelSize full = full_60mp,
                    float width = 1500.0f, float height = 1000.0f, float density = 2.0f) {
    ViewGeometry g;
    g.x = 20.0f;
    g.y = 40.0f;
    g.width = width;
    g.height = height;
    g.density = density;
    g.image_width = preview.width;
    g.image_height = preview.height;
    g.file_scale = static_cast<float>(file_scale(full, preview).shown);
    return g;
}

// The view at `percent` of the file, centred on preview pixel (cx, cy).
ViewState at_percent(const ViewGeometry& g, double percent, float cx = 1280.0f, float cy = 853.0f) {
    ViewState s;
    s.fit = false;
    s.zoom = static_cast<float>(percent / 100.0 * g.file_scale);
    s.centre_x = cx;
    s.centre_y = cy;
    return view_settled(s, g);
}

// A ready base, the picture view, no gesture, nothing shown or asked for yet.
DetailFrameInput input(double percent = 100.0, PixelSize preview = preview_60mp,
                       PixelSize full = full_60mp) {
    DetailFrameInput in;
    in.geometry = canvas(preview, full);
    in.view = at_percent(in.geometry, percent, static_cast<float>(preview.width) / 2.0f,
                         static_cast<float>(preview.height) / 2.0f);
    in.framed_preview_now = preview;
    in.framed_full = full;
    in.base_state = DetailBaseState::ready;
    in.base = 1;
    in.settings = 5;
    return in;
}

// What the app records after carrying out `plan`'s submit (the renderer admitting as predicted).
SubmittedDetail recorded(const DetailSubmit& s) {
    return {s.key, s.request.region, s.output, s.picture, s.admitted, s.hold};
}

// The frame the renderer would give back for a request made with the settings' `frame`.
ShownDetail frame_of(const DetailSubmit& s, const std::optional<prism::Frame>& frame = {}) {
    return {s.key, s.request.region, s.picture, frame};
}

// The input one frame later, with the plan carried out and its frame arrived.
DetailFrameInput settled_after(DetailFrameInput in) {
    const DetailFramePlan plan = plan_detail_frame(in);
    EXPECT_TRUE(plan.submit.has_value());
    if (plan.submit) {
        in.submitted = recorded(*plan.submit);
        in.shown = frame_of(*plan.submit, in.frame);
    }
    return in;
}

// --- Which views get real pixels -----------------------------------------------------------------

TEST(DetailCanvasView, EachFlagGivesItsViewAndNoneGivesThePicture) {
    EXPECT_EQ(canvas_view(false, false, false, false, false, false), CanvasView::picture);
    EXPECT_EQ(canvas_view(true, false, false, false, false, false), CanvasView::framing);
    EXPECT_EQ(canvas_view(false, true, false, false, false, false), CanvasView::test_strip);
    EXPECT_EQ(canvas_view(false, false, true, false, false, false), CanvasView::compare);
    EXPECT_EQ(canvas_view(false, false, false, true, false, false), CanvasView::earlier_stage);
    EXPECT_EQ(canvas_view(false, false, false, false, true, false), CanvasView::clip_overlay);
    EXPECT_EQ(canvas_view(false, false, false, false, false, true), CanvasView::stroke_mask);
    // Framing comes first: its canvas replaces every other.
    EXPECT_EQ(canvas_view(true, true, true, true, true, true), CanvasView::framing);
}

constexpr CanvasView preview_only_views[] = {CanvasView::framing,        CanvasView::test_strip,
                                             CanvasView::compare,        CanvasView::earlier_stage,
                                             CanvasView::clip_overlay,   CanvasView::stroke_mask};

TEST(DetailCanvasPlan, EachPreviewOnlyViewAsksForNothingAndDrawsNothingAtAnyZoom) {
    for (const CanvasView mode : preview_only_views) {
        SCOPED_TRACE("mode " + std::to_string(static_cast<int>(mode)) + ", real pixels at 400%");
        DetailFrameInput in = input(400.0);
        in.mode = mode;
        DetailFramePlan plan = plan_detail_frame(in);
        EXPECT_FALSE(plan.submit);
        EXPECT_FALSE(plan.draw);
        EXPECT_FALSE(plan.clear);  // nothing to clear

        // Entering the view with real pixels shown clears them.
        DetailFrameInput shown = settled_after(input(400.0));
        shown.mode = mode;
        plan = plan_detail_frame(shown);
        EXPECT_TRUE(plan.clear);
        EXPECT_FALSE(plan.submit);
        EXPECT_FALSE(plan.draw);
    }
}

TEST(DetailCanvasPlan, PreviewOnlyViewsHaveNoChipAtFit) {
    for (const CanvasView mode : preview_only_views) {
        SCOPED_TRACE("mode " + std::to_string(static_cast<int>(mode)) + ", at Fit");
        DetailFrameInput in = input();
        in.view = ViewState{};
        in.mode = mode;
        EXPECT_EQ(plan_detail_frame(in).chip, PreviewChip::none);

        // A large Retina canvas, where Fit enlarges the preview: still Fit, still no chip.
        DetailFrameInput big = in;
        big.geometry = canvas(preview_60mp, full_60mp, 3000.0f, 2000.0f, 2.0f);
        ASSERT_GT(view_fit_zoom(big.geometry), 2.0f);
        EXPECT_EQ(plan_detail_frame(big).chip, PreviewChip::none);
    }
}

TEST(DetailCanvasPlan, PreviewOnlyViewsHaveNoChipWhileThePreviewIsNotEnlarged) {
    for (const CanvasView mode : preview_only_views) {
        SCOPED_TRACE("mode " + std::to_string(static_cast<int>(mode)) + ", 26%, not at Fit");
        DetailFrameInput in = input(100.0);
        in.geometry = canvas(preview_60mp, full_60mp, 600.0f, 400.0f, 1.0f);
        in.view = at_percent(in.geometry, 26.0);  // under one device pixel per preview pixel
        ASSERT_FALSE(in.view.fit);
        in.mode = mode;
        EXPECT_EQ(plan_detail_frame(in).chip, PreviewChip::none);
    }
}

TEST(DetailCanvasPlan, PreviewOnlyViewsSayPreviewWhenEnlargedWhateverTheBase) {
    for (const CanvasView mode : preview_only_views) {
        for (const DetailBaseState state :
             {DetailBaseState::ready, DetailBaseState::reading,
              DetailBaseState::failed, DetailBaseState::too_big, DetailBaseState::off}) {
            // 400% and 1600%: no zoom limit applies.
            for (const double percent : {400.0, 1600.0}) {
                SCOPED_TRACE("mode " + std::to_string(static_cast<int>(mode)) + ", base state " +
                             std::to_string(static_cast<int>(state)) + ", " +
                             std::to_string(static_cast<int>(percent)) + "%");
                DetailFrameInput in = input(percent);
                in.base_state = state;
                in.mode = mode;
                EXPECT_EQ(plan_detail_frame(in).chip, PreviewChip::preview_only);
            }
        }
    }
}

TEST(DetailCanvasPlan, PreviewOnlyViewsOfAPhotoNoBiggerThanThePreviewHaveNoChipAtAnyZoom) {
    // A 1600 x 1067 photo whose preview is the whole of it (DetailBaseState::not_needed).
    const PixelSize whole{1600, 1067};
    for (const CanvasView mode : preview_only_views) {
        for (const double percent : {100.0, 400.0, 800.0}) {
            SCOPED_TRACE(static_cast<int>(mode));
            SCOPED_TRACE(percent);
            DetailFrameInput in = input(percent, whole, whole);
            in.base_state = DetailBaseState::not_needed;
            in.mode = mode;
            ASSERT_TRUE(real_pixels_needed(
                static_cast<double>(view_zoom(in.view, in.geometry)) / in.geometry.density,
                in.geometry.density));
            EXPECT_EQ(plan_detail_frame(in).chip, PreviewChip::none);
        }
    }
    // The same views on the 60 MP photo, at the same zoom, still say so.
    for (const CanvasView mode : preview_only_views) {
        SCOPED_TRACE("mode " + std::to_string(static_cast<int>(mode)) + ", real pixels at 400%");
        DetailFrameInput in = input(400.0);
        in.mode = mode;
        EXPECT_EQ(plan_detail_frame(in).chip, PreviewChip::preview_only);
    }
}

TEST(DetailCanvasPlan, ThePictureViewKeepsItsOwnReasonsNotThePreviewOnlyOne) {
    DetailFrameInput in = input(400.0);
    in.base_state = DetailBaseState::reading;
    EXPECT_EQ(plan_detail_frame(in).chip, PreviewChip::reading);
    in.base_state = DetailBaseState::ready;
    EXPECT_EQ(plan_detail_frame(in).chip, PreviewChip::none);
    in.view = ViewState{};
    EXPECT_EQ(plan_detail_frame(in).chip, PreviewChip::none);
}

TEST(DetailCanvasPlan, ThePictureViewZoomedInAsksForRealPixels) {
    const DetailFramePlan plan = plan_detail_frame(input(100.0));
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.submit->key, (DetailKey{1, 5}));
    EXPECT_EQ(plan.submit->picture, full_60mp);
    EXPECT_TRUE(plan.submit->admitted);
    EXPECT_FALSE(plan.draw);  // nothing has arrived yet
    EXPECT_FALSE(plan.clear);
    EXPECT_EQ(plan.chip, PreviewChip::none);  // a render under way is not a reason
}

// --- Fit and zooming out -------------------------------------------------------------------------

TEST(DetailCanvasPlan, FitAsksForNothingDrawsNothingAndHasNoChip) {
    DetailFrameInput in = input();
    in.view = ViewState{};  // fit
    in.base_state = DetailBaseState::reading;
    DetailFramePlan plan = plan_detail_frame(in);
    EXPECT_FALSE(plan.submit);
    EXPECT_FALSE(plan.draw);
    EXPECT_FALSE(plan.clear);
    EXPECT_EQ(plan.chip, PreviewChip::none);

    // A large Retina canvas, where Fit enlarges the preview (2.3 device pixels per preview pixel):
    // still nothing, whatever the base, so Fit looks as in 0.6.0.
    for (const DetailBaseState state :
         {DetailBaseState::ready, DetailBaseState::reading, DetailBaseState::too_big,
          DetailBaseState::failed, DetailBaseState::off}) {
        DetailFrameInput big = input();
        big.geometry = canvas(preview_60mp, full_60mp, 3000.0f, 2000.0f, 2.0f);
        ASSERT_GT(view_fit_zoom(big.geometry), 2.0f);
        big.view = ViewState{};
        big.base_state = state;
        plan = plan_detail_frame(big);
        EXPECT_FALSE(plan.submit);
        EXPECT_FALSE(plan.draw);
        EXPECT_EQ(plan.chip, PreviewChip::none);
    }

    // Going to Fit with real pixels shown clears them.
    DetailFrameInput shown = settled_after(input(100.0));
    shown.view = ViewState{};
    plan = plan_detail_frame(shown);
    EXPECT_TRUE(plan.clear);
    EXPECT_FALSE(plan.submit);
    EXPECT_FALSE(plan.draw);
}

TEST(DetailCanvasPlan, ZoomingOutUntilThePreviewIsNotEnlargedClears) {
    // A 600 x 400 point canvas at 1x, whose fit is 6% of the file.
    DetailFrameInput in = input(100.0);
    in.geometry = canvas(preview_60mp, full_60mp, 600.0f, 400.0f, 1.0f);
    in.view = at_percent(in.geometry, 100.0);
    in = settled_after(in);
    // 26% of the file: under one device pixel per preview pixel (the preview is 26.8% of it).
    in.view = at_percent(in.geometry, 26.0);
    ASSERT_FALSE(in.view.fit);
    const DetailFramePlan plan = plan_detail_frame(in);
    EXPECT_TRUE(plan.clear);
    EXPECT_FALSE(plan.submit);
    EXPECT_FALSE(plan.draw);
    EXPECT_EQ(plan.chip, PreviewChip::none);
}

// --- Asking only when something changed ----------------------------------------------------------

TEST(DetailCanvasPlan, NothingIsAskedForWhenNothingChanged) {
    const DetailFrameInput in = settled_after(input(100.0));
    const DetailFramePlan plan = plan_detail_frame(in);
    EXPECT_FALSE(plan.submit);
    EXPECT_FALSE(plan.clear);
    EXPECT_TRUE(plan.draw);
}

TEST(DetailCanvasPlan, EachChangeAsksAgain) {
    const DetailFrameInput base = settled_after(input(100.0));

    DetailFrameInput settings = base;
    settings.settings = 6;
    DetailFramePlan plan = plan_detail_frame(settings);
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.submit->key, (DetailKey{1, 6}));
    EXPECT_FALSE(plan.clear);

    // A pan of a few pixels stays inside the snapped region: nothing to ask for.
    DetailFrameInput small_pan = base;
    small_pan.view = view_panned_by(base.view, base.geometry, 3.0f, 2.0f);
    EXPECT_FALSE(plan_detail_frame(small_pan).submit);

    // A pan of 200 points moves the region by 400 file pixels: asked for again.
    DetailFrameInput pan = base;
    pan.view = view_panned_by(base.view, base.geometry, 200.0f, 0.0f);
    plan = plan_detail_frame(pan);
    ASSERT_TRUE(plan.submit);
    EXPECT_NE(plan.submit->request.region, base.submitted->region);

    // Another output size for the same region (the last one recorded smaller).
    DetailFrameInput output = base;
    output.submitted->output = {output.submitted->output.width / 2,
                                output.submitted->output.height / 2};
    plan = plan_detail_frame(output);
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.submit->output, base.submitted->output);

    // Another framed picture (the frame changed) with the same region.
    DetailFrameInput picture = base;
    picture.submitted->picture = {9000, 6000};
    EXPECT_TRUE(plan_detail_frame(picture).submit);

    // Another base: what was asked for and shown is cleared, and the new base asked for at once.
    DetailFrameInput other_base = base;
    other_base.base = 2;
    plan = plan_detail_frame(other_base);
    EXPECT_TRUE(plan.clear);
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.submit->key, (DetailKey{2, 5}));
    EXPECT_FALSE(plan.draw);
}

TEST(DetailCanvasPlan, AGestureAsksWithoutTheMarginAndIdleWithIt) {
    DetailFrameInput in = input(100.0);
    in.gesture = true;
    const std::optional<FrameBox> visible = visible_file_region(in.view, in.geometry, full_60mp);
    ASSERT_TRUE(visible);
    DetailFramePlan plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.submit->request.region, detail_request(*visible, full_60mp, false).region);
    EXPECT_EQ(plan.submit->request.budget, RegionBudget::live);

    // The gesture ends with the view where it was: asked again, now with the margin.
    in.submitted = recorded(*plan.submit);
    in.shown = frame_of(*plan.submit);
    in.gesture = false;
    plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.submit->request.region, detail_request(*visible, full_60mp, true).region);
    EXPECT_NE(plan.submit->request.region, in.submitted->region);
    EXPECT_TRUE(plan.draw);  // the gesture's frame stays until the wider one arrives
}

// --- The budget ----------------------------------------------------------------------------------

TEST(DetailCanvasPlan, ARegionTooBigForAGestureWaitsHidesTheFrameAndSaysSo) {
    // 50% of the file on a 3000 x 2000 device pixel canvas: about 6000 x 4000 file pixels.
    DetailFrameInput in = settled_after(input(50.0));
    ASSERT_EQ(in.submitted->admitted, true);
    in.gesture = true;
    in.settings = 6;  // a slider drag
    DetailFramePlan plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.submit->request.budget, RegionBudget::on_release);
    EXPECT_FALSE(plan.submit->admitted);
    EXPECT_FALSE(plan.draw);  // the frame of the old settings is hidden: the preview shows
    EXPECT_EQ(plan.chip, PreviewChip::region_too_big_live);

    // Refused, and still dragging: nothing asked for again, still hidden.
    in.submitted = recorded(*plan.submit);
    plan = plan_detail_frame(in);
    EXPECT_FALSE(plan.submit);
    EXPECT_FALSE(plan.draw);
    EXPECT_EQ(plan.chip, PreviewChip::region_too_big_live);

    // Let go: asked for (with the margin) and admitted. The frame from before the drag does not
    // come back meanwhile (the picture would jump back to the old settings): the preview stays
    // until the new frame arrives.
    in.gesture = false;
    plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_TRUE(plan.submit->admitted);
    EXPECT_FALSE(plan.draw);
    EXPECT_EQ(plan.chip, PreviewChip::none);
    in.submitted = recorded(*plan.submit);
    EXPECT_FALSE(plan_detail_frame(in).draw);  // still on its way
    in.shown = frame_of(*plan.submit);
    EXPECT_TRUE(plan_detail_frame(in).draw);  // arrived

    // The next drag (live now, nothing refused) shows real pixels a moment behind again.
    in.settings = 7;
    in.view = at_percent(in.geometry, 100.0);
    plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_TRUE(plan.submit->admitted);
    EXPECT_FALSE(plan.submit->hold);
    EXPECT_TRUE(plan.draw);
}

TEST(DetailCanvasPlan, APanIntoARegionTooBigForTheGestureHidesEvenACurrentFrame) {
    // Real pixels of the current settings, then a drag that pans: the new region is over 8 MP, so
    // it waits, and the frame (of the right settings, but now a refused request behind it) hides.
    DetailFrameInput in = settled_after(input(50.0));
    in.gesture = true;
    in.view = view_panned_by(in.view, in.geometry, 200.0f, 0.0f);
    const DetailFramePlan plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_FALSE(plan.submit->admitted);
    EXPECT_EQ(in.shown->key.settings, in.settings);
    EXPECT_FALSE(plan.draw);
    EXPECT_EQ(plan.chip, PreviewChip::region_too_big_live);
}

TEST(DetailCanvasPlan, ARefusedRequestIsAskedAgainWhenTheGestureEndsEvenIfNothingElseChanged) {
    // A 24 MP photo at 100% on a canvas just smaller than it: the region is the whole photo with
    // or without the margin, so letting go changes nothing but the gesture.
    constexpr PixelSize full{6000, 4000};
    constexpr PixelSize preview{2560, 1707};
    DetailFrameInput in = input(100.0, preview, full);
    in.geometry = canvas(preview, full, 2950.0f, 1975.0f, 2.0f);
    in.view = at_percent(in.geometry, 100.0, 1280.0f, 853.0f);
    ASSERT_FALSE(in.view.fit);
    in.gesture = true;
    DetailFramePlan plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    ASSERT_EQ(plan.submit->request.region, (FrameBox{0, 0, 6000, 4000}));
    ASSERT_EQ(plan.submit->request.budget, RegionBudget::on_release);
    EXPECT_FALSE(plan.submit->admitted);
    in.submitted = recorded(*plan.submit);
    EXPECT_FALSE(plan_detail_frame(in).submit);  // still dragging: not asked again

    in.gesture = false;
    plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.submit->request.region, in.submitted->region);  // the same region
    EXPECT_TRUE(plan.submit->admitted);
}

TEST(DetailCanvasPlan, TheGestureReasonOnlyShowsWhenThePreviewIsWhatIsDrawn) {
    // A gesture over 8 MP whose region was rendered (admitted before the gesture made it wait, say)
    // and has not changed since: the frame stays drawn, so the chip has no reason to give.
    DetailFrameInput in = input(50.0);
    in.gesture = true;
    const DetailFramePlan first = plan_detail_frame(in);
    ASSERT_TRUE(first.submit);
    ASSERT_EQ(first.submit->request.budget, RegionBudget::on_release);
    in.submitted = recorded(*first.submit);
    in.submitted->admitted = true;
    in.shown = frame_of(*first.submit);
    const DetailFramePlan plan = plan_detail_frame(in);
    ASSERT_FALSE(plan.submit);
    EXPECT_TRUE(plan.draw);
    EXPECT_EQ(plan.chip, PreviewChip::none);
}

TEST(DetailCanvasPlan, ARegionOverThirtyMegapixelsIsNeverAskedFor) {
    // 33% of the file, just past the fit (31%): 9090 x 6060 file pixels on the canvas, 55 MP.
    DetailFrameInput in = input(33.0);
    ASSERT_FALSE(in.view.fit);
    DetailFramePlan plan = plan_detail_frame(in);
    EXPECT_FALSE(plan.submit);
    EXPECT_FALSE(plan.draw);
    EXPECT_EQ(plan.chip, PreviewChip::region_too_big);

    // Zooming out that far with real pixels shown clears them.
    DetailFrameInput shown = settled_after(input(100.0));
    shown.view = at_percent(shown.geometry, 33.0);
    plan = plan_detail_frame(shown);
    EXPECT_TRUE(plan.clear);
    EXPECT_FALSE(plan.draw);
    EXPECT_EQ(plan.chip, PreviewChip::region_too_big);
}

// --- Which frame is drawn ------------------------------------------------------------------------

TEST(DetailCanvasPlan, AFrameOfAnotherBaseIsNeverDrawn) {
    DetailFrameInput in = settled_after(input(100.0));
    in.shown->key.base = 7;  // a frame of an earlier read
    const DetailFramePlan plan = plan_detail_frame(in);
    EXPECT_FALSE(plan.draw);
    EXPECT_TRUE(plan.clear);
    EXPECT_TRUE(plan.submit);  // the current base, asked for again after the clear
}

TEST(DetailCanvasPlan, AFrameIsDrawnWithCurrentSettingsOrWithARequestForThemOnItsWay) {
    const DetailFrameInput shown = settled_after(input(100.0));

    // The current settings.
    EXPECT_TRUE(plan_detail_frame(shown).draw);

    // Older settings, and a request for the current ones admitted: drawn (a moment behind).
    DetailFrameInput behind = shown;
    behind.settings = 6;
    DetailFramePlan plan = plan_detail_frame(behind);
    ASSERT_TRUE(plan.submit);
    EXPECT_TRUE(plan.submit->admitted);
    EXPECT_TRUE(plan.draw);
    // The same, a frame later, with that request recorded and still on its way.
    behind.submitted = recorded(*plan.submit);
    plan = plan_detail_frame(behind);
    EXPECT_FALSE(plan.submit);
    EXPECT_TRUE(plan.draw);

    // (Older settings with the newer request refused during a gesture: hidden, see
    // ARegionTooBigForAGestureWaitsHidesTheFrameAndSaysSo.)

    // A frame of another framed picture (made before the frame changed): not drawn.
    DetailFrameInput reframed = shown;
    reframed.shown->picture = {9000, 6000};
    EXPECT_FALSE(plan_detail_frame(reframed).draw);
}

TEST(DetailCanvasPlan, AFrameOfAnotherCropOfTheSameSizeIsNotDrawn) {
    // Real pixels of one crop; then undo moves the crop to another part of the photo, the same
    // size. The old frame shows another part of the photo: not drawn while the new one renders.
    prism::Frame left;
    left.left = 0.0f;
    left.right = 0.5f;
    left.aspect = prism::Aspect::free;
    prism::Frame right = left;
    right.left = 0.5f;
    right.right = 1.0f;
    DetailFrameInput in = input(100.0);
    in.frame = left;
    in = settled_after(in);
    ASSERT_TRUE(plan_detail_frame(in).draw);
    in.frame = right;
    in.settings = 6;
    const DetailFramePlan plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_TRUE(plan.submit->admitted);  // on its way, but of another part of the photo
    EXPECT_FALSE(plan.draw);
    // The same frame with other settings (a slider): drawn a moment behind, as before.
    DetailFrameInput slider = settled_after([&] {
        DetailFrameInput i = input(100.0);
        i.frame = left;
        return i;
    }());
    slider.settings = 6;
    EXPECT_TRUE(plan_detail_frame(slider).draw);
}

TEST(DetailCanvasPlan, WithoutAReadyBaseTheChipSaysWhyAndNothingIsAskedFor) {
    const std::vector<std::pair<DetailBaseState, PreviewChip>> cases{
        {DetailBaseState::reading, PreviewChip::reading},
        {DetailBaseState::failed, PreviewChip::failed},
        {DetailBaseState::too_big, PreviewChip::too_big},
        {DetailBaseState::off, PreviewChip::not_kept}};
    for (const auto& [state, chip] : cases) {
        SCOPED_TRACE(static_cast<int>(state));
        DetailFrameInput in = input(100.0);
        in.base_state = state;
        in.base = 0;
        DetailFramePlan plan = plan_detail_frame(in);
        EXPECT_FALSE(plan.submit);
        EXPECT_FALSE(plan.draw);
        EXPECT_FALSE(plan.clear);
        EXPECT_EQ(plan.chip, chip);
    }
    // The state rules even with a base number still about (the app numbers what detail_base()
    // gives; a base that isn't ready is never rendered from).
    DetailFrameInput numbered = input(100.0);
    numbered.base_state = DetailBaseState::reading;
    const DetailFramePlan reading = plan_detail_frame(numbered);
    EXPECT_FALSE(reading.submit);
    EXPECT_EQ(reading.chip, PreviewChip::reading);
    // The base dropped (another read started) with real pixels shown: cleared.
    DetailFrameInput dropped = settled_after(input(100.0));
    dropped.base_state = DetailBaseState::reading;
    dropped.base = 0;
    const DetailFramePlan plan = plan_detail_frame(dropped);
    EXPECT_TRUE(plan.clear);
    EXPECT_FALSE(plan.draw);
    EXPECT_EQ(plan.chip, PreviewChip::reading);
}

TEST(DetailCanvasPlan, ANotNeededBaseIsUsedAsAReadyOne) {
    DetailFrameInput in = input(400.0, {2000, 1333}, {2000, 1333});  // a small photo
    in.base_state = DetailBaseState::not_needed;
    const DetailFramePlan plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.submit);
    EXPECT_EQ(plan.chip, PreviewChip::none);
}

TEST(DetailCanvasPlan, NothingHappensWhileTheTextureIsAPictureOfBefore) {
    DetailFrameInput in = settled_after(input(100.0));
    in.framed_preview_now = {2177, 1453};  // the frame changed; the new preview hasn't arrived
    in.settings = 6;
    const DetailFramePlan plan = plan_detail_frame(in);
    EXPECT_FALSE(plan.submit);
    EXPECT_FALSE(plan.draw);
    EXPECT_FALSE(plan.clear);
}

// --- Where it is drawn, and how ------------------------------------------------------------------

// A turned, cropped 60 MP photo: its framed full size and framed preview size are rounded on their
// own, so the two axes' scales differ (by 0.14% here), and a single scale would put the far edge
// about 7 device pixels off at 100%.
constexpr PixelSize framed_full_7{8138, 5424};
constexpr PixelSize framed_preview_7{2177, 1453};

TEST(DetailCanvasPlan, ARegionLandsOnItsOwnDevicePixelsAlongEachAxis) {
    const FileScale k = file_scale(framed_full_7, framed_preview_7);
    ASSERT_GT(std::fabs(k.x - k.y), 0.004);
    const ViewGeometry g = canvas(framed_preview_7, framed_full_7);
    for (const double percent : {100.0, 250.0, 800.0}) {
        SCOPED_TRACE(percent);
        // Each corner of the picture in turn, so every edge is reached.
        for (const auto& [cx, cy] : std::vector<std::pair<float, float>>{
                 {0.0f, 0.0f}, {2177.0f, 1453.0f}, {0.0f, 1453.0f}, {2177.0f, 0.0f}}) {
            const ViewState s = at_percent(g, percent, cx, cy);
            const ViewPlacement p = view_placement(s, g);
            const FrameBox whole{0, 0, framed_full_7.width, framed_full_7.height};
            const ViewRect r = detail_screen_rect(s, g, framed_full_7, whole);
            // The whole picture lands exactly on the preview's rectangle, to the device pixel.
            EXPECT_FLOAT_EQ(r.min.x, p.at.x);
            EXPECT_FLOAT_EQ(r.min.y, p.at.y);
            EXPECT_NEAR(r.max.x * g.density, std::round((p.at.x + p.shown.x) * g.density), 1e-3);
            EXPECT_NEAR(r.max.y * g.density, std::round((p.at.y + p.shown.y) * g.density), 1e-3);
            // A region's edge in the middle of the picture lands where that file position is on
            // the preview, to the nearest device pixel.
            const FrameBox middle{4069, 2712, 512, 384};
            const ViewRect m = detail_screen_rect(s, g, framed_full_7, middle);
            const double want_x = p.at.x + 4069.0 / framed_full_7.width * p.shown.x;
            const double want_y = p.at.y + 2712.0 / framed_full_7.height * p.shown.y;
            EXPECT_NEAR(m.min.x * g.density, std::round(want_x * g.density), 1e-3);
            EXPECT_NEAR(m.min.y * g.density, std::round(want_y * g.density), 1e-3);
            // Every edge is on a whole device pixel.
            for (const float edge : {m.min.x, m.min.y, m.max.x, m.max.y}) {
                EXPECT_NEAR(edge * g.density, std::round(edge * g.density), 1e-3);
            }
        }
    }
}

TEST(DetailCanvasPlan, TheDrawnFrameIsWhereItsRegionIs) {
    DetailFrameInput in = settled_after(input(100.0, framed_preview_7, framed_full_7));
    const DetailFramePlan plan = plan_detail_frame(in);
    ASSERT_TRUE(plan.draw);
    const ViewRect want = detail_screen_rect(view_settled(in.view, in.geometry), in.geometry,
                                             framed_full_7, in.shown->region);
    EXPECT_FLOAT_EQ(plan.where.min.x, want.min.x);
    EXPECT_FLOAT_EQ(plan.where.min.y, want.min.y);
    EXPECT_FLOAT_EQ(plan.where.max.x, want.max.x);
    EXPECT_FLOAT_EQ(plan.where.max.y, want.max.y);
}

TEST(DetailCanvasFiltering, PhotoPixelsShowAsSquaresPast200PercentOfTheFile) {
    const float k = static_cast<float>(file_scale(full_60mp, preview_60mp).shown);
    EXPECT_FALSE(detail_shows_pixels(1.0f * k, k));
    EXPECT_FALSE(detail_shows_pixels(2.0f * k, k));
    EXPECT_FALSE(detail_shows_pixels(2.00001f * k, k));  // 200% as far as anyone can tell
    EXPECT_TRUE(detail_shows_pixels(2.01f * k, k));
    EXPECT_TRUE(detail_shows_pixels(8.0f * k, k));
    // The same zoom that shows the preview's pixels as squares (2.5 device pixels per preview
    // pixel) is 67% of the file: photo pixels are smooth there.
    EXPECT_TRUE(view_shows_pixels(2.5f));
    EXPECT_FALSE(detail_shows_pixels(2.5f, k));
    // A preview that is the file: the preview's own rule.
    EXPECT_EQ(detail_shows_pixels(2.5f, 1.0f), view_shows_pixels(2.5f));
    EXPECT_EQ(detail_shows_pixels(2.5f, 0.0f), view_shows_pixels(2.5f));

    // The plan says the same for the frame it draws.
    EXPECT_FALSE(plan_detail_frame(settled_after(input(100.0))).nearest);
    EXPECT_TRUE(plan_detail_frame(settled_after(input(400.0))).nearest);
}

// --- The base's number, and gestures -------------------------------------------------------------

TEST(DetailBaseNumber, ANewNumberForAnotherBaseOrAnotherPhoto) {
    DetailBaseNumber n;
    EXPECT_EQ(n.update(nullptr, false), 0u);
    auto a = std::make_shared<const prism::Photo>();
    const std::uint64_t first = n.update(a, false);
    EXPECT_NE(first, 0u);
    EXPECT_EQ(n.update(a, false), first);  // the same base, frame after frame
    EXPECT_EQ(n.current(), first);
    const std::uint64_t reopened = n.update(a, true);  // another photo, same object
    EXPECT_GT(reopened, first);
    auto b = std::make_shared<const prism::Photo>();
    const std::uint64_t second = n.update(b, false);
    EXPECT_GT(second, reopened);
    EXPECT_EQ(n.update(nullptr, false), 0u);  // dropped
    EXPECT_GT(n.update(b, false), second);    // back: a new number all the same
}

TEST(DetailBaseNumber, ANewObjectAtTheSameAddressIsAnotherNumber) {
    // One photo seen through two owners with no-op deleters: the same address, a control block
    // each. This is the address reuse a freed base followed by a new one can give, made certain.
    // The first owner is kept, so the seen base is not expired and only the comparison of the
    // control blocks can tell the two apart.
    prism::Photo photo;
    const auto none = [](const prism::Photo*) {};
    const std::shared_ptr<const prism::Photo> a(&photo, none);
    const std::shared_ptr<const prism::Photo> b(&photo, none);
    ASSERT_EQ(a.get(), b.get());
    ASSERT_TRUE(a.owner_before(b) || b.owner_before(a));
    DetailBaseNumber n;
    const std::uint64_t first = n.update(a, false);
    EXPECT_EQ(n.update(a, false), first);  // the same owner: the same number
    EXPECT_GT(n.update(b, false), first);  // another owner of the same address: another number
}

TEST(DetailCanvasGesture, AnyActivityIsAGesture) {
    EXPECT_FALSE(canvas_gesture({}));
    CanvasActivity a;
    a.hand_drag = true;
    EXPECT_TRUE(canvas_gesture(a));
    a = {};
    a.wheel = true;
    EXPECT_TRUE(canvas_gesture(a));
    a = {};
    a.pinch = true;
    EXPECT_TRUE(canvas_gesture(a));
    a = {};
    a.brush_stroke = true;
    EXPECT_TRUE(canvas_gesture(a));
    a = {};
    a.control_active = true;
    EXPECT_TRUE(canvas_gesture(a));
}

TEST(DetailCanvasGesture, AWheelCountsForAMomentAfterItsLastEvent) {
    EXPECT_FALSE(wheel_gesture_live(1000.0, std::nullopt));
    EXPECT_TRUE(wheel_gesture_live(1000.0, 1000.0));
    EXPECT_TRUE(wheel_gesture_live(1000.0 + wheel_gesture_ms - 1.0, 1000.0));
    EXPECT_FALSE(wheel_gesture_live(1000.0 + wheel_gesture_ms, 1000.0));
}

}  // namespace

namespace {

// --- The zoom chip and 100% ----------------------------------------------------------------------

TEST(DetailCanvasChip, TheChipReadsPercentOfTheFile) {
    const ViewGeometry g = canvas();  // fit 1.17 device pixels per preview pixel
    EXPECT_EQ(canvas_zoom_chip(ViewState{}, g, PreviewChip::none), "31%");
    EXPECT_EQ(canvas_zoom_chip(at_percent(g, 100.0), g, PreviewChip::none), "100%");
    EXPECT_EQ(canvas_zoom_chip(at_percent(g, 400.0), g, PreviewChip::reading), "400% · preview");
    EXPECT_NEAR(view_file_percent(at_percent(g, 800.0), g), 800.0, 1e-3);
    // A preview that is the file reads as the zoom itself, as 0.6.0 did ("of preview" aside).
    const ViewGeometry same = canvas({1600, 1067}, {1600, 1067}, 800.0f, 600.0f, 2.0f);
    ViewState s;
    s.fit = false;
    s.zoom = 1.5f;
    s.centre_x = 800.0f;
    s.centre_y = 533.0f;
    EXPECT_EQ(canvas_zoom_chip(s, same, PreviewChip::none), "150%");
}

TEST(DetailCanvasChip, HundredPercentIsOfTheFile) {
    const ViewGeometry g = canvas();
    EXPECT_TRUE(view_actual_size_available(g));
    EXPECT_FALSE(view_at_actual_size(ViewState{}, g));
    EXPECT_TRUE(view_at_actual_size(at_percent(g, 100.0), g));
    ViewState preview_100;  // 100% of the preview: 27% of the file
    preview_100.fit = false;
    preview_100.zoom = 1.0f;
    EXPECT_FALSE(view_at_actual_size(preview_100, g));
    // A photo smaller than the canvas in device pixels: its fit is beyond 100% already.
    const ViewGeometry small = canvas({1200, 800}, {1200, 800});
    EXPECT_FALSE(view_actual_size_available(small));
}

// --- Sample's markers decide from this frame's plan ----------------------------------------------

// A sample read of `in` (a frame's input and the plan made from it), with the pin and the cursor's
// pixel both at the file pixel in the middle of the view.
SampleReads reads_in(const DetailFrameInput& in, bool have_photo = true, bool strip = false) {
    const DetailFramePlan plan = plan_detail_frame(in);
    const FilePixel middle{full_60mp.width / 2, full_60mp.height / 2};
    SampleReadInput read;
    read.have_photo = have_photo;
    read.strip = strip;
    read.plan = plan;
    read.shown = in.shown;
    read.base = in.base;
    read.settings = in.settings;
    read.has_pin = true;
    read.pin_file = middle;
    read.hovering = true;
    read.hover_file = middle;
    return sample_reads(read);
}

TEST(DetailCanvasSample, EachFramesMarkersFollowThatFramesPlanNotTheLastOne) {
    // Frame 1: zoomed to 400%, the region asked for, nothing drawn yet: the preview's pixel.
    const DetailFrameInput first = input(400.0);
    ASSERT_FALSE(plan_detail_frame(first).draw);
    EXPECT_EQ(reads_in(first), (SampleReads{false, false}));
    // Frame 2: the frame has arrived and is drawn. The markers of this very frame are the photo
    // pixel's, not the preview's of frame 1.
    const DetailFrameInput second = settled_after(first);
    ASSERT_TRUE(plan_detail_frame(second).draw);
    EXPECT_EQ(reads_in(second), (SampleReads{true, true}));
    // Frame 3: back to Fit, where nothing is drawn. This frame's markers are the preview's at once.
    DetailFrameInput third = second;
    third.view = ViewState{};
    ASSERT_FALSE(plan_detail_frame(third).draw);
    EXPECT_EQ(reads_in(third), (SampleReads{false, false}));
    // Frame 4: a slider moved, so the frame shown is of other settings and the request is refused
    // while it is dragged: no photo pixel is read either, the same frame.
    DetailFrameInput fourth = second;
    fourth.settings = 6;
    fourth.shown->key.settings = 5;
    fourth.gesture = true;
    fourth.submitted->admitted = false;
    fourth.submitted->hold = true;
    EXPECT_EQ(reads_in(fourth), (SampleReads{false, false}));
}

TEST(DetailCanvasSample, APhotoPixelIsReadOnlyWithAPhotoNoStripAndWholePixels) {
    const DetailFrameInput drawn = settled_after(input(400.0));
    ASSERT_EQ(reads_in(drawn), (SampleReads{true, true}));
    EXPECT_EQ(reads_in(drawn, false), (SampleReads{false, false}));      // no photo kept
    EXPECT_EQ(reads_in(drawn, true, true), (SampleReads{false, false}));  // the strip is on
    // Below 100% a device pixel shows several photo pixels, none of them whole: the preview's.
    DetailFrameInput below = input();
    below.geometry = canvas(preview_60mp, full_60mp, 600.0f, 400.0f, 1.0f);
    below.view = at_percent(below.geometry, 40.0);  // 1.5 device pixels per preview pixel
    below = settled_after(below);
    const DetailFramePlan plan = plan_detail_frame(below);
    ASSERT_TRUE(plan.draw);
    ASSERT_FALSE(plan.whole_pixels);
    EXPECT_EQ(reads_in(below), (SampleReads{false, false}));
}

TEST(DetailCanvasSample, EachSlotDecidesOnItsOwnPixelAndPresence) {
    const DetailFrameInput drawn = settled_after(input(400.0));
    const DetailFramePlan plan = plan_detail_frame(drawn);
    ASSERT_TRUE(plan.draw);
    SampleReadInput in;
    in.have_photo = true;
    in.plan = plan;
    in.shown = drawn.shown;
    in.base = drawn.base;
    in.settings = drawn.settings;
    const FilePixel inside{drawn.shown->region.x0 + 1, drawn.shown->region.y0 + 1};
    const FilePixel outside{drawn.shown->region.x0 - 1, drawn.shown->region.y0 + 1};
    in.has_pin = true;
    in.pin_file = inside;
    in.hovering = true;
    in.hover_file = outside;  // not on the region drawn: the preview's
    EXPECT_EQ(sample_reads(in), (SampleReads{true, false}));
    in.pin_file = outside;
    in.hover_file = inside;
    EXPECT_EQ(sample_reads(in), (SampleReads{false, true}));
    in.has_pin = false;  // no pin: it reads nothing, whatever pixel it was given
    in.pin_file = inside;
    EXPECT_EQ(sample_reads(in), (SampleReads{false, true}));
    in.hovering = false;
    in.pin_file.reset();
    in.hover_file = inside;
    EXPECT_EQ(sample_reads(in), (SampleReads{false, false}));
    // A slot with no photo pixel found yet reads the preview.
    in.has_pin = true;
    in.hovering = true;
    in.hover_file.reset();
    EXPECT_EQ(sample_reads(in), (SampleReads{false, false}));
}

TEST(DetailCanvasSample, ASlotsMarkerIsThePhotoPixelsSquareOnlyWhileItReadsThePhoto) {
    const ViewGeometry g = canvas();
    const ViewState s = at_percent(g, 800.0);
    const FilePixel file{4782, 3188};
    const SamplePin preview{1280, 853};
    const SampleMarker photo = sample_slot_marker(s, g, full_60mp, preview, true, file);
    const SampleMarker want = file_pixel_marker(s, g, full_60mp, file);
    EXPECT_EQ(photo.square.min.x, want.square.min.x);
    EXPECT_EQ(photo.square.max.y, want.square.max.y);
    const SampleMarker from_preview = sample_slot_marker(s, g, full_60mp, preview, false, file);
    const SampleMarker want_preview = sample_marker(s, g, preview);
    EXPECT_EQ(from_preview.square.min.x, want_preview.square.min.x);
    EXPECT_EQ(from_preview.square.max.x, want_preview.square.max.x);
    EXPECT_GT(from_preview.square.width(), photo.square.width());  // a preview pixel is larger
    // Reading the photo but its pixel not found: the preview's.
    const SampleMarker none = sample_slot_marker(s, g, full_60mp, preview, true, std::nullopt);
    EXPECT_EQ(none.square.min.x, want_preview.square.min.x);
}

}  // namespace

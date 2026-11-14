#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "prism/frame.hpp"
#include "prism/gui/brush.hpp"
#include "prism/gui/canvas_input.hpp"
#include "prism/gui/detail_base.hpp"
#include "prism/gui/detail_renderer.hpp"
#include "prism/gui/detail_view.hpp"
#include "prism/gui/neutral_pick.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/image.hpp"
#include "prism/resize.hpp"

// Real-pixel zoom (#37) on the canvas: each frame, what the canvas does about real pixels, decided
// here as pure functions so it can be tested without a window. The app only carries it out: it
// asks the DetailRenderer for what plan_detail_frame says, uploads the frames it gets back, and
// draws the shown one where the plan says, over the enlarged preview.

namespace prism::gui {

// What the canvas shows, as far as real pixels go. Only the final picture in the normal view gets
// real pixels; everything else draws from the preview's own data and stays exactly as in 0.6.0
// (no request, no "· preview" on the chip).
enum class CanvasView {
    picture,        // the final picture (Output encode), nothing over it from the preview's data
    framing,        // the easel: the whole turned photo on the GPU (#58)
    test_strip,     // the test strip's bands are rendered from the preview
    compare,        // the split Compare view: its other half is a preview render
    earlier_stage,  // a stage before Output encode is viewed in the light path
    clip_overlay,   // J: the clipping overlay is the preview's clip mask
    stroke_mask,    // M in Brush mode: the stroke mask is the preview's
};

// Which view the canvas is in, from the app's state: the first that holds of framing, the test
// strip, Compare (split), an earlier stage, the clipping overlay, the stroke mask; else picture.
[[nodiscard]] CanvasView canvas_view(bool framing, bool test_strip, bool compare,
                                     bool earlier_stage, bool clip_overlay,
                                     bool stroke_mask) noexcept;

// The number DetailKey::base carries for the session's detail base: a new one whenever
// Session::detail_base() gives another photo (a pointer to another object, or none), or another
// photo is opened (whose not_needed base could be at the address of the last one's). 0 while there
// is no base. It keeps only a weak reference, so it never holds a base the session let go of, and
// an object at a freed one's address still counts as another.
class DetailBaseNumber {
public:
    // UI thread, once per frame, with session.detail_base() and whether a photo was opened since
    // the last call. Returns the number now.
    std::uint64_t update(const std::shared_ptr<const Photo>& base, bool photo_opened);
    [[nodiscard]] std::uint64_t current() const noexcept { return current_; }

private:
    std::weak_ptr<const Photo> seen_;
    std::uint64_t last_ = 0;     // the last number given
    std::uint64_t current_ = 0;  // 0: no base
};

// How long after a wheel or trackpad scroll event the canvas still counts as being zoomed or
// panned, in milliseconds: wheel events come in bursts with gaps of a frame or more, which must not
// end the gesture (the region would be asked for with its margin and dropped again at the next
// event).
inline constexpr double wheel_gesture_ms = 200.0;

// Whether a wheel or trackpad scroll still counts as under way at `now_ms`: within wheel_gesture_ms
// of the last one (`last_ms`, nothing if there has been none).
[[nodiscard]] bool wheel_gesture_live(double now_ms, std::optional<double> last_ms) noexcept;

// What is under way on the canvas, as the app knows it this frame.
struct CanvasActivity {
    bool hand_drag = false;       // the picture is being dragged (panned)
    bool wheel = false;           // wheel_gesture_live: scrolling or zooming with the wheel
    bool pinch = false;           // a trackpad pinch this frame
    bool brush_stroke = false;    // a Dodge & burn stroke is being painted
    bool control_active = false;  // a control is being dragged (control_is_active): a slider
};

// Whether a gesture is under way: any of the above. RegionBudget::on_release regions wait for none.
[[nodiscard]] bool canvas_gesture(const CanvasActivity& a) noexcept;

// The detail frame now uploaded for drawing: what DetailRenderer::poll gave.
struct ShownDetail {
    DetailKey key;
    FrameBox region;
    PixelSize picture;           // DetailFrame::picture
    std::optional<Frame> frame;  // DetailFrame::frame
    bool operator==(const ShownDetail&) const = default;
};

// The request last handed to the renderer.
struct SubmittedDetail {
    DetailKey key;
    FrameBox region;
    PixelSize output;
    PixelSize picture;      // the framed picture the region is in
    bool admitted = false;  // DetailRenderer::submit gave it a generation (not 0)
    // A request was refused during a gesture since the shown frame's settings were asked for:
    // until a frame of the current settings arrives, no frame of older ones is drawn
    // (DetailSubmit).
    bool hold = false;
    bool operator==(const SubmittedDetail&) const = default;
};

// What plan_detail_frame looks at.
struct DetailFrameInput {
    // The view, and the canvas with the picture as drawn: its image is the preview texture (the
    // framed preview), and its file_scale is set.
    ViewState view;
    ViewGeometry geometry;
    // Session::preview_base()'s size. The texture is the current picture only when it is this size;
    // after a frame change the old texture shows for a moment, and nothing is placed on it then.
    PixelSize framed_preview_now;
    // The framed picture at full size: plan_export_frame(settings.frame, detail base size).size, or
    // 0 x 0 with no detail base.
    PixelSize framed_full;
    // The current settings' frame (EditSettings::frame): a frame of older settings is drawn only
    // while it shows the same part of the photo.
    std::optional<Frame> frame;
    DetailBaseState base_state = DetailBaseState::off;
    std::uint64_t base = 0;      // DetailBaseNumber: 0 is no base
    std::uint64_t settings = 0;  // Session::settings_generation()
    bool gesture = false;        // canvas_gesture
    CanvasView mode = CanvasView::picture;
    std::optional<ShownDetail> shown;          // the frame uploaded, if any
    std::optional<SubmittedDetail> submitted;  // the last request, if any (since the last clear)
};

// What to ask the renderer for: DetailRenderRequest without the base and the settings, which the
// app adds (Session::detail_base(), Session::settings()).
struct DetailSubmit {
    DetailRequest request;
    PixelSize output;
    DetailKey key;
    PixelSize picture;  // for SubmittedDetail
    // What detail_admit will say, for the record the app keeps until submit() says.
    bool admitted = false;
    // For SubmittedDetail::hold: this request is refused, or an earlier one was and no frame of
    // the settings asked for since has arrived.
    bool hold = false;
};

// What the app does this frame, in this order: clear() the renderer and forget the shown frame and
// the last request; submit(); draw the shown frame at `where` (after drawing the preview, before
// the overlays), sampled with nearest filtering if `nearest`; give the chip `chip`.
struct DetailFramePlan {
    bool clear = false;
    std::optional<DetailSubmit> submit;
    bool draw = false;
    ViewRect where;
    bool nearest = false;
    // The frame drawn shows each file pixel whole: at 100% or more of the file (within
    // real_pixels_tolerance). Below 100% it is shrunk by area averaging, so a device pixel shows
    // several file pixels and no one of them is what is on screen.
    bool whole_pixels = false;
    PreviewChip chip = PreviewChip::none;
};

// The canvas's rule for real pixels (#37):
//
// - Only for CanvasView::picture, with the view zoomed in (not Fit) so far that the preview is
//   enlarged (real_pixels_needed). Otherwise nothing is asked for and nothing drawn, and anything
//   asked for or shown is cleared. Fit has no chip; a preview-only view that is enlarged has
//   PreviewChip::preview_only (preview_only_chip), at no zoom limit.
// - Without a usable detail base (none, or not ready): nothing is asked for or drawn, anything of
//   an old base is cleared, and the chip says why (preview_chip).
// - The request is detail_request for the visible file region, with the margin only when no
//   gesture is under way, and detail_output_size for the view. It is handed over only when it
//   differs from the last one (region, output, picture, base or settings), or the last was refused
//   during a gesture and is now admitted. Over 30 MP (RegionBudget::never) nothing is asked for,
//   anything asked for or shown is cleared, and the chip says region_too_big.
// - A frame of another base than the current one is never drawn; the request and frame of an old
//   base are cleared (and the new base asked for in the same frame).
// - The shown frame is drawn only if it is in the current framed picture (its size, and for a
//   frame of older settings the same Frame), and its settings are the current ones or the last
//   request is for the current ones and admitted (a slider drag shows real pixels a moment behind
//   rather than none). If the last request was refused during a gesture, nothing is drawn (the
//   preview shows) and the chip says region_too_big_live; that reason only shows when nothing is
//   drawn. After such a refusal only a frame of the current settings is drawn, until one arrives:
//   letting go never brings back the picture from before the drag.
// - While the texture is not yet the current picture (framed_preview_now differs), nothing is
//   asked for, drawn or cleared.
// - `where` is the frame's region taken to preview pixels by the per-axis file scale, then to the
//   screen (view_image_to_screen), each edge on a whole device pixel as view_placement puts the
//   picture's corner. `nearest` is detail_shows_pixels.
[[nodiscard]] DetailFramePlan plan_detail_frame(const DetailFrameInput& in);

// Where `region` of a `framed_full` picture lands on the screen for the view: its edges taken to
// preview pixels by the per-axis file scale (file_scale(framed_full, image size)), to the screen by
// view_image_to_screen, and each rounded to a whole device pixel.
[[nodiscard]] ViewRect detail_screen_rect(const ViewState& s, const ViewGeometry& g,
                                          PixelSize framed_full, const FrameBox& region) noexcept;

// Whether a detail frame is drawn with nearest sampling: the preview's rule (view_shows_pixels)
// applied to device pixels per file pixel rather than per preview pixel, so photo pixels show as
// squares past 200% of the file. `zoom` is device pixels per preview pixel, `file_scale` file
// pixels per preview pixel (ViewGeometry::file_scale).
[[nodiscard]] bool detail_shows_pixels(float zoom, float file_scale) noexcept;

// --- The zoom chip and 100% ----------------------------------------------------------------------

// The zoom in percent of the file for the view: 100 x device pixels per file pixel.
[[nodiscard]] double view_file_percent(const ViewState& s, const ViewGeometry& g) noexcept;

// The zoom chip's text: file_zoom_chip(view_file_percent, chip), e.g. "25%", "400% · preview".
[[nodiscard]] std::string canvas_zoom_chip(const ViewState& s, const ViewGeometry& g,
                                           PreviewChip chip);

// Whether the view is at 100% of the file (zoomed, not Fit, within view_same_zoom of it), so the
// chip's 100% is lit and the View menu's "100% (real pixels)" has nothing to do.
[[nodiscard]] bool view_at_actual_size(const ViewState& s, const ViewGeometry& g) noexcept;

// Whether 100% of the file can be chosen: not when the fit already shows the photo at 100% or
// more (a photo smaller than the canvas in device pixels).
[[nodiscard]] bool view_actual_size_available(const ViewGeometry& g) noexcept;

// --- Sample and Pick on real pixels (#37, session 7) ---------------------------------------------

// A pixel of the framed picture at full size (the detail base's framed picture).
struct FilePixel {
    int x = 0;
    int y = 0;
    bool operator==(const FilePixel&) const = default;
};

// The file pixel under a screen point: the point taken to preview pixels (view_screen_to_image),
// then to file pixels by the per-axis scale, rounded down. Nothing off the picture or the canvas
// (as view_pixel_at), or with either size empty. The drawn squares (detail_screen_rect) have their
// edges on whole device pixels, so within half a device pixel of an edge the pixel found can be the
// neighbour of the square drawn there; at 100% and above that is under half a photo pixel.
[[nodiscard]] std::optional<FilePixel> file_pixel_at(const ViewState& s, const ViewGeometry& g,
                                                     PixelSize framed_full,
                                                     ViewPoint screen) noexcept;

// The file pixel at the middle of preview pixel (x, y) of a `framed_preview` picture, for a pin
// made where only the preview was shown: it reads the photo there once real pixels are.
[[nodiscard]] FilePixel file_pixel_of(int x, int y, PixelSize framed_preview,
                                      PixelSize framed_full) noexcept;

// What Sample and Pick read.
enum class PixelSource {
    preview,  // the preview pixel, as in 0.6.0
    photo,    // the photo's own pixel (sample_photo_trace, pick_neutral_photo)
};

// Which: the photo's pixel only where real pixels of it are on the canvas now, whole, of the
// current base and settings (`plan` draws the shown frame at 100% or more,
// DetailFramePlan::whole_pixels; the frame is of the current settings, and its region holds
// `pixel`); otherwise the preview's. Below 100% the canvas shows file pixels averaged, so the
// preview's pixel (with its line about what it averages) is read, as in 0.6.0. A frame standing in
// with older settings a moment behind a drag is not read: what is read is always the current
// settings' value.
[[nodiscard]] PixelSource sample_source(const DetailFramePlan& plan,
                                        const std::optional<ShownDetail>& shown, std::uint64_t base,
                                        std::uint64_t settings, FilePixel pixel) noexcept;

// What the canvas's Sample markers and block do about the photo's own pixels this frame, decided
// from this frame's plan (plan_detail_frame) before the markers are drawn: a marker drawn from a
// decision of the frame before would show a preview pixel's square for one frame after the real
// pixels arrive or leave, and the other way round.
struct SampleReadInput {
    bool have_photo = false;  // Session::detail_base() is there
    bool strip = false;       // the test strip is on: its bands are not the photo's pixels
    DetailFramePlan plan;     // this frame's
    std::optional<ShownDetail> shown;
    std::uint64_t base = 0;      // DetailBaseNumber::current()
    std::uint64_t settings = 0;  // Session::settings_generation()
    // The pin, if there is one, and its photo pixel (none until it has one).
    bool has_pin = false;
    std::optional<FilePixel> pin_file;
    // The cursor's pixel: `hovering` if the cursor is on the picture, or no pin holds the block and
    // the last pixel read is still shown; and its photo pixel.
    bool hovering = false;
    std::optional<FilePixel> hover_file;
};

struct SampleReads {
    bool pin_photo = false;    // the pin reads (and is marked at) the photo's pixel
    bool hover_photo = false;  // the cursor's pixel does
    bool operator==(const SampleReads&) const = default;
};

// A slot reads the photo's pixel where sample_source says so (real pixels of it, whole, of the
// current base and settings, on the canvas now), with a photo and no test strip; the preview's
// pixel everywhere else, as in 0.6.0.
[[nodiscard]] SampleReads sample_reads(const SampleReadInput& in) noexcept;

// Where a Sample slot's marker goes: the photo pixel's square if the slot reads the photo (`photo`)
// and has its photo pixel, the preview pixel's otherwise (sample_marker).
[[nodiscard]] SampleMarker sample_slot_marker(const ViewState& s, const ViewGeometry& g,
                                              PixelSize framed_full, SamplePin preview, bool photo,
                                              const std::optional<FilePixel>& file) noexcept;

// `pixel` of a `from` picture taken to a `to` picture of another size (the file's size was an
// estimate until the full-size photo arrived): the pixel at the same place, by its middle, per
// axis.
[[nodiscard]] FilePixel file_pixel_rescaled(FilePixel pixel, PixelSize from, PixelSize to) noexcept;

// --- Pick's footprint: the same photo area at every zoom -----------------------------------------

// How many photo pixels across Pick's square is: the preview's 5 x 5 patch (pick_patch_size) as
// photo pixels. 5 x the photo pixels per preview pixel (scale.shown), rounded to the nearest whole
// number, plus 1 if that is even so the square has a middle pixel, and never below 5. A photo the
// preview shows whole (1 : 1) gives 5; a 9564 px wide photo on the 2560 px preview gives 19.
[[nodiscard]] int pick_footprint_size(FileScale scale) noexcept;

// The square of `size` (odd) photo pixels across centred on `centre`, in a `framed_full` picture,
// cut off at its edges, as a region. Nothing for a centre off the picture or an empty picture.
[[nodiscard]] std::optional<FrameBox> pick_footprint(PixelSize framed_full, int size,
                                                     FilePixel centre) noexcept;

// What a click at a screen point averages, whatever the zoom: the footprint around the photo pixel
// under the point. `full` is the framed picture at full size, or the preview's own size when there
// is none (framed_full empty); `size` is pick_footprint_size of the scale from the preview to it.
struct PickFootprint {
    PixelSize full;
    int size = pick_patch_size;
    FilePixel centre;
    FrameBox box;  // pick_footprint(full, size, centre)
};

// Nothing off the picture or the canvas (file_pixel_at). The view's image is the framed preview
// (g.image_width x g.image_height).
[[nodiscard]] std::optional<PickFootprint> pick_footprint_at(const ViewState& s,
                                                             const ViewGeometry& g,
                                                             PixelSize framed_full,
                                                             ViewPoint screen) noexcept;

// The footprint's box in the preview's pixels (the framed preview of `framed_preview`), by the
// per-axis scale from `framed_preview` to `full`: where Pick averages the preview below 100%
// (pick_neutral_area). Edges fall inside preview pixels. Empty if either size is.
[[nodiscard]] PatchArea pick_footprint_in_preview(const FrameBox& box, PixelSize framed_preview,
                                                  PixelSize full) noexcept;

// The ring Pick shows, at every zoom: around the footprint of `pixel` (`size` photo pixels across,
// pick_footprint), cut off at the picture's edges, on whole device pixels. Not on the canvas if the
// pixel's middle isn't, or the pixel is off the picture.
[[nodiscard]] PickMarker file_pick_marker(const ViewState& s, const ViewGeometry& g,
                                          PixelSize framed_full, FilePixel pixel,
                                          int size) noexcept;

// Where a file pixel's marker goes on the screen: its square (detail_screen_rect of the 1 x 1
// region), the centre, whether the centre is on the canvas, and shows_square past 200% of the file
// (detail_shows_pixels), as sample_marker does for a preview pixel.
[[nodiscard]] SampleMarker file_pixel_marker(const ViewState& s, const ViewGeometry& g,
                                             PixelSize framed_full, FilePixel pixel) noexcept;

// --- The brush on real pixels --------------------------------------------------------------------

// The long side, in pixels, the brush's point spacing is measured on (min_point_spacing): the whole
// photo's long side on the canvas (brush_long_side), or while real pixels are drawn (`real_pixels`)
// that long side in file pixels: brush_long_side times the file scale along the shown axis
// (file_scale(framed_full, canvas size)), rounded. Only the one-pixel floor of min_point_spacing
// depends on it: with the brush's smallest radius (0.5% of the long side, whose 0.15 x radius is
// 1/1333 of it) that floor matters only for a preview under about 1333 px across the whole photo,
// so with the 2560 px preview it changes nothing today. The strokes themselves stay in photo
// coordinates (u, v), whatever the zoom.
[[nodiscard]] int brush_spacing_long_side(const BrushCanvas& canvas, bool real_pixels,
                                          PixelSize framed_full) noexcept;

}  // namespace prism::gui

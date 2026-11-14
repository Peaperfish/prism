#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "prism/frame.hpp"
#include "prism/gui/detail_base.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/resize.hpp"

// Real-pixel zoom (#37): the view's numbers measured against the file rather than the preview.
// These are pure functions; the canvas (gui_app) asks them what to show.
//
// The view's image space stays the framed preview's pixels (view_transform.hpp), so panning, the
// brush and the framing canvas are untouched. What changes is what a zoom means: 100% is one pixel
// of the file (the framed picture at the export's own size, before any reduction) on one device
// pixel. On a 2x Retina screen a file pixel is then half a point across.
//
// Units used below:
//   points per preview pixel  the view's scale: screen units across one preview pixel. ViewState's
//                             zoom is device pixels per preview pixel, so this is that zoom divided
//                             by ViewGeometry::density.
//   density                   device pixels per screen unit (ImGui's DisplayFramebufferScale).
//   file percent              device pixels per file pixel, times 100.

namespace prism::gui {

// How many file pixels one preview pixel stands for, along each axis of the framed picture: the
// framed full size (plan_export_frame on the export's decode) over the framed preview size
// (plan_export_frame on the preview base). The two are rounded to whole pixels on their own, so
// the axes differ slightly (about 0.03% on a 60 MP photo) and each is kept.
//
// `shown` is the one the zoom is read along: the axis on which the framed preview is longer (x
// for a square). Its ratio is the most exact, since the half pixel either size can be rounded by
// is the smallest part of the longer side; and it is the side the chip's percentage is about.
struct FileScale {
    double x = 1.0;
    double y = 1.0;
    double shown = 1.0;
};

// The scale from `framed_preview` up to `framed_full`. 1 on both axes if either size is empty.
[[nodiscard]] FileScale file_scale(PixelSize framed_full, PixelSize framed_preview) noexcept;

// The zoom in percent of the file for a view showing `points_per_preview_pixel` on a screen of
// `density`: 100 * points_per_preview_pixel * density / scale.shown.
[[nodiscard]] double file_zoom_percent(double points_per_preview_pixel, double density,
                                       FileScale scale) noexcept;

// The view's scale (points per preview pixel) that shows `percent` of the file: the inverse of
// file_zoom_percent; 0 if density isn't above 0. For "100% (real pixels)" and for the zoom steps,
// which step in file percent: view_step_zoom(percent / 100, fit_percent / 100, direction) * 100
// gives the next stop, its top stop being 800%.
[[nodiscard]] double view_scale_for_file_percent(double percent, double density,
                                                 FileScale scale) noexcept;

// The chip's number: "N%", N rounded to the nearest whole percent, halves away from zero, as
// view_zoom_label rounds. No " of preview": the percent is of the file.
[[nodiscard]] std::string file_zoom_label(double percent);

// How far two device-pixels-per-preview-pixel zooms may differ and still count as the same: the
// tolerance of view_same_zoom. Zooms are stored as floats, whose rounding is about 6e-8 of the
// value, so this is far above that noise; and it is far below anything visible (at a zoom of
// 1 + 1e-4 a 2560 px preview is a quarter of a device pixel wider).
inline constexpr double real_pixels_tolerance = 1e-4;

// Whether the canvas would show preview pixels enlarged, so real pixels are wanted: more than one
// device pixel per preview pixel, by more than real_pixels_tolerance. At or below that the preview
// already has a pixel (or more) for each device pixel, and nothing changes from 0.6.0.
[[nodiscard]] bool real_pixels_needed(double points_per_preview_pixel, double density) noexcept;

// A rectangle in preview pixels (the view's image space): [x0, x1) x [y0, y1), continuous.
struct PreviewRect {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
};

// The file pixels `preview` covers, as render_region takes them: pixels of the framed picture at
// full size (`framed_full`), measured with the per-axis scale from `framed_preview`. Each edge is
// rounded outward (an edge in the middle of a file pixel takes that pixel in), then clamped to the
// framed picture. Nothing if nothing of the picture is left, either size is empty or an edge is
// NaN.
[[nodiscard]] std::optional<FrameBox> file_region(const PreviewRect& preview,
                                                  PixelSize framed_preview,
                                                  PixelSize framed_full) noexcept;

// The part of the picture on the canvas, in file pixels: view_visible_rect taken to preview pixels
// through view_screen_to_image, then file_region. The view's image is the framed preview
// (g.image_width x g.image_height).
[[nodiscard]] std::optional<FrameBox> visible_file_region(const ViewState& s, const ViewGeometry& g,
                                                          PixelSize framed_full) noexcept;

// The design's region snapping (#37 section 5): a detail render asks for the visible region with
// its edges moved outward to the nearest multiple of detail_cell file pixels, so a pan of a few
// pixels needs no new render; when no gesture is under way one more cell on each side, so a small
// pan is sharp at once. Clamped to the framed picture.
inline constexpr int detail_cell = 128;

// How many file pixels a requested region may have: the caps bound what is rendered, so they are
// measured on the requested region, snapping and margin included. Megapixels are millions of
// pixels.
inline constexpr std::int64_t region_live_max_pixels = 8'000'000;  // rendered while dragging
inline constexpr std::int64_t region_max_pixels = 30'000'000;      // rendered at all

enum class RegionBudget {
    live,        // at most 8 MP: rendered at once, also during a drag, a pan or a zoom
    on_release,  // over 8 MP, at most 30 MP: rendered only once no gesture is under way
    never,       // over 30 MP: not rendered; the preview stays
};

// What a detail render asks for, from the visible region (visible_file_region): the snapped
// region, with the margin when `idle`, and the budget of that region (an empty one is live). The
// budget is only worked out here, so it can't be measured on the visible region by mistake.
struct DetailRequest {
    FrameBox region;
    RegionBudget budget = RegionBudget::live;
};
[[nodiscard]] DetailRequest detail_request(const FrameBox& visible, PixelSize framed_full,
                                           bool idle) noexcept;

// Whether a region of this budget is rendered now, with or without a gesture under way.
[[nodiscard]] bool region_renders_now(RegionBudget budget, bool gesture) noexcept;

// The size a region's render is drawn from: what the detail worker hands the canvas.
//  - At 100% and above (one device pixel or more per file pixel, along the shown axis, within
//    real_pixels_tolerance of 100%): the region itself, so each file pixel is drawn whole.
//  - Below 100%: the region shrunk by area averaging (prism::downscale) to the device pixels it
//    covers, so the texture is never larger than the canvas in device pixels. Its long side is the
//    region's long side times the device pixels per file pixel, rounded to the nearest pixel
//    (halves up) and at least 1; the short side is downscaled_size's, so it is exactly what
//    downscale(image, long side) gives.
// Never 0 x 0: an empty region (which file_region never gives) counts as 1 x 1. A NaN density gives
// the region itself.
[[nodiscard]] PixelSize detail_output_size(const FrameBox& region, double points_per_preview_pixel,
                                           double density, FileScale scale);

// Why the zoom chip says "· preview": the canvas shows preview pixels enlarged, and real pixels
// can't take their place for a reason that lasts. A render of real pixels that is merely under way
// is not a reason: the chip would flash on every pan and slider move.
enum class PreviewChip {
    none,      // no chip: at Fit, the preview isn't enlarged, or real pixels are (or soon will be)
               // shown
    not_kept,  // DetailBaseState::off: no full-size photo is kept
    reading,   // the full-size photo is still being read
    failed,    // reading the full-size photo failed, or it ran out of memory
    too_big,   // the full-size photo would take more than a quarter of the memory
    region_too_big,       // the requested region is over 30 MP (RegionBudget::never)
    region_too_big_live,  // it is over 8 MP during a gesture (RegionBudget::on_release then)
    preview_only,         // a view drawn from the preview (J, M, Compare, an earlier stage, the
                          // easel, the test strip): it has no real pixels, whatever the base
};

// The chip's rule. `enlarged` is real_pixels_needed for the view; `at_fit` is ViewState::fit;
// `budget` is detail_request's for the visible region; `gesture` is whether a drag, pan, wheel or
// brush stroke is under way. Nothing at all when the preview isn't enlarged, or the view is at
// Fit, whatever the state: at Fit the canvas looks and reads as in 0.6.0, even where Fit enlarges
// the preview (a large Retina canvas) and real pixels can't be shown. Then a base that isn't read
// gives its reason; a ready one (or one not needed, where the preview is the full photo) gives the
// budget's.
[[nodiscard]] PreviewChip preview_chip(bool enlarged, bool at_fit, DetailBaseState base,
                                       RegionBudget budget, bool gesture) noexcept;

// The chip's rule for the views that stay on the preview (preview_only). `needed` is
// real_pixels_needed for the view and `at_fit` is ViewState::fit: the preview is enlarged when it
// is needed and the view is not at Fit, as the normal view decides (plan_detail_frame). No zoom
// limit applies. Not for a photo no bigger than the preview (`preview_is_photo`: the preview is the
// whole photo, DetailBaseState::not_needed): there is nothing more to show, as in the normal view.
// PreviewChip::preview_only then, else none.
[[nodiscard]] PreviewChip preview_only_chip(bool needed, bool at_fit,
                                            bool preview_is_photo) noexcept;

// The chip: "N%" (file_zoom_label), or "N% · preview" when `chip` gives a reason.
[[nodiscard]] std::string file_zoom_chip(double percent, PreviewChip chip);

// The View menu item that goes to 100%: "100% (real pixels)".
[[nodiscard]] std::string_view real_pixels_menu_item() noexcept;

// Explain for 100%: "100%: one pixel of the photo on each pixel of your screen, the photo at its
// full size. On a Retina screen a photo pixel is half a point across."
[[nodiscard]] std::string_view real_pixels_explain() noexcept;

// The zoom's Explain line while real pixels are drawn: "Showing the photo's own pixels."
[[nodiscard]] std::string_view real_pixels_shown_explain() noexcept;

// One Explain line per chip reason, saying why the preview is shown; empty for PreviewChip::none.
// For example "Showing the preview, enlarged: the full-size photo is still being read."
[[nodiscard]] std::string_view preview_chip_explain(PreviewChip chip) noexcept;

}  // namespace prism::gui

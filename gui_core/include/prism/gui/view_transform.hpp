#pragma once

#include <optional>
#include <span>
#include <string>

namespace prism::gui {

// Where the picture sits on the canvas: zoom and pan as pure functions, with no window involved.
//
// Three kinds of coordinates:
//   screen  ImGui's units, the ones the canvas rectangle and the mouse are given in. Points on a
//           Mac, physical pixels on Windows.
//   device  the display's real pixels: screen * ViewGeometry::density.
//   image   the preview's pixels, (0, 0) at the picture's top left corner.
//
// Zoom is in device pixels per image pixel, so 1 means one preview pixel on one display pixel on
// any display, and the picture on a Retina screen is then half as many points wide. A view is only
// a way of looking at the photo: it is never part of the settings, undo or a recipe.

struct ViewPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// A rectangle in screen coordinates. Empty if max is not beyond min on both axes.
struct ViewRect {
    ViewPoint min;
    ViewPoint max;

    [[nodiscard]] float width() const noexcept { return max.x - min.x; }
    [[nodiscard]] float height() const noexcept { return max.y - min.y; }
    [[nodiscard]] bool empty() const noexcept { return !(max.x > min.x && max.y > min.y); }
};

// The canvas and the picture this frame.
struct ViewGeometry {
    float x = 0.0f;  // the canvas, in screen coordinates
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float density = 1.0f;  // device pixels per screen unit
    int image_width = 0;
    int image_height = 0;
    // File pixels per image pixel along the axis the zoom is read on (FileScale::shown in
    // detail_view.hpp): how many pixels of the photo at its export size one preview pixel stands
    // for. 1 when the preview is the whole file. It moves where 100% and the closest zoom are
    // (view_actual_zoom, view_max_zoom): both are measured in file pixels. Not finite or not above
    // 0 counts as 1.
    float file_scale = 1.0f;
};

// What the user chose. While `fit` is on the zoom and centre are ignored: the whole picture follows
// the canvas as it changes size.
struct ViewState {
    bool fit = true;
    float zoom = 1.0f;
    float centre_x = 0.0f;  // the image point at the middle of the canvas, in image pixels
    float centre_y = 0.0f;
};

// The picture's rectangle for this frame: `at` is its top left corner, whole device pixels, so that
// at zoom 1 each preview pixel lands on exactly one display pixel.
struct ViewPlacement {
    ViewPoint at;
    ViewPoint shown;  // its size, in screen units
};

// The closest the view can go is 8 device pixels per file pixel (800% of the file): 8 device pixels
// per image pixel when the preview is the whole file, and 8 x ViewGeometry::file_scale per image
// pixel when it is smaller (view_max_zoom). Fit is the furthest out: a smaller picture would show
// nothing more. So a tiny photo whose fit is above this stays at its fit.
inline constexpr float view_zoom_max = 8.0f;

// The zoom (device pixels per image pixel) for 100% of the file: one device pixel per file pixel,
// ViewGeometry::file_scale.
[[nodiscard]] float view_actual_zoom(const ViewGeometry& g) noexcept;

// The closest zoom (device pixels per image pixel) for `g`: view_zoom_max file pixels, or the fit's
// zoom if that is closer.
[[nodiscard]] float view_max_zoom(const ViewGeometry& g) noexcept;

// The zoom at which the whole picture just fits the canvas. 1 for an empty canvas or image.
[[nodiscard]] float view_fit_zoom(const ViewGeometry& g) noexcept;

// The zoom in effect: the fit's while `fit` is on.
[[nodiscard]] float view_zoom(const ViewState& s, const ViewGeometry& g) noexcept;

// `s` made valid for `g`, for when the canvas or picture changed. The zoom stays within fit and the
// maximum, and returns to fit once the whole picture is on the canvas at it. An axis on which the
// picture is smaller than the canvas is centred; on one where it is larger, its edge can't come
// inside the canvas's edge.
[[nodiscard]] ViewState view_settled(const ViewState& s, const ViewGeometry& g) noexcept;

[[nodiscard]] ViewPlacement view_placement(const ViewState& s, const ViewGeometry& g) noexcept;

// The canvas's rectangle, and the part of the picture that is on it.
[[nodiscard]] ViewRect view_canvas_rect(const ViewGeometry& g) noexcept;
[[nodiscard]] ViewRect view_visible_rect(const ViewState& s, const ViewGeometry& g) noexcept;

// `r` cut down to `limit`. Empty if they don't meet.
[[nodiscard]] ViewRect view_intersect(const ViewRect& r, const ViewRect& limit) noexcept;

// Between screen and image coordinates, through the picture as it is drawn (view_placement). These
// take any point, on the picture or not. The result of screen_to_image for an empty image is (0, 0).
[[nodiscard]] ViewPoint view_screen_to_image(const ViewState& s, const ViewGeometry& g,
                                             ViewPoint screen) noexcept;
[[nodiscard]] ViewPoint view_image_to_screen(const ViewState& s, const ViewGeometry& g,
                                             ViewPoint image) noexcept;

// The whole preview pixel under a screen point; nothing if the point is off the picture or off the
// canvas (a picture zoomed past the canvas's edge isn't under the cursor there).
struct ViewPixel {
    int x = 0;
    int y = 0;

    bool operator==(const ViewPixel&) const = default;
};
[[nodiscard]] std::optional<ViewPixel> view_pixel_at(const ViewState& s, const ViewGeometry& g,
                                                     ViewPoint screen) noexcept;

// Goes to `zoom`, keeping the image point under `anchor` (a screen point) where it is, as far as the
// limits of view_settled allow. A zoom at or below the fit gives the fit.
[[nodiscard]] ViewState view_zoomed_to(const ViewState& s, const ViewGeometry& g, float zoom,
                                       ViewPoint anchor) noexcept;

// As view_zoomed_to, about the middle of the canvas.
[[nodiscard]] ViewState view_zoomed_to(const ViewState& s, const ViewGeometry& g,
                                       float zoom) noexcept;

// A double-click: to 100% of the file (view_actual_zoom) about `anchor`, or back to fit from 100%.
[[nodiscard]] ViewState view_toggled(const ViewState& s, const ViewGeometry& g,
                                     ViewPoint anchor) noexcept;

// `s` for a ViewGeometry::file_scale that changed from `old_scale` to `new_scale` (the full-size
// photo arrived a little larger or smaller than the estimate it was measured on, or the frame
// changed): the same percent of the file, about the same image point. Fit, and scales that can't
// be used, leave it as it is.
[[nodiscard]] ViewState view_rescaled(const ViewState& s, float old_scale,
                                      float new_scale) noexcept;

// Moves the picture by this many screen units (dx > 0: the picture moves right).
[[nodiscard]] ViewState view_panned_by(const ViewState& s, const ViewGeometry& g, float dx,
                                       float dy) noexcept;

// The next zoom for the keys: the next of 25, 33, 50, 67, 100, 150, 200, 300, 400, 600 and 800% of
// the file beyond `zoom`, in the direction of `direction` (> 0: in). Fit is a stop too: stepping
// out below it gives the fit's zoom, and the steps start from there, whatever it is. Zooms are in
// device pixels per image pixel, as everywhere here; `file_scale` is ViewGeometry::file_scale, so a
// stop of p% is the zoom p / 100 x file_scale (with 1, the stops are the zooms themselves).
[[nodiscard]] float view_step_zoom(float zoom, float fit_zoom, int direction,
                                   float file_scale = 1.0f) noexcept;

// True if the two zooms would read as the same number and draw the same size: the zoom after a
// stop has been reached, or a fit that happens to be 100%.
[[nodiscard]] bool view_same_zoom(float a, float b) noexcept;

// Whether to draw the picture with nearest sampling: past 200% its pixels should show as pixels, for
// someone looking at what one holds, rather than as a blur.
[[nodiscard]] bool view_shows_pixels(float zoom) noexcept;

// Where the zoom's chip of this size goes: the canvas's bottom right corner, `margin_x` from its
// right edge and `margin_y` from its bottom, moved up past anything of `taken` in its way (keeping
// those margins from it). Nothing if it doesn't fit without covering them. With one `margin` it is
// the same on every side.
[[nodiscard]] std::optional<ViewPoint> view_chip_position(const ViewRect& canvas, float width,
                                                          float height, float margin_x,
                                                          float margin_y,
                                                          std::span<const ViewRect> taken) noexcept;
[[nodiscard]] std::optional<ViewPoint> view_chip_position(const ViewRect& canvas, float width,
                                                          float height, float margin,
                                                          std::span<const ViewRect> taken) noexcept;

}  // namespace prism::gui

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "prism/gui/settings.hpp"
#include "prism/image.hpp"

namespace prism::gui {

// --- The HDR canvas on macOS (#106)
// ---------------------------------------------------------------
//
// The decisions behind showing light above white on an HDR screen, as plain functions; the app
// (gui_app/main.cpp, app.cpp) only follows them. Processing stays on the CPU: the preview, the
// regions and the export run the same C++ code, and the GPU only displays the picture this file
// describes.
//
// "UI-target mode" (spike "SDL linear renderer on macOS", #97, and #96's display decision): the
// renderer's output is linear (SDL_COLORSPACE_SRGB_LINEAR, an extended-range float layer); the
// canvas is drawn first, straight into it with SDL calls; Dear ImGui draws into an offscreen float
// target tagged SDL_COLORSPACE_SRGB, which is then laid over the canvas 1:1 with
// SDL_SCALEMODE_PIXELART and SDL_BLENDMODE_BLEND_PREMULTIPLIED. Measured in the spike: the UI is
// within 1 level of today's renderer.

enum class DisplayMode {
    sdr,            // today's renderer, unchanged: sRGB output, the canvas in ImGui's draw list
    hdr_ui_target,  // UI-target mode, above
};

// The mode for a window: UI-target mode only on macOS (Windows HDR display is Later, #96), only
// while the window's screen can show HDR (SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN), and only while the
// photo's HDR is on (output_headroom). With HDR off, or on any other screen, Prism keeps today's
// renderer exactly (#97's decision), so turning HDR on or off recreates the renderer. (SDL counts
// an XDR screen as HDR at any brightness, so the screen alone would put almost every Mac in
// UI-target mode.)
[[nodiscard]] DisplayMode display_mode(bool screen_shows_hdr, bool macos, bool hdr_on) noexcept;

// Whether the canvas shows an HDR picture rather than today's 8-bit one: in UI-target mode, with
// HDR on in the settings (output_headroom), while the final picture is viewed (the HDR branch is
// part of Output encode; earlier stages and comparisons with them stay SDR), without a test strip
// (its bands are SDR exposures), and on a screen with room above white right now (a display
// headroom above 1; at 1 the viewer weight is 0 and the picture would be the SDR one anyway).
// `sdr_view` is a view this version shows in SDR only (sdr_only_view). Those draw the 8-bit
// picture through Dear ImGui exactly as today, inside the UI target.
[[nodiscard]] bool hdr_canvas_on(DisplayMode mode, const EditSettings& s, StageId viewed,
                                 bool test_strip, float display_headroom,
                                 bool sdr_view = false) noexcept;

// What the canvas shows besides the plain picture this frame.
struct CanvasViews {
    bool clip_overlay = false;    // the J overlay
    bool stroke_overlay = false;  // the Dodge & burn strokes' overlay (M)
    bool real_pixels = false;     // real pixels drawn over the enlarged preview (#37)
    bool split = false;           // split compare
    bool easel = false;           // the easel (framing, #58)
};

// Whether this version shows the canvas in SDR only (hdr_canvas_on's `sdr_view`): with real
// pixels, a split compare or the easel. The J overlay and the strokes don't: they are drawn into
// the HDR picture (hdr_with_overlays).
[[nodiscard]] bool sdr_only_view(const CanvasViews& views) noexcept;

// The weight a viewer of the exported file gives its gain map on a screen with `display_headroom`
// (#96 decision 10 as changed: HDRCapacityMin 0, HDRCapacityMax = N): prism::gain_map_weight with
// that metadata, clamp(log2 D / N, 0, 1). The canvas uses the same value, so it shows what the
// file will show on this screen.
[[nodiscard]] float canvas_weight(float display_headroom, float headroom_stops) noexcept;

// The display headroom a preview render asks for (PreviewOptions::hdr_display_headroom): the
// headroom that gives canvas_weight rounded to 1/256, so a screen whose headroom wanders a little
// (brightness changes send many events) or stays at or above 2^N (the weight clamped at 1) asks
// for the same picture and starts no new render. 1 at weight 0; 2^N at weight 1.
[[nodiscard]] float request_headroom(float display_headroom, float headroom_stops) noexcept;

// The picture a viewer of the exported file shows on this screen, made on the CPU from the
// preview's two pictures: `sdr_srgb8` (encode_srgb8 of process_hdr's SDR picture, the bytes the
// SDR canvas shows) as the base, the gain map make_gain_map makes from it and `hdr`, applied at
// canvas_weight(display_headroom, headroom_stops). Linear float RGB, width x height, values above
// 1 where the screen can show them. Weight 0 gives the decoded base exactly (the SDR picture).
// Bit-identical for any thread count. Throws std::invalid_argument as make_gain_map does.
[[nodiscard]] Image viewer_picture(std::span<const std::uint8_t> sdr_srgb8, const Image& hdr,
                                   float headroom_stops, float display_headroom, int threads = 0);

// IEEE 754 half precision (binary16), round to nearest even: overflow gives infinity, NaN a quiet
// NaN, and values below the smallest subnormal 0 (with their sign).
[[nodiscard]] std::uint16_t half_from_float(float v) noexcept;
[[nodiscard]] float float_from_half(std::uint16_t h) noexcept;

// `img` (linear RGB) as RGBA half floats with alpha 1, row-major, width * height * 4 values: the
// texels of an SDL_PIXELFORMAT_RGBA64_FLOAT texture. Bit-identical for any thread count. Empty if
// the pixel buffer doesn't match width x height (or the picture is empty).
[[nodiscard]] std::vector<std::uint16_t> rgba_half_from(const Image& img, int threads = 0);

// --- What is drawn over the canvas ---------------------------------------------------------------
//
// In UI-target mode a translucent overlay laid over HDR pixels blends in linear light, so it no
// longer dims them as on an SDR screen: 50% black over +2 stops leaves +1 stop (#97). Such
// overlays are drawn into the canvas picture instead, on the CPU, over the SDR picture
// (into_picture below), and windows that sit over the canvas get opaque backgrounds.

enum class CanvasOverlay {
    easel_shade,     // the easel's shading outside the frame (#58)
    clip_overlay,    // the J clip overlay
    stroke_overlay,  // the Dodge & burn strokes' overlay
    split_line,      // the split compare's dividing line
    frame_lines,     // the easel's frame outline and handles (opaque lines)
    sample_pin,      // Sample mode's pin (an opaque mark)
    tooltip,         // tooltips and popups over the canvas
};

enum class OverlayPlacement {
    on_top,         // drawn by ImGui over the canvas, as today
    into_picture,   // drawn into the canvas picture on the CPU (into_picture)
    opaque_on_top,  // drawn by ImGui over the canvas, with an opaque background
};

// Where `overlay` goes. On an SDR canvas everything stays on top, as today. On an HDR canvas the
// translucent overlays over pixels (easel shading, the clip overlay, the strokes, the split line)
// go into the picture, tooltips and popups get opaque backgrounds, and opaque marks stay on top.
[[nodiscard]] OverlayPlacement overlay_placement(CanvasOverlay overlay, bool hdr_canvas) noexcept;

// A linear RGB colour with straight (not premultiplied) alpha.
struct LinearRgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

// One pixel of an overlay drawn into the HDR canvas picture: `picture` (the viewer picture's
// pixel, linear) where the overlay is clear (a = 0); otherwise the overlay over the SDR picture's
// pixel `sdr` (linear, 0 to 1) blended as today's renderer blends it, in sRGB codes:
// srgb_to_linear(linear_to_srgb(colour) * a + linear_to_srgb(sdr) * (1 - a)). So a shaded area
// shows the SDR picture darkened exactly as an SDR screen shows it, and no highlight shines
// through. Alpha is clamped to 0..1 (NaN counts as 0). Channels are r, g, b.
void into_picture(const float picture[3], const float sdr[3], const LinearRgba& overlay,
                  float out[3]) noexcept;

// An overlay pixel as the SDR canvas draws it (ABGR8888 bytes: R low, A high; straight alpha, the
// colour in sRGB codes) as a LinearRgba: each colour code through srgb8_to_linear, alpha / 255.
[[nodiscard]] LinearRgba linear_rgba_from_abgr(std::uint32_t abgr) noexcept;

// The HDR canvas's picture with translucent overlays drawn into it (#106): `hdr_rgba` (the
// preview's viewer picture, PreviewFrame::hdr_rgba, RGBA halves) and `sdr_rgb8` (the same frame's
// 8-bit SDR picture), width x height; `layers` are overlay pixels in ABGR (clip_overlay_pixels,
// stroke_overlay_pixels), bottom first, in the order the SDR canvas draws them. Where every layer
// is clear the halves are copied bit for bit. Elsewhere each covered layer goes over the pixel
// with into_picture: the first over the SDR picture (srgb8_to_linear of its bytes), each next one
// over the result, so the pixel is the SDR canvas's blend of the same layers and no light above
// white shines through. Alpha stays the picture's. A layer of the wrong size is left out. Empty
// if `hdr_rgba` or `sdr_rgb8` doesn't match width x height (or the picture is empty).
// Bit-identical for any thread count.
[[nodiscard]] std::vector<std::uint16_t> hdr_with_overlays(
    std::span<const std::uint16_t> hdr_rgba, std::span<const std::uint8_t> sdr_rgb8, int width,
    int height, std::span<const std::span<const std::uint32_t>> layers, int threads = 0);

// UI-target mode's compositing of one pixel: the UI target's texel, premultiplied 8-bit sRGB
// (what ImGui drew into it, decoded by SDL's pixel-art path as sRGB), laid over the canvas's
// linear pixel with SDL_BLENDMODE_BLEND_PREMULTIPLIED: srgb_to_linear(ui) + canvas * (1 - a),
// where the colour is decoded from its straight form (ui / a) and multiplied by a again. A fully
// opaque UI pixel is its own colour, exactly; a clear one leaves the canvas as it is.
void ui_over_canvas(const float canvas[3], const std::uint8_t ui_premultiplied[4],
                    float out[3]) noexcept;

}  // namespace prism::gui

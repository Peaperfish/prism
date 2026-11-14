#pragma once

#include <array>
#include <optional>
#include <string>

#include "prism/frame.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"

namespace prism::gui {

// The white balance picker (#21): click something that should be grey, and the White balance stage
// is set to the light that makes it so. No ImGui here, so all of it is tested without a window.

// The picker averages a square of this many pixels across, centred on the pixel clicked, on the
// preview. On the photo's own pixels the square is the same piece of the photo, measured in photo
// pixels (pick_footprint_size in detail_canvas.hpp); the functions below take its size, odd, with
// this as the default.
inline constexpr int pick_patch_size = 5;

// The pixels of a width x height picture the patch of `size` pixels across (odd) around (x, y)
// covers: x0 to x1 and y0 to y1, both ends included, cut off at the picture's edges. Nothing for a
// pixel off the picture.
struct PatchBounds {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    bool operator==(const PatchBounds&) const = default;
};
[[nodiscard]] std::optional<PatchBounds> pick_patch_bounds(int width, int height, int x, int y,
                                                           int size = pick_patch_size) noexcept;

// What the patch holds: its mean per channel (linear, as decoded: before any white balance, so that
// picking again gives the same answer), and whether any of its pixels is at the file's ceiling or
// above white (see file_ceiling_epsilon in clipping.hpp).
struct PickPatch {
    std::array<double, 3> mean{};
    bool clipped = false;
    int pixels = 0;

    bool operator==(const PickPatch&) const = default;
};

// Nothing for a pixel off the picture, an empty image, or pixels that don't match its size.
[[nodiscard]] std::optional<PickPatch> pick_patch(const Image& base, int x, int y,
                                                  int size = pick_patch_size) noexcept;

// A rectangle of an image, in its pixels, continuous: [x0, x1) x [y0, y1). A pixel is the unit
// square from (column, row).
struct PatchArea {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
};

// The patch over `area` of `base`, where the area's edges may fall inside pixels: the mean is
// weighted by how much of each pixel the area covers, and `pixels` counts the pixels it touches.
// With an area on whole pixels this is pick_patch's mean over those pixels. Nothing for an empty
// area, one outside the image, an empty image, or pixels that don't match its size. The area is cut
// off at the image's edges.
[[nodiscard]] std::optional<PickPatch> pick_patch_area(const Image& base,
                                                       const PatchArea& area) noexcept;

// The patch mean below this share of white's light is too dark to say what colour the light was.
inline constexpr double pick_dark_limit = 0.005;

// The chromaticity of the light that makes the decoded colour `c` (linear sRGB, already adapted from
// the as-shot light to D65) neutral. white_balance_matrix(W, as_shot) = xyz_to_srgb * B * srgb_to_xyz
// with B = bradford(white of W, as_shot); it maps c to a grey exactly when B * XYZ(c) is D65-shaped,
// i.e. when XYZ(white of W) is proportional to bradford(D65, as_shot) * XYZ(c). The grey's level is
// free, so only the chromaticity of that XYZ matters. Nothing if it isn't a finite chromaticity.
[[nodiscard]] std::optional<Chromaticity> neutral_light_xy(const std::array<double, 3>& c,
                                                           Chromaticity as_shot);

enum class PickOutcome {
    picked,
    too_dark,
    clipped,
    unavailable,  // off the picture, or a light that has no Kelvin and tint
};

struct NeutralPick {
    PickOutcome outcome = PickOutcome::unavailable;
    WhiteBalance light;    // rounded (10 K, whole tint) and within the ranges; for `picked`
    bool limited = false;  // the light was beyond the sliders' ranges, and has been clamped
};

// Picks at pixel (x, y) of `base`: the light from the patch around it. `as_shot` is the photo's
// as-shot light, or none for the 6500 K / tint 0 that photos without one are taken to have.
[[nodiscard]] NeutralPick pick_neutral(const Image& base, int x, int y,
                                       const std::optional<Chromaticity>& as_shot,
                                       int size = pick_patch_size) noexcept;

// Picks from the light of `area` of `base` (pick_patch_area): what pick_neutral says of a patch
// that is not on whole pixels. This is Pick on the preview below 100%, over the piece of the photo
// that the photo's own pixels would average at 100%.
[[nodiscard]] NeutralPick pick_neutral_area(const Image& base, const PatchArea& area,
                                            const std::optional<Chromaticity>& as_shot) noexcept;

// Real pixels (#37): pick_neutral at pixel (x, y) of the framed picture at full size, from `photo`
// (the full-size decode, Session::detail_base) framed by `frame` as an export frames it: the patch
// is `size` pixels of that picture across (odd), cut off at its edges, and averaged from the
// pixels an export makes. `as_shot` as for pick_neutral. Unavailable for a pixel off the framed
// picture; throws std::invalid_argument for an invalid frame.
[[nodiscard]] NeutralPick pick_neutral_photo(const Photo& photo, const std::optional<Frame>& frame,
                                             int x, int y,
                                             const std::optional<Chromaticity>& as_shot,
                                             int size = pick_patch_size);

// The chip's words: "Neutral here: 5180 K, tint +4" (plus " (limited to the slider's range)"), or the
// reason nothing changed: "Too dark to judge; click a lighter grey" or "Clipped; click a grey that
// isn't blown out". Empty for `unavailable`.
[[nodiscard]] std::string pick_message(const NeutralPick& pick);

// Chooses the picked light, turning the stage on. Does nothing unless the pick was `picked`.
void apply_pick(EditSettings& s, const NeutralPick& pick) noexcept;

}  // namespace prism::gui

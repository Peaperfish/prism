#include "prism/gui/neutral_pick.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "prism/camera_profile.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/white_balance.hpp"

namespace prism::gui {

std::optional<PatchBounds> pick_patch_bounds(int width, int height, int x, int y) noexcept {
    if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height) {
        return std::nullopt;
    }
    constexpr int reach = pick_patch_size / 2;
    return PatchBounds{std::max(0, x - reach), std::max(0, y - reach),
                       std::min(width - 1, x + reach), std::min(height - 1, y + reach)};
}

std::optional<PickPatch> pick_patch(const Image& base, int x, int y) noexcept {
    const std::optional<PatchBounds> bounds = pick_patch_bounds(base.width, base.height, x, y);
    if (!bounds || base.pixels.size() != static_cast<std::size_t>(base.width) *
                                             static_cast<std::size_t>(base.height) * Image::channels) {
        return std::nullopt;
    }
    constexpr float ceiling = 1.0f - file_ceiling_epsilon;
    PickPatch patch;
    std::array<double, 3> sum{};
    for (int row = bounds->y0; row <= bounds->y1; ++row) {
        for (int column = bounds->x0; column <= bounds->x1; ++column) {
            const float* p = base.pixels.data() +
                             (static_cast<std::size_t>(row) * static_cast<std::size_t>(base.width) +
                              static_cast<std::size_t>(column)) * Image::channels;
            for (int c = 0; c < 3; ++c) {
                sum[static_cast<std::size_t>(c)] += p[c];
                patch.clipped = patch.clipped || p[c] >= ceiling;
            }
            ++patch.pixels;
        }
    }
    for (std::size_t c = 0; c < 3; ++c) {
        patch.mean[c] = sum[c] / patch.pixels;
    }
    return patch;
}

std::optional<Chromaticity> neutral_light_xy(const std::array<double, 3>& c,
                                             Chromaticity as_shot) {
    const Vec3 xyz = multiply(bradford(white_d65, as_shot), multiply(srgb_to_xyz(), c));
    const double sum = xyz[0] + xyz[1] + xyz[2];
    if (!std::isfinite(sum) || !(sum > 0.0)) {
        return std::nullopt;
    }
    return Chromaticity{xyz[0] / sum, xyz[1] / sum};
}

NeutralPick pick_neutral(const Image& base, int x, int y,
                         const std::optional<Chromaticity>& as_shot) noexcept {
    NeutralPick result;
    const std::optional<PickPatch> patch = pick_patch(base, x, y);
    if (!patch) {
        return result;
    }
    for (const double m : patch->mean) {
        if (!(m > 0.0) || m < pick_dark_limit) {
            result.outcome = PickOutcome::too_dark;
            return result;
        }
    }
    if (patch->clipped) {
        result.outcome = PickOutcome::clipped;
        return result;
    }
    const std::optional<Chromaticity> xy =
        neutral_light_xy(patch->mean, as_shot.value_or(white_of(default_as_shot)));
    if (!xy) {
        return result;
    }
    const WhiteBalance raw = white_balance_of(*xy);
    if (!std::isfinite(raw.kelvin) || !std::isfinite(raw.tint)) {
        return result;
    }
    // What the sliders would show: rounded as they round, with and without the clamp.
    const double kelvin = std::round(raw.kelvin / 10.0) * 10.0;
    const double tint = std::round(raw.tint);
    result.light = {snap_kelvin(raw.kelvin), snap_tint(raw.tint)};
    result.limited = kelvin != result.light.kelvin || tint != result.light.tint;
    // The search stops at its ends: a light bluer than 15000 K or redder than 1000 K lands on the end
    // with the rest of its distance lost. Half a tint step (in uv) tells that from a rounding error.
    const Uv wanted = uv_of(*xy);
    const Uv found = uv_of(white_of(raw));
    result.limited = result.limited || std::hypot(wanted.u - found.u, wanted.v - found.v) >
                                           0.5 * tint_duv_per_unit;
    result.outcome = PickOutcome::picked;
    return result;
}

std::string pick_message(const NeutralPick& pick) {
    switch (pick.outcome) {
        case PickOutcome::picked:
            return "Neutral here: " + format_kelvin(pick.light.kelvin) + ", tint " +
                   format_tint(pick.light.tint) +
                   (pick.limited ? " (limited to the slider's range)" : "");
        case PickOutcome::too_dark:
            return "Too dark to judge; click a lighter grey";
        case PickOutcome::clipped:
            return "Clipped; click a grey that isn't blown out";
        case PickOutcome::unavailable:
            break;
    }
    return {};
}

void apply_pick(EditSettings& s, const NeutralPick& pick) noexcept {
    if (pick.outcome != PickOutcome::picked) {
        return;
    }
    s.white_balance = pick.light;
    s.white_balance_on = true;
}

}  // namespace prism::gui

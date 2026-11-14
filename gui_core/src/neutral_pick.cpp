#include "prism/gui/neutral_pick.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "prism/camera_profile.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/white_balance.hpp"

namespace prism::gui {

std::optional<PatchBounds> pick_patch_bounds(int width, int height, int x, int y,
                                             int size) noexcept {
    if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height || size < 1) {
        return std::nullopt;
    }
    const int reach = size / 2;
    return PatchBounds{std::max(0, x - reach), std::max(0, y - reach),
                       std::min(width - 1, x + reach), std::min(height - 1, y + reach)};
}

std::optional<PickPatch> pick_patch(const Image& base, int x, int y, int size) noexcept {
    const std::optional<PatchBounds> bounds =
        pick_patch_bounds(base.width, base.height, x, y, size);
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

std::optional<PickPatch> pick_patch_area(const Image& base, const PatchArea& area) noexcept {
    if (base.width < 1 || base.height < 1 ||
        base.pixels.size() != static_cast<std::size_t>(base.width) *
                                  static_cast<std::size_t>(base.height) * Image::channels ||
        !(area.x1 > area.x0) || !(area.y1 > area.y0)) {
        return std::nullopt;
    }
    const double x0 = std::max(0.0, area.x0);
    const double y0 = std::max(0.0, area.y0);
    const double x1 = std::min(static_cast<double>(base.width), area.x1);
    const double y1 = std::min(static_cast<double>(base.height), area.y1);
    if (!(x1 > x0) || !(y1 > y0)) {
        return std::nullopt;
    }
    // How much of the unit pixel at `index` the span [lo, hi) covers.
    const auto cover = [](int index, double lo, double hi) {
        return std::max(0.0, std::min(hi, index + 1.0) - std::max(lo, static_cast<double>(index)));
    };
    constexpr float ceiling = 1.0f - file_ceiling_epsilon;
    PickPatch patch;
    std::array<double, 3> sum{};
    double weight = 0.0;
    const int first_row = static_cast<int>(std::floor(y0));
    const int last_row = std::min(base.height - 1, static_cast<int>(std::ceil(y1)) - 1);
    const int first_column = static_cast<int>(std::floor(x0));
    const int last_column = std::min(base.width - 1, static_cast<int>(std::ceil(x1)) - 1);
    for (int row = first_row; row <= last_row; ++row) {
        const double wy = cover(row, y0, y1);
        for (int column = first_column; column <= last_column; ++column) {
            const double w = wy * cover(column, x0, x1);
            if (!(w > 0.0)) {
                continue;
            }
            const float* p = base.pixels.data() +
                             (static_cast<std::size_t>(row) * static_cast<std::size_t>(base.width) +
                              static_cast<std::size_t>(column)) * Image::channels;
            for (int c = 0; c < 3; ++c) {
                sum[static_cast<std::size_t>(c)] += w * p[c];
                patch.clipped = patch.clipped || p[c] >= ceiling;
            }
            weight += w;
            ++patch.pixels;
        }
    }
    if (!(weight > 0.0)) {
        return std::nullopt;
    }
    for (std::size_t c = 0; c < 3; ++c) {
        patch.mean[c] = sum[c] / weight;
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

namespace {

// The light the patch says was neutral, as pick_neutral and pick_neutral_area report it.
NeutralPick pick_from_patch(const std::optional<PickPatch>& patch,
                            const std::optional<Chromaticity>& as_shot) noexcept {
    NeutralPick result;
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

}  // namespace

NeutralPick pick_neutral(const Image& base, int x, int y,
                         const std::optional<Chromaticity>& as_shot, int size) noexcept {
    return pick_from_patch(pick_patch(base, x, y, size), as_shot);
}

NeutralPick pick_neutral_area(const Image& base, const PatchArea& area,
                              const std::optional<Chromaticity>& as_shot) noexcept {
    return pick_from_patch(pick_patch_area(base, area), as_shot);
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

NeutralPick pick_neutral_photo(const Photo& photo, const std::optional<Frame>& frame, int x, int y,
                               const std::optional<Chromaticity>& as_shot, int size) {
    const int w = photo.image.width;
    const int h = photo.image.height;
    if (w < 1 || h < 1) {
        return {};
    }
    const ExportFrame plan = plan_export_frame(frame, w, h);
    const std::optional<PatchBounds> bounds =
        pick_patch_bounds(plan.size.width, plan.size.height, x, y, size);
    if (!bounds) {
        return {};
    }
    // The patch's pixels of the framed picture, made as an export makes them; the patch is then
    // the whole of this small image, around the same pixel.
    const FrameBox part{bounds->x0, bounds->y0, bounds->x1 - bounds->x0 + 1,
                        bounds->y1 - bounds->y0 + 1};
    Image patch;
    if (plan.fitted) {
        patch = apply_frame(photo.image, *plan.fitted, 1, part);
    } else {
        patch.width = part.width;
        patch.height = part.height;
        patch.pixels.reserve(static_cast<std::size_t>(part.width) *
                             static_cast<std::size_t>(part.height) * Image::channels);
        for (int row = part.y0; row < part.y0 + part.height; ++row) {
            const auto first = photo.image.pixels.begin() +
                               static_cast<std::ptrdiff_t>(
                                   (static_cast<std::size_t>(row) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(part.x0)) *
                                   Image::channels);
            patch.pixels.insert(patch.pixels.end(), first,
                                first + static_cast<std::ptrdiff_t>(part.width) * Image::channels);
        }
    }
    return pick_neutral(patch, x - part.x0, y - part.y0, as_shot, size);
}

}  // namespace prism::gui

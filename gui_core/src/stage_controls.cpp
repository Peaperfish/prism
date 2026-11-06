#include "prism/gui/stage_controls.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

#include "prism/utf8_path.hpp"

namespace prism::gui {

namespace {

// `v` with two decimals, in the shortest way that always uses '.', whatever the locale.
std::string two_places(float v) {
    char buffer[64];
    const std::to_chars_result r =
        std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::fixed, 2);
    return r.ec == std::errc{} ? std::string(buffer, r.ptr) : std::string("?");
}

// `v` in the shortest form that reads back the same, e.g. 2.2 or 4.
std::string shortest(float v) {
    char buffer[64];
    const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof buffer, v);
    return r.ec == std::errc{} ? std::string(buffer, r.ptr) : std::string("?");
}

constexpr CurveParameter gamma_parameter{"Gamma", 0.5f, 4.0f, 2.2f, false};
// Extended Reinhard maps the white point to 1 and is the identity at white 1: below that it would
// brighten instead of compress, pushing everything above `white` past 1.
constexpr CurveParameter reinhard_parameter{"White point", 1.0f, 16.0f, 4.0f, true};

}  // namespace

std::optional<SimpleControl> stage_slider(StageId stage) noexcept {
    switch (stage) {
        case StageId::exposure: return SimpleControl::brightness;
        case StageId::saturation: return SimpleControl::colour;
        case StageId::contrast: return SimpleControl::contrast;
        default: return std::nullopt;
    }
}

StageId stage_of(SimpleControl control) noexcept {
    switch (control) {
        case SimpleControl::brightness: return StageId::exposure;
        case SimpleControl::colour: return StageId::saturation;
        case SimpleControl::contrast: return StageId::contrast;
    }
    return StageId::exposure;
}

std::string format_exposure(float stops) {
    std::string digits = two_places(std::fabs(stops));
    const bool negative = std::signbit(stops) && digits != "0.00";
    return (negative ? "-" : "+") + digits + " stops";
}

std::string format_multiplier(float value) {
    return "\u00D7" + two_places(value);
}

std::string format_saturation(float value) {
    return value == 0.0f ? "black and white" : format_multiplier(value);
}

std::string stage_value_text(const EditSettings& s, StageId stage) {
    switch (stage) {
        case StageId::exposure: return format_exposure(s.exposure_stops);
        case StageId::saturation: return format_saturation(s.saturation);
        case StageId::contrast: return format_multiplier(s.contrast);
        case StageId::matrix:
            if (!s.matrix) {
                return "none";
            }
            // A matrix from a recipe without a `from` label has no file to name.
            return s.matrix->path.empty() ? std::string("recipe matrix")
                                          : utf8(s.matrix->path.filename());
        case StageId::curve:
            switch (s.curve) {
                case CurveKind::none: return "none";
                case CurveKind::gamma: return "gamma " + shortest(s.gamma);
                case CurveKind::reinhard: return "Reinhard, white " + shortest(s.reinhard_white);
                case CurveKind::filmic: return "filmic";
            }
            return {};
        case StageId::decode:
        case StageId::encode: return {};
    }
    return {};
}

std::string_view curve_kind_name(CurveKind kind) noexcept {
    switch (kind) {
        case CurveKind::none: return "None";
        case CurveKind::gamma: return "Gamma";
        case CurveKind::reinhard: return "Reinhard";
        case CurveKind::filmic: return "Filmic";
    }
    return {};
}

std::optional<CurveParameter> curve_parameter(CurveKind kind) noexcept {
    switch (kind) {
        case CurveKind::gamma: return gamma_parameter;
        case CurveKind::reinhard: return reinhard_parameter;
        default: return std::nullopt;
    }
}

float curve_parameter_value(const EditSettings& s) {
    const std::optional<CurveParameter> p = curve_parameter(s.curve);
    if (!p) {
        return 0.0f;
    }
    const float v = s.curve == CurveKind::gamma ? s.gamma : s.reinhard_white;
    return std::isnan(v) ? p->default_value : std::clamp(v, p->min, p->max);
}

float curve_slider_position(const CurveParameter& p, float value) noexcept {
    if (std::isnan(value)) {
        value = p.default_value;
    }
    value = std::clamp(value, p.min, p.max);
    if (value == p.min) {
        return 0.0f;
    }
    if (value == p.max) {
        return 1.0f;
    }
    const float t = p.logarithmic ? std::log(value / p.min) / std::log(p.max / p.min)
                                  : (value - p.min) / (p.max - p.min);
    return std::clamp(t, 0.0f, 1.0f);
}

float curve_value_at(const CurveParameter& p, float position) noexcept {
    const float t = std::isnan(position) ? 0.0f : std::clamp(position, 0.0f, 1.0f);
    if (t == 0.0f) {
        return p.min;
    }
    if (t == 1.0f) {
        return p.max;
    }
    const float v = p.logarithmic ? p.min * std::pow(p.max / p.min, t) : p.min + t * (p.max - p.min);
    return std::clamp(v, p.min, p.max);
}

void set_curve_kind(EditSettings& s, CurveKind kind) {
    s.curve = kind;
    s.curve_on = true;
}

void set_curve_parameter(EditSettings& s, float value) {
    const std::optional<CurveParameter> p = curve_parameter(s.curve);
    if (!p) {
        return;
    }
    const float v = std::isnan(value) ? p->default_value : std::clamp(value, p->min, p->max);
    (s.curve == CurveKind::gamma ? s.gamma : s.reinhard_white) = v;
    s.curve_on = true;
}

std::string stage_caption(StageId stage) {
    return stage == StageId::decode ? "Original"
                                    : "after " + std::string(stage_info(stage).name);
}

std::optional<StageId> effective_compare(StageId viewed, std::optional<StageId> compare) noexcept {
    return compare == viewed ? std::nullopt : compare;
}

StageId furthest_shown(StageId viewed, std::optional<StageId> compare) noexcept {
    return compare && static_cast<int>(*compare) > static_cast<int>(viewed) ? *compare : viewed;
}

bool stage_after_view(StageId stage, StageId viewed, std::optional<StageId> compare) noexcept {
    return static_cast<int>(stage) > static_cast<int>(furthest_shown(viewed, compare));
}

bool hidden_by_view(const EditSettings& s, StageId stage, StageId viewed,
                    std::optional<StageId> compare) {
    if (static_cast<int>(stage) <= static_cast<int>(furthest_shown(viewed, compare)) ||
        !stage_enabled(s, stage)) {
        return false;
    }
    switch (stage) {
        case StageId::exposure: return s.exposure_stops != 0.0f;
        case StageId::matrix: return s.matrix && s.matrix->values != identity_matrix;
        case StageId::saturation: return s.saturation != 1.0f;
        case StageId::contrast: return s.contrast != 1.0f;
        case StageId::curve: return s.curve != CurveKind::none;
        case StageId::decode:
        case StageId::encode: return false;
    }
    return false;
}

std::string view_hint(StageId viewed, std::optional<StageId> compare) {
    if (effective_compare(viewed, compare)) {
        return "Not visible here: the furthest stage shown is after " +
               std::string(stage_info(furthest_shown(viewed, compare)).name) + ".";
    }
    return "Not visible here: you're viewing after " + std::string(stage_info(viewed).name) + ".";
}

EscapeAction escape_action(const EscapeContext& c) noexcept {
    if (c.text_editing || c.popup_open || c.item_active) {
        return EscapeAction::none;
    }
    if (c.window_focused) {
        return EscapeAction::close_window;
    }
    if (c.sampling) {
        return EscapeAction::clear_sample;
    }
    if (c.test_strip) {
        return EscapeAction::clear_test_strip;
    }
    if (c.comparing) {
        return EscapeAction::clear_compare;
    }
    return c.viewing_earlier ? EscapeAction::show_final : EscapeAction::none;
}

}  // namespace prism::gui

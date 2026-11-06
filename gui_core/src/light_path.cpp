#include "prism/gui/light_path.hpp"

#include <algorithm>
#include <charconv>

#include "prism/gui/explanations.hpp"
#include "prism/utf8_path.hpp"

namespace prism::gui {

namespace {

// `v` as text, whatever the locale: to two places, or as short as reads back the same.
std::string number_text(float v, bool two_places) {
    char buffer[64];
    const std::to_chars_result r =
        two_places ? std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::fixed, 2)
                   : std::to_chars(buffer, buffer + sizeof buffer, v);
    return r.ec == std::errc{} ? std::string(buffer, r.ptr) : std::string("?");
}

void add_mark(std::vector<ClipMark>& marks, ClipMarkKind kind, ClipFate fate,
              const ClipCounts& counts, std::uint64_t total, std::string_view where,
              std::string what, std::string note) {
    if (fate == ClipFate::none) {
        return;
    }
    ClipMark mark;
    mark.kind = kind;
    mark.fate = fate;
    mark.percent = format_clip_percent(counts.any, total);
    mark.text = mark.percent + " " + std::string(where);
    if (fate == ClipFate::recoverable) {
        mark.text += " here";
    }
    mark.what = std::move(what);
    mark.note = std::move(note);
    marks.push_back(std::move(mark));
}

}  // namespace

std::vector<ClipMark> clip_marks(const StageClip& clip, std::uint64_t total) {
    std::vector<ClipMark> marks;
    if (!clip.ran) {
        return marks;
    }
    constexpr const char* later_may_recover =
        "Out of range, but a later stage may still bring them back.";
    add_mark(marks, ClipMarkKind::below_black, clip.under_fate, clip.under, total, "below black",
             "Shadows below black",
             clip.under_fate == ClipFate::lost
                 ? "Clipped here: this stage cut them off, so the detail is gone."
                 : later_may_recover);
    const bool file_ceiling = clip.stage == StageId::decode;
    add_mark(marks, ClipMarkKind::above_white, clip.over_fate, clip.over, total, "above white",
             file_ceiling ? "Highlights at white" : "Highlights above white",
             clip.over_fate != ClipFate::lost ? later_may_recover
             : file_ceiling ? "Already clipped in the file: no edit can bring them back."
                            : "Clipped here: this stage cut them off, so the detail is gone.");
    add_mark(marks, ClipMarkKind::out_of_gamut, clip.gamut_fate, clip.gamut, total, "outside sRGB",
             "Colours outside sRGB",
             clip.gamut_fate == ClipFate::lost
                 ? "Clipped here: these colours were more saturated than the screen and file can "
                   "show, so their hue can shift."
                 : "Colours more saturated than the screen and file can show. They get clipped "
                   "later and their hue can shift.");
    return marks;
}

std::string decode_subtitle(const EditSettings& s, std::string_view file_kind) {
    if (file_kind == "RAW") {
        return s.raw_camera_matrix ? "RAW, your camera matrix" : "RAW, camera colour";
    }
    if (file_kind == "HEIC") {
        return "HEIC, converted from its own colours";
    }
    return std::string(file_kind) + ", read as sRGB";
}

std::string matrix_value(const EditSettings& s, InputKind kind) {
    if (!s.matrix) {
        return kind == InputKind::raw ? "Camera default" : "None";
    }
    return s.matrix->path.empty() ? std::string("From recipe") : utf8(s.matrix->path.filename());
}

std::string compare_action_text(StageId viewed, std::optional<StageId> compare) {
    const std::optional<StageId> shown = effective_compare(viewed, compare);
    return "Compare: " + (shown ? stage_caption(*shown) : std::string("Off"));
}

LightPathRow light_path_row(const LightPathInputs& in, StageId stage) {
    const EditSettings& s = in.settings;
    const StageInfo& info = stage_info(stage);
    LightPathRow row;
    row.stage = stage;
    row.name = std::string(info.name);
    row.can_toggle = info.can_toggle;
    row.enabled = stage_enabled(s, stage);
    row.viewed = stage == in.viewed;
    row.dimmed = stage_after_view(stage, in.viewed, in.compare);
    if (row.dimmed) {
        row.tooltip = view_hint(in.viewed, in.compare);
    } else if (const Explanation* e = explanation_for(explain_id(stage))) {
        row.tooltip = std::string(e->summary);
    }

    switch (stage) {
        case StageId::decode:
            row.subtitle = decode_subtitle(s, in.file_kind);
            break;
        case StageId::encode:
            row.subtitle = "sRGB for the screen";
            break;
        case StageId::matrix:
            row.value = matrix_value(s, in.kind);
            row.value_muted = true;
            if (s.matrix) {
                row.value_tooltip = s.matrix->path.empty() ? "From a recipe that names no file."
                                                           : utf8(s.matrix->path);
            }
            break;
        case StageId::curve:
            row.curve_tabs = true;
            row.curve = s.curve;
            row.parameter = curve_parameter(s.curve);
            if (row.parameter) {
                row.parameter_value = curve_parameter_value(s);
                row.parameter_text = number_text(row.parameter_value, true);
                row.parameter_reset =
                    "Reset to " + number_text(row.parameter->default_value, false);
            }
            break;
        case StageId::exposure:
        case StageId::saturation:
        case StageId::contrast: {
            const SimpleControl control = *stage_slider(stage);
            const SimpleControls positions = simple_from(s);
            const SimpleOutOfRange beyond = outside_simple_range(s);
            row.slider = control;
            row.slider_position = control == SimpleControl::brightness ? positions.brightness
                                  : control == SimpleControl::contrast ? positions.contrast
                                                                       : positions.colour;
            row.beyond_slider = control == SimpleControl::brightness ? beyond.brightness
                                : control == SimpleControl::contrast ? beyond.contrast
                                                                     : beyond.colour;
            row.slider_reset = "Reset to " + stage_value_text(EditSettings{}, stage);
            // The stored value, also while the stage is off: it comes back with that one.
            EditSettings stored = s;
            set_stage_enabled(stored, stage, true);
            row.value = stage_value_text(stored, stage);
            if (!row.enabled) {
                row.value += " when on";
                row.value_muted = true;
            }
            row.test_strip = stage == StageId::exposure;
            break;
        }
    }
    if (in.clips != nullptr) {
        row.marks = clip_marks(in.clips->at(stage), in.clips->sample_count);
    }
    return row;
}

LightPathView light_path_view(const LightPathInputs& in) {
    LightPathView view;
    view.compare_text = compare_action_text(in.viewed, in.compare);
    view.compare = effective_compare(in.viewed, in.compare);
    view.viewed = in.viewed;
    view.rows.reserve(pipeline_stages().size());
    for (const StageInfo& stage : pipeline_stages()) {
        view.rows.push_back(light_path_row(in, stage.id));
    }
    return view;
}

bool row_expanded(const LightPathRow& row) noexcept {
    return row.stage == StageId::curve && row.parameter.has_value();
}

LightPathRowLayout light_path_row_layout(const LightPathRow& row,
                                         const LightPathMetrics& m) noexcept {
    LightPathRowLayout layout;
    layout.line_y = m.pad_top;
    float y = m.pad_top + m.line + m.gap;
    const auto take = [&](float height) {
        const float at = y;
        y += height + m.gap;
        return at;
    };
    switch (row.stage) {
        case StageId::decode:
        case StageId::encode:
            layout.subtitle_y = take(m.secondary);
            break;
        case StageId::exposure:
        case StageId::saturation:
        case StageId::contrast:
            layout.slider_y = take(m.slider);
            break;
        case StageId::curve:
            layout.tabs_y = take(m.tabs);
            if (row_expanded(row)) {
                layout.parameter_y = take(m.secondary);
                layout.parameter_slider_y = take(m.slider);
            }
            break;
        case StageId::matrix:
            break;
    }
    layout.clip_y = y;
    layout.clip_height = row.stage == StageId::exposure ? std::max(m.secondary, m.strip_button)
                                                        : m.secondary;
    layout.height = y + layout.clip_height + m.pad_bottom;
    return layout;
}

}  // namespace prism::gui

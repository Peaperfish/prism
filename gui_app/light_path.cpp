#include "light_path.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>

#include "imgui_internal.h"
#include "prism/gui/layout.hpp"
#include "prism/gui/paper_grade.hpp"
#include "prism/gui/typed_value.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace prism::gui_app {

namespace {

// How strongly a row past what's on screen is drawn: quiet, but still a working row.
constexpr float dimmed_alpha = 0.5f;

// A press on the row, or on its clip marks, acts at once and doesn't hold the active id: it isn't a
// control being used, so it doesn't stop J, T, I or the clip report (see active_control.hpp).
constexpr ImGuiButtonFlags press_only =
    ImGuiButtonFlags_PressedOnClick | static_cast<ImGuiButtonFlags>(ImGuiButtonFlags_NoHoldingActiveId);

float px(float mock_px) {
    return std::round(mock(mock_px));
}

// A dimmed row draws everything in it at a lower alpha, and a window opened from inside would take
// that alpha along. So tooltips and popups are opened at full strength, under this.
class FullStrength {
public:
    FullStrength() { ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f); }
    ~FullStrength() { ImGui::PopStyleVar(); }
    FullStrength(const FullStrength&) = delete;
    FullStrength& operator=(const FullStrength&) = delete;
};

ImU32 colour_u32(const gui::Rgba& c) {
    // GetColorU32 applies the style's alpha, so a dimmed row dims everything drawn in it.
    return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, c.a));
}

float text_width(std::string_view text) {
    return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
}

// Text in the current font, its line centred in a band that starts at `y`.
void text_in_band(ImDrawList* draw, float x, float y, float band, ImU32 colour,
                  std::string_view text) {
    draw->AddText(ImVec2(x, std::round(y + (band - ImGui::GetFontSize()) * 0.5f)), colour,
                  text.data(), text.data() + text.size());
}

gui::LightPathMetrics row_metrics() {
    gui::LightPathMetrics m;
    m.pad_top = px(9.0f);
    m.pad_bottom = px(10.0f);
    m.line = std::round(std::max(ImGui::GetFontSize(), switch_size().y));
    ImGui::PushFont(nullptr, token_type().small);
    m.secondary = std::ceil(ImGui::GetFontSize());
    ImGui::PopFont();
    m.slider = slider_height();
    m.tabs = segmented_height();
    m.strip_button = px(24.0f);
    m.gap = px(4.0f);
    return m;
}

ImU32 mark_colour(const Palette& colours, gui::ClipMarkKind kind) {
    switch (kind) {
        case gui::ClipMarkKind::below_black: return colours.clip_shadows_fill;
        case gui::ClipMarkKind::above_white: return colours.clip_highlights_fill;
        case gui::ClipMarkKind::out_of_gamut: return colours.clip_gamut_fill;
    }
    return colours.clip_highlights_fill;
}

// The dot on the rail. The viewed stage's is the panel's one piece of amber.
void draw_dot(ImDrawList* draw, ImVec2 centre, bool viewed) {
    const gui::TokenColours& c = token_colours();
    const float radius = mock(7.0f);
    const float border = px(2.0f);
    if (viewed) {
        draw->AddCircleFilled(centre, radius + mock(3.0f), colour_u32(c.accent_glow));
    }
    // Opaque first: the rail mustn't show through a dimmed dot.
    draw->AddCircleFilled(centre, radius,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(c.panel.r, c.panel.g, c.panel.b, 1.0f)));
    draw->AddCircleFilled(centre, radius, colour_u32(viewed ? c.accent : c.node));
    draw->AddCircle(centre, radius - border * 0.5f, colour_u32(viewed ? c.accent_ring : c.faint), 0,
                    border);
}

// The clip marks on their line, from `x`, in at most `room`: each with its words if all fit, else
// each with just its number (the colour says which). Their tooltip has the whole story. The line
// keeps its place when there is nothing to show.
void draw_clip_marks(const LightPathData& data, const gui::LightPathRow& row, float x, float y,
                     float height, float room, const LightPathCallbacks& cb) {
    if (row.marks.empty() || room <= 0.0f) {
        return;
    }
    const gui::TokenMetrics& m = token_metrics();
    const Palette& colours = palette(data.theme);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::PushFont(nullptr, token_type().small);

    const float dot = 2 * m.dot_radius + m.dot_gap;
    float words = 0.0f;
    for (const gui::ClipMark& mark : row.marks) {
        words += dot + text_width(mark.text) + m.dot_between;
    }
    const bool with_words = words - m.dot_between <= room;

    draw->PushClipRect(ImVec2(x, y), ImVec2(x + room, y + height), true);
    float at = x;
    const float cy = std::round(y + height * 0.5f);
    for (const gui::ClipMark& mark : row.marks) {
        const ImU32 colour = ImGui::GetColorU32(mark_colour(colours, mark.kind));
        const ImVec2 centre(at + m.dot_radius, cy);
        if (mark.fate == gui::ClipFate::lost) {
            draw->AddCircleFilled(centre, m.dot_radius, colour);
        } else {
            const float line = px(1.5f);
            draw->AddCircle(centre, m.dot_radius - line * 0.5f, colour, 0, line);
        }
        at += dot;
        const std::string& text = with_words ? mark.text : mark.percent;
        text_in_band(draw, at, y, height, ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
        at += text_width(text) + m.dot_between;
    }
    draw->PopClipRect();
    ImGui::PopFont();

    // An item of its own, for the tooltip's delay. Not a stop for Tab, and a press on it is still a
    // press on the row.
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav | ImGuiItemFlags_NoTabStop, true);
    const float width = std::clamp(at - m.dot_between - x, 1.0f, room);
    if (ImGui::InvisibleButton("##clip", ImVec2(width, height), press_only)) {
        cb.view_stage(row.stage);
    }
    ImGui::PopItemFlag();
    if (ImGui::IsItemHovered()) {
        cb.hover(gui::explain::clip_dot);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        const FullStrength full;
        ImGui::BeginTooltip();
        for (const gui::ClipMark& mark : row.marks) {
            ImGui::Text("%s: %s of pixels", mark.what.c_str(), mark.percent.c_str());
            ImGui::PushTextWrapPos(token_metrics().tooltip_wrap);
            ImGui::TextDisabled("%s", mark.note.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Filled dot: detail lost here.  Hollow dot: still recoverable.");
        ImGui::TextDisabled("Orange: above white.  Blue: below black.");
        ImGui::TextDisabled("Violet: too saturated for sRGB.");
        ImGui::EndTooltip();
    }
}

// The Test strip button at the right of the clip line, drawn in its pressed look while the strip
// is on. Returns its left edge.
float draw_strip_button(const LightPathData& data, float right, float y, float line_height,
                        const LightPathCallbacks& cb) {
    constexpr const char* label = "Test strip";
    ImGui::PushFont(nullptr, token_type().small);
    const float height = px(24.0f);
    const float width = text_width(label) + 2 * px(8.0f);
    const float left = right - width;
    ImGui::SetCursorScreenPos(ImVec2(left, std::round(y + (line_height - height) * 0.5f)));
    const bool can_act = data.strip_on || data.strip_available;
    ImGui::BeginDisabled(!can_act);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(6.0f));
    if (data.strip_on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button(label, ImVec2(width, height))) {
        cb.toggle_strip();
    }
    if (data.strip_on) {
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    ImGui::PopFont();
    const FullStrength full;
    if (can_act) {
        cb.describe(gui::explain::strip);
    } else {
        cb.explain(gui::explain::strip, false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                 ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", data.strip_hint.c_str());
        }
    }
    return left;
}

// Contrast's slider: half grades, 0 to 10, snapped. Typing takes a grade or a multiplier and gives
// the multiplier; a drag gives the grade's multiplier only when it moved to another half grade, so
// an off-grid value (from a recipe, or typed) is kept until then.
void draw_grade_slider(const gui::LightPathRow& row, float x, float y, float width,
                       const LightPathCallbacks& cb) {
    ImGui::BeginDisabled(!row.enabled);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::SetNextItemWidth(width);
    float position = row.slider_position;
    SliderTyping typing;
    const bool changed = styled_slider_typed(
        "##slider", &position, 0.0f, static_cast<float>(gui::grade_slider_steps),
        static_cast<float>(gui::grade_slider_neutral), row.slider_typed, &typing,
        ImGuiSliderFlags_AlwaysClamp, 1.0f);
    if (typing.text) {
        if (const std::optional<float> typed =
                gui::typed_row_value(gui::SimpleControl::contrast, *typing.text)) {
            if (cb.contrast_slider) {
                cb.contrast_slider(*typed, gui::SliderFrame{true, true, true});
            }
        }
    } else if (!typing.open && !typing.just_closed) {
        const gui::SliderFrame frame{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
        if ((frame.activated || frame.changed || frame.deactivated) && cb.contrast_slider) {
            cb.contrast_slider(changed ? std::optional<float>(gui::contrast_for_grade_position(position))
                                       : std::nullopt,
                               frame);
        }
    }
    cb.explain(gui::explain_id(row.stage), changed);
    {
        const FullStrength full;
        if (!typing.open && ImGui::BeginPopupContextItem("reset")) {
            if (ImGui::MenuItem(row.slider_reset.c_str())) {
                cb.reset_slider(gui::SimpleControl::contrast);
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndDisabled();
}

void draw_stage_slider(const gui::LightPathRow& row, float x, float y, float width,
                       const LightPathCallbacks& cb) {
    const gui::SimpleControl control = *row.slider;
    if (control == gui::SimpleControl::contrast) {
        draw_grade_slider(row, x, y, width, cb);
        return;
    }
    ImGui::BeginDisabled(!row.enabled);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::SetNextItemWidth(width);
    float position = row.slider_position;
    // Ctrl/Cmd-click types a value in the unit of the value line (stops, or the multiplier), not
    // the slider's position: the text is read by gui_core, and a value it accepts is one change
    // (all three parts of the gesture at once, so one undo step). One it rejects leaves the value.
    SliderTyping typing;
    const bool changed = styled_slider_typed("##slider", &position, gui::simple_min,
                                             gui::simple_max, 0.0f, row.slider_typed, &typing);
    if (typing.text) {
        if (const std::optional<float> typed = gui::typed_slider_position(control, *typing.text)) {
            cb.slider(control, *typed, gui::SliderFrame{true, true, true});
        }
    } else if (!typing.open && !typing.just_closed) {
        const gui::SliderFrame frame{ImGui::IsItemActivated(), changed,
                                     ImGui::IsItemDeactivated()};
        if (frame.activated || frame.changed || frame.deactivated) {
            cb.slider(control, position, frame);
        }
    }
    cb.explain(gui::explain_id(row.stage), changed);
    {
        const FullStrength full;
        if (!typing.open && ImGui::BeginPopupContextItem("reset")) {
            if (ImGui::MenuItem(row.slider_reset.c_str())) {
                cb.reset_slider(control);
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndDisabled();
}

// One tab per curve: choosing one is one undo step. Greyed out and fixed while the stage is off.
void draw_curve_tabs(const gui::LightPathRow& row, float x, float y, float width,
                     const LightPathCallbacks& cb) {
    std::array<const char*, std::size(gui::curve_kinds)> labels{};
    std::array<std::string, std::size(gui::curve_kinds)> names;
    int current = 0;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        names[i] = std::string(gui::curve_kind_name(gui::curve_kinds[i]));
        labels[i] = names[i].c_str();
        if (gui::curve_kinds[i] == row.curve) {
            current = static_cast<int>(i);
        }
    }
    ImGui::BeginDisabled(!row.enabled);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    const bool chosen = segmented(
        "##curve", labels, &current,
        [&](int i) {
            const FullStrength full;
            cb.describe(gui::explain_id(gui::curve_kinds[static_cast<std::size_t>(i)]));
            ImGui::PushID(i);
            if (ImGui::BeginPopupContextItem("reset_kind")) {
                if (ImGui::MenuItem("Reset to None")) {
                    cb.set_curve_kind(gui::CurveKind::none);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        },
        width);
    if (chosen) {
        cb.set_curve_kind(gui::curve_kinds[static_cast<std::size_t>(current)]);
    }
    ImGui::EndDisabled();
}

// The curve's one number: its name and value on one line, its slider on the next. The slider moves
// a position (gui_core maps it to the value, in ratios for Reinhard), so it takes no typed input:
// that would be a position, not the value.
void draw_curve_parameter(const gui::LightPathRow& row, const gui::LightPathRowLayout& layout,
                          float x, float top, float width, const LightPathCallbacks& cb) {
    const gui::CurveParameter& p = *row.parameter;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const gui::LightPathMetrics metrics = row_metrics();
    ImGui::PushFont(nullptr, token_type().small);
    const float line_y = top + *layout.parameter_y;
    text_in_band(draw, x, line_y, metrics.secondary, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                 p.label);
    text_in_band(draw, x + width - text_width(row.parameter_text), line_y, metrics.secondary,
                 ImGui::GetColorU32(row.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                 row.parameter_text);
    ImGui::PopFont();

    ImGui::BeginDisabled(!row.enabled);
    ImGui::SetCursorScreenPos(ImVec2(x, top + *layout.parameter_slider_y));
    ImGui::SetNextItemWidth(width);
    float position = gui::curve_slider_position(p, row.parameter_value);
    const bool changed = styled_slider("##curve_parameter", &position, 0.0f, 1.0f, 0.0f,
                                       ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    const gui::SliderFrame frame{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
    if (frame.activated || frame.changed || frame.deactivated) {
        // Unmoved, the value stays exactly as stored rather than going through the mapping.
        cb.curve_parameter(changed ? gui::curve_value_at(p, position) : row.parameter_value, frame);
    }
    cb.explain(row.curve == gui::CurveKind::gamma ? gui::explain::curve_gamma_value
                                                  : gui::explain::curve_white,
               changed);
    {
        const FullStrength full;
        if (ImGui::BeginPopupContextItem("reset_parameter")) {
            if (ImGui::MenuItem(row.parameter_reset.c_str())) {
                cb.reset_curve_parameter();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndDisabled();
}

// One row. Everything in it is placed from `layout`, and the row's own button comes last.
void draw_row(const LightPathData& data, const gui::LightPathRow& row,
              const gui::LightPathRowLayout& layout, const gui::LightPathMetrics& metrics,
              ImVec2 row_min, float width, float dot_x, const LightPathCallbacks& cb) {
    const gui::TokenColours& colours = token_colours();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 row_max(row_min.x + width, row_min.y + layout.height);

    ImGui::PushID(static_cast<int>(row.stage));
    if (row.dimmed) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * dimmed_alpha);
    }

    // The wash behind the viewed stage, and half of it under the mouse.
    const bool over = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(row_min, row_max);
    if (row.viewed || over) {
        gui::Rgba wash = colours.view_bg;
        wash.a *= row.viewed ? 1.0f : 0.5f;
        draw->AddRectFilled(row_min, row_max, colour_u32(wash), px(8.0f));
    }
    const float line_y = row_min.y + layout.line_y;
    draw_dot(draw, ImVec2(dot_x, std::round(line_y + metrics.line * 0.5f)), row.viewed);

    const float x = row_min.x + px(27.0f);
    const float right = row_max.x - px(8.0f);
    const float content = std::max(1.0f, right - x);

    // The first line: the name, the value and the switch.
    ImGui::PushFont(fonts().semibold, token_type().body);
    const ImU32 name_colour = ImGui::GetColorU32(row.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    text_in_band(draw, x, line_y, metrics.line, name_colour, row.name);
    const float name_end = x + text_width(row.name);
    ImGui::PopFont();

    float value_right = right;
    if (row.can_toggle) {
        const ImVec2 size = switch_size();
        ImGui::SetCursorScreenPos(ImVec2(
            right - size.x, std::round(line_y + (metrics.line - size.y) * 0.5f)));
        bool on = row.enabled;
        if (styled_switch("##on", &on)) {
            cb.set_stage_on(row.stage, on);
        }
        {
            const FullStrength full;
            cb.describe(gui::explain_id(row.stage));
        }
        value_right = right - size.x - px(8.0f);
    }
    // What's on the first line never wraps: what doesn't fit is shortened, or left out.
    const auto measure = [](std::string_view s) { return text_width(s); };
    float value_left = value_right;
    if (!row.value.empty()) {
        const bool small = row.stage == gui::StageId::matrix;
        ImGui::PushFont(nullptr, small ? token_type().small : token_type().body);
        const float room = std::max(0.0f, value_right - name_end - px(8.0f));
        const std::string shown = gui::ellipsize_middle(row.value, room, measure);
        value_left = value_right - text_width(shown);
        text_in_band(draw, value_left, line_y, metrics.line,
                     ImGui::GetColorU32(row.value_muted ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                     shown);
        ImGui::PopFont();
    }
    if (row.beyond_slider) {
        constexpr const char* flag = "(beyond the slider)";
        ImGui::PushFont(nullptr, token_type().small);
        const float flag_x = name_end + px(6.0f);
        if (flag_x + text_width(flag) + px(6.0f) <= value_left) {
            text_in_band(draw, flag_x, line_y, metrics.line,
                         ImGui::GetColorU32(ImGuiCol_TextDisabled), flag);
        }
        ImGui::PopFont();
    }

    if (layout.subtitle_y) {
        ImGui::PushFont(nullptr, token_type().small);
        text_in_band(draw, x, row_min.y + *layout.subtitle_y, metrics.secondary,
                     ImGui::GetColorU32(ImGuiCol_TextDisabled),
                     gui::ellipsize_middle(row.subtitle, content, measure));
        ImGui::PopFont();
    }
    if (layout.slider_y) {
        draw_stage_slider(row, x, row_min.y + *layout.slider_y, content, cb);
    }
    if (layout.tabs_y) {
        draw_curve_tabs(row, x, row_min.y + *layout.tabs_y, content, cb);
    }
    if (layout.parameter_y) {
        draw_curve_parameter(row, layout, x, row_min.y, content, cb);
    }

    // The clip line, with Exposure's Test strip button at its right.
    const float clip_y = row_min.y + layout.clip_y;
    float marks_right = right;
    if (row.test_strip) {
        marks_right = draw_strip_button(data, right, clip_y, layout.clip_height, cb) - px(8.0f);
    }
    draw_clip_marks(data, row, x, clip_y, layout.clip_height, marks_right - x, cb);

    // Last: the row itself, for the presses nothing above took. ImGui's first taker of a position
    // keeps it, so this one is never hovered over a control.
    ImGui::SetCursorScreenPos(row_min);
    if (ImGui::InvisibleButton("##row", ImVec2(width, layout.height), press_only)) {
        cb.view_stage(row.stage);
    }
    cb.explain(gui::explain_id(row.stage), false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        const bool over_value = !row.value_tooltip.empty() && ImGui::GetMousePos().x >= value_left &&
                                ImGui::GetMousePos().y < line_y + metrics.line;
        const FullStrength full;
        ImGui::SetTooltip("%s", over_value ? row.value_tooltip.c_str() : row.tooltip.c_str());
    }

    if (row.dimmed) {
        ImGui::PopStyleVar();
    }
    ImGui::PopID();
}

// Off, Original (Decode) and every stage, then Swap sides. The viewed stage is the right half of
// the split, the chosen one the left half. Choosing the viewed stage itself is the same as Off.
void draw_compare_popup(const LightPathData& data, const LightPathCallbacks& cb) {
    if (!ImGui::BeginPopup("##compare")) {
        return;
    }
    const std::optional<gui::StageId> shown = data.view.compare;
    if (ImGui::Selectable("Off", !shown)) {
        cb.set_compare(std::nullopt);
    }
    cb.describe(gui::explain::compare);
    for (const gui::StageInfo& stage : gui::pipeline_stages()) {
        const std::string caption = gui::stage_caption(stage.id);
        if (ImGui::Selectable(caption.c_str(), shown == stage.id)) {
            cb.set_compare(stage.id == data.view.viewed ? std::nullopt
                                                        : std::optional<gui::StageId>(stage.id));
        }
        cb.describe(gui::explain::compare);
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!shown);
    if (ImGui::Selectable("Swap sides")) {
        cb.swap_compare();
    }
    ImGui::EndDisabled();
    cb.describe(gui::explain::compare_swap);
    ImGui::EndPopup();
}

}  // namespace

std::vector<LightPathRowRect> draw_light_path(const LightPathData& data,
                                              const LightPathCallbacks& cb) {
    bool title_hovered = false;
    const bool compare_clicked =
        section_text("Light path", data.view.compare_text.c_str(), &title_hovered);
    const ImVec2 action_corner = ImGui::GetItemRectMax();
    cb.describe(gui::explain::compare);
    if (title_hovered) {
        cb.hover(gui::explain::light_path);
    }
    if (compare_clicked) {
        // Under the action, with their right edges together.
        ImGui::SetNextWindowPos(action_corner, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
        ImGui::OpenPopup("##compare");
    }
    draw_compare_popup(data, cb);

    const gui::LightPathMetrics metrics = row_metrics();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);

    std::vector<gui::LightPathRowLayout> layouts;
    layouts.reserve(data.view.rows.size());
    float bottom = origin.y;
    float first_dot = origin.y;
    float last_dot = origin.y;
    for (const gui::LightPathRow& row : data.view.rows) {
        layouts.push_back(gui::light_path_row_layout(row, metrics));
        const float dot_y = std::round(bottom + layouts.back().line_y + metrics.line * 0.5f);
        first_dot = layouts.size() == 1 ? dot_y : first_dot;
        last_dot = dot_y;
        bottom += layouts.back().height;
    }

    // The rail, under everything: from the first dot to the last.
    const float rail = px(2.0f);
    const float dot_x = origin.x + px(8.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(dot_x - rail * 0.5f, first_dot),
                                              ImVec2(dot_x + rail * 0.5f, last_dot),
                                              colour_u32(token_colours().rail));

    std::vector<LightPathRowRect> rects;
    rects.reserve(data.view.rows.size());
    float y = origin.y;
    for (std::size_t i = 0; i < data.view.rows.size(); ++i) {
        const ImVec2 min(origin.x, y);
        draw_row(data, data.view.rows[i], layouts[i], metrics, min, width, dot_x, cb);
        y += layouts[i].height;
        rects.push_back({data.view.rows[i].stage, min, ImVec2(min.x + width, y)});
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    return rects;
}

}  // namespace prism::gui_app

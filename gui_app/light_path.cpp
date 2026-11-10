#include "light_path.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <memory>

#include "imgui_internal.h"
#include "prism/gui/layout.hpp"
#include "prism/gui/paper_grade.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/ranges.hpp"
#include "prism/white_balance.hpp"
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

// The row's sides: its text starts after the rail's dot, and ends a little inside the row.
float row_left() { return px(27.0f); }
float row_right_inset() { return px(8.0f); }
// Between a row's name and its value, or the switch and what is left of it.
float first_line_gap() { return px(8.0f); }
// Around "(beyond the slider)".
float flag_gap() { return px(6.0f); }
// Whether the row says "(beyond the slider)" after its name. Contrast does not: its value already
// says "Softer than grade 0" or "Harder than grade 5", which is the same thing in more words.
bool shows_beyond_flag(const gui::LightPathRow& row) {
    return row.beyond_slider && row.stage != gui::StageId::contrast;
}
// The padding at each side of the Test strip and the Pick neutral buttons' labels.
float strip_button_padding() { return px(8.0f); }
float pick_button_padding() { return px(6.0f); }
// The Brush and Clear buttons of Dodge & burn's controls line.
float brush_button_padding() { return px(8.0f); }
// Between a Tone curve tab's edge and its label.
float tab_padding() { return px(6.0f); }
// The least a White balance slider's track is wide.
float wb_min_track() { return px(72.0f); }

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

// Writes down a text that has to fit, for the tests (the font it is measured in is the current one).
void note_fit(const LightPathData& data, const char* what, std::string_view text,
              std::string_view shown, float room) {
    if (data.fits != nullptr) {
        data.fits->push_back({what, std::string(text), std::string(shown), text_width(text), room});
    }
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
    if (data.fits != nullptr) {
        std::string all_words;
        std::string all_percents;
        for (const gui::ClipMark& mark : row.marks) {
            all_words += (all_words.empty() ? "" : "  ") + mark.text;
            all_percents += (all_percents.empty() ? "" : "  ") + mark.percent;
        }
        data.fits->push_back({"Clip note", all_words, with_words ? all_words : all_percents,
                              words - m.dot_between, room});
    }

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
// is on. Like Pick neutral it is as tall as its line and the gap (which is empty above and below
// the line anyway), so it adds nothing to the row. Returns its left edge.
float draw_strip_button(const LightPathData& data, float right, float y, float line_height,
                        float gap, const LightPathCallbacks& cb) {
    const std::string label(gui::light_path_text::test_strip);
    ImGui::PushFont(nullptr, token_type().small);
    const float height = line_height + gap;
    const float width = text_width(label) + 2 * strip_button_padding();
    const float left = right - width;
    ImGui::SetCursorScreenPos(ImVec2(left, y - gap * 0.5f));
    const bool can_act = data.strip_on || data.strip_available;
    ImGui::BeginDisabled(!can_act);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(strip_button_padding(), 0.0f));
    if (data.strip_on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button(label.c_str(), ImVec2(width, height))) {
        cb.toggle_strip();
    }
    if (data.strip_on) {
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar(2);
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

// The Pick neutral button at the right of White balance's subtitle line, drawn in its pressed look
// while Pick mode is on. It is as tall as the line and its gap (which is empty above and below the
// line anyway), so it adds nothing to the row. Acts on the press. Returns its left edge.
float draw_pick_button(const LightPathData& data, float right, float line_y, float line_height,
                       float gap, const LightPathCallbacks& cb) {
    const std::string label(gui::light_path_text::pick_neutral);
    ImGui::PushFont(nullptr, token_type().small);
    const float height = line_height + gap;
    const float width = text_width(label) + 2 * pick_button_padding();
    const float left = right - width;
    ImGui::SetCursorScreenPos(ImVec2(left, line_y - gap * 0.5f));
    const bool can_act = data.pick_on || data.pick_available;
    ImGui::BeginDisabled(!can_act);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pick_button_padding(), 0.0f));
    if (data.pick_on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    ImGui::PushID("pick");
    if (ImGui::ButtonEx(label.c_str(), ImVec2(width, height), press_only) && cb.toggle_pick) {
        cb.toggle_pick();
    }
    ImGui::PopID();
    if (data.pick_on) {
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar(2);
    ImGui::EndDisabled();
    ImGui::PopFont();
    const FullStrength full;
    if (can_act) {
        cb.describe(gui::explain::pick);
    } else {
        cb.explain(gui::explain::pick, false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                 ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", data.pick_hint.c_str());
        }
    }
    return left;
}

// Dodge & burn's controls line: Brush, the Strength slider with its value, and Clear. The Brush is
// drawn in its pressed look while Brush mode is on, and acts on the press like Pick neutral. The
// buttons are as tall as the line. Strength is the brush's setting, so it works whether the stage
// is on or off; Clear is greyed out with no strokes.
void draw_brush_controls(const LightPathData& data, const gui::LightPathRow& row, float x, float y,
                         float width, const LightPathCallbacks& cb) {
    const gui::LightPathRow::BrushControls& controls = *row.brush_controls;
    const float height = slider_height();
    const std::string brush_label(gui::light_path_text::brush);
    const std::string clear_label(gui::light_path_text::clear_strokes);
    ImGui::PushFont(nullptr, token_type().small);
    const float brush_width = text_width(brush_label) + 2 * brush_button_padding();
    const float clear_width = text_width(clear_label) + 2 * brush_button_padding();
    const float value_width = text_width(gui::light_path_text::widest_strength);
    ImGui::PopFont();
    const float gap = first_line_gap();
    const float track = std::max(1.0f, width - brush_width - clear_width - value_width - 3 * gap);

    const auto button = [&](const std::string& label, float left, float button_width, bool pressed,
                            bool can_act, const char* id, std::string_view explain_id,
                            const std::string& hint, const std::function<void()>& act) {
        ImGui::PushFont(nullptr, token_type().small);
        ImGui::SetCursorScreenPos(ImVec2(left, y));
        ImGui::BeginDisabled(!can_act);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(brush_button_padding(), 0.0f));
        if (pressed) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        ImGui::PushID(id);
        if (ImGui::ButtonEx(label.c_str(), ImVec2(button_width, height), press_only) && act) {
            act();
        }
        ImGui::PopID();
        if (pressed) {
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar(2);
        ImGui::EndDisabled();
        ImGui::PopFont();
        const FullStrength full;
        if (can_act) {
            cb.describe(explain_id);
        } else {
            cb.explain(explain_id, false);
            if (!hint.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                                      ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", hint.c_str());
            }
        }
    };
    button(brush_label, x, brush_width, data.brush_on, data.brush_on || data.brush_available,
           "brush", gui::explain::dodge_burn_brush, data.brush_hint, cb.toggle_brush);

    ImGui::PushID("strength");
    ImGui::SetCursorScreenPos(ImVec2(x + brush_width + gap, y));
    ImGui::SetNextItemWidth(track);
    float strength = controls.strength;
    const bool changed =
        styled_slider("##slider", &strength, gui::brush_strength_min, gui::brush_strength_max,
                      gui::brush_strength_min, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    // A step of 0.1: the value the row will show next, also for the text beside the slider.
    const float snapped = gui::snap_brush_strength(strength);
    if (changed && snapped != controls.strength && cb.brush_strength) {
        cb.brush_strength(snapped);
    }
    cb.explain(gui::explain::dodge_burn_strength, changed);
    {
        const FullStrength full;
        if (ImGui::BeginPopupContextItem("reset")) {
            if (ImGui::MenuItem(controls.strength_reset.c_str()) && cb.brush_strength) {
                cb.brush_strength(gui::brush_strength_default);
            }
            ImGui::EndPopup();
        }
    }
    ImGui::PopID();
    const float value_left = x + brush_width + gap + track + gap;
    const std::string value = changed ? gui::brush_strength_text(snapped) : controls.strength_text;
    ImGui::PushFont(nullptr, token_type().small);
    text_in_band(ImGui::GetWindowDrawList(), value_left + value_width - text_width(value), y, height,
                 ImGui::GetColorU32(ImGuiCol_Text), value);
    note_fit(data, "Strength value", value, value, value_width);
    if (data.fits != nullptr) {
        data.fits->push_back({"Strength track", "track", track >= wb_min_track() ? "track" : "",
                              wb_min_track(), track});
    }
    ImGui::PopFont();

    button(clear_label, x + width - clear_width, clear_width, false, controls.can_clear, "clear",
           gui::explain::dodge_burn_clear, std::string(), cb.clear_strokes);
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

// The captions of White balance's two sliders, and the room they take at the sliders' left: the
// wider caption's width, so both sliders start at the same place whatever they are set to. Each
// line reads caption, slider, value, and the values share one column, as wide as the widest value
// there can be, so both sliders also end at the same place.
constexpr const char* kelvin_caption = "Kelvin";
constexpr const char* tint_caption = "Tint";
constexpr const char* widest_kelvin = "15000 K";
constexpr const char* widest_tint = "\xE2\x88\x92" "100";  // U+2212, not a hyphen

float white_balance_caption_width() {
    ImGui::PushFont(nullptr, token_type().small);
    const float width = std::max(text_width(kelvin_caption), text_width(tint_caption));
    ImGui::PopFont();
    return std::ceil(width) + px(8.0f);
}

float white_balance_value_width() {
    ImGui::PushFont(nullptr, token_type().small);
    const float width = std::max(text_width(widest_kelvin), text_width(widest_tint));
    ImGui::PopFont();
    return std::ceil(width);
}

// One of White balance's sliders, as caption, slider, value. Kelvin's position is even in mireds
// (gui_core maps it); Tint's is the tint itself, in whole numbers. Typing takes the number as the
// value shows it. A drag reports the value only on a frame that moved it, so a press alone leaves
// an As shot photo As shot.
void draw_white_balance_slider(const LightPathData& data, const gui::LightPathRow& row,
                               gui::WhiteBalanceControl control, float x, float y, float width,
                               const LightPathCallbacks& cb) {
    const gui::LightPathRow::WhiteBalanceSliders& wb = *row.white_balance;
    const bool kelvin = control == gui::WhiteBalanceControl::kelvin;
    const float caption = white_balance_caption_width();
    const float value_width = white_balance_value_width();
    const float track = std::max(1.0f, width - caption - first_line_gap() - value_width);
    ImGui::PushFont(nullptr, token_type().small);
    text_in_band(ImGui::GetWindowDrawList(), x, y, slider_height(),
                 ImGui::GetColorU32(ImGuiCol_TextDisabled), kelvin ? kelvin_caption : tint_caption);
    ImGui::PopFont();

    ImGui::PushID(kelvin ? "kelvin" : "tint");
    ImGui::BeginDisabled(!row.enabled);
    ImGui::SetCursorScreenPos(ImVec2(x + caption, y));
    ImGui::SetNextItemWidth(track);
    float position = kelvin ? wb.kelvin_position : wb.tint_position;
    SliderTyping typing;
    const bool changed =
        kelvin ? styled_slider_typed("##slider", &position, 0.0f, 1.0f, wb.kelvin_as_shot,
                                     wb.kelvin_typed, &typing, ImGuiSliderFlags_AlwaysClamp)
               : styled_slider_typed("##slider", &position, static_cast<float>(tint_range.min),
                                     static_cast<float>(tint_range.max), wb.tint_as_shot,
                                     wb.tint_typed, &typing, ImGuiSliderFlags_AlwaysClamp, 1.0f);
    if (typing.text) {
        if (const std::optional<double> typed =
                gui::typed_white_balance_value(control, *typing.text)) {
            if (cb.white_balance_slider) {
                cb.white_balance_slider(control, *typed, gui::SliderFrame{true, true, true});
            }
        }
    } else if (!typing.open && !typing.just_closed) {
        const gui::SliderFrame frame{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
        if ((frame.activated || frame.changed || frame.deactivated) && cb.white_balance_slider) {
            const double value = kelvin ? gui::kelvin_at_slider_position(position)
                                        : static_cast<double>(position);
            cb.white_balance_slider(control, changed ? std::optional<double>(value) : std::nullopt,
                                    frame);
        }
    }
    cb.explain(gui::explain_id(row.stage), changed);
    {
        const FullStrength full;
        if (!typing.open && ImGui::BeginPopupContextItem("reset")) {
            if (ImGui::MenuItem(row.slider_reset.c_str()) && cb.reset_white_balance) {
                cb.reset_white_balance();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::PopID();

    // The value, right-aligned in the shared column. On the frame a drag moves the slider it is the
    // slider's own (snapped as the setter will), else the one the row was made with.
    const std::string value =
        changed ? (kelvin ? gui::format_kelvin(gui::kelvin_at_slider_position(position))
                          : gui::format_tint(position))
                : (kelvin ? wb.kelvin_text : wb.tint_text);
    ImGui::PushFont(nullptr, token_type().small);
    text_in_band(ImGui::GetWindowDrawList(), x + width - text_width(value), y, slider_height(),
                 ImGui::GetColorU32(row.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), value);
    note_fit(data, kelvin ? "Kelvin value" : "Tint value", value, value, value_width);
    if (data.fits != nullptr) {
        // The track keeps its least width: a sliver of a slider is not a slider.
        data.fits->push_back({kelvin ? "Kelvin track" : "Tint track", "track", track >= wb_min_track() ? "track" : "",
                              wb_min_track(), track});
    }
    ImGui::PopFont();
}

// One tab per curve: choosing one is one undo step. Greyed out and fixed while the stage is off.
void draw_curve_tabs(const LightPathData& data, const gui::LightPathRow& row, float x, float y,
                     float width, const LightPathCallbacks& cb) {
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
    if (data.fits != nullptr) {
        // Every label with room around it, in the face the current tab is drawn in.
        const float room = segmented_tab_width(width, static_cast<int>(labels.size())) -
                           2 * tab_padding();
        ImGui::PushFont(fonts().semibold, token_type().small);
        for (const std::string& name : names) {
            note_fit(data, "Tone curve tab", name, name, room);
        }
        ImGui::PopFont();
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
void draw_curve_parameter(const LightPathData& data, const gui::LightPathRow& row,
                          const gui::LightPathRowLayout& layout, float x, float top, float width,
                          const LightPathCallbacks& cb) {
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
    if (data.fits != nullptr) {
        const std::string both = std::string(p.label) + " " + row.parameter_text;
        data.fits->push_back({"Curve number", both, both,
                              text_width(p.label) + first_line_gap() + text_width(row.parameter_text),
                              width});
    }
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

    const float x = row_min.x + row_left();
    const float right = row_max.x - row_right_inset();
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
            // Dodge & burn's switch has its own words: the row's are about the strokes.
            cb.describe(row.stage == gui::StageId::dodge_burn ? gui::explain::dodge_burn_switch
                                                              : gui::explain_id(row.stage));
        }
        value_right = right - size.x - first_line_gap();
    }
    // What's on the first line never wraps: what doesn't fit is shortened, or left out.
    const auto measure = [](std::string_view s) { return text_width(s); };
    float value_left = value_right;
    if (!row.value.empty()) {
        const bool small = row.stage == gui::StageId::matrix;
        ImGui::PushFont(nullptr, small ? token_type().small : token_type().body);
        const float room = std::max(0.0f, value_right - name_end - first_line_gap());
        const std::string shown = gui::ellipsize_middle(row.value, room, measure);
        note_fit(data, "Value", row.value, shown, room);
        value_left = value_right - text_width(shown);
        text_in_band(draw, value_left, line_y, metrics.line,
                     ImGui::GetColorU32(row.value_muted ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                     shown);
        ImGui::PopFont();
    }
    if (shows_beyond_flag(row)) {
        const std::string flag(gui::light_path_text::beyond_slider);
        ImGui::PushFont(nullptr, token_type().small);
        const float flag_x = name_end + flag_gap();
        const bool fits = flag_x + text_width(flag) + flag_gap() <= value_left;
        if (fits) {
            text_in_band(draw, flag_x, line_y, metrics.line,
                         ImGui::GetColorU32(ImGuiCol_TextDisabled), flag);
        }
        note_fit(data, "Beyond the slider", flag, fits ? flag : "",
                 value_left - flag_gap() - flag_x);
        ImGui::PopFont();
    }

    if (layout.subtitle_y) {
        float subtitle_room = content;
        if (row.pick_neutral) {
            const float left = draw_pick_button(data, right, row_min.y + *layout.subtitle_y,
                                                metrics.secondary, metrics.gap, cb);
            subtitle_room = std::max(1.0f, left - first_line_gap() - x);
        }
        ImGui::PushFont(nullptr, token_type().small);
        const std::string subtitle = gui::ellipsize_middle(row.subtitle, subtitle_room, measure);
        text_in_band(draw, x, row_min.y + *layout.subtitle_y, metrics.secondary,
                     ImGui::GetColorU32(ImGuiCol_TextDisabled), subtitle);
        note_fit(data, "Subtitle", row.subtitle, subtitle, subtitle_room);
        ImGui::PopFont();
    }
    if (row.white_balance && layout.slider_y && layout.second_slider_y) {
        draw_white_balance_slider(data, row, gui::WhiteBalanceControl::kelvin, x,
                                  row_min.y + *layout.slider_y, content, cb);
        draw_white_balance_slider(data, row, gui::WhiteBalanceControl::tint, x,
                                  row_min.y + *layout.second_slider_y, content, cb);
    } else if (layout.slider_y && row.slider) {
        draw_stage_slider(row, x, row_min.y + *layout.slider_y, content, cb);
    }
    if (layout.controls_y && row.brush_controls) {
        draw_brush_controls(data, row, x, row_min.y + *layout.controls_y, content, cb);
    }
    if (layout.tabs_y) {
        draw_curve_tabs(data, row, x, row_min.y + *layout.tabs_y, content, cb);
    }
    if (layout.parameter_y) {
        draw_curve_parameter(data, row, layout, x, row_min.y, content, cb);
    }

    // The clip line, with Exposure's Test strip button at its right.
    const float clip_y = row_min.y + layout.clip_y;
    float marks_right = right;
    if (row.test_strip) {
        marks_right = draw_strip_button(data, right, clip_y, layout.clip_height, metrics.gap, cb) -
                      first_line_gap();
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
        if (stage.hidden) {
            continue;
        }
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

float light_path_min_width() {
    const std::string flag(gui::light_path_text::beyond_slider);
    const gui::TokenMetrics& tm = token_metrics();

    // Every text that can be on a row's first line, in the worst light each stage can put it in:
    // a stage's value at its extremes, a stage that is off ("when on"), a chosen white balance at
    // the ends of its sliders, and every kind of file.
    std::vector<gui::EditSettings> variants;
    const auto add = [&](auto&& change) {
        for (const bool on : {true, false}) {
            gui::EditSettings s;
            change(s, on);
            variants.push_back(s);
        }
    };
    for (const float stops : {-10.0f, 10.0f}) {
        add([&](gui::EditSettings& s, bool on) { s.exposure_stops = stops; s.exposure_on = on; });
    }
    for (const float saturation : {0.0f, 1.0f, 10.0f}) {
        add([&](gui::EditSettings& s, bool on) { s.saturation = saturation; s.saturation_on = on; });
    }
    // Soft and hard ends, and values between the half grades.
    for (const float contrast : {0.3f, 0.6f, 1.15f, 1.45f, 2.4f, 6.0f}) {
        add([&](gui::EditSettings& s, bool on) { s.contrast = contrast; s.contrast_on = on; });
    }
    for (const prism::WhiteBalance light : {prism::WhiteBalance{15000, -100}, prism::WhiteBalance{2000, 100}}) {
        add([&](gui::EditSettings& s, bool on) { s.white_balance = light; s.white_balance_on = on; });
    }
    // Dodge & burn at its widest: the most of both kinds (the text gives out long before 999
    // strokes of each could be made), on and off.
    {
        auto field = std::make_shared<prism::StrokeField>();
        field->aspect = 1.5f;
        for (int i = 0; i < 1998; ++i) {
            field->strokes.push_back(std::make_shared<const prism::FieldStroke>(
                prism::FieldStroke{{{0.5f, 0.5f}}, 0.05f, 0.5f, i < 999 ? -0.5f : 0.5f}));
        }
        const gui::Strokes most(std::move(field));
        add([&](gui::EditSettings& s, bool on) { s.strokes = most; s.dodge_burn_on = on; });
    }
    add([](gui::EditSettings& s, bool on) { s.matrix = gui::MatrixFile{}; s.matrix_on = on; });
    add([](gui::EditSettings& s, bool) { s.raw_camera_matrix = gui::MatrixFile{}; });
    add([](gui::EditSettings&, bool) {});

    float row_need = 0.0f;  // the room inside a row, between its margins
    const float switch_room = switch_size().x + first_line_gap();
    const auto first_line = [&](const gui::LightPathRow& row) {
        ImGui::PushFont(fonts().semibold, token_type().body);
        const float name = text_width(row.name);
        ImGui::PopFont();
        const bool small = row.stage == gui::StageId::matrix;
        ImGui::PushFont(nullptr, small ? token_type().small : token_type().body);
        const float value = text_width(row.value);
        ImGui::PopFont();
        ImGui::PushFont(nullptr, token_type().small);
        const float between =
            shows_beyond_flag(row) ? 2 * flag_gap() + text_width(flag) : first_line_gap();
        ImGui::PopFont();
        const float toggle = row.can_toggle ? switch_room : 0.0f;
        row_need = std::max(row_need, name + std::max(first_line_gap(), between) + value + toggle);
    };
    const auto small_text = [&](std::string_view text) {
        ImGui::PushFont(nullptr, token_type().small);
        const float width = text_width(text);
        ImGui::PopFont();
        return width;
    };
    const float pick_room = first_line_gap() + small_text(gui::light_path_text::pick_neutral) +
                            2 * pick_button_padding();
    for (const gui::EditSettings& s : variants) {
        for (const bool light_known : {true, false}) {
            for (const gui::InputKind kind : {gui::InputKind::raw, gui::InputKind::image}) {
                const std::optional<prism::Chromaticity> white =
                    light_known ? std::optional<prism::Chromaticity>(
                                      prism::white_of(prism::WhiteBalance{5328, 26.7}))
                                : std::nullopt;
                for (const char* file : {"RAW", "JPEG", "PNG", "HEIC"}) {
                    const gui::LightPathView view =
                        gui::light_path_view({s, kind, file, gui::final_stage, std::nullopt, nullptr, white});
                    for (const gui::LightPathRow& row : view.rows) {
                        first_line(row);
                        if (row.parameter) {  // the Tone curve's number: its name and value
                            row_need = std::max(row_need, small_text(row.parameter->label) +
                                                              first_line_gap() +
                                                              small_text(row.parameter_text));
                        }
                        if (!row.subtitle.empty()) {
                            row_need = std::max(row_need, small_text(row.subtitle) +
                                                              (row.pick_neutral ? pick_room : 0.0f));
                        }
                    }
                }
            }
        }
    }

    // Dodge & burn's controls line: Brush, a track, the value and Clear.
    row_need = std::max(row_need, small_text(gui::light_path_text::brush) + wb_min_track() +
                                      small_text(gui::light_path_text::widest_strength) +
                                      small_text(gui::light_path_text::clear_strokes) +
                                      4 * brush_button_padding() + 3 * first_line_gap());

    // White balance's sliders: caption, a track, value.
    row_need = std::max(row_need, white_balance_caption_width() + wb_min_track() +
                                      first_line_gap() + white_balance_value_width());

    // The Tone curve's tabs, each label (in the face the current one is drawn in) with room around
    // it, and its number's line.
    ImGui::PushFont(fonts().semibold, token_type().small);
    float widest_tab = 0.0f;
    for (const gui::CurveKind kind : gui::curve_kinds) {
        widest_tab = std::max(widest_tab, text_width(gui::curve_kind_name(kind)));
    }
    ImGui::PopFont();
    row_need = std::max(row_need, segmented_width_for(std::ceil(widest_tab) + 2 * tab_padding(),
                                                      static_cast<int>(std::size(gui::curve_kinds))));

    // A clip note beside its button: one mark with its words, whichever kind and however many
    // digits. (Two or three marks on a row that cannot hold their words fall back to their numbers,
    // which is by design.)
    float note = 0.0f;
    for (const std::uint64_t count : {100000ull, 88000ull, 8800ull, 1ull}) {
        for (const int kind : {0, 1, 2}) {
            gui::StageClip clip;
            clip.ran = true;
            gui::ClipCounts& counts = kind == 0 ? clip.under : kind == 1 ? clip.over : clip.gamut;
            counts.any = count;
            (kind == 0 ? clip.under_fate : kind == 1 ? clip.over_fate : clip.gamut_fate) =
                gui::ClipFate::recoverable;
            for (const gui::ClipMark& mark : gui::clip_marks(clip, 100000)) {
                note = std::max(note, 2 * tm.dot_radius + tm.dot_gap + small_text(mark.text));
            }
        }
    }
    row_need = std::max(row_need, note);
    row_need = std::max(row_need, note + first_line_gap() + small_text(gui::light_path_text::test_strip) +
                                          2 * strip_button_padding());

    // The header: the title, the gap between items, and the longest Compare action.
    ImGui::PushFont(fonts().semibold, token_type().section);
    const float title = text_width(gui::light_path_text::title);
    ImGui::PopFont();
    float action = 0.0f;
    ImGui::PushFont(fonts().regular, token_type().section);
    action = std::max(action, text_width(gui::compare_action_text(gui::final_stage, std::nullopt)));
    for (const gui::StageInfo& stage : gui::pipeline_stages()) {
        if (stage.hidden) {
            continue;
        }
        const gui::StageId viewed = stage.id == gui::final_stage ? gui::StageId::decode : gui::final_stage;
        action = std::max(action, text_width(gui::compare_action_text(viewed, stage.id)));
    }
    ImGui::PopFont();
    const float header_need = title + ImGui::GetStyle().ItemSpacing.x + action;

    const float inside =
        std::max(row_need + row_left() + row_right_inset(), header_need);
    // The panel's own padding at both sides, and the scrollbar that takes room when the panel scrolls.
    return std::ceil(inside + 2 * tm.panel_pad.x + ImGui::GetStyle().ScrollbarSize);
}

float light_path_min_mock_px() {
    return light_path_min_width() / mock(1.0f);
}

std::vector<LightPathRowRect> draw_light_path(const LightPathData& data,
                                              const LightPathCallbacks& cb) {
    bool title_hovered = false;
    const float header_x = ImGui::GetCursorScreenPos().x;
    const bool compare_clicked =
        section_text(std::string(gui::light_path_text::title).c_str(), data.view.compare_text.c_str(), &title_hovered);
    const ImVec2 action_corner = ImGui::GetItemRectMax();
    if (data.fits != nullptr) {
        // The title and the action share a line: the action sits at the right, and must leave the
        // usual gap between items after the title.
        ImGui::PushFont(fonts().semibold, token_type().section);
        const float title_end = header_x + text_width(gui::light_path_text::title);
        ImGui::PopFont();
        data.fits->push_back({"Header", std::string(gui::light_path_text::title) + " " + data.view.compare_text,
                              ImGui::GetItemRectMin().x - title_end >= ImGui::GetStyle().ItemSpacing.x
                                  ? std::string(gui::light_path_text::title) + " " + data.view.compare_text
                                  : std::string(),
                              ImGui::GetStyle().ItemSpacing.x, ImGui::GetItemRectMin().x - title_end});
    }
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

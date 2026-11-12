#include "framing_bar.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "prism/gui/explanations.hpp"
#include "prism/gui/framing.hpp"
#include "prism/gui/layout.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace prism::gui_app {

namespace {

// #58's words on the bar.
constexpr std::string_view aspect_label = "Aspect";
constexpr std::string_view straighten_label = "Straighten";
constexpr std::string_view reset_label = "Reset";
constexpr std::string_view done_label = "Done (Return)";
constexpr std::string_view done_short = "Done";
constexpr std::string_view turn_key = "X";

ImU32 u32(const gui::Rgba& c) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
}

float text_width(std::string_view text) {
    return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
}

float px(float mock_px) {
    return std::round(mock(mock_px));
}

float small_line() {
    ImGui::PushFont(nullptr, token_type().small);
    const float h = std::ceil(ImGui::GetTextLineHeight());
    ImGui::PopFont();
    return h;
}

float gap() {
    return px(8.0f);
}

float line_gap() {
    return px(6.0f);
}

// A button's width for `label` with the frame padding.
float button_width(std::string_view label) {
    return std::ceil(text_width(label) + 2 * ImGui::GetStyle().FramePadding.x);
}

// The ⇄ symbol: two arrows, the upper pointing right and the lower left (the interface font has no
// glyph for it), `width` wide and centred at `centre`.
void draw_turn_arrows(ImDrawList* draw, ImVec2 centre, float width, ImU32 colour) {
    const float half = width * 0.5f;
    const float rise = std::max(2.0f, std::round(width * 0.18f));
    const float head = std::max(2.0f, std::round(width * 0.28f));
    const float thick = std::max(1.0f, mock(1.25f));
    const float top = std::round(centre.y - rise);
    const float bottom = std::round(centre.y + rise);
    draw->AddLine(ImVec2(centre.x - half, top), ImVec2(centre.x + half, top), colour, thick);
    draw->AddLine(ImVec2(centre.x + half, top), ImVec2(centre.x + half - head, top - head), colour,
                  thick);
    draw->AddLine(ImVec2(centre.x - half, bottom), ImVec2(centre.x + half, bottom), colour, thick);
    draw->AddLine(ImVec2(centre.x - half, bottom), ImVec2(centre.x - half + head, bottom + head),
                  colour, thick);
}

// The widest the angle's words can be, so the slider never moves as they change.
float widest_words() {
    float widest = 0.0f;
    for (const float angle : {-44.95f, 44.95f, -0.04f, 0.04f, 0.0f}) {
        widest = std::max(widest, text_width(gui::framing_turn_words(angle)));
    }
    return std::ceil(widest);
}

float widest_aspect_name() {
    float widest = 0.0f;
    for (const Aspect aspect : all_aspects) {
        widest = std::max(widest, text_width(gui::framing_aspect_name(aspect)));
    }
    return std::ceil(widest);
}

void note(ImVec2* min, ImVec2* max) {
    *min = ImGui::GetItemRectMin();
    *max = ImGui::GetItemRectMax();
}

}  // namespace

float framing_bar_height() {
    const gui::TokenMetrics& m = token_metrics();
    return std::ceil(2 * m.badge_padding.y + 2 * ImGui::GetFrameHeight() + 2 * small_line() +
                     3 * line_gap());
}

void draw_framing_bar(const FramingBarData& data, const FramingBarCallbacks& cb, float width,
                      FramingBarRects* rects) {
    FramingBarRects local;
    FramingBarRects& r = rects != nullptr ? *rects : local;
    r = {};
    const gui::TokenColours& tokens = token_colours();
    const gui::TokenMetrics& metrics = token_metrics();
    const float height = framing_bar_height();
    const auto describe = [&](std::string_view id) {
        if (cb.describe) {
            cb.describe(id);
        }
    };

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(u32(tokens.strip_bar_bg)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(u32(tokens.strip_bar_text)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(metrics.badge_padding.x, metrics.badge_padding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics.overlay_rounding);
    ImGui::BeginChild("framing_bar", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    r.bar_min = ImGui::GetWindowPos();
    r.bar_max = ImVec2(r.bar_min.x + width, r.bar_min.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float inner = std::max(1.0f, width - 2 * metrics.badge_padding.x);
    const float right_edge = origin.x + inner;
    const float frame_h = ImGui::GetFrameHeight();
    const float line2_y = origin.y + frame_h + line_gap();
    const float line3_y = line2_y + frame_h + line_gap();
    const float line4_y = line3_y + small_line() + line_gap();

    // --- Line 1: the shape on the left, Reset and Done on the right. -----------------------------
    ImGui::PushFont(fonts().semibold, token_type().body);
    const float aspect_label_w = text_width(aspect_label);
    const float straighten_label_w = text_width(straighten_label);
    ImGui::PopFont();
    const float chevron_room = frame_h;
    const float combo_w = widest_aspect_name() + 2 * ImGui::GetStyle().FramePadding.x + chevron_room;
    const float arrows_w = std::round(ImGui::GetFontSize() * 1.1f);
    const float turn_w = std::ceil(arrows_w + gap() * 0.5f + text_width(turn_key) +
                                   2 * ImGui::GetStyle().FramePadding.x);
    const float reset_w = button_width(reset_label);
    // What gives way on a narrow canvas, in order: the "Aspect" label, the "(Return)", then Reset goes
    // to the end of the second line.
    const float left_part = combo_w + gap() + turn_w;
    const float right_full = reset_w + gap() + button_width(done_label);
    const bool show_aspect_label = aspect_label_w + gap() + left_part + 2 * gap() + right_full <= inner;
    const bool reset_on_first = left_part + 2 * gap() + reset_w + gap() + button_width(done_short) <= inner;
    const std::string_view done_text =
        left_part + 2 * gap() + right_full <= inner ? done_label : done_short;
    r.done_label = std::string(done_text);
    const float done_w = button_width(done_text);

    float x = origin.x;
    if (show_aspect_label) {
        ImGui::PushFont(fonts().semibold, token_type().body);
        draw->AddText(ImVec2(x, std::round(origin.y + (frame_h - ImGui::GetFontSize()) * 0.5f)),
                      ImGui::GetColorU32(ImGuiCol_Text), aspect_label.data(),
                      aspect_label.data() + aspect_label.size());
        ImGui::PopFont();
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(ImVec2(x, origin.y),
                                       ImVec2(x + aspect_label_w, origin.y + frame_h)) &&
            cb.hover) {
            cb.hover(gui::explain::framing_aspect);
        }
        x += aspect_label_w + gap();
    }
    ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
    ImGui::SetNextItemWidth(combo_w);
    const std::string current(gui::framing_aspect_name(data.aspect));
    const bool open = styled_begin_combo("##aspect", current.c_str());
    note(&r.aspect_min, &r.aspect_max);
    describe(gui::explain::framing_aspect);
    if (open) {
        for (const Aspect aspect : all_aspects) {
            const std::string text(gui::framing_aspect_menu_text(aspect));
            if (ImGui::Selectable(text.c_str(), aspect == data.aspect) && aspect != data.aspect &&
                cb.set_aspect) {
                cb.set_aspect(aspect);
            }
            describe(gui::explain::framing_aspect);
        }
        ImGui::EndCombo();
    }
    x += combo_w + gap();

    // ⇄ X: a button drawn by hand around the arrows.
    ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
    const bool turned = ImGui::InvisibleButton("##turn_aspect", ImVec2(turn_w, frame_h));
    note(&r.turn_min, &r.turn_max);
    {
        const ImGuiCol fill = ImGui::IsItemActive()    ? ImGuiCol_ButtonActive
                              : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered
                                                       : ImGuiCol_Button;
        draw->AddRectFilled(r.turn_min, r.turn_max, ImGui::GetColorU32(fill),
                            ImGui::GetStyle().FrameRounding);
        const float pad = ImGui::GetStyle().FramePadding.x;
        const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
        draw_turn_arrows(draw, ImVec2(r.turn_min.x + pad + arrows_w * 0.5f, (r.turn_min.y + r.turn_max.y) * 0.5f),
                         arrows_w, ink);
        draw->AddText(ImVec2(r.turn_min.x + pad + arrows_w + gap() * 0.5f,
                             std::round(r.turn_min.y + (frame_h - ImGui::GetFontSize()) * 0.5f)),
                      ink, turn_key.data(), turn_key.data() + turn_key.size());
    }
    describe(gui::explain::framing_turn_aspect);
    if (turned && cb.turn_aspect) {
        cb.turn_aspect();
    }

    const auto reset_button = [&](float left, float top) {
        ImGui::SetCursorScreenPos(ImVec2(left, top));
        if (ImGui::Button(std::string(reset_label).c_str(), ImVec2(reset_w, frame_h)) && cb.reset) {
            cb.reset();
        }
        note(&r.reset_min, &r.reset_max);
        describe(gui::explain::framing_reset);
    };
    if (reset_on_first) {
        reset_button(right_edge - done_w - gap() - reset_w, origin.y);
    }
    ImGui::SetCursorScreenPos(ImVec2(right_edge - done_w, origin.y));
    if (ImGui::Button(std::string(done_text).c_str(), ImVec2(done_w, frame_h)) && cb.done) {
        cb.done();
    }
    note(&r.done_min, &r.done_max);
    describe(gui::explain::framing_done);

    // --- Line 2: Straighten (and Reset, on a narrow canvas). -----------------------------------
    const float line2_right = reset_on_first ? right_edge : right_edge - reset_w - gap();
    if (!reset_on_first) {
        reset_button(line2_right + gap(), line2_y);
    }
    const float slider_least = px(60.0f);
    const float room2 = line2_right - origin.x;
    const bool show_straighten_label =
        straighten_label_w + gap() + slider_least + gap() + widest_words() <= room2;
    x = origin.x;
    if (show_straighten_label) {
        ImGui::PushFont(fonts().semibold, token_type().body);
        draw->AddText(ImVec2(x, std::round(line2_y + (frame_h - ImGui::GetFontSize()) * 0.5f)),
                      ImGui::GetColorU32(ImGuiCol_Text), straighten_label.data(),
                      straighten_label.data() + straighten_label.size());
        ImGui::PopFont();
        x += straighten_label_w + gap();
    }
    // The words keep the room their widest needs, so the slider never moves as they change; only on
    // the narrowest canvas are they shortened, their whole text the tooltip.
    const float words_w = std::max(0.0f, std::min(widest_words(), line2_right - x - slider_least - gap()));
    const float slider_w = std::max(slider_least, line2_right - words_w - gap() - x);
    ImGui::SetCursorScreenPos(ImVec2(x, std::round(line2_y + (frame_h - slider_height()) * 0.5f)));
    ImGui::SetNextItemWidth(slider_w);
    float angle = data.angle;
    SliderTyping typing;
    const std::string field = gui::typed_angle_text(data.angle);
    const bool changed =
        styled_slider_typed("##straighten", &angle, -45.0f, 45.0f, 0.0f, field, &typing, 0, 0.1f);
    note(&r.slider_min, &r.slider_max);
    if (typing.text) {
        if (const std::optional<float> typed = gui::typed_angle(*typing.text); typed && cb.set_angle) {
            cb.set_angle(*typed);
        }
    } else if (!typing.open && !typing.just_closed) {
        if (ImGui::IsItemActivated() && cb.begin_turn) {
            cb.begin_turn();
        }
        if (changed && cb.turn_to) {
            cb.turn_to(angle);
        }
        if (ImGui::IsItemDeactivated() && cb.end_turn) {
            cb.end_turn();
        }
    }
    if (!typing.open) {
        describe(gui::explain::framing_straighten);
    }
    const std::string words = gui::framing_turn_words(data.angle);
    const auto measure = [](std::string_view t) { return text_width(t); };
    const std::string words_shown = gui::ellipsize_middle(words, words_w, measure);
    const float words_x = x + slider_w + gap();
    draw->AddText(ImVec2(words_x, std::round(line2_y + (frame_h - ImGui::GetFontSize()) * 0.5f)),
                  ImGui::GetColorU32(ImGuiCol_Text), words_shown.c_str());
    r.words_min = ImVec2(words_x, line2_y);
    r.words_max = ImVec2(words_x + words_w, line2_y + frame_h);
    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(r.words_min, r.words_max)) {
        if (cb.hover) {
            cb.hover(gui::explain::framing_angle);
        }
        if (words_shown != words) {
            ImGui::SetTooltip("%s", words.c_str());
        }
    }

    // --- Lines 3 and 4: the live line and the hint, in the small type. -------------------------
    ImGui::PushFont(nullptr, token_type().small);
    const auto text_line = [&](const std::string& text, float y, ImGuiCol colour, ImVec2* min,
                               ImVec2* max, std::string* shown) {
        *shown = gui::ellipsize_middle(text, inner, measure);
        draw->AddText(ImVec2(origin.x, y), ImGui::GetColorU32(colour), shown->c_str());
        *min = ImVec2(origin.x, y);
        *max = ImVec2(origin.x + text_width(*shown), y + small_line());
        if (*shown != text && ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(*min, *max)) {
            ImGui::SetTooltip("%s", text.c_str());
        }
    };
    text_line(data.live_line, line3_y, ImGuiCol_Text, &r.live_min, &r.live_max, &r.live_shown);
    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(r.live_min, r.live_max) && cb.hover) {
        cb.hover(gui::explain::easel);
    }
    text_line(data.hint, line4_y, ImGuiCol_TextDisabled, &r.hint_min, &r.hint_max, &r.hint_shown);
    ImGui::PopFont();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

}  // namespace prism::gui_app

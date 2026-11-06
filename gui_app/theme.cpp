#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <span>

#include "fonts.hpp"
#include "imgui_internal.h"

namespace prism::gui_app {

namespace {

ImVec4 grey(float v, float alpha = 1.0f) {
    return ImVec4(v, v, v, alpha);
}

ImVec4 to_vec4(const gui::Rgba& c) {
    return ImVec4(c.r, c.g, c.b, c.a);
}

ImU32 to_u32(const gui::Rgba& c) {
    return ImGui::ColorConvertFloat4ToU32(to_vec4(c));
}

ImVec2 to_vec2(const gui::Vec2& v) {
    return ImVec2(v.x, v.y);
}

// The tokens' colours on top of ImGui's own style (which supplies the ones the tokens don't name,
// e.g. the tab and plot colours): quiet greys, so the chrome around a photo carries no colour
// cast, and the amber accent only on the Export button and the viewed stage (drawn by the app).
void apply_colours(ImGuiStyle& style, const gui::TokenColours& t) {
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = to_vec4(t.text);
    c[ImGuiCol_TextDisabled] = to_vec4(t.text_muted);
    c[ImGuiCol_WindowBg] = to_vec4(t.panel);
    c[ImGuiCol_ChildBg] = grey(0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = to_vec4(t.popup_bg);
    c[ImGuiCol_MenuBarBg] = to_vec4(t.chrome);
    c[ImGuiCol_Border] = to_vec4(t.hair);
    c[ImGuiCol_BorderShadow] = grey(0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = to_vec4(t.control);
    c[ImGuiCol_FrameBgHovered] = to_vec4(t.control_hovered);
    c[ImGuiCol_FrameBgActive] = to_vec4(t.control_active);
    c[ImGuiCol_TitleBg] = to_vec4(t.title_bg);
    c[ImGuiCol_TitleBgActive] = to_vec4(t.title_bg_active);
    c[ImGuiCol_TitleBgCollapsed] = to_vec4(t.title_bg);
    c[ImGuiCol_ScrollbarBg] = grey(0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = to_vec4(t.scrollbar_grab);
    c[ImGuiCol_ScrollbarGrabHovered] = to_vec4(t.control_border);
    c[ImGuiCol_ScrollbarGrabActive] = to_vec4(t.fill);
    c[ImGuiCol_Button] = to_vec4(t.control);
    c[ImGuiCol_ButtonHovered] = to_vec4(t.control_hovered);
    c[ImGuiCol_ButtonActive] = to_vec4(t.control_active);
    c[ImGuiCol_Header] = to_vec4(t.control);
    c[ImGuiCol_HeaderHovered] = to_vec4(t.control_hovered);
    c[ImGuiCol_HeaderActive] = to_vec4(t.control_active);
    c[ImGuiCol_SliderGrab] = to_vec4(t.fill);
    c[ImGuiCol_SliderGrabActive] = to_vec4(t.text);
    c[ImGuiCol_CheckMark] = to_vec4(t.text);  // ImGui's own (menu ticks); boxes are ours
    c[ImGuiCol_Separator] = to_vec4(t.hair);
    c[ImGuiCol_SeparatorHovered] = to_vec4(t.control_border);
    c[ImGuiCol_SeparatorActive] = to_vec4(t.control_border);
    c[ImGuiCol_ModalWindowDimBg] = to_vec4(t.modal_dim);
}

void apply_metrics(ImGuiStyle& style, const gui::TokenMetrics& m) {
    style.WindowPadding = to_vec2(m.window_padding);
    style.FramePadding = to_vec2(m.frame_padding);
    style.ItemSpacing = to_vec2(m.item_spacing);
    style.ItemInnerSpacing = to_vec2(m.item_inner_spacing);
    style.WindowRounding = m.window_rounding;
    style.PopupRounding = m.popup_rounding;
    style.ChildRounding = m.child_rounding;
    style.FrameRounding = m.frame_rounding;
    style.GrabRounding = m.grab_rounding;
    style.ScrollbarSize = m.scrollbar_size;
    style.ScrollbarRounding = m.scrollbar_rounding;
    style.WindowBorderSize = m.window_border;
    style.FrameBorderSize = m.frame_border;
    style.SeparatorTextBorderSize = m.separator_text_border;
    style.SeparatorTextPadding = to_vec2(m.separator_text_padding);
}

Fonts loaded_fonts;
const gui::TokenSet* current_set = &gui::token_set(gui::Theme::dark);
gui::TokenMetrics current_metrics = gui::scaled(gui::token_set(gui::Theme::dark).metrics, 1.0f);
float section_size = 13.0f;  // the current theme's section header size (before the scale)

}  // namespace

const Palette& palette(gui::Theme theme) {
    static const Palette dark{
        to_u32(gui::token_set(gui::Theme::dark).colours.canvas),  // canvas: neutral, near 18% grey
        IM_COL32(235, 235, 235, 255),  // canvas text
        IM_COL32(150, 150, 150, 255),  // drop outline
        IM_COL32(120, 180, 255, 255),  // drop highlight
        ImVec4(0.45f, 0.12f, 0.12f, 0.96f),
        ImVec4(1.0f, 0.93f, 0.93f, 1.0f),
        ImVec4(0.75f, 0.30f, 0.30f, 1.0f),
        ImVec4(0.18f, 0.18f, 0.18f, 0.96f),
        grey(0.92f),
        grey(0.40f),
        IM_COL32(0, 0, 0, 180),
        IM_COL32(255, 255, 255, 255),
        IM_COL32(245, 90, 90, 235),    // red line
        IM_COL32(85, 215, 105, 235),   // green line
        IM_COL32(95, 155, 255, 240),   // blue line
        IM_COL32(55, 130, 230, 255),   // shadows clipped fill
        IM_COL32(225, 85, 45, 255),    // highlights clipped fill
        IM_COL32(205, 95, 240, 255),   // gamut (outside sRGB) fill
        IM_COL32(225, 225, 225, 255),  // scope trace
        IM_COL32(255, 255, 255, 52),   // scope guide lines
        IM_COL32(245, 195, 120, 240),  // skin-tone line
        IM_COL32(70, 215, 225, 240),   // cyan target
        IM_COL32(225, 95, 205, 240),   // magenta target
        IM_COL32(235, 215, 70, 240),   // yellow target
        IM_COL32(255, 70, 30, 215),    // clipping overlay: above white
        IM_COL32(30, 140, 255, 215),   // clipping overlay: below black
        IM_COL32(235, 60, 245, 235),   // clipping overlay: outside sRGB (stripes)
        to_vec4(gui::token_set(gui::Theme::dark).colours.chrome),
    };
    static const Palette light{
        to_u32(gui::token_set(gui::Theme::light).colours.canvas),  // canvas: neutral mid-grey
        IM_COL32(245, 245, 245, 255),  // canvas text
        IM_COL32(190, 190, 190, 255),  // drop outline (on the grey canvas)
        IM_COL32(150, 200, 255, 255),  // drop highlight
        ImVec4(0.99f, 0.90f, 0.90f, 0.97f),
        ImVec4(0.45f, 0.04f, 0.04f, 1.0f),
        ImVec4(0.80f, 0.35f, 0.35f, 1.0f),
        ImVec4(0.97f, 0.97f, 0.97f, 0.97f),
        grey(0.10f),
        grey(0.70f),
        IM_COL32(0, 0, 0, 180),
        IM_COL32(255, 255, 255, 255),
        IM_COL32(245, 90, 90, 235),    // red line
        IM_COL32(85, 215, 105, 235),   // green line
        IM_COL32(95, 155, 255, 240),   // blue line
        IM_COL32(30, 110, 215, 255),   // shadows clipped fill
        IM_COL32(215, 65, 30, 255),    // highlights clipped fill
        IM_COL32(155, 45, 195, 255),   // gamut (outside sRGB) fill
        IM_COL32(225, 225, 225, 255),  // scope trace
        IM_COL32(255, 255, 255, 52),   // scope guide lines
        IM_COL32(245, 195, 120, 240),  // skin-tone line
        IM_COL32(70, 215, 225, 240),   // cyan target
        IM_COL32(225, 95, 205, 240),   // magenta target
        IM_COL32(235, 215, 70, 240),   // yellow target
        IM_COL32(255, 60, 20, 225),    // clipping overlay: above white
        IM_COL32(10, 110, 245, 225),   // clipping overlay: below black
        IM_COL32(190, 20, 200, 240),   // clipping overlay: outside sRGB (stripes)
        to_vec4(gui::token_set(gui::Theme::light).colours.chrome),
    };
    return theme == gui::Theme::light ? light : dark;
}

const Fonts& fonts() {
    return loaded_fonts;
}

void load_fonts(ImGuiIO& io) {
    // The data lives in the executable for the whole run, which is what ImGui needs of a font it
    // doesn't own.
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    const std::span<const unsigned char> regular = inter_regular();
    const std::span<const unsigned char> semibold = inter_semibold();
    const float body = gui::token_set(gui::Theme::dark).type.body;
    loaded_fonts.regular = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(regular.data()), static_cast<int>(regular.size()), body,
        &config);
    loaded_fonts.semibold = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(semibold.data()), static_cast<int>(semibold.size()), body,
        &config);
}

void apply_style(gui::Theme theme, float scale) {
    const gui::TokenSet& tokens = gui::token_set(theme);
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.ScaleAllSizes(scale);  // what the tokens don't name keeps ImGui's defaults, scaled
    apply_colours(style, tokens.colours);
    apply_metrics(style, gui::scaled(tokens.metrics, scale));
    style.FontSizeBase = tokens.type.body;  // before the scale; FontScaleDpi applies it
    style.FontScaleDpi = scale;
    section_size = tokens.type.section;
    current_set = &tokens;
    current_metrics = gui::scaled(tokens.metrics, scale);
}

const gui::TokenColours& token_colours() {
    return current_set->colours;
}

const gui::TokenMetrics& token_metrics() {
    return current_metrics;
}

const gui::TokenType& token_type() {
    return current_set->type;
}

float hair() {
    return std::max(1.0f, std::round(current_metrics.hair_width));
}

void space_before_section() {
    // Below whatever came before, but not at the top of a window or panel.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float pad = style.WindowPadding.y;
    if (ImGui::GetCursorPosY() > pad + 1.0f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - style.ItemSpacing.y + current_metrics.section_gap);
    }
}

bool section_text(const char* text, const char* action, bool* title_hovered,
                  ImGuiButtonFlags action_flags) {
    // The title in the semi-bold face, small and quiet, with no rule under it: the headers name
    // groups, the controls beneath them are what the eye should land on. An optional action sits
    // at the right as plain muted text.
    space_before_section();
    const ImGuiStyle& style = ImGui::GetStyle();
    bool clicked = false;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushFont(loaded_fonts.semibold, section_size);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    if (title_hovered != nullptr) {
        *title_hovered = ImGui::IsItemHovered();
    }
    ImGui::PopFont();
    if (action != nullptr) {
        ImGui::PushFont(loaded_fonts.regular, section_size);
        const float width = ImGui::CalcTextSize(action, nullptr, true).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
        // Plain text that lights up under the cursor: no background, and the same size always.
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::IsMouseHoveringRect(
                                                 ImGui::GetCursorScreenPos(),
                                                 ImVec2(ImGui::GetCursorScreenPos().x + width,
                                                        ImGui::GetCursorScreenPos().y +
                                                            ImGui::GetFrameHeight()))
                                                 ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                 : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
        clicked = ImGui::ButtonEx(action, ImVec2(0.0f, 0.0f), action_flags);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        ImGui::PopFont();
    }
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - style.ItemSpacing.y + current_metrics.section_head_gap);
    return clicked;
}

}  // namespace prism::gui_app

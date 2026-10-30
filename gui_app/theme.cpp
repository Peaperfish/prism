#include "theme.hpp"

namespace prism::gui_app {

namespace {

ImVec4 grey(float v, float alpha = 1.0f) {
    return ImVec4(v, v, v, alpha);
}

// ImGui's dark style, with neutral greys instead of its blue-tinted frames and title bars: the
// chrome around a photo shouldn't carry a colour cast. The accent stays blue, used only for what
// is hovered, active or chosen.
void dark_colours(ImGuiStyle& style) {
    ImGui::StyleColorsDark(&style);
    ImVec4* c = style.Colors;
    const ImVec4 accent(0.28f, 0.52f, 0.86f, 1.0f);
    c[ImGuiCol_Text] = grey(0.92f);
    c[ImGuiCol_TextDisabled] = grey(0.60f);  // about 5:1 against the background
    c[ImGuiCol_WindowBg] = grey(0.145f);
    c[ImGuiCol_ChildBg] = grey(0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = grey(0.17f, 0.98f);
    c[ImGuiCol_MenuBarBg] = grey(0.11f);
    c[ImGuiCol_Border] = grey(0.30f);
    c[ImGuiCol_FrameBg] = grey(0.22f);
    c[ImGuiCol_FrameBgHovered] = grey(0.28f);
    c[ImGuiCol_FrameBgActive] = grey(0.32f);
    c[ImGuiCol_TitleBg] = grey(0.11f);
    c[ImGuiCol_TitleBgActive] = grey(0.18f);
    c[ImGuiCol_Button] = grey(0.27f);
    c[ImGuiCol_ButtonHovered] = grey(0.34f);
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = grey(0.27f);
    c[ImGuiCol_HeaderHovered] = grey(0.34f);
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.42f, 0.64f, 0.95f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.64f, 0.95f, 1.0f);
    c[ImGuiCol_Separator] = grey(0.30f);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ModalWindowDimBg] = grey(0.0f, 0.45f);
}

// ImGui's light style, likewise neutral, with white fields and visible borders so controls stand
// out from the light grey background.
void light_colours(ImGuiStyle& style) {
    ImGui::StyleColorsLight(&style);
    ImVec4* c = style.Colors;
    const ImVec4 accent(0.16f, 0.42f, 0.78f, 1.0f);
    c[ImGuiCol_Text] = grey(0.08f);
    c[ImGuiCol_TextDisabled] = grey(0.40f);  // about 6:1 against the background
    c[ImGuiCol_WindowBg] = grey(0.93f);
    c[ImGuiCol_ChildBg] = grey(0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = grey(0.98f, 0.98f);
    c[ImGuiCol_MenuBarBg] = grey(0.88f);
    c[ImGuiCol_Border] = grey(0.70f);
    c[ImGuiCol_FrameBg] = grey(1.0f);
    c[ImGuiCol_FrameBgHovered] = grey(0.96f);
    c[ImGuiCol_FrameBgActive] = grey(0.90f);
    c[ImGuiCol_TitleBg] = grey(0.86f);
    c[ImGuiCol_TitleBgActive] = grey(0.80f);
    c[ImGuiCol_Button] = grey(0.84f);
    c[ImGuiCol_ButtonHovered] = grey(0.78f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.62f, 0.76f, 0.95f, 1.0f);
    c[ImGuiCol_Header] = grey(0.84f);
    c[ImGuiCol_HeaderHovered] = grey(0.78f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.62f, 0.76f, 0.95f, 1.0f);
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.10f, 0.32f, 0.64f, 1.0f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_Separator] = grey(0.72f);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ModalWindowDimBg] = grey(0.20f, 0.35f);
}

}  // namespace

const Palette& palette(gui::Theme theme) {
    static const Palette dark{
        IM_COL32(60, 60, 60, 255),     // canvas: neutral 24% grey
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
    };
    static const Palette light{
        IM_COL32(96, 96, 96, 255),    // canvas: a lighter neutral grey, still well below mid-grey
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
    };
    return theme == gui::Theme::light ? light : dark;
}

void apply_style(gui::Theme theme, float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    if (theme == gui::Theme::light) {
        light_colours(style);
    } else {
        dark_colours(style);
    }
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.FrameBorderSize = theme == gui::Theme::light ? 1.0f : 0.0f;
    style.FontSizeBase = 15.0f;  // ImGui's vector font reads best from 15 px
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}

}  // namespace prism::gui_app

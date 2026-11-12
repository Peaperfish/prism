#include "export_sheet.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "imgui_internal.h"
#include "prism/gui/explanations.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace prism::gui_app {

namespace {

using Mode = gui::ExportSize::Mode;

// A press on these acts at once and doesn't hold the active id (see active_control.hpp).
constexpr ImGuiButtonFlags press_only =
    ImGuiButtonFlags_PressedOnClick | static_cast<ImGuiButtonFlags>(ImGuiButtonFlags_NoHoldingActiveId);

float px(float mock_px) {
    return std::round(mock(mock_px));
}

ImU32 u32(const gui::Rgba& c) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
}

// A font's line height, whole pixels, for a face and size.
float line_height(ImFont* font, float size) {
    ImGui::PushFont(font, size);
    const float h = std::ceil(ImGui::GetFontSize());
    ImGui::PopFont();
    return h;
}

// Offsets from the sheet's content origin. Only mock sizes and font sizes go in.
struct Layout {
    float width = 0, height = 0;
    float title_h = 0, label_h = 0, tabs_h = 0, row_h = 0, result_h = 0, button_h = 0;
    float title_y = 0, label_y = 0, tabs_y = 0, row_y = 0, result_y = 0, buttons_y = 0;
    float field_w = 0, button_w = 0, button_gap = 0, unit_gap = 0;
};

Layout layout() {
    Layout l;
    l.width = px(340.0f);
    l.title_h = line_height(fonts().semibold, token_type().section);
    l.label_h = line_height(nullptr, token_type().body);
    l.tabs_h = segmented_height();
    l.row_h = std::max(px(30.0f), std::ceil(ImGui::GetFrameHeight()));
    // Two lines of the small type: the longest text ("Not smaller than the photo: exports at
    // full size, ...") wraps to two lines at this width, and the room is there for one as well.
    l.result_h = 2 * line_height(nullptr, token_type().small);
    l.button_h = std::max(px(30.0f), std::ceil(ImGui::GetFrameHeight()));
    l.field_w = px(96.0f);
    l.button_w = px(92.0f);
    l.button_gap = px(8.0f);
    l.unit_gap = px(8.0f);

    float y = 0;
    l.title_y = y;
    y += l.title_h + px(16.0f);
    l.label_y = y;
    y += l.label_h + px(6.0f);
    l.tabs_y = y;
    y += l.tabs_h + px(14.0f);
    l.row_y = y;
    y += l.row_h + px(8.0f);
    l.result_y = y;
    y += l.result_h + px(18.0f);
    l.buttons_y = y;
    y += l.button_h;
    l.height = y;
    return l;
}

int digit_filter(ImGuiInputTextCallbackData* data) {
    return data->EventChar >= '0' && data->EventChar <= '9' ? 0 : 1;
}

int& number_of(gui::ExportSize& size) {
    return size.mode == Mode::percent ? size.percent : size.long_side;
}

void write_text(std::array<char, 9>& text, int value) {
    std::snprintf(text.data(), text.size(), "%d", value);
}

ImVec2 add(ImVec2 a, float x, float y) {
    return ImVec2(a.x + x, a.y + y);
}

}  // namespace

float export_sheet_padding() {
    return px(18.0f);
}

ImVec2 export_sheet_size() {
    const Layout l = layout();
    return ImVec2(l.width + 2 * export_sheet_padding(), l.height + 2 * export_sheet_padding());
}

int export_field_value(Mode mode, std::string_view text, int current) {
    if (text.empty() || text.size() > 8) {
        return current;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return current;
        }
        value = value * 10 + (c - '0');
    }
    return mode == Mode::percent ? std::clamp(value, 1, 99) : std::max(value, 1);
}

ExportSheetResult draw_export_sheet(ExportSheetState& state, const ExportSheetContext& context,
                                    const ExportSheetCallbacks& cb, ExportSheetRects* out) {
    const Layout l = layout();
    const gui::TokenColours& colours = token_colours();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ExportSheetRects rects;
    const auto explain = [&](std::string_view id) {
        if (cb.explain) {
            cb.explain(id);
        }
    };
    // Text at a place, clipped to its box, so what it says never moves or resizes anything.
    const auto text_in = [&](ImVec2 min, ImVec2 max, const std::string& text, ImFont* font,
                             float size, ImU32 colour, bool wrap, bool centre_y) {
        ImGui::PushFont(font, size);
        const float wrap_width = wrap ? max.x - min.x : 0.0f;
        const ImVec2 extent = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap_width);
        const float y = centre_y ? min.y + std::floor((max.y - min.y - extent.y) * 0.5f) : min.y;
        draw->PushClipRect(min, max, true);
        draw->AddText(ImGui::GetFont(), size, ImVec2(min.x, y), colour, text.c_str(), nullptr,
                      wrap_width);
        draw->PopClipRect();
        ImGui::PopFont();
    };

    // Title.
    rects.title_min = add(origin, 0, l.title_y);
    rects.title_max = add(origin, l.width, l.title_y + l.title_h);
    text_in(rects.title_min, rects.title_max, "Export", fonts().semibold, token_type().section,
            u32(colours.text), false, false);

    // "Size" and the segmented control.
    rects.label_min = add(origin, 0, l.label_y);
    rects.label_max = add(origin, l.width, l.label_y + l.label_h);
    text_in(rects.label_min, rects.label_max, "Size", nullptr, token_type().body,
            u32(colours.text_muted), false, false);

    rects.tabs_min = add(origin, 0, l.tabs_y);
    rects.tabs_max = add(origin, l.width, l.tabs_y + l.tabs_h);
    ImGui::SetCursorScreenPos(rects.tabs_min);
    static constexpr std::array<const char*, 3> labels{"Original", "Long side", "Percent"};
    static constexpr std::array<Mode, 3> modes{Mode::original, Mode::long_side, Mode::percent};
    int current = 0;
    for (std::size_t i = 0; i < modes.size(); ++i) {
        if (modes[i] == state.size.mode) {
            current = static_cast<int>(i);
        }
    }
    if (segmented("##export_size", labels, &current,
                  [&](int) { explain(gui::explain::export_size); }, l.width)) {
        state.size.mode = modes[static_cast<std::size_t>(current)];
    }

    // The value row: the same box in every mode.
    rects.row_min = add(origin, 0, l.row_y);
    rects.row_max = add(origin, l.width, l.row_y + l.row_h);
    const bool has_field = state.size.mode != Mode::original;
    if (has_field) {
        const char* const id = state.size.mode == Mode::percent ? "##export_percent" : "##export_long_side";
        const ImVec2 field_size(l.field_w, std::ceil(ImGui::GetFrameHeight()));
        rects.field_min = add(rects.row_min, 0, std::floor((l.row_h - field_size.y) * 0.5f));
        rects.field_max = add(rects.field_min, field_size.x, field_size.y);
        ImGui::SetCursorScreenPos(rects.field_min);
        ImGui::SetNextItemWidth(field_size.x);
        if (state.focus_field) {
            ImGui::SetKeyboardFocusHere();
            state.focus_field = false;
        }
        const bool typing = ImGui::GetActiveID() == ImGui::GetID(id);
        if (!typing) {
            write_text(state.text, number_of(state.size));
        }
        const bool edited = ImGui::InputText(
            id, state.text.data(), state.text.size(),
            ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_AutoSelectAll, digit_filter);
        explain(gui::explain::export_value);
        if (edited) {
            // At once, so the result line follows the typing; the field itself keeps what was typed
            // until it is left, so a "0" can be typed on the way to "05".
            int& number = number_of(state.size);
            number = export_field_value(state.size.mode, state.text.data(), number);
        }
        const std::string unit = state.size.mode == Mode::percent ? "%" : "px";
        const ImVec2 unit_min(rects.field_max.x + l.unit_gap, rects.row_min.y);
        text_in(unit_min, ImVec2(rects.row_max.x, rects.row_max.y), unit, nullptr, token_type().body,
                u32(colours.text_muted), false, true);
    } else {
        rects.field_min = rects.field_max = rects.row_min;
        rects.row_text =
            gui::export_original_text(context.file, context.file_size_exact, context.framed);
        text_in(rects.row_min, rects.row_max, rects.row_text, nullptr, token_type().body,
                u32(colours.text_muted), false, true);
    }

    // The result line: what the export will write.
    rects.result_min = add(origin, 0, l.result_y);
    rects.result_max = add(origin, l.width, l.result_y + l.result_h);
    const gui::ExportPlan plan = gui::plan_export(state.size, context.file, context.start);
    rects.result_text =
        gui::export_result_text(state.size, plan, context.file_size_exact, context.framed);
    text_in(rects.result_min, rects.result_max, rects.result_text, nullptr, token_type().small,
            u32(colours.text), true, false);

    // Buttons, right-aligned: Cancel, then Save As….
    const float buttons_right = l.width;
    rects.save_max = add(origin, buttons_right, l.buttons_y + l.button_h);
    rects.save_min = ImVec2(rects.save_max.x - l.button_w, origin.y + l.buttons_y);
    rects.cancel_max = ImVec2(rects.save_min.x - l.button_gap, rects.save_max.y);
    rects.cancel_min = ImVec2(rects.cancel_max.x - l.button_w, rects.save_min.y);
    ImGui::SetCursorScreenPos(rects.cancel_min);
    const bool cancel_clicked =
        ImGui::ButtonEx("Cancel", ImVec2(l.button_w, l.button_h), press_only);
    ImGui::SetCursorScreenPos(rects.save_min);
    const bool save_clicked =
        ImGui::ButtonEx("Save As\xE2\x80\xA6", ImVec2(l.button_w, l.button_h), press_only);

    rects.height = l.height;
    ImGui::SetCursorScreenPos(add(origin, 0, l.height));
    if (out != nullptr) {
        *out = rects;
    }

    if (cancel_clicked) {
        return ExportSheetResult::cancel;
    }
    if (save_clicked) {
        return ExportSheetResult::save_as;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        return ExportSheetResult::cancel;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        return ExportSheetResult::save_as;
    }
    return ExportSheetResult::none;
}

}  // namespace prism::gui_app

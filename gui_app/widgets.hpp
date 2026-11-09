#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "imgui.h"

namespace prism::gui_app {

// Prism's own looks for ImGui's controls. Each one still calls the ImGui widget underneath (or
// ImGui's own item and button behaviour), so dragging, the keyboard, focus, disabled state, typing
// into a slider and right-click popups work exactly as ImGui's do; only the drawing is replaced.
// Sizes and colours come from the theme's tokens (theme.hpp).

// A thin track with the fill colour from `neutral` to the value, a centre tick when `neutral` is
// inside the range, and a white round thumb. `neutral` is
// the value that does nothing (0 for Exposure's position, the left end for a curve's number), so
// the fill does not always start at the left end. Like ImGui::SliderFloat with an empty format; the item
// is the last item afterwards (IsItemActivated, BeginPopupContextItem...). Set the width first
// with SetNextItemWidth.
bool styled_slider(const char* id, float* value, float min, float max, float neutral,
                   ImGuiSliderFlags flags = 0);
bool styled_slider_int(const char* id, int* value, int min, int max, int neutral,
                       ImGuiSliderFlags flags = 0);

// What typing into a slider did in one frame (see styled_slider_typed).
struct SliderTyping {
    bool open = false;                // the field is the last item (showing, or closing this frame)
    std::optional<std::string> text;  // what was typed, once confirmed: Enter, or clicking away
                                      // after an edit. Esc, or clicking away unedited, gives none.
    // The slider is back after the field closed this frame or last: ImGui still counts the field's
    // end as the item's deactivation then, which is not a drag ending. Ignore its gesture events.
    bool just_closed = false;
};

// styled_slider whose Ctrl-click (Cmd-click on a Mac) opens a text field in the slider's place,
// holding `field_text` selected, instead of ImGui's own typing (which would type the slider's
// position). The caller reads the text, in whatever unit its row shows (gui_core's typed_value.hpp),
// and applies it as one change. The field has the slider's height, so nothing around it moves. While it is
// open the slider is not drawn and nothing is returned changed; the last item is the field's, so a
// caller should not attach the slider's popup or gesture to it (check `typing->open`). Disabled, it
// never opens. Returns whether dragging changed *value.
bool styled_slider_typed(const char* id, float* value, float min, float max, float neutral,
                         std::string_view field_text, SliderTyping* typing,
                         ImGuiSliderFlags flags = 0);

// A slider's height with the current font.
[[nodiscard]] float slider_height();

// A small rounded box, vertically centred on its label (or alone, for an empty or "##" label):
// the frame colour with an outline when off, the accent with a white tick when on. Like
// ImGui::Checkbox otherwise, down to the keyboard and the hit area, which covers the label too.
bool styled_checkbox(const char* label, bool* value);

// An on/off switch: a mock(26) x mock(15) pill, the fill colour with a panel-coloured knob at the
// right when on, the track colour with the knob at the left when off. A real ImGui item (hover,
// nav focus ring, Space/Enter when focused, dimmed when disabled); returns true when it was
// toggled. It never animates and never changes size. `id` is hidden text, like "##on".
bool styled_switch(const char* id, bool* value);

// The switch's size, for laying a row out around it.
[[nodiscard]] ImVec2 switch_size();

// A segmented control: one row of equal-width tabs in a rounded well (padding and gaps mock(2),
// tabs mock(26) tall), the current one raised on the control_on colour in the semi-bold face, the
// others flat, muted and regular, with a hover wash. Each tab is a real ImGui item (arrow keys and
// Tab move focus between them, Space and Enter select). The text size is the same in both states
// and the tab widths never depend on the state, so nothing reflows. Returns true when another tab
// was chosen (*current is set). `after_item(i)` is called right after tab i is submitted, so the
// caller can hook the last item (explain, tooltips). The row takes `width`, or the available width
// if that is 0.
bool segmented(const char* id, std::span<const char* const> labels, int* current,
               const std::function<void(int)>& after_item = {}, float width = 0.0f);

// The height of a segmented control: its tabs and the well's padding.
[[nodiscard]] float segmented_height();

// The same, round, with a dot: ImGui::RadioButton.
bool styled_radio(const char* label, int* current, int value);

enum class Chevron { down, right, left };

// A thin chevron centred at `centre`, sized from the theme's chevron tokens: what stands in for
// ImGui's heavy filled triangles.
void draw_chevron(ImDrawList* draw, ImVec2 centre, Chevron direction, ImU32 colour);

// ImGui::BeginCombo with a light chevron at its right end instead of the arrow button. Call
// EndCombo when it returns true, as usual. Set the width first with SetNextItemWidth.
bool styled_begin_combo(const char* id, const char* preview);

}  // namespace prism::gui_app

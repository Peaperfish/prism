#pragma once

#include "imgui.h"

namespace prism::gui_app {

// Prism's own looks for ImGui's controls. Each one still calls the ImGui widget underneath (or
// ImGui's own item and button behaviour), so dragging, the keyboard, focus, disabled state, typing
// into a slider and right-click popups work exactly as ImGui's do; only the drawing is replaced.
// Sizes and colours come from the theme's tokens (theme.hpp).

// A thin track with the accent filling from `neutral` to the value, and a round knob. `neutral` is
// the value that does nothing (0 for brightness, the default for a curve parameter), so the fill
// does not always start at the left end. Like ImGui::SliderFloat with an empty format; the item
// is the last item afterwards (IsItemActivated, BeginPopupContextItem...). Set the width first
// with SetNextItemWidth.
bool styled_slider(const char* id, float* value, float min, float max, float neutral,
                   ImGuiSliderFlags flags = 0);
bool styled_slider_int(const char* id, int* value, int min, int max, int neutral,
                       ImGuiSliderFlags flags = 0);

// A small rounded box, vertically centred on its label (or alone, for an empty or "##" label):
// the frame colour with an outline when off, the accent with a white tick when on. Like
// ImGui::Checkbox otherwise, down to the keyboard and the hit area, which covers the label too.
bool styled_checkbox(const char* label, bool* value);

// The same, round, with a dot: ImGui::RadioButton.
bool styled_radio(const char* label, int* current, int value);

enum class Chevron { down, right, left };

// A thin chevron centred at `centre`, sized from the theme's chevron tokens: what stands in for
// ImGui's heavy filled triangles.
void draw_chevron(ImDrawList* draw, ImVec2 centre, Chevron direction, ImU32 colour);

// ImGui::BeginCombo with a light chevron at its right end instead of the arrow button. Call
// EndCombo when it returns true, as usual. Set the width first with SetNextItemWidth.
bool styled_begin_combo(const char* id, const char* preview);

// A collapsible section's header, in the look of section_text (semi-bold, muted, a hairline) with
// a chevron that points right when closed and down when open. The whole line is the click area.
// Returns true when it was clicked; *open is toggled.
bool section_header(const char* title, bool* open);

}  // namespace prism::gui_app

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

}  // namespace prism::gui_app

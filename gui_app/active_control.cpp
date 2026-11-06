#include "active_control.hpp"

#include "imgui.h"
#include "imgui_internal.h"

namespace prism::gui_app {

bool control_is_active(bool canvas_press) {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    if (g.ActiveId == 0 || canvas_press) {
        return false;
    }
    // A press on empty window space (or on a disabled item): see the header.
    return g.ActiveIdWindow == nullptr || g.ActiveId != g.ActiveIdWindow->MoveId;
}

}  // namespace prism::gui_app

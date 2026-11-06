#pragma once

namespace prism::gui_app {

// Whether a real control is being held (a slider dragged, a text field edited, a button pressed):
// something that Undo, J, T, I and the clip report must not interrupt. `canvas_press` is the
// app's own flag for a press that began on the photo.
//
// ImGui calls a press on empty window space an active item too: StartMouseMovingWindow() makes the
// window's MoveId the active id (with that window as ActiveIdWindow), even when the window cannot
// be moved. So a click on the panel's padding leaves IsAnyItemActive() true for as long as the
// button is down, which would block the shortcuts. The id is recognised by comparing it with the
// MoveId of the active id's own window: a real item never has that id, and the pair is set
// together, so it holds for child windows too (the panel is a child), where checking the root
// window's MoveId would miss.
[[nodiscard]] bool control_is_active(bool canvas_press);

}  // namespace prism::gui_app

#pragma once

#include <SDL3/SDL.h>

namespace prism::gui_app {

// The window's contents: menu, canvas, control panel and status bar. Lives on the main thread
// (SDL's renderer may only be used there) and is drawn once per frame with Dear ImGui.
class App {
public:
    // Every SDL event, after ImGui has seen it.
    void handle_event(const SDL_Event& event);

    // Draws one frame. Called between ImGui::NewFrame() and ImGui::Render().
    void frame();

    // True while the window must keep redrawing although no event arrives (e.g. a render is in
    // flight); otherwise the main loop sleeps until the next event.
    [[nodiscard]] bool animating() const;

private:
    void draw_menu();
    void draw_canvas();
    void draw_panel();
    void draw_status_bar();

    int quiet_frames_ = 0;  // frames drawn since the last event
};

}  // namespace prism::gui_app

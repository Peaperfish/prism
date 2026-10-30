#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "file_dialogs.hpp"
#include "preview_texture.hpp"
#include "prism/gui/background_export.hpp"
#include "prism/gui/live_preview.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"

namespace prism::gui_app {

// The window's contents: menu, canvas, control panel and status bar. Lives on the main thread
// (SDL's renderer may only be used there) and is drawn once per frame with Dear ImGui. It holds
// no photo logic of its own: the Session opens, edits and exports, LivePreview renders.
class App {
public:
    // `dialogs` receives the file dialogs' answers; it must outlive any dialog still open, so it
    // is owned by main() rather than by the App.
    App(SDL_Window* window, SDL_Renderer* renderer, DialogQueue& dialogs);

    // Opens `path_utf8` (e.g. from the command line) on the next frames.
    void open_utf8(const char* path_utf8);

    // Every SDL event, after ImGui has seen it.
    void handle_event(const SDL_Event& event);

    // Draws one frame. Called between ImGui::NewFrame() and ImGui::Render().
    void frame();

    // True while the window must keep redrawing although no event arrives (e.g. a render is in
    // flight); otherwise the main loop sleeps until the next event.
    [[nodiscard]] bool animating() const;

private:
    // Asks for `path` to be opened. The open itself happens a frame later, so the window can
    // first show "Opening ..." (decoding a large RAW file takes about a second).
    void request_open(std::filesystem::path path);
    void open_now(const std::filesystem::path& path);
    void show_open_dialog();
    void show_save_dialog();
    void take_export_result();
    void take_dialog_results();
    void update_preview();
    void show_error(gui::Error error);
    void report(gui::MaybeError error);
    void undo();
    void redo();

    void handle_shortcuts();
    void draw_menu();
    void draw_canvas();
    void draw_banner();
    void draw_panel();
    void draw_presets();
    void draw_simple_slider(const char* label, gui::SimpleControl control);
    void draw_status_bar();
    void draw_saving_popup();

    SDL_Window* window_;
    DialogQueue& dialogs_;
    gui::Session session_;
    gui::LivePreview preview_;
    PreviewTexture texture_;
    gui::BackgroundExport exporter_;
    double last_render_ms_ = 0;

    std::optional<std::filesystem::path> pending_open_;
    bool pending_open_shown_ = false;  // the "Opening ..." frame has been drawn
    bool dialog_open_ = false;         // a file dialog is up: don't open another

    bool drag_over_ = false;                      // files are being dragged over the window
    std::vector<std::filesystem::path> dropped_;  // the files of the current drop

    std::optional<gui::Error> error_;  // shown as a banner until dismissed
    std::string notice_;               // a short note that isn't an error, e.g. "Saved ..."

    int quiet_frames_ = 0;  // frames drawn since the last event
};

}  // namespace prism::gui_app

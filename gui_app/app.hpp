#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "file_dialogs.hpp"
#include "preview_texture.hpp"
#include "scope_view.hpp"
#include "prism/gui/background_export.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/histogram.hpp"
#include "prism/gui/live_preview.hpp"
#include "prism/gui/preferences.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"

namespace prism::gui_app {

// The window's contents: menu, canvas, control panel and status bar. Lives on the main thread
// (SDL's renderer may only be used there) and is drawn once per frame with Dear ImGui. It holds
// no photo logic of its own: the Session opens, edits and exports, LivePreview renders.
class App {
public:
    // `dialogs` receives the file dialogs' answers; it must outlive any dialog still open, so it
    // is owned by main() rather than by the App. `preferences_file` is where Preferences are
    // read from now and saved to on every change (empty: nowhere, the defaults are used).
    App(SDL_Window* window, SDL_Renderer* renderer, DialogQueue& dialogs,
        std::filesystem::path preferences_file);

    // Opens `path_utf8` (e.g. from the command line) on the next frames.
    void open_utf8(const char* path_utf8);

    // Call before ImGui::NewFrame(): rebuilds the style when the theme or the scale changed.
    void before_frame();

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
    [[nodiscard]] gui::Theme theme() const;
    // The UI's scale: the display's content scale, or the Interface size preference.
    [[nodiscard]] float scale() const;
    static float content_scale(SDL_Window* window);
    void apply_minimum_size();
    void undo();
    void redo();

    void handle_shortcuts();
    void draw_menu();
    void draw_canvas();
    void draw_banner();
    void draw_stage_badge();
    void draw_splitter(float canvas_width, float width, float height, float available);
    void draw_panel();
    void draw_histogram();
    void draw_histogram_graph(const gui::StageHistogram& h, float avail_w);
    void draw_clip_chips(const gui::StageHistogram& h, float avail_w);
    void draw_scope_selector();
    void draw_clip_overlay_button();
    void toggle_clip_overlay();
    void update_clip_overlay_texture();
    bool draw_stage_clip_indicator(const gui::StageClip& clip, ImVec2 row_min, ImVec2 row_max);
    void draw_clipping_summary();
    void draw_presets();
    void draw_simple_slider(const char* label, gui::SimpleControl control);
    void draw_slider(const char* label, gui::SimpleControl control, bool in_stage_row);
    void draw_pipeline();
    void draw_status_bar();
    void draw_saving_popup();
    void draw_preferences();
    void draw_about();
    void draw_notices();
    void save_preferences();
    void set_preferences(gui::Preferences prefs);

    SDL_Window* window_;
    DialogQueue& dialogs_;
    gui::Session session_;
    gui::LivePreview preview_;
    PreviewTexture texture_;
    ScopeView scope_view_;
    std::optional<gui::StageHistogram> histogram_;
    gui::StageId histogram_stage_ = gui::final_stage;  // the stage histogram_ was measured on
    gui::StageId viewed_stage_ = gui::final_stage;
    // Clipping at every stage, of the frame on screen. Only asked for (and so only present) while
    // the pipeline view is open: pipeline_open_ is what the last frame's draw_pipeline saw.
    std::optional<gui::ClipReport> clips_;
    bool pipeline_open_ = true;
    // The clip mask of the frame on screen (empty: none), and what the overlay texture was made
    // from, to rebuild it when the mask or the theme changes.
    std::vector<std::uint8_t> clip_mask_;
    int clip_mask_width_ = 0;
    int clip_mask_height_ = 0;
    bool overlay_dirty_ = false;
    std::optional<gui::Theme> overlay_theme_;
    gui::BackgroundExport exporter_;
    double last_render_ms_ = 0;

    std::optional<std::filesystem::path> pending_open_;
    bool pending_open_shown_ = false;  // the "Opening ..." frame has been drawn
    bool dialog_open_ = false;         // a file dialog is up: don't open another

    bool drag_over_ = false;                      // files are being dragged over the window
    std::vector<std::filesystem::path> dropped_;  // the files of the current drop

    std::optional<gui::Error> error_;  // shown as a banner until dismissed
    std::string notice_;               // a short note that isn't an error, e.g. "Saved ..."

    std::filesystem::path prefs_file_;
    gui::Preferences prefs_;
    bool prefs_open_ = false;
    // SDL event that the macOS app menu's Preferences item sends; 0 if it isn't connected, and
    // then Preferences is in our own Edit menu.
    Uint32 native_prefs_event_ = 0;
    bool about_open_ = false;
    bool notices_open_ = false;  // the full third-party notices, opened from About
    // Likewise for the macOS app menu's "About Prism" item; 0: About is in our Help menu.
    Uint32 native_about_event_ = 0;
    float display_scale_ = 1.0f;  // the display's content scale (1.5 at 150% on Windows)
    bool style_dirty_ = true;     // rebuild the style before the next frame

    int quiet_frames_ = 0;  // frames drawn since the last event
};

}  // namespace prism::gui_app

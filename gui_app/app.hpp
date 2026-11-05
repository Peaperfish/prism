#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "file_dialogs.hpp"
#include "preview_texture.hpp"
#include "scope_view.hpp"
#include "prism/gui/background_export.hpp"
#include "prism/gui/canvas_input.hpp"
#include "prism/gui/clip_note.hpp"
#include "prism/gui/close_guard.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/explanations.hpp"
#include "prism/gui/histogram.hpp"
#include "prism/gui/layout.hpp"
#include "prism/gui/live_preview.hpp"
#include "prism/gui/pixel_sample.hpp"
#include "prism/gui/preferences.hpp"
#include "prism/gui/recipe_ui.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/gui/view_transform.hpp"

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

    // The window was asked to close (Cmd+Q, the close button, File > Quit). It only quits now if
    // nothing unexported is at stake; otherwise it asks first (see close_guard.hpp). The main loop
    // ends when quit_wanted() turns true.
    void request_quit();
    [[nodiscard]] bool quit_wanted() const noexcept { return quit_; }

    // Call before ImGui::NewFrame(): rebuilds the style when the theme or the scale changed.
    void before_frame();

    // Every SDL event, after ImGui has seen it.
    void handle_event(const SDL_Event& event);

    // A wheel event, with SDL's own deltas. It waits for the next frame, which knows whether the
    // cursor is over the canvas.
    void canvas_wheel(gui::WheelDelta wheel, bool trackpad, std::uint64_t now_ms);

    // Draws one frame. Called between ImGui::NewFrame() and ImGui::Render().
    void frame();

    // True while the window must keep redrawing although no event arrives (e.g. a render is in
    // flight); otherwise the main loop sleeps until the next event.
    [[nodiscard]] bool animating() const;

    // What the window is cleared to before ImGui draws (the current theme's window colour).
    [[nodiscard]] ImVec4 clear_colour() const;

private:
    // Asks for `path` to be opened. The open itself happens a frame later, so the window can
    // first show "Opening ..." (decoding a large RAW file takes about a second).
    void request_open(std::filesystem::path path);
    // The part of request_open that really starts it (after the unexported-edits question).
    void start_open(std::filesystem::path path);
    // Unexported edits (#32): a quit or an open is held while the guard decides what happens.
    [[nodiscard]] gui::CloseState close_state() const;
    void begin_close(gui::Pending pending);
    void apply_close_step(gui::CloseStep step);
    void draw_close_prompt();
    void open_now(const std::filesystem::path& path);
    void show_open_dialog();
    void show_save_dialog();
    void take_export_result();
    // Recipes (#18). Saving: the sheet asks for a name and a description, the system's dialog for
    // the file, and the recipe is written at once. recipes_available() says whether a recipe can
    // be saved or loaded now: a photo is open and no dialog, sheet, export or close question is
    // in progress.
    [[nodiscard]] bool recipes_available() const;
    // for_close: the close prompt's Save Recipe... asks, while a quit or open waits on it.
    void begin_save_recipe(bool for_close = false);
    void draw_recipe_sheet();
    void show_recipe_save_dialog();
    // Writes the recipe to the chosen file; says how it ended.
    gui::RecipeEnd take_recipe_save_dialog(const DialogResult& result);
    void remember_recipe_folder(const std::filesystem::path& file);
    // Loading: the dialog, then the file is read and applied as one undo step. A recipe that
    // can't be read changes nothing and opens the "Recipe not loaded" window. remember_folder:
    // it came through the dialog, whose folder is then remembered.
    void show_recipe_load_dialog();
    void load_recipe_file(const std::filesystem::path& path, bool remember_folder);
    void draw_recipe_error();
    void draw_look_recipe();
    // The files of a drop that has just ended.
    void take_drop();
    // The panel's Load recipe... / Save recipe... row, in the Look section.
    void draw_recipe_buttons();
    // "Recipe: <name>" under the Look menu, and what it sets, while the settings are still its.
    void draw_recipe_line();
    void take_dialog_results();
    void take_export_dialog(DialogResult& result);
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
    // Zoom. Keys and menu items act on the canvas as it was last drawn.
    [[nodiscard]] bool view_available() const;
    void view_fit();
    void view_actual_size();
    void view_step(int direction);  // > 0: in
    // Around the picture's AddImage calls: nearest sampling while the zoom shows pixels. Around those
    // only, since the labels drawn after them must stay linear.
    void begin_picture_sampling(ImDrawList* draw) const;
    void end_picture_sampling(ImDrawList* draw) const;
    // Applies what arrived since the last frame, if it belongs to the canvas.
    void apply_canvas_input();
    // Space: whether the hand is held over the canvas. Called before the canvas's items.
    void update_hand_key();
    // A press, drag or double-click on the canvas. Called after its items, to see if they took it.
    void update_hand_drag();
    // Sample mode. Which pixel the cursor reads, then the traces of what is on show; called every
    // frame after the canvas's items.
    void toggle_sample();
    void clear_sample();
    void update_sample_cursor();
    void update_sample_traces();
    void draw_sample_markers(ImDrawList* draw) const;
    void draw_sample_block();
    // The trace the Sample block shows: the pin's, else the last pixel the cursor read.
    [[nodiscard]] const gui::PixelTrace* sample_on_show() const;
    [[nodiscard]] gui::SampleScale sample_scale() const;
    // A control is being used. Dear ImGui also counts a press on a window's empty space as an active
    // item, and a press on the canvas isn't a control: with it the render would stop asking for the
    // clip report, and render once more on release.
    [[nodiscard]] bool control_active() const;
    // The zoom's chip: the number, Fit and 100%. Drawn last on the canvas, clear of canvas_taken_.
    void draw_zoom_chip();
    void draw_banner();
    // The test strip. Whether it is on and usable (strip_ is cleared when the viewed stage can't
    // show it).
    [[nodiscard]] bool strip_active() const { return strip_.has_value(); }
    void toggle_test_strip();
    void set_strip_step(gui::StripStep step);
    // The bar above the canvas: step choice, close, the teaching line.
    void draw_strip_bar(float width);
    // Dividers, chips, the hovered band's highlight and the click, over the picture drawn at
    // `at` with size `shown`.
    void draw_strip_bands(ImDrawList* draw, ImVec2 at, ImVec2 shown);
    // Makes the histogram and scopes those of band `band` of the frame on screen.
    void show_strip_band(int band);
    // The Test strip button of the Brightness slider (Adjust and the Exposure stage row).
    void draw_strip_button();
    void draw_stage_badge();
    // True while the canvas shows two halves: a compare stage is set and its picture has arrived.
    [[nodiscard]] bool splitting() const;
    // Both halves of the picture drawn at `at` with size `shown`, the divider and the two chips.
    void draw_split(ImDrawList* draw, ImVec2 at, ImVec2 shown);
    void draw_splitter(float canvas_width, float width, float height, float available);
    // The control panel with the Explain area under it (when on); `height` is for both.
    void draw_panel_column(float height);
    void draw_panel();
    // The Explain area (#16). explain() is called right after a control is submitted: it notes
    // the control as hovered, focused or changed, for the next frame's area. describe() does that
    // and also shows the control's one-line summary as its tooltip once the mouse rests on it.
    void explain(std::string_view id, bool changed = false);
    void describe(std::string_view id, bool changed = false,
                  ImGuiHoveredFlags tooltip_after = ImGuiHoveredFlags_DelayNormal);
    struct ExplainText {
        std::string summary;
        std::string detail;
        std::string live;
    };
    [[nodiscard]] ExplainText explain_text() const;
    void draw_explain_area(float width, float height);
    void draw_histogram();
    void draw_histogram_graph(const gui::StageHistogram& h, float avail_w);
    void draw_clip_chips(const gui::StageHistogram& h, float avail_w);
    void draw_scope_selector();
    void draw_clip_overlay_button();
    void toggle_clip_overlay();
    void update_clip_overlay_texture();
    // `baseline` is the screen y of the row name's baseline.
    bool draw_stage_clip_indicator(const gui::StageClip& clip, ImVec2 row_min, ImVec2 row_max,
                                   float baseline);
    void draw_clipping_summary();
    void draw_presets();
    void draw_simple_slider(const char* label, gui::SimpleControl control);
    void draw_slider(const char* label, gui::SimpleControl control, bool in_stage_row);
    void draw_matrix_name();
    void draw_curve_controls();
    void draw_compare_controls();
    void draw_pipeline();
    void draw_status_bar();
    // The clipping note at the right end of the status bar (#31); `width` is the room it may use.
    void draw_clip_note(float width);
    [[nodiscard]] float clip_note_width(const gui::ClipNote& note) const;
    void draw_saving_popup();
    void note_if_focused(bool* open_flag);
    void draw_preferences();
    void draw_about();
    void draw_notices();
    void save_preferences();
    void set_preferences(gui::Preferences prefs);
    // View menu and Cmd/Ctrl +, -, 0: one preset bigger or smaller (direction +1 / -1), or back to
    // Auto (0). Saved like the Preferences window's choice.
    void step_interface_size(int direction);

    SDL_Window* window_;
    DialogQueue& dialogs_;
    gui::Session session_;
    gui::LivePreview preview_;
    PreviewTexture texture_;
    ScopeView scope_view_;
    std::optional<gui::StageHistogram> histogram_;
    // The test strip: on (with its step), the step chosen last (it comes back when the strip is
    // turned on again), and, for the frame on screen, each band's histogram and scopes. The
    // histogram_ and scopes shown are those of strip_shown_band_ (the hovered band, else the
    // middle one). View state like the compare stage: not in the settings or undo.
    std::optional<gui::TestStrip> strip_;
    gui::StripStep strip_step_ = gui::strip_default_step;
    std::vector<gui::StripBandFrame> strip_bands_;
    int strip_shown_band_ = -1;
    gui::StageId histogram_stage_ = gui::final_stage;  // the stage histogram_ was measured on
    gui::StageId viewed_stage_ = gui::final_stage;
    // The stage to compare the viewed one with (Original is Decode), or none. View state like
    // viewed_stage_: not in the settings, not in undo, back to none when a file is opened. The
    // left half of the split shows it, the right half the viewed stage.
    std::optional<gui::StageId> compare_stage_;
    float split_fraction_ = gui::split_fraction_default;  // where the divider is, 0 to 1
    // Clipping at every stage, of the frame on screen. Only asked for (and so only present) while
    // the pipeline view is open: pipeline_open_ is what the last frame's draw_pipeline saw.
    std::optional<gui::ClipReport> clips_;
    // The status bar's clipping note, from the newest frame that carried a clip report. While the
    // pipeline view is closed the report is only asked for when nothing is being dragged, so a
    // drag costs what it always did; the note keeps its last text meanwhile, shown muted until a
    // frame that has a report for the current settings arrives (note_fresh_).
    std::optional<gui::ClipNote> clip_note_;
    std::optional<gui::ClipReport> note_report_;
    bool last_frame_had_clips_ = false;
    // Which row's clipping dots the mouse is on, and since when (for their tooltip's delay).
    std::optional<gui::StageId> dot_hover_stage_;
    double dot_hover_since_ = 0;
    bool pipeline_open_ = true;
    // The clip mask of the frame on screen (empty: none), and what the overlay texture was made
    // from, to rebuild it when the mask or the theme changes.
    std::vector<std::uint8_t> clip_mask_;
    int clip_mask_width_ = 0;
    int clip_mask_height_ = 0;
    bool overlay_dirty_ = false;
    std::optional<gui::Theme> overlay_theme_;
    gui::BackgroundExport exporter_;
    // What the photo was last saved with, by export or as a recipe, to tell unsaved edits from
    // none.
    gui::SavedBaseline baseline_;
    gui::EditSettings exporting_settings_;  // the settings of the export in flight
    // The save sheet while it is up: what is typed in its two fields, and the hint for an empty
    // name. Plain buffers, as ImGui's text fields want.
    struct RecipeSheet {
        std::array<char, 256> name{};
        std::array<char, 512> description{};
        std::string hint;
        bool appearing = true;  // the first frame: put the keyboard in the Name field
        bool for_close = false;  // opened by the close prompt's Save Recipe...
    };
    std::optional<RecipeSheet> recipe_sheet_;
    // The recipe the save dialog will write, and the settings it was made from (those on screen
    // when the sheet's Save... was pressed).
    Recipe saving_recipe_;
    gui::EditSettings saving_recipe_settings_;
    // The recipe loaded onto the open photo, if any.
    gui::RecipeTracker recipes_;
    // The "Recipe not loaded" window: what it shows, whether it is open, and whether it should
    // take the focus (the frame it appears).
    gui::RecipeErrorView recipe_error_;
    bool recipe_error_open_ = false;
    bool recipe_error_focus_ = false;
    int recipe_error_centring_ = 0;  // frames left to keep it centred
    // The View recipe window: the look it was opened for, and the same three as above.
    gui::LookRecipeView look_recipe_;
    bool look_recipe_open_ = false;
    bool look_recipe_focus_ = false;
    int look_recipe_centring_ = 0;
    bool quit_ = false;
    // The quit or open being held back, where it is, and (for an open) which file.
    enum class ClosePhase {
        none,
        waiting,
        asking,
        saving_dialog,
        exporting,
        naming_recipe,         // the prompt's Save Recipe...: the sheet is up
        saving_recipe_dialog,  // ... and then its save dialog
    };
    std::optional<gui::Pending> close_pending_;
    ClosePhase close_phase_ = ClosePhase::none;
    std::optional<std::filesystem::path> close_open_path_;
    double last_render_ms_ = 0;

    gui::ViewState view_;              // how the picture is zoomed and panned; a view, never an edit
    gui::ViewGeometry view_geometry_;  // the canvas and the picture, as of the last frame drawn
    // The wheel events of this frame, and whose they are.
    struct CanvasWheelEvent {
        gui::WheelDelta wheel;
        bool trackpad = false;
        std::uint64_t now_ms = 0;
    };
    std::vector<CanvasWheelEvent> canvas_wheels_;
    gui::ScrollLatch scroll_latch_;
    gui::PointerPlatform pointer_platform_ = gui::PointerPlatform::other;
    gui::PinchTracker pinch_{gui::PinchScale::per_event};  // set for the platform when constructed
    float pinch_factor_ = 1.0f;                            // this frame's pinching, all told
    gui::HandDrag hand_drag_;
    gui::SampleMode sample_;
    gui::SampleClick sample_click_;
    std::optional<gui::SamplePin> sample_hover_;  // the pixel under the cursor this frame
    gui::SampleCache sample_pin_trace_;
    gui::SampleCache sample_hover_trace_;
    bool hand_key_held_ = false;   // Space is down and the canvas has it
    bool canvas_press_ = false;    // the left button went down on the canvas itself, and is still down
    std::vector<gui::ViewRect> canvas_taken_;  // this frame's chips and notes on the canvas

    std::optional<std::filesystem::path> pending_open_;
    bool pending_open_shown_ = false;  // the "Opening ..." frame has been drawn
    bool dialog_open_ = false;         // a file dialog is up: don't open another

    bool drag_over_ = false;                      // files are being dragged over the window
    std::vector<std::filesystem::path> dropped_;  // the files of the current drop (see take_drop)

    std::optional<gui::Error> error_;  // shown as a banner until dismissed
    bool error_details_open_ = false;  // the banner's Details are unfolded
    std::string notice_;               // a short note that isn't an error, e.g. "Saved ..."

    // The Explain area follows the hovered control, else the focused one, else the last one shown
    // (or dragged or changed), so a gap between controls keeps the entry just left. The three are
    // filled while a frame is drawn; the area shows the choice made at the end of the previous
    // frame, so its text agrees within a frame.
    std::string explain_hover_;
    std::string explain_focus_;
    std::string explain_last_;
    std::string explain_shown_;

    std::filesystem::path prefs_file_;
    gui::Preferences prefs_;
    bool prefs_open_ = false;
    // SDL event that the macOS app menu's Preferences item sends; 0 if it isn't connected, and
    // then Preferences is in our own Edit menu.
    Uint32 native_prefs_event_ = 0;
    bool about_open_ = false;
    bool notices_open_ = false;  // the full third-party notices, opened from About
    bool popup_open_last_frame_ = false;   // for Esc: see handle_shortcuts
    bool item_active_last_frame_ = false;
    bool* focused_window_last_frame_ = nullptr;  // open flag of the focused closable window
    bool* focused_window_this_frame_ = nullptr;
    // Likewise for the macOS app menu's "About Prism" item; 0: About is in our Help menu.
    Uint32 native_about_event_ = 0;
    float display_scale_ = 1.0f;  // the display's content scale (1.5 at 150% on Windows)
    bool style_dirty_ = true;     // rebuild the style before the next frame

    int quiet_frames_ = 0;  // frames drawn since the last event
};

}  // namespace prism::gui_app

#include "app.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include "about_info.hpp"
#include "active_control.hpp"
#include "imgui.h"
#include "export_sheet.hpp"
#include "imgui_internal.h"  // ClearActiveID: Esc leaves a text field without undoing the typing
#include "light_path.hpp"
#include "macos_menu.hpp"
#include "scopes_panel.hpp"
#include "theme.hpp"
#include "widgets.hpp"
#include "prism/gui/explanations.hpp"
#include "prism/gui/export_size.hpp"
#include "prism/gui/file_types.hpp"
#include "prism/gui/layout.hpp"
#include "prism/gui/neutral_pick.hpp"
#include "prism/gui/paper_grade.hpp"
#include "prism/gui/photo_format.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/slider_gesture.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/heic.hpp"
#include "prism/raw.hpp"
#include "prism/utf8_path.hpp"
#include "prism/version.hpp"

namespace prism::gui_app {

namespace fs = std::filesystem;

namespace {

// ImGui sometimes needs a frame or two after an event to settle (hover, popups opening).
constexpr int settle_frames = 3;

float ui_scale() {
    return ImGui::GetStyle().FontScaleDpi;
}

// "Ctrl+O" on Windows and Linux, "Cmd+O" on macOS, where ImGui maps Ctrl shortcuts to Cmd.
std::string shortcut_label(const char* keys) {
    return std::string(ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd+" : "Ctrl+") + keys;
}

// A rectangle outline drawn as dashes, for the drop area.
void dashed_rect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 colour, float dash,
                 float thickness) {
    auto line = [&](ImVec2 a, ImVec2 b) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        for (float t = 0; t < length; t += 2 * dash) {
            const float t1 = std::min(t + dash, length);
            draw->AddLine(ImVec2(a.x + dx * t / length, a.y + dy * t / length),
                          ImVec2(a.x + dx * t1 / length, a.y + dy * t1 / length), colour,
                          thickness);
        }
    };
    line(min, ImVec2(max.x, min.y));
    line(ImVec2(max.x, min.y), max);
    line(max, ImVec2(min.x, max.y));
    line(ImVec2(min.x, max.y), min);
}

// Centres `text` horizontally in the current window's content region, at the cursor's height.
void centred_text(const std::string& text) {
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) / 2 +
                                            ImGui::GetCursorPosX()));
    ImGui::TextUnformatted(text.c_str());
}

// Text on a dark rounded box at `pos`, for messages drawn over the image.
void overlay_text(ImDrawList* draw, const Palette& colours, ImVec2 pos, const std::string& text) {
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const float pad = token_metrics().overlay_padding;
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x + 2 * pad, pos.y + size.y + 2 * pad),
                        colours.overlay_background, token_metrics().overlay_rounding);
    draw->AddText(ImVec2(pos.x + pad, pos.y + pad), colours.overlay_text, text.c_str());
}

// The rectangle overlay_text covers.
gui::ViewRect overlay_rect(ImVec2 pos, const std::string& text) {
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const float pad = token_metrics().overlay_padding;
    return {{pos.x, pos.y}, {pos.x + size.x + 2 * pad, pos.y + size.y + 2 * pad}};
}

// A "Details" line with a thin chevron (right when closed, down when open) instead of ImGui's
// filled tree arrow. The whole line is the click area. True while open.
bool details_toggle(const char* label, bool* open) {
    const ImVec2 text = ImGui::CalcTextSize(label);
    const float chevron = token_metrics().chevron_size;
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = 2 * chevron + gap + text.x;
    if (ImGui::InvisibleButton(label, ImVec2(width, ImGui::GetTextLineHeightWithSpacing()))) {
        *open = !*open;
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const float cy = std::round(min.y + ImGui::GetTextLineHeight() * 0.5f);
    draw_chevron(ImGui::GetWindowDrawList(), ImVec2(std::round(min.x + chevron), cy),
                 *open ? Chevron::down : Chevron::right, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 2 * chevron + gap, min.y),
                                        ImGui::GetColorU32(ImGuiCol_Text), label);
    return *open;
}

std::string file_name(const fs::path& path) {
    return utf8(path.filename());
}

// `text` into one of ImGui's fixed text buffers, cut short (on a whole UTF-8 character) if it
// doesn't fit.
template <std::size_t N>
void copy_to_buffer(std::array<char, N>& buffer, std::string_view text) {
    std::size_t n = std::min(text.size(), N - 1);
    while (n > 0 && n < text.size() && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) {
        --n;
    }
    text.copy(buffer.data(), n);
    buffer[n] = '\0';
}

// What was typed in a one-line field, without the spaces around it; nothing if that leaves none.
std::optional<std::string> typed_text(const char* buffer) {
    std::string_view text(buffer);
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    return text.empty() ? std::nullopt : std::optional<std::string>(text);
}

bool folder_exists(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

// The app's session keeps the full-size photo for real-pixel zoom (#37): it reads it in the
// background after opening, and App polls for it and exports through begin_export.
gui::SessionOptions app_session_options() {
    gui::SessionOptions options;
    options.detail_base = true;
    return options;
}

}  // namespace

App::App(SDL_Window* window, SDL_Renderer* renderer, DialogQueue& dialogs,
         fs::path preferences_file)
    : window_(window),
      dialogs_(dialogs),
      session_(app_session_options()),
      texture_(renderer),
      detail_texture_(renderer),
      scope_view_(renderer),
      histogram_texture_(renderer),
      prefs_file_(std::move(preferences_file)),
      display_scale_(content_scale(window)) {
#if defined(__APPLE__)
    pointer_platform_ = gui::PointerPlatform::macos;
#elif defined(_WIN32)
    pointer_platform_ = gui::PointerPlatform::windows;
#else
    if (const char* driver = SDL_GetCurrentVideoDriver(); driver != nullptr &&
                                                          std::string_view(driver) == "wayland") {
        pointer_platform_ = gui::PointerPlatform::wayland;
    }
#endif
    pinch_ = gui::PinchTracker(gui::pinch_scale_of(pointer_platform_));
    if (!prefs_file_.empty()) {
        prefs_ = gui::load_preferences(prefs_file_);  // the defaults if missing or unreadable
    }
    session_.set_export_size(prefs_.export_size);  // the Export sheet opens on the last choice
    apply_minimum_size();
#ifdef __APPLE__
    const Uint32 event = SDL_RegisterEvents(1);
    if (event != 0 && connect_macos_preferences_item(event)) {
        native_prefs_event_ = event;
    }
    const Uint32 about_event = SDL_RegisterEvents(1);
    if (about_event != 0 && connect_macos_about_item(about_event)) {
        native_about_event_ = about_event;
    }
#endif
}

void App::save_preferences() {
    if (prefs_file_.empty()) {
        return;
    }
    if (std::optional<std::string> error = gui::save_preferences(prefs_file_, prefs_)) {
        show_error({gui::ErrorKind::save_failed, "Couldn't save your preferences.", *error});
    }
}

void App::set_preferences(gui::Preferences prefs) {
    if (prefs == prefs_) {
        return;
    }
    prefs_ = std::move(prefs);
    style_dirty_ = true;  // theme and size apply live, from the next frame
    save_preferences();
}

float App::content_scale(SDL_Window* window) {
    // The display's content scale: how much bigger the UI should be drawn (1.5 at 150% on
    // Windows). Not SDL_GetWindowDisplayScale, which also includes the pixel density (2 on a
    // Retina Mac): ImGui already gets that through its framebuffer scale and renders crisp text
    // with it, so including it again would draw everything twice as large.
    const float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
    return scale > 0 ? scale : 1.0f;
}

void App::apply_minimum_size() {
    // Small enough for a laptop at 200%, big enough that the panel and canvas both fit: room for
    // the panel at the narrowest that shows its texts in full, a splitter and some canvas, and
    // never less than 640 wide. Window coordinates are points on macOS but pixels on Windows,
    // hence the content scale.
    const float width = gui::window_minimum_width(panel_text_min_, token_metrics().item_spacing.x,
                                                  scale(), 640 * display_scale_);
    SDL_SetWindowMinimumSize(window_, static_cast<int>(std::ceil(width)),
                             static_cast<int>(440 * display_scale_));
}

float App::scale() const {
    return gui::interface_scale(prefs_, display_scale_);
}

void App::open_utf8(const char* path_utf8) {
    request_open(path_from_utf8(path_utf8));
}

void App::request_open(fs::path path) {
    if (close_pending_) {
        return;  // a quit or an open is already being decided
    }
    close_open_path_ = std::move(path);
    begin_close(gui::Pending::open_photo);
}

void App::start_open(fs::path path) {
    pending_open_ = std::move(path);
    pending_open_shown_ = false;
}

void App::request_quit() {
    if (close_pending_ == gui::Pending::quit) {
        return;
    }
    close_open_path_.reset();  // a quit wins over an open that is still being decided
    recipe_sheet_.reset();     // and over a save sheet: it closes before the question
    export_sheet_.reset();     // and over the Export sheet
    close_pending_.reset();
    close_phase_ = ClosePhase::none;
    begin_close(gui::Pending::quit);
    if (close_pending_) {
        SDL_RestoreWindow(window_);  // so the question can be seen
    }
}

gui::CloseState App::close_state() const {
    gui::CloseState state;
    state.has_file = session_.has_file();
    state.dirty = state.has_file && baseline_.dirty(session_.settings());
    state.has_strokes = state.has_file && !session_.settings().strokes.empty();
    state.has_frame = state.has_file && session_.settings().frame.has_value();
    state.export_running = exporter_.busy();
    return state;
}

void App::begin_close(gui::Pending pending) {
    close_pending_ = pending;
    apply_close_step(gui::close_requested(close_state()));
}

void App::apply_close_step(gui::CloseStep step) {
    switch (step) {
        case gui::CloseStep::proceed: {
            const gui::Pending pending = *close_pending_;
            close_pending_.reset();
            close_phase_ = ClosePhase::none;
            if (pending == gui::Pending::quit) {
                quit_ = true;
            } else if (close_open_path_) {
                start_open(std::move(*close_open_path_));
            }
            close_open_path_.reset();
            break;
        }
        case gui::CloseStep::ask:
            close_phase_ = ClosePhase::asking;
            break;
        case gui::CloseStep::wait_for_export:
            close_phase_ = ClosePhase::waiting;
            break;
        case gui::CloseStep::choose_file:
            close_phase_ = ClosePhase::saving_dialog;
            begin_export(true);
            if (!export_sheet_) {
                apply_close_step(gui::CloseStep::cancel);  // the sheet could not open
            }
            break;
        case gui::CloseStep::await_export:
            close_phase_ = ClosePhase::exporting;
            break;
        case gui::CloseStep::name_recipe:
            close_phase_ = ClosePhase::naming_recipe;
            begin_save_recipe(true);
            if (!recipe_sheet_) {
                apply_close_step(gui::CloseStep::cancel);
            }
            break;
        case gui::CloseStep::choose_recipe_file:
            close_phase_ = ClosePhase::saving_recipe_dialog;
            show_recipe_save_dialog();
            if (!dialog_open_) {
                apply_close_step(gui::CloseStep::cancel);  // the dialog could not open
            }
            break;
        case gui::CloseStep::cancel:
            close_pending_.reset();
            close_phase_ = ClosePhase::none;
            close_open_path_.reset();
            break;
    }
}

void App::open_now(const fs::path& path) {
    gui::MaybeError error;
    try {
        error = session_.open_file(path);
    } catch (const std::exception& e) {
        // Session reports what it expects as an Error; anything else still must not crash.
        error = gui::Error{gui::ErrorKind::unreadable,
                           "Couldn't open '" + file_name(path) + "'.", e.what()};
    }
    if (error) {
        show_error(std::move(*error));  // the previous photo, if any, stays open
        return;
    }
    error_.reset();
    if (gui::framing_leaves(framing_.on(), gui::FramingEvent::photo_opened)) {
        framing_ = {};  // the old photo's draft and gesture go with it, as the brush's do
    }
    baseline_.opened(session_.settings());
    recipes_.photo_opened();
    texture_.reset();  // don't show the previous photo under the new name
    drop_detail();     // nor its real pixels
    detail_photo_opened_ = true;
    histogram_.reset();
    clips_.reset();
    clip_note_.reset();
    note_report_.reset();
    last_frame_had_clips_ = false;
    clip_mask_.clear();
    texture_.reset_overlay();
    stroke_mask_.clear();
    texture_.reset_stroke_overlay();
    scope_view_.set(std::nullopt);
    {
        const gui::ViewAfterOpen start = gui::view_after_open();
        viewed_stage_ = start.viewed;  // each photo opens at Decode, the first row
        compare_stage_ = start.compare;
    }
    strip_.reset();
    strip_bands_.clear();
    view_ = {};  // each photo starts whole
    sample_.opened();
    // The new photo's history has no gesture in it: what the brush was doing is dropped, not
    // committed, before it leaves.
    brush_gesture_ = {};
    stroke_painter_ = {};
    if (gui::brush_leaves(brush_.on(), gui::BrushEvent::photo_opened, viewed_stage_)) {
        leave_brush();
    }
    sample_click_.cancel();
    leave_pick();
    sample_pin_trace_.clear();
    sample_hover_trace_.clear();
    sample_pin_photo_trace_.clear();
    sample_hover_photo_trace_.clear();
    sample_pin_file_.reset();
    sample_pin_file_for_.reset();
    sample_hover_file_.reset();
    sample_pin_photo_ = false;
    sample_hover_photo_ = false;
    explain_last_.clear();
    SDL_SetWindowTitle(window_, ("Prism — " + file_name(path)).c_str());
}

void App::show_open_dialog() {
    if (dialog_open_ || recipe_sheet_ || export_sheet_) {
        return;
    }
    dialog_open_ = true;
    const fs::path folder = session_.has_file() ? session_.path().parent_path() : fs::path();
    gui_app::show_open_dialog(window_, dialogs_, folder);
}

// Export…: the sheet first, on the remembered size. Its Save As… goes on to the save dialog.
void App::begin_export(bool for_close) {
    if (dialog_open_ || recipe_sheet_ || export_sheet_ || !session_.has_file() ||
        exporter_.busy()) {
        return;
    }
    ExportSheetState sheet;
    sheet.size = session_.export_size();
    export_sheet_ = sheet;
    export_sheet_for_close_ = for_close;
}

void App::show_save_dialog() {
    if (dialog_open_ || recipe_sheet_ || export_sheet_ || !session_.has_file() ||
        exporter_.busy()) {
        return;
    }
    dialog_open_ = true;
    const gui::ExportFormat format = gui::default_export_format(session_.path());
    // The name says the size only when the export is really reduced (a RAW file's half-size decode
    // can already be what 50% asks for).
    gui_app::show_save_dialog(
        window_, dialogs_,
        gui::suggested_export_path(session_.path(), format, session_.export_plan().long_side),
        format);
}

void App::take_dialog_results() {
    for (DialogResult& result : dialogs_.take_all()) {
        dialog_open_ = false;
        switch (result.kind) {
            case DialogResult::Kind::open:
                if (!result.error.empty()) {
                    show_error({gui::ErrorKind::unreadable, "Couldn't show the file dialog.",
                                result.error});
                } else if (result.path) {
                    request_open(std::move(*result.path));
                }
                break;
            case DialogResult::Kind::save:
                take_export_dialog(result);
                break;
            case DialogResult::Kind::recipe_load:
                if (!result.error.empty()) {
                    show_error({gui::ErrorKind::unreadable, "Couldn't show the file dialog.",
                                result.error});
                } else if (result.path && session_.has_file()) {
                    load_recipe_file(*result.path, true);
                }
                break;
            case DialogResult::Kind::recipe_save: {
                const gui::RecipeEnd end = take_recipe_save_dialog(result);
                if (close_pending_ && close_phase_ == ClosePhase::saving_recipe_dialog) {
                    apply_close_step(gui::recipe_save_finished(end));
                }
                break;
            }
        }
    }
}

// The export's save dialog answered: start the export, or (cancelled or failed) drop the quit or
// open that asked for it.
void App::take_export_dialog(DialogResult& result) {
    const bool for_close = close_phase_ == ClosePhase::saving_dialog;
    if (!result.error.empty()) {
        show_error({gui::ErrorKind::unreadable, "Couldn't show the file dialog.", result.error});
    }
    if (!result.path || !session_.has_file()) {
        if (for_close) {
            apply_close_step(gui::save_dialog_closed(false));
        }
        return;
    }
    // The export works on a copy of the settings: what the preview shows right now.
    const fs::path output = gui::with_export_extension(*result.path, result.format);
    error_.reset();
    notice_.clear();
    exporting_settings_ = session_.settings();
    exports_.start(output);  // the session pauses its read of the full-size photo meanwhile
    if (for_close) {
        apply_close_step(gui::save_dialog_closed(true));
    }
}

bool App::recipes_available() const {
    return session_.has_file() && !dialog_open_ && !recipe_sheet_ && !export_sheet_ &&
           !exporter_.busy() &&
           !close_pending_;
}

// Save Recipe…: the sheet first, with the name and description Prism suggests.
void App::begin_save_recipe(bool for_close) {
    // The close prompt's Save Recipe… comes while a quit or open is pending, which otherwise
    // keeps recipes unavailable.
    if (for_close ? (!session_.has_file() || dialog_open_ || recipe_sheet_ || export_sheet_ || exporter_.busy())
                  : !recipes_available()) {
        return;
    }
    const gui::RecipeNameDefault suggested =
        gui::suggest_recipe_name(recipes_.current(), session_.kind(), session_.settings());
    RecipeSheet sheet;
    copy_to_buffer(sheet.name, suggested.name);
    copy_to_buffer(sheet.description, suggested.description);
    sheet.hint = std::string(suggested.hint);
    sheet.for_close = for_close;
    recipe_sheet_ = std::move(sheet);
}

// The save sheet: Name, Description, what is saved, Save… and Cancel. Modal. Esc cancels when no
// text field has the keyboard; inside a field the first Esc only leaves the field. Enter in a
// field is Save….
void App::draw_recipe_sheet() {
    constexpr const char* popup = "Save Recipe";
    if (recipe_sheet_ && !ImGui::IsPopupOpen(popup)) {
        ImGui::OpenPopup(popup);
    }
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(popup, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    if (!recipe_sheet_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    RecipeSheet& sheet = *recipe_sheet_;
    // As of the last frame, before the fields below see the keys. ImGui would undo what was typed
    // when Esc leaves a field, so the field is let go of here first and keeps its text.
    const bool field_active = ImGui::IsAnyItemActive();
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const bool enter = field_active && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (escape && field_active) {
        ImGui::ClearActiveID();
    }

    const float width = token_metrics().prompt_wrap;
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(width);
    if (sheet.appearing) {
        ImGui::SetKeyboardFocusHere();
        sheet.appearing = false;
    }
    ImGui::InputTextWithHint("##recipe_name", sheet.hint.c_str(), sheet.name.data(),
                             sheet.name.size());
    explain(gui::explain::recipe_name);  // no tooltip: it would cover the field below
    ImGui::TextUnformatted("Description (optional)");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##recipe_description", sheet.description.data(), sheet.description.size());
    explain(gui::explain::recipe_description);  // no tooltip: it would cover the field below
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    // A chosen white balance is saved too; As shot is each photo's own light and is not.
    ImGui::TextUnformatted(
        session_.settings().white_balance
            ? "Saves all five stages and the white balance you chose. The photo and RAW decoding "
              "options aren't included."
            : "Saves all five stages. The photo and RAW decoding options aren't included.");
    if (const std::string_view note = gui::recipe_sheet_strokes_note(session_.settings());
        !note.empty()) {
        ImGui::TextUnformatted(note.data(), note.data() + note.size());
    }
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const bool save = ImGui::Button("Save…") || enter;
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel") || (escape && !field_active);
    if (save || cancel) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();

    if (save) {
        // The recipe is made now, from what is on screen as Save… is pressed.
        saving_recipe_settings_ = session_.settings();
        saving_recipe_ = gui::recipe_from(saving_recipe_settings_);
        saving_recipe_.name = typed_text(sheet.name.data());
        saving_recipe_.description = typed_text(sheet.description.data());
        const bool for_close = sheet.for_close;
        recipe_sheet_.reset();
        if (for_close) {
            apply_close_step(gui::recipe_sheet_closed(true));  // shows the dialog
        } else {
            show_recipe_save_dialog();
        }
    } else if (cancel) {
        const bool for_close = sheet.for_close;
        recipe_sheet_.reset();
        if (for_close) {
            apply_close_step(gui::recipe_sheet_closed(false));
        }
    }
}

// The Export sheet: Size, its number, what will be written, Cancel and Save As…. Modal, a fixed size
// from export_sheet_size(). Save As… keeps the choice (for this session and in the preferences) and
// opens the save dialog; Cancel, or Esc, closes it and changes nothing.
void App::draw_export_popup() {
    constexpr const char* popup = "export_sheet";
    if (export_sheet_ && !ImGui::IsPopupOpen(popup)) {
        ImGui::OpenPopup(popup);
    }
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(export_sheet_size());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(export_sheet_padding(), export_sheet_padding()));
    const bool open = ImGui::BeginPopupModal(
        popup, nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!open) {
        return;
    }
    if (!export_sheet_ || !session_.has_file()) {
        export_sheet_.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ExportSheetCallbacks callbacks;
    callbacks.explain = [this](std::string_view id) { explain(id); };
    const ExportSheetContext context{session_.framed_file_size(), session_.framed_start_size(),
                                     session_.file_size_exact(),
                                     session_.preview_framing().has_value()};
    const ExportSheetResult result = draw_export_sheet(*export_sheet_, context, callbacks);
    if (result != ExportSheetResult::none) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();

    if (result == ExportSheetResult::none) {
        return;
    }
    const gui::ExportSize chosen = export_sheet_->size;
    const bool for_close = export_sheet_for_close_;
    export_sheet_.reset();
    export_sheet_for_close_ = false;
    if (result == ExportSheetResult::cancel) {
        if (for_close) {
            apply_close_step(gui::save_dialog_closed(false));
        }
        return;
    }
    session_.set_export_size(chosen);
    gui::Preferences p = prefs_;
    p.export_size = chosen;
    set_preferences(std::move(p));
    show_save_dialog();
    if (for_close && !dialog_open_) {
        apply_close_step(gui::CloseStep::cancel);  // the dialog could not open
    }
}

void App::show_recipe_save_dialog() {
    if (dialog_open_ || !session_.has_file()) {
        return;
    }
    dialog_open_ = true;
    const fs::path folder =
        gui::recipe_start_folder(prefs_.recipe_folder, session_.path(), folder_exists);
    const std::string name = gui::suggested_recipe_file_name(saving_recipe_.name.value_or(""));
    gui_app::show_recipe_save_dialog(window_, dialogs_, folder / path_from_utf8(name));
}

gui::RecipeEnd App::take_recipe_save_dialog(const DialogResult& result) {
    if (!result.error.empty()) {
        show_error({gui::ErrorKind::unreadable, "Couldn't show the file dialog.", result.error});
        return gui::RecipeEnd::failed;
    }
    if (!result.path) {
        return gui::RecipeEnd::cancelled;
    }
    // Written here and now: a recipe is a few hundred bytes.
    const fs::path path = gui::with_recipe_extension(*result.path);
    try {
        prism::save_recipe(path, saving_recipe_);
    } catch (const std::exception& e) {
        show_error({gui::ErrorKind::save_failed,
                    "Couldn't save the recipe " + file_name(path) + ".", e.what()});
        return gui::RecipeEnd::failed;
    }
    baseline_.recipe_saved(saving_recipe_settings_, file_name(path));
    error_.reset();
    notice_ = "Saved recipe " + file_name(path) + ".";
    remember_recipe_folder(path);
    return gui::RecipeEnd::saved;
}

void App::remember_recipe_folder(const fs::path& file) {
    gui::Preferences p = prefs_;
    p.recipe_folder = file.parent_path();
    set_preferences(std::move(p));
}

void App::show_recipe_load_dialog() {
    if (!recipes_available()) {
        return;
    }
    dialog_open_ = true;
    gui_app::show_recipe_load_dialog(
        window_, dialogs_,
        gui::recipe_start_folder(prefs_.recipe_folder, session_.path(), folder_exists));
}

void App::load_recipe_file(const fs::path& path, bool remember_folder) {
    const std::string name = file_name(path);
    Recipe recipe;
    try {
        recipe = prism::load_recipe(path);
    } catch (const RecipeError& e) {
        recipe_error_ = gui::recipe_error_view(e, name);
        recipe_error_open_ = true;
        recipe_error_focus_ = true;
        return;
    } catch (const std::exception& e) {
        // Not expected (load_recipe reports everything as a RecipeError), but shown the same way.
        recipe_error_ = gui::recipe_error_view(RecipeError(name, {{0, e.what()}}), name);
        recipe_error_open_ = true;
        recipe_error_focus_ = true;
        return;
    }
    // Its own undo step, never merged into a gesture; a recipe never re-decodes the file.
    session_.end_gesture();
    const gui::EditSettings before = session_.settings();
    if (gui::MaybeError error = session_.apply_recipe(recipe)) {
        show_error(std::move(*error));
        return;
    }
    recipes_.loaded(gui::make_loaded_recipe(recipe, name, before, session_.settings()));
    recipe_error_open_ = false;  // an earlier recipe's mistakes no longer apply
    error_.reset();
    if (remember_folder) {
        remember_recipe_folder(path);
    }
}

// "Recipe not loaded": a normal window, not modal, that takes the focus as it appears. Esc closes
// it while it has the focus (note_if_focused).
void App::draw_recipe_error() {
    if (!recipe_error_open_) {
        return;
    }
    // Centred for its first few frames, while its size settles (the table's wrapped rows are
    // measured over a frame or two); after that it can be moved.
    if (recipe_error_focus_) {
        ImGui::SetNextWindowFocus();
        recipe_error_focus_ = false;
        recipe_error_centring_ = 3;
    }
    if (recipe_error_centring_ > 0) {
        --recipe_error_centring_;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
    }
    if (!ImGui::Begin("Recipe not loaded", &recipe_error_open_,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    note_if_focused(&recipe_error_open_);
    const float wrap = token_metrics().about_wrap;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(recipe_error_.header.c_str());
    ImGui::PopTextWrapPos();
    const float line_width = ImGui::CalcTextSize("Line").x;
    const float message_width = wrap - line_width - ImGui::GetStyle().CellPadding.x * 4;
    if (ImGui::BeginTable("mistakes", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, line_width);
        ImGui::TableSetupColumn("Mistake", ImGuiTableColumnFlags_WidthFixed, message_width);
        // A plain muted header, not ImGui's header row, whose background the theme doesn't set.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Line");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Mistake");
        for (const gui::RecipeErrorRow& row : recipe_error_.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", row.line.c_str());
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(row.message.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }
    if (!recipe_error_.note.empty()) {
        ImGui::TextDisabled("%s", recipe_error_.note.c_str());
    }
    ImGui::Spacing();
    if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(recipe_error_.copy_text.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        recipe_error_open_ = false;
    }
    ImGui::End();
}

// View recipe: a built-in look's file, to read and copy. A normal window, not modal, like "Recipe
// not loaded", and Esc closes it the same way. Clicking into the text gives the first Esc to the
// text, as for any field.
void App::draw_look_recipe() {
    if (!look_recipe_open_) {
        return;
    }
    if (look_recipe_focus_) {
        ImGui::SetNextWindowFocus();
        look_recipe_focus_ = false;
        look_recipe_centring_ = 3;
    }
    if (look_recipe_centring_ > 0) {
        --look_recipe_centring_;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
    }
    // ###: the same window whichever look it shows.
    const std::string title = look_recipe_.title + "###look_recipe";
    if (!ImGui::Begin(title.c_str(), &look_recipe_open_,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    note_if_focused(&look_recipe_open_);
    // Sized to the text, but on a small window or at a large interface size it wraps and scrolls.
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 room = ImGui::GetMainViewport()->WorkSize;
    const ImVec2 text = ImGui::CalcTextSize(look_recipe_.text.c_str());
    const ImVec2 size(std::min(text.x + style.FramePadding.x * 2 + style.ScrollbarSize,
                               room.x * 0.85f),
                      std::min(text.y + ImGui::GetTextLineHeight() + style.FramePadding.y * 2,
                               room.y * 0.65f));
    // ReadOnly: ImGui never writes to the buffer, and selecting and Cmd/Ctrl+C still work.
    ImGui::InputTextMultiline("##look_recipe_text", look_recipe_.text.data(),
                              look_recipe_.text.size() + 1, size,
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x);
    ImGui::TextDisabled("%s", look_recipe_.note.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(look_recipe_.text.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        look_recipe_open_ = false;
    }
    ImGui::End();
}

// Under the Look dropdown while a recipe is loaded: which stages it sets, and its description as a
// tooltip. The recipe's name is on the dropdown itself.
void App::draw_recipe_line() {
    if (!recipes_.line(session_.settings())) {
        return;
    }
    const gui::LoadedRecipe& loaded = *recipes_.current();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(gui::stages_line(loaded.stages).c_str());
    ImGui::PopStyleColor();
    explain(gui::explain::recipe_loaded);
    if (!loaded.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", loaded.description.c_str());
    }
    ImGui::PopTextWrapPos();
}

void App::take_export_result() {
    std::optional<gui::ExportResult> result = exports_.poll();
    if (!result) {
        return;
    }
    gui::ExportEnd end = gui::ExportEnd::saved;
    if (result->error && result->error->kind == gui::ErrorKind::cancelled) {
        notice_ = "Export cancelled. Nothing was written.";
        end = gui::ExportEnd::cancelled;
    } else if (result->error) {
        show_error(std::move(*result->error));
        end = gui::ExportEnd::failed;
    } else {
        notice_ = "Saved " + file_name(result->output) + ".";
        // What was exported, not what is on screen now.
        baseline_.exported(exporting_settings_, file_name(result->output));
    }
    if (close_pending_ && close_phase_ == ClosePhase::exporting) {
        apply_close_step(gui::export_finished(end));
    } else if (close_pending_ && close_phase_ == ClosePhase::waiting) {
        apply_close_step(gui::close_requested(close_state()));  // decide again with the new state
    }
}

prism::PixelSize App::framed_full_size() const {
    if (const std::shared_ptr<const prism::Photo> base = session_.detail_base()) {
        try {
            return prism::plan_export_frame(session_.settings().frame, base->image.width,
                                            base->image.height)
                .size;
        } catch (const std::invalid_argument&) {
            // The session's settings are valid; nothing to measure if they somehow aren't.
        }
    }
    return session_.framed_start_size();
}

void App::set_view_file_scale(float scale) {
    view_ = gui::view_rescaled(view_, view_file_scale_, scale);
    view_file_scale_ = scale;
    view_geometry_.file_scale = scale;
}

bool App::canvas_gesture_now() const {
    gui::CanvasActivity activity;
    activity.hand_drag = hand_drag_.dragging();
    activity.wheel = gui::wheel_gesture_live(static_cast<double>(SDL_GetTicks()), last_wheel_ms_);
    activity.pinch = pinch_this_frame_;
    activity.brush_stroke = brush_gesture_.stroking();
    activity.control_active = control_active();
    return gui::canvas_gesture(activity);
}

void App::drop_detail() {
    detail_renderer_.clear();
    detail_texture_.reset();
    detail_shown_.reset();
    detail_submitted_.reset();
    detail_awaited_ = 0;
    detail_plan_ = {};
}

void App::update_detail() {
    const std::shared_ptr<const prism::Photo> base = session_.detail_base();
    const std::uint64_t base_id =
        detail_base_number_.update(base, std::exchange(detail_photo_opened_, false));

    // A frame that finished since the last one: only one of the current base comes back.
    if (std::optional<gui::DetailFrame> frame = detail_renderer_.poll(base_id)) {
        quiet_frames_ = 0;
        if (frame->generation >= detail_awaited_) {
            detail_awaited_ = 0;  // the newest asked for has arrived
        }
        if (frame->error.empty() && detail_texture_.upload(*frame)) {
            detail_shown_ =
                gui::ShownDetail{frame->key, frame->region, frame->picture, frame->frame};
        } else {
            // A failed render (or upload) shows nothing: the enlarged preview stays.
            detail_texture_.reset();
            detail_shown_.reset();
        }
    }

    gui::DetailFrameInput in;
    in.view = view_;
    in.geometry = view_geometry_;
    if (const std::shared_ptr<const prism::Image> preview = session_.preview_base()) {
        in.framed_preview_now = {preview->width, preview->height};
    }
    if (base) {
        in.framed_full = framed_full_size();
    }
    in.frame = session_.settings().frame;
    in.base_state = session_.detail_base_state();
    in.base = base_id;
    in.settings = session_.settings_generation();
    in.gesture = canvas_gesture_now();
    in.mode = gui::canvas_view(
        framing_.on(), strip_.has_value(),
        !strip_ && gui::effective_compare(viewed_stage_, compare_stage_).has_value(),
        viewed_stage_ != gui::final_stage, prefs_.clip_overlay, stroke_mask_on_);
    in.shown = detail_shown_;
    in.submitted = detail_submitted_;
    detail_plan_ = gui::plan_detail_frame(in);

    if (detail_plan_.clear) {
        detail_renderer_.clear();
        detail_texture_.reset();
        detail_shown_.reset();
        detail_submitted_.reset();
        detail_awaited_ = 0;
    }
    if (detail_plan_.submit && base) {
        const gui::DetailSubmit& ask = *detail_plan_.submit;
        gui::DetailRenderRequest request;
        request.base = base;
        request.settings = session_.settings();
        request.request = ask.request;
        request.output = ask.output;
        request.key = ask.key;
        std::uint64_t generation = 0;
        try {
            generation = detail_renderer_.submit(std::move(request), in.gesture);
        } catch (const std::invalid_argument&) {
            // plan_detail_frame never asks for an empty region or a size downscale can't make;
            // if it did, the enlarged preview stays rather than the app stopping.
        }
        detail_submitted_ =
            gui::SubmittedDetail{ask.key,     ask.request.region, ask.output,
                                 ask.picture, generation != 0,    ask.hold || generation == 0};
        // A refused request also dropped the one waiting; while the gesture goes on the app draws
        // anyway, and on release the region is asked for again.
        detail_awaited_ = generation;
    }
}

void App::update_preview() {
    // Scopes and the clip mask are only computed while they are on screen (the overlay on); the
    // histogram and the per-stage clipping come with every frame.
    if (strip_ && !gui::strip_available(viewed_stage_)) {
        strip_.reset();  // the Decode stage shows no edit, so there is nothing to vary
    }
    if (pick_.on() && !gui::pick_available(viewed_stage_, compare_stage_)) {
        leave_pick();  // White balance isn't in the picture, so there is nothing to judge a grey by
    }
    if (gui::brush_leaves(brush_.on(), gui::BrushEvent::view_changed, viewed_stage_)) {
        leave_brush();  // the strokes aren't in the picture before Dodge & burn
    }
    gui::PreviewOptions options;
    options.scopes = prefs_.scope != gui::ScopeChoice::histogram;
    // The Light path is on screen whenever a photo is open, and shows every stage's clipping, so
    // it needs the report also during a drag: a stage's marks follow its slider. It costs about
    // 4.6 ms on a 2560 preview, once per render started, not per UI frame. Not with a strip, whose
    // bands each have their own light.
    options.clip_report = !strip_;
    options.clip_mask = prefs_.clip_overlay;
    options.stroke_mask = stroke_mask_on_;
    options.compare_stage = strip_ ? std::nullopt : gui::effective_compare(viewed_stage_, compare_stage_);
    options.test_strip = strip_;
    options.as_shot_white = session_.as_shot_white();
    if (framing_.on()) {
        // While framing the canvas shows the WHOLE photo, turned on the GPU (#58): the render is of
        // the whole base, without the frame, so a frame committed during framing renders nothing
        // new; only leaving makes one framed render. The scopes then measure the whole photo too.
        gui::EditSettings whole = session_.settings();
        whole.frame.reset();
        options.framing.reset();
        options.compare_stage.reset();
        preview_.update(session_.whole_base(), whole, viewed_stage_, options);
    } else {
        options.framing = session_.preview_framing();
        preview_.update(session_.preview_base(), session_.settings(), viewed_stage_, options);
    }
    std::optional<gui::PreviewFrame> frame = preview_.poll();
    if (!frame) {
        return;
    }
    if (!frame->error.empty()) {
        show_error({gui::ErrorKind::invalid_settings, "Couldn't render the preview.",
                    frame->error});
        return;
    }
    if (!texture_.upload(*frame)) {
        show_error({gui::ErrorKind::out_of_memory, "Couldn't show the preview.", SDL_GetError()});
        return;
    }
    if (frame->compare) {
        if (!texture_.upload_compare(*frame->compare)) {
            show_error({gui::ErrorKind::out_of_memory, "Couldn't show the preview.",
                        SDL_GetError()});
            return;
        }
    } else {
        texture_.reset_compare();
    }
    histogram_ = std::move(frame->histogram);
    last_frame_had_clips_ = frame->clips.has_value();
    if (frame->clips) {
        clip_note_ = gui::make_clip_note(*frame->clips);
        note_report_ = *frame->clips;
    }
    clips_ = std::move(frame->clips);
    clip_mask_ = std::move(frame->clip_mask);
    clip_mask_width_ = frame->width;
    clip_mask_height_ = frame->height;
    overlay_dirty_ = true;
    stroke_mask_ = std::move(frame->stroke_mask);
    stroke_mask_width_ = frame->width;
    stroke_mask_height_ = frame->height;
    stroke_overlay_dirty_ = true;
    scope_view_.set(std::move(frame->scopes));
    // A strip frame's histogram and scopes are the middle band's; the others wait in strip_bands_
    // for the cursor.
    strip_bands_ = std::move(frame->strip);
    strip_shown_band_ = strip_bands_.empty()
                            ? -1
                            : gui::strip_middle(static_cast<int>(strip_bands_.size()));
    histogram_stage_ = frame->stage;  // may differ from viewed_stage_ until the next frame
    last_render_ms_ = frame->total_ms;
}

// (Re)makes the overlay texture when the mask or the theme changed. Called while drawing, where
// the theme is known.
void App::update_clip_overlay_texture() {
    if (!prefs_.clip_overlay || clip_mask_.empty()) {
        texture_.reset_overlay();
        overlay_theme_.reset();
        return;
    }
    const gui::Theme current = theme();
    if (!overlay_dirty_ && overlay_theme_ == current && texture_.overlay() != nullptr) {
        return;
    }
    const Palette& colours = palette(current);
    texture_.upload_overlay(clip_mask_, clip_mask_width_, clip_mask_height_,
                            colours.clip_overlay_over, colours.clip_overlay_under,
                            colours.clip_overlay_gamut);
    overlay_dirty_ = false;
    overlay_theme_ = current;
}

// The same for the stroke mask, while it is on.
void App::update_stroke_overlay_texture() {
    if (!stroke_mask_on_ || stroke_mask_.empty()) {
        texture_.reset_stroke_overlay();
        stroke_overlay_theme_.reset();
        return;
    }
    const gui::Theme current = theme();
    if (!stroke_overlay_dirty_ && stroke_overlay_theme_ == current &&
        texture_.stroke_overlay() != nullptr) {
        return;
    }
    const Palette& colours = palette(current);
    texture_.upload_stroke_overlay(stroke_mask_, stroke_mask_width_, stroke_mask_height_,
                                   colours.stroke_overlay_dodge, colours.stroke_overlay_burn);
    stroke_overlay_dirty_ = false;
    stroke_overlay_theme_ = current;
}

void App::draw_picture_overlays(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec2 uv0,
                                ImVec2 uv1) const {
    const auto id = [](SDL_Texture* t) {
        return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(t)));
    };
    if (texture_.overlay() != nullptr && prefs_.clip_overlay &&
        clip_mask_width_ == texture_.width() && clip_mask_height_ == texture_.height()) {
        draw->AddImage(id(texture_.overlay()), min, max, uv0, uv1);
    }
    if (texture_.stroke_overlay() != nullptr && stroke_mask_on_ &&
        stroke_mask_width_ == texture_.width() && stroke_mask_height_ == texture_.height()) {
        draw->AddImage(id(texture_.stroke_overlay()), min, max, uv0, uv1);
    }
}

void App::toggle_clip_overlay() {
    if (!session_.has_file()) {
        return;
    }
    gui::Preferences p = prefs_;
    p.clip_overlay = !p.clip_overlay;
    set_preferences(std::move(p));
}

void App::show_error(gui::Error error) {
    error_ = std::move(error);
}

void App::report(gui::MaybeError error) {
    if (error) {
        show_error(std::move(*error));
    }
}

void App::undo() {
    // Not during a framing drag: its release commits from where it began (FramingState::follow).
    if (framing_.gesture() != gui::FramingGesture::none) {
        return;
    }
    if (session_.history().can_undo()) {
        const std::optional<prism::Frame> before = session_.settings().frame;
        report(session_.undo());
        framing_.follow(session_.settings().frame);  // the draft follows the frame undone
        sample_.frame_changed(before, session_.settings().frame);
    }
}

void App::redo() {
    if (framing_.gesture() != gui::FramingGesture::none) {
        return;
    }
    if (session_.history().can_redo()) {
        const std::optional<prism::Frame> before = session_.settings().frame;
        report(session_.redo());
        framing_.follow(session_.settings().frame);
        sample_.frame_changed(before, session_.settings().frame);
    }
}

gui::Theme App::theme() const {
    gui::SystemTheme system = gui::SystemTheme::unknown;
    switch (SDL_GetSystemTheme()) {
        case SDL_SYSTEM_THEME_LIGHT:
            system = gui::SystemTheme::light;
            break;
        case SDL_SYSTEM_THEME_DARK:
            system = gui::SystemTheme::dark;
            break;
        default:
            break;
    }
    return gui::resolve_theme(prefs_.theme, system);
}

ImVec4 App::clear_colour() const {
    return palette(theme()).window_clear;
}

void App::before_frame() {
    if (style_dirty_) {
        apply_style(theme(), scale());
        style_dirty_ = false;
        panel_text_min_dirty_ = true;
    }
}

void App::handle_event(const SDL_Event& event) {
    quiet_frames_ = 0;
    if (native_prefs_event_ != 0 && event.type == native_prefs_event_) {
        prefs_open_ = true;
        ImGui::SetWindowFocus("Preferences");
        return;
    }
    if (native_about_event_ != 0 && event.type == native_about_event_) {
        about_open_ = true;
        ImGui::SetWindowFocus("About Prism");
        return;
    }
    switch (event.type) {
        case SDL_EVENT_SYSTEM_THEME_CHANGED:
            style_dirty_ = true;  // "System" follows the OS live
            break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            // Moved to another monitor, or its scale changed. The pixel density is handled by
            // ImGui's framebuffer scale on its own (fonts are rasterised for it each frame).
            display_scale_ = content_scale(window_);
            apply_minimum_size();
            style_dirty_ = true;
            break;
        case SDL_EVENT_DROP_BEGIN:
            drag_over_ = true;
            dropped_.clear();
            break;
        case SDL_EVENT_DROP_POSITION:
            drag_over_ = true;
            break;
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data != nullptr) {
                dropped_.push_back(path_from_utf8(event.drop.data));  // UTF-8 from SDL
            }
            break;
        case SDL_EVENT_DROP_COMPLETE:
            drag_over_ = false;
            take_drop();
            break;
        // No check of the window: SDL's macOS backend sends these with windowID 0. And no pinch from
        // ImGui: its backend doesn't look at them.
        case SDL_EVENT_PINCH_BEGIN:
            pinch_.begin();
            break;
        case SDL_EVENT_PINCH_UPDATE:
            pinch_factor_ *= pinch_.update(event.pinch.scale);
            break;
        case SDL_EVENT_PINCH_END:
            break;
        case SDL_EVENT_MOUSE_MOTION:
            // No motion events arrive during a system drag: the drag left without dropping.
            drag_over_ = false;
            break;
        default:
            break;
    }
}

// A drop of photos and recipes (see classify_drop): a photo opens as before, and a recipe on an
// open photo is applied, as an edit (no unsaved-edits question). Nothing happens while the close
// question, the save sheet, a file dialog or an export is in progress.
void App::take_drop() {
    std::vector<fs::path> paths = std::move(dropped_);
    dropped_.clear();
    gui::DropContext context;
    context.has_photo = session_.has_file();
    context.busy = close_pending_.has_value() || recipe_sheet_.has_value() ||
                   export_sheet_.has_value() || dialog_open_ ||
                   exporter_.busy();
    const gui::DropAction action = gui::classify_drop(paths, context);
    switch (action.kind) {
        case gui::DropAction::Kind::ignore:
            return;
        case gui::DropAction::Kind::open_photo:
            notice_ = action.notice;
            request_open(action.path);
            return;
        case gui::DropAction::Kind::apply_recipe:
            notice_ = action.notice;
            load_recipe_file(action.path, false);
            return;
        case gui::DropAction::Kind::notice_only:
            notice_ = action.notice;
            return;
    }
}

bool App::animating() const {
    // Real pixels: keep drawing while the chip waits for the full-size photo, while a requested
    // frame is on its way, and until a wheel gesture has ended (the region is then asked for with
    // its margin).
    const bool detail_busy =
        detail_plan_.chip == gui::PreviewChip::reading || detail_awaited_ != 0 ||
        gui::wheel_gesture_live(static_cast<double>(SDL_GetTicks()), last_wheel_ms_);
    return quiet_frames_ < settle_frames || preview_.rendering() || pending_open_.has_value() ||
           exporter_.busy() || close_pending_.has_value() || detail_busy;
}

void App::frame() {
    ++quiet_frames_;
    take_dialog_results();
    // The "Opening…" frame was presented: now decode (this frame shows the result).
    if (pending_open_ && pending_open_shown_) {
        const fs::path path = std::move(*pending_open_);
        pending_open_.reset();
        open_now(path);
    }
    take_export_result();
    if (session_.poll_detail_base()) {
        quiet_frames_ = 0;  // the full-size photo arrived (or failed): draw with it
    }
    explain_hover_.clear();
    explain_focus_.clear();
    handle_shortcuts();
    update_preview();
    if (panel_text_min_dirty_) {
        // The fonts and the style are in place now, so the texts can be measured: the panel is
        // never narrower than they need, a width saved before is widened to it, and the window
        // leaves room for it.
        panel_text_min_ = light_path_min_width();
        panel_text_min_dirty_ = false;
        const int widened = gui::widen_panel_width(prefs_.panel_width, ui_scale(), panel_text_min_);
        if (widened != prefs_.panel_width) {
            prefs_.panel_width = widened;
            save_preferences();
        }
        apply_minimum_size();
    }

    // One window covering the whole SDL window, without a title bar: the app's main view.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    // The regions (menu bar, canvas, panel, status bar) meet edge to edge, so the window has no
    // padding of its own; the menu bar's left inset is the item spacing at Begin.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(token_metrics().panel_pad.x, ImGui::GetStyle().ItemSpacing.y));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::Begin("Prism", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    draw_menu();

    const float status_height = std::round(token_metrics().status_height);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float body_height = std::max(1.0f, avail.y - status_height);
    // The splitter between canvas and panel: as wide as the usual gap between items, drawn in the
    // canvas colour, so the panel's edge (with its hair line) is where the panel begins.
    const float splitter = ImGui::GetStyle().ItemSpacing.x;
    const float panel = gui::panel_width_pixels(static_cast<float>(prefs_.panel_width), ui_scale(),
                                                avail.x, panel_text_min_);
    const float canvas = std::max(1.0f, avail.x - panel - splitter);
    // The panel's surface and the hair line on its left, under the panel's and the Explain area's
    // own (transparent) windows.
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const gui::TokenColours& c = token_colours();
        const auto u32 = [](const gui::Rgba& rgba) {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(rgba.r, rgba.g, rgba.b, rgba.a));
        };
        const float left = origin.x + canvas + splitter;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, origin.y),
                                                  ImVec2(origin.x + avail.x, origin.y + body_height),
                                                  u32(c.panel));
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(left, origin.y), ImVec2(left + hair(), origin.y + body_height), u32(c.hair));
    }

    // The canvas column: the test strip's bar (when on) above the canvas.
    ImGui::BeginGroup();
    float bar_height = 0;
    if (strip_) {
        const float top = ImGui::GetCursorPosY();
        draw_strip_bar(canvas);
        bar_height = ImGui::GetCursorPosY() - top;
    } else if (framing_.on()) {
        const float top = ImGui::GetCursorPosY();
        draw_framing_bar(canvas);
        bar_height = ImGui::GetCursorPosY() - top;
    }
    // NoScrollWithMouse too: the picture's hit areas must never make the canvas itself scroll.
    ImGui::BeginChild("canvas", ImVec2(canvas, std::max(1.0f, body_height - bar_height)),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    draw_canvas();
    draw_banner();
    draw_stage_badge();
    draw_decode_chip();
    draw_brush_chip();
    draw_zoom_chip();
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 0.0f);
    draw_splitter(canvas, splitter, body_height, avail.x);
    ImGui::SameLine(0.0f, 0.0f);
    draw_panel_column(body_height);
    ImGui::SetCursorScreenPos(
        ImVec2(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - status_height));

    draw_status_bar(status_height);
    draw_saving_popup();
    draw_close_prompt();
    draw_recipe_sheet();
    draw_export_popup();
    ImGui::End();
    focused_window_this_frame_ = nullptr;
    draw_preferences();
    draw_about();
    draw_notices();
    draw_recipe_error();
    draw_look_recipe();

    if (pending_open_) {
        pending_open_shown_ = true;
    }
    // For Esc next frame: ImGui will have used the key on these before the app sees it.
    // Hovered, else focused, else what was shown a moment ago: a gap between controls keeps
    // showing the control the cursor just left. A control changed or dragged also sets
    // explain_last_ (see explain()), so it stays when the cursor leaves it mid-drag.
    explain_shown_ = std::string(gui::explain_to_show(explain_hover_, explain_focus_, explain_last_));
    explain_last_ = explain_shown_;
    popup_open_last_frame_ =
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    item_active_last_frame_ = control_active();
    focused_window_last_frame_ = focused_window_this_frame_;
}

void App::step_interface_size(int direction) {
    gui::Preferences p = prefs_;
    p.interface_size = direction == 0 ? gui::interface_size_auto
                                      : gui::step_interface_size(p.interface_size, direction,
                                                                 display_scale_);
    set_preferences(std::move(p));
}

gui::OpenSheets App::open_sheets() const {
    gui::OpenSheets open;
    open.recipe_sheet = recipe_sheet_.has_value();
    open.export_sheet = export_sheet_.has_value();
    open.close_prompt = close_pending_ && close_phase_ == ClosePhase::asking;
    open.saving = exporter_.busy();  // the "Saving" box shows exactly while it does
    return open;
}

void App::handle_shortcuts() {
    // Hold M: the stroke mask, for as long as the key is down (not a shortcut, which fires once).
    const bool blocked = gui::shortcuts_blocked(open_sheets());
    stroke_mask_on_ = gui::stroke_mask_shown(brush_.on() && !blocked,
                                             ImGui::IsKeyDown(ImGuiKey_M),
                                             ImGui::GetIO().WantTextInput);
    // The sheets are modal, and ImGui lets global shortcuts through a modal: so while one is up
    // nothing but quitting is handled here (Esc and Enter are each sheet's own, read where it is
    // drawn). No I, T, J, zoom, undo, open or interface size behind it.
    if (blocked) {
        if (!ImGui::GetIO().ConfigMacOSXBehaviors &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        return;
    }
    // RouteGlobal: they work wherever the focus is, unless a focused widget (e.g. a text field)
    // claims the same keys.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
        show_open_dialog();
    }
    // On macOS the app menu's item takes Cmd+, itself when it is connected.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Comma, ImGuiInputFlags_RouteGlobal)) {
        prefs_open_ = true;
        ImGui::SetWindowFocus("Preferences");
    }
    // Interface size. "+" is Shift+= on most keyboards, so Cmd+= and Cmd+Shift+= both mean bigger;
    // the keypad has its own keys.
    const auto global = [](ImGuiKeyChord chord) {
        return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal);
    };
    if (global(ImGuiMod_Ctrl | ImGuiKey_Equal) ||
        global(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Equal) ||
        global(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)) {
        step_interface_size(+1);
    }
    if (global(ImGuiMod_Ctrl | ImGuiKey_Minus) || global(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract)) {
        step_interface_size(-1);
    }
    if (global(ImGuiMod_Ctrl | ImGuiKey_0) || global(ImGuiMod_Ctrl | ImGuiKey_Keypad0)) {
        step_interface_size(0);
    }
    // Quit. On macOS the app menu's Quit item takes Cmd+Q itself. Elsewhere this is the same
    // event as File > Quit and the close button, so the unexported-edits question comes first.
    if (!ImGui::GetIO().ConfigMacOSXBehaviors &&
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_E, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        begin_export();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
        show_recipe_load_dialog();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        begin_save_recipe();
    }
    // Esc backs out one level: first the comparison, then the viewed stage. Read as a plain key
    // press, not a Shortcut(): a Shortcut route loses to the panel once a slider or combo there
    // has the keyboard focus, so Esc did nothing until the picture was clicked.
    // The easel's keys (#58), through framing_key: nothing behind a sheet (handled above), in a text
    // field, during a brush stroke, or while a menu or list is open.
    gui::FramingKeyContext framing_keys;
    framing_keys.has_file = session_.has_file();
    framing_keys.framing = framing_.on();
    framing_keys.gesture = framing_.gesture() != gui::FramingGesture::none;
    framing_keys.sheets = open_sheets();
    framing_keys.text_editing = ImGui::GetIO().WantTextInput;
    framing_keys.stroking = brush_gesture_.stroking();
    framing_keys.popup_open = popup_open_last_frame_;
    const auto act_on_framing_key = [this](gui::FramingKeyAction action) {
        switch (action) {
            case gui::FramingKeyAction::begin_framing:
                begin_framing();
                break;
            case gui::FramingKeyAction::leave_framing:
                leave_framing();
                break;
            case gui::FramingKeyAction::cancel_gesture:
                (void)framing_.cancel();  // the draft goes back; no undo step
                break;
            case gui::FramingKeyAction::turn_aspect: {
                const std::optional<prism::Frame> before = session_.settings().frame;
                report(framing_.flip_aspect(session_));
                sample_.frame_changed(before, session_.settings().frame);
                break;
            }
            case gui::FramingKeyAction::none:
                break;
        }
    };
    bool escape_taken = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        // Esc during a framing drag cancels just that drag; otherwise escape_action decides.
        const gui::FramingKeyAction action = gui::framing_key(gui::FramingKey::escape, framing_keys);
        escape_taken = action != gui::FramingKeyAction::none;
        act_on_framing_key(action);
    }
    // Return as a plain key press, as Esc: a Shortcut route would lose it to a focused control.
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        act_on_framing_key(gui::framing_key(gui::FramingKey::return_key, framing_keys));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escape_taken) {
        gui::EscapeContext esc;
        esc.framing = framing_.on();
        esc.brushing = brush_.on();
        esc.picking = pick_.on();
        esc.sampling = sample_.on();
        esc.test_strip = strip_.has_value();
        esc.comparing = gui::effective_compare(viewed_stage_, compare_stage_).has_value();
        esc.viewing_earlier = viewed_stage_ != gui::final_stage;
        esc.text_editing = ImGui::GetIO().WantTextInput;  // as of the last frame
        esc.popup_open = popup_open_last_frame_;
        esc.item_active = item_active_last_frame_;
        esc.window_focused = focused_window_last_frame_ != nullptr;
        switch (gui::escape_action(esc)) {
            case gui::EscapeAction::close_window:
                *focused_window_last_frame_ = false;  // the window's own open flag
                break;
            case gui::EscapeAction::leave_brush:
                apply_brush(brush_gesture_.escape());  // a stroke under way goes, else Brush mode
                break;
            case gui::EscapeAction::leave_pick:
                leave_pick();
                break;
            case gui::EscapeAction::clear_sample:
                clear_sample();
                break;
            case gui::EscapeAction::clear_test_strip:
                strip_.reset();
                break;
            case gui::EscapeAction::clear_compare:
                compare_stage_.reset();
                break;
            case gui::EscapeAction::show_final:
                viewed_stage_ = gui::final_stage;
                compare_stage_.reset();  // one that equalled the viewed stage goes too
                break;
            case gui::EscapeAction::leave_framing:
                leave_framing();  // keeps the frame
                break;
            case gui::EscapeAction::none:
                break;
        }
    }
    // Not in the middle of a drag: History would end the drag's step, but the slider would keep
    // sending values that each became a step of their own.
    if (control_active() || brush_gesture_.stroking() ||
        framing_.gesture() != gui::FramingGesture::none) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        undo();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)) {
        redo();
    }
    if (ImGui::Shortcut(ImGuiKey_J, ImGuiInputFlags_RouteGlobal)) {
        toggle_clip_overlay();  // J: "jumping" highlights; free of every other shortcut
    }
    if (ImGui::Shortcut(ImGuiKey_T, ImGuiInputFlags_RouteGlobal)) {
        toggle_test_strip();  // T: the test strip, routed like J so typing never triggers it
    }
    if (ImGui::Shortcut(ImGuiKey_I, ImGuiInputFlags_RouteGlobal)) {
        toggle_sample();  // I: the sampler, routed like J and T
    }
    if (ImGui::Shortcut(ImGuiKey_W, ImGuiInputFlags_RouteGlobal)) {
        toggle_pick();  // W: the white balance picker, routed like the others
    }
    if (ImGui::Shortcut(ImGuiKey_B, ImGuiInputFlags_RouteGlobal)) {
        toggle_brush();  // B: Dodge & burn's brush, routed like the others
    }
    if (ImGui::Shortcut(ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) {
        act_on_framing_key(gui::framing_key(gui::FramingKey::c, framing_keys));  // C: the easel
    }
    if (framing_.on() && global(ImGuiKey_X)) {
        act_on_framing_key(gui::framing_key(gui::FramingKey::x, framing_keys));  // X: turn the shape
    }
    // The brush's own keys, only in Brush mode (brush_key does nothing otherwise).
    if (brush_.on()) {
        if (global(ImGuiKey_X)) {
            (void)gui::brush_key(brush_, gui::BrushKey::x, false);
        }
        if (global(ImGuiKey_LeftBracket)) {
            (void)gui::brush_key(brush_, gui::BrushKey::left_bracket, false);
        }
        if (global(ImGuiKey_RightBracket)) {
            (void)gui::brush_key(brush_, gui::BrushKey::right_bracket, false);
        }
        if (global(ImGuiMod_Shift | ImGuiKey_LeftBracket)) {
            (void)gui::brush_key(brush_, gui::BrushKey::left_bracket, true);
        }
        if (global(ImGuiMod_Shift | ImGuiKey_RightBracket)) {
            (void)gui::brush_key(brush_, gui::BrushKey::right_bracket, true);
        }
    }
    // Zoom: plain keys, routed like J and T. Cmd/Ctrl with + - 0 is the interface size, above.
    if (global(ImGuiKey_F)) {
        view_fit();
    }
    if (global(ImGuiKey_1)) {
        view_actual_size();
    }
    if (global(ImGuiKey_Equal) || global(ImGuiMod_Shift | ImGuiKey_Equal) ||
        global(ImGuiKey_KeypadAdd)) {
        view_step(+1);
    }
    if (global(ImGuiKey_Minus) || global(ImGuiKey_KeypadSubtract)) {
        view_step(-1);
    }
}

void App::draw_menu() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open…", shortcut_label("O").c_str(), false, !dialog_open_)) {
            show_open_dialog();
        }
        if (ImGui::MenuItem("Export…", shortcut_label("E").c_str(), false,
                            session_.has_file() && !dialog_open_ && !exporter_.busy())) {
            begin_export();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Load Recipe…", shortcut_label("Shift+O").c_str(), false,
                            recipes_available())) {
            show_recipe_load_dialog();
        }
        if (ImGui::MenuItem("Save Recipe…", shortcut_label("Shift+S").c_str(), false,
                            recipes_available())) {
            begin_save_recipe();
        }
        ImGui::Separator();
        // On macOS, Cmd+Q belongs to the app menu's own Quit item.
        const std::string quit_keys =
            ImGui::GetIO().ConfigMacOSXBehaviors ? std::string() : shortcut_label("Q");
        if (ImGui::MenuItem("Quit", quit_keys.empty() ? nullptr : quit_keys.c_str())) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", shortcut_label("Z").c_str(), false,
                            session_.history().can_undo())) {
            undo();
        }
        if (ImGui::MenuItem("Redo", shortcut_label("Shift+Z").c_str(), false,
                            session_.history().can_redo())) {
            redo();
        }
        if (native_prefs_event_ == 0) {  // otherwise it's in the macOS app menu
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences…", shortcut_label(",").c_str())) {
                prefs_open_ = true;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        const int size = prefs_.interface_size;
        if (ImGui::MenuItem("Bigger Interface", shortcut_label("+").c_str(), false,
                            gui::step_interface_size(size, +1, display_scale_) != size)) {
            step_interface_size(+1);
        }
        if (ImGui::MenuItem("Smaller Interface", shortcut_label("-").c_str(), false,
                            gui::step_interface_size(size, -1, display_scale_) != size)) {
            step_interface_size(-1);
        }
        if (ImGui::MenuItem("Interface Size: Auto", shortcut_label("0").c_str(), false,
                            size != gui::interface_size_auto)) {
            step_interface_size(0);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Test Strip", "T", strip_.has_value(),
                            session_.has_file() &&
                                (strip_.has_value() || gui::strip_available(viewed_stage_)))) {
            toggle_test_strip();
        }
        if (session_.has_file() && !strip_ && !gui::strip_available(viewed_stage_) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", gui::strip_unavailable_hint());
        }
        if (ImGui::MenuItem("Sample Colour", "I", sample_.on(),
                            session_.has_file() || sample_.on())) {
            toggle_sample();
        }
        const bool can_pick = gui::pick_available(viewed_stage_, compare_stage_);
        if (ImGui::MenuItem("Pick Neutral", "W", pick_.on(),
                            pick_.on() || (session_.has_file() && can_pick))) {
            toggle_pick();
        }
        if (session_.has_file() && !pick_.on() && !can_pick &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", gui::view_hint(viewed_stage_, compare_stage_).c_str());
        }
        const gui::BrushMenuItem brush_item =
            gui::brush_menu_item(brush_.on(), session_.has_file(), viewed_stage_);
        if (ImGui::MenuItem("Brush", "B", brush_item.checked, brush_item.enabled)) {
            toggle_brush();
        }
        if (brush_item.shows_hint &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", gui::brush_unavailable_hint);
        }
        if (ImGui::MenuItem("Frame", "C", framing_.on(), session_.has_file() || framing_.on())) {
            toggle_framing();  // the easel (#58)
        }
        ImGui::Separator();
        const bool can_zoom = view_available();
        const float zoom = can_zoom ? gui::view_zoom(view_, view_geometry_) : 1.0f;
        const float fit = can_zoom ? gui::view_fit_zoom(view_geometry_) : 1.0f;
        if (ImGui::MenuItem(
                "Zoom In", "=", false,
                can_zoom &&
                    !gui::view_same_zoom(
                        gui::view_step_zoom(zoom, fit, +1, view_geometry_.file_scale), zoom))) {
            view_step(+1);
        }
        if (ImGui::MenuItem("Zoom Out", "-", false, can_zoom && !view_.fit)) {
            view_step(-1);
        }
        if (ImGui::MenuItem("Zoom to Fit", "F", false, can_zoom && !view_.fit)) {
            view_fit();
        }
        // Greyed out where it can't go: a photo so small that even its fit is above 100%.
        if (ImGui::MenuItem(std::string(gui::real_pixels_menu_item()).c_str(), "1", false,
                            can_zoom && gui::view_actual_size_available(view_geometry_) &&
                                !gui::view_at_actual_size(view_, view_geometry_))) {
            view_actual_size();
        }
        ImGui::EndMenu();
    }
    if (native_about_event_ == 0) {  // otherwise it's in the macOS app menu
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About Prism")) {
                about_open_ = true;
            }
            ImGui::EndMenu();
        }
    }
    ImGui::EndMenuBar();
    // The hair line under the menu bar.
    const ImRect bar = ImGui::GetCurrentWindow()->MenuBarRect();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(bar.Min.x, bar.Max.y - hair()), ImVec2(bar.Max.x, bar.Max.y),
        ImGui::ColorConvertFloat4ToU32(ImVec4(token_colours().hair.r, token_colours().hair.g,
                                              token_colours().hair.b, token_colours().hair.a)));
}

void App::draw_canvas() {
    canvas_taken_.clear();
    canvas_chips_.clear();
    sample_hover_.reset();
    detail_plan_.draw = false;  // until this frame's canvas says otherwise
    update_hand_key();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const Palette& colours = palette(theme());
    draw->AddRectFilled(min, max, colours.canvas);  // neutral grey in both themes
    const float margin = token_metrics().canvas_margin;
    if (texture_.texture() == nullptr) {
        canvas_wheels_.clear();  // nothing to scroll or pinch
        pinch_factor_ = 1.0f;
    }

    if (framing_.on() && session_.has_file()) {
        // The easel (#58): the whole photo turned under the blades. No render runs for a drag.
        update_clip_overlay_texture();
        draw_framing_canvas(draw, min, size);
        update_framing_pointer();
        const std::string_view fit_note = gui::framing_fit_note(framing_.fitted());
        if (!fit_note.empty()) {
            const ImVec2 fit_at(min.x + margin, max.y - margin - 4 * ImGui::GetFontSize());
            overlay_text(draw, colours, fit_at, std::string(fit_note));
            canvas_taken_.push_back(overlay_rect(fit_at, std::string(fit_note)));
        }
    } else if (texture_.texture() != nullptr && render_drawable()) {
        // Where the view puts the picture this frame: letterboxed at fit, and clipped to the
        // canvas (the child window's clip rectangle) when zoomed in past it.
        view_geometry_ = {min.x,
                          min.y,
                          size.x,
                          size.y,
                          ImGui::GetIO().DisplayFramebufferScale.x,
                          texture_.width(),
                          texture_.height()};
        // Zoom is measured in file pixels (#37): 100% is one pixel of the framed picture at its
        // full size on one device pixel.
        set_view_file_scale(static_cast<float>(
            gui::file_scale(framed_full_size(), {texture_.width(), texture_.height()}).shown));
        apply_canvas_input();
        view_ = gui::view_settled(view_, view_geometry_);
        update_detail();
        const gui::ViewPlacement placed = gui::view_placement(view_, view_geometry_);
        const ImVec2 at(placed.at.x, placed.at.y);
        const ImVec2 shown(placed.shown.x, placed.shown.y);
        update_clip_overlay_texture();
        update_stroke_overlay_texture();
        if (splitting()) {
            draw_split(draw, at, shown);
        } else {
            begin_picture_sampling(draw);
            draw->AddImage(ImTextureRef(static_cast<ImTextureID>(
                               reinterpret_cast<std::uintptr_t>(texture_.texture()))),
                           at, ImVec2(at.x + shown.x, at.y + shown.y));
            if (detail_plan_.draw && detail_texture_.texture() != nullptr) {
                // Real pixels over the enlarged preview, on their own region's place; the preview
                // shows around them. Sampled by the rule for file pixels, then back to the
                // preview's rule for the overlays.
                const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
                draw->AddCallback(detail_plan_.nearest ? platform.DrawCallback_SetSamplerNearest
                                                       : platform.DrawCallback_SetSamplerLinear,
                                  nullptr);
                draw->AddImage(ImTextureRef(static_cast<ImTextureID>(
                                   reinterpret_cast<std::uintptr_t>(detail_texture_.texture()))),
                               ImVec2(detail_plan_.where.min.x, detail_plan_.where.min.y),
                               ImVec2(detail_plan_.where.max.x, detail_plan_.where.max.y));
                draw->AddCallback(gui::view_shows_pixels(gui::view_zoom(view_, view_geometry_))
                                      ? platform.DrawCallback_SetSamplerNearest
                                      : platform.DrawCallback_SetSamplerLinear,
                                  nullptr);
            }
            draw_picture_overlays(draw, at, ImVec2(at.x + shown.x, at.y + shown.y),
                                  ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
            end_picture_sampling(draw);
            if (strip_ && !strip_bands_.empty()) {
                draw_strip_bands(draw, at, shown);
            }
        }
        update_hand_drag();
        update_sample_cursor();
        draw_sample_markers(draw);
        update_pick_cursor();
        draw_pick_overlay(draw);
        draw_brush_ring(draw);
    } else if (!session_.has_file() && !pending_open_) {
        // Empty state: a dashed drop area with the one thing to do. It lights up while a file is
        // dragged over the window.
        const float inset = token_metrics().drop_inset;
        dashed_rect(draw, ImVec2(min.x + inset, min.y + inset),
                    ImVec2(max.x - inset, max.y - inset),
                    drag_over_ ? colours.drop_highlight : colours.drop_outline,
                    token_metrics().drop_dash, token_metrics().drop_line);
        ImGui::SetCursorPosY(size.y / 2 - ImGui::GetFrameHeightWithSpacing() * 1.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, colours.canvas_text);
        centred_text("Drop a photo here");
        ImGui::PopStyleColor();
        const float button = ImGui::CalcTextSize("Open photo…").x +
                             ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetCursorPosX((size.x - button) / 2);
        if (ImGui::Button("Open photo…")) {
            show_open_dialog();
        }
        ImGui::TextUnformatted("");
        ImGui::PushStyleColor(ImGuiCol_Text, colours.canvas_text);
        centred_text(heic_support_available()
                         ? (raw_support_available() ? "PNG, JPEG, HEIC or RAW"
                                                    : "PNG, JPEG or HEIC")
                         : (raw_support_available() ? "PNG, JPEG or RAW" : "PNG or JPEG"));
        ImGui::PopStyleColor();
    }

    const ImVec2 note_at(min.x + margin, max.y - margin - 2 * ImGui::GetFontSize());
    const std::string opening = pending_open_ ? "Opening " + file_name(*pending_open_) + "…" : "";
    const std::string rendering = "Rendering…";
    if (pending_open_) {
        overlay_text(draw, colours, note_at, opening);
    } else if (preview_.rendering()) {
        overlay_text(draw, colours, note_at, rendering);
    }
    // Kept clear even between renders, so the zoom's chip doesn't jump each time the note shows.
    canvas_taken_.push_back(overlay_rect(note_at, pending_open_ ? opening : rendering));
    if (drag_over_ && session_.has_file()) {
        draw->AddRect(min, max, colours.drop_highlight, 0.0f, 0,
                  token_metrics().drop_line_active);
    }
    update_sample_traces();
}

bool App::view_available() const {
    return session_.has_file() && texture_.texture() != nullptr;
}

void App::view_fit() {
    view_ = {};
}

void App::view_actual_size() {
    if (view_available()) {
        view_ = gui::view_zoomed_to(view_, view_geometry_, gui::view_actual_zoom(view_geometry_));
    }
}

void App::view_step(int direction) {
    if (view_available()) {
        const float next = gui::view_step_zoom(gui::view_zoom(view_, view_geometry_),
                                               gui::view_fit_zoom(view_geometry_), direction,
                                               view_geometry_.file_scale);
        view_ = gui::view_zoomed_to(view_, view_geometry_, next);
    }
}

void App::canvas_wheel(gui::WheelDelta wheel, bool trackpad, std::uint64_t now_ms) {
    canvas_wheels_.push_back({wheel, trackpad, now_ms});
}

void App::apply_canvas_input() {
    const ImGuiIO& io = ImGui::GetIO();
    // Over the canvas or a chip on it; a popup or a modal window above it hides it from this.
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const gui::ViewPoint cursor{io.MousePos.x, io.MousePos.y};
    const float pan_unit = gui::wheel_pan_unit(pointer_platform_) * display_scale_;
    for (const CanvasWheelEvent& e : canvas_wheels_) {
        if (!scroll_latch_.on_canvas(hovered, e.now_ms)) {
            continue;
        }
        last_wheel_ms_ = static_cast<double>(e.now_ms);  // a gesture until it has rested a moment
        // On a Mac ImGui's Ctrl is the Cmd key.
        const gui::CanvasScroll scroll = gui::canvas_scroll(e.wheel, e.trackpad, io.KeyCtrl, pan_unit);
        if (scroll.kind == gui::CanvasScrollKind::pan) {
            view_ = gui::view_panned_by(view_, view_geometry_, scroll.dx, scroll.dy);
        } else if (scroll.kind == gui::CanvasScrollKind::zoom) {
            view_ = gui::view_zoomed_to(view_, view_geometry_,
                                        gui::view_zoom(view_, view_geometry_) * scroll.factor, cursor);
        }
    }
    canvas_wheels_.clear();
    // SDL gives a pinch no position, so it goes about the cursor.
    if (pinch_factor_ != 1.0f && hovered) {
        last_wheel_ms_ = static_cast<double>(SDL_GetTicks());
        view_ = gui::view_zoomed_to(view_, view_geometry_,
                                    gui::view_zoom(view_, view_geometry_) * pinch_factor_, cursor);
    }
    pinch_this_frame_ = pinch_factor_ != 1.0f && hovered;
    pinch_factor_ = 1.0f;
}

// Dear ImGui's SDL_Renderer backend forces its own sampler on every draw, linear unless told, so the
// texture's own scale mode would do nothing here.
void App::begin_picture_sampling(ImDrawList* draw) const {
    if (gui::view_shows_pixels(gui::view_zoom(view_, view_geometry_))) {
        draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
    }
}

void App::end_picture_sampling(ImDrawList* draw) const {
    if (gui::view_shows_pixels(gui::view_zoom(view_, view_geometry_))) {
        draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
    }
}

bool App::control_active() const {
    return control_is_active(canvas_press_);
}

void App::update_hand_key() {
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        canvas_press_ = false;
        hand_drag_.release();
    }
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const bool claimed = !gui::shortcuts_blocked(open_sheets()) &&
                         gui::canvas_claims_space(hovered, io.WantTextInput, control_active());
    if (claimed) {
        // Every frame, not only once it is down: keyboard navigation reads it before this runs.
        ImGui::SetKeyOwner(ImGuiKey_Space, ImGui::GetID("##canvas_hand"));
    }
    // Without an owner to ask for: a press on the canvas hides the keys from all but the press itself.
    hand_key_held_ = claimed && ImGui::IsKeyDown(ImGuiKey_Space);
}

void App::update_hand_drag() {
    const ImGuiIO& io = ImGui::GetIO();
    const gui::ViewPoint cursor{io.MousePos.x, io.MousePos.y};
    const bool on_canvas = ImGui::IsWindowHovered();  // the canvas itself, not a chip on it
    const bool over_control = ImGui::IsAnyItemHovered();
    if (on_canvas && !over_control && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        canvas_press_ = true;
        if (gui::sample_press_can_pin(sample_.on(), strip_.has_value(), over_control,
                                      hand_key_held_)) {
            // Its own record of the press: at fit, where a drag moves nothing, the hand isn't pressed.
            sample_click_.press(cursor, io.MouseDragThreshold);
        }
        if (gui::pick_press_can_pick(pick_.on(), gui::pick_available(viewed_stage_, compare_stage_),
                                     strip_.has_value(), over_control, hand_key_held_)) {
            pick_click_.press(cursor, io.MouseDragThreshold);  // as the sampler's, for the same reason
        }
        update_brush_press(true, over_control);
        if (hand_key_held_) {
            hand_drag_.press(cursor, 0.0f);
        } else if (gui::plain_drag_pans(view_.fit, strip_.has_value(), over_control, brush_.on())) {
            hand_drag_.press(cursor, io.MouseDragThreshold);
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            gui::double_click_zooms(strip_.has_value(), over_control, hand_key_held_,
                                    brush_.on())) {
            view_ = gui::view_toggled(view_, view_geometry_, cursor);
            hand_drag_.release();
            sample_click_.cancel();
            pick_click_.cancel();
        }
    } else {
        sample_click_.move(cursor);
        pick_click_.move(cursor);
        update_brush_press(false, over_control);
        if (const std::optional<gui::ViewPoint> moved = hand_drag_.move(cursor)) {
            view_ = gui::view_panned_by(view_, view_geometry_, moved->x, moved->y);
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && sample_click_.release(cursor)) {
        const std::shared_ptr<const Image> base = session_.preview_base();
        const std::optional<gui::ViewPixel> pixel = gui::view_pixel_at(view_, view_geometry_, cursor);
        if (base && pixel && base->width == texture_.width() && base->height == texture_.height()) {
            sample_.place({pixel->x, pixel->y}, base->width, base->height,
                          session_.settings().frame);
            // The photo pixel clicked, for reading it where real pixels are on the canvas.
            sample_pin_file_size_ = framed_full_size();
            sample_pin_file_ =
                gui::file_pixel_at(view_, view_geometry_, sample_pin_file_size_, cursor);
            sample_pin_file_for_ = sample_.pin();
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && pick_click_.release(cursor)) {
        pick_at(cursor);
    }
    if (hand_drag_.dragging() || (hand_key_held_ && !view_.fit)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
}

void App::toggle_framing() {
    if (framing_.on()) {
        leave_framing();
    } else {
        begin_framing();
    }
}

void App::begin_framing() {
    if (framing_.on() || !session_.has_file() || !framing_.begin(session_)) {
        return;
    }
    // Every canvas tool off and no comparison (#58): what they show is the framed picture.
    const gui::FramingEntry entry =
        gui::framing_entry({sample_.on(), pick_.on(), strip_.has_value(), brush_.on()},
                           compare_stage_);
    if (!entry.tools.sampling) {
        clear_sample();
    }
    if (!entry.tools.picking) {
        leave_pick();
    }
    if (!entry.tools.strip) {
        strip_.reset();
    }
    if (!entry.tools.brushing) {
        leave_brush();
    }
    compare_stage_ = entry.compare;
    hand_drag_.release();
    view_ = {};  // the whole turned photo, fitted
}

void App::leave_framing() {
    if (!framing_.on()) {
        return;
    }
    const std::optional<prism::Frame> before = session_.settings().frame;
    report(framing_.leave(session_));  // a drag under way is committed: one undo step
    sample_.frame_changed(before, session_.settings().frame);
    hand_drag_.release();
    view_ = {};  // the framed picture, fitted
}

void App::draw_framing_canvas(ImDrawList* draw, ImVec2 min, ImVec2 size) {
    const prism::PixelSize photo = framing_.photo();
    const prism::Frame& draft = framing_.draft();
    gui::ViewGeometry canvas{
        min.x,       min.y,       size.x, size.y, ImGui::GetIO().DisplayFramebufferScale.x,
        photo.width, photo.height};
    // The zoom in file pixels, as on the normal canvas: the whole photo at the export's size over
    // the whole photo framing shows. Framing itself always shows the preview (#58).
    canvas.file_scale = static_cast<float>(
        gui::file_scale(session_.export_start_size(), {photo.width, photo.height}).shown);
    view_geometry_ = gui::framing_geometry(canvas, draft.angle, photo.width, photo.height,
                                           token_metrics().canvas_margin);
    set_view_file_scale(canvas.file_scale);
    if (framing_.gesture() == gui::FramingGesture::none) {
        apply_canvas_input();
    } else {
        // A drag maps the pointer through the view at its press: the view holds still until it ends.
        canvas_wheels_.clear();
        pinch_factor_ = 1.0f;
        pinch_this_frame_ = false;
    }
    view_ = gui::view_settled(view_, view_geometry_);
    update_detail();  // framing shows the preview: this clears any real pixels
    framing_view_ =
        gui::framing_view_placed(draft.angle, photo.width, photo.height, view_, view_geometry_);

    // The whole photo's render, once it has arrived (until then the canvas stays empty).
    const bool picture = texture_.texture() != nullptr && render_drawable();
    const std::array<gui::CanvasPoint, 4> corners = gui::photo_corners(framing_view_);
    const auto at = [](gui::CanvasPoint p) {
        return ImVec2(static_cast<float>(p.x), static_cast<float>(p.y));
    };
    const auto id = [](SDL_Texture* t) {
        return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(t)));
    };
    if (picture) {
        begin_picture_sampling(draw);
        draw->AddImageQuad(id(texture_.texture()), at(corners[0]), at(corners[1]), at(corners[2]),
                           at(corners[3]));
        // J's marks lie on the same quad (#58).
        if (texture_.overlay() != nullptr && prefs_.clip_overlay &&
            clip_mask_width_ == texture_.width() && clip_mask_height_ == texture_.height()) {
            draw->AddImageQuad(id(texture_.overlay()), at(corners[0]), at(corners[1]),
                               at(corners[2]), at(corners[3]));
        }
        end_picture_sampling(draw);
    }

    // The veil: the canvas's grey at 60% over everything outside the frame, so what is cut away is
    // still seen, quietly.
    const gui::CanvasRect rect = gui::frame_rect(framing_view_, draft);
    const ImVec2 cmax(min.x + size.x, min.y + size.y);
    // Zoomed in, the frame can reach past the canvas: the veil's pieces stop at its edges.
    const auto inside = [&](gui::CanvasPoint p) {
        return ImVec2(std::clamp(static_cast<float>(p.x), min.x, cmax.x),
                      std::clamp(static_cast<float>(p.y), min.y, cmax.y));
    };
    const ImVec2 fmin = inside(rect.min);
    const ImVec2 fmax = inside(rect.max);
    ImU32 veil = palette(theme()).canvas;
    veil = (veil & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::lround(255 * 0.6)) << IM_COL32_A_SHIFT);
    draw->AddRectFilled(min, ImVec2(cmax.x, fmin.y), veil);
    draw->AddRectFilled(ImVec2(min.x, fmax.y), cmax, veil);
    draw->AddRectFilled(ImVec2(min.x, fmin.y), ImVec2(fmin.x, fmax.y), veil);
    draw->AddRectFilled(ImVec2(fmax.x, fmin.y), ImVec2(cmax.x, fmax.y), veil);

    // The guides while a gesture is under way, then the blades and the eight handles: a light line
    // on a dark one, so they show on any picture.
    const float scale = ui_scale();
    const ImU32 dark = IM_COL32(0, 0, 0, 150);
    const ImU32 light = IM_COL32(255, 255, 255, 230);
    const ImU32 guide = IM_COL32(255, 255, 255, 110);
    for (const gui::CanvasLine& line :
         gui::framing_guide_lines(rect, gui::framing_guides(framing_.gesture()))) {
        draw->AddLine(at(line.from), at(line.to), guide, 1.0f * scale);
    }
    const ImVec2 bmin = at(rect.min);  // the blades where they are, on the canvas or not
    const ImVec2 bmax = at(rect.max);
    draw->AddRect(bmin, bmax, dark, 0.0f, 0, 3.0f * scale);
    draw->AddRect(bmin, bmax, light, 0.0f, 0, 1.0f * scale);
    const float handle = std::round(mock(4.0f));
    const ImVec2 mid((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
    for (const ImVec2 h : {bmin, ImVec2(mid.x, bmin.y), ImVec2(bmax.x, bmin.y), ImVec2(bmax.x, mid.y),
                           bmax, ImVec2(mid.x, bmax.y), ImVec2(bmin.x, bmax.y), ImVec2(bmin.x, mid.y)}) {
        draw->AddRectFilled(ImVec2(h.x - handle - 1, h.y - handle - 1),
                            ImVec2(h.x + handle + 1, h.y + handle + 1), dark);
        draw->AddRectFilled(ImVec2(h.x - handle, h.y - handle), ImVec2(h.x + handle, h.y + handle),
                            light);
    }
    // The horizon line being drawn (Option/Alt-drag).
    if (const std::optional<gui::CanvasLine> horizon = framing_.horizon()) {
        draw->AddLine(at(horizon->from), at(horizon->to), dark, 3.0f * scale);
        draw->AddLine(at(horizon->from), at(horizon->to), light, 1.25f * scale);
    }
}

void App::update_framing_pointer() {
    const ImGuiIO& io = ImGui::GetIO();
    const gui::ViewPoint cursor{io.MousePos.x, io.MousePos.y};
    const gui::CanvasPoint point{cursor.x, cursor.y};
    const bool on_canvas = ImGui::IsWindowHovered();  // the canvas itself, not a chip on it
    const bool over_control = ImGui::IsAnyItemHovered();
    const double reach = mock(10.0f);
    const auto pointer_gesture = [this] {
        const gui::FramingGesture g = framing_.gesture();
        return g == gui::FramingGesture::handle || g == gui::FramingGesture::move ||
               g == gui::FramingGesture::horizon;
    };
    if (on_canvas && !over_control && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        canvas_press_ = true;
        if (hand_key_held_) {
            hand_drag_.press(cursor, 0.0f);  // Space: the hand, as ever
        } else if (!framing_.press(point, io.KeyAlt, framing_view_, reach) && !view_.fit) {
            hand_drag_.press(cursor, io.MouseDragThreshold);  // outside the frame: pan when zoomed
        }
    } else {
        if (pointer_gesture() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            framing_.move(point);  // the draft only: nothing is rendered or built
        }
        if (const std::optional<gui::ViewPoint> moved = hand_drag_.move(cursor)) {
            view_ = gui::view_panned_by(view_, view_geometry_, moved->x, moved->y);
        }
    }
    // The release commits: one undo step, and the framed base is built once. A release missed (the
    // button let go outside the window) ends the gesture the same way.
    if (pointer_gesture() && (ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                              !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        const std::optional<prism::Frame> before = session_.settings().frame;
        report(framing_.release(point, session_));
        sample_.frame_changed(before, session_.settings().frame);
    }

    // The cursor says what a press would take hold of.
    if (hand_drag_.dragging() || (hand_key_held_ && !view_.fit)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        return;
    }
    if (!on_canvas || over_control || hand_key_held_) {
        return;
    }
    const gui::FrameHandle handle = framing_.gesture() != gui::FramingGesture::none
                                        ? framing_.handle()
                                        : gui::frame_handle_at(framing_view_, framing_.draft(),
                                                               point, reach);
    switch (handle) {
        case gui::FrameHandle::left:
        case gui::FrameHandle::right: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); break;
        case gui::FrameHandle::top:
        case gui::FrameHandle::bottom: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); break;
        case gui::FrameHandle::top_left:
        case gui::FrameHandle::bottom_right: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE); break;
        case gui::FrameHandle::top_right:
        case gui::FrameHandle::bottom_left: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW); break;
        case gui::FrameHandle::inside: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); break;
        case gui::FrameHandle::none: break;
    }
}

void App::draw_framing_bar(float width) {
    const prism::Frame& draft = framing_.draft();
    const bool exact = session_.file_size_exact();
    const prism::PixelSize file{session_.file_width(), session_.file_height()};
    FramingBarData data;
    data.aspect = draft.aspect;
    data.portrait = draft.portrait;
    data.angle = draft.angle;
    // The draft's frame on the file, as the export will measure it.
    data.live_line = gui::framing_live_line(gui::framed_export_size(draft, file), file, draft.angle,
                                            exact);
    data.hint = std::string(gui::framing_hint(ImGui::GetIO().ConfigMacOSXBehaviors));
    FramingBarCallbacks cb;
    cb.begin_turn = [this] { framing_.begin_turn(); };
    cb.turn_to = [this](float angle) { framing_.turn_to(angle); };
    cb.end_turn = [this] {
        const std::optional<prism::Frame> before = session_.settings().frame;
        report(framing_.commit(session_));
        sample_.frame_changed(before, session_.settings().frame);
    };
    cb.set_angle = [this](float angle) {
        const std::optional<prism::Frame> before = session_.settings().frame;
        report(framing_.set_angle(angle, session_));
        sample_.frame_changed(before, session_.settings().frame);
    };
    cb.set_aspect = [this](prism::Aspect aspect) {
        const std::optional<prism::Frame> before = session_.settings().frame;
        report(framing_.set_aspect(aspect, session_));
        sample_.frame_changed(before, session_.settings().frame);
    };
    cb.turn_aspect = [this] {
        const std::optional<prism::Frame> before = session_.settings().frame;
        report(framing_.flip_aspect(session_));
        sample_.frame_changed(before, session_.settings().frame);
    };
    cb.reset = [this] {
        const std::optional<prism::Frame> before = session_.settings().frame;
        report(framing_.reset(session_));
        sample_.frame_changed(before, session_.settings().frame);
    };
    cb.done = [this] { leave_framing(); };
    cb.describe = [this](std::string_view id) { describe(id); };
    cb.hover = [this](std::string_view id) { explain_hover_ = std::string(id); };
    gui_app::draw_framing_bar(data, cb, width);
}

void App::toggle_sample() {
    if (sample_.on()) {
        clear_sample();
    } else if (session_.has_file()) {
        if (gui::framing_leaves(framing_.on(), gui::FramingEvent::tool_chosen)) {
            leave_framing();
        }
        sample_.toggle();
        leave_pick();  // one tool at a time
        leave_brush();
    }
}

void App::clear_sample() {
    sample_.leave();
    sample_click_.cancel();
    sample_hover_.reset();
    sample_pin_trace_.clear();
    sample_hover_trace_.clear();
    sample_pin_photo_trace_.clear();
    sample_hover_photo_trace_.clear();
    sample_pin_file_.reset();
    sample_pin_file_for_.reset();
    sample_hover_file_.reset();
    sample_pin_photo_ = false;
    sample_hover_photo_ = false;
}

void App::toggle_pick() {
    const gui::CanvasTools before{sample_.on(), pick_.on(), strip_.has_value(), brush_.on()};
    const gui::CanvasTools after = gui::tools_after_key(
        before, gui::ToolKey::pick,
        session_.has_file() && gui::pick_available(viewed_stage_, compare_stage_), true);
    if (after == before) {
        return;
    }
    // Only a picker that really turns on leaves framing: it needs the framed picture on screen.
    if (after.picking && gui::framing_leaves(framing_.on(), gui::FramingEvent::tool_chosen)) {
        leave_framing();
    }
    if (!after.picking) {
        leave_pick();
        return;
    }
    pick_.toggle();
    pick_message_.clear();
    if (before.sampling) {
        clear_sample();
    }
    if (before.strip) {
        strip_.reset();
    }
    leave_brush();
}

void App::toggle_brush() {
    const gui::CanvasTools before{sample_.on(), pick_.on(), strip_.has_value(), brush_.on()};
    const gui::CanvasTools after = gui::tools_after_key(
        before, gui::ToolKey::brush, true, true,
        session_.has_file() && gui::brush_available(viewed_stage_));
    if (after == before) {
        return;
    }
    // Only a brush that really turns on leaves framing.
    if (after.brushing && gui::framing_leaves(framing_.on(), gui::FramingEvent::tool_chosen)) {
        leave_framing();
    }
    if (!after.brushing) {
        leave_brush();
        return;
    }
    brush_.toggle();
    if (before.sampling) {
        clear_sample();
    }
    if (before.picking) {
        leave_pick();
    }
    if (before.strip) {
        strip_.reset();
    }
}

void App::leave_brush() {
    apply_brush(brush_gesture_.finish());  // a stroke under way keeps what it painted
    brush_.leave();
}

std::optional<gui::BrushCanvas> App::brush_canvas() const {
    // As for a pick: the base's pixels are what is on screen only when the two are the same size.
    const std::shared_ptr<const Image> base = session_.preview_base();
    if (!base || base->width != texture_.width() || base->height != texture_.height() ||
        base->width <= 0 || base->height <= 0) {
        return std::nullopt;
    }
    return gui::brush_canvas(session_);
}

gui::BrushStyle App::brush_style() const {
    gui::BrushStyle style;
    style.radius = brush_.radius();
    style.softness = brush_.softness();
    style.strength = brush_.strength();
    return style;
}

void App::update_brush_press(bool pressed, bool over_control) {
    const ImGuiIO& io = ImGui::GetIO();
    const gui::ViewPoint cursor{io.MousePos.x, io.MousePos.y};
    const gui::ViewPoint at = gui::view_screen_to_image(view_, view_geometry_, cursor);
    const gui::BrushModifiers keys{io.KeyAlt, hand_key_held_};
    if (pressed) {
        const std::optional<gui::BrushCanvas> canvas = brush_canvas();
        if (canvas &&
            gui::brush_takes_press(brush_.on(), gui::brush_available(viewed_stage_), over_control,
                                   gui::view_point_in_any(cursor, canvas_chips_))) {
            stroke_canvas_ = *canvas;
            const gui::BrushStyle style = brush_style();
            // A file pixel apart while real pixels are drawn (#37), a preview pixel otherwise.
            const float spacing = min_point_spacing(
                style.radius,
                gui::brush_spacing_long_side(*canvas, detail_plan_.draw, framed_full_size()));
            apply_brush(brush_gesture_.press(at, keys, brush_.tone(), spacing, *canvas));
        }
        return;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        apply_brush(brush_gesture_.release(at, keys));
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        apply_brush(brush_gesture_.move(at, keys));
    }
}

void App::apply_brush(const std::vector<gui::BrushAction>& actions) {
    for (const gui::BrushAction& action : actions) {
        switch (action.kind) {
            case gui::BrushActionKind::leave_brush:
                if (gui::brush_leaves(brush_.on(), gui::BrushEvent::escape, viewed_stage_)) {
                    leave_brush();
                }
                break;
            case gui::BrushActionKind::passthrough:
                break;  // the hand's: update_hand_drag moves the picture
            default:
                report(stroke_painter_.apply(action, brush_style(), stroke_canvas_, session_));
                break;
        }
    }
}

void App::clear_strokes() {
    report(session_.clear_strokes());  // one undo step, or none without strokes
}

void App::leave_pick() {
    pick_.leave();
    pick_click_.cancel();
    pick_hover_.reset();
    pick_message_.clear();
}

// A click in Pick mode. Only on a base that is the picture on screen, as the sampler requires: the
// pixels clicked are the base's.
void App::pick_at(gui::ViewPoint cursor) {
    const std::shared_ptr<const Image> base = session_.preview_base();
    const std::optional<gui::ViewPixel> pixel = gui::view_pixel_at(view_, view_geometry_, cursor);
    if (!base || !pixel || base->width != texture_.width() || base->height != texture_.height()) {
        return;
    }
    // Real pixels (#37): where they are on the canvas, the patch is the photo's own pixels.
    const std::shared_ptr<const prism::Photo> photo = session_.detail_base();
    const std::optional<gui::FilePixel> file =
        gui::file_pixel_at(view_, view_geometry_, framed_full_size(), cursor);
    const gui::NeutralPick pick =
        photo && reads_photo_at(file)
            ? gui::pick_neutral_photo(*photo, session_.settings().frame, file->x, file->y,
                                      gui::as_shot_white_of(photo->info))
            : gui::pick_neutral(*base, pixel->x, pixel->y, session_.as_shot_white());
    if (pick.outcome == gui::PickOutcome::unavailable) {
        return;
    }
    pick_message_ = gui::pick_message(pick);
    pick_message_until_ = ImGui::GetTime() + 6.0;
    if (pick.outcome == gui::PickOutcome::picked) {
        gui::EditSettings edited = session_.settings();
        gui::apply_pick(edited, pick);
        report(session_.set_settings(edited));  // one undo step
    }
}

void App::update_pick_cursor() {
    pick_hover_.reset();
    pick_hover_file_.reset();
    if (!pick_.on()) {
        return;
    }
    const std::shared_ptr<const Image> base = session_.preview_base();
    if (!base || base->width != texture_.width() || base->height != texture_.height() ||
        !ImGui::IsWindowHovered() || ImGui::IsAnyItemHovered() || strip_) {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (const std::optional<gui::ViewPixel> pixel =
            gui::view_pixel_at(view_, view_geometry_, {mouse.x, mouse.y})) {
        pick_hover_ = gui::SamplePin{pixel->x, pixel->y};
        pick_hover_file_ =
            gui::file_pixel_at(view_, view_geometry_, framed_full_size(), {mouse.x, mouse.y});
    }
}

// The ring around the patch a click would average (white on a dark edge, like the sampler's marker,
// so that it shows on any picture), and the chip with the last pick's words at the canvas's foot.
void App::draw_pick_overlay(ImDrawList* draw) {
    if (!pick_.on()) {
        return;
    }
    const float scale = ui_scale();
    const float line = std::max(1.0f, std::round(scale));
    if (pick_hover_ && gui::pick_available(viewed_stage_, compare_stage_)) {
        // Around the patch a click averages: the photo's pixels where it reads them (#37).
        const gui::PickMarker m = reads_photo_at(pick_hover_file_)
                                      ? gui::file_pick_marker(view_, view_geometry_,
                                                              framed_full_size(), *pick_hover_file_)
                                      : gui::pick_marker(view_, view_geometry_, *pick_hover_,
                                                         texture_.width(), texture_.height());
        if (m.on_canvas) {
            // At least as big as a ring can be seen at.
            const float least = 9.0f * scale;
            const float grow_x = std::max(0.0f, (least - m.patch.width()) / 2);
            const float grow_y = std::max(0.0f, (least - m.patch.height()) / 2);
            const ImVec2 min(m.patch.min.x - grow_x, m.patch.min.y - grow_y);
            const ImVec2 max(m.patch.max.x + grow_x, m.patch.max.y + grow_y);
            draw->AddRect(ImVec2(min.x - line, min.y - line), ImVec2(max.x + line, max.y + line),
                          IM_COL32(0, 0, 0, 200), 0.0f, 0, line);
            draw->AddRect(min, max, IM_COL32(255, 255, 255, 255), 0.0f, 0, line);
        }
    }
    if (pick_message_.empty() || ImGui::GetTime() > pick_message_until_) {
        return;
    }
    const gui::TokenMetrics& metrics = token_metrics();
    const gui::TokenColours& tokens = token_colours();
    const auto rgba = [](const gui::Rgba& c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    };
    const gui::ViewRect canvas = gui::view_canvas_rect(view_geometry_);
    ImGui::PushFont(nullptr, token_type().small);
    const ImVec2 text = ImGui::CalcTextSize(pick_message_.c_str());
    const float pad = metrics.strip_chip_margin * 0.75f;
    const ImVec2 chip_min(canvas.min.x + metrics.strip_chip_margin,
                          canvas.max.y - metrics.strip_chip_margin - text.y - pad);
    const ImVec2 chip_max(chip_min.x + text.x + 2 * pad, chip_min.y + text.y + pad);
    draw->PushClipRect(ImVec2(canvas.min.x, canvas.min.y), ImVec2(canvas.max.x, canvas.max.y), true);
    draw->AddRectFilled(chip_min, chip_max, rgba(tokens.strip_chip_bg), metrics.overlay_rounding);
    draw->AddText(ImVec2(chip_min.x + pad, chip_min.y + pad / 2), rgba(tokens.strip_chip_text),
                  pick_message_.c_str());
    draw->PopClipRect();
    canvas_taken_.push_back({{chip_min.x, chip_min.y}, {chip_max.x, chip_max.y}});
    canvas_chips_.push_back(canvas_taken_.back());
    ImGui::PopFont();
}

void App::update_sample_cursor() {
    if (!sample_.on()) {
        return;
    }
    const std::shared_ptr<const Image> base = session_.preview_base();
    // The strip's whole picture is one item, and a band can be read; any other item is a control.
    if (!base || base->width != texture_.width() || base->height != texture_.height() ||
        !ImGui::IsWindowHovered() || (ImGui::IsAnyItemHovered() && !strip_)) {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (const std::optional<gui::ViewPixel> pixel =
            gui::view_pixel_at(view_, view_geometry_, {mouse.x, mouse.y})) {
        sample_hover_ = gui::SamplePin{pixel->x, pixel->y};
        sample_.read(*sample_hover_, base->width, base->height, session_.settings().frame);
        sample_hover_file_ =
            gui::file_pixel_at(view_, view_geometry_, framed_full_size(), {mouse.x, mouse.y});
    }
}

void App::update_sample_traces() {
    if (!sample_.on()) {
        return;
    }
    const std::shared_ptr<const Image> base = session_.preview_base();
    if (!base) {
        return;
    }
    sample_.base_changed(base->width, base->height, session_.settings().frame);
    if (base->width != texture_.width() || base->height != texture_.height()) {
        return;  // the picture on screen isn't of this base yet: its pixels mean something else
    }
    const std::optional<gui::StageId> compare =
        strip_ ? std::nullopt : gui::effective_compare(viewed_stage_, compare_stage_);
    const auto read = [&](gui::SampleCache& cache, gui::SamplePin pixel) {
        (void)cache.read(base, session_.settings(), strip_, viewed_stage_, compare, pixel.x, pixel.y,
                         session_.as_shot_white(), session_.preview_framing());
    };
    // Real pixels (#37): a slot reads the photo's own pixel where real pixels of it are on the
    // canvas, and the preview's elsewhere, as in 0.6.0.
    const std::shared_ptr<const prism::Photo> photo = session_.detail_base();
    const auto read_photo = [&](gui::PhotoSampleCache& cache,
                                const std::optional<gui::FilePixel>& file) {
        if (!photo || !reads_photo_at(file)) {
            cache.clear();
            return false;
        }
        return cache.read(photo, session_.settings(), file->x, file->y,
                          gui::as_shot_white_of(photo->info)) != nullptr;
    };
    if (sample_.pin() != sample_pin_file_for_) {
        // A pin placed or moved where its photo pixel wasn't taken from the cursor: the photo pixel
        // in the middle of its preview pixel.
        sample_pin_file_.reset();
        sample_pin_file_size_ = framed_full_size();
        if (sample_.pin()) {
            sample_pin_file_ =
                gui::file_pixel_of(sample_.pin()->x, sample_.pin()->y, {base->width, base->height},
                                   sample_pin_file_size_);
        }
        sample_pin_file_for_ = sample_.pin();
    } else if (sample_pin_file_ && framed_full_size() != sample_pin_file_size_) {
        // The full-size photo arrived a little larger or smaller than the size estimated before:
        // the pin's photo pixel moves with it.
        sample_pin_file_ =
            gui::file_pixel_rescaled(*sample_pin_file_, sample_pin_file_size_, framed_full_size());
        sample_pin_file_size_ = framed_full_size();
    }
    sample_pin_photo_ = sample_.pin() && read_photo(sample_pin_photo_trace_, sample_pin_file_);
    if (sample_.pin() && !sample_pin_photo_) {
        read(sample_pin_trace_, *sample_.pin());
    } else {
        sample_pin_trace_.clear();
    }
    // With a pin the cursor's pixel only matters while it is on the picture. Without one the block
    // keeps the last pixel read, so that its rows are still there to point at.
    const bool hovering = sample_hover_.has_value() || (!sample_.pin() && sample_.last_read());
    sample_hover_photo_ = hovering && read_photo(sample_hover_photo_trace_, sample_hover_file_);
    if (!sample_hover_photo_) {
        if (sample_hover_) {
            read(sample_hover_trace_, *sample_hover_);
        } else if (!sample_.pin() && sample_.last_read()) {
            read(sample_hover_trace_, *sample_.last_read());
        }
    }
}

bool App::reads_photo_at(const std::optional<gui::FilePixel>& pixel) const {
    return pixel && !strip_ &&
           gui::sample_source(detail_plan_, detail_shown_, detail_base_number_.current(),
                              session_.settings_generation(), *pixel) == gui::PixelSource::photo;
}

gui::SampleMarker App::sample_marker_for(gui::SamplePin preview, bool photo,
                                         const std::optional<gui::FilePixel>& file) const {
    if (photo && file) {
        return gui::file_pixel_marker(view_, view_geometry_, framed_full_size(), *file);
    }
    return gui::sample_marker(view_, view_geometry_, preview);
}

const gui::PixelTrace* App::sample_hover_on_show() const {
    return sample_hover_photo_ ? sample_hover_photo_trace_.current()
                               : sample_hover_trace_.current();
}

const gui::PixelTrace* App::sample_on_show() const {
    if (!sample_.on()) {
        return nullptr;
    }
    if (sample_.pin()) {
        return sample_pin_photo_ ? sample_pin_photo_trace_.current() : sample_pin_trace_.current();
    }
    return sample_.last_read() ? sample_hover_on_show() : nullptr;
}

gui::SampleScale App::sample_scale() const {
    const gui::CanvasMeasure measure = canvas_measure();
    // A photo pixel averages nothing: the line says it is a real pixel instead.
    const bool photo = sample_.pin() ? sample_pin_photo_ : sample_hover_photo_;
    return {std::max(texture_.width(), texture_.height()), measure.file_long_side, measure.framed,
            photo};
}

bool App::render_drawable() const {
    const std::shared_ptr<const Image> whole = session_.whole_base();
    const std::shared_ptr<const Image> framed = session_.preview_base();
    if (!whole || !framed) {
        return false;
    }
    return gui::render_drawable(framing_.on(), {texture_.width(), texture_.height()},
                                {whole->width, whole->height}, {framed->width, framed->height});
}

gui::CanvasMeasure App::canvas_measure() const {
    return gui::canvas_measure(session_, framing_.on());
}

// White on a dark edge, so that it shows on any picture, in either theme: the picture's colours
// are not the theme's. Nothing of it depends on the interface's colours for that reason.
void App::draw_sample_markers(ImDrawList* draw) const {
    if (!sample_.on()) {
        return;
    }
    const float scale = ui_scale();
    const float line = std::max(1.0f, std::round(scale));
    const auto outline = [&](const gui::SampleMarker& m, ImU32 light, ImU32 dark) {
        // A pixel smaller than the marker could be seen at still gets a marker that can.
        const float least = 5.0f * scale;
        const float grow_x = std::max(0.0f, (least - m.square.width()) / 2);
        const float grow_y = std::max(0.0f, (least - m.square.height()) / 2);
        const ImVec2 min(m.square.min.x - grow_x - line / 2, m.square.min.y - grow_y - line / 2);
        const ImVec2 max(m.square.max.x + grow_x + line / 2, m.square.max.y + grow_y + line / 2);
        draw->AddRect(ImVec2(min.x - line, min.y - line), ImVec2(max.x + line, max.y + line), dark,
                      0.0f, 0, line);
        draw->AddRect(min, max, light, 0.0f, 0, line);
    };
    const auto crosshair = [&](const gui::SampleMarker& m, ImU32 light, ImU32 dark) {
        const float gap = 2.0f * scale;
        const float arm = 7.0f * scale;
        const ImVec2 c(m.centre.x, m.centre.y);
        const auto arms = [&](ImU32 colour, float width, float extra) {
            draw->AddLine(ImVec2(c.x - arm - extra, c.y), ImVec2(c.x - gap + extra, c.y), colour, width);
            draw->AddLine(ImVec2(c.x + gap - extra, c.y), ImVec2(c.x + arm + extra, c.y), colour, width);
            draw->AddLine(ImVec2(c.x, c.y - arm - extra), ImVec2(c.x, c.y - gap + extra), colour, width);
            draw->AddLine(ImVec2(c.x, c.y + gap - extra), ImVec2(c.x, c.y + arm + extra), colour, width);
        };
        arms(dark, 3.0f * line, line);
        arms(light, line, 0.0f);
    };
    // The cursor's marker, unless it is the pin's own pixel: the photo's pixel while both read the
    // photo (many lie under one preview pixel), the preview's otherwise.
    const bool hover_is_pin = sample_hover_photo_ && sample_pin_photo_
                                  ? sample_hover_file_ == sample_pin_file_
                                  : sample_hover_ == sample_.pin();
    if (sample_hover_ && !hover_is_pin) {
        const gui::SampleMarker m =
            sample_marker_for(*sample_hover_, sample_hover_photo_, sample_hover_file_);
        if (m.on_canvas) {
            outline(m, IM_COL32(255, 255, 255, 130), IM_COL32(0, 0, 0, 90));
        }
    }
    if (sample_.pin()) {
        const gui::SampleMarker m =
            sample_marker_for(*sample_.pin(), sample_pin_photo_, sample_pin_file_);
        if (m.on_canvas) {
            const ImU32 light = IM_COL32(255, 255, 255, 255);
            const ImU32 dark = IM_COL32(0, 0, 0, 200);
            if (m.shows_square) {
                outline(m, light, dark);
            } else {
                crosshair(m, light, dark);
            }
        }
    }
}

void App::draw_sample_block() {
    if (!sample_.on()) {
        return;
    }
    section_text("Sample");
    explain(gui::explain::sample);
    const gui::TokenColours& c = token_colours();
    const ImVec4 text(c.text.r, c.text.g, c.text.b, c.text.a);
    const ImVec4 muted(c.text_muted.r, c.text_muted.g, c.text_muted.b, c.text_muted.a);
    ImGui::PushFont(nullptr, token_type().small);
    ImGui::PushTextWrapPos(0.0f);
    const auto explained_row = [&](const std::string& line, std::string_view id,
                                   const ImVec4& colour) {
        // The whole width of the panel answers to the pointer, not only where the text is, so a
        // short row like "Nothing clipped." is as easy to point at as a long one.
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
        const ImVec2 end = ImGui::GetItemRectMax();
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(start, ImVec2(start.x + width, end.y))) {
            explain_hover_ = std::string(id);
        }
    };
    if (const gui::PixelTrace* trace = sample_on_show()) {
        const std::optional<gui::StageId> compare =
            strip_ ? std::nullopt : gui::effective_compare(viewed_stage_, compare_stage_);
        const bool off_screen =
            sample_.pin() &&
            !sample_marker_for(*sample_.pin(), sample_pin_photo_, sample_pin_file_).on_canvas;
        const auto stage_rows = [&](gui::StageId stage, const gui::SampleRows& rows) {
            if (compare) {
                ImGui::PushFont(fonts().semibold, token_type().small);
                explained_row(gui::sample_stage_title(stage), gui::explain::sample, text);
                ImGui::PopFont();
            }
            explained_row(rows.values + (off_screen ? " (off screen)" : ""),
                          gui::explain::sample_values, text);
            explained_row(rows.light, gui::explain::sample_light, text);
            explained_row(rows.range, gui::explain::sample_range, text);
        };
        const gui::SampleRows viewed = gui::sample_rows(*trace, viewed_stage_, sample_scale());
        stage_rows(viewed_stage_, viewed);
        if (compare) {
            stage_rows(*compare, gui::sample_rows(*trace, *compare));
        }
        if (!viewed.average.empty()) {
            explained_row(viewed.average, gui::explain::sample_range, muted);
        }
        if (sample_.pin() && sample_hover_) {
            if (const gui::PixelTrace* under = sample_hover_on_show()) {
                explained_row(gui::sample_cursor_line(*under, viewed_stage_),
                              gui::explain::sample_values, muted);
            }
        }
    } else {
        explained_row("Point at the photo to read a pixel; click to pin it.", gui::explain::sample,
                      muted);
    }
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

void App::toggle_test_strip() {
    if (!session_.has_file()) {
        return;
    }
    if (strip_) {
        strip_.reset();
        return;
    }
    if (!gui::strip_available(viewed_stage_)) {
        return;
    }
    if (gui::framing_leaves(framing_.on(), gui::FramingEvent::tool_chosen)) {
        leave_framing();
    }
    gui::TestStrip strip;
    strip.step = strip_step_;
    strip_ = strip;
    compare_stage_.reset();  // a comparison and a strip don't mix
    leave_pick();
    leave_brush();
}

void App::set_strip_step(gui::StripStep step) {
    strip_step_ = step;
    if (strip_) {
        strip_->step = step;
    }
}

void App::show_strip_band(int band) {
    if (band < 0 || band >= static_cast<int>(strip_bands_.size())) {
        return;
    }
    strip_shown_band_ = band;
    histogram_ = strip_bands_[static_cast<std::size_t>(band)].histogram;
    scope_view_.set(strip_bands_[static_cast<std::size_t>(band)].scopes);
}

// The strip's bar, above the canvas: its name, the step choice, a close button, and a line that
// says what the steps mean. On the overlay's dark box, like the other text over the photo.
void App::draw_strip_bar(float width) {
    const gui::TokenColours& tokens = token_colours();
    const gui::TokenMetrics& metrics = token_metrics();
    const auto rgba = [](const gui::Rgba& c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    };
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(rgba(tokens.strip_bar_bg)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(rgba(tokens.strip_bar_text)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(metrics.badge_padding.x, metrics.badge_padding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics.overlay_rounding);
    ImGui::BeginChild("strip_bar", ImVec2(width, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float line_top = ImGui::GetCursorPosY();
    const float frame = ImGui::GetFrameHeight();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(fonts().semibold, token_type().body);
    ImGui::TextUnformatted("Test strip");
    ImGui::PopFont();

    struct Choice {
        gui::StripStep step;
        const char* text;
    };
    const Choice choices[] = {{gui::StripStep::third, "\u2153 stop"},
                              {gui::StripStep::half, "\u00BD stop"},
                              {gui::StripStep::whole, "1 stop"}};
    const gui::StripStep current = strip_->step;
    for (const Choice& choice : choices) {
        const ImVec2 text = ImGui::CalcTextSize(choice.text);
        const ImVec2 size(text.x + 2 * metrics.strip_pill_pad_x,
                          text.y + 2 * metrics.strip_pill_pad_y);
        ImGui::SameLine();
        ImGui::SetCursorPosY(line_top + (frame - size.y) / 2);
        ImGui::PushID(static_cast<int>(choice.step));
        const bool picked = ImGui::InvisibleButton("##step", size);
        ImGui::PopID();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        if (choice.step == current) {
            draw->AddRectFilled(min, max, rgba(tokens.strip_bar_pill), metrics.chip_rounding);
        } else if (ImGui::IsItemHovered()) {
            draw->AddRectFilled(min, max, rgba(tokens.strip_bar_hover), metrics.chip_rounding);
        }
        draw->AddText(ImVec2(min.x + metrics.strip_pill_pad_x, min.y + metrics.strip_pill_pad_y),
                      rgba(tokens.strip_bar_text), choice.text);
        if (picked) {
            set_strip_step(choice.step);
        }
    }

    const float close_size = frame * 0.8f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - close_size);
    ImGui::SetCursorPosY(line_top + (frame - close_size) / 2);
    if (ImGui::InvisibleButton("##close_strip", ImVec2(close_size, close_size))) {
        strip_.reset();
    }
    const ImVec2 cmin = ImGui::GetItemRectMin();
    const ImVec2 cmax = ImGui::GetItemRectMax();
    if (ImGui::IsItemHovered()) {
        draw->AddRectFilled(cmin, cmax, rgba(tokens.strip_bar_hover), metrics.chip_rounding);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("Close the test strip (Esc)");
    }
    const ImVec2 mid((cmin.x + cmax.x) / 2, (cmin.y + cmax.y) / 2);
    const float arm = metrics.chevron_size * 0.8f;
    draw->AddLine(ImVec2(mid.x - arm, mid.y - arm), ImVec2(mid.x + arm, mid.y + arm),
                  rgba(tokens.strip_bar_text), metrics.chevron_thickness);
    draw->AddLine(ImVec2(mid.x - arm, mid.y + arm), ImVec2(mid.x + arm, mid.y - arm),
                  rgba(tokens.strip_bar_text), metrics.chevron_thickness);

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(gui::strip_teaching_line(current).c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        explain_hover_ = std::string(gui::explain::strip_line);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// The bands over the picture: thin dividers, a chip with each band's label, a wash and edge on the
// band under the cursor. Clicking a band sets the exposure to it; the strip stays on and is
// centred on the new value. Hovering also decides whose histogram and scopes the panel shows.
void App::draw_strip_bands(ImDrawList* draw, ImVec2 at, ImVec2 shown) {
    const Palette& colours = palette(theme());
    const gui::TokenColours& tokens = token_colours();
    const gui::TokenMetrics& metrics = token_metrics();
    const auto rgba = [](const gui::Rgba& c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    };
    (void)colours;
    const int count = static_cast<int>(strip_bands_.size());
    const int middle = gui::strip_middle(count);
    const int image_w = texture_.width();
    const int image_h = texture_.height();
    const float sx = shown.x / static_cast<float>(image_w);
    const float sy = shown.y / static_cast<float>(image_h);
    const gui::ViewRect visible = gui::view_visible_rect(view_, view_geometry_);

    // One hit area over the part of the picture on the canvas; the band is found from the cursor.
    // Not while the hand is at work: the same press must not set the exposure.
    bool clicked = false;
    bool hovered = false;
    if (!hand_key_held_ && !hand_drag_.pressed()) {
        const ImVec2 saved = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(visible.min.x, visible.min.y));
        clicked = ImGui::InvisibleButton(
            "##strip_bands", ImVec2(std::max(1.0f, visible.width()), std::max(1.0f, visible.height())));
        hovered = ImGui::IsItemHovered();
        ImGui::SetCursorScreenPos(saved);
    }
    std::optional<int> hot;
    if (hovered) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        hot = gui::strip_band_at(image_w, image_h, count, (mouse.x - at.x) / sx,
                                 (mouse.y - at.y) / sy);
    }
    const int shown_band = hot.value_or(middle);
    if (shown_band != strip_shown_band_) {
        show_strip_band(shown_band);
    }
    if (clicked && hot) {
        gui::EditSettings edited = session_.settings();
        if (gui::apply_strip_band(edited, strip_->step, count, *hot)) {
            report(session_.set_settings(edited));  // one undo step
        }
    }

    const auto screen = [&](const gui::StripRect& r) {
        return std::pair{ImVec2(std::round(at.x + static_cast<float>(r.x) * sx),
                                std::round(at.y + static_cast<float>(r.y) * sy)),
                         ImVec2(std::round(at.x + static_cast<float>(r.x + r.width) * sx),
                                std::round(at.y + static_cast<float>(r.y + r.height) * sy))};
    };
    const bool vertical = gui::strip_vertical(image_w, image_h);
    const float line = std::max(1.0f, std::round(metrics.strip_divider_width));
    for (int i = 1; i < count; ++i) {
        const gui::StripRect& r = strip_bands_[static_cast<std::size_t>(i)].rect;
        if (r.width <= 0 || r.height <= 0) {
            continue;
        }
        const auto [min, max] = screen(r);
        if (vertical) {
            draw->AddRectFilled(ImVec2(min.x - line / 2, min.y), ImVec2(min.x + line / 2, max.y),
                                rgba(tokens.strip_divider));
        } else {
            draw->AddRectFilled(ImVec2(min.x, min.y - line / 2), ImVec2(max.x, min.y + line / 2),
                                rgba(tokens.strip_divider));
        }
    }
    if (hot) {
        // Cut to the canvas, so that the outline of a band that runs off it still shows.
        const auto [band_min, band_max] = screen(strip_bands_[static_cast<std::size_t>(*hot)].rect);
        const gui::ViewRect on_canvas =
            gui::view_intersect({{band_min.x, band_min.y}, {band_max.x, band_max.y}}, visible);
        const ImVec2 min(on_canvas.min.x, on_canvas.min.y);
        const ImVec2 max(on_canvas.max.x, on_canvas.max.y);
        const float edge = metrics.strip_hover_edge_width;
        draw->AddRectFilled(min, max, rgba(tokens.strip_hover_fill));
        draw->AddRect(ImVec2(min.x + edge / 2, min.y + edge / 2),
                      ImVec2(max.x - edge / 2, max.y - edge / 2), rgba(tokens.strip_hover_edge), 0.0f,
                      0, edge);
    }
    const float pad = metrics.overlay_padding;
    for (int i = 0; i < count; ++i) {
        const gui::StripRect& r = strip_bands_[static_cast<std::size_t>(i)].rect;
        if (r.width <= 0 || r.height <= 0) {
            continue;
        }
        // In the band's corner on the canvas, so a band that is partly off it keeps its label.
        const auto [band_min, band_max] = screen(r);
        const gui::ViewRect on_canvas =
            gui::view_intersect({{band_min.x, band_min.y}, {band_max.x, band_max.y}}, visible);
        if (on_canvas.empty()) {
            continue;
        }
        const ImVec2 min(on_canvas.min.x, on_canvas.min.y);
        const ImVec2 max(on_canvas.max.x, on_canvas.max.y);
        const std::string label = gui::strip_label(strip_->step, count, i);
        const ImVec2 text = ImGui::CalcTextSize(label.c_str());
        const ImVec2 chip_min(min.x + metrics.strip_chip_margin, min.y + metrics.strip_chip_margin);
        canvas_taken_.push_back(gui::view_intersect(
            {{chip_min.x, chip_min.y}, {chip_min.x + text.x + 2 * pad, chip_min.y + text.y + pad}},
            {{min.x, min.y}, {max.x, max.y}}));
        draw->PushClipRect(min, max, true);
        draw->AddRectFilled(chip_min, ImVec2(chip_min.x + text.x + 2 * pad, chip_min.y + text.y + pad),
                            rgba(tokens.strip_chip_bg), metrics.overlay_rounding);
        draw->AddText(ImVec2(chip_min.x + pad, chip_min.y + pad / 2), rgba(tokens.strip_chip_text),
                      label.c_str());
        draw->PopClipRect();
    }
    if (hot && *hot != middle) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Click to use this band's exposure");
        }
    } else if (hot && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("The current exposure");
    }
}

bool App::splitting() const {
    return texture_.texture() != nullptr && texture_.compare_texture() != nullptr &&
           texture_.compare_width() == texture_.width() &&
           texture_.compare_height() == texture_.height() &&
           gui::effective_compare(viewed_stage_, compare_stage_).has_value();
}

// The compare stage on the left, the viewed stage on the right, one picture size and position.
// The divider is a whole pixel and both halves take their edge and their texture coordinate from
// it, so they meet exactly. It divides the part of the picture on the canvas, so zoomed in it stays
// within reach and the picture slides under it.
void App::draw_split(ImDrawList* draw, ImVec2 at, ImVec2 shown) {
    const Palette& colours = palette(theme());
    const float scale = ui_scale();
    const gui::ViewRect visible = gui::view_visible_rect(view_, view_geometry_);
    const gui::SplitLayout split = gui::split_layout(visible.min.x, visible.width(), split_fraction_);
    const float top = visible.min.y;
    const float bottom = visible.max.y;
    const float image_top = at.y;
    const float image_bottom = at.y + shown.y;
    const float image_right = at.x + shown.x;
    const float u_divider = shown.x > 0 ? (split.divider_x - at.x) / shown.x : 0.0f;
    const auto id = [](SDL_Texture* t) {
        return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(t)));
    };
    begin_picture_sampling(draw);
    draw->AddImage(id(texture_.compare_texture()), ImVec2(at.x, image_top),
                   ImVec2(split.divider_x, image_bottom), ImVec2(0.0f, 0.0f),
                   ImVec2(u_divider, 1.0f));
    draw->AddImage(id(texture_.texture()), ImVec2(split.divider_x, image_top),
                   ImVec2(image_right, image_bottom), ImVec2(u_divider, 0.0f), ImVec2(1.0f, 1.0f));
    // The overlays belong to the viewed stage: only over its half.
    draw_picture_overlays(draw, ImVec2(split.divider_x, image_top),
                          ImVec2(image_right, image_bottom), ImVec2(u_divider, 0.0f),
                          ImVec2(1.0f, 1.0f));
    end_picture_sampling(draw);

    // The divider: a line with a dark edge so it shows on any picture, and a grip in its middle.
    const gui::TokenMetrics& metrics = token_metrics();
    const gui::TokenColours& tokens = token_colours();
    const auto rgba = [](const gui::Rgba& c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    };
    const float line = metrics.divider_line;
    draw->AddRectFilled(ImVec2(split.divider_x - line, top), ImVec2(split.divider_x + line, bottom),
                        rgba(tokens.divider_edge));
    draw->AddRectFilled(ImVec2(split.divider_x - line / 2, top),
                        ImVec2(split.divider_x + line / 2, bottom), rgba(tokens.divider_line));
    const float mid = (top + bottom) / 2;
    const float grip = metrics.divider_grip;
    draw->AddCircleFilled(ImVec2(split.divider_x, mid), grip, rgba(tokens.divider_line));
    draw->AddCircle(ImVec2(split.divider_x, mid), grip, rgba(tokens.grip_ring), 0, 1.0f * scale);
    const float arrow = metrics.chevron_size;
    const ImU32 ink = rgba(tokens.grip_ink);
    draw_chevron(draw, ImVec2(split.divider_x - arrow * 0.5f - 1 * scale, mid), Chevron::left, ink);
    draw_chevron(draw, ImVec2(split.divider_x + arrow * 0.5f + 1 * scale, mid), Chevron::right,
                 ink);

    // Dragging: a strip around the divider, as tall as the picture. The cursor is put back after,
    // so the banner and badge below this lay out as before.
    // It wins over a plain drag of the picture, but not over the hand already at work.
    if (!hand_key_held_ && !hand_drag_.pressed()) {
        const ImVec2 saved = ImGui::GetCursorScreenPos();
        const float strip = metrics.divider_strip;
        ImGui::SetCursorScreenPos(ImVec2(split.divider_x - strip / 2, top));
        ImGui::InvisibleButton("##split_divider", ImVec2(strip, std::max(1.0f, bottom - top)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            split_fraction_ =
                gui::split_fraction_from_x(ImGui::GetIO().MousePos.x, visible.min.x, visible.width());
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive()) {
            ImGui::SetTooltip("Drag to move the divider");
        }
        ImGui::SetCursorScreenPos(saved);
    }

    // One chip in each half's top corner, named by the stage each picture really shows.
    const float margin = metrics.divider_chip_margin;
    const std::string left_text = gui::stage_caption(texture_.compare_stage());
    const std::string right_text = gui::stage_caption(histogram_stage_);
    const float pad = metrics.overlay_padding;
    draw->PushClipRect(ImVec2(split.left_x0, top), ImVec2(split.divider_x, bottom), true);
    overlay_text(draw, colours, ImVec2(split.left_x0 + margin, top + margin), left_text);
    draw->PopClipRect();
    canvas_taken_.push_back(
        gui::view_intersect(overlay_rect(ImVec2(split.left_x0 + margin, top + margin), left_text),
                            {{split.left_x0, top}, {split.divider_x, bottom}}));
    canvas_chips_.push_back(canvas_taken_.back());
    const float right_width = ImGui::CalcTextSize(right_text.c_str()).x + 2 * pad;
    draw->PushClipRect(ImVec2(split.divider_x, top), ImVec2(split.right_x1, bottom), true);
    overlay_text(draw, colours, ImVec2(split.right_x1 - margin - right_width, top + margin),
                 right_text);
    draw->PopClipRect();
    canvas_taken_.push_back(gui::view_intersect(
        overlay_rect(ImVec2(split.right_x1 - margin - right_width, top + margin), right_text),
        {{split.divider_x, top}, {split.right_x1, bottom}}));
    canvas_chips_.push_back(canvas_taken_.back());
}

void App::draw_banner() {
    if (!error_ && notice_.empty()) {
        return;
    }
    const float margin = token_metrics().canvas_margin;
    ImGui::SetCursorPos(ImVec2(margin, margin));
    const bool is_error = error_.has_value();
    const Palette& colours = palette(theme());
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          is_error ? colours.error_background : colours.notice_background);
    ImGui::PushStyleColor(ImGuiCol_Text, is_error ? colours.error_text : colours.notice_text);
    ImGui::PushStyleColor(ImGuiCol_Border, is_error ? colours.error_border : colours.notice_border);
    // Across the canvas, but not wider than a comfortable line length on a big window.
    const float banner_width =
        std::min(ImGui::GetContentRegionAvail().x - margin, token_metrics().banner_max_width);
    ImGui::BeginChild("banner", ImVec2(std::max(1.0f, banner_width), 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(is_error ? error_->message.c_str() : notice_.c_str());
    ImGui::PopTextWrapPos();
    if (is_error && !error_->details.empty()) {
        if (details_toggle("Details", &error_details_open_)) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(error_->details.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::SmallButton("Copy")) {
                ImGui::SetClipboardText((error_->message + "\n" + error_->details).c_str());
            }
        }
    }
    if (ImGui::SmallButton("Dismiss")) {
        error_.reset();
        notice_.clear();
        error_details_open_ = false;
    }
    ImGui::EndChild();
    canvas_taken_.push_back({{ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y},
                             {ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y}});
    ImGui::PopStyleColor(3);
}

// Top middle of the canvas, quiet and without a timer: why Decode looks the way it does. Drawn like
// the pick chip; left out where the canvas is too narrow for its words, and registered so a press on
// it is not a pan, a sample or a stroke.
void App::draw_decode_chip() {
    if (!view_available() || splitting() || !gui::decode_chip_shown(viewed_stage_, compare_stage_)) {
        return;
    }
    const gui::TokenMetrics& metrics = token_metrics();
    const gui::TokenColours& tokens = token_colours();
    const auto rgba = [](const gui::Rgba& c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    };
    const std::string text_string(gui::decode_chip_text());
    const gui::ViewRect canvas = gui::view_canvas_rect(view_geometry_);
    ImGui::PushFont(nullptr, token_type().small);
    const ImVec2 text = ImGui::CalcTextSize(text_string.c_str());
    const float pad = metrics.strip_chip_margin * 0.75f;
    const float width = text.x + 2 * pad;
    if (width > canvas.width() - 2 * metrics.strip_chip_margin) {
        ImGui::PopFont();
        return;
    }
    const float left = std::round(canvas.min.x + (canvas.width() - width) / 2);
    const float height = text.y + pad;
    // Under whatever already sits where it would go (the badge, a banner): never on top of it.
    float top = canvas.min.y + metrics.strip_chip_margin;
    for (const gui::ViewRect& taken : canvas_taken_) {
        if (!gui::view_intersect({{left, top}, {left + width, top + height}}, taken).empty()) {
            top = std::max(top, taken.max.y + metrics.strip_chip_margin);
        }
    }
    if (top + height > canvas.max.y) {
        ImGui::PopFont();
        return;
    }
    const ImVec2 chip_min(left, top);
    const ImVec2 chip_max(left + width, top + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(canvas.min.x, canvas.min.y), ImVec2(canvas.max.x, canvas.max.y), true);
    draw->AddRectFilled(chip_min, chip_max, rgba(tokens.strip_chip_bg), metrics.overlay_rounding);
    draw->AddText(ImVec2(chip_min.x + pad, chip_min.y + pad / 2), rgba(tokens.strip_chip_text),
                  text_string.c_str());
    draw->PopClipRect();
    canvas_taken_.push_back({{chip_min.x, chip_min.y}, {chip_max.x, chip_max.y}});
    canvas_chips_.push_back(canvas_taken_.back());
    ImGui::PopFont();
}

void App::draw_stage_badge() {
    // Two halves have their own chips; the badge is for the single picture.
    if (!session_.has_file() || viewed_stage_ == gui::final_stage || splitting()) {
        return;
    }
    const float margin = token_metrics().canvas_margin;
    if (!error_ && notice_.empty()) {
        ImGui::SetCursorPos(ImVec2(margin, margin));
    } else {
        ImGui::SetCursorPosX(margin);
    }

    const Palette& colours = palette(theme());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colours.notice_background);
    ImGui::PushStyleColor(ImGuiCol_Text, colours.notice_text);
    ImGui::PushStyleColor(ImGuiCol_Border, colours.notice_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(token_metrics().badge_padding.x, token_metrics().badge_padding.y));
    ImGui::BeginChild("stage_badge", ImVec2(0, 0),
                      ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
    const std::string label = "Viewing: after " + std::string(gui::stage_info(viewed_stage_).name);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Final image (Esc)")) {
        viewed_stage_ = gui::final_stage;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        explain_hover_ = std::string(gui::explain::viewing);
    }
    ImGui::EndChild();
    canvas_taken_.push_back({{ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y},
                             {ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y}});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

// Top middle of the canvas, dark like the zoom's chip: the two tones, the one strokes take lit
// (X switches them too), and under them what a stroke does now. Clear of the other chips; on a
// canvas too narrow for the words, the tones alone, and with no room for those, nothing.
void App::draw_brush_chip() {
    if (!brush_.on() || !view_available()) {
        return;
    }
    const gui::TokenMetrics& metrics = token_metrics();
    const gui::TokenColours& tokens = token_colours();
    const auto rgba4 = [](const gui::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); };
    const std::optional<gui::BrushTone> stroke =
        brush_gesture_.stroking() ? std::optional<gui::BrushTone>(brush_gesture_.tone())
                                  : std::nullopt;
    const gui::BrushTone shown = gui::brush_shown_tone(brush_.tone(), ImGui::GetIO().KeyAlt, stroke);
    const std::string status = gui::brush_status_text(shown, brush_.strength());
    const std::string dodge(gui::brush_dodge_label);
    const std::string burn(gui::brush_burn_label);

    ImGui::PushFont(nullptr, token_type().small);
    const float pad = metrics.zoom_chip_pad;
    const float button_pad = metrics.zoom_chip_button_pad_x;
    const float button_pad_y = ImGui::GetStyle().FramePadding.y * 0.5f;
    const float line = ImGui::GetTextLineHeight();
    const float tones_width = ImGui::CalcTextSize(dodge.c_str()).x +
                              ImGui::CalcTextSize(burn.c_str()).x + 4 * button_pad;
    const float status_width = ImGui::CalcTextSize(status.c_str()).x + 2 * button_pad;
    const float tones_height = line + 2 * button_pad_y;
    const gui::ViewRect canvas = gui::view_canvas_rect(view_geometry_);
    const float margin = metrics.zoom_chip_margin.y;
    bool with_status = true;
    float width = std::max(tones_width, status_width) + 2 * pad;
    float height = tones_height + line + 2 * pad;
    if (width > canvas.width() - 2 * margin) {
        with_status = false;
        width = tones_width + 2 * pad;
        height = tones_height + 2 * pad;
    }
    if (width > canvas.width() - 2 * margin || height > canvas.height() - 2 * margin) {
        ImGui::PopFont();
        return;
    }
    const ImVec2 at(std::round(canvas.min.x + (canvas.width() - width) / 2),
                    std::round(canvas.min.y + margin));
    ImGui::SetCursorScreenPos(at);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgba4(tokens.zoom_chip_bg));
    ImGui::PushStyleColor(ImGuiCol_Text, rgba4(tokens.zoom_chip_text));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba4(tokens.zoom_chip_hovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgba4(tokens.zoom_chip_current));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics.zoom_chip_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("brush_chip", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(button_pad, button_pad_y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                        std::max(0.0f, metrics.zoom_chip_rounding - pad));
    // One segment: lit while it is the base tone. A click on it makes it the base tone.
    const auto segment = [&](const std::string& text, gui::BrushTone tone) {
        const bool current = brush_.tone() == tone;
        ImGui::PushStyleColor(ImGuiCol_Button, current ? rgba4(tokens.zoom_chip_current)
                                                       : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, current ? rgba4(tokens.zoom_chip_current_text)
                                                     : rgba4(tokens.zoom_chip_idle_text));
        if (ImGui::Button(text.c_str())) {
            brush_.set_tone(tone);
        }
        ImGui::PopStyleColor(2);
        describe(gui::explain::dodge_burn_tone);
    };
    ImGui::SetCursorPosX(pad + (width - 2 * pad - tones_width) / 2);
    segment(dodge, gui::BrushTone::dodge);
    ImGui::SameLine(0.0f, 0.0f);
    segment(burn, gui::BrushTone::burn);
    if (with_status) {
        ImGui::SetCursorPosX(pad + (width - 2 * pad - ImGui::CalcTextSize(status.c_str()).x) / 2);
        ImGui::TextUnformatted(status.c_str());
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
    canvas_taken_.push_back({{ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y},
                             {ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y}});
    canvas_chips_.push_back(canvas_taken_.back());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    ImGui::PopFont();
}

void App::draw_brush_ring(ImDrawList* draw) {
    const ImGuiIO& io = ImGui::GetIO();
    const gui::ViewPoint cursor{io.MousePos.x, io.MousePos.y};
    if (!brush_.on() || !gui::brush_available(viewed_stage_) || !ImGui::IsWindowHovered() ||
        ImGui::IsAnyItemHovered() || hand_key_held_ || brush_gesture_.panning() ||
        gui::view_point_in_any(cursor, canvas_chips_)) {
        return;
    }
    const std::optional<gui::BrushCanvas> canvas = brush_canvas();
    if (!canvas) {
        return;
    }
    const gui::BrushRing ring =
        gui::brush_ring(view_, view_geometry_, *canvas, brush_.radius(), brush_.softness());
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);  // the ring is the cursor
    const float scale = ui_scale();
    const ImVec2 centre(cursor.x, cursor.y);
    // A light line on a dark one, so the rings show on any picture.
    const ImU32 dark = IM_COL32(0, 0, 0, 150);
    const ImU32 light = IM_COL32(255, 255, 255, 230);
    const ImU32 light_soft = IM_COL32(255, 255, 255, 140);
    draw->AddCircle(centre, ring.outer, dark, 0, 3.0f * scale);
    draw->AddCircle(centre, ring.outer, light, 0, 1.25f * scale);
    if (ring.inner >= 1.0f && ring.outer - ring.inner >= 2.0f * scale) {
        draw->AddCircle(centre, ring.inner, dark, 0, 2.0f * scale);
        draw->AddCircle(centre, ring.inner, light_soft, 0, 1.0f * scale);
    }
    // The centre, so a small brush still shows where it is.
    const float tick = 3.0f * scale;
    draw->AddLine(ImVec2(centre.x - tick, centre.y), ImVec2(centre.x + tick, centre.y), light,
                  1.0f * scale);
    draw->AddLine(ImVec2(centre.x, centre.y - tick), ImVec2(centre.x, centre.y + tick), light,
                  1.0f * scale);
}

void App::draw_zoom_chip() {
    if (!view_available()) {
        return;
    }
    const gui::TokenMetrics& metrics = token_metrics();
    const gui::TokenColours& tokens = token_colours();
    const auto rgba4 = [](const gui::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); };
    // Percent of the file (#37), with "· preview" while enlarged preview pixels are shown for a
    // reason that lasts, or because the view is drawn from the preview.
    const std::string label = gui::canvas_zoom_chip(view_, view_geometry_, detail_plan_.chip);
    // The choice the view is at is lit; clicking it does nothing. 100% can't be chosen where Fit
    // already shows the photo at 100% or more.
    const bool fit_current = view_.fit;
    const bool actual_current = gui::view_at_actual_size(view_, view_geometry_);
    const bool actual_unavailable = !gui::view_actual_size_available(view_geometry_);

    ImGui::PushFont(nullptr, token_type().small);
    const float pad = metrics.zoom_chip_pad;
    const float button_pad = metrics.zoom_chip_button_pad_x;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    const float button_pad_y = ImGui::GetStyle().FramePadding.y * 0.5f;
    const float label_width = ImGui::CalcTextSize(label.c_str()).x;
    const float buttons_width = ImGui::CalcTextSize("Fit").x + ImGui::CalcTextSize("100%").x +
                                4 * button_pad + gap;
    const float height = ImGui::GetTextLineHeight() + 2 * button_pad_y + 2 * pad;
    // The number and both buttons; on a canvas with no room for those, the number alone; and with
    // no room for that, nothing (the keys and the View menu still work).
    const gui::ViewRect canvas = gui::view_canvas_rect(view_geometry_);
    bool with_buttons = true;
    float width = button_pad + label_width + gap + buttons_width + 2 * pad;
    std::optional<gui::ViewPoint> at = gui::view_chip_position(
        canvas, width, height, metrics.zoom_chip_margin.x, metrics.zoom_chip_margin.y, canvas_taken_);
    if (!at) {
        with_buttons = false;
        width = 2 * button_pad + label_width + 2 * pad;
        at = gui::view_chip_position(canvas, width, height, metrics.zoom_chip_margin.x,
                                     metrics.zoom_chip_margin.y, canvas_taken_);
    }
    if (at) {
        ImGui::SetCursorScreenPos(ImVec2(at->x, at->y));
        // Dark in both themes, so the chip has its own colours, not the theme's.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, rgba4(tokens.zoom_chip_bg));
        ImGui::PushStyleColor(ImGuiCol_Text, rgba4(tokens.zoom_chip_text));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba4(tokens.zoom_chip_hovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgba4(tokens.zoom_chip_current));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics.zoom_chip_rounding);
        ImGui::BeginChild("zoom_chip", ImVec2(width, height),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(button_pad, button_pad_y));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::max(0.0f, metrics.zoom_chip_rounding - pad));
        ImGui::AlignTextToFramePadding();
        ImGui::SetCursorPosX(pad + button_pad);
        ImGui::TextUnformatted(label.c_str());
        describe(gui::explain::view_zoom);
        // One choice: lit when it is the current one (a brighter wash, white text), plain
        // otherwise. Clicking the lit one does nothing.
        const auto choice = [&](const char* text, bool current, bool unavailable) {
            ImGui::PushStyleColor(ImGuiCol_Button, current ? rgba4(tokens.zoom_chip_current)
                                                           : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, current ? rgba4(tokens.zoom_chip_current_text)
                                                         : rgba4(tokens.zoom_chip_idle_text));
            ImGui::BeginDisabled(unavailable);
            const bool clicked = ImGui::Button(text) && !current;
            ImGui::EndDisabled();
            ImGui::PopStyleColor(2);
            return clicked;
        };
        if (with_buttons) {
            ImGui::SameLine(0.0f, gap);
            if (choice("Fit", fit_current, false)) {
                view_fit();
            }
            describe(gui::explain::view_fit);
            ImGui::SameLine(0.0f, 0.0f);
            if (choice("100%", actual_current, actual_unavailable)) {
                view_actual_size();
            }
            describe(gui::explain::view_actual);
        }
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
    }
    ImGui::PopFont();
}

void App::draw_splitter(float canvas_width, float width, float height, float available) {
    // The gap itself only takes room and is painted in the canvas colour.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    {
        const gui::Rgba& c = token_colours().canvas;
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                  ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a)));
    }
    // The grab area is centred on the panel's drawn edge (the gap's right end), so it reaches over
    // the canvas and the panel both. A window of its own, above them, takes the hover and the press.
    const gui::SplitterGrab grab = gui::splitter_grab(origin.x + width, width, ui_scale());
    ImGui::SetNextWindowPos(ImVec2(grab.x, origin.y));
    ImGui::SetNextWindowSize(ImVec2(grab.width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##splitter_grab", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::InvisibleButton("splitter", ImVec2(grab.width, height));
        const bool hit = gui::splitter_grab_hit(grab, ImGui::GetIO().MousePos.x);
        if ((ImGui::IsItemHovered() && hit) || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            // Relative to the panel as it is: a press moves nothing, and panel_width_pixels clamps
            // the width next frame.
            const float current = available - canvas_width - width;
            const float panel = gui::dragged_panel_pixels(current, ImGui::GetIO().MouseDelta.x);
            prefs_.panel_width = gui::widen_panel_width(
                gui::panel_points_from_pixels(panel, ui_scale()), ui_scale(), panel_text_min_);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive()) {
            ImGui::SetTooltip("Drag to resize the panel");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void App::explain(std::string_view id, bool changed) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        explain_hover_ = id;
    }
    if (ImGui::IsItemFocused()) {
        explain_focus_ = id;
    }
    if (changed || ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) {
        explain_last_ = id;
    }
}

// explain(), and the control's summary as its tooltip. A disabled control still explains itself.
void App::describe(std::string_view id, bool changed, ImGuiHoveredFlags tooltip_after) {
    explain(id, changed);
    if (ImGui::IsItemHovered(tooltip_after | ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (const gui::Explanation* e = gui::explanation_for(id)) {
            ImGui::SetTooltip("%s", std::string(e->summary).c_str());
        }
    }
}

App::ExplainText App::explain_text() const {
    const gui::Explanation* e = gui::explanation_for(explain_shown_);
    if (e == nullptr) {
        return {"Point at a control to see what it does to the light.", "", ""};
    }
    gui::LiveContext context;
    context.kind = session_.kind();
    context.viewed = viewed_stage_;
    context.compare = gui::effective_compare(viewed_stage_, compare_stage_);
    context.histogram = histogram_ ? &*histogram_ : nullptr;
    context.clips = gui::clip_report_to_show(note_report_, strip_.has_value());
    context.strip_step = strip_ ? strip_->step : strip_step_;
    context.recipe = recipes_.current();
    if (texture_.texture() != nullptr) {
        const gui::CanvasMeasure measure = canvas_measure();
        context.preview_long_side = std::max(texture_.width(), texture_.height());
        context.file_long_side = measure.file_long_side;
        context.measures_frame = measure.framed;
        context.view_state = detail_plan_.draw ? gui::real_pixels_shown_explain()
                                               : gui::preview_chip_explain(detail_plan_.chip);
    }
    context.file_size_exact = session_.file_size_exact();
    context.file_width = session_.file_width();
    context.file_height = session_.file_height();
    context.sample = sample_on_show();
    context.as_shot_white = session_.as_shot_white();
    return {std::string(e->summary), std::string(e->detail),
            gui::live_line(explain_shown_, session_.settings(), context)};
}

void App::draw_explain_area(float width, float height) {
    const gui::TokenMetrics& m = token_metrics();
    const gui::TokenColours& c = token_colours();
    const auto vec = [](const gui::Rgba& rgba) { return ImVec4(rgba.r, rgba.g, rgba.b, rgba.a); };
    // Its own background (explain), a hair line above it instead of a border.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(c.explain));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.explain_pad.x, m.explain_pad.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("explain", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    const ExplainText text = explain_text();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, m.explain_gap));
    const auto part = [&](const std::string& s, ImFont* font, float size, ImVec4 colour) {
        if (s.empty()) {
            return;
        }
        ImGui::PushFont(font, size);
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextUnformatted(s.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    };
    const bool hint = gui::explanation_for(explain_shown_) == nullptr;
    // The title in the body colour and semi-bold; the body and the live line quiet.
    part(text.summary, hint ? nullptr : fonts().semibold,
         hint ? token_type().small : token_type().explain_title, vec(hint ? c.text_muted : c.text));
    part(text.detail, nullptr, token_type().small, vec(c.text_muted));
    part(text.live, nullptr, token_type().small, vec(c.text_muted));
    ImGui::PopStyleVar();
    ImGui::PopTextWrapPos();
    // A text longer than the area scrolls inside it: a short fade at the bottom edge says there
    // is more, and goes once the end is reached. Drawn only, so the height stays as given (#38).
    if (ImGui::GetScrollMaxY() > 0.0f && ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 0.5f) {
        const float border = 0.0f;
        const float scrollbar = ImGui::GetCurrentWindow()->ScrollbarSizes.x;
        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImGui::PushFont(nullptr, token_type().small);
        const float fade = ImGui::GetTextLineHeight() * 1.5f;
        ImGui::PopFont();
        const ImVec2 min(origin.x + border, origin.y + size.y - border - fade);
        const ImVec2 max(origin.x + size.x - border - scrollbar, origin.y + size.y - border);
        const ImU32 clear = ImGui::ColorConvertFloat4ToU32(ImVec4(c.explain.r, c.explain.g, c.explain.b, 0.0f));
        const ImU32 solid = ImGui::ColorConvertFloat4ToU32(vec(c.explain));
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min, max, clear, clear, solid, solid);
    }
    // The hair line along the top edge, over any text scrolled up under it.
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                      ImGui::GetWindowPos().y + hair()),
        ImGui::ColorConvertFloat4ToU32(vec(c.hair)));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void App::draw_panel_column(float height) {
    const float width = ImGui::GetContentRegionAvail().x;
    const bool explaining = prefs_.show_explanations && session_.has_file();
    // A fixed height (about five lines of Small text): it must not follow the text shown, or the
    // panel above it changes size as the cursor crosses the controls (#38). A longer text
    // scrolls inside the area.
    float explain_h = 0.0f;
    if (explaining) {
        ImGui::PushFont(nullptr, token_type().small);
        const float line = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        explain_h = gui::explain_area_height(token_metrics(), line, height);
    }
    // The panel's surface is drawn by frame(); the panel and the Explain area touch each other.
    const gui::TokenMetrics& m = token_metrics();
    const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
    const float header_h = session_.has_file() ? std::round(m.header_height) : 0.0f;
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing.x, 0.0f));  // no gap below
    if (header_h > 0.0f) {
        draw_panel_header(width, header_h);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.panel_pad.x, m.panel_pad.y));
    ImGui::BeginChild("panel", ImVec2(width, std::max(1.0f, height - header_h - (explaining ? explain_h : 0.0f))),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);  // the usual one inside
    draw_panel();
    // The bottom padding is a little more than the top one; the child adds the top's amount.
    ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, m.panel_pad_bottom - m.panel_pad.y)));
    ImGui::PopStyleVar();
    ImGui::EndChild();
    if (explaining) {
        draw_explain_area(width, explain_h);
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

// The two curved arrows of the Undo and Redo buttons, drawn in a 24 x 24 box (stroke 1.8): the
// arrow's head, then its stem running into a half turn and back. Redo is Undo mirrored.
static void draw_history_icon(ImDrawList* draw, ImVec2 centre, float size, bool redo, ImU32 colour) {
    const float unit = size / 24.0f;
    const auto at = [&](float x, float y) {
        const float u = redo ? 24.0f - x : x;
        return ImVec2(centre.x + (u - 12.0f) * unit, centre.y + (y - 12.0f) * unit);
    };
    const float stroke = 1.8f * unit;
    const ImVec2 head[] = {at(9, 14), at(4, 9), at(9, 4)};
    draw->AddPolyline(head, 3, colour, ImDrawFlags_RoundCornersAll, stroke);
    // The stem: left along the top, round the bend, back along the bottom.
    draw->PathLineTo(at(4, 9));
    constexpr float quarter = 1.5707963f;
    const ImVec2 bend = at(14.5f, 14.5f);
    if (redo) {
        draw->PathArcTo(bend, 5.5f * unit, quarter * 3, quarter, 16);  // through the left side
    } else {
        draw->PathArcTo(bend, 5.5f * unit, -quarter, quarter, 16);  // through the right side
    }
    draw->PathLineTo(at(11, 20));
    draw->PathStroke(colour, ImDrawFlags_None, stroke);
}

// Undo and Redo as icon buttons, a spacer, and Export as the only coloured button, centred in a
// header as tall as the design's. The buttons are ordinary ImGui buttons (focus, the disabled
// state and the clicks are ImGui's) drawn over with the icons.
void App::draw_panel_header(float width, float height) {
    const gui::TokenMetrics& m = token_metrics();
    const gui::TokenColours& c = token_colours();
    const auto vec = [](const gui::Rgba& rgba) { return ImVec4(rgba.r, rgba.g, rgba.b, rgba.a); };
    const auto u32 = [](const gui::Rgba& rgba) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(rgba.r, rgba.g, rgba.b, rgba.a));
    };
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float button = std::round(m.header_button);
    const float top = origin.y + std::round((height - hair() - button) * 0.5f);
    const float export_h = std::round(m.export_height);
    const float export_top = origin.y + std::round((height - hair() - export_h) * 0.5f);
    const std::string mod = ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd" : "Ctrl";

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(c.control_hovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec(c.control_active));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m.header_button_rounding);
    const auto icon_button = [&](const char* id, float x, bool redo, bool enabled, const std::string& tip) {
        ImGui::SetCursorScreenPos(ImVec2(x, top));
        ImGui::BeginDisabled(!enabled);
        const bool pressed = ImGui::Button(id, ImVec2(button, button));
        ImGui::EndDisabled();
        const ImVec2 lo = ImGui::GetItemRectMin();
        const ImVec2 hi = ImGui::GetItemRectMax();
        draw_history_icon(ImGui::GetWindowDrawList(), ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f),
                          m.header_icon, redo, u32(enabled ? c.text : c.faint));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tip.c_str());
        }
        return pressed;
    };
    const float left = origin.x + m.panel_pad.x;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    if (icon_button("##undo", left, false, session_.history().can_undo(), "Undo (" + mod + "+Z)")) {
        undo();
    }
    if (icon_button("##redo", left + button + gap, true, session_.history().can_redo(),
                    "Redo (Shift+" + mod + "+Z)")) {
        redo();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    // Export: accent fill, dark semi-bold label.
    const char* const label = "Export\xe2\x80\xa6";
    ImGui::PushFont(fonts().semibold, token_type().section);
    const float export_width = std::round(ImGui::CalcTextSize(label).x + 2 * m.export_pad_x);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - m.panel_pad.x - export_width, export_top));
    ImGui::PushStyleColor(ImGuiCol_Button, vec(c.accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(c.accent_active));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec(c.accent_active));
    ImGui::PushStyleColor(ImGuiCol_Text, vec(c.text_on_accent));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m.header_button_rounding);
    ImGui::BeginDisabled(dialog_open_ || exporter_.busy());
    if (ImGui::Button(label, ImVec2(export_width, export_h))) {
        begin_export();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::PopFont();

    // The hair line below the header, then on to the body.
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(origin.x, origin.y + height - hair()), ImVec2(origin.x + width, origin.y + height),
        u32(c.hair));
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
}

void App::draw_panel() {
    if (!session_.has_file()) {
        ImGui::TextDisabled("Open a photo to edit it.");
        return;
    }
    draw_scopes_panel();

    draw_presets();

    draw_light_path_panel();

    draw_sample_block();  // last, as the spec's section 4
}

void App::draw_scopes_panel() {
    ScopesData data;
    data.scope = prefs_.scope;
    data.theme = theme();
    // The viewed stage, a test strip band's title (whose histogram this is), and while framing that
    // the scopes measure the whole photo (#58).
    const std::string stage_name = histogram_stage_ != gui::final_stage
                                       ? std::string(gui::stage_info(histogram_stage_).name)
                                       : std::string();
    const std::string band = strip_ && strip_shown_band_ >= 0
                                 ? gui::strip_band_title(strip_->step,
                                                         static_cast<int>(strip_bands_.size()),
                                                         strip_shown_band_)
                                 : std::string();
    data.detail = gui::scopes_title_detail(stage_name, band, gui::scope_source(framing_.on()));
    data.clip_overlay_on = prefs_.clip_overlay;
    data.histogram = histogram_ ? &*histogram_ : nullptr;
    data.pixel_density = ImGui::GetIO().DisplayFramebufferScale.x;
    if (const gui::ClipReport* report = gui::clip_report_to_show(note_report_, strip_.has_value())) {
        data.clip_sentence = gui::make_clip_note(*report).full;
    }
    SDL_Texture* picture = nullptr;
    if (prefs_.scope == gui::ScopeChoice::histogram) {
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        picture = histogram_texture_.update(histogram_ ? *histogram_ : gui::StageHistogram{},
                                            scopes_histogram_size(width, data.pixel_density),
                                            gui::histogram_paint_colours(data.theme));
    } else {
        picture = scope_view_.texture(prefs_.scope, data.theme);
    }
    data.plot_texture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(picture));

    ScopesCallbacks cb;
    cb.choose_scope = [this](gui::ScopeChoice scope) {
        gui::Preferences p = prefs_;
        p.scope = scope;
        set_preferences(std::move(p));
    };
    cb.toggle_clip_overlay = [this] { toggle_clip_overlay(); };
    cb.explain = [this](std::string_view id) { explain(id); };
    cb.describe = [this](std::string_view id) { describe(id); };
    cb.hover = [this](std::string_view id) { explain_hover_ = std::string(id); };
    draw_scopes(data, cb);
}

// The Look section: its header (with "Reset to <default look>" at the right while the settings
// are not the default look), and one full-width dropdown. The dropdown shows the look's name, the
// loaded recipe ("Recipe: name"), or "Custom"; its list holds the looks, then the recipe actions.
void App::draw_presets() {
    const gui::InputKind kind = session_.kind();
    const gui::Preset* current = gui::matching_preset(kind, session_.settings());
    const gui::Preset& start = gui::default_preset(kind);

    const std::string reset_text = "Reset to " + std::string(start.name);
    const bool reset = section_text("Look", &start != current ? reset_text.c_str() : nullptr);
    if (&start != current) {
        describe(gui::explain::look_reset, reset);
    }
    if (reset) {
        gui::EditSettings s = session_.settings();
        gui::apply_preset(s, start);
        report(session_.set_settings(s));
    }

    // A loaded recipe names the button, unless the settings are exactly a built-in look the
    // recipe is not (a look picked after loading it).
    const std::optional<std::string> recipe = recipes_.line(session_.settings());
    const bool recipe_shown =
        recipe && (current == nullptr || session_.settings() == recipes_.current()->after);
    const std::string shown = recipe_shown ? "Recipe: " + *recipe
                              : current != nullptr ? std::string(current->name)
                                                   : "Custom";
    const gui::TokenMetrics& m = token_metrics();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(m.control_pad_x, std::max(0.0f, std::round((m.control_height -
                                                                          ImGui::GetFontSize()) * 0.5f))));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m.control_rounding);
    const bool open = styled_begin_combo("##preset", shown.c_str());
    ImGui::PopStyleVar(2);
    describe(gui::explain::look);
    if (open) {
        for (const gui::Preset& preset : gui::presets_for(kind)) {
            if (ImGui::Selectable(std::string(preset.name).c_str(), &preset == current)) {
                gui::EditSettings s = session_.settings();
                gui::apply_preset(s, preset);
                report(session_.set_settings(s));  // one undo step
            }
            describe(gui::explain_id(preset));
        }
        ImGui::Separator();
        ImGui::BeginDisabled(current == nullptr);
        const bool view = ImGui::Selectable("View recipe\xe2\x80\xa6");
        ImGui::EndDisabled();
        describe(gui::explain::look_view_recipe, view);
        if (view && current != nullptr) {
            look_recipe_ = gui::look_recipe_view(kind, *current);
            look_recipe_open_ = true;
            look_recipe_focus_ = true;
        }
        ImGui::BeginDisabled(!recipes_available());
        const bool load = ImGui::Selectable("Load recipe\xe2\x80\xa6");
        describe(gui::explain::recipe_load);
        const bool save = ImGui::Selectable("Save recipe\xe2\x80\xa6");
        describe(gui::explain::recipe_save);
        ImGui::EndDisabled();
        if (load) {
            show_recipe_load_dialog();
        } else if (save) {
            begin_save_recipe();
        }
        ImGui::EndCombo();
    }
    draw_recipe_line();
}

void App::draw_light_path_panel() {
    const std::string file_kind = gui::file_kind_label(session_.path());
    LightPathData data;
    data.view = gui::light_path_view({session_.settings(), session_.kind(), file_kind,
                                      viewed_stage_, compare_stage_,
                                      clips_ ? &*clips_ : nullptr, session_.as_shot_white(),
                                      brush_.strength()});
    data.theme = theme();
    data.strip_on = strip_.has_value();
    data.strip_available = gui::strip_available(viewed_stage_);
    data.strip_hint = gui::strip_unavailable_hint();
    data.pick_on = pick_.on();
    data.pick_available = gui::pick_available(viewed_stage_, compare_stage_);
    data.pick_hint = gui::view_hint(viewed_stage_, compare_stage_);
    data.brush_on = brush_.on();
    data.brush_available = gui::brush_available(viewed_stage_);
    data.brush_hint = gui::brush_unavailable_hint;
    data.easel_value = gui::easel_value(session_.settings().frame, session_.framed_file_size(),
                                        session_.file_size_exact());
    data.framing = framing_.on();
    data.easel_available = session_.has_file();

    LightPathCallbacks cb;
    cb.toggle_framing = [this] { toggle_framing(); };
    cb.view_stage = [this](gui::StageId stage) { viewed_stage_ = stage; };
    cb.set_stage_on = [this](gui::StageId stage, bool on) {
        gui::EditSettings s = session_.settings();
        gui::set_stage_enabled(s, stage, on);
        report(session_.set_settings(s));  // one undo step in History
    };
    cb.slider = [this](gui::SimpleControl control, float position, const gui::SliderFrame& frame) {
        gui::EditSettings edited = session_.settings();
        gui::apply_simple_control(edited, control, position);
        // A drag updates the preview every frame but becomes one undo step, on release.
        report(gui::apply_slider_frame(session_, frame, edited));
    };
    cb.contrast_slider = [this](std::optional<float> contrast, const gui::SliderFrame& frame) {
        gui::EditSettings edited = session_.settings();
        if (contrast) {
            gui::apply_contrast(edited, *contrast);
        }
        report(gui::apply_slider_frame(session_, frame, edited));
    };
    cb.reset_slider = [this](gui::SimpleControl control) {
        gui::EditSettings s = session_.settings();
        gui::reset_simple_control(s, control);
        report(session_.set_settings(s));
    };
    cb.white_balance_slider = [this](gui::WhiteBalanceControl control, std::optional<double> value,
                                     const gui::SliderFrame& frame) {
        gui::EditSettings edited = session_.settings();
        if (value) {
            gui::set_white_balance(edited, control, *value, session_.as_shot_white());
        }
        report(gui::apply_slider_frame(session_, frame, edited));
    };
    cb.reset_white_balance = [this] {
        gui::EditSettings s = session_.settings();
        gui::reset_white_balance(s);
        report(session_.set_settings(s));
    };
    cb.set_curve_kind = [this](gui::CurveKind kind) {
        gui::EditSettings s = session_.settings();
        gui::set_curve_kind(s, kind);
        report(session_.set_settings(s));
    };
    cb.curve_parameter = [this](float value, const gui::SliderFrame& frame) {
        gui::EditSettings edited = session_.settings();
        gui::set_curve_parameter(edited, value);
        report(gui::apply_slider_frame(session_, frame, edited));
    };
    cb.reset_curve_parameter = [this] {
        gui::EditSettings s = session_.settings();
        if (const std::optional<gui::CurveParameter> p = gui::curve_parameter(s.curve)) {
            gui::set_curve_parameter(s, p->default_value);
            report(session_.set_settings(s));
        }
    };
    cb.toggle_strip = [this] { toggle_test_strip(); };
    cb.toggle_pick = [this] { toggle_pick(); };
    cb.toggle_brush = [this] { toggle_brush(); };
    cb.brush_strength = [this](float strength) { brush_.set_strength(strength); };
    cb.clear_strokes = [this] { clear_strokes(); };
    cb.set_compare = [this](std::optional<gui::StageId> stage) {
        if (stage && gui::framing_leaves(framing_.on(), gui::FramingEvent::compare_chosen)) {
            leave_framing();  // the comparison is of the framed picture
        }
        compare_stage_ = stage;
        if (stage) {
            strip_.reset();  // a comparison and a strip don't mix
        }
    };
    cb.swap_compare = [this] {
        if (const std::optional<gui::StageId> shown =
                gui::effective_compare(viewed_stage_, compare_stage_)) {
            if (gui::framing_leaves(framing_.on(), gui::FramingEvent::compare_chosen)) {
                leave_framing();
            }
            compare_stage_ = viewed_stage_;
            viewed_stage_ = *shown;
            strip_.reset();
        }
    };
    cb.explain = [this](std::string_view id, bool changed) { explain(id, changed); };
    cb.describe = [this](std::string_view id) { describe(id); };
    cb.hover = [this](std::string_view id) { explain_hover_ = std::string(id); };
    draw_light_path(data, cb);
}

void App::draw_status_bar(float height) {
    // Its own surface (chrome) with a hair line above; the text sits centred in it.
    const gui::TokenColours& colours = token_colours();
    const ImVec2 top_left = ImGui::GetCursorScreenPos();
    const float window_width = ImGui::GetWindowWidth();
    const auto u32 = [](const gui::Rgba& c) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    };
    ImGui::GetWindowDrawList()->AddRectFilled(top_left, ImVec2(top_left.x + window_width, top_left.y + height),
                                              u32(colours.chrome));
    ImGui::GetWindowDrawList()->AddRectFilled(top_left, ImVec2(top_left.x + window_width, top_left.y + hair()),
                                              u32(colours.hair));
    const float pad = token_metrics().panel_pad.x;
    ImGui::SetCursorScreenPos(ImVec2(
        top_left.x + pad, top_left.y + std::round((height + hair() - ImGui::GetTextLineHeight()) * 0.5f)));
    if (!session_.has_file()) {
        ImGui::TextDisabled("No photo open");
        return;
    }
    const bool raw = session_.kind() == gui::InputKind::raw;
    const std::string separator = " \xc2\xb7 ";
    const std::string size_text = std::to_string(session_.decoded_width()) + " \xc3\x97 " +
                                  std::to_string(session_.decoded_height());
    const std::string details = separator + size_text + separator + gui::file_kind_label(session_.path());
    const std::string camera = gui::camera_line(session_.photo_info());
    const auto measure = [](std::string_view s) {
        return ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
    };
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float total = std::max(1.0f, window_width - 2 * pad);
    const float note_natural = clip_note_ ? clip_note_width(*clip_note_) : 0.0f;
    const float note_room = clip_note_ ? std::min(note_natural, total * 0.5f) + gap : 0.0f;
    const std::string name = file_name(session_.path());

    // The camera group sits at the bar's centre; it is shown only when it clears the whole left
    // group and the room kept for the clip note on both sides, so that it never overlaps either.
    const float camera_width = camera.empty() ? 0.0f : measure(camera);
    const float camera_x = std::round((window_width - camera_width) * 0.5f);
    // The room kept for the note is that of the longest text it can have, not of the text it has
    // now: whether the camera line shows must not change when a render changes the note.
    constexpr std::string_view widest_note = "99% highlights clipped at Output encode  +3 more";
    const float note_reserved =
        2 * token_metrics().dot_radius + token_metrics().dot_gap + measure(widest_note);
    const bool show_camera =
        !camera.empty() && camera_x >= pad + measure(name + details) + gap * 2 &&
        camera_x + camera_width <= window_width - pad - note_reserved - gap * 2;

    // The file name gives way first (shortened in the middle, keeping its extension), so the line
    // never runs off the window's edge or into the camera group.
    const float left_room =
        show_camera ? camera_x - pad - gap : std::max(1.0f, total - note_room);
    const float name_width = std::max(left_room - measure(details), left_room * 0.3f);
    const std::string status = gui::ellipsize_middle(
        gui::ellipsize_middle(name, name_width, measure) + details, left_room, measure);
    ImGui::TextDisabled("%s", status.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        std::string tip = utf8(session_.path());
        if (texture_.texture() != nullptr) {
            tip += "\nPreview " + std::to_string(texture_.width()) + " \xc3\x97 " +
                   std::to_string(texture_.height()) + ", " +
                   std::to_string(static_cast<int>(std::lround(last_render_ms_))) + " ms";
        }
        if (raw) {
            tip += "\nRAW, half-size preview";
        }
        ImGui::SetTooltip("%s", tip.c_str());
    }
    if (show_camera) {
        ImGui::SetCursorScreenPos(ImVec2(top_left.x + camera_x, ImGui::GetItemRectMin().y));
        ImGui::TextDisabled("%s", camera.c_str());
        const std::string tip = gui::camera_tooltip(session_.photo_info());
        if (!tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    if (clip_note_) {
        draw_clip_note(note_room);
    }
}

float App::clip_note_width(const gui::ClipNote& note) const {
    const float dot = note.kind == gui::ClipNoteKind::none
                          ? 0.0f
                          : 2 * token_metrics().dot_radius + token_metrics().dot_gap;
    return dot + ImGui::CalcTextSize(note.text.c_str()).x;
}

// Right-aligned on the status bar's line: a dot in the colour of what clipped (as in the Light path
// rows), then the short text; hover for the full sentence. Muted while it is not for the settings
// on screen yet.
void App::draw_clip_note(float room) {
    const gui::ClipNote& note = *clip_note_;
    const Palette& colours = palette(theme());
    const gui::TokenMetrics& m = token_metrics();
    const bool fresh = last_frame_had_clips_ && !strip_ && !preview_.rendering();
    const float natural = clip_note_width(note);
    const float width = std::min(natural, std::max(1.0f, room - ImGui::GetStyle().ItemSpacing.x));
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                             ImGui::GetWindowWidth() - m.panel_pad.x - width));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##clip_note", ImVec2(width, ImGui::GetTextLineHeight()));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float text_x = at.x;
    if (note.kind != gui::ClipNoteKind::none) {
        const ImU32 dot = note.kind == gui::ClipNoteKind::highlights ? colours.clip_highlights_fill
                          : note.kind == gui::ClipNoteKind::shadows  ? colours.clip_shadows_fill
                                                                     : colours.clip_gamut_fill;
        draw->AddCircleFilled(ImVec2(at.x + m.dot_radius, at.y + ImGui::GetTextLineHeight() * 0.5f),
                              m.dot_radius, dot);
        text_x += 2 * m.dot_radius + m.dot_gap;
    }
    const auto measure = [](std::string_view s) {
        return ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
    };
    const std::string text = gui::ellipsize_middle(note.text, std::max(1.0f, at.x + width - text_x), measure);
    // Quiet until it matters: muted text for "No clipping" and while stale, the body colour when
    // something clipped and the note is current.
    const bool loud = fresh && note.kind != gui::ClipNoteKind::none;
    draw->AddText(ImVec2(text_x, at.y),
                  ImGui::GetColorU32(loud ? ImGuiCol_Text : ImGuiCol_TextDisabled), text.c_str());
    explain(gui::explain::clip_note);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(m.tooltip_wrap);
        ImGui::TextUnformatted(gui::clip_note_tooltip(note, strip_.has_value()).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Called inside a window's Begin/End: remembers the window's open flag if it, or one of its child
// windows, has the focus, for next frame's Esc.
void App::note_if_focused(bool* open_flag) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        focused_window_this_frame_ = open_flag;
    }
}

void App::draw_preferences() {
    if (!prefs_open_) {
        return;
    }
    // A normal window, not modal: the photo stays editable, and every change shows at once.
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Preferences", &prefs_open_,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    note_if_focused(&prefs_open_);
    gui::Preferences p = prefs_;

    section_text("Appearance");
    int theme = static_cast<int>(p.theme);
    styled_radio("System", &theme, static_cast<int>(gui::ThemeChoice::system));
    describe(gui::explain::pref_theme);
    ImGui::SameLine();
    styled_radio("Light", &theme, static_cast<int>(gui::ThemeChoice::light));
    describe(gui::explain::pref_theme);
    ImGui::SameLine();
    styled_radio("Dark", &theme, static_cast<int>(gui::ThemeChoice::dark));
    describe(gui::explain::pref_theme);
    p.theme = static_cast<gui::ThemeChoice>(theme);

    section_text("Interface size");
    const std::string automatic =
        "Auto (" + std::to_string(static_cast<int>(std::lround(display_scale_ * 100))) + "%)";
    const std::string current = p.interface_size == gui::interface_size_auto
                                    ? automatic
                                    : std::to_string(p.interface_size) + "%";
    ImGui::SetNextItemWidth(token_metrics().prefs_control_width);
    if (styled_begin_combo("##size", current.c_str())) {
        if (ImGui::Selectable(automatic.c_str(), p.interface_size == gui::interface_size_auto)) {
            p.interface_size = gui::interface_size_auto;
        }
        for (int size = gui::interface_size_min; size <= gui::interface_size_max;
             size += gui::interface_size_step) {
            if (ImGui::Selectable((std::to_string(size) + "%").c_str(),
                                  p.interface_size == size)) {
                p.interface_size = size;
            }
        }
        ImGui::EndCombo();
    }
    describe(gui::explain::pref_size);
    ImGui::SameLine();
    ImGui::TextDisabled("Auto follows the display.");

    section_text("Layout");
    // Label and value on one line over the slider, as for the photo's sliders.
    // As wide as the window's widest row (the size combo and its hint), not just the combo.
    const float slider_width =
        token_metrics().prefs_control_width + ImGui::GetStyle().ItemSpacing.x +
        ImGui::CalcTextSize("Auto follows the display.").x;
    char width_text[16];
    std::snprintf(width_text, sizeof width_text, "%d pt", p.panel_width);
    ImGui::TextUnformatted("Panel width");
    ImGui::SameLine(ImGui::GetCursorPosX() + slider_width - ImGui::CalcTextSize(width_text).x);
    ImGui::TextUnformatted(width_text);
    ImGui::SetNextItemWidth(slider_width);
    int width = p.panel_width;
    const int panel_min_points =
        gui::widen_panel_width(gui::panel_width_min, ui_scale(), panel_text_min_);
    if (styled_slider_int("##panel_width", &width, panel_min_points, gui::panel_width_max,
                          gui::panel_width_default, ImGuiSliderFlags_AlwaysClamp)) {
        p.panel_width = gui::widen_panel_width(width, ui_scale(), panel_text_min_);
    }
    describe(gui::explain::pref_panel);
    const bool dragging = ImGui::IsItemActive();
    const bool released = ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::Button("Reset layout")) {
        p.panel_width = gui::panel_width_default;
    }
    ImGui::TextDisabled("You can also drag the panel's left edge.");

    section_text("Help");
    styled_checkbox("Show explanations", &p.show_explanations);
    describe(gui::explain::pref_explain);

    ImGui::Separator();
    if (ImGui::Button("Restore defaults")) {
        gui::Preferences defaults;
        defaults.unknown = p.unknown;  // keep what a newer version of Prism wrote
        defaults.recipe_folder = p.recipe_folder;  // not shown here: where recipes are kept
        p = defaults;
    }

    if (dragging) {
        prefs_.panel_width = p.panel_width;  // live while dragging; saved once, on release
    } else if (released) {
        prefs_.panel_width = p.panel_width;
        save_preferences();
    }
    set_preferences(std::move(p));
    ImGui::End();
}

void App::draw_about() {
    if (!about_open_) {
        return;
    }
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("About Prism", &about_open_,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    note_if_focused(&about_open_);
    // Wrap long lines at a readable width rather than the window's (it sizes itself to them).
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + token_metrics().about_wrap);
    ImGui::Text("Prism %.*s", static_cast<int>(version().size()), version().data());

    section_text("Licence");
    ImGui::TextWrapped("PolyForm Noncommercial License 1.0.0");
    const std::string_view notice = required_notice();
    ImGui::TextWrapped("%.*s", static_cast<int>(notice.size()), notice.data());
    ImGui::TextWrapped(
        "The full terms are in the LICENSE.md file that comes with Prism, and at "
        "https://polyformproject.org/licenses/noncommercial/1.0.0");

    section_text("Third-party components");
    if (ImGui::BeginTable("components", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const ThirdPartyComponent& c : third_party_components()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.name.data(), c.name.data() + c.name.size());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%.*s", static_cast<int>(c.licence.size()), c.licence.data());
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Third-party notices…")) {
        notices_open_ = true;
        ImGui::SetWindowFocus("Third-party notices");
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
}

void App::draw_notices() {
    if (!notices_open_) {
        return;
    }
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(token_metrics().notices_window.x, token_metrics().notices_window.y), ImGuiCond_Appearing);
    if (!ImGui::Begin("Third-party notices", &notices_open_,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    note_if_focused(&notices_open_);
    const std::string_view text = third_party_notices();
    ImGui::BeginChild("text", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::EndChild();
    ImGui::End();
}

void App::draw_saving_popup() {
    // Modal while the export runs: it decodes the file again at full size, which takes seconds.
    if (exporter_.busy() && !ImGui::IsPopupOpen("Saving")) {
        ImGui::OpenPopup("Saving");
    }
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Saving", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (!exporter_.busy()) {
            ImGui::CloseCurrentPopup();
        }
        // A quit or an open waiting for this export: say so. Cancel then drops the quit or open
        // and lets the export finish; otherwise it cancels the export.
        const bool waiting = close_pending_ && close_phase_ == ClosePhase::waiting;
        const std::string_view text =
            waiting ? gui::waiting_text(*close_pending_) : std::string_view("Saving…");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + token_metrics().prompt_wrap);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(token_metrics().progress_width, 0),
                           "");
        if (ImGui::Button("Cancel") || (waiting && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
            if (waiting) {
                apply_close_step(gui::CloseStep::cancel);
            } else {
                exports_.cancel();
            }
        }
        ImGui::EndPopup();
    }
}

// "You have edits that haven't been exported or saved as a recipe.", what was saved last, and
// Export…, Save Recipe…, Quit/Open Without Saving and Cancel in the platform's order. Modal;
// Esc is Cancel; Export… is the default (Enter).
void App::draw_close_prompt() {
    const bool asking = close_pending_ && close_phase_ == ClosePhase::asking;
    if (asking && !ImGui::IsPopupOpen("close_prompt")) {
        ImGui::OpenPopup("close_prompt");
    }
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("close_prompt", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        return;
    }
    if (!asking) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const bool has_strokes = !session_.settings().strokes.empty();
    const bool has_frame = session_.settings().frame.has_value();
    const gui::PromptText text = gui::prompt_text(*close_pending_, has_strokes, has_frame);
    const std::string detail = gui::prompt_detail(baseline_.last_save());
    const float wrap = token_metrics().prompt_wrap;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text.message.data(), text.message.data() + text.message.size());
    if (!text.strokes_note.empty()) {
        ImGui::TextUnformatted(text.strokes_note.data(),
                               text.strokes_note.data() + text.strokes_note.size());
    }
    if (!detail.empty()) {
        ImGui::TextDisabled("%s", detail.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const auto label = [&](gui::PromptChoice c) {
        switch (c) {
            case gui::PromptChoice::export_first: return std::string(text.export_button);
            case gui::PromptChoice::save_recipe: return std::string(text.recipe_button);
            case gui::PromptChoice::discard: return std::string(text.discard_button);
            case gui::PromptChoice::cancel: return std::string(text.cancel_button);
        }
        return std::string();
    };
    const gui::PromptButtons buttons =
        gui::prompt_buttons(ImGui::GetIO().ConfigMacOSXBehaviors, has_strokes || has_frame);
    const ImGuiStyle& style = ImGui::GetStyle();
    float widths[4];
    float packed = 0;
    for (int i = 0; i < buttons.count; ++i) {
        widths[i] = ImGui::CalcTextSize(label(buttons.order[i]).c_str()).x + 2 * style.FramePadding.x;
        packed += widths[i] + (i > 0 ? style.ItemSpacing.x : 0.0f);
    }
    // With a gap, the row is as wide as the text above it (or the buttons and a clear gap), and
    // the buttons after the gap sit at its right end.
    const float row = std::max(wrap, packed + 3 * style.ItemSpacing.x);
    float right_group = 0;
    for (int i = buttons.gap_after; i < buttons.count && buttons.gap_after > 0; ++i) {
        right_group += widths[i] + (i > buttons.gap_after ? style.ItemSpacing.x : 0.0f);
    }
    const float row_x = ImGui::GetCursorPosX();
    std::optional<gui::PromptChoice> choice;
    for (int i = 0; i < buttons.count; ++i) {
        if (i > 0) {
            if (buttons.gap_after > 0 && i == buttons.gap_after) {
                ImGui::SameLine(row_x + row - right_group);
            } else {
                ImGui::SameLine();
            }
        }
        const gui::PromptChoice c = buttons.order[i];
        if (ImGui::Button(label(c).c_str())) {
            choice = c;
        }
        if (c == gui::PromptChoice::export_first) {
            ImGui::SetItemDefaultFocus();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        choice = gui::PromptChoice::cancel;
    }
    if (choice) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    if (choice) {
        apply_close_step(gui::prompt_choice(*choice));
    }
}

}  // namespace prism::gui_app


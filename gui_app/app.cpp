#include "app.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>

#include "about_info.hpp"
#include "imgui.h"
#include "imgui_internal.h"  // ClearActiveID: Esc leaves a text field without undoing the typing
#include "macos_menu.hpp"
#include "theme.hpp"
#include "widgets.hpp"
#include "prism/gui/explanations.hpp"
#include "prism/gui/file_types.hpp"
#include "prism/gui/layout.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/slider_gesture.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/gui/test_strip.hpp"
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

}  // namespace

App::App(SDL_Window* window, SDL_Renderer* renderer, DialogQueue& dialogs,
         fs::path preferences_file)
    : window_(window),
      dialogs_(dialogs),
      texture_(renderer),
      scope_view_(renderer),
      prefs_file_(std::move(preferences_file)),
      display_scale_(content_scale(window)) {
    if (!prefs_file_.empty()) {
        prefs_ = gui::load_preferences(prefs_file_);  // the defaults if missing or unreadable
    }
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
    // Small enough for a laptop at 200%, big enough that the panel and canvas both fit. Window
    // coordinates are points on macOS but pixels on Windows, hence the content scale.
    SDL_SetWindowMinimumSize(window_, static_cast<int>(640 * display_scale_),
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
            show_save_dialog();
            if (!dialog_open_) {
                apply_close_step(gui::CloseStep::cancel);  // the dialog could not open
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
    baseline_.opened(session_.settings());
    recipes_.photo_opened();
    texture_.reset();  // don't show the previous photo under the new name
    histogram_.reset();
    clips_.reset();
    clip_note_.reset();
    note_report_.reset();
    last_frame_had_clips_ = false;
    clip_mask_.clear();
    texture_.reset_overlay();
    scope_view_.set(std::nullopt);
    viewed_stage_ = gui::final_stage;
    compare_stage_.reset();
    strip_.reset();
    strip_bands_.clear();
    explain_last_.clear();
    SDL_SetWindowTitle(window_, ("Prism - " + file_name(path)).c_str());
}

void App::show_open_dialog() {
    if (dialog_open_ || recipe_sheet_) {
        return;
    }
    dialog_open_ = true;
    const fs::path folder = session_.has_file() ? session_.path().parent_path() : fs::path();
    gui_app::show_open_dialog(window_, dialogs_, folder);
}

void App::show_save_dialog() {
    if (dialog_open_ || recipe_sheet_ || !session_.has_file() || exporter_.busy()) {
        return;
    }
    dialog_open_ = true;
    const gui::ExportFormat format = gui::default_export_format(session_.path());
    gui_app::show_save_dialog(window_, dialogs_,
                              gui::suggested_export_path(session_.path(), format), format);
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
    exporter_.start(session_.export_job(), output);
    if (for_close) {
        apply_close_step(gui::save_dialog_closed(true));
    }
}

bool App::recipes_available() const {
    return session_.has_file() && !dialog_open_ && !recipe_sheet_ && !exporter_.busy() &&
           !close_pending_;
}

// Save Recipe...: the sheet first, with the name and description Prism suggests.
void App::begin_save_recipe(bool for_close) {
    // The close prompt's Save Recipe... comes while a quit or open is pending, which otherwise
    // keeps recipes unavailable.
    if (for_close ? (!session_.has_file() || dialog_open_ || recipe_sheet_ || exporter_.busy())
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

// The save sheet: Name, Description, what is saved, Save... and Cancel. Modal. Esc cancels when no
// text field has the keyboard; inside a field the first Esc only leaves the field. Enter in a
// field is Save....
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
    ImGui::TextUnformatted(
        "Saves all five stages. The photo and RAW decoding options aren't included.");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const bool save = ImGui::Button("Save...") || enter;
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel") || (escape && !field_active);
    if (save || cancel) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();

    if (save) {
        // The recipe is made now, from what is on screen as Save... is pressed.
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

void App::draw_recipe_buttons() {
    ImGui::BeginDisabled(!recipes_available());
    const bool load = ImGui::SmallButton("Load recipe...");
    describe(gui::explain::recipe_load);
    ImGui::SameLine();
    const bool save = ImGui::SmallButton("Save recipe...");
    describe(gui::explain::recipe_save);
    ImGui::EndDisabled();
    if (load) {
        show_recipe_load_dialog();
    } else if (save) {
        begin_save_recipe();
    }
}

void App::draw_recipe_line() {
    const std::optional<std::string> line = recipes_.line(session_.settings());
    if (!line) {
        return;
    }
    const gui::LoadedRecipe& loaded = *recipes_.current();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(("Recipe: " + *line).c_str());
    explain(gui::explain::recipe_loaded);
    if (!loaded.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", loaded.description.c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(gui::stages_line(loaded.stages).c_str());
    ImGui::PopStyleColor();
    explain(gui::explain::recipe_loaded);
    ImGui::PopTextWrapPos();
}

void App::take_export_result() {
    std::optional<gui::ExportResult> result = exporter_.poll();
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

void App::update_preview() {
    // Scopes, the per-stage clipping and the clip mask are only computed while they are on screen
    // (the pipeline view open, the overlay on); the histogram comes with every frame.
    if (strip_ && !gui::strip_available(viewed_stage_)) {
        strip_.reset();  // the Decode stage shows no edit, so there is nothing to vary
    }
    gui::PreviewOptions options;
    options.scopes = prefs_.scope != gui::ScopeChoice::histogram;
    // The pipeline view needs the report every frame. For the status bar's note it is enough once
    // the picture settles: not while a control is being dragged (that is what made the report
    // too dear for every drag frame), but at once for any other change and when a photo opens.
    options.clip_report = (pipeline_open_ || !ImGui::IsAnyItemActive()) && !strip_;  // not with a strip
    options.clip_mask = prefs_.clip_overlay;
    options.compare_stage = strip_ ? std::nullopt : gui::effective_compare(viewed_stage_, compare_stage_);
    options.test_strip = strip_;
    preview_.update(session_.preview_base(), session_.settings(), viewed_stage_, options);
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
    if (session_.history().can_undo()) {
        report(session_.undo());
    }
}

void App::redo() {
    if (session_.history().can_redo()) {
        report(session_.redo());
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
    context.busy = close_pending_.has_value() || recipe_sheet_.has_value() || dialog_open_ ||
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
    return quiet_frames_ < settle_frames || preview_.rendering() || pending_open_.has_value() ||
           exporter_.busy() || close_pending_.has_value();
}

void App::frame() {
    ++quiet_frames_;
    take_dialog_results();
    // The "Opening ..." frame was presented: now decode (this frame shows the result).
    if (pending_open_ && pending_open_shown_) {
        const fs::path path = std::move(*pending_open_);
        pending_open_.reset();
        open_now(path);
    }
    take_export_result();
    explain_hover_.clear();
    explain_focus_.clear();
    handle_shortcuts();
    update_preview();

    // One window covering the whole SDL window, without a title bar: the app's main view.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Prism", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);

    draw_menu();

    const float status_height = ImGui::GetFrameHeightWithSpacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float body_height = std::max(1.0f, avail.y - status_height);
    // The splitter between canvas and panel: as wide as the usual gap between items.
    const float splitter = ImGui::GetStyle().ItemSpacing.x;
    const float panel =
        gui::panel_width_pixels(static_cast<float>(prefs_.panel_width), ui_scale(), avail.x);
    const float canvas = std::max(1.0f, avail.x - panel - splitter);

    // The canvas column: the test strip's bar (when on) above the canvas.
    ImGui::BeginGroup();
    float bar_height = 0;
    if (strip_) {
        const float top = ImGui::GetCursorPosY();
        draw_strip_bar(canvas);
        bar_height = ImGui::GetCursorPosY() - top;
    }
    ImGui::BeginChild("canvas", ImVec2(canvas, std::max(1.0f, body_height - bar_height)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    draw_canvas();
    draw_banner();
    draw_stage_badge();
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 0.0f);
    draw_splitter(canvas, splitter, body_height, avail.x);
    ImGui::SameLine(0.0f, 0.0f);
    draw_panel_column(body_height);

    draw_status_bar();
    draw_saving_popup();
    draw_close_prompt();
    draw_recipe_sheet();
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
    explain_shown_ = !explain_hover_.empty() ? explain_hover_
                     : !explain_focus_.empty() ? explain_focus_
                                               : explain_last_;
    popup_open_last_frame_ =
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    item_active_last_frame_ = ImGui::IsAnyItemActive();
    focused_window_last_frame_ = focused_window_this_frame_;
}

void App::step_interface_size(int direction) {
    gui::Preferences p = prefs_;
    p.interface_size = direction == 0 ? gui::interface_size_auto
                                      : gui::step_interface_size(p.interface_size, direction,
                                                                 display_scale_);
    set_preferences(std::move(p));
}

void App::handle_shortcuts() {
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
        show_save_dialog();
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
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        gui::EscapeContext esc;
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
            case gui::EscapeAction::none:
                break;
        }
    }
    // Not in the middle of a drag: History would end the drag's step, but the slider would keep
    // sending values that each became a step of their own.
    if (ImGui::IsAnyItemActive()) {
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
}

void App::draw_menu() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", shortcut_label("O").c_str(), false, !dialog_open_)) {
            show_open_dialog();
        }
        if (ImGui::MenuItem("Export...", shortcut_label("E").c_str(), false,
                            session_.has_file() && !dialog_open_ && !exporter_.busy())) {
            show_save_dialog();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Load Recipe...", shortcut_label("Shift+O").c_str(), false,
                            recipes_available())) {
            show_recipe_load_dialog();
        }
        if (ImGui::MenuItem("Save Recipe...", shortcut_label("Shift+S").c_str(), false,
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
            if (ImGui::MenuItem("Preferences...", shortcut_label(",").c_str())) {
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
}

void App::draw_canvas() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const Palette& colours = palette(theme());
    draw->AddRectFilled(min, max, colours.canvas);  // neutral grey in both themes
    const float margin = token_metrics().canvas_margin;

    if (texture_.texture() != nullptr) {
        // Fit the whole image into the canvas, keeping its aspect ratio, centred (letterboxed).
        const float scale = std::min(size.x / static_cast<float>(texture_.width()),
                                     size.y / static_cast<float>(texture_.height()));
        const ImVec2 shown(texture_.width() * scale, texture_.height() * scale);
        const ImVec2 at(min.x + (size.x - shown.x) / 2, min.y + (size.y - shown.y) / 2);
        update_clip_overlay_texture();
        if (splitting()) {
            draw_split(draw, at, shown);
        } else {
            draw->AddImage(ImTextureRef(static_cast<ImTextureID>(
                               reinterpret_cast<std::uintptr_t>(texture_.texture()))),
                           at, ImVec2(at.x + shown.x, at.y + shown.y));
            if (texture_.overlay() != nullptr && prefs_.clip_overlay &&
                clip_mask_width_ == texture_.width() && clip_mask_height_ == texture_.height()) {
                draw->AddImage(ImTextureRef(static_cast<ImTextureID>(
                                   reinterpret_cast<std::uintptr_t>(texture_.overlay()))),
                               at, ImVec2(at.x + shown.x, at.y + shown.y));
            }
            if (strip_ && !strip_bands_.empty()) {
                draw_strip_bands(draw, at, shown);
            }
        }
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
        const float button = ImGui::CalcTextSize("Open photo...").x +
                             ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetCursorPosX((size.x - button) / 2);
        if (ImGui::Button("Open photo...")) {
            show_open_dialog();
        }
        ImGui::TextUnformatted("");
        ImGui::PushStyleColor(ImGuiCol_Text, colours.canvas_text);
        centred_text(raw_support_available() ? "PNG, JPEG or RAW" : "PNG or JPEG");
        ImGui::PopStyleColor();
    }

    if (pending_open_) {
        overlay_text(draw, colours, ImVec2(min.x + margin, max.y - margin - 2 * ImGui::GetFontSize()),
                     "Opening " + file_name(*pending_open_) + "...");
    } else if (preview_.rendering()) {
        overlay_text(draw, colours, ImVec2(min.x + margin, max.y - margin - 2 * ImGui::GetFontSize()),
                     "Rendering...");
    }
    if (drag_over_ && session_.has_file()) {
        draw->AddRect(min, max, colours.drop_highlight, 0.0f, 0,
                  token_metrics().drop_line_active);
    }
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
    gui::TestStrip strip;
    strip.step = strip_step_;
    strip_ = strip;
    compare_stage_.reset();  // a comparison and a strip don't mix
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

    // One hit area over the whole picture; the band is found from the cursor.
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(at);
    const bool clicked =
        ImGui::InvisibleButton("##strip_bands", ImVec2(std::max(1.0f, shown.x), std::max(1.0f, shown.y)));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(saved);
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
        const auto [min, max] = screen(strip_bands_[static_cast<std::size_t>(*hot)].rect);
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
        const auto [min, max] = screen(r);
        const std::string label = gui::strip_label(strip_->step, count, i);
        const ImVec2 text = ImGui::CalcTextSize(label.c_str());
        const ImVec2 chip_min(min.x + metrics.strip_chip_margin, min.y + metrics.strip_chip_margin);
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

// The Test strip toggle that sits beside Brightness, in Adjust and in the Exposure stage's row.
void App::draw_strip_button() {
    const bool on = strip_.has_value();
    const bool available = gui::strip_available(viewed_stage_);
    const float line_height = ImGui::GetFrameHeight();
    ImGui::PushFont(nullptr, token_type().small);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(token_metrics().selector_pad, ImGui::GetStyle().FramePadding.y * 0.5f));
    // Centred on the line of the label beside it (as tall as a frame).
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (line_height - ImGui::GetFrameHeight()) / 2);
    ImGui::BeginDisabled(!on && !available);
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button("Test strip")) {
        toggle_test_strip();
    }
    if (on) {
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopFont();
    if (!on && !available) {
        explain(gui::explain::strip);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", gui::strip_unavailable_hint());
        }
    } else {
        describe(gui::explain::strip);
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
// it, so they meet exactly.
void App::draw_split(ImDrawList* draw, ImVec2 at, ImVec2 shown) {
    const Palette& colours = palette(theme());
    const float scale = ui_scale();
    const gui::SplitLayout split = gui::split_layout(at.x, shown.x, split_fraction_);
    const float top = at.y;
    const float bottom = at.y + shown.y;
    const auto id = [](SDL_Texture* t) {
        return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(t)));
    };
    draw->AddImage(id(texture_.compare_texture()), ImVec2(split.left_x0, top),
                   ImVec2(split.divider_x, bottom), ImVec2(0.0f, 0.0f),
                   ImVec2(split.u_divider, 1.0f));
    draw->AddImage(id(texture_.texture()), ImVec2(split.divider_x, top),
                   ImVec2(split.right_x1, bottom), ImVec2(split.u_divider, 0.0f),
                   ImVec2(1.0f, 1.0f));
    // The clipping overlay belongs to the viewed stage: only over its half.
    if (texture_.overlay() != nullptr && prefs_.clip_overlay &&
        clip_mask_width_ == texture_.width() && clip_mask_height_ == texture_.height()) {
        draw->AddImage(id(texture_.overlay()), ImVec2(split.divider_x, top),
                       ImVec2(split.right_x1, bottom), ImVec2(split.u_divider, 0.0f),
                       ImVec2(1.0f, 1.0f));
    }

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
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    const float strip = metrics.divider_strip;
    ImGui::SetCursorScreenPos(ImVec2(split.divider_x - strip / 2, top));
    ImGui::InvisibleButton("##split_divider", ImVec2(strip, std::max(1.0f, shown.y)));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
        split_fraction_ = gui::split_fraction_from_x(ImGui::GetIO().MousePos.x, at.x, shown.x);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive()) {
        ImGui::SetTooltip("Drag to move the divider");
    }
    ImGui::SetCursorScreenPos(saved);

    // One chip in each half's top corner, named by the stage each picture really shows.
    const float margin = metrics.divider_chip_margin;
    const std::string left_text = gui::stage_caption(texture_.compare_stage());
    const std::string right_text = gui::stage_caption(histogram_stage_);
    const float pad = metrics.overlay_padding;
    draw->PushClipRect(ImVec2(split.left_x0, top), ImVec2(split.divider_x, bottom), true);
    overlay_text(draw, colours, ImVec2(split.left_x0 + margin, top + margin), left_text);
    draw->PopClipRect();
    const float right_width = ImGui::CalcTextSize(right_text.c_str()).x + 2 * pad;
    draw->PushClipRect(ImVec2(split.divider_x, top), ImVec2(split.right_x1, bottom), true);
    overlay_text(draw, colours, ImVec2(split.right_x1 - margin - right_width, top + margin),
                 right_text);
    draw->PopClipRect();
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
    ImGui::PopStyleColor(3);
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
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void App::draw_splitter(float canvas_width, float width, float height, float available) {
    ImGui::InvisibleButton("splitter", ImVec2(width, height));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
        // The panel is what lies right of the mouse; panel_width_pixels clamps it next frame.
        const float panel = available - (canvas_width + ImGui::GetIO().MouseDelta.x) - width;
        prefs_.panel_width = gui::panel_points_from_pixels(panel, ui_scale());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive()) {
        ImGui::SetTooltip("Drag to resize the panel");
    }
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
    context.clips = note_report_ ? &*note_report_ : nullptr;
    context.strip_step = strip_ ? strip_->step : strip_step_;
    context.recipe = recipes_.current();
    return {std::string(e->summary), std::string(e->detail),
            gui::live_line(explain_shown_, session_.settings(), context)};
}

// The height the Explain area needs for its text at `width`, never more than `max_height`.
float App::explain_height(float width, float max_height) const {
    const gui::TokenMetrics& m = token_metrics();
    const ExplainText text = explain_text();
    const float wrap = std::max(1.0f, width - 2 * m.explain_pad.x - 2 * m.window_border);
    float height = 2 * m.explain_pad.y + 2 * m.window_border;
    int parts = 0;
    const auto add = [&](const std::string& s, ImFont* font) {
        if (s.empty()) {
            return;
        }
        ImGui::PushFont(font, token_type().small);
        height += ImGui::CalcTextSize(s.c_str(), nullptr, false, wrap).y;
        ImGui::PopFont();
        if (parts++ > 0) {
            height += m.explain_gap;
        }
    };
    add(text.summary, fonts().semibold);
    add(text.detail, nullptr);
    add(text.live, nullptr);
    return std::min(height, max_height);
}

void App::draw_explain_area(float width, float height) {
    const gui::TokenMetrics& m = token_metrics();
    const gui::TokenColours& c = token_colours();
    const auto vec = [](const gui::Rgba& rgba) { return ImVec4(rgba.r, rgba.g, rgba.b, rgba.a); };
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(c.plot_bg));
    ImGui::PushStyleColor(ImGuiCol_Border, vec(c.plot_border));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.explain_pad.x, m.explain_pad.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, m.plot_rounding);
    ImGui::BeginChild("explain", ImVec2(width, height),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    const ExplainText text = explain_text();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, m.explain_gap));
    const auto part = [&](const std::string& s, ImFont* font, ImVec4 colour) {
        if (s.empty()) {
            return;
        }
        ImGui::PushFont(font, token_type().small);
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextUnformatted(s.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    };
    const bool hint = gui::explanation_for(explain_shown_) == nullptr;
    part(text.summary, hint ? nullptr : fonts().semibold, vec(hint ? c.text_muted : c.text));
    part(text.detail, nullptr, vec(c.text));
    part(text.live, nullptr, vec(c.text_muted));
    ImGui::PopStyleVar();
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void App::draw_panel_column(float height) {
    const float width = ImGui::GetContentRegionAvail().x;
    const bool explaining = prefs_.show_explanations && session_.has_file();
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    // About five lines of Small text (and never more than 45% of a very short panel); a longer
    // text scrolls inside the area.
    float cap = height * 0.45f;
    if (explaining) {
        const gui::TokenMetrics& m = token_metrics();
        ImGui::PushFont(nullptr, token_type().small);
        const float line = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        cap = std::min(cap, 2 * m.explain_pad.y + 2 * m.window_border + gui::explain_max_lines * line +
                                (gui::explain_max_lines - 1) * m.explain_gap);
    }
    const float explain_h = explaining ? explain_height(width, cap) : 0.0f;
    ImGui::BeginGroup();
    ImGui::BeginChild("panel",
                      ImVec2(width, std::max(1.0f, height - (explaining ? explain_h + spacing : 0.0f))),
                      ImGuiChildFlags_Borders);
    draw_panel();
    ImGui::EndChild();
    if (explaining) {
        draw_explain_area(width, explain_h);
    }
    ImGui::EndGroup();
}

void App::draw_panel() {
    if (!session_.has_file()) {
        ImGui::TextDisabled("Open a photo to edit it.");
        return;
    }
    // Three buttons filling the panel's width, in equal thirds when the labels fit. Otherwise
    // Export gets the width its label needs and Undo and Redo share the rest, and on a very
    // narrow panel Export moves to a row of its own.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float padding = 2 * ImGui::GetStyle().FramePadding.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float export_needed = ImGui::CalcTextSize("Export...").x + padding;
    const float undo_needed =
        std::max(ImGui::CalcTextSize("Undo").x, ImGui::CalcTextSize("Redo").x) + padding;
    float export_width = std::max((avail - 2 * spacing) / 3, export_needed);
    float undo_width = (avail - 2 * spacing - export_width) / 2;
    const bool one_row = undo_width >= undo_needed;
    if (!one_row) {
        undo_width = (avail - spacing) / 2;
        export_width = avail;
    }
    ImGui::BeginDisabled(!session_.history().can_undo());
    if (ImGui::Button("Undo", ImVec2(undo_width, 0))) {
        undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session_.history().can_redo());
    if (ImGui::Button("Redo", ImVec2(undo_width, 0))) {
        redo();
    }
    ImGui::EndDisabled();
    if (one_row) {
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(dialog_open_ || exporter_.busy());
    if (ImGui::Button("Export...", ImVec2(export_width, 0))) {
        show_save_dialog();
    }
    ImGui::EndDisabled();

    draw_histogram();

    section_text("Look");
    draw_presets();
    draw_recipe_buttons();

    section_text("Adjust");
    draw_simple_slider("Brightness", gui::SimpleControl::brightness);
    draw_simple_slider("Contrast", gui::SimpleControl::contrast);
    draw_simple_slider("Colour", gui::SimpleControl::colour);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Right-click a slider to reset it. Ctrl-click (Cmd-click on Mac) to type a "
                       "value.");
    ImGui::PopStyleColor();

    draw_pipeline();
}

void App::draw_histogram() {
    if (!histogram_ || histogram_->pixel_count == 0) {
        return;
    }
    const char* const scope_titles[] = {"Histogram", "RGB parade", "Waveform", "Vectorscope"};
    const std::string base_title = scope_titles[static_cast<int>(prefs_.scope)];
    std::string detail =
        histogram_stage_ == gui::final_stage ? "" : std::string(gui::stage_info(histogram_stage_).name);
    if (strip_ && strip_shown_band_ >= 0) {
        // With a test strip: whose histogram this is.
        if (!detail.empty()) {
            detail += ", ";
        }
        detail += gui::strip_band_title(strip_->step, static_cast<int>(strip_bands_.size()),
                                        strip_shown_band_);
    }
    const std::string title = detail.empty() ? base_title : base_title + " (" + detail + ")";
    section_text(title.c_str());

    const gui::StageHistogram& h = *histogram_;
    const float avail_w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    if (prefs_.scope == gui::ScopeChoice::histogram) {
        draw_histogram_graph(h, avail_w);
    } else {
        scope_view_.draw(prefs_.scope, theme(), avail_w, ui_scale());
    }
    explain(gui::explain_id(prefs_.scope));
    if (prefs_.scope == gui::ScopeChoice::vectorscope && scope_view_.skin_line_hovered()) {
        explain_hover_ = std::string(gui::explain::scope_skin);
    }
    draw_clip_chips(h, avail_w);
    draw_scope_selector();
    draw_clip_overlay_button();
}

void App::draw_histogram_graph(const gui::StageHistogram& h, float avail_w) {
    const Palette& colours = palette(theme());
    const float graph_h = std::round(token_metrics().plot_height);
    const float rounding = token_metrics().plot_rounding;

    ImGui::InvisibleButton("##histogram_graph", ImVec2(avail_w, graph_h));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 box_min = ImGui::GetItemRectMin();
    const ImVec2 box_max = ImGui::GetItemRectMax();
    draw->AddRectFilled(box_min, box_max, colours.histogram_background, rounding);

    // Scale to the tallest interior bin (1..254) so an all-black or all-white border doesn't
    // flatten the curve; fall back to all 256 bins when only 0 and 255 have any counts.
    std::uint64_t max_count = 0;
    std::uint64_t max_all = 0;
    for (const gui::StageHistogram::Counts* c : {&h.red, &h.green, &h.blue, &h.luminance}) {
        max_count = std::max(max_count, *std::max_element(c->begin() + 1, c->end() - 1));
        max_all = std::max(max_all, *std::max_element(c->begin(), c->end()));
    }
    if (max_count == 0) {
        max_count = max_all;
    }

    const float pad = token_metrics().plot_padding;
    const ImVec2 plot_min(box_min.x + pad, box_min.y + pad);
    const ImVec2 plot_max(box_max.x - pad, box_max.y - pad);
    const float plot_w = std::max(1.0f, plot_max.x - plot_min.x);
    const float plot_h = std::max(1.0f, plot_max.y - plot_min.y);

    const auto build_points = [&](const gui::StageHistogram::Counts& counts) {
        std::array<ImVec2, gui::StageHistogram::bins> pts{};
        for (int b = 0; b < gui::StageHistogram::bins; ++b) {
            const float x = plot_min.x + (static_cast<float>(b) / 255.0f) * plot_w;
            const double ratio =
                max_count > 0
                    ? std::min(1.0, static_cast<double>(counts[b]) / static_cast<double>(max_count))
                    : 0.0;
            const float y = plot_max.y - static_cast<float>(ratio) * plot_h;
            pts[static_cast<std::size_t>(b)] = ImVec2(x, y);
        }
        return pts;
    };

    draw->PushClipRect(box_min, box_max, true);
    const auto luma_pts = build_points(h.luminance);
    for (int b = 0; b + 1 < gui::StageHistogram::bins; ++b) {
        const ImVec2 p0 = luma_pts[static_cast<std::size_t>(b)];
        const ImVec2 p1 = luma_pts[static_cast<std::size_t>(b + 1)];
        draw->AddQuadFilled(p0, p1, ImVec2(p1.x, plot_max.y), ImVec2(p0.x, plot_max.y),
                            colours.histogram_luma_fill);
    }
    draw->AddPolyline(luma_pts.data(), gui::StageHistogram::bins, colours.histogram_luma_line,
                      ImDrawFlags_None, token_metrics().plot_luma_line);

    const float line_thickness = token_metrics().plot_line;
    const auto red_pts = build_points(h.red);
    const auto green_pts = build_points(h.green);
    const auto blue_pts = build_points(h.blue);
    draw->AddPolyline(blue_pts.data(), gui::StageHistogram::bins, colours.histogram_blue,
                      ImDrawFlags_None, line_thickness);
    draw->AddPolyline(green_pts.data(), gui::StageHistogram::bins, colours.histogram_green,
                      ImDrawFlags_None, line_thickness);
    draw->AddPolyline(red_pts.data(), gui::StageHistogram::bins, colours.histogram_red,
                      ImDrawFlags_None, line_thickness);
    draw->PopClipRect();
    draw->AddRect(box_min, box_max, colours.histogram_border, rounding, 0,
                  token_metrics().plot_border_width);
}

void App::draw_clip_chips(const gui::StageHistogram& h, float avail_w) {
    const Palette& colours = palette(theme());
    const float s = ui_scale();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Two small clip indicators below the graph: shadows on the left, highlights on the right.
    const bool shadows_clipped = gui::has_shadow_clipping(h);
    const bool highlights_clipped = gui::has_highlight_clipping(h);
    const gui::TokenMetrics& metrics = token_metrics();
    const gui::TokenColours& tokens = token_colours();
    const auto rgba = [](const gui::Rgba& c, float lighten = 0.0f) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r + (1 - c.r) * lighten,
                                                     c.g + (1 - c.g) * lighten,
                                                     c.b + (1 - c.b) * lighten, c.a));
    };
    const float chip_h = ImGui::GetFontSize() + 2.0f * metrics.chip_pad_y;
    const float gap = metrics.chip_gap;
    const float chip_w = std::max(1.0f, (avail_w - gap) * 0.5f);

    const auto draw_clip_chip = [&](const char* id, const char* label, bool left_arrow, bool active,
                                    ImU32 active_fill, ImU32 active_border, std::uint64_t r_clip, std::uint64_t g_clip,
                                    std::uint64_t b_clip, const char* tooltip_title,
                                    std::string_view explain_id) {
        ImGui::InvisibleButton(id, ImVec2(chip_w, chip_h));
        const ImVec2 cmin = ImGui::GetItemRectMin();
        const ImVec2 cmax = ImGui::GetItemRectMax();
        const ImU32 fill = active ? active_fill : colours.clip_inactive_fill;
        // A lit chip's edge is its own colour, lighter, not a pale outline.
        const ImU32 border = active ? active_border : colours.clip_inactive_border;
        const ImU32 text_col = active ? rgba(tokens.chip_text) : colours.clip_inactive_text;
        draw->AddRectFilled(cmin, cmax, fill, metrics.chip_rounding);
        draw->AddRect(cmin, cmax, border, metrics.chip_rounding, 0, 1.0f * s);

        const float tri = token_metrics().chevron_size;
        const float cy = (cmin.y + cmax.y) * 0.5f;
        const float pad_x = metrics.chip_pad_x;
        if (left_arrow) {
            const float tx = cmin.x + pad_x;
            draw_chevron(draw, ImVec2(std::round(tx + tri * 0.5f), std::round(cy)), Chevron::left,
                         text_col);
            draw->AddText(ImVec2(tx + tri + metrics.chip_text_gap, cmin.y + metrics.chip_pad_y), text_col,
                          label);
        } else {
            const float tx = cmax.x - pad_x;
            draw_chevron(draw, ImVec2(std::round(tx - tri * 0.5f), std::round(cy)), Chevron::right,
                         text_col);
            const ImVec2 tsize = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(tx - tri - metrics.chip_text_gap - tsize.x, cmin.y + metrics.chip_pad_y),
                          text_col,
                          label);
        }

        explain(explain_id);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s\nR %.2f%%   G %.2f%%   B %.2f%%", tooltip_title,
                              gui::pixel_percent(r_clip, h.pixel_count),
                              gui::pixel_percent(g_clip, h.pixel_count),
                              gui::pixel_percent(b_clip, h.pixel_count));
        }
    };

    draw_clip_chip("##clip_shadows", "Shadows", true, shadows_clipped, rgba(tokens.chip_shadows),
                   rgba(tokens.chip_shadows, 0.25f), h.red_clips.under_black, h.green_clips.under_black, h.blue_clips.under_black,
                   "Shadows clipped (below black):", gui::explain::chip_under);
    ImGui::SameLine(0.0f, gap);
    draw_clip_chip("##clip_highlights", "Highlights", false, highlights_clipped,
                   rgba(tokens.chip_highlights), rgba(tokens.chip_highlights, 0.25f), h.red_clips.over_white, h.green_clips.over_white,
                   h.blue_clips.over_white, "Highlights clipped (above white):",
                   gui::explain::chip_over);
}

void App::draw_scope_selector() {
    struct Choice {
        gui::ScopeChoice scope;
        const char* label;
    };
    static const Choice choices[] = {
        {gui::ScopeChoice::histogram, "Histogram"},
        {gui::ScopeChoice::parade, "Parade"},
        {gui::ScopeChoice::waveform, "Waveform"},
        {gui::ScopeChoice::vectorscope, "Vectorscope"},
    };
    const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    // A slightly smaller font, so the full names fit in one row at the default panel width.
    ImGui::PushFont(nullptr, token_type().small);
    std::string_view hovered_id;
    const float pad = token_metrics().selector_pad;  // tighter than a button so all four fit a row
    const float gap = token_metrics().selector_gap;  // and closer together
    float needed = 3 * gap;
    for (const Choice& c : choices) {
        needed += ImGui::CalcTextSize(c.label).x + 2 * pad;
    }
    const bool one_row = needed <= avail;
    const float row_w = one_row ? avail : (avail - spacing) * 0.5f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(pad, ImGui::GetStyle().FramePadding.y * 0.7f));
    int index = 0;
    for (const Choice& c : choices) {
        const bool selected = prefs_.scope == c.scope;
        const float w = one_row ? ImGui::CalcTextSize(c.label).x + 2 * pad +
                                      (avail - needed) / static_cast<float>(std::size(choices))
                                : row_w;
        if (index % (one_row ? 4 : 2) != 0) {
            ImGui::SameLine(0.0f, one_row ? gap : spacing);
        }
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(c.label, ImVec2(w, 0)) && !selected) {
            gui::Preferences p = prefs_;
            p.scope = c.scope;
            set_preferences(std::move(p));
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
        explain(gui::explain_id(c.scope));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            hovered_id = gui::explain_id(c.scope);
        }
        ++index;
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
    if (const gui::Explanation* e = gui::explanation_for(hovered_id)) {
        ImGui::SetTooltip("%s", std::string(e->summary).c_str());  // in the normal font size
    }
}

void App::draw_clip_overlay_button() {
    const bool on = prefs_.clip_overlay;
    ImGui::PushFont(nullptr, token_type().small);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(token_metrics().selector_pad, ImGui::GetStyle().FramePadding.y * 0.7f));
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button("Show clipping on photo  (J)", ImVec2(-FLT_MIN, 0))) {
        toggle_clip_overlay();
    }
    if (on) {
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
    describe(gui::explain::clip_overlay);
}

void App::draw_presets() {
    const gui::InputKind kind = session_.kind();
    const gui::Preset* current = gui::matching_preset(kind, session_.settings());
    const std::string shown = current != nullptr ? std::string(current->name) : "Custom";
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (styled_begin_combo("##preset", shown.c_str())) {
        for (const gui::Preset& preset : gui::presets_for(kind)) {
            if (ImGui::Selectable(std::string(preset.name).c_str(), &preset == current)) {
                gui::EditSettings s = session_.settings();
                gui::apply_preset(s, preset);
                report(session_.set_settings(s));  // one undo step
            }
            describe(gui::explain_id(preset));
        }
        ImGui::EndCombo();
    }
    describe(gui::explain::look);
    if (current != nullptr) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(gui::look_values_line(session_.settings()).c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        explain(gui::explain::look);
        const bool view = ImGui::SmallButton("View recipe");
        describe(gui::explain::look_view_recipe, view);
        if (view) {
            look_recipe_ = gui::look_recipe_view(kind, *current);
            look_recipe_open_ = true;
            look_recipe_focus_ = true;
        }
    }
    draw_recipe_line();
    const gui::Preset& start = gui::default_preset(kind);
    if (&start != current) {
        const bool reset = ImGui::Button(("Reset to " + std::string(start.name)).c_str());
        describe(gui::explain::look_reset, reset);
        if (reset) {
            gui::EditSettings s = session_.settings();
            gui::apply_preset(s, start);
            report(session_.set_settings(s));
        }
    }
}

void App::draw_simple_slider(const char* label, gui::SimpleControl control) {
    draw_slider(label, control, false);
}

// One of Simple's three sliders, or (in_stage_row) the same slider in its stage's row in the
// pipeline: both edit the same EditSettings field through the same conversions, so they always
// agree. In a stage row the value is shown in its own units ("+0.70 stops"), and a stage that is
// off is greyed out and cannot be moved.
void App::draw_slider(const char* label, gui::SimpleControl control, bool in_stage_row) {
    const gui::StageId stage = gui::stage_of(control);
    // A stage that is off sits at 0 in both places, since that is what it does to the picture; its
    // row's text still gives the value it will come back with.
    const gui::EditSettings& shown = session_.settings();
    const bool enabled = !in_stage_row || gui::stage_enabled(shown, stage);
    const gui::SimpleControls positions = gui::simple_from(shown);
    const gui::SimpleOutOfRange beyond = gui::outside_simple_range(shown);
    float value = 0;
    bool custom = false;
    switch (control) {
        case gui::SimpleControl::brightness:
            value = positions.brightness;
            custom = beyond.brightness;
            break;
        case gui::SimpleControl::contrast:
            value = positions.contrast;
            custom = beyond.contrast;
            break;
        case gui::SimpleControl::colour:
            value = positions.colour;
            custom = beyond.colour;
            break;
    }

    ImGui::PushID(label);
    if (in_stage_row) {
        gui::EditSettings stored = shown;
        gui::set_stage_enabled(stored, stage, true);
        std::string text = gui::stage_value_text(stored, stage);
        if (!enabled) {
            text += " when on";
        }
        // The value in the regular face at full strength; for a stage that is off, in the muted
        // colour (which is checked for contrast) rather than the dimmed look of a disabled item.
        if (!enabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        ImGui::TextUnformatted(text.c_str());
        if (!enabled) {
            ImGui::PopStyleColor();
        }
        if (custom) {
            ImGui::SameLine();
            ImGui::TextDisabled("(beyond the slider)");
        }
        if (control == gui::SimpleControl::brightness) {
            ImGui::SameLine();
            draw_strip_button();
        }
    } else {
        if (control == gui::SimpleControl::brightness) {
            ImGui::AlignTextToFramePadding();
        }
        ImGui::TextUnformatted(label);
        if (control == gui::SimpleControl::brightness) {
            ImGui::SameLine();
            draw_strip_button();
        }
        if (custom) {
            // Set beyond the slider's range (e.g. in Advanced mode): shown at its end until moved.
            ImGui::SameLine();
            ImGui::TextDisabled("(custom)");
        }
        // The value at the right end of the label's line rather than on the slider, where it would
        // sit on top of the grab at 0.
        char text[16];
        std::snprintf(text, sizeof text, "%+.0f", static_cast<double>(value));
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() +
                                                             ImGui::GetContentRegionAvail().x -
                                                             ImGui::CalcTextSize(text).x));
        ImGui::TextUnformatted(text);
    }
    ImGui::BeginDisabled(!enabled);
    ImGui::SetNextItemWidth(-FLT_MIN);
    // Ctrl/Cmd-click still types a value.
    const bool changed =
        styled_slider("##slider", &value, gui::simple_min, gui::simple_max, 0.0f);
    const gui::SliderFrame frame{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
    gui::EditSettings edited = session_.settings();
    gui::apply_simple_control(edited, control, value);
    // A drag updates the preview every frame but becomes one undo step, on release.
    report(gui::apply_slider_frame(session_, frame, edited));
    explain(in_stage_row ? gui::explain_id(stage) : gui::explain_id(control), changed);

    if (ImGui::BeginPopupContextItem("reset")) {
        const std::string neutral =
            in_stage_row ? gui::stage_value_text(gui::EditSettings{}, stage) : "0";
        if (ImGui::MenuItem(("Reset to " + neutral).c_str())) {
            gui::EditSettings s = session_.settings();
            gui::apply_simple_control(s, control, 0);
            report(session_.set_settings(s));
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

// The dots and percentages at the right of a stage's row: highlights above white, shadows with no
// light, and colours outside sRGB, each hidden at 0. A filled dot means the stage clamped them (the
// detail is gone), a hollow one that they are out of range but a later stage may still bring them
// back. True if the mouse is over them (and their tooltip was shown).
bool App::draw_stage_clip_indicator(const gui::StageClip& clip, ImVec2 row_min, ImVec2 row_max,
                                    float baseline) {
    if (!clips_ || !clip.ran) {
        return false;
    }
    const Palette& colours = palette(theme());
    const float s = ui_scale();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const std::uint64_t total = clips_->sample_count;

    struct Mark {
        const gui::ClipCounts* counts;
        gui::ClipFate fate;
        ImU32 colour;
        const char* what;
        const char* recoverable_note;  // nullptr: the general wording
        const char* lost_note;
    };
    const Mark marks[] = {
        {&clip.over, clip.over_fate, colours.clip_highlights_fill,
         clip.stage == gui::StageId::decode ? "Highlights at white" : "Highlights above white",
         nullptr, nullptr},
        {&clip.under, clip.under_fate, colours.clip_shadows_fill, "Shadows below black", nullptr,
         nullptr},
        {&clip.gamut, clip.gamut_fate, colours.clip_gamut_fill, "Colours outside sRGB",
         "Colours more saturated than the screen and file can show. They get clipped later and "
         "their hue can shift.",
         "Clipped here: these colours were more saturated than the screen and file can show, so "
         "their hue can shift."},
    };

    const float radius = token_metrics().dot_radius;
    const float gap = token_metrics().dot_gap;
    const float between = token_metrics().dot_between;
    const float inset = token_metrics().dot_inset;
    ImGui::PushFont(nullptr, token_type().small);
    // The percentages stand on the row name's baseline, and the dots are centred on the height of
    // their digits.
    const float text_top = baseline - ImGui::GetFontBaked()->Ascent;
    const float cy = baseline - ImGui::GetFontSize() * 0.36f;
    float right = row_max.x - inset;
    float left = right;
    // Drawn right to left (the last in the array is first here), so highlights end up on the left
    // and the percentages line up across rows.
    for (int i = 2; i >= 0; --i) {
        const Mark& m = marks[i];
        if (m.fate == gui::ClipFate::none) {
            continue;
        }
        const std::string text = gui::format_clip_percent(m.counts->any, total);
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const float text_x = right - size.x;
        draw->AddText(ImVec2(text_x, text_top), ImGui::GetColorU32(ImGuiCol_Text),
                      text.c_str());
        const ImVec2 centre(text_x - gap - radius, cy);
        if (m.fate == gui::ClipFate::lost) {
            draw->AddCircleFilled(centre, radius, m.colour);
        } else {
            draw->AddCircle(centre, radius - 0.75f * s, m.colour, 0, 1.75f * s);
        }
        left = centre.x - radius;
        right = left - between;
    }
    ImGui::PopFont();
    if (left >= row_max.x - inset) {
        return false;  // nothing drawn
    }

    const bool hovered = ImGui::IsMouseHoveringRect(ImVec2(left - 4.0f * s, row_min.y),
                                                    ImVec2(row_max.x, row_max.y)) &&
                         ImGui::IsWindowHovered();
    // The same delay as every tooltip: the mouse must rest on this row's dots. (They are not an
    // item of their own, so ImGui's own hover timer can't be used.)
    if (!hovered && dot_hover_stage_ == clip.stage) {
        dot_hover_stage_.reset();
    } else if (hovered && dot_hover_stage_ != clip.stage) {
        dot_hover_stage_ = clip.stage;
        dot_hover_since_ = ImGui::GetTime();
    }
    if (hovered && ImGui::GetTime() - dot_hover_since_ >= ImGui::GetStyle().HoverDelayNormal) {
        ImGui::BeginTooltip();
        for (const Mark& m : marks) {
            if (m.fate == gui::ClipFate::none) {
                continue;
            }
            ImGui::Text("%s: %s of pixels", m.what,
                        gui::format_clip_percent(m.counts->any, total).c_str());
            const char* note =
                m.fate != gui::ClipFate::lost
                    ? (m.recoverable_note ? m.recoverable_note
                                          : "Out of range, but a later stage may still bring them "
                                            "back.")
                : m.lost_note ? m.lost_note
                : clip.stage == gui::StageId::decode
                    ? "Already clipped in the file: no edit can bring them back."
                    : "Clipped here: this stage cut them off, so the detail is gone.";
            ImGui::PushTextWrapPos(token_metrics().tooltip_wrap);
            ImGui::TextDisabled("%s", note);
            ImGui::PopTextWrapPos();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Filled dot: detail lost here.  Hollow dot: still recoverable.");
        ImGui::TextDisabled("Orange: above white.  Blue: below black.");
        ImGui::TextDisabled("Violet: too saturated for sRGB.");
        ImGui::EndTooltip();
    }
    return hovered;
}

// One plain line under the pipeline view about where clipping starts and where it costs detail.
void App::draw_clipping_summary() {
    if (!clips_ || strip_ || !session_.has_file()) {
        return;
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(gui::describe_clipping(*clips_).c_str());
    explain(gui::explain::clip_summary);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
    const std::string gamut = gui::describe_gamut(*clips_);
    if (!gamut.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(gamut.c_str());
    }
    ImGui::PopTextWrapPos();
    if (hovered) {
        ImGui::SetTooltip("%s", std::string(gui::explanation_for(gui::explain::clip_summary)->summary).c_str());
    }
}

// The colour matrix's file name, "recipe matrix", or "none". Read-only: a matrix comes from a
// recipe.
void App::draw_matrix_name() {
    const gui::EditSettings& s = session_.settings();
    const std::string text = gui::stage_value_text(s, gui::StageId::matrix);
    const auto measure = [](std::string_view t) {
        return ImGui::CalcTextSize(t.data(), t.data() + t.size()).x;
    };
    const bool dim = !s.matrix || !s.matrix_on;
    if (dim) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    ImGui::TextUnformatted(
        gui::ellipsize_middle(text, ImGui::GetContentRegionAvail().x, measure).c_str());
    if (dim) {
        ImGui::PopStyleColor();
    }
    if (s.matrix && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", s.matrix->path.empty() ? "From a recipe that names no file."
                                                       : utf8(s.matrix->path).c_str());
    }
}

// The tone curve's kind and its one number (gamma, or Reinhard's white point). A change of kind is
// one undo step; the number is a slider like the others. Greyed out and fixed while the stage is
// off.
void App::draw_curve_controls() {
    const gui::EditSettings& current = session_.settings();
    ImGui::BeginDisabled(!current.curve_on);

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (styled_begin_combo("##curve", std::string(gui::curve_kind_name(current.curve)).c_str())) {
        for (const gui::CurveKind kind : gui::curve_kinds) {
            if (ImGui::Selectable(std::string(gui::curve_kind_name(kind)).c_str(),
                                  kind == current.curve)) {
                gui::EditSettings s = current;
                gui::set_curve_kind(s, kind);
                report(session_.set_settings(s));
            }
            explain(gui::explain_id(kind));
        }
        ImGui::EndCombo();
    }
    explain(gui::explain_id(gui::StageId::curve));
    if (ImGui::BeginPopupContextItem("reset_kind")) {
        if (ImGui::MenuItem("Reset to None")) {
            gui::EditSettings s = current;
            gui::set_curve_kind(s, gui::CurveKind::none);
            report(session_.set_settings(s));
        }
        ImGui::EndPopup();
    }

    if (const std::optional<gui::CurveParameter> p = gui::curve_parameter(current.curve)) {
        // Label and value on their own line, as for Simple's sliders, not on top of the grab. The
        // slider moves a position (gui_core maps it to the value, in ratios for Reinhard), so it
        // takes no typed input: that would be a position, not the value.
        const float value = gui::curve_parameter_value(current);
        // Text keeps its full alpha even while the controls are disabled, in the muted colour.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
        if (!current.curve_on) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        ImGui::TextUnformatted(std::string(p->label).c_str());
        char text[16];
        std::snprintf(text, sizeof text, "%.2f", static_cast<double>(value));
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() +
                                                             ImGui::GetContentRegionAvail().x -
                                                             ImGui::CalcTextSize(text).x));
        ImGui::TextUnformatted(text);
        if (!current.curve_on) {
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();
        float position = gui::curve_slider_position(*p, value);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed =
            styled_slider("##curve_parameter", &position, 0.0f, 1.0f,
                          gui::curve_slider_position(*p, p->default_value),
                          ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
        const gui::SliderFrame frame{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
        gui::EditSettings edited = current;
        // Unmoved, the value stays exactly as stored rather than going through the mapping.
        gui::set_curve_parameter(edited, changed ? gui::curve_value_at(*p, position) : value);
        report(gui::apply_slider_frame(session_, frame, edited));
        explain(current.curve == gui::CurveKind::gamma ? gui::explain::curve_gamma_value
                                                       : gui::explain::curve_white,
                changed);
        if (ImGui::BeginPopupContextItem("reset_parameter")) {
            char neutral[16];
            std::snprintf(neutral, sizeof neutral, "Reset to %g",
                          static_cast<double>(p->default_value));
            if (ImGui::MenuItem(neutral)) {
                gui::EditSettings s = session_.settings();
                gui::set_curve_parameter(s, p->default_value);
                report(session_.set_settings(s));
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndDisabled();
}

// "Compare with": Off, Original or a stage, and Swap. The viewed stage is the right half of the
// split, the chosen one the left half. Choosing the viewed stage itself is the same as Off.
void App::draw_compare_controls() {
    const std::optional<gui::StageId> shown = gui::effective_compare(viewed_stage_, compare_stage_);
    const float swap_width =
        ImGui::CalcTextSize("Swap").x + ImGui::GetStyle().FramePadding.x * 2;
    // Swap on the label's row, right-aligned, so the combo below has the whole width and a name
    // like "after Output encode" fits.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Compare with");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - swap_width);
    ImGui::BeginDisabled(!shown);
    if (ImGui::Button("Swap") && shown) {
        const gui::StageId was_viewed = viewed_stage_;
        viewed_stage_ = *shown;
        compare_stage_ = was_viewed;
        strip_.reset();
    }
    ImGui::EndDisabled();
    describe(gui::explain::compare_swap);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string current = shown ? gui::stage_caption(*shown) : std::string("Off");
    if (styled_begin_combo("##compare_with", current.c_str())) {
        if (ImGui::Selectable("Off", !shown)) {
            compare_stage_.reset();
        }
        for (const gui::StageInfo& stage : gui::pipeline_stages()) {
            const bool is_viewed = stage.id == viewed_stage_;
            const std::string caption = gui::stage_caption(stage.id);
            if (ImGui::Selectable(caption.c_str(), shown == stage.id)) {
                if (is_viewed) {
                    compare_stage_.reset();  // comparing a stage with itself: Off
                } else {
                    compare_stage_ = stage.id;
                    strip_.reset();  // a comparison and a strip don't mix
                }
            }
        }
        ImGui::EndCombo();
    }
    describe(gui::explain::compare);
}

void App::draw_pipeline() {
    // The per-stage clipping is only worked out while this section is open.
    section_header("Pipeline", &pipeline_open_);
    if (!pipeline_open_) {
        return;
    }
    draw_compare_controls();
    if (strip_) {
        ImGui::TextDisabled("Test strip on: the clipping dots are hidden.");
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float row_h = ImGui::GetFrameHeight();
    const float box = std::round(token_metrics().checkbox_size);
    // The name starts after the checkbox (or the gap where there is none); what hangs under it, the
    // value and its control, lines up with it.
    const float inset = style.ItemInnerSpacing.x;
    const float name_indent = inset + box + style.ItemInnerSpacing.x;
    for (const gui::StageInfo& stage : gui::pipeline_stages()) {
        ImGui::PushID(static_cast<int>(stage.id));
        const bool enabled = gui::stage_enabled(session_.settings(), stage.id);
        const bool selected = viewed_stage_ == stage.id;

        // The row is one hit area across the panel's width, drawn with its own highlight so that
        // it covers the checkbox too; the checkbox, submitted over it, takes its own clicks.
        const ImVec2 row_pos = ImGui::GetCursorScreenPos();
        const float row_w = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(0.0f, row_h))) {
            viewed_stage_ = stage.id;
        }
        ImGui::PopStyleColor(3);
        const ImVec2 row_min = ImGui::GetItemRectMin();
        const ImVec2 row_max = ImVec2(row_pos.x + row_w, row_pos.y + row_h);
        const bool row_over =
            ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(row_min, row_max);
        if (selected || row_over) {
            const gui::TokenColours& tokens = token_colours();
            const gui::Rgba& fill = selected ? tokens.header_active : tokens.header_hovered;
            ImGui::GetWindowDrawList()->AddRectFilled(
                row_min, row_max, ImGui::GetColorU32(ImVec4(fill.r, fill.g, fill.b, fill.a)),
                style.FrameRounding);
        }
        const bool row_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
        explain(gui::explain_id(stage.id));

        ImGui::SetCursorScreenPos(ImVec2(row_pos.x + inset, row_pos.y));
        if (stage.can_toggle) {
            bool on = enabled;
            if (styled_checkbox("##on", &on)) {
                gui::EditSettings s = session_.settings();
                gui::set_stage_enabled(s, stage.id, on);
                report(session_.set_settings(s));  // one undo step in History
            }
            describe(gui::explain_id(stage.id));
        } else {
            ImGui::Dummy(ImVec2(box, row_h));
        }
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

        // The name in the semi-bold face, in the muted colour when the stage is off.
        std::string name(stage.name);
        if (!enabled) {
            name += " (off)";
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        ImGui::PushFont(fonts().semibold, token_type().body);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name.c_str());
        // Where the name's letters sit: the dots and percentages at the row's right share it.
        const float name_baseline = ImGui::GetItemRectMin().y + ImGui::GetFontBaked()->Ascent;
        ImGui::PopFont();
        if (!enabled) {
            ImGui::PopStyleColor();
        }

        const bool over_indicator =
            clips_ && !strip_ && draw_stage_clip_indicator(clips_->at(stage.id), row_min, row_max, name_baseline);
        if (over_indicator) {
            explain_hover_ = std::string(gui::explain::clip_dot);
        } else if (row_hovered) {
            ImGui::SetTooltip("%s",
                              std::string(gui::explanation_for(gui::explain_id(stage.id))->summary).c_str());
        }
        if (gui::hidden_by_view(session_.settings(), stage.id, viewed_stage_, compare_stage_)) {
            // An edit after the viewed stage changes the photo but not what is on screen.
            ImGui::Indent(name_indent);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", gui::view_hint(viewed_stage_, compare_stage_).c_str());
            ImGui::PopTextWrapPos();
            ImGui::Unindent(name_indent);
        }
        if (const std::optional<gui::SimpleControl> slider = gui::stage_slider(stage.id)) {
            ImGui::Indent(name_indent);
            draw_slider(std::string(stage.slug).c_str(), *slider, true);
            ImGui::Unindent(name_indent);
        } else if (stage.id == gui::StageId::matrix || stage.id == gui::StageId::curve) {
            ImGui::Indent(name_indent);
            if (stage.id == gui::StageId::matrix) {
                draw_matrix_name();
            } else {
                draw_curve_controls();
            }
            ImGui::Unindent(name_indent);
        }
        ImGui::PopID();
    }
    draw_clipping_summary();
}

void App::draw_status_bar() {
    if (!session_.has_file()) {
        ImGui::TextDisabled("No photo open");
        return;
    }
    const bool raw = session_.kind() == gui::InputKind::raw;
    std::string details = "  |  " + std::to_string(session_.decoded_width()) + " x " +
                          std::to_string(session_.decoded_height()) +
                          (raw ? "  |  RAW, half-size preview" : "");
    if (texture_.texture() != nullptr) {
        details += "  |  preview " + std::to_string(texture_.width()) + " x " +
                   std::to_string(texture_.height()) + ", " +
                   std::to_string(static_cast<int>(std::lround(last_render_ms_))) + " ms";
    }
    // The file name gives way first (shortened in the middle, keeping its extension), then the
    // details, so the line never runs off the window's edge.
    const auto measure = [](std::string_view s) {
        return ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
    };
    const float total = ImGui::GetContentRegionAvail().x;
    const float note_room =
        clip_note_ ? std::min(clip_note_width(*clip_note_), total * 0.5f) + ImGui::GetStyle().ItemSpacing.x
                   : 0.0f;
    const float width = std::max(1.0f, total - note_room);
    const float name_width = std::max(width - measure(details), width * 0.3f);
    const std::string status =
        gui::ellipsize_middle(gui::ellipsize_middle(file_name(session_.path()), name_width,
                                                    measure) +
                                  details,
                              width, measure);
    ImGui::TextDisabled("%s", status.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", utf8(session_.path()).c_str());
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

// Right-aligned on the status bar's line: a dot in the colour of what clipped (as in the pipeline
// rows), then the short text; hover for the full sentence. Muted while it is not for the settings
// on screen yet.
void App::draw_clip_note(float room) {
    const gui::ClipNote& note = *clip_note_;
    const Palette& colours = palette(theme());
    const gui::TokenMetrics& m = token_metrics();
    const bool fresh = last_frame_had_clips_ && !preview_.rendering();
    const float natural = clip_note_width(note);
    const float width = std::min(natural, std::max(1.0f, room - ImGui::GetStyle().ItemSpacing.x));
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
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
        ImGui::TextUnformatted(note.full.c_str());
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
    if (styled_slider_int("##panel_width", &width, gui::panel_width_min, 800,
                          gui::panel_width_default, ImGuiSliderFlags_AlwaysClamp)) {
        p.panel_width = gui::clamp_panel_width(width);
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
    if (ImGui::Button("Third-party notices...")) {
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
            waiting ? gui::waiting_text(*close_pending_) : std::string_view("Saving...");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + token_metrics().prompt_wrap);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(token_metrics().progress_width, 0),
                           "");
        if (ImGui::Button("Cancel") || (waiting && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
            if (waiting) {
                apply_close_step(gui::CloseStep::cancel);
            } else {
                exporter_.cancel();
            }
        }
        ImGui::EndPopup();
    }
}

// "You have edits that haven't been exported or saved as a recipe.", what was saved last, and
// Export..., Save Recipe..., Quit/Open Without Saving and Cancel in the platform's order. Modal;
// Esc is Cancel; Export... is the default (Enter).
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
    const gui::PromptText text = gui::prompt_text(*close_pending_);
    const std::string detail = gui::prompt_detail(baseline_.last_save());
    const float wrap = token_metrics().prompt_wrap;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text.message.data(), text.message.data() + text.message.size());
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
    const gui::PromptButtons buttons = gui::prompt_buttons(ImGui::GetIO().ConfigMacOSXBehaviors);
    const ImGuiStyle& style = ImGui::GetStyle();
    float widths[4];
    float packed = 0;
    for (int i = 0; i < 4; ++i) {
        widths[i] = ImGui::CalcTextSize(label(buttons.order[i]).c_str()).x + 2 * style.FramePadding.x;
        packed += widths[i] + (i > 0 ? style.ItemSpacing.x : 0.0f);
    }
    // With a gap, the row is as wide as the text above it (or the buttons and a clear gap), and
    // the buttons after the gap sit at its right end.
    const float row = std::max(wrap, packed + 3 * style.ItemSpacing.x);
    float right_group = 0;
    for (int i = buttons.gap_after; i < 4 && buttons.gap_after > 0; ++i) {
        right_group += widths[i] + (i > buttons.gap_after ? style.ItemSpacing.x : 0.0f);
    }
    const float row_x = ImGui::GetCursorPosX();
    std::optional<gui::PromptChoice> choice;
    for (int i = 0; i < 4; ++i) {
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


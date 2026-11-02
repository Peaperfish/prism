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
#include "macos_menu.hpp"
#include "theme.hpp"
#include "prism/gui/file_types.hpp"
#include "prism/gui/layout.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/slider_gesture.hpp"
#include "prism/gui/stage_controls.hpp"
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
    const float pad = 6.0f * ui_scale();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x + 2 * pad, pos.y + size.y + 2 * pad),
                        colours.overlay_background, 4.0f * ui_scale());
    draw->AddText(ImVec2(pos.x + pad, pos.y + pad), colours.overlay_text, text.c_str());
}

std::string file_name(const fs::path& path) {
    return utf8(path.filename());
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
    pending_open_ = std::move(path);
    pending_open_shown_ = false;
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
    texture_.reset();  // don't show the previous photo under the new name
    histogram_.reset();
    clips_.reset();
    clip_mask_.clear();
    texture_.reset_overlay();
    scope_view_.set(std::nullopt);
    viewed_stage_ = gui::final_stage;
    compare_stage_.reset();
    SDL_SetWindowTitle(window_, ("Prism - " + file_name(path)).c_str());
}

void App::show_open_dialog() {
    if (dialog_open_) {
        return;
    }
    dialog_open_ = true;
    const fs::path folder = session_.has_file() ? session_.path().parent_path() : fs::path();
    gui_app::show_open_dialog(window_, dialogs_, folder);
}

void App::show_save_dialog() {
    if (dialog_open_ || !session_.has_file() || exporter_.busy()) {
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
        if (!result.error.empty()) {
            show_error({gui::ErrorKind::unreadable, "Couldn't show the file dialog.",
                        result.error});
        } else if (!result.path) {
            // Cancelled.
        } else if (result.kind == DialogResult::Kind::open) {
            request_open(std::move(*result.path));
        } else if (session_.has_file()) {
            // The export works on a copy of the settings: what the preview shows right now.
            const fs::path output = gui::with_export_extension(*result.path, result.format);
            error_.reset();
            notice_.clear();
            exporter_.start(session_.export_job(), output);
        }
    }
}

void App::take_export_result() {
    std::optional<gui::ExportResult> result = exporter_.poll();
    if (!result) {
        return;
    }
    if (result->error && result->error->kind == gui::ErrorKind::cancelled) {
        notice_ = "Export cancelled. Nothing was written.";
    } else if (result->error) {
        show_error(std::move(*result->error));
    } else {
        notice_ = "Saved " + file_name(result->output) + ".";
    }
}

void App::update_preview() {
    // Scopes, the per-stage clipping and the clip mask are only computed while they are on screen
    // (the pipeline view open, the overlay on); the histogram comes with every frame.
    gui::PreviewOptions options;
    options.scopes = prefs_.scope != gui::ScopeChoice::histogram;
    options.clip_report = pipeline_open_;
    options.clip_mask = prefs_.clip_overlay;
    options.compare_stage = gui::effective_compare(viewed_stage_, compare_stage_);
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
    clips_ = std::move(frame->clips);
    clip_mask_ = std::move(frame->clip_mask);
    clip_mask_width_ = frame->width;
    clip_mask_height_ = frame->height;
    overlay_dirty_ = true;
    scope_view_.set(std::move(frame->scopes));
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
            if (!dropped_.empty()) {
                notice_ = dropped_.size() > 1 ? "Prism opens one photo at a time." : "";
                request_open(dropped_.front());
                dropped_.clear();
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            // No motion events arrive during a system drag: the drag left without dropping.
            drag_over_ = false;
            break;
        default:
            break;
    }
}

bool App::animating() const {
    return quiet_frames_ < settle_frames || preview_.rendering() || pending_open_.has_value() ||
           exporter_.busy();
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

    ImGui::BeginChild("canvas", ImVec2(canvas, body_height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    draw_canvas();
    draw_banner();
    draw_stage_badge();
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    draw_splitter(canvas, splitter, body_height, avail.x);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("panel", ImVec2(0, body_height), ImGuiChildFlags_Borders);
    draw_panel();
    ImGui::EndChild();

    draw_status_bar();
    draw_saving_popup();
    ImGui::End();
    focused_window_this_frame_ = nullptr;
    draw_preferences();
    draw_about();
    draw_notices();

    if (pending_open_) {
        pending_open_shown_ = true;
    }
    // For Esc next frame: ImGui will have used the key on these before the app sees it.
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
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_E, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        show_save_dialog();
    }
    // Esc backs out one level: first the comparison, then the viewed stage. Read as a plain key
    // press, not a Shortcut(): a Shortcut route loses to the panel once a slider or combo there
    // has the keyboard focus, so Esc did nothing until the picture was clicked.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        gui::EscapeContext esc;
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
        if (ImGui::MenuItem("Quit")) {
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
    const float margin = 12.0f * ui_scale();

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
        }
    } else if (!session_.has_file() && !pending_open_) {
        // Empty state: a dashed drop area with the one thing to do. It lights up while a file is
        // dragged over the window.
        const float inset = 24.0f * ui_scale();
        dashed_rect(draw, ImVec2(min.x + inset, min.y + inset),
                    ImVec2(max.x - inset, max.y - inset),
                    drag_over_ ? colours.drop_highlight : colours.drop_outline,
                    8.0f * ui_scale(), 2.0f * ui_scale());
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
        draw->AddRect(min, max, colours.drop_highlight, 0.0f, 0, 3.0f * ui_scale());
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
    const float line = 2.0f * scale;
    draw->AddRectFilled(ImVec2(split.divider_x - line, top), ImVec2(split.divider_x + line, bottom),
                        IM_COL32(0, 0, 0, 90));
    draw->AddRectFilled(ImVec2(split.divider_x - line / 2, top),
                        ImVec2(split.divider_x + line / 2, bottom), IM_COL32(255, 255, 255, 235));
    const float mid = (top + bottom) / 2;
    const float grip = 9.0f * scale;
    draw->AddCircleFilled(ImVec2(split.divider_x, mid), grip, IM_COL32(255, 255, 255, 235));
    draw->AddCircle(ImVec2(split.divider_x, mid), grip, IM_COL32(0, 0, 0, 110), 0, 1.0f * scale);
    const float arrow = 4.0f * scale;
    const ImU32 ink = IM_COL32(40, 40, 40, 255);
    draw->AddTriangleFilled(ImVec2(split.divider_x - arrow - 1 * scale, mid),
                            ImVec2(split.divider_x - 1 * scale, mid - arrow),
                            ImVec2(split.divider_x - 1 * scale, mid + arrow), ink);
    draw->AddTriangleFilled(ImVec2(split.divider_x + arrow + 1 * scale, mid),
                            ImVec2(split.divider_x + 1 * scale, mid - arrow),
                            ImVec2(split.divider_x + 1 * scale, mid + arrow), ink);

    // Dragging: a strip around the divider, as tall as the picture. The cursor is put back after,
    // so the banner and badge below this lay out as before.
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    const float strip = 14.0f * scale;
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
    const float margin = 10.0f * scale;
    const std::string left_text = gui::stage_caption(texture_.compare_stage());
    const std::string right_text = gui::stage_caption(histogram_stage_);
    const float pad = 6.0f * scale;
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
    const float margin = 12.0f * ui_scale();
    ImGui::SetCursorPos(ImVec2(margin, margin));
    const bool is_error = error_.has_value();
    const Palette& colours = palette(theme());
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          is_error ? colours.error_background : colours.notice_background);
    ImGui::PushStyleColor(ImGuiCol_Text, is_error ? colours.error_text : colours.notice_text);
    ImGui::PushStyleColor(ImGuiCol_Border, is_error ? colours.error_border : colours.notice_border);
    // Across the canvas, but not wider than a comfortable line length on a big window.
    const float banner_width =
        std::min(ImGui::GetContentRegionAvail().x - margin, 720.0f * ui_scale());
    ImGui::BeginChild("banner", ImVec2(std::max(1.0f, banner_width), 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(is_error ? error_->message.c_str() : notice_.c_str());
    ImGui::PopTextWrapPos();
    if (is_error && !error_->details.empty()) {
        if (ImGui::TreeNode("Details")) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(error_->details.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::SmallButton("Copy")) {
                ImGui::SetClipboardText((error_->message + "\n" + error_->details).c_str());
            }
            ImGui::TreePop();
        }
    }
    if (ImGui::SmallButton("Dismiss")) {
        error_.reset();
        notice_.clear();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);
}

void App::draw_stage_badge() {
    // Two halves have their own chips; the badge is for the single picture.
    if (!session_.has_file() || viewed_stage_ == gui::final_stage || splitting()) {
        return;
    }
    const float margin = 12.0f * ui_scale();
    if (!error_ && notice_.empty()) {
        ImGui::SetCursorPos(ImVec2(margin, margin));
    } else {
        ImGui::SetCursorPosX(margin);
    }

    const Palette& colours = palette(theme());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colours.notice_background);
    ImGui::PushStyleColor(ImGuiCol_Text, colours.notice_text);
    ImGui::PushStyleColor(ImGuiCol_Border, colours.notice_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * ui_scale(), 5.0f * ui_scale()));
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

    section_text("Adjust");
    draw_simple_slider("Brightness", gui::SimpleControl::brightness);
    draw_simple_slider("Contrast", gui::SimpleControl::contrast);
    draw_simple_slider("Colour", gui::SimpleControl::colour);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Right-click a slider to reset it.");
    ImGui::PopStyleColor();

    draw_pipeline();
}

void App::draw_histogram() {
    if (!histogram_ || histogram_->pixel_count == 0) {
        return;
    }
    const char* const scope_titles[] = {"Histogram", "RGB parade", "Waveform", "Vectorscope"};
    const std::string base_title = scope_titles[static_cast<int>(prefs_.scope)];
    const std::string title =
        histogram_stage_ == gui::final_stage
            ? base_title
            : base_title + " (" + std::string(gui::stage_info(histogram_stage_).name) + ")";
    section_text(title.c_str());

    const gui::StageHistogram& h = *histogram_;
    const float avail_w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    if (prefs_.scope == gui::ScopeChoice::histogram) {
        draw_histogram_graph(h, avail_w);
    } else {
        scope_view_.draw(prefs_.scope, theme(), avail_w, ui_scale());
    }
    draw_clip_chips(h, avail_w);
    draw_scope_selector();
    draw_clip_overlay_button();
}

void App::draw_histogram_graph(const gui::StageHistogram& h, float avail_w) {
    const Palette& colours = palette(theme());
    const float s = ui_scale();
    const float graph_h = std::round(68.0f * s);
    const float rounding = 4.0f * s;

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

    const float pad = 4.0f * s;
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
                      ImDrawFlags_None, 1.0f * s);

    const float line_thickness = 1.5f * s;
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
    draw->AddRect(box_min, box_max, colours.histogram_border, rounding, 0, 1.0f * s);
}

void App::draw_clip_chips(const gui::StageHistogram& h, float avail_w) {
    const Palette& colours = palette(theme());
    const float s = ui_scale();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Two small clip indicators below the graph: shadows on the left, highlights on the right.
    const bool shadows_clipped = gui::has_shadow_clipping(h);
    const bool highlights_clipped = gui::has_highlight_clipping(h);
    const float chip_h = ImGui::GetFontSize() + 6.0f * s;
    const float gap = 6.0f * s;
    const float chip_w = std::max(1.0f, (avail_w - gap) * 0.5f);

    const auto draw_clip_chip = [&](const char* id, const char* label, bool left_arrow, bool active,
                                    ImU32 active_fill, std::uint64_t r_clip, std::uint64_t g_clip,
                                    std::uint64_t b_clip, const char* tooltip_title) {
        ImGui::InvisibleButton(id, ImVec2(chip_w, chip_h));
        const ImVec2 cmin = ImGui::GetItemRectMin();
        const ImVec2 cmax = ImGui::GetItemRectMax();
        const ImU32 fill = active ? active_fill : colours.clip_inactive_fill;
        const ImU32 border = active ? colours.clip_active_border : colours.clip_inactive_border;
        const ImU32 text_col = active ? colours.clip_active_text : colours.clip_inactive_text;
        draw->AddRectFilled(cmin, cmax, fill, 3.0f * s);
        draw->AddRect(cmin, cmax, border, 3.0f * s, 0, 1.0f * s);

        const float tri = 4.5f * s;
        const float cy = (cmin.y + cmax.y) * 0.5f;
        const float pad_x = 7.0f * s;
        if (left_arrow) {
            const float tx = cmin.x + pad_x;
            draw->AddTriangleFilled(ImVec2(tx, cy), ImVec2(tx + tri, cy - tri * 0.85f),
                                    ImVec2(tx + tri, cy + tri * 0.85f), text_col);
            draw->AddText(ImVec2(tx + tri + 5.0f * s, cmin.y + 3.0f * s), text_col, label);
        } else {
            const float tx = cmax.x - pad_x;
            draw->AddTriangleFilled(ImVec2(tx, cy), ImVec2(tx - tri, cy - tri * 0.85f),
                                    ImVec2(tx - tri, cy + tri * 0.85f), text_col);
            const ImVec2 tsize = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(tx - tri - 5.0f * s - tsize.x, cmin.y + 3.0f * s), text_col,
                          label);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\nR %.2f%%   G %.2f%%   B %.2f%%", tooltip_title,
                              gui::pixel_percent(r_clip, h.pixel_count),
                              gui::pixel_percent(g_clip, h.pixel_count),
                              gui::pixel_percent(b_clip, h.pixel_count));
        }
    };

    draw_clip_chip("##clip_shadows", "Shadows", true, shadows_clipped, colours.clip_shadows_fill,
                   h.red_clips.under_black, h.green_clips.under_black, h.blue_clips.under_black,
                   "Shadows clipped (below black):");
    ImGui::SameLine(0.0f, gap);
    draw_clip_chip("##clip_highlights", "Highlights", false, highlights_clipped,
                   colours.clip_highlights_fill, h.red_clips.over_white, h.green_clips.over_white,
                   h.blue_clips.over_white, "Highlights clipped (above white):");
}

void App::draw_scope_selector() {
    struct Choice {
        gui::ScopeChoice scope;
        const char* label;
        const char* tip;
    };
    static const Choice choices[] = {
        {gui::ScopeChoice::histogram, "Histogram",
         "Shows how many pixels are at each brightness, from black on the left to white on the "
         "right."},
        {gui::ScopeChoice::parade, "Parade",
         "Shows how bright the red, green and blue parts of the image are, side by side, from "
         "left to right."},
        {gui::ScopeChoice::waveform, "Waveform",
         "Shows how bright each part of the image is, from left to right."},
        {gui::ScopeChoice::vectorscope, "Vectorscope",
         "Shows which colours are in the image and how strong they are; the centre is neutral "
         "grey."},
    };
    const float s = ui_scale();
    const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    // A slightly smaller font, so the full names fit in one row at the default panel width.
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
    const char* hovered_tip = nullptr;
    const float pad = 3.0f * s;  // tighter than a normal button so all four fit in a row
    const float gap = 2.0f * s;  // and closer together
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
        if (ImGui::IsItemHovered()) {
            hovered_tip = c.tip;
        }
        ++index;
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
    if (hovered_tip != nullptr) {
        ImGui::SetTooltip("%s", hovered_tip);  // in the normal font size
    }
}

void App::draw_clip_overlay_button() {
    const bool on = prefs_.clip_overlay;
    const float s = ui_scale();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(3.0f * s, ImGui::GetStyle().FramePadding.y * 0.7f));
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
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Marks pixels that are brighter than white (orange-red), have no light left "
            "(blue) or are too saturated for sRGB (violet stripes).\nPixels an earlier stage "
            "already clipped stay marked at the stages after it.");
    }
}

void App::draw_presets() {
    const gui::InputKind kind = session_.kind();
    const gui::Preset* current = gui::matching_preset(kind, session_.settings());
    const std::string shown = current != nullptr ? std::string(current->name) : "Custom";
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##preset", shown.c_str())) {
        for (const gui::Preset& preset : gui::presets_for(kind)) {
            if (ImGui::Selectable(std::string(preset.name).c_str(), &preset == current)) {
                gui::EditSettings s = session_.settings();
                gui::apply_preset(s, preset);
                report(session_.set_settings(s));  // one undo step
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", std::string(preset.description).c_str());
            }
        }
        ImGui::EndCombo();
    }
    const gui::Preset& start = gui::default_preset(kind);
    if (&start != current &&
        ImGui::Button(("Reset to " + std::string(start.name)).c_str())) {
        gui::EditSettings s = session_.settings();
        gui::apply_preset(s, start);
        report(session_.set_settings(s));
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
    ImGui::BeginDisabled(!enabled);
    if (in_stage_row) {
        gui::EditSettings stored = shown;
        gui::set_stage_enabled(stored, stage, true);
        std::string text = gui::stage_value_text(stored, stage);
        if (!enabled) {
            text += " when on";
        }
        ImGui::TextUnformatted(text.c_str());
        if (custom) {
            ImGui::SameLine();
            ImGui::TextDisabled("(beyond the slider)");
        }
    } else {
        ImGui::TextUnformatted(label);
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
    ImGui::SetNextItemWidth(-FLT_MIN);
    // An empty format draws no text on the slider; Ctrl/Cmd-click still types a value.
    const bool changed =
        ImGui::SliderFloat("##slider", &value, gui::simple_min, gui::simple_max, "");
    const gui::SliderFrame frame{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
    gui::EditSettings edited = session_.settings();
    gui::apply_simple_control(edited, control, value);
    // A drag updates the preview every frame but becomes one undo step, on release.
    report(gui::apply_slider_frame(session_, frame, edited));

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
bool App::draw_stage_clip_indicator(const gui::StageClip& clip, ImVec2 row_min, ImVec2 row_max) {
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

    const float radius = 4.0f * s;
    const float gap = 4.0f * s;
    const float between = 8.0f * s;
    const float cy = (row_min.y + row_max.y) * 0.5f;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
    float right = row_max.x - 6.0f * s;
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
        draw->AddText(ImVec2(text_x, cy - size.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text),
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
    if (left >= row_max.x - 6.0f * s) {
        return false;  // nothing drawn
    }

    const bool hovered = ImGui::IsMouseHoveringRect(ImVec2(left - 4.0f * s, row_min.y),
                                                    ImVec2(row_max.x, row_max.y)) &&
                         ImGui::IsWindowHovered();
    if (hovered) {
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
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
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
    if (!clips_ || !session_.has_file()) {
        return;
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(gui::describe_clipping(*clips_).c_str());
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
    const std::string gamut = gui::describe_gamut(*clips_);
    if (!gamut.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(gamut.c_str());
    }
    ImGui::PopTextWrapPos();
    if (hovered) {
        ImGui::SetTooltip(
            "Detail is lost only where a stage cuts values off at white or black.\n"
            "Until then the values are kept, so a later stage can still bring them back.");
    }
}

// The colour matrix's file name, or "none". Read-only: the matrix is loaded with --matrix.
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
        ImGui::SetTooltip("%s", utf8(s.matrix->path).c_str());
    }
}

// The tone curve's kind and its one number (gamma, or Reinhard's white point). A change of kind is
// one undo step; the number is a slider like the others. Greyed out and fixed while the stage is
// off.
void App::draw_curve_controls() {
    const gui::EditSettings& current = session_.settings();
    ImGui::BeginDisabled(!current.curve_on);

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##curve", std::string(gui::curve_kind_name(current.curve)).c_str())) {
        for (const gui::CurveKind kind : gui::curve_kinds) {
            if (ImGui::Selectable(std::string(gui::curve_kind_name(kind)).c_str(),
                                  kind == current.curve)) {
                gui::EditSettings s = current;
                gui::set_curve_kind(s, kind);
                report(session_.set_settings(s));
            }
        }
        ImGui::EndCombo();
    }
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
        ImGui::TextUnformatted(std::string(p->label).c_str());
        char text[16];
        std::snprintf(text, sizeof text, "%.2f", static_cast<double>(value));
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() +
                                                             ImGui::GetContentRegionAvail().x -
                                                             ImGui::CalcTextSize(text).x));
        ImGui::TextUnformatted(text);
        float position = gui::curve_slider_position(*p, value);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed =
            ImGui::SliderFloat("##curve_parameter", &position, 0.0f, 1.0f, "",
                               ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
        const gui::SliderFrame frame{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
        gui::EditSettings edited = current;
        // Unmoved, the value stays exactly as stored rather than going through the mapping.
        gui::set_curve_parameter(edited, changed ? gui::curve_value_at(*p, position) : value);
        report(gui::apply_slider_frame(session_, frame, edited));
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
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Swap the two halves.");
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string current = shown ? gui::stage_caption(*shown) : std::string("Off");
    if (ImGui::BeginCombo("##compare_with", current.c_str())) {
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
                }
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("Show another point in the pipeline next to the one you're viewing.\n"
                          "It is the left half; the stage you click below is the right half.");
    }
}

void App::draw_pipeline() {
    // The per-stage clipping is only worked out while this section is open.
    pipeline_open_ = ImGui::CollapsingHeader("Pipeline", ImGuiTreeNodeFlags_DefaultOpen);
    if (!pipeline_open_) {
        return;
    }
    draw_compare_controls();

    const float row_h = ImGui::GetFrameHeight();
    for (const gui::StageInfo& stage : gui::pipeline_stages()) {
        ImGui::PushID(static_cast<int>(stage.id));
        const bool enabled = gui::stage_enabled(session_.settings(), stage.id);

        if (stage.can_toggle) {
            bool on = enabled;
            if (ImGui::Checkbox("##on", &on)) {
                gui::EditSettings s = session_.settings();
                gui::set_stage_enabled(s, stage.id, on);
                report(session_.set_settings(s));  // one undo step in History
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", std::string(stage.description).c_str());
            }
        } else {
            ImGui::Dummy(ImVec2(row_h, row_h));
        }
        ImGui::SameLine();

        const bool selected = viewed_stage_ == stage.id;
        std::string row_label(stage.name);
        if (!enabled) {
            row_label += " (off)";
        }
        if (!enabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        if (ImGui::Selectable(row_label.c_str(), selected, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, row_h))) {
            viewed_stage_ = stage.id;
        }
        ImGui::PopStyleVar();
        if (!enabled) {
            ImGui::PopStyleColor();
        }
        const bool row_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
        const ImVec2 row_min = ImGui::GetItemRectMin();
        const ImVec2 row_max = ImGui::GetItemRectMax();
        const bool over_indicator =
            clips_ && draw_stage_clip_indicator(clips_->at(stage.id), row_min, row_max);
        if (row_hovered && !over_indicator) {
            ImGui::SetTooltip("%s", std::string(stage.description).c_str());
        }
        if (gui::hidden_by_view(session_.settings(), stage.id, viewed_stage_, compare_stage_)) {
            // An edit after the viewed stage changes the photo but not what is on screen.
            ImGui::Indent(row_h + ImGui::GetStyle().ItemSpacing.x);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", gui::view_hint(viewed_stage_, compare_stage_).c_str());
            ImGui::PopTextWrapPos();
            ImGui::Unindent(row_h + ImGui::GetStyle().ItemSpacing.x);
        }
        if (const std::optional<gui::SimpleControl> slider = gui::stage_slider(stage.id)) {
            ImGui::Indent(row_h + ImGui::GetStyle().ItemSpacing.x);
            draw_slider(std::string(stage.slug).c_str(), *slider, true);
            ImGui::Unindent(row_h + ImGui::GetStyle().ItemSpacing.x);
        } else if (stage.id == gui::StageId::matrix || stage.id == gui::StageId::curve) {
            ImGui::Indent(row_h + ImGui::GetStyle().ItemSpacing.x);
            if (stage.id == gui::StageId::matrix) {
                draw_matrix_name();
            } else {
                draw_curve_controls();
            }
            ImGui::Unindent(row_h + ImGui::GetStyle().ItemSpacing.x);
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
    const float width = ImGui::GetContentRegionAvail().x;
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
    ImGui::RadioButton("System", &theme, static_cast<int>(gui::ThemeChoice::system));
    ImGui::SameLine();
    ImGui::RadioButton("Light", &theme, static_cast<int>(gui::ThemeChoice::light));
    ImGui::SameLine();
    ImGui::RadioButton("Dark", &theme, static_cast<int>(gui::ThemeChoice::dark));
    p.theme = static_cast<gui::ThemeChoice>(theme);

    section_text("Interface size");
    const std::string automatic =
        "Auto (" + std::to_string(static_cast<int>(std::lround(display_scale_ * 100))) + "%)";
    const std::string current = p.interface_size == gui::interface_size_auto
                                    ? automatic
                                    : std::to_string(p.interface_size) + "%";
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Auto (200%)").x * 1.6f);
    if (ImGui::BeginCombo("##size", current.c_str())) {
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
    ImGui::SameLine();
    ImGui::TextDisabled("Auto follows the display.");

    section_text("Layout");
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Auto (200%)").x * 1.6f);
    int width = p.panel_width;
    if (ImGui::SliderInt("Panel width", &width, gui::panel_width_min, 800, "%d pt",
                         ImGuiSliderFlags_AlwaysClamp)) {
        p.panel_width = gui::clamp_panel_width(width);
    }
    const bool dragging = ImGui::IsItemActive();
    const bool released = ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::Button("Reset layout")) {
        p.panel_width = gui::panel_width_default;
    }
    ImGui::TextDisabled("You can also drag the panel's left edge.");

    ImGui::Separator();
    if (ImGui::Button("Restore defaults")) {
        gui::Preferences defaults;
        defaults.unknown = p.unknown;  // keep what a newer version of Prism wrote
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
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440 * ui_scale());
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
    ImGui::SetNextWindowSize(ImVec2(720 * ui_scale(), 520 * ui_scale()), ImGuiCond_Appearing);
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
        ImGui::TextUnformatted("Saving...");
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(240 * ui_scale(), 0),
                           "");
        if (ImGui::Button("Cancel")) {
            exporter_.cancel();
        }
        ImGui::EndPopup();
    }
}

}  // namespace prism::gui_app

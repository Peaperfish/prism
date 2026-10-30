#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <utility>

#include "imgui.h"
#include "theme.hpp"
#include "prism/gui/file_types.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/slider_gesture.hpp"
#include "prism/raw.hpp"
#include "prism/utf8_path.hpp"

namespace prism::gui_app {

namespace fs = std::filesystem;

namespace {

constexpr float panel_width = 320.0f;  // the control panel, in unscaled pixels
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

App::App(SDL_Window* window, SDL_Renderer* renderer, DialogQueue& dialogs)
    : window_(window), dialogs_(dialogs), texture_(renderer),
      display_scale_(SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay())) {}

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
    preview_.update(session_.preview_base(), session_.settings());
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
    last_render_ms_ = frame->total_ms;
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

void App::before_frame() {
    if (style_dirty_) {
        apply_style(theme(), display_scale_);
        style_dirty_ = false;
    }
}

void App::handle_event(const SDL_Event& event) {
    quiet_frames_ = 0;
    switch (event.type) {
        case SDL_EVENT_SYSTEM_THEME_CHANGED:
            style_dirty_ = true;  // "System" follows the OS live
            break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            display_scale_ = SDL_GetWindowDisplayScale(window_);
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
    const float panel = panel_width * ui_scale();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float body_height = std::max(1.0f, avail.y - status_height);

    ImGui::BeginChild("canvas", ImVec2(std::max(1.0f, avail.x - panel), body_height),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    draw_canvas();
    draw_banner();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("panel", ImVec2(0, body_height), ImGuiChildFlags_Borders);
    draw_panel();
    ImGui::EndChild();

    draw_status_bar();
    draw_saving_popup();
    ImGui::End();

    if (pending_open_) {
        pending_open_shown_ = true;
    }
}

void App::handle_shortcuts() {
    // RouteGlobal: they work wherever the focus is, unless a focused widget (e.g. a text field)
    // claims the same keys.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
        show_open_dialog();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_E, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        show_save_dialog();
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
        ImGui::EndMenu();
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
        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(
                           reinterpret_cast<std::uintptr_t>(texture_.texture()))),
                       at, ImVec2(at.x + shown.x, at.y + shown.y));
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
    ImGui::BeginChild("banner", ImVec2(ImGui::GetContentRegionAvail().x - margin, 0),
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

void App::draw_panel() {
    if (!session_.has_file()) {
        ImGui::TextDisabled("Open a photo to edit it.");
        return;
    }
    ImGui::BeginDisabled(!session_.history().can_undo());
    if (ImGui::Button("Undo")) {
        undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!session_.history().can_redo());
    if (ImGui::Button("Redo")) {
        redo();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(dialog_open_ || exporter_.busy());
    if (ImGui::Button("Export...")) {
        show_save_dialog();
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Look");
    draw_presets();

    ImGui::SeparatorText("Adjust");
    draw_simple_slider("Brightness", gui::SimpleControl::brightness);
    draw_simple_slider("Contrast", gui::SimpleControl::contrast);
    draw_simple_slider("Colour", gui::SimpleControl::colour);
    ImGui::TextDisabled("Right-click a slider to reset it.");
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
    const gui::SimpleControls positions = gui::simple_from(session_.settings());
    const gui::SimpleOutOfRange beyond = gui::outside_simple_range(session_.settings());
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
    ImGui::TextUnformatted(label);
    if (custom) {
        // Set beyond the slider's range (e.g. in Advanced mode): shown at its end until moved.
        ImGui::SameLine();
        ImGui::TextDisabled("(custom)");
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed =
        ImGui::SliderFloat("##slider", &value, gui::simple_min, gui::simple_max, "%+.0f");
    const gui::SliderFrame frame{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
    gui::EditSettings edited = session_.settings();
    gui::apply_simple_control(edited, control, value);
    // A drag updates the preview every frame but becomes one undo step, on release.
    report(gui::apply_slider_frame(session_, frame, edited));

    if (ImGui::BeginPopupContextItem("reset")) {
        if (ImGui::MenuItem("Reset to 0")) {
            gui::EditSettings s = session_.settings();
            gui::apply_simple_control(s, control, 0);
            report(session_.set_settings(s));
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void App::draw_status_bar() {
    if (!session_.has_file()) {
        ImGui::TextDisabled("No photo open");
        return;
    }
    const bool raw = session_.kind() == gui::InputKind::raw;
    std::string status = file_name(session_.path()) + "  |  " +
                         std::to_string(session_.decoded_width()) + " x " +
                         std::to_string(session_.decoded_height()) +
                         (raw ? "  |  RAW, half-size preview" : "");
    if (texture_.texture() != nullptr) {
        status += "  |  preview " + std::to_string(texture_.width()) + " x " +
                  std::to_string(texture_.height()) + ", " +
                  std::to_string(static_cast<int>(std::lround(last_render_ms_))) + " ms";
    }
    ImGui::TextDisabled("%s", status.c_str());
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

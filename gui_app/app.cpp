#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <utility>

#include "imgui.h"
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
void overlay_text(ImDrawList* draw, ImVec2 pos, const std::string& text) {
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const float pad = 6.0f * ui_scale();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x + 2 * pad, pos.y + size.y + 2 * pad),
                        IM_COL32(0, 0, 0, 170), 4.0f * ui_scale());
    draw->AddText(ImVec2(pos.x + pad, pos.y + pad), IM_COL32(255, 255, 255, 255), text.c_str());
}

std::string file_name(const fs::path& path) {
    return utf8(path.filename());
}

}  // namespace

App::App(SDL_Window* window, SDL_Renderer* renderer, DialogQueue& dialogs)
    : window_(window), dialogs_(dialogs), texture_(renderer) {}

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

void App::take_dialog_results() {
    for (DialogResult& result : dialogs_.take_all()) {
        dialog_open_ = false;
        if (!result.error.empty()) {
            show_error({gui::ErrorKind::unreadable, "Couldn't show the file dialog.",
                        result.error});
        } else if (result.path && result.kind == DialogResult::Kind::open) {
            request_open(std::move(*result.path));
        }
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

void App::handle_event(const SDL_Event& event) {
    quiet_frames_ = 0;
    switch (event.type) {
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
    return quiet_frames_ < settle_frames || preview_.rendering() || pending_open_.has_value();
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
}

void App::draw_menu() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", shortcut_label("O").c_str(), false, !dialog_open_)) {
            show_open_dialog();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
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
    draw->AddRectFilled(min, max, IM_COL32(60, 60, 60, 255));  // neutral grey
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
                    drag_over_ ? IM_COL32(120, 180, 255, 255) : IM_COL32(150, 150, 150, 255),
                    8.0f * ui_scale(), 2.0f * ui_scale());
        ImGui::SetCursorPosY(size.y / 2 - ImGui::GetFrameHeightWithSpacing() * 1.5f);
        centred_text("Drop a photo here");
        const float button = ImGui::CalcTextSize("Open photo...").x +
                             ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetCursorPosX((size.x - button) / 2);
        if (ImGui::Button("Open photo...")) {
            show_open_dialog();
        }
        ImGui::TextUnformatted("");
        centred_text(raw_support_available() ? "PNG, JPEG or RAW" : "PNG or JPEG");
    }

    if (pending_open_) {
        overlay_text(draw, ImVec2(min.x + margin, max.y - margin - 2 * ImGui::GetFontSize()),
                     "Opening " + file_name(*pending_open_) + "...");
    } else if (preview_.rendering()) {
        overlay_text(draw, ImVec2(min.x + margin, max.y - margin - 2 * ImGui::GetFontSize()),
                     "Rendering...");
    }
    if (drag_over_ && session_.has_file()) {
        draw->AddRect(min, max, IM_COL32(120, 180, 255, 255), 0.0f, 0, 3.0f * ui_scale());
    }
}

void App::draw_banner() {
    if (!error_ && notice_.empty()) {
        return;
    }
    const float margin = 12.0f * ui_scale();
    ImGui::SetCursorPos(ImVec2(margin, margin));
    const bool is_error = error_.has_value();
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          is_error ? ImVec4(0.45f, 0.12f, 0.12f, 0.95f)
                                   : ImVec4(0.15f, 0.15f, 0.15f, 0.95f));
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
    ImGui::PopStyleColor();
}

void App::draw_panel() {
    if (!session_.has_file()) {
        ImGui::TextDisabled("Open a photo to edit it.");
        return;
    }
    ImGui::TextUnformatted(file_name(session_.path()).c_str());
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

}  // namespace prism::gui_app

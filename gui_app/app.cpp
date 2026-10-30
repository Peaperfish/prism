#include "app.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

namespace prism::gui_app {

namespace {

constexpr float panel_width = 320.0f;  // the control panel, in unscaled pixels
// ImGui sometimes needs a frame or two after an event to settle (hover, popups opening).
constexpr int settle_frames = 3;

float ui_scale() {
    return ImGui::GetStyle().FontScaleDpi;
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
void centred_text(const char* text) {
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) / 2 +
                                            ImGui::GetCursorPosX()));
    ImGui::TextUnformatted(text);
}

}  // namespace

void App::handle_event(const SDL_Event&) {
    quiet_frames_ = 0;
}

bool App::animating() const {
    return quiet_frames_ < settle_frames;
}

void App::frame() {
    ++quiet_frames_;

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
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("panel", ImVec2(0, body_height), ImGuiChildFlags_Borders);
    draw_panel();
    ImGui::EndChild();

    draw_status_bar();
    ImGui::End();
}

void App::draw_menu() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
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

    // Empty state: a dashed drop area with the one thing to do.
    const float inset = 24.0f * ui_scale();
    dashed_rect(draw, ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, max.y - inset),
                IM_COL32(150, 150, 150, 255), 8.0f * ui_scale(), 2.0f * ui_scale());
    ImGui::SetCursorPosY(size.y / 2 - ImGui::GetTextLineHeightWithSpacing() * 1.5f);
    centred_text("Drop a photo here");
    centred_text("PNG, JPEG or RAW");
}

void App::draw_panel() {
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("Open a photo to edit it.");
    ImGui::EndDisabled();
}

void App::draw_status_bar() {
    ImGui::TextDisabled("No photo open");
}

}  // namespace prism::gui_app

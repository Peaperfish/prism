#include <gtest/gtest.h>

#include "active_control.hpp"
#include "imgui.h"
#include "imgui_internal.h"

namespace {

using prism::gui_app::control_is_active;

// A headless ImGui: a context, a display size and a built font atlas, with no backend; the mouse
// is fed through the input queue.
class ImGuiHeadless : public ::testing::Test {
protected:
    void SetUp() override {
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(400, 300);
        io.Fonts->AddFontDefault();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    void TearDown() override {
        ImGui::DestroyContext(context_);
    }

    // One frame with a window at (0, 0) of 200 x 200 that holds a button at its top left, the rest
    // being empty space. Returns whether the button was clicked this frame.
    bool frame() {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(200, 200));
        ImGui::Begin("panel", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoTitleBar);
        const bool clicked = ImGui::Button("button", ImVec2(60, 30));
        ImGui::End();
        ImGui::Render();
        return clicked;
    }

    void mouse_to(float x, float y) {
        ImGui::GetIO().AddMousePosEvent(x, y);
    }
    void mouse(bool down) {
        ImGui::GetIO().AddMouseButtonEvent(0, down);
    }

    ImGuiContext* context_ = nullptr;
};

TEST_F(ImGuiHeadless, NothingIsActiveUntilAPress) {
    mouse_to(150, 150);
    frame();
    frame();
    EXPECT_FALSE(control_is_active(false));
}

TEST_F(ImGuiHeadless, WidgetPressOnEmptyPanelSpaceIsNotAControl) {
    mouse_to(150, 150);  // the window's padding, away from the button
    frame();
    frame();
    mouse(true);
    frame();
    // ImGui does count it as an active item (the window's move id)...
    EXPECT_TRUE(ImGui::IsAnyItemActive());
    // ...but it is not a control: shortcuts and the clip report must carry on.
    EXPECT_FALSE(control_is_active(false));
    frame();
    EXPECT_FALSE(control_is_active(false));
    mouse(false);
    frame();
    EXPECT_FALSE(control_is_active(false));
}

TEST_F(ImGuiHeadless, WidgetPressOnAButtonIsAControl) {
    mouse_to(20, 15);  // on the button
    frame();
    frame();
    mouse(true);
    frame();
    EXPECT_TRUE(ImGui::IsAnyItemActive());
    EXPECT_TRUE(control_is_active(false));
    mouse(false);
    frame();
    EXPECT_FALSE(control_is_active(false));
}

TEST_F(ImGuiHeadless, WidgetPressThatBeganOnTheCanvasIsNotAControl) {
    mouse_to(20, 15);
    frame();
    frame();
    mouse(true);
    frame();
    EXPECT_FALSE(control_is_active(true));
}

}  // namespace

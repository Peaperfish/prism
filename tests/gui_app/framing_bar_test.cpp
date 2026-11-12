#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "framing_bar.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "prism/frame.hpp"
#include "prism/gui/explanations.hpp"
#include "prism/gui/framing.hpp"
#include "prism/gui/layout.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace {

using namespace prism::gui_app;
namespace gui = prism::gui;

// The framing bar (#58) with a headless ImGui and the app's real fonts, style and widgets, in a
// window like the app's canvas column: the mouse fed through the input queue, every callback
// writing down what it was told.
class FramingBarHeadless : public ::testing::Test {
protected:
    void SetUp() override {
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(2400, 800);
        load_fonts(io);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        apply_style(gui::Theme::dark, 1.0f);
        data_.live_line = gui::framing_live_line({5120, 3413}, {6000, 4000}, 1.2f);
        data_.hint = std::string(gui::framing_hint(true));
        data_.angle = 1.2f;
    }
    void TearDown() override {
        ImGui::DestroyContext(context_);
    }

    FramingBarCallbacks callbacks() {
        FramingBarCallbacks cb;
        cb.begin_turn = [this] { calls_.push_back("begin_turn"); };
        cb.turn_to = [this](float a) {
            calls_.push_back("turn_to");
            angles_.push_back(a);
            data_.angle = a;  // as the app's draft does
        };
        cb.end_turn = [this] { calls_.push_back("end_turn"); };
        cb.set_angle = [this](float a) {
            calls_.push_back("set_angle");
            angles_.push_back(a);
        };
        cb.set_aspect = [this](prism::Aspect a) { aspects_.push_back(a); };
        cb.turn_aspect = [this] { calls_.push_back("turn_aspect"); };
        cb.reset = [this] { calls_.push_back("reset"); };
        cb.done = [this] { calls_.push_back("done"); };
        cb.describe = [this](std::string_view id) { ids_.insert(std::string(id)); };
        cb.hover = [this](std::string_view id) { ids_.insert(std::string(id)); };
        return cb;
    }

    // One frame of the bar, `width_` wide, at (10, 20) in a window as tall as the screen.
    FramingBarRects frame() {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("canvas column", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImGui::SetCursorScreenPos(ImVec2(10, 20));
        FramingBarRects rects;
        draw_framing_bar(data_, callbacks(), width_, &rects);
        bottom_ = ImGui::GetCursorScreenPos().y;
        ImGui::End();
        ImGui::EndFrame();
        return rects;
    }
    FramingBarRects settled() {
        frame();
        return frame();
    }

    void mouse_to(ImVec2 at) {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
    }
    void mouse(bool down) {
        ImGui::GetIO().AddMouseButtonEvent(0, down);
    }
    void click_at(ImVec2 at) {
        mouse_to(at);
        frame();
        frame();
        mouse(true);
        frame();
        mouse(false);
        frame();
    }
    static ImVec2 middle(ImVec2 min, ImVec2 max) {
        return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    }

    // The narrowest canvas the window allows at `scale`, and a wide one.
    static std::vector<float> widths(float scale) {
        return {static_cast<float>(gui::canvas_width_min) * scale, 420.0f * scale,
                640.0f * scale, 900.0f * scale, 1600.0f * scale};
    }

    static void expect_inside(ImVec2 min, ImVec2 max, const FramingBarRects& r, const char* what,
                              float width) {
        EXPECT_GE(min.x, r.bar_min.x) << what << " at " << width;
        EXPECT_GE(min.y, r.bar_min.y) << what << " at " << width;
        EXPECT_LE(max.x, r.bar_max.x + 0.5f) << what << " at " << width;
        EXPECT_LE(max.y, r.bar_max.y + 0.5f) << what << " at " << width;
    }
    static bool overlap(ImVec2 amin, ImVec2 amax, ImVec2 bmin, ImVec2 bmax) {
        return amin.x < bmax.x && bmin.x < amax.x && amin.y < bmax.y && bmin.y < amax.y;
    }

    ImGuiContext* context_ = nullptr;
    FramingBarData data_;
    float width_ = 900.0f;
    float bottom_ = 0.0f;
    std::vector<std::string> calls_;
    std::vector<float> angles_;
    std::vector<prism::Aspect> aspects_;
    std::set<std::string> ids_;
};

TEST_F(FramingBarHeadless, TheBarIsOneHeightWhateverItShowsAndHoweverWide) {
    ImGui::NewFrame();
    const float height = framing_bar_height();
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    ImGui::EndFrame();
    for (const float width : widths(1.0f)) {
        width_ = width;
        for (const float angle : {0.0f, 1.2f, -44.95f, -0.04f}) {
            for (const prism::Aspect aspect : prism::all_aspects) {
                data_.angle = angle;
                data_.aspect = aspect;
                const FramingBarRects r = settled();
                EXPECT_FLOAT_EQ(r.bar_max.y - r.bar_min.y, height) << width;
                EXPECT_FLOAT_EQ(bottom_ - 20.0f, height + spacing) << width;  // the cursor under it
            }
        }
    }
}

TEST_F(FramingBarHeadless, TheHeightIsTheSameAtEveryInterfaceSizeAndTheme) {
    apply_style(gui::Theme::dark, 1.0f);
    ImGui::NewFrame();
    const float base = framing_bar_height();
    ImGui::EndFrame();
    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
        for (const gui::Theme theme : {gui::Theme::dark, gui::Theme::light}) {
            apply_style(theme, scale);
            width_ = 900.0f * scale;
            const FramingBarRects r = settled();
            ImGui::NewFrame();
            const float expected = framing_bar_height();
            ImGui::EndFrame();
            EXPECT_FLOAT_EQ(r.bar_max.y - r.bar_min.y, expected) << scale;
            // The design's height at that size, give or take the fonts' rounding.
            EXPECT_NEAR(expected, base * scale, base * scale * 0.06f) << scale;
        }
    }
}

TEST_F(FramingBarHeadless, EveryPartStaysInsideTheBarFromTheNarrowestCanvasUp) {
    for (const float scale : {1.0f, 1.5f, 2.0f}) {
        apply_style(gui::Theme::dark, scale);
        for (const float width : widths(scale)) {
            width_ = width;
            data_.angle = -44.95f;  // the widest words
            data_.aspect = prism::Aspect::sixteen_nine;
            const FramingBarRects r = settled();
            expect_inside(r.aspect_min, r.aspect_max, r, "aspect", width);
            expect_inside(r.turn_min, r.turn_max, r, "turn", width);
            expect_inside(r.reset_min, r.reset_max, r, "reset", width);
            expect_inside(r.done_min, r.done_max, r, "done", width);
            expect_inside(r.slider_min, r.slider_max, r, "slider", width);
            expect_inside(r.words_min, r.words_max, r, "words", width);
            expect_inside(r.live_min, r.live_max, r, "live line", width);
            expect_inside(r.hint_min, r.hint_max, r, "hint", width);
            // Nothing on a line covers anything else on it.
            EXPECT_FALSE(overlap(r.aspect_min, r.aspect_max, r.turn_min, r.turn_max)) << width;
            EXPECT_FALSE(overlap(r.turn_min, r.turn_max, r.reset_min, r.reset_max)) << width;
            EXPECT_FALSE(overlap(r.reset_min, r.reset_max, r.done_min, r.done_max)) << width;
            EXPECT_FALSE(overlap(r.slider_min, r.slider_max, r.words_min, r.words_max)) << width;
            EXPECT_FALSE(overlap(r.slider_min, r.slider_max, r.reset_min, r.reset_max)) << width;
            EXPECT_FALSE(overlap(r.words_min, r.words_max, r.reset_min, r.reset_max)) << width;
            EXPECT_FALSE(overlap(r.aspect_min, r.aspect_max, r.done_min, r.done_max)) << width;
            EXPECT_FALSE(overlap(r.turn_min, r.turn_max, r.done_min, r.done_max)) << width;
        }
    }
}

TEST_F(FramingBarHeadless, TheDesignsWordsShowWhereThereIsRoomAndGiveWayWhereNot) {
    width_ = 1600.0f;
    FramingBarRects r = settled();
    EXPECT_EQ(r.done_label, "Done (Return)");
    EXPECT_EQ(r.live_shown,
              "Frame 5120 \xC3\x97 3413 of 6000 \xC3\x97 4000 (73% of the photo), turned 1.2\xC2\xB0 "
              "clockwise.");
    EXPECT_EQ(r.hint_shown,
              "Drag the blades to frame the print \xE2\x80\x94 drag inside to move them, "
              "\xE2\x8C\xA5-drag along the horizon to level it.");
    // The narrowest canvas: "Done", and the two text lines shortened, never past the bar.
    width_ = static_cast<float>(gui::canvas_width_min);
    r = settled();
    EXPECT_EQ(r.done_label, "Done");
    EXPECT_NE(r.hint_shown, data_.hint);
    EXPECT_NE(r.hint_shown.find("\xE2\x80\xA6"), std::string::npos);
}

TEST_F(FramingBarHeadless, EveryGlyphTheBarDrawsIsInTheFont) {
    // ⌥, —, ×, °, ½ and the minus sign: the interface font must have them, or they draw as "?".
    std::string all = std::string(gui::framing_hint(true)) + std::string(gui::framing_hint(false)) +
                      gui::framing_live_line({5120, 3413}, {6000, 4000}, -1.2f) +
                      gui::framing_turn_words(-0.04f) + gui::typed_angle_text(-1.25f);
    for (const prism::Aspect aspect : prism::all_aspects) {
        all += std::string(gui::framing_aspect_menu_text(aspect));
    }
    ImGui::NewFrame();
    ImFont* font = ImGui::GetFont();
    const char* p = all.c_str();
    const char* const end = p + all.size();
    while (p < end) {
        unsigned int c = 0;
        const int n = ImTextCharFromUtf8(&c, p, end);
        ASSERT_GT(n, 0);
        EXPECT_TRUE(font->IsGlyphInFont(static_cast<ImWchar>(c))) << "U+" << std::hex << c;
        p += n;
    }
    ImGui::EndFrame();
}

TEST_F(FramingBarHeadless, TheSliderIsOneGestureAndTypingIsOneStep) {
    data_.angle = 0.0f;
    FramingBarRects r = settled();
    const ImVec2 at = middle(r.slider_min, r.slider_max);
    mouse_to(at);
    frame();
    mouse(true);
    frame();
    for (int i = 1; i <= 5; ++i) {
        mouse_to(ImVec2(at.x + 10.0f * static_cast<float>(i), at.y));
        frame();
    }
    mouse(false);
    frame();
    frame();
    ASSERT_FALSE(calls_.empty());
    EXPECT_EQ(calls_.front(), "begin_turn");
    EXPECT_EQ(calls_.back(), "end_turn");
    EXPECT_EQ(std::count(calls_.begin(), calls_.end(), "begin_turn"), 1);
    EXPECT_EQ(std::count(calls_.begin(), calls_.end(), "end_turn"), 1);
    ASSERT_FALSE(angles_.empty());
    for (const float a : angles_) {
        EXPECT_GE(a, -45.0f);
        EXPECT_LE(a, 45.0f);
        // In steps of 0.1°.
        EXPECT_NEAR(a * 10.0f, std::round(a * 10.0f), 1e-3f) << a;
    }
    EXPECT_GT(angles_.back(), 0.0f);  // to the right: clockwise
}

TEST_F(FramingBarHeadless, TheButtonsAndTheAspectMenuDoWhatTheySay) {
    FramingBarRects r = settled();
    click_at(middle(r.reset_min, r.reset_max));
    click_at(middle(r.done_min, r.done_max));
    click_at(middle(r.turn_min, r.turn_max));
    EXPECT_EQ(calls_, (std::vector<std::string>{"reset", "done", "turn_aspect"}));
    // The menu: its entries are #58's, and a pick is one call.
    click_at(middle(r.aspect_min, r.aspect_max));
    frame();
    ImGuiWindow* popup = ImGui::GetCurrentContext()->OpenPopupStack.empty()
                             ? nullptr
                             : ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
    ASSERT_NE(popup, nullptr);
    const float item = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
    // Original, Free, 1:1, then 4:5.
    click_at(ImVec2(popup->Pos.x + popup->Size.x * 0.5f, popup->DC.CursorStartPos.y + item * 3.5f));
    ASSERT_EQ(aspects_.size(), 1u);
    EXPECT_EQ(aspects_[0], prism::Aspect::four_five);
}

TEST_F(FramingBarHeadless, EveryControlExplainsItself) {
    FramingBarRects r = settled();
    mouse_to(middle(r.words_min, r.words_max));
    frame();
    mouse_to(middle(r.live_min, r.live_max));
    frame();
    for (const std::string_view id :
         {gui::explain::framing_aspect, gui::explain::framing_turn_aspect, gui::explain::framing_reset,
          gui::explain::framing_done, gui::explain::framing_straighten, gui::explain::framing_angle,
          gui::explain::easel}) {
        EXPECT_EQ(ids_.count(std::string(id)), 1u) << id;
        EXPECT_NE(gui::explanation_for(id), nullptr) << id;
    }
}

}  // namespace

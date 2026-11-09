#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "active_control.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "prism/gui/explanations.hpp"
#include "prism/gui/histogram.hpp"
#include "scopes_panel.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace {

using namespace prism::gui_app;
namespace gui = prism::gui;

constexpr float panel_width = 440.0f;
constexpr std::array<gui::ScopeChoice, 4> all_scopes{
    gui::ScopeChoice::histogram, gui::ScopeChoice::parade, gui::ScopeChoice::waveform,
    gui::ScopeChoice::vectorscope};

gui::StageHistogram some_histogram(std::uint64_t below = 1, std::uint64_t above = 0) {
    gui::StageHistogram h;
    h.pixel_count = 1000;
    h.any_under_black = below;
    h.any_over_white = above;
    h.red_clips.under_black = below;
    h.green_clips.over_white = above;
    for (std::size_t b = 0; b < h.red.size(); ++b) {
        h.red[b] = b % 7;
        h.green[b] = b % 5;
        h.blue[b] = b % 3;
        h.luminance[b] = b % 11;
    }
    return h;
}

// The section with a headless ImGui and the app's real fonts, style and widgets: no backend, the
// mouse fed through the input queue, and every callback writing down what it was told.
class ScopesHeadless : public ::testing::Test {
protected:
    void SetUp() override {
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(600, 800);
        load_fonts(io);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        apply_style(gui::Theme::dark, 1.0f);
        data_.plot_texture = 1;
        data_.clip_sentence = "Highlights clip at Output encode (2.0% of pixels).";
    }
    void TearDown() override {
        ImGui::DestroyContext(context_);
    }

    struct Placed {
        ScopesRects rects;
        float bottom = 0.0f;
    };

    ScopesCallbacks callbacks() {
        ScopesCallbacks cb;
        cb.choose_scope = [this](gui::ScopeChoice s) { chosen_.push_back(s); };
        cb.toggle_clip_overlay = [this] { ++toggles_; };
        cb.explain = [this](std::string_view id) {
            ids_.insert(std::string(id));
            if (ImGui::IsItemHovered()) {
                hovered_ids_.insert(std::string(id));
            }
        };
        cb.describe = cb.explain;
        cb.hover = [this](std::string_view id) {
            ids_.insert(std::string(id));
            hovered_ids_.insert(std::string(id));
        };
        return cb;
    }

    // One frame of the section in a window like the app's panel.
    Placed frame() {
        hovered_ids_.clear();
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(panel_width, 780));
        ImGui::Begin("panel", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoTitleBar);
        ScopesData data = data_;
        data.histogram = histogram_ ? &*histogram_ : nullptr;
        Placed placed;
        placed.rects = draw_scopes(data, callbacks());
        placed.bottom = ImGui::GetCursorScreenPos().y;
        ImGui::End();
        ImGui::EndFrame();
        return placed;
    }
    // Twice: what a change shows comes through on the frame after it.
    Placed settled() {
        frame();
        return frame();
    }

    static void expect_same(const Placed& a, const Placed& b) {
        const auto same = [](ImVec2 p, ImVec2 q) {
            EXPECT_FLOAT_EQ(p.x, q.x);
            EXPECT_FLOAT_EQ(p.y, q.y);
        };
        EXPECT_FLOAT_EQ(a.bottom, b.bottom);
        same(a.rects.tabs_min, b.rects.tabs_min);
        same(a.rects.tabs_max, b.rects.tabs_max);
        same(a.rects.plot_min, b.rects.plot_min);
        same(a.rects.plot_max, b.rects.plot_max);
        same(a.rects.readout_min, b.rects.readout_min);
        same(a.rects.readout_max, b.rects.readout_max);
    }

    void mouse_to(ImVec2 at) {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
    }
    void mouse(bool down) {
        ImGui::GetIO().AddMouseButtonEvent(0, down);
    }
    static ImVec2 middle(ImVec2 min, ImVec2 max) {
        return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    }

    ImGuiContext* context_ = nullptr;
    ScopesData data_;
    std::optional<gui::StageHistogram> histogram_ = some_histogram();
    std::vector<gui::ScopeChoice> chosen_;
    int toggles_ = 0;
    std::set<std::string> ids_;
    std::set<std::string> hovered_ids_;
};

// --- #38: nothing that is shown moves anything ----------------------------------------------------

TEST_F(ScopesHeadless, AllFourScopesTakeTheSameHeightWithAndWithoutData) {
    const Placed first = settled();
    for (const bool have_data : {true, false}) {
        histogram_ = have_data ? std::optional(some_histogram()) : std::nullopt;
        for (const gui::ScopeChoice scope : all_scopes) {
            for (const ImTextureID texture : {ImTextureID{1}, ImTextureID{0}}) {
                data_.scope = scope;
                data_.plot_texture = texture;
                expect_same(settled(), first);
            }
        }
    }
}

TEST_F(ScopesHeadless, ThePlotBoxIsTheDesignsHeight) {
    const Placed p = settled();
    EXPECT_FLOAT_EQ(p.rects.plot_max.y - p.rects.plot_min.y, scopes_plot_height());
    EXPECT_FLOAT_EQ(scopes_plot_height(), std::round(gui::mock(132.0f, 1.0f)));
    EXPECT_FLOAT_EQ(p.rects.plot_max.x - p.rects.plot_min.x, p.rects.tabs_max.x - p.rects.tabs_min.x);
}

TEST_F(ScopesHeadless, TheReadoutLinesTextChangesNothing) {
    const Placed first = settled();
    for (const auto& [below, above] :
         {std::array<std::uint64_t, 2>{0, 0}, {1, 999}, {999, 1}, {1000, 1000}, {123, 70}}) {
        histogram_ = some_histogram(below, above);
        expect_same(settled(), first);
    }
}

TEST_F(ScopesHeadless, TheReadoutKeepsItsHeightWithoutData) {
    const Placed with = settled();
    histogram_.reset();
    const Placed without = settled();
    expect_same(without, with);
    EXPECT_GT(without.rects.readout_max.y, without.rects.readout_min.y);
    EXPECT_GE(without.rects.readout_max.y - without.rects.readout_min.y, ImGui::GetFontSize() * 0.8f);
}

TEST_F(ScopesHeadless, AnEmptyHistogramIsNoData) {
    const Placed first = settled();
    histogram_ = gui::StageHistogram{};
    expect_same(settled(), first);
}

TEST_F(ScopesHeadless, TheTitleAndTheOverlayStateChangeNothing) {
    const Placed first = settled();
    data_.clip_overlay_on = true;
    expect_same(settled(), first);
    data_.detail = "Exposure";
    expect_same(settled(), first);
    data_.detail = "Colour matrix, a band with a very long title that cannot fit beside the action "
                   "in a panel of this width at all";
    expect_same(settled(), first);
}

TEST_F(ScopesHeadless, HoveringAnythingMovesNothing) {
    for (const gui::ScopeChoice scope : all_scopes) {
        data_.scope = scope;
        mouse_to(ImVec2(-100, -100));
        const Placed first = settled();
        const std::array<ImVec2, 6> places{
            middle(first.rects.action_min, first.rects.action_max),
            middle(first.rects.tabs_min, first.rects.tabs_max),
            middle(first.rects.plot_min, first.rects.plot_max),
            first.rects.readout_min,
            middle(first.rects.readout_min, first.rects.readout_max),
            ImVec2(first.rects.readout_max.x - 1.0f, first.rects.readout_min.y + 1.0f),
        };
        for (const ImVec2 at : places) {
            mouse_to(at);
            // Past the tooltip's delay: it opens a window of its own.
            for (int i = 0; i < 40; ++i) {
                expect_same(frame(), first);
            }
        }
    }
}

TEST_F(ScopesHeadless, TheHeightIsTheSameAtEveryInterfaceSizeAndTheme) {
    for (const float scale : {0.75f, 1.0f, 1.5f, 2.0f}) {
        for (const gui::Theme theme : {gui::Theme::dark, gui::Theme::light}) {
            apply_style(theme, scale);
            data_.theme = theme;
            data_.scope = gui::ScopeChoice::histogram;
            const Placed first = settled();
            EXPECT_FLOAT_EQ(first.rects.plot_max.y - first.rects.plot_min.y,
                            std::round(gui::mock(132.0f, scale)));
            for (const gui::ScopeChoice scope : all_scopes) {
                data_.scope = scope;
                expect_same(settled(), first);
            }
        }
    }
}

TEST_F(ScopesHeadless, ThePlotComesUnderTheTabsAndTheReadoutUnderThePlot) {
    const Placed p = settled();
    EXPECT_GT(p.rects.tabs_min.y, p.rects.action_min.y);
    EXPECT_GE(p.rects.plot_min.y, p.rects.tabs_max.y);
    EXPECT_GE(p.rects.readout_min.y, p.rects.plot_max.y);
    EXPECT_GE(p.bottom, p.rects.readout_max.y);
}

// --- The Show clipping action -----------------------------------------------------------------------

TEST_F(ScopesHeadless, APressOnTheClippingActionTogglesAtOnceAndHoldsNothing) {
    const Placed p = settled();
    mouse_to(middle(p.rects.action_min, p.rects.action_max));
    settled();
    mouse(true);
    frame();
    EXPECT_EQ(toggles_, 1);
    EXPECT_FALSE(control_is_active(false));
    data_.clip_overlay_on = true;  // the label changes under the mouse
    frame();
    EXPECT_FALSE(control_is_active(false));
    EXPECT_EQ(toggles_, 1);  // held: once
    mouse(false);
    settled();
    EXPECT_EQ(toggles_, 1);  // let go: no second time

    mouse(true);
    frame();
    EXPECT_EQ(toggles_, 2);
    mouse(false);
    frame();
}

TEST_F(ScopesHeadless, TheClippingActionIsAtTheRightEdgeAndExplainsItself) {
    const Placed p = settled();
    EXPECT_NEAR(p.rects.action_max.x, p.rects.tabs_max.x, 1.0f);
    mouse_to(middle(p.rects.action_min, p.rects.action_max));
    settled();
    EXPECT_EQ(hovered_ids_.count(std::string(gui::explain::clip_overlay)), 1u);
}

TEST_F(ScopesHeadless, ALongTitleLeavesTheActionWhereItIs) {
    const Placed first = settled();
    data_.detail = "Colour matrix, a band with a very long title that cannot fit beside the action "
                   "in a panel of this width at all";
    const Placed p = settled();
    EXPECT_FLOAT_EQ(p.rects.action_min.x, first.rects.action_min.x);
    EXPECT_FLOAT_EQ(p.rects.action_max.x, first.rects.action_max.x);
}

// --- The tabs and the plot ----------------------------------------------------------------------------

TEST_F(ScopesHeadless, ATabChoosesItsScopeInOrder) {
    const Placed p = settled();
    const float width = p.rects.tabs_max.x - p.rects.tabs_min.x;
    for (int i = 3; i >= 0; --i) {
        // The current one, Histogram, answers nothing.
        mouse_to(ImVec2(p.rects.tabs_min.x + width * (static_cast<float>(i) + 0.5f) / 4.0f,
                        (p.rects.tabs_min.y + p.rects.tabs_max.y) * 0.5f));
        settled();
        mouse(true);
        frame();
        mouse(false);
        settled();
    }
    EXPECT_EQ(chosen_, (std::vector<gui::ScopeChoice>{gui::ScopeChoice::vectorscope,
                                                      gui::ScopeChoice::waveform,
                                                      gui::ScopeChoice::parade}));
}

TEST_F(ScopesHeadless, APressOnThePlotOrTheReadoutIsNotAControlBeingUsed) {
    const Placed p = settled();
    for (const ImVec2 at : {middle(p.rects.plot_min, p.rects.plot_max),
                            middle(p.rects.readout_min, p.rects.readout_max)}) {
        mouse_to(at);
        settled();
        mouse(true);
        frame();
        EXPECT_FALSE(control_is_active(false));
        frame();
        EXPECT_FALSE(control_is_active(false));
        mouse(false);
        settled();
    }
    EXPECT_EQ(toggles_, 0);
    EXPECT_TRUE(chosen_.empty());
}

TEST_F(ScopesHeadless, ThePlotExplainsTheChosenScope) {
    for (const gui::ScopeChoice scope : all_scopes) {
        data_.scope = scope;
        const Placed p = settled();
        // Away from the vectorscope's skin line.
        mouse_to(ImVec2(p.rects.plot_min.x + 3.0f, p.rects.plot_max.y - 3.0f));
        settled();
        EXPECT_EQ(hovered_ids_, (std::set<std::string>{std::string(gui::explain_id(scope))}));
    }
}

TEST_F(ScopesHeadless, TheVectorscopesSkinLineExplainsItself) {
    data_.scope = gui::ScopeChoice::vectorscope;
    const Placed p = settled();
    // A little up and to the left of the centre: the line runs at 123 degrees.
    const ImVec2 centre = middle(p.rects.plot_min, p.rects.plot_max);
    mouse_to(ImVec2(centre.x - 0.545f * 30.0f, centre.y - 0.839f * 30.0f));
    settled();
    EXPECT_EQ(hovered_ids_.count(std::string(gui::explain::scope_skin)), 1u);
}

// --- The readout's explanations and tooltip -----------------------------------------------------------

TEST_F(ScopesHeadless, EachPartOfTheReadoutExplainsItsOwnThing) {
    const Placed p = settled();
    const float y = (p.rects.readout_min.y + p.rects.readout_max.y) * 0.5f;
    mouse_to(ImVec2(p.rects.readout_min.x + 4.0f, y));
    settled();
    EXPECT_EQ(hovered_ids_.count(std::string(gui::explain::chip_under)), 1u);
    mouse_to(ImVec2(p.rects.readout_max.x - 4.0f, y));
    settled();
    EXPECT_EQ(hovered_ids_.count(std::string(gui::explain::chip_over)), 1u);
    mouse_to(ImVec2((p.rects.readout_min.x + p.rects.readout_max.x) * 0.5f, y));
    settled();
    EXPECT_EQ(hovered_ids_.count(std::string(gui::explain::clip_summary)), 1u);
}

TEST_F(ScopesHeadless, TheReadoutsTooltipHoldsTheClippingSentence) {
    const Placed p = settled();
    const float y = (p.rects.readout_min.y + p.rects.readout_max.y) * 0.5f;
    const auto tooltip_at = [&](float x) {
        mouse_to(ImVec2(x, y));
        // HoveredIdTimer starts once the item is hovered: rest there past the tooltip's delay.
        for (int i = 0; i < 90; ++i) {
            frame();
        }
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::LogToBuffer();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(panel_width, 780));
        ImGui::Begin("panel", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoTitleBar);
        ScopesData data = data_;
        data.histogram = histogram_ ? &*histogram_ : nullptr;
        draw_scopes(data, callbacks());
        ImGui::End();
        const std::string logged = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::EndFrame();
        return logged;
    };
    const std::string middle_text = tooltip_at((p.rects.readout_min.x + p.rects.readout_max.x) * 0.5f);
    EXPECT_NE(middle_text.find(data_.clip_sentence), std::string::npos) << middle_text;
    EXPECT_EQ(middle_text.find("R 0."), std::string::npos) << middle_text;
    const std::string note(gui::readout_overlay_note);
    EXPECT_EQ(middle_text.find(note), std::string::npos) << middle_text;
    // An end adds its channels, as the old chips' tooltips had them.
    const std::string left_text = tooltip_at(p.rects.readout_min.x + 4.0f);
    EXPECT_NE(left_text.find(data_.clip_sentence), std::string::npos) << left_text;
    EXPECT_NE(left_text.find("Below black:  R 0.10%   G 0.00%   B 0.00%"), std::string::npos) << left_text;
    EXPECT_NE(left_text.find(note), std::string::npos) << left_text;
    histogram_ = some_histogram(0, 20);
    const std::string right_text = tooltip_at(p.rects.readout_max.x - 4.0f);
    EXPECT_NE(right_text.find("Above white:  R 0.00%   G 2.00%   B 0.00%"), std::string::npos) << right_text;
    EXPECT_NE(right_text.find(note), std::string::npos) << right_text;
}

TEST_F(ScopesHeadless, EveryControlAskedForHasAnEntry) {
    for (const gui::ScopeChoice scope : all_scopes) {
        data_.scope = scope;
        const Placed p = settled();
        const float y = (p.rects.readout_min.y + p.rects.readout_max.y) * 0.5f;
        mouse_to(ImVec2(p.rects.readout_min.x + 4.0f, y));
        settled();
        mouse_to(ImVec2(p.rects.readout_max.x - 4.0f, y));
        settled();
        mouse_to(ImVec2((p.rects.readout_min.x + p.rects.readout_max.x) * 0.5f, y));
        settled();
    }
    EXPECT_GE(ids_.size(), 7u);
    for (const std::string& id : ids_) {
        EXPECT_NE(gui::explanation_for(id), nullptr) << id;
    }
}

}  // namespace

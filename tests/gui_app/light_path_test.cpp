#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "active_control.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "light_path.hpp"
#include "prism/gui/explanations.hpp"
#include "prism/gui/light_path.hpp"
#include "prism/gui/settings.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace {

using namespace prism::gui_app;
namespace gui = prism::gui;

constexpr float panel_width = 440.0f;
constexpr float scroll_room = 1400.0f;

gui::ClipReport report_with(gui::ClipFate fate) {
    gui::ClipReport report;
    report.sample_count = 1000;
    for (const gui::StageInfo& stage : gui::pipeline_stages()) {
        gui::StageClip& clip = report.stages[static_cast<std::size_t>(stage.id)];
        clip.stage = stage.id;
        clip.ran = true;
        clip.over.any = clip.under.any = clip.gamut.any = 70;
        clip.over_fate = clip.under_fate = clip.gamut_fate = fate;
    }
    return report;
}

// The panel with a headless ImGui and the app's real fonts, style and widgets: no backend, the
// mouse fed through the input queue, and every callback writing down what it was told.
class LightPathHeadless : public ::testing::Test {
protected:
    void SetUp() override {
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(600, scroll_room + 100);
        load_fonts(io);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        apply_style(gui::Theme::dark, 1.0f);
        settings_.exposure_stops = 0.5f;
    }
    void TearDown() override {
        ImGui::DestroyContext(context_);
    }

    LightPathCallbacks callbacks() {
        LightPathCallbacks cb;
        cb.view_stage = [this](gui::StageId s) { viewed_calls_.push_back(s); };
        cb.set_stage_on = [this](gui::StageId s, bool on) { switches_.push_back({s, on}); };
        cb.slider = [this](gui::SimpleControl c, float position, const gui::SliderFrame& f) {
            slider_frames_.push_back({c, position, f});
        };
        cb.reset_slider = [this](gui::SimpleControl c) { resets_.push_back(c); };
        cb.set_curve_kind = [this](gui::CurveKind k) { curves_.push_back(k); };
        cb.curve_parameter = [this](float, const gui::SliderFrame&) { ++parameter_frames_; };
        cb.reset_curve_parameter = [this] { ++parameter_resets_; };
        cb.toggle_strip = [this] { ++strip_toggles_; };
        cb.set_compare = [this](std::optional<gui::StageId> s) { compares_.push_back(s); };
        cb.swap_compare = [this] { ++swaps_; };
        const auto note = [this](std::string_view id) {
            ids_.insert(std::string(id));
            alphas_.push_back(ImGui::GetStyle().Alpha);
            // Every control's place this frame, in order: a row's parts must not move.
            items_.push_back({ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().y});
        };
        cb.explain = [note](std::string_view id, bool) { note(id); };
        cb.describe = [this, note](std::string_view id) {
            note(id);
            describe_alphas_.push_back(ImGui::GetStyle().Alpha);
        };
        cb.hover = [this](std::string_view id) { ids_.insert(std::string(id)); };
        return cb;
    }

    // One frame of the panel in a window like the app's panel.
    std::vector<LightPathRowRect> frame() {
        items_.clear();
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(panel_width, scroll_room));
        ImGui::Begin("panel", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoTitleBar);
        LightPathData data;
        data.view = gui::light_path_view({settings_, kind_, "RAW", viewed_, compare_,
                                          clips_ ? &*clips_ : nullptr});
        data.theme = gui::Theme::dark;
        data.strip_on = strip_on_;
        data.strip_available = strip_available_;
        data.strip_hint = "The hint.";
        const std::vector<LightPathRowRect> rects = draw_light_path(data, callbacks());
        bottom_ = ImGui::GetCursorScreenPos().y;
        ImGui::End();
        ImGui::EndFrame();
        return rects;
    }

    void mouse_to(ImVec2 at) {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
    }
    void mouse(bool down, int button = 0) {
        ImGui::GetIO().AddMouseButtonEvent(button, down);
    }
    // The mouse rests at `at`, then a press and a release. Separate frames, as with a real mouse.
    void click_at(ImVec2 at) {
        mouse_to(at);
        frame();
        frame();
        mouse(true);
        frame();
        mouse(false);
        frame();
    }

    static const LightPathRowRect& row_of(const std::vector<LightPathRowRect>& rects,
                                          gui::StageId stage) {
        for (const LightPathRowRect& rect : rects) {
            if (rect.stage == stage) {
                return rect;
            }
        }
        ADD_FAILURE() << "no row";
        return rects.front();
    }
    static std::vector<float> heights_of(const std::vector<LightPathRowRect>& rects) {
        std::vector<float> heights;
        for (const LightPathRowRect& rect : rects) {
            heights.push_back(rect.max.y - rect.min.y);
        }
        return heights;
    }

    // The first line's right end, where the switch is, and the slider's middle.
    static ImVec2 switch_of(const LightPathRowRect& row) {
        const gui::LightPathMetrics m = metrics();
        return ImVec2(row.max.x - std::round(mock(8.0f)) - switch_size().x * 0.5f,
                      row.min.y + m.pad_top + m.line * 0.5f);
    }
    static ImVec2 slider_of(const LightPathRowRect& row) {
        const gui::LightPathMetrics m = metrics();
        return ImVec2((row.min.x + row.max.x) * 0.6f,
                      row.min.y + m.pad_top + m.line + m.gap + m.slider * 0.5f);
    }
    static ImVec2 name_of(const LightPathRowRect& row) {
        const gui::LightPathMetrics m = metrics();
        return ImVec2(row.min.x + std::round(mock(35.0f)), row.min.y + m.pad_top + m.line * 0.5f);
    }
    static ImVec2 empty_of(const LightPathRowRect& row) {
        // Right of the name, left of the value: nothing there.
        const gui::LightPathMetrics m = metrics();
        return ImVec2(row.min.x + (row.max.x - row.min.x) * 0.45f,
                      row.min.y + m.pad_top + m.line * 0.5f);
    }
    static ImVec2 dot_of(const LightPathRowRect& row) {
        const gui::LightPathMetrics m = metrics();
        return ImVec2(row.min.x + std::round(mock(8.0f)), row.min.y + m.pad_top + m.line * 0.5f);
    }
    // The same sizes the panel uses (the tests' positions only need the first lines).
    static gui::LightPathMetrics metrics() {
        gui::LightPathMetrics m;
        m.pad_top = std::round(mock(9.0f));
        m.line = std::round(std::max(ImGui::GetFontSize(), switch_size().y));
        m.gap = std::round(mock(4.0f));
        m.slider = slider_height();
        return m;
    }

    ImGuiContext* context_ = nullptr;
    gui::EditSettings settings_;
    gui::InputKind kind_ = gui::InputKind::raw;
    gui::StageId viewed_ = gui::final_stage;
    std::optional<gui::StageId> compare_;
    std::optional<gui::ClipReport> clips_;
    bool strip_on_ = false;
    bool strip_available_ = true;

    struct SliderCall {
        gui::SimpleControl control;
        float position;
        gui::SliderFrame frame;
    };
    std::vector<gui::StageId> viewed_calls_;
    std::vector<std::pair<gui::StageId, bool>> switches_;
    std::vector<SliderCall> slider_frames_;
    std::vector<gui::SimpleControl> resets_;
    std::vector<gui::CurveKind> curves_;
    int parameter_frames_ = 0;
    int parameter_resets_ = 0;
    int strip_toggles_ = 0;
    std::vector<std::optional<gui::StageId>> compares_;
    int swaps_ = 0;
    std::set<std::string> ids_;
    struct Item {
        float top;
        float bottom;
        bool operator==(const Item&) const = default;
    };
    std::vector<Item> items_;
    std::vector<float> alphas_;
    std::vector<float> describe_alphas_;
    float bottom_ = 0;
};

// --- Heights -----------------------------------------------------------------------------------

TEST_F(LightPathHeadless, RowsAreAsTallAsTheLayoutSaysAndFollowOneAnother) {
    const std::vector<LightPathRowRect> rects = frame();
    ASSERT_EQ(rects.size(), gui::pipeline_stages().size());
    for (std::size_t i = 1; i < rects.size(); ++i) {
        EXPECT_EQ(rects[i].min.y, rects[i - 1].max.y);
        EXPECT_GT(rects[i].max.y, rects[i].min.y);
    }
    EXPECT_GE(bottom_, rects.back().max.y);
}

TEST_F(LightPathHeadless, EveryItemStaysInsideItsRow) {
    settings_.curve = gui::CurveKind::reinhard;
    clips_ = report_with(gui::ClipFate::lost);
    const std::vector<LightPathRowRect> rects = frame();
    ASSERT_FALSE(items_.empty());
    const float first = rects.front().min.y;
    const float last = rects.back().max.y;
    for (const Item& item : items_) {
        if (item.bottom <= first) {
            continue;  // the header's action
        }
        EXPECT_GE(item.top, first);
        EXPECT_LE(item.bottom, last);
        bool inside_a_row = false;
        for (const LightPathRowRect& rect : rects) {
            inside_a_row = inside_a_row || (item.top >= rect.min.y && item.bottom <= rect.max.y);
        }
        EXPECT_TRUE(inside_a_row) << item.top << " to " << item.bottom;
    }
}

TEST_F(LightPathHeadless, ClipDataArrivingOrChangingMovesNothing) {
    frame();
    const std::vector<LightPathRowRect> without = frame();
    const std::vector<float> heights = heights_of(without);
    // The controls only, without the clip marks' own hover item: no mouse here, and they report
    // nothing through explain, so the lists can be compared as they are.
    const std::vector<Item> items = items_;
    const float bottom = bottom_;
    for (const gui::ClipFate fate :
         {gui::ClipFate::recoverable, gui::ClipFate::lost, gui::ClipFate::none}) {
        clips_ = report_with(fate);
        frame();
        const std::vector<LightPathRowRect> with = frame();
        EXPECT_EQ(heights_of(with), heights);
        EXPECT_EQ(items_, items);
        EXPECT_EQ(bottom_, bottom);
    }
    clips_.reset();
    frame();
    EXPECT_EQ(heights_of(frame()), heights);
    EXPECT_EQ(items_, items);
}

TEST_F(LightPathHeadless, HoveringMovesNothing) {
    mouse_to(ImVec2(-100, -100));
    frame();
    const std::vector<LightPathRowRect> still = frame();
    const std::vector<float> heights = heights_of(still);
    const std::vector<Item> items = items_;
    for (const LightPathRowRect& row : still) {
        for (const ImVec2 at : {empty_of(row), dot_of(row), switch_of(row), slider_of(row)}) {
            mouse_to(at);
            frame();
            EXPECT_EQ(heights_of(frame()), heights);
            EXPECT_EQ(items_, items);
        }
    }
    // Past the tooltip's delay too.
    mouse_to(empty_of(still[1]));
    for (int i = 0; i < 90; ++i) {
        frame();
    }
    EXPECT_EQ(heights_of(frame()), heights);
    EXPECT_EQ(items_, items);
}

TEST_F(LightPathHeadless, ViewingDimmingAndSwitchingStagesOffMoveNothing) {
    const std::vector<float> heights = heights_of(frame());
    for (const gui::StageInfo& stage : gui::pipeline_stages()) {
        viewed_ = stage.id;
        EXPECT_EQ(heights_of(frame()), heights) << stage.slug;
    }
    viewed_ = gui::final_stage;
    settings_.exposure_on = settings_.saturation_on = settings_.contrast_on = false;
    settings_.curve_on = settings_.matrix_on = false;
    EXPECT_EQ(heights_of(frame()), heights);
}

// --- Presses -----------------------------------------------------------------------------------

TEST_F(LightPathHeadless, APressOnARowsEmptyAreaViewsThatStage) {
    const std::vector<LightPathRowRect> rects = frame();
    for (const LightPathRowRect& row : rects) {
        viewed_calls_.clear();
        click_at(empty_of(row));
        ASSERT_EQ(viewed_calls_.size(), 1u) << static_cast<int>(row.stage);
        EXPECT_EQ(viewed_calls_[0], row.stage);
    }
}

TEST_F(LightPathHeadless, ThePressActsAtOnceAndIsNotAControlBeingUsed) {
    const std::vector<LightPathRowRect> rects = frame();
    mouse_to(empty_of(row_of(rects, gui::StageId::saturation)));
    frame();
    frame();
    mouse(true);
    frame();
    ASSERT_EQ(viewed_calls_.size(), 1u);
    EXPECT_FALSE(control_is_active(false));
    frame();
    EXPECT_FALSE(control_is_active(false));
    mouse(false);
    frame();
    EXPECT_EQ(viewed_calls_.size(), 1u);
}

TEST_F(LightPathHeadless, APressOnTheDotOrTheNameViewsThatStage) {
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::contrast);
    click_at(dot_of(row));
    ASSERT_EQ(viewed_calls_.size(), 1u);
    EXPECT_EQ(viewed_calls_[0], gui::StageId::contrast);
    click_at(name_of(row));
    ASSERT_EQ(viewed_calls_.size(), 2u);
    EXPECT_EQ(viewed_calls_[1], gui::StageId::contrast);
}

TEST_F(LightPathHeadless, APressOnASwitchTogglesItAndDoesNotViewTheStage) {
    const std::vector<LightPathRowRect> rects = frame();
    click_at(switch_of(row_of(rects, gui::StageId::contrast)));
    EXPECT_TRUE(viewed_calls_.empty());
    ASSERT_EQ(switches_.size(), 1u);
    EXPECT_EQ(switches_[0].first, gui::StageId::contrast);
    EXPECT_FALSE(switches_[0].second);
}

TEST_F(LightPathHeadless, APressOnASliderMovesItAndDoesNotViewTheStage) {
    const std::vector<LightPathRowRect> rects = frame();
    mouse_to(slider_of(row_of(rects, gui::StageId::exposure)));
    frame();
    frame();
    mouse(true);
    frame();
    EXPECT_TRUE(control_is_active(false));
    frame();
    mouse(false);
    frame();
    frame();
    EXPECT_TRUE(viewed_calls_.empty());
    ASSERT_FALSE(slider_frames_.empty());
    EXPECT_EQ(slider_frames_.front().control, gui::SimpleControl::brightness);
    EXPECT_TRUE(slider_frames_.front().frame.activated);
    EXPECT_TRUE(slider_frames_.back().frame.deactivated);
    EXPECT_GT(slider_frames_.front().position, 0.0f);
}

TEST_F(LightPathHeadless, ADragThatLeavesTheRowStaysTheSlidersAndIsOneGesture) {
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::saturation);
    mouse_to(slider_of(row));
    frame();
    frame();
    mouse(true);
    frame();
    // Down over the next rows' empty areas, and back.
    mouse_to(empty_of(row_of(rects, gui::StageId::contrast)));
    frame();
    mouse_to(empty_of(row_of(rects, gui::StageId::curve)));
    frame();
    mouse(false);
    frame();
    frame();
    EXPECT_TRUE(viewed_calls_.empty());
    int activated = 0;
    int deactivated = 0;
    for (const SliderCall& call : slider_frames_) {
        EXPECT_EQ(call.control, gui::SimpleControl::colour);
        activated += call.frame.activated;
        deactivated += call.frame.deactivated;
    }
    EXPECT_EQ(activated, 1);
    EXPECT_EQ(deactivated, 1);
}

TEST_F(LightPathHeadless, APressOnASliderThatIsOffDoesNothing) {
    settings_.saturation_on = false;
    const std::vector<LightPathRowRect> rects = frame();
    click_at(slider_of(row_of(rects, gui::StageId::saturation)));
    EXPECT_TRUE(viewed_calls_.empty());
    EXPECT_TRUE(slider_frames_.empty());
}

TEST_F(LightPathHeadless, ARightClickOnASliderOffersItsReset) {
    const std::vector<LightPathRowRect> rects = frame();
    const ImVec2 at = slider_of(row_of(rects, gui::StageId::contrast));
    mouse_to(at);
    frame();
    frame();
    mouse(true, 1);
    frame();
    mouse(false, 1);
    frame();
    frame();
    EXPECT_TRUE(viewed_calls_.empty());
    // The popup opened by the mouse: its only item is right under the cursor, a little down.
    mouse_to(ImVec2(at.x + 12, at.y + 12));
    frame();
    frame();
    mouse(true);
    frame();
    mouse(false);
    frame();
    ASSERT_EQ(resets_.size(), 1u);
    EXPECT_EQ(resets_[0], gui::SimpleControl::contrast);
    EXPECT_TRUE(viewed_calls_.empty());
    EXPECT_TRUE(slider_frames_.empty());
}

// --- Typing a value into a slider ----------------------------------------------------------------

// Ctrl-click on the row's slider, with the mouse resting there first: the field opens.
#define PRISM_TYPED_OPEN(stage)                                                    \
    const std::vector<LightPathRowRect> rects = frame();                           \
    const ImVec2 slider_at = slider_of(row_of(rects, gui::StageId::stage));        \
    ImGui::GetIO().ConfigMacOSXBehaviors = false;                                  \
    mouse_to(slider_at);                                                           \
    frame();                                                                       \
    frame();                                                                       \
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);                               \
    frame();                                                                       \
    mouse(true);                                                                   \
    frame();                                                                       \
    mouse(false);                                                                  \
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);                              \
    frame();                                                                       \
    frame();                                                                       \
    frame()

TEST_F(LightPathHeadless, TypedValueOpensAFieldOnTheValueLinesUnit) {
    PRISM_TYPED_OPEN(exposure);
    EXPECT_TRUE(ImGui::GetIO().WantTextInput);
    EXPECT_TRUE(slider_frames_.empty());  // opening changes nothing
    EXPECT_TRUE(viewed_calls_.empty());
    // Select-all on open: what is typed replaces "+0.50 stops".
    ImGui::GetIO().AddInputCharactersUTF8("1.5");
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    frame();
    // One call with the whole gesture in it (one undo step), at 1.5 stops = position 75.
    ASSERT_EQ(slider_frames_.size(), 1u);
    EXPECT_EQ(slider_frames_[0].control, gui::SimpleControl::brightness);
    EXPECT_FLOAT_EQ(slider_frames_[0].position, 75.0f);
    EXPECT_TRUE(slider_frames_[0].frame.activated);
    EXPECT_TRUE(slider_frames_[0].frame.changed);
    EXPECT_TRUE(slider_frames_[0].frame.deactivated);
    EXPECT_FALSE(ImGui::GetIO().WantTextInput);  // the field closed
}

TEST_F(LightPathHeadless, TypedFactorOnSaturationIsAFactorNotAPosition) {
    PRISM_TYPED_OPEN(saturation);
    ImGui::GetIO().AddInputCharactersUTF8("1,4 x");
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    ASSERT_EQ(slider_frames_.size(), 1u);
    EXPECT_EQ(slider_frames_[0].control, gui::SimpleControl::colour);
    EXPECT_FLOAT_EQ(slider_frames_[0].position, 40.0f);  // ×1.4 is +40 on the slider
}

TEST_F(LightPathHeadless, TypedTextTheRowRejectsLeavesTheValue) {
    PRISM_TYPED_OPEN(contrast);
    ImGui::GetIO().AddInputCharactersUTF8("lots");
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    frame();
    EXPECT_TRUE(slider_frames_.empty());
    EXPECT_FALSE(ImGui::GetIO().WantTextInput);
}

TEST_F(LightPathHeadless, TypedClickingAwayAfterAnEditKeepsIt) {
    PRISM_TYPED_OPEN(exposure);
    ImGui::GetIO().AddInputCharactersUTF8("-1");
    frame();
    mouse_to(empty_of(row_of(rects, gui::StageId::contrast)));
    frame();
    mouse(true);
    frame();
    mouse(false);
    frame();
    frame();
    ASSERT_EQ(slider_frames_.size(), 1u);
    EXPECT_FLOAT_EQ(slider_frames_[0].position, -50.0f);
}

TEST_F(LightPathHeadless, TypedEscapeLeavesTheValue) {
    {
        PRISM_TYPED_OPEN(exposure);
        ImGui::GetIO().AddInputCharactersUTF8("2");
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        frame();
        EXPECT_TRUE(slider_frames_.empty());
        EXPECT_FALSE(ImGui::GetIO().WantTextInput);
    }
}

TEST_F(LightPathHeadless, TypedFieldKeepsEveryRowWhereItWas) {
    const std::vector<float> before = heights_of(frame());
    PRISM_TYPED_OPEN(exposure);
    EXPECT_TRUE(ImGui::GetIO().WantTextInput);
    EXPECT_EQ(heights_of(frame()), before);
    EXPECT_EQ(heights_of(rects), before);
}

TEST_F(LightPathHeadless, TypedPlainClickStillDragsTheSlider) {
    const std::vector<LightPathRowRect> rects = frame();
    click_at(slider_of(row_of(rects, gui::StageId::exposure)));
    ASSERT_FALSE(slider_frames_.empty());
    EXPECT_FALSE(ImGui::GetIO().WantTextInput);
    EXPECT_TRUE(slider_frames_.front().frame.activated);
    EXPECT_TRUE(slider_frames_.back().frame.deactivated);
}

TEST_F(LightPathHeadless, TypedValueNeverOpensOnAStageThatIsOff) {
    settings_.contrast_on = false;
    PRISM_TYPED_OPEN(contrast);
    EXPECT_FALSE(ImGui::GetIO().WantTextInput);
    EXPECT_TRUE(slider_frames_.empty());
}

TEST_F(LightPathHeadless, TheCurveTabsChooseAndDoNotViewTheStage) {
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::curve);
    const gui::LightPathMetrics m = metrics();
    const float content_x = row.min.x + std::round(mock(27.0f));
    const float content_w = row.max.x - std::round(mock(8.0f)) - content_x;
    const float y = row.min.y + m.pad_top + m.line + m.gap + segmented_height() * 0.5f;
    // Four equal tabs: the third is Reinhard.
    click_at(ImVec2(content_x + content_w * 0.625f, y));
    EXPECT_TRUE(viewed_calls_.empty());
    ASSERT_EQ(curves_.size(), 1u);
    EXPECT_EQ(curves_[0], gui::CurveKind::reinhard);
}

TEST_F(LightPathHeadless, TheTestStripButtonTogglesAndDoesNotViewTheStage) {
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::exposure);
    // At the right of the clip line, the row's last.
    const ImVec2 at(row.max.x - std::round(mock(8.0f)) - 10.0f,
                    row.max.y - std::round(mock(10.0f)) - std::round(mock(24.0f)) * 0.5f);
    click_at(at);
    EXPECT_EQ(strip_toggles_, 1);
    EXPECT_TRUE(viewed_calls_.empty());
    // Greyed out: no toggle, and still not a press on the row.
    strip_available_ = false;
    click_at(at);
    EXPECT_EQ(strip_toggles_, 1);
    EXPECT_TRUE(viewed_calls_.empty());
}

TEST_F(LightPathHeadless, APressOnClipMarksIsAPressOnTheRow) {
    clips_ = report_with(gui::ClipFate::lost);
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::encode);
    click_at(ImVec2(row.min.x + std::round(mock(27.0f)) + 6.0f,
                    row.max.y - std::round(mock(10.0f)) - 4.0f));
    ASSERT_EQ(viewed_calls_.size(), 1u);
    EXPECT_EQ(viewed_calls_[0], gui::StageId::encode);
    EXPECT_TRUE(ids_.count(std::string(gui::explain::clip_dot)));
}

// --- The Compare popup -------------------------------------------------------------------------

TEST_F(LightPathHeadless, TheCompareActionOpensAPopupThatChooses) {
    viewed_ = gui::StageId::contrast;
    // The action is at the header's right end: find it from the first row, above it.
    std::vector<LightPathRowRect> rects = frame();
    const ImVec2 action(rects.front().max.x - 12.0f, rects.front().min.y - 12.0f);
    click_at(action);
    EXPECT_TRUE(viewed_calls_.empty());
    ImGuiWindow* popup = nullptr;
    frame();
    popup = ImGui::GetCurrentContext()->OpenPopupStack.empty()
                ? nullptr
                : ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
    ASSERT_NE(popup, nullptr);
    // Row height is the same for all its items: Off, Original, six stages (after Decode), Swap.
    const float top = popup->DC.CursorStartPos.y;
    const float item = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
    click_at(ImVec2(popup->Pos.x + popup->Size.x * 0.5f, top + item * 1.5f));
    ASSERT_EQ(compares_.size(), 1u);
    ASSERT_TRUE(compares_[0].has_value());
    EXPECT_EQ(*compares_[0], gui::StageId::decode);
    EXPECT_TRUE(viewed_calls_.empty());
}

TEST_F(LightPathHeadless, SwapSidesIsOfferedOnlyWhileComparing) {
    viewed_ = gui::StageId::contrast;
    std::vector<LightPathRowRect> rects = frame();
    const ImVec2 action(rects.front().max.x - 12.0f, rects.front().min.y - 12.0f);
    const auto press_swap = [&] {
        click_at(action);
        frame();
        ImGuiWindow* popup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
        click_at(ImVec2(popup->Pos.x + popup->Size.x * 0.5f,
                        popup->Pos.y + popup->Size.y - ImGui::GetStyle().WindowPadding.y - 4.0f));
        // Whatever happened, close it for the next round.
        mouse_to(ImVec2(590, 1400));
        frame();
        mouse(true);
        frame();
        mouse(false);
        frame();
        frame();
    };
    press_swap();
    EXPECT_EQ(swaps_, 0);
    compare_ = gui::StageId::exposure;
    frame();
    press_swap();
    EXPECT_EQ(swaps_, 1);
}

// --- Explanations ------------------------------------------------------------------------------

TEST_F(LightPathHeadless, EveryControlAskedForHasAnEntry) {
    settings_.curve = gui::CurveKind::gamma;
    clips_ = report_with(gui::ClipFate::lost);
    std::vector<LightPathRowRect> rects = frame();
    for (const LightPathRowRect& row : rects) {
        mouse_to(empty_of(row));
        frame();
        frame();
    }
    settings_.curve = gui::CurveKind::reinhard;
    frame();
    ASSERT_FALSE(ids_.empty());
    for (const std::string& id : ids_) {
        EXPECT_NE(gui::explanation_for(id), nullptr) << id;
    }
    for (const gui::StageInfo& stage : gui::pipeline_stages()) {
        EXPECT_TRUE(ids_.count(std::string(gui::explain_id(stage.id)))) << stage.slug;
    }
    for (const gui::CurveKind kind : gui::curve_kinds) {
        EXPECT_TRUE(ids_.count(std::string(gui::explain_id(kind))));
    }
    EXPECT_TRUE(ids_.count(std::string(gui::explain::curve_gamma_value)));
    EXPECT_TRUE(ids_.count(std::string(gui::explain::curve_white)));
    EXPECT_TRUE(ids_.count(std::string(gui::explain::compare)));
    EXPECT_TRUE(ids_.count(std::string(gui::explain::strip)));
}

// --- Dimming -----------------------------------------------------------------------------------

TEST_F(LightPathHeadless, ADimmedRowIsDimmedButWhatItOpensIsNot) {
    frame();
    for (const float alpha : alphas_) {
        EXPECT_EQ(alpha, ImGui::GetStyle().Alpha);
    }
    alphas_.clear();
    describe_alphas_.clear();
    viewed_ = gui::StageId::decode;
    frame();
    bool dimmed = false;
    for (const float alpha : alphas_) {
        dimmed = dimmed || alpha < ImGui::GetStyle().Alpha;  // the sliders and the rows' buttons
    }
    EXPECT_TRUE(dimmed);
    ASSERT_FALSE(describe_alphas_.empty());
    for (const float alpha : describe_alphas_) {  // what can open a tooltip
        EXPECT_EQ(alpha, 1.0f);
    }
}

TEST_F(LightPathHeadless, ADimmedRowStillWorks) {
    viewed_ = gui::StageId::exposure;
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::saturation);
    click_at(switch_of(row));
    ASSERT_EQ(switches_.size(), 1u);
    EXPECT_EQ(switches_[0].first, gui::StageId::saturation);
    click_at(empty_of(row));
    ASSERT_EQ(viewed_calls_.size(), 1u);
    EXPECT_EQ(viewed_calls_[0], gui::StageId::saturation);
}

}  // namespace

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
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
#include "prism/gui/framing.hpp"
#include "prism/gui/layout.hpp"
#include "prism/gui/light_path.hpp"
#include "prism/gui/paper_grade.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/white_balance.hpp"
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
        cb.contrast_slider = [this](std::optional<float> contrast, const gui::SliderFrame& f) {
            contrast_frames_.push_back({contrast, f});
        };
        cb.reset_slider = [this](gui::SimpleControl c) { resets_.push_back(c); };
        // As the app does: the value goes through gui_core's setter, so a drag carries on.
        cb.white_balance_slider = [this](gui::WhiteBalanceControl c, std::optional<double> value,
                                         const gui::SliderFrame& f) {
            white_balance_frames_.push_back({c, value, f});
            if (value) {
                gui::set_white_balance(settings_, c, *value, as_shot_);
            }
        };
        cb.reset_white_balance = [this] { ++white_balance_resets_; };
        cb.set_curve_kind = [this](gui::CurveKind k) { curves_.push_back(k); };
        cb.curve_parameter = [this](float, const gui::SliderFrame&) { ++parameter_frames_; };
        cb.reset_curve_parameter = [this] { ++parameter_resets_; };
        cb.toggle_strip = [this] { ++strip_toggles_; };
        cb.toggle_pick = [this] { ++pick_toggles_; };
        cb.toggle_brush = [this] { ++brush_toggles_; };
        cb.brush_strength = [this](float v) { strength_calls_.push_back(v); };
        cb.clear_strokes = [this] { ++clears_; };
        cb.toggle_framing = [this] { ++framing_toggles_; };
        cb.set_compare = [this](std::optional<gui::StageId> s) { compares_.push_back(s); };
        cb.swap_compare = [this] { ++swaps_; };
        const auto note = [this](std::string_view id) {
            ids_.insert(std::string(id));
            alphas_.push_back(ImGui::GetStyle().Alpha);
            // Every control's place this frame, in order: a row's parts must not move.
            items_.push_back({ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().y});
            placed_.push_back({std::string(id), ImGui::GetItemRectMin(), ImGui::GetItemRectMax()});
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
        placed_.clear();
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width_, height_));
        // The app's panel is a child with the panel's padding, and scrolls (so it has a scrollbar).
        if (app_padding_) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                ImVec2(token_metrics().panel_pad.x, token_metrics().panel_pad.y));
        }
        ImGui::Begin("panel", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoTitleBar);
        if (app_padding_) {
            ImGui::PopStyleVar();
        }
        LightPathData data;
        data.fits = fits_;
        data.view = gui::light_path_view({settings_, kind_, file_kind_, viewed_, compare_,
                                          clips_ ? &*clips_ : nullptr, as_shot_,
                                          brush_strength_});
        data.theme = gui::Theme::dark;
        data.strip_on = strip_on_;
        data.strip_available = strip_available_;
        data.strip_hint = "The hint.";
        data.pick_on = pick_on_;
        data.pick_available = pick_available_;
        data.pick_hint = "The pick hint.";
        data.brush_on = brush_on_;
        data.brush_available = brush_available_;
        data.brush_hint = "The brush hint.";
        data.easel_value = easel_value_;
        data.framing = framing_;
        data.easel_available = easel_available_;
        data.easel_rect = &easel_;
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

    // The sizes the app's row_metrics gives, all of them: for the height a row is laid out at.
    static gui::LightPathMetrics full_metrics() {
        gui::LightPathMetrics m = metrics();
        m.pad_bottom = std::round(mock(10.0f));
        ImGui::PushFont(nullptr, token_type().small);
        m.secondary = std::ceil(ImGui::GetFontSize());
        ImGui::PopFont();
        m.tabs = segmented_height();
        return m;
    }

    // Where an item that explained itself was drawn in the last frame.
    struct Placed {
        std::string id;
        ImVec2 min;
        ImVec2 max;
        [[nodiscard]] ImVec2 centre() const {
            return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        }
    };
    // White balance's sliders as the last frame drew them: Kelvin, then Tint. The row explains
    // itself four times, in the order it submits its items: the switch, the two sliders, the row.
    [[nodiscard]] std::vector<Placed> white_balance_sliders() const {
        std::vector<Placed> all;
        for (const Placed& p : placed_) {
            if (p.id == gui::explain_id(gui::StageId::white_balance)) {
                all.push_back(p);
            }
        }
        if (all.size() != 4) {
            ADD_FAILURE() << "the White balance row drew " << all.size() << " items";
            return {Placed{}, Placed{}};
        }
        return {all[1], all[2]};
    }
    // The items of Dodge & burn's controls line as the last frame drew them, by their explanation.
    [[nodiscard]] Placed placed_with(std::string_view id) const {
        for (const Placed& p : placed_) {
            if (p.id == id) {
                return p;
            }
        }
        ADD_FAILURE() << "nothing drew " << id;
        return Placed{};
    }
    // The Pick neutral buttons as the last frame drew them.
    [[nodiscard]] std::vector<Placed> pick_buttons() const {
        std::vector<Placed> all;
        for (const Placed& p : placed_) {
            if (p.id == gui::explain::pick) {
                all.push_back(p);
            }
        }
        return all;
    }

    // The mouse rests at `from`, presses, moves to `to`, and lets go: one drag, a frame at a time.
    void drag(ImVec2 from, ImVec2 to) {
        mouse_to(from);
        frame();
        frame();
        mouse(true);
        frame();
        mouse_to(ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f));
        frame();
        mouse_to(to);
        frame();
        frame();
        mouse(false);
        frame();
        frame();
    }
    // Ctrl-click at `at`, with the mouse resting there first: a slider there opens its field.
    void ctrl_click(ImVec2 at) {
        ImGui::GetIO().ConfigMacOSXBehaviors = false;
        mouse_to(at);
        frame();
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        mouse(true);
        frame();
        mouse(false);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        frame();
        frame();
    }
    void type_and_enter(const char* text) {
        ImGui::GetIO().AddInputCharactersUTF8(text);
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        frame();
    }

    ImGuiContext* context_ = nullptr;
    gui::EditSettings settings_;
    std::optional<prism::Chromaticity> as_shot_;
    gui::InputKind kind_ = gui::InputKind::raw;
    gui::StageId viewed_ = gui::final_stage;
    std::optional<gui::StageId> compare_;
    std::optional<gui::ClipReport> clips_;
    std::string_view file_kind_ = "RAW";
    float width_ = panel_width;
    float height_ = scroll_room;
    bool app_padding_ = false;
    std::vector<TextFit>* fits_ = nullptr;
    bool strip_on_ = false;
    bool strip_available_ = true;
    bool pick_on_ = false;
    bool pick_available_ = true;
    int pick_toggles_ = 0;
    bool brush_on_ = false;
    float brush_strength_ = 0.5f;
    bool brush_available_ = true;
    int brush_toggles_ = 0;
    std::vector<float> strength_calls_;
    int clears_ = 0;
    std::string easel_value_ = "Whole photo";
    bool framing_ = false;
    bool easel_available_ = true;
    int framing_toggles_ = 0;
    LightPathData::Rect easel_{};

    struct SliderCall {
        gui::SimpleControl control;
        float position;
        gui::SliderFrame frame;
    };
    std::vector<gui::StageId> viewed_calls_;
    std::vector<std::pair<gui::StageId, bool>> switches_;
    std::vector<SliderCall> slider_frames_;
    struct ContrastCall {
        std::optional<float> contrast;
        gui::SliderFrame frame;
    };
    std::vector<ContrastCall> contrast_frames_;
    std::vector<gui::SimpleControl> resets_;
    struct WhiteBalanceCall {
        gui::WhiteBalanceControl control;
        std::optional<double> value;
        gui::SliderFrame frame;
    };
    std::vector<WhiteBalanceCall> white_balance_frames_;
    int white_balance_resets_ = 0;
    std::vector<Placed> placed_;
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
    ASSERT_EQ(rects.size(), gui::pipeline_stages().size());  // every stage has a row
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

// The Contrast slider is in paper grades: a drag lands on half grades, as one gesture, and a value
// between two of them is left alone until the slider moves.
TEST_F(LightPathHeadless, DraggingContrastLandsOnHalfGradesAsOneGesture) {
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::contrast);
    const ImVec2 start = slider_of(row);
    mouse_to(start);
    frame();
    frame();
    mouse(true);
    frame();
    for (const float fraction : {0.7f, 0.83f, 0.9f}) {
        mouse_to(ImVec2(row.min.x + (row.max.x - row.min.x) * fraction, start.y));
        frame();
    }
    mouse(false);
    frame();
    frame();
    EXPECT_TRUE(viewed_calls_.empty());
    EXPECT_TRUE(slider_frames_.empty());  // not through the other sliders' callback
    ASSERT_FALSE(contrast_frames_.empty());
    int activated = 0;
    int deactivated = 0;
    std::optional<float> last;
    for (const ContrastCall& call : contrast_frames_) {
        activated += call.frame.activated;
        deactivated += call.frame.deactivated;
        if (call.contrast) {
            last = call.contrast;
            const float position = gui::grade_slider_position(*call.contrast);
            EXPECT_EQ(*call.contrast, gui::contrast_for_grade(position / 2.0f));
        }
    }
    EXPECT_EQ(activated, 1);
    EXPECT_EQ(deactivated, 1);
    ASSERT_TRUE(last.has_value());
    EXPECT_GT(*last, 1.0f);  // dragged right of grade 2
}

TEST_F(LightPathHeadless, DrawingAnOffGridContrastChangesNothing) {
    settings_.contrast = gui::contrast_for_grade(2.7f);
    for (int i = 0; i < 4; ++i) {
        frame();
    }
    EXPECT_TRUE(contrast_frames_.empty());
    EXPECT_EQ(settings_.contrast, gui::contrast_for_grade(2.7f));
    // Pressing the knob where it is drawn (nearest half grade, 2.5) is not a move either.
    const std::vector<LightPathRowRect> rects = frame();
    click_at(slider_of(row_of(rects, gui::StageId::contrast)));
    for (const ContrastCall& call : contrast_frames_) {
        EXPECT_FALSE(call.frame.changed && call.contrast &&
                     *call.contrast == gui::contrast_for_grade(2.7f));
    }
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
    ASSERT_TRUE(ImGui::GetIO().WantTextInput);  // the field is open
    ASSERT_TRUE(contrast_frames_.empty());
    ImGui::GetIO().AddInputCharactersUTF8("lots");
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    frame();
    // Contrast reports through its own callback, not the other rows' slider.
    EXPECT_TRUE(contrast_frames_.empty());
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
        ASSERT_TRUE(ImGui::GetIO().WantTextInput);  // the field is open
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

TEST_F(LightPathHeadless, APressOnClipMarksActsAtOnceAndIsNotAControlBeingUsed) {
    clips_ = report_with(gui::ClipFate::lost);
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::encode);
    mouse_to(ImVec2(row.min.x + std::round(mock(27.0f)) + 6.0f,
                    row.max.y - std::round(mock(10.0f)) - 4.0f));
    frame();
    frame();
    mouse(true);
    frame();
    ASSERT_EQ(viewed_calls_.size(), 1u);
    EXPECT_TRUE(ids_.count(std::string(gui::explain::clip_dot)));
    EXPECT_FALSE(control_is_active(false));
    frame();
    EXPECT_FALSE(control_is_active(false));
    mouse(false);
    frame();
    EXPECT_EQ(viewed_calls_.size(), 1u);
}

// --- The Compare popup -------------------------------------------------------------------------

TEST_F(LightPathHeadless, TheCompareActionOpensAPopupThatChooses) {
    viewed_ = gui::StageId::contrast;
    // The action is at the header's right end: find it from the first row, above it.
    std::vector<LightPathRowRect> rects = frame();
    const ImVec2 action(rects.front().max.x - 12.0f, easel_.min.y - 12.0f);
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
    const ImVec2 action(rects.front().max.x - 12.0f, easel_.min.y - 12.0f);
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

TEST_F(LightPathHeadless, DodgeAndBurnHasARowAfterExposureAndAnEntryInTheComparePopup) {
    // With strokes or without, on or off: the row is there, and so is its entry in the popup.
    viewed_ = gui::StageId::contrast;
    for (int with = 0; with < 2; ++with) {
        settings_ = {};
        settings_.exposure_stops = 0.5f;
        if (with) {
            auto field = std::make_shared<prism::StrokeField>();
            field->aspect = 1.5f;
            field->strokes.push_back(std::make_shared<const prism::FieldStroke>(
                prism::FieldStroke{{{0.5f, 0.5f}}, 0.1f, 0.5f, -1.0f}));
            settings_.strokes = gui::Strokes(std::move(field));
        }
        std::vector<LightPathRowRect> rects = frame();
        EXPECT_EQ(rects.size(), 9u);  // Decode, seven edits, Output encode
        ASSERT_GE(rects.size(), 3u);
        EXPECT_EQ(rects[1].stage, gui::StageId::exposure);
        EXPECT_EQ(rects[2].stage, gui::StageId::dodge_burn);
        const ImVec2 action(rects.front().max.x - 12.0f, easel_.min.y - 12.0f);
        click_at(action);
        frame();
        // The header's Compare action, then every entry of the popup (Off and a stage each)
        // describe themselves once.
        std::size_t popup_entries = 0;
        for (const auto& p : placed_) {
            popup_entries += p.id == gui::explain::compare ? 1 : 0;
        }
        EXPECT_EQ(popup_entries, 11u);  // the action, Off and those nine stages
        // Close it for the next round.
        mouse_to(ImVec2(590, 1400));
        frame();
        mouse(true);
        frame();
        mouse(false);
        frame();
        frame();
    }
}

// --- Dodge & burn's row -------------------------------------------------------------------------

namespace {
gui::Strokes many_strokes(std::size_t count) {
    auto field = std::make_shared<prism::StrokeField>();
    field->aspect = 1.5f;
    for (std::size_t i = 0; i < count; ++i) {
        field->strokes.push_back(std::make_shared<const prism::FieldStroke>(
            prism::FieldStroke{{{0.5f, 0.5f}}, 0.05f, 0.5f, i % 2 ? 0.5f : -0.5f}));
    }
    return gui::Strokes(std::move(field));
}
}  // namespace

TEST_F(LightPathHeadless, DodgeAndBurnRowIsTheSameHeightWhateverItHoldsOrShows) {
    const float expected = [&] {
        const std::vector<LightPathRowRect> rects = frame();
        const LightPathRowRect& row = row_of(rects, gui::StageId::dodge_burn);
        return row.max.y - row.min.y;
    }();
    const gui::LightPathRow plain = gui::light_path_row(
        {settings_, kind_, file_kind_, viewed_, compare_, nullptr, as_shot_}, gui::StageId::dodge_burn);
    EXPECT_EQ(expected, gui::light_path_row_layout(plain, full_metrics()).height);
    // A controls line is one slider high, between the first line and the clip line.
    EXPECT_GT(expected, full_metrics().pad_top + full_metrics().line + full_metrics().slider);
    for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{500}}) {
        for (const bool on : {true, false}) {
            for (const gui::StageId viewed : {gui::StageId::dodge_burn, gui::StageId::exposure,
                                              gui::StageId::curve, gui::final_stage}) {
                settings_.strokes = many_strokes(count);
                settings_.dodge_burn_on = on;
                viewed_ = viewed;
                for (const bool pressed : {false, true}) {
                    brush_on_ = pressed;
                    const std::vector<LightPathRowRect> rects = frame();
                    const LightPathRowRect& row = row_of(rects, gui::StageId::dodge_burn);
                    EXPECT_EQ(row.max.y - row.min.y, expected)
                        << count << (on ? " on " : " off ") << static_cast<int>(viewed);
                    // And the rows below it stay where they were.
                    EXPECT_EQ(rects.size(), gui::pipeline_stages().size());
                }
            }
        }
    }
}

TEST_F(LightPathHeadless, TheBrushStrengthAndClearAreInsideTheDodgeAndBurnRowOnOneLine) {
    settings_.strokes = many_strokes(3);
    frame();
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::dodge_burn);
    const Placed brush = placed_with(gui::explain::dodge_burn_brush);
    const Placed strength = placed_with(gui::explain::dodge_burn_strength);
    const Placed clear = placed_with(gui::explain::dodge_burn_clear);
    const gui::LightPathMetrics m = metrics();
    for (const Placed& p : {brush, strength, clear}) {
        EXPECT_GE(p.min.y, row.min.y + m.pad_top + m.line) << p.id;
        EXPECT_LE(p.max.y, row.max.y) << p.id;
        EXPECT_GT(p.min.x, row.min.x) << p.id;
        EXPECT_LT(p.max.x, row.max.x) << p.id;
        EXPECT_FLOAT_EQ(p.max.y - p.min.y, slider_height()) << p.id;
        EXPECT_EQ(p.min.y, brush.min.y) << p.id;  // one line
    }
    // Brush at the left, Strength between, Clear at the right.
    EXPECT_LT(brush.max.x, strength.min.x);
    EXPECT_LT(strength.max.x, clear.min.x);
    EXPECT_GT(strength.max.x - strength.min.x, 70.0f);
    // The switch has its own words.
    EXPECT_TRUE(ids_.count(std::string(gui::explain::dodge_burn_switch)));
}

TEST_F(LightPathHeadless, TheStrengthLabelAndValueHaveTheirRoomBesideTheSlider) {
    std::vector<TextFit> fits;
    fits_ = &fits;
    frame();
    fits.clear();
    frame();
    fits_ = nullptr;
    const Placed brush = placed_with(gui::explain::dodge_burn_brush);
    const Placed strength = placed_with(gui::explain::dodge_burn_strength);
    const Placed clear = placed_with(gui::explain::dodge_burn_clear);
    const auto fit = [&](const char* what) {
        const auto it = std::find_if(fits.begin(), fits.end(),
                                     [&](const TextFit& f) { return f.what == what; });
        EXPECT_NE(it, fits.end()) << what;
        return it == fits.end() ? TextFit{} : *it;
    };
    const TextFit label = fit("Strength label");
    const TextFit value = fit("Strength value");
    EXPECT_EQ(label.text, "Strength");
    EXPECT_GT(label.width, 0.0f);
    // "Strength" between Brush and the slider, the widest value between the slider and Clear,
    // each with a gap on both sides.
    EXPECT_GT(strength.min.x - brush.max.x, label.width);
    EXPECT_GT(clear.min.x - strength.max.x, value.room);
}

TEST_F(LightPathHeadless, PressingBrushTogglesTheBrushAndDoesNotViewTheStage) {
    frame();
    frame();
    click_at(placed_with(gui::explain::dodge_burn_brush).centre());
    EXPECT_EQ(brush_toggles_, 1);
    EXPECT_TRUE(viewed_calls_.empty());
    // Greyed out where it is not available: a press does nothing (nor does it view the stage).
    brush_available_ = false;
    frame();
    frame();
    click_at(placed_with(gui::explain::dodge_burn_brush).centre());
    EXPECT_EQ(brush_toggles_, 1);
    // But a Brush that is on can always be turned off.
    brush_on_ = true;
    frame();
    frame();
    click_at(placed_with(gui::explain::dodge_burn_brush).centre());
    EXPECT_EQ(brush_toggles_, 2);
}

TEST_F(LightPathHeadless, ClearWorksOnlyWithStrokesAndSaysNothingElse) {
    frame();
    frame();
    click_at(placed_with(gui::explain::dodge_burn_clear).centre());
    EXPECT_EQ(clears_, 0);  // nothing to clear: greyed out
    EXPECT_TRUE(viewed_calls_.empty());
    settings_.strokes = many_strokes(2);
    frame();
    frame();
    click_at(placed_with(gui::explain::dodge_burn_clear).centre());
    EXPECT_EQ(clears_, 1);
    EXPECT_TRUE(viewed_calls_.empty());
}

TEST_F(LightPathHeadless, TheStrengthSliderMovesInTenthsOfAStop) {
    frame();
    frame();
    const Placed strength = placed_with(gui::explain::dodge_burn_strength);
    drag(ImVec2(strength.min.x + 4.0f, strength.centre().y),
         ImVec2(strength.max.x - 1.0f, strength.centre().y));
    ASSERT_FALSE(strength_calls_.empty());
    for (const float v : strength_calls_) {
        EXPECT_FLOAT_EQ(v, std::round(v * 10.0f) / 10.0f) << v;
        EXPECT_GE(v, 0.1f);
        EXPECT_LE(v, 2.0f);
    }
    EXPECT_FLOAT_EQ(strength_calls_.back(), 2.0f);
}

TEST_F(LightPathHeadless, ARightClickOnTheStrengthSliderOffersResetToHalfAStop) {
    frame();
    frame();
    const Placed strength = placed_with(gui::explain::dodge_burn_strength);
    const ImVec2 at = strength.centre();
    mouse_to(at);
    frame();
    frame();
    mouse(true, 1);
    frame();
    mouse(false, 1);
    frame();
    frame();
    EXPECT_TRUE(strength_calls_.empty());  // opening the menu changes nothing
    // The popup's only item is right under the cursor, a little down.
    mouse_to(ImVec2(at.x + 12, at.y + 12));
    frame();
    frame();
    mouse(true);
    frame();
    mouse(false);
    frame();
    frame();
    ASSERT_EQ(strength_calls_.size(), 1u);
    EXPECT_FLOAT_EQ(strength_calls_[0], 0.5f);
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
        if (stage.hidden) {
            continue;
        }
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

// --- White balance -----------------------------------------------------------------------------

TEST_F(LightPathHeadless, WhiteBalanceHasTwoSlidersInsideItsRowOneUnderTheOther) {
    frame();
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::white_balance);
    const std::vector<Placed> sliders = white_balance_sliders();
    ASSERT_EQ(sliders.size(), 2u);
    const Placed& kelvin = sliders[0];
    const Placed& tint = sliders[1];
    for (const Placed& slider : sliders) {
        EXPECT_GE(slider.min.y, row.min.y);
        EXPECT_LE(slider.max.y, row.max.y);
        EXPECT_GT(slider.min.x, row.min.x);
        EXPECT_LT(slider.max.x, row.max.x);
        EXPECT_FLOAT_EQ(slider.max.y - slider.min.y, slider_height());
        EXPECT_GT(slider.max.x - slider.min.x, 100.0f);
    }
    // Tint under Kelvin, the same width, starting at the same place after the captions.
    EXPECT_GE(tint.min.y, kelvin.max.y);
    EXPECT_EQ(tint.min.x, kelvin.min.x);
    EXPECT_EQ(tint.max.x, kelvin.max.x);
    // Under the first line and the subtitle, and above the row's clip line.
    const gui::LightPathMetrics m = metrics();
    EXPECT_GT(kelvin.min.y, row.min.y + m.pad_top + m.line + m.gap);
    EXPECT_LT(tint.max.y, row.max.y - m.gap);
    // The row is the next after Dodge & burn (itself after Exposure) and the one before Colour matrix.
    EXPECT_EQ(row.min.y, row_of(rects, gui::StageId::dodge_burn).max.y);
    EXPECT_EQ(row_of(rects, gui::StageId::dodge_burn).min.y, row_of(rects, gui::StageId::exposure).max.y);
    EXPECT_EQ(row.max.y, row_of(rects, gui::StageId::matrix).min.y);
}

TEST_F(LightPathHeadless, PickNeutralIsOneButtonInsideTheWhiteBalanceRowAboveItsSliders) {
    frame();
    const std::vector<LightPathRowRect> rects = frame();
    const LightPathRowRect& row = row_of(rects, gui::StageId::white_balance);
    const std::vector<Placed> buttons = pick_buttons();
    ASSERT_EQ(buttons.size(), 1u);  // only that row has one
    const Placed& button = buttons[0];
    EXPECT_GE(button.min.y, row.min.y);
    EXPECT_LE(button.max.y, row.max.y);
    EXPECT_GT(button.min.x, row.min.x);
    EXPECT_LE(button.max.x, row.max.x - std::round(mock(8.0f)));
    // On the subtitle line: under the first line, and clear of the first slider.
    const gui::LightPathMetrics m = metrics();
    EXPECT_GE(button.min.y, row.min.y + m.pad_top + m.line);
    EXPECT_LE(button.max.y, white_balance_sliders()[0].min.y);
}

TEST_F(LightPathHeadless, PickNeutralDoesNotChangeTheRowsHeight) {
    frame();
    const std::vector<LightPathRowRect> rects = frame();
    const std::vector<float> heights = heights_of(rects);
    // The height the layout gives a White balance row that has no button.
    gui::LightPathRow without = gui::light_path_row({settings_, kind_, "RAW", viewed_, compare_,
                                                    nullptr, as_shot_},
                                                   gui::StageId::white_balance);
    ASSERT_TRUE(without.pick_neutral);
    const float with_button = gui::light_path_row_layout(without, full_metrics()).height;
    without.pick_neutral = false;
    EXPECT_EQ(gui::light_path_row_layout(without, full_metrics()).height, with_button);
    const LightPathRowRect& row = row_of(rects, gui::StageId::white_balance);
    EXPECT_EQ(row.max.y - row.min.y, with_button);
    // And pressed in, or greyed out, it is the same row, in the same place, with the same parts.
    const std::vector<Item> items = items_;
    const float bottom = bottom_;
    for (const auto [on, available] : {std::pair{true, true}, {false, false}, {true, false}}) {
        pick_on_ = on;
        pick_available_ = available;
        frame();
        EXPECT_EQ(heights_of(frame()), heights) << on << available;
        EXPECT_EQ(items_, items) << on << available;
        EXPECT_EQ(bottom_, bottom) << on << available;
    }
}

TEST_F(LightPathHeadless, PressingPickNeutralTogglesPickModeAndDoesNotViewTheStage) {
    frame();
    frame();
    const Placed button = pick_buttons().at(0);
    click_at(button.centre());
    EXPECT_EQ(pick_toggles_, 1);  // on the press
    EXPECT_TRUE(viewed_calls_.empty());
    EXPECT_TRUE(white_balance_frames_.empty());
    // Pressed in, it still toggles (that leaves the mode).
    pick_on_ = true;
    click_at(button.centre());
    EXPECT_EQ(pick_toggles_, 2);
    // Greyed out (viewing Decode): no toggle, and still not a press on the row.
    pick_on_ = false;
    pick_available_ = false;
    click_at(button.centre());
    EXPECT_EQ(pick_toggles_, 2);
    EXPECT_TRUE(viewed_calls_.empty());
}

TEST_F(LightPathHeadless, PickNeutralExplainsItself) {
    frame();
    frame();
    EXPECT_TRUE(ids_.count(std::string(gui::explain::pick)));
}

TEST_F(LightPathHeadless, WhiteBalanceRowIsAsTallAsShotChosenAndOff) {
    frame();
    const std::vector<float> heights = heights_of(frame());
    const std::vector<Item> items = items_;
    const float bottom = bottom_;
    const auto same = [&](const char* what) {
        frame();
        EXPECT_EQ(heights_of(frame()), heights) << what;
        EXPECT_EQ(items_, items) << what;
        EXPECT_EQ(bottom_, bottom) << what;
    };
    settings_.white_balance = prism::WhiteBalance{2000, -100};
    same("chosen");
    settings_.white_balance_on = false;
    same("chosen, off");
    settings_.white_balance.reset();
    same("as shot, off");
    settings_.white_balance_on = true;
    as_shot_ = prism::white_of(prism::WhiteBalance{5328, 26.7});
    same("as shot, with the camera's light");
    settings_.white_balance = prism::WhiteBalance{15000, 100};
    same("chosen, with the camera's light");
    kind_ = gui::InputKind::image;
    as_shot_.reset();
    same("a JPEG");
}

TEST_F(LightPathHeadless, DraggingKelvinChangesOnlyKelvinAsOneGesture) {
    as_shot_ = prism::white_of(prism::WhiteBalance{5328, 26.7});
    frame();
    frame();
    const Placed kelvin = white_balance_sliders()[0];
    const float width = kelvin.max.x - kelvin.min.x;
    // From the slider's middle (the mired midpoint, about 3530 K) a fifth of the way right.
    drag(kelvin.centre(), ImVec2(kelvin.centre().x + width * 0.2f, kelvin.centre().y + 40.0f));
    EXPECT_TRUE(viewed_calls_.empty());
    EXPECT_TRUE(slider_frames_.empty());
    EXPECT_TRUE(contrast_frames_.empty());
    ASSERT_FALSE(white_balance_frames_.empty());
    int activated = 0;
    int deactivated = 0;
    for (const WhiteBalanceCall& call : white_balance_frames_) {
        EXPECT_EQ(call.control, gui::WhiteBalanceControl::kelvin);
        activated += call.frame.activated;
        deactivated += call.frame.deactivated;
        // A frame that moved nothing carries no value.
        EXPECT_EQ(call.value.has_value(), call.frame.changed);
    }
    EXPECT_EQ(activated, 1);
    EXPECT_EQ(deactivated, 1);
    ASSERT_TRUE(settings_.white_balance);
    EXPECT_EQ(settings_.white_balance->tint, 27.0);  // from as shot, untouched
    const double chosen = settings_.white_balance->kelvin;
    EXPECT_EQ(std::fmod(chosen, 10.0), 0.0);
    // Past the mired midpoint, and well short of where a slider linear in Kelvin would be (11100).
    EXPECT_GT(chosen, 3600.0);
    EXPECT_LT(chosen, 6000.0);
    // The knob is where the choice is.
    frame();
    const gui::LightPathRow row = gui::light_path_row(
        {settings_, kind_, "RAW", viewed_, compare_, nullptr, as_shot_},
        gui::StageId::white_balance);
    EXPECT_NEAR(row.white_balance->kelvin_position, 0.7f, 0.05f);
}

TEST_F(LightPathHeadless, DraggingTintChangesOnlyTintAsOneGesture) {
    as_shot_ = prism::white_of(prism::WhiteBalance{5328, 26.7});
    frame();
    frame();
    const Placed tint = white_balance_sliders()[1];
    const float width = tint.max.x - tint.min.x;
    // From the middle (tint 0) a quarter of the way left: towards green.
    drag(tint.centre(), ImVec2(tint.centre().x - width * 0.25f, tint.centre().y - 40.0f));
    EXPECT_TRUE(viewed_calls_.empty());
    EXPECT_TRUE(slider_frames_.empty());
    ASSERT_FALSE(white_balance_frames_.empty());
    int activated = 0;
    int deactivated = 0;
    for (const WhiteBalanceCall& call : white_balance_frames_) {
        EXPECT_EQ(call.control, gui::WhiteBalanceControl::tint);
        activated += call.frame.activated;
        deactivated += call.frame.deactivated;
    }
    EXPECT_EQ(activated, 1);
    EXPECT_EQ(deactivated, 1);
    ASSERT_TRUE(settings_.white_balance);
    EXPECT_EQ(settings_.white_balance->kelvin, 5330.0);  // from as shot, untouched
    EXPECT_EQ(settings_.white_balance->tint, std::round(settings_.white_balance->tint));
    EXPECT_NEAR(settings_.white_balance->tint, -50.0, 6.0);
}

TEST_F(LightPathHeadless, APressOnAWhiteBalanceSliderWhereItSitsLeavesThePhotoAsShot) {
    frame();
    frame();
    // As shot without a camera's light: tint 0 is the middle of its slider.
    click_at(white_balance_sliders()[1].centre());
    ASSERT_FALSE(white_balance_frames_.empty());
    EXPECT_TRUE(white_balance_frames_.front().frame.activated);
    EXPECT_TRUE(white_balance_frames_.back().frame.deactivated);
    EXPECT_FALSE(settings_.white_balance);
    EXPECT_TRUE(viewed_calls_.empty());
}

TEST_F(LightPathHeadless, AWhiteBalanceSliderThatIsOffDoesNothing) {
    settings_.white_balance = prism::WhiteBalance{5200, 10};
    settings_.white_balance_on = false;
    frame();
    frame();
    const std::vector<Placed> sliders = white_balance_sliders();
    click_at(sliders[0].centre());
    click_at(sliders[1].centre());
    ctrl_click(sliders[0].centre());
    EXPECT_FALSE(ImGui::GetIO().WantTextInput);
    EXPECT_TRUE(white_balance_frames_.empty());
    EXPECT_TRUE(viewed_calls_.empty());
    EXPECT_EQ(settings_.white_balance, (prism::WhiteBalance{5200, 10}));
}

TEST_F(LightPathHeadless, ARightClickOnAWhiteBalanceSliderOffersResetToAsShot) {
    settings_.white_balance = prism::WhiteBalance{5200, 10};
    for (const std::size_t which : {std::size_t{0}, std::size_t{1}}) {
        white_balance_resets_ = 0;
        frame();
        frame();
        const ImVec2 at = white_balance_sliders()[which].centre();
        mouse_to(at);
        frame();
        frame();
        mouse(true, 1);
        frame();
        mouse(false, 1);
        frame();
        frame();
        // The popup's only item is right under the cursor, a little down.
        mouse_to(ImVec2(at.x + 12, at.y + 12));
        frame();
        frame();
        mouse(true);
        frame();
        mouse(false);
        frame();
        frame();
        EXPECT_EQ(white_balance_resets_, 1) << which;
    }
    EXPECT_TRUE(resets_.empty());
    EXPECT_TRUE(white_balance_frames_.empty());
    EXPECT_TRUE(viewed_calls_.empty());
}

TEST_F(LightPathHeadless, TypedKelvinIsOneChangeAndLeavesTint) {
    as_shot_ = prism::white_of(prism::WhiteBalance{5328, 26.7});
    frame();
    frame();
    ctrl_click(white_balance_sliders()[0].centre());
    EXPECT_TRUE(ImGui::GetIO().WantTextInput);
    EXPECT_TRUE(white_balance_frames_.empty());  // opening changes nothing
    type_and_enter("5 200");  // replaces "5330 K", which opens selected
    ASSERT_EQ(white_balance_frames_.size(), 1u);
    const WhiteBalanceCall& call = white_balance_frames_[0];
    EXPECT_EQ(call.control, gui::WhiteBalanceControl::kelvin);
    EXPECT_EQ(call.value, 5200.0);
    EXPECT_TRUE(call.frame.activated && call.frame.changed && call.frame.deactivated);
    EXPECT_EQ(settings_.white_balance, (prism::WhiteBalance{5200, 27}));
    EXPECT_FALSE(ImGui::GetIO().WantTextInput);
    EXPECT_TRUE(viewed_calls_.empty());
}

TEST_F(LightPathHeadless, TypedTintTakesARealMinusSignAndLeavesKelvin) {
    as_shot_ = prism::white_of(prism::WhiteBalance{5328, 26.7});
    frame();
    frame();
    ctrl_click(white_balance_sliders()[1].centre());
    EXPECT_TRUE(ImGui::GetIO().WantTextInput);
    type_and_enter("\xE2\x88\x92" "10");
    ASSERT_EQ(white_balance_frames_.size(), 1u);
    EXPECT_EQ(white_balance_frames_[0].control, gui::WhiteBalanceControl::tint);
    EXPECT_EQ(white_balance_frames_[0].value, -10.0);
    EXPECT_TRUE(white_balance_frames_[0].frame.activated &&
                white_balance_frames_[0].frame.deactivated);
    EXPECT_EQ(settings_.white_balance, (prism::WhiteBalance{5330, -10}));
}

TEST_F(LightPathHeadless, TypedWhiteBalanceTextThatIsNotANumberLeavesThePhotoAsShot) {
    frame();
    frame();
    ctrl_click(white_balance_sliders()[0].centre());
    type_and_enter("warm");
    EXPECT_TRUE(white_balance_frames_.empty());
    EXPECT_FALSE(settings_.white_balance);
    EXPECT_FALSE(ImGui::GetIO().WantTextInput);
}

TEST_F(LightPathHeadless, TypedWhiteBalanceFieldKeepsEveryRowWhereItWas) {
    frame();
    const std::vector<float> heights = heights_of(frame());
    const std::vector<Placed> sliders = white_balance_sliders();
    for (const Placed& slider : sliders) {
        ctrl_click(slider.centre());
        EXPECT_TRUE(ImGui::GetIO().WantTextInput);
        EXPECT_EQ(heights_of(frame()), heights);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        frame();
        EXPECT_EQ(heights_of(frame()), heights);
    }
    EXPECT_TRUE(white_balance_frames_.empty());
}

// --- The minimum width -------------------------------------------------------------------------

// A clip report in which every stage has just one kind of mark, recoverable, at `count` of 1000.
gui::ClipReport one_mark_report(gui::ClipMarkKind kind, std::uint64_t count) {
    gui::ClipReport report;
    report.sample_count = 1000;
    for (const gui::StageInfo& stage : gui::pipeline_stages()) {
        gui::StageClip& clip = report.stages[static_cast<std::size_t>(stage.id)];
        clip.stage = stage.id;
        clip.ran = true;
        switch (kind) {
            case gui::ClipMarkKind::below_black:
                clip.under.any = count;
                clip.under_fate = gui::ClipFate::recoverable;
                break;
            case gui::ClipMarkKind::above_white:
                clip.over.any = count;
                clip.over_fate = gui::ClipFate::recoverable;
                break;
            case gui::ClipMarkKind::out_of_gamut:
                clip.gamut.any = count;
                clip.gamut_fate = gui::ClipFate::recoverable;
                break;
        }
    }
    return report;
}

// The panel at its minimum width, as the app draws it (the panel's padding, and a scrollbar), with
// the worst cases of every text in it, one after the other. Every text that has to fit is written
// down by the panel (LightPathData::fits) and must have been drawn in full.
class MinimumWidthHeadless : public LightPathHeadless {
protected:
    void SetUp() override {
        LightPathHeadless::SetUp();
        app_padding_ = true;
        height_ = 600.0f;  // shorter than the rows, so the panel scrolls
        fits_ = &frame_fits_;
    }

    // Draws the case and keeps what it wrote down.
    void run_case() {
        frame();
        frame_fits_.clear();
        frame();
        fits_seen_.insert(fits_seen_.end(), frame_fits_.begin(), frame_fits_.end());
    }

    void run_all_cases(float scale) {
        apply_style(gui::Theme::dark, scale);
        ImGui::NewFrame();
        const float minimum = light_path_min_width();
        ImGui::EndFrame();
        width_ = minimum;
        as_shot_ = prism::white_of(prism::WhiteBalance{5328, 26.7});

        // Exposure at both ends of its range, on (with "beyond the slider") and off ("when on").
        for (const float stops : {-10.0f, 10.0f}) {
            for (const bool on : {true, false}) {
                settings_ = {};
                settings_.exposure_stops = stops;
                settings_.exposure_on = on;
                run_case();
            }
        }
        // Contrast: past both ends of the grades, between two, and off.
        for (const float contrast : {0.6f, 2.4f, 1.15f, 1.45f}) {
            for (const bool on : {true, false}) {
                settings_ = {};
                settings_.contrast = contrast;
                settings_.contrast_on = on;
                run_case();
            }
        }
        settings_ = {};
        settings_.saturation = 0.0f;
        run_case();
        // White balance: As shot on a RAW file with its light and on files without one, and a
        // chosen light at the ends of both sliders, on and off.
        settings_ = {};
        run_case();
        for (const std::string_view file : {"JPEG", "PNG", "HEIC", "RAW"}) {
            file_kind_ = file;
            kind_ = file == "RAW" ? gui::InputKind::raw : gui::InputKind::image;
            as_shot_ = std::nullopt;
            run_case();
        }
        as_shot_ = prism::white_of(prism::WhiteBalance{5328, 26.7});
        file_kind_ = "RAW";
        kind_ = gui::InputKind::raw;
        for (const prism::WhiteBalance light :
             {prism::WhiteBalance{15000, -100}, prism::WhiteBalance{2000, 100}}) {
            for (const bool on : {true, false}) {
                settings_ = {};
                settings_.white_balance = light;
                settings_.white_balance_on = on;
                run_case();
            }
        }
        // Dodge & burn: the most strokes of one kind and of both, on and off.
        for (const bool on : {true, false}) {
            for (const std::size_t burns : {std::size_t{0}, std::size_t{999}}) {
                settings_ = {};
                settings_.dodge_burn_on = on;
                auto field = std::make_shared<prism::StrokeField>();
                field->aspect = 1.5f;
                for (std::size_t i = 0; i < 999 + burns; ++i) {
                    field->strokes.push_back(std::make_shared<const prism::FieldStroke>(
                        prism::FieldStroke{{{0.5f, 0.5f}}, 0.05f, 0.5f, i < burns ? -0.5f : 0.5f}));
                }
                settings_.strokes = gui::Strokes(std::move(field));
                run_case();
            }
        }
        settings_ = {};
        // Strength at its widest reading, "2.0 stops".
        brush_strength_ = 2.0f;
        run_case();
        brush_strength_ = 0.5f;
        // Colour matrix: the camera's, none, and a recipe's.
        run_case();
        kind_ = gui::InputKind::image;
        file_kind_ = "JPEG";
        run_case();
        settings_.matrix = gui::MatrixFile{};
        run_case();
        kind_ = gui::InputKind::raw;
        file_kind_ = "RAW";
        settings_ = {};
        settings_.raw_camera_matrix = gui::MatrixFile{};
        run_case();
        // Tone curve: each kind, and the number's line at its widest.
        for (const gui::CurveKind curve : gui::curve_kinds) {
            settings_ = {};
            settings_.curve = curve;
            settings_.reinhard_white = 16.0f;
            settings_.gamma = 4.0f;
            run_case();
        }
        // A clip note on every row, of each kind and at its widest, beside Exposure's button.
        settings_ = {};
        for (const gui::ClipMarkKind kind :
             {gui::ClipMarkKind::below_black, gui::ClipMarkKind::above_white,
              gui::ClipMarkKind::out_of_gamut}) {
            for (const std::uint64_t count : {1000ull, 880ull, 88ull}) {
                clips_ = one_mark_report(kind, count);
                run_case();
            }
        }
        clips_.reset();
        // The header with Compare on, at its longest.
        viewed_ = gui::StageId::curve;
        for (const gui::StageInfo& stage : gui::pipeline_stages()) {
            if (stage.hidden) {
                continue;
            }
            compare_ = stage.id;
            run_case();
        }
        viewed_ = gui::final_stage;
        compare_.reset();
    }

    [[nodiscard]] bool seen(std::string_view what, std::string_view text) const {
        return std::any_of(fits_seen_.begin(), fits_seen_.end(), [&](const TextFit& fit) {
            return fit.what == what && fit.text == text;
        });
    }
    [[nodiscard]] bool seen_starting(std::string_view what, std::string_view start) const {
        return std::any_of(fits_seen_.begin(), fits_seen_.end(), [&](const TextFit& fit) {
            return fit.what == what && fit.text.rfind(start, 0) == 0;
        });
    }

    std::vector<TextFit> frame_fits_;
    std::vector<TextFit> fits_seen_;
};

TEST_F(MinimumWidthHeadless, EveryWorstCaseTextIsDrawnInFullAtTheMinimumWidth) {
    for (const float scale : {1.0f, 1.5f, 2.0f}) {
        fits_seen_.clear();
        run_all_cases(scale);
        for (const TextFit& fit : fits_seen_) {
            EXPECT_TRUE(fit.whole()) << scale << "x: " << fit.what << " '" << fit.text << "' drawn as '"
                                     << fit.shown << "', " << fit.width << " wide in " << fit.room;
        }
        // The worst cases were there to be checked.
        EXPECT_TRUE(seen("Value", "-10.00 stops")) << scale;
        EXPECT_TRUE(seen("Value", "+10.00 stops")) << scale;
        EXPECT_TRUE(seen("Value", "+10.00 stops when on")) << scale;
        EXPECT_TRUE(seen("Beyond the slider", std::string(gui::light_path_text::beyond_slider))) << scale;
        EXPECT_TRUE(seen("Value", "Softer than grade 0 \xC2\xB7 \xC3\x97" "0.60")) << scale;
        EXPECT_TRUE(seen("Value", "Harder than grade 5 \xC2\xB7 \xC3\x97" "2.40 when on")) << scale;
        EXPECT_TRUE(seen_starting("Value", "Grade 2")) << scale;  // between two grades
        EXPECT_TRUE(seen("Value", "As shot")) << scale;
        EXPECT_TRUE(seen("Value", "Chosen when on")) << scale;
        EXPECT_TRUE(seen("Subtitle", "The light the photo was lit by")) << scale;
        EXPECT_TRUE(seen("Subtitle", "Light not recorded, taken as 6500 K")) << scale;
        EXPECT_TRUE(seen("Subtitle", "HEIC, converted from its own colours")) << scale;
        EXPECT_TRUE(seen("Kelvin value", "15000 K")) << scale;
        EXPECT_TRUE(seen("Tint value", "\xE2\x88\x92" "100")) << scale;
        EXPECT_TRUE(seen("Value", "Camera default")) << scale;
        // Dodge & burn: the longest value, and the controls line's label, value and track.
        EXPECT_TRUE(seen("Value", std::string(gui::widest_strokes_value))) << scale;
        EXPECT_TRUE(seen("Value", "999 dodges when on")) << scale;
        EXPECT_TRUE(seen("Strength label", "Strength")) << scale;
        EXPECT_TRUE(seen("Strength value", "2.0 stops")) << scale;
        EXPECT_TRUE(seen("Strength track", "track")) << scale;
        EXPECT_TRUE(seen("Value", "From recipe")) << scale;
        for (const char* tab : {"None", "Gamma", "Reinhard", "Filmic"}) {
            EXPECT_TRUE(seen("Tone curve tab", tab)) << scale << " " << tab;
        }
        EXPECT_TRUE(seen("Curve number", "White point 16.00")) << scale;
        EXPECT_TRUE(seen("Clip note", "100% outside sRGB here")) << scale;
        EXPECT_TRUE(seen("Clip note", "100% above white here")) << scale;
        EXPECT_TRUE(seen("Clip note", "100% below black here")) << scale;
        EXPECT_TRUE(seen("Header", "Light path Compare: after Output encode")) << scale;
        EXPECT_TRUE(seen("Kelvin track", "track")) << scale;
    }
}

TEST_F(MinimumWidthHeadless, DodgeAndBurnDoesNotMakeThePanelWiderThanItWas) {
    apply_style(gui::Theme::dark, 1.0f);
    ImGui::NewFrame();
    // 428 pixels at the interface size 1, as before the Dodge & burn row (layout_test.cpp).
    EXPECT_LE(light_path_min_width(), 428.0f);
    ImGui::EndFrame();
}

TEST_F(MinimumWidthHeadless, TheDodgeAndBurnWorstCaseIsInTheWorstCases) {
    // 1998 strokes, burns and dodges. The text ends in "when on" because the stage is switched off.
    auto field = std::make_shared<prism::StrokeField>();
    field->aspect = 1.5f;
    for (std::size_t i = 0; i < 1998; ++i) {
        field->strokes.push_back(std::make_shared<const prism::FieldStroke>(
            prism::FieldStroke{{{0.5f, 0.5f}}, 0.05f, 0.5f, i < 999 ? -0.5f : 0.5f}));
    }
    settings_ = {};
    settings_.strokes = gui::Strokes(std::move(field));
    settings_.dodge_burn_on = false;
    const gui::LightPathView view =
        gui::light_path_view({settings_, gui::InputKind::image, "JPEG", gui::final_stage, std::nullopt, nullptr, std::nullopt});
    std::string text;
    for (const gui::LightPathRow& row : view.rows) {
        if (row.stage == gui::StageId::dodge_burn) {
            text = row.value;
        }
    }
    ASSERT_EQ(text, std::string(gui::widest_strokes_value));
    ASSERT_TRUE(text.size() > 7 && text.substr(text.size() - 7) == "when on");

    // The text is one the minimum width is measured from.
    bool listed = false;
    for (const gui::EditSettings& worst : light_path_worst_case_settings()) {
        const gui::LightPathView worst_view = gui::light_path_view(
            {worst, gui::InputKind::image, "JPEG", gui::final_stage, std::nullopt, nullptr, std::nullopt});
        for (const gui::LightPathRow& row : worst_view.rows) {
            listed = listed || (row.stage == gui::StageId::dodge_burn && row.value == text);
        }
    }
    EXPECT_TRUE(listed) << "'" << text << "' is not among the worst cases";

    // At the minimum width the text is drawn in full.
    ImGui::NewFrame();
    width_ = light_path_min_width();
    ImGui::EndFrame();
    run_case();
    EXPECT_TRUE(seen("Value", text));
    for (const TextFit& fit : fits_seen_) {
        if (fit.what == "Value" && fit.text == text) {
            EXPECT_TRUE(fit.whole()) << "'" << fit.text << "' drawn as '" << fit.shown << "', "
                                     << fit.width << " wide in " << fit.room;
        }
    }
}

TEST_F(MinimumWidthHeadless, ThePanelReportsTextsThatDoNotFitWhenItIsNarrower) {
    // The test above is only worth something if it can fail: 40 mock pixels narrower than the
    // minimum, the worst cases of the rows that set it are reported as cut.
    ImGui::NewFrame();
    width_ = light_path_min_width() - mock(40.0f);
    ImGui::EndFrame();
    as_shot_ = prism::white_of(prism::WhiteBalance{5328, 26.7});
    for (const float stops : {-10.0f, 10.0f}) {
        settings_ = {};
        settings_.exposure_stops = stops;
        run_case();
    }
    settings_ = {};
    settings_.contrast = 2.4f;
    settings_.contrast_on = false;
    run_case();
    settings_ = {};
    settings_.curve = gui::CurveKind::reinhard;
    run_case();
    const auto cut = std::count_if(fits_seen_.begin(), fits_seen_.end(),
                                   [](const TextFit& fit) { return !fit.whole(); });
    EXPECT_GE(cut, 2);
}

TEST_F(MinimumWidthHeadless, AWhiteBalanceNumberBeyondTheColumnIsReportedAsNotFitting) {
    const TextFit wide{"Kelvin value", "115000 K", "115000 K", 80.0f, 60.0f};
    EXPECT_FALSE(wide.whole());
    const TextFit cut{"Value", "Chosen when on", "Cho\xE2\x80\xA6", 40.0f, 60.0f};
    EXPECT_FALSE(cut.whole());
    const TextFit fine{"Value", "As shot", "As shot", 40.0f, 60.0f};
    EXPECT_TRUE(fine.whole());
}

TEST_F(MinimumWidthHeadless, ASavedWidthBelowTheMinimumIsWidenedToIt) {
    ImGui::NewFrame();
    const float minimum = light_path_min_width();
    ImGui::EndFrame();
    // What an older version saved: 320 points, narrower than the texts need.
    ASSERT_GT(minimum, 320.0f);
    EXPECT_GE(minimum, 240.0f);
    EXPECT_GE(static_cast<float>(gui::widen_panel_width(320, 1.0f, minimum)), minimum);
    EXPECT_GE(gui::panel_width_pixels(320.0f, 1.0f, 1280.0f, minimum), minimum);
    EXPECT_GE(gui::panel_width_pixels(240.0f, 1.0f, 1280.0f, minimum), minimum);
    // The default panel width holds the texts, so a photo opened with it shows nothing cut.
    EXPECT_GE(static_cast<float>(gui::panel_width_default), minimum);
    // The mockup's pixels: the same minimum at 1.5x is 1.5x the pixels, give or take the fonts.
    apply_style(gui::Theme::dark, 1.5f);
    ImGui::NewFrame();
    const float bigger = light_path_min_width();
    ImGui::EndFrame();
    EXPECT_NEAR(bigger, minimum * 1.5f, minimum * 0.03f);
}

TEST_F(MinimumWidthHeadless, TheWhiteBalanceValuesLineUpWhateverTheyRead) {
    app_padding_ = true;
    ImGui::NewFrame();
    width_ = light_path_min_width();
    ImGui::EndFrame();
    std::vector<std::pair<ImVec2, ImVec2>> kelvin_rects;
    std::vector<std::pair<ImVec2, ImVec2>> tint_rects;
    std::vector<float> kelvin_rooms;
    std::vector<float> tint_rooms;
    const prism::WhiteBalance lights[] = {
        {15000, -100}, {2000, 100}, {5810, 34}, {5330, 0}, {6500, -8}};
    for (std::size_t i = 0; i <= std::size(lights); ++i) {
        settings_ = {};
        if (i < std::size(lights)) {
            settings_.white_balance = lights[i];
        }
        frame_fits_.clear();
        frame();
        frame();
        const std::vector<Placed> sliders = white_balance_sliders();
        kelvin_rects.push_back({sliders[0].min, sliders[0].max});
        tint_rects.push_back({sliders[1].min, sliders[1].max});
        for (const TextFit& fit : frame_fits_) {
            if (fit.what == "Kelvin value") {
                kelvin_rooms.push_back(fit.room);
                EXPECT_TRUE(fit.whole()) << fit.text;
            } else if (fit.what == "Tint value") {
                tint_rooms.push_back(fit.room);
                EXPECT_TRUE(fit.whole()) << fit.text;
            }
        }
    }
    ASSERT_EQ(kelvin_rects.size(), std::size(lights) + 1);
    for (std::size_t i = 0; i < kelvin_rects.size(); ++i) {
        // One slider under the other, the same width, ending in the same place whatever they read.
        EXPECT_EQ(kelvin_rects[i].first.x, tint_rects[i].first.x) << i;
        EXPECT_EQ(kelvin_rects[i].second.x, tint_rects[i].second.x) << i;
        EXPECT_EQ(kelvin_rects[i].second.x, kelvin_rects[0].second.x) << i;
        EXPECT_EQ(kelvin_rects[i].first.x, kelvin_rects[0].first.x) << i;
    }
    // And the values share one column, wide enough for the widest (15000 K and the real minus -100).
    ImGui::NewFrame();
    ImGui::PushFont(nullptr, token_type().small);
    const float widest = std::max(ImGui::CalcTextSize("15000 K").x,
                                  ImGui::CalcTextSize("\xE2\x88\x92" "100").x);
    ImGui::PopFont();
    ImGui::EndFrame();
    for (const float room : kelvin_rooms) {
        EXPECT_EQ(room, kelvin_rooms[0]);
        EXPECT_GE(room, widest);
    }
    for (const float room : tint_rooms) {
        EXPECT_EQ(room, kelvin_rooms[0]);
    }
}

// --- The Easel line (#58) ------------------------------------------------------------------------

// The Light path panel, looked at for its Easel line.
class EaselLineHeadless : public LightPathHeadless {};

TEST_F(EaselLineHeadless, TheLineIsBetweenTheHeaderAndTheFirstRowAndAlwaysTheSameHeight) {
    const std::vector<LightPathRowRect> first = frame();
    EXPECT_FLOAT_EQ(easel_.max.y - easel_.min.y, easel_line_height());
    EXPECT_FLOAT_EQ(easel_.max.y, first.front().min.y);
    EXPECT_EQ(first.front().stage, gui::StageId::decode);  // not a stage: Decode is still first
    const LightPathData::Rect line = easel_;
    // Whatever it says, framing or not, with a photo or without: nothing moves.
    for (const char* value : {"4:5 \xC2\xB7 1.2\xC2\xB0 clockwise", "Free \xC2\xB7 about 5120 \xC3\x97 3413",
                              "Whole photo"}) {
        for (const bool framing : {false, true}) {
            for (const bool available : {true, false}) {
                easel_value_ = value;
                framing_ = framing;
                easel_available_ = available;
                frame();
                const std::vector<LightPathRowRect> rects = frame();
                EXPECT_FLOAT_EQ(easel_.min.y, line.min.y);
                EXPECT_FLOAT_EQ(easel_.max.y, line.max.y);
                ASSERT_EQ(rects.size(), first.size());
                for (std::size_t r = 0; r < rects.size(); ++r) {
                    EXPECT_FLOAT_EQ(rects[r].min.y, first[r].min.y) << value;
                    EXPECT_FLOAT_EQ(rects[r].max.y, first[r].max.y) << value;
                }
            }
        }
    }
}

TEST_F(EaselLineHeadless, APressOnTheLineOrItsButtonTogglesFramingAndViewsNoStage) {
    frame();
    const float middle_y = (easel_.min.y + easel_.max.y) * 0.5f;
    // On its name.
    click_at(ImVec2(easel_.min.x + std::round(mock(27.0f)) + 4.0f, middle_y));
    EXPECT_EQ(framing_toggles_, 1);
    // On the Frame button at its right end.
    click_at(ImVec2(easel_.max.x - std::round(mock(8.0f)) - 6.0f, middle_y));
    EXPECT_EQ(framing_toggles_, 2);
    // Its empty middle.
    click_at(ImVec2((easel_.min.x + easel_.max.x) * 0.5f, middle_y));
    EXPECT_EQ(framing_toggles_, 3);
    EXPECT_TRUE(viewed_calls_.empty());
    EXPECT_TRUE(compares_.empty());
    EXPECT_TRUE(switches_.empty());
}

TEST_F(EaselLineHeadless, WithoutAPhotoAPressDoesNothing) {
    easel_available_ = false;
    frame();
    const float middle_y = (easel_.min.y + easel_.max.y) * 0.5f;
    click_at(ImVec2((easel_.min.x + easel_.max.x) * 0.5f, middle_y));
    click_at(ImVec2(easel_.max.x - std::round(mock(8.0f)) - 6.0f, middle_y));
    EXPECT_EQ(framing_toggles_, 0);
    EXPECT_TRUE(viewed_calls_.empty());
}

TEST_F(EaselLineHeadless, ItExplainsItselfAndIsNotInTheComparePopup) {
    frame();
    EXPECT_EQ(ids_.count(std::string(gui::explain::easel)), 1u);
    ASSERT_NE(gui::explanation_for(gui::explain::easel), nullptr);
    // The Compare popup lists Off, the stages and Swap sides: no Easel.
    viewed_ = gui::StageId::contrast;
    std::vector<LightPathRowRect> rects = frame();
    click_at(ImVec2(rects.front().max.x - 12.0f, easel_.min.y - 12.0f));
    frame();
    ImGuiWindow* popup = ImGui::GetCurrentContext()->OpenPopupStack.empty()
                             ? nullptr
                             : ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
    ASSERT_NE(popup, nullptr);
    const float item = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
    // Off, Original, then a row per stage after Decode, then Swap sides.
    const int stages = static_cast<int>(std::count_if(
        gui::pipeline_stages().begin(), gui::pipeline_stages().end(),
        [](const gui::StageInfo& st) { return !st.hidden; }));
    EXPECT_LT(popup->ContentSize.y, item * static_cast<float>(stages + 3));
}

TEST_F(MinimumWidthHeadless, TheEaselsWidestValuesAreDrawnWholeAtTheMinimumWidth) {
    for (const float scale : {1.0f, 1.5f, 2.0f}) {
        apply_style(gui::Theme::dark, scale);
        ImGui::NewFrame();
        width_ = light_path_min_width();
        ImGui::EndFrame();
        fits_seen_.clear();
        for (const prism::Aspect aspect : prism::all_aspects) {
            for (const float angle : {0.0f, -44.95f, 44.95f, -0.04f}) {
                for (const bool exact : {true, false}) {
                    prism::Frame f;
                    f.aspect = aspect;
                    f.angle = angle;
                    easel_value_ = gui::easel_value(f, {65535, 65535}, exact);
                    run_case();
                }
            }
        }
        int easel_fits = 0;
        for (const TextFit& fit : fits_seen_) {
            if (fit.what == "Easel value") {
                ++easel_fits;
                EXPECT_TRUE(fit.whole()) << scale << " '" << fit.text << "' drawn as '" << fit.shown
                                         << "', " << fit.width << " wide in " << fit.room;
            }
        }
        EXPECT_GT(easel_fits, 0) << scale;
        EXPECT_TRUE(seen("Easel value", "Original \xC2\xB7 about 65535 \xC3\x97 65535")) << scale;
        EXPECT_TRUE(seen("Easel value", "Original \xC2\xB7 under 0.1\xC2\xB0 anticlockwise")) << scale;
    }
    // Worth something only if it can fail: on a panel much narrower than the minimum, the widest
    // values are reported as cut.
    apply_style(gui::Theme::dark, 1.0f);
    ImGui::NewFrame();
    width_ = light_path_min_width() * 0.6f;
    ImGui::EndFrame();
    fits_seen_.clear();
    prism::Frame f;
    f.angle = -44.95f;
    easel_value_ = gui::easel_value(f, {65535, 65535}, false);
    run_case();
    EXPECT_TRUE(std::any_of(fits_seen_.begin(), fits_seen_.end(), [](const TextFit& fit) {
        return fit.what == "Easel value" && !fit.whole();
    }));
}

}  // namespace

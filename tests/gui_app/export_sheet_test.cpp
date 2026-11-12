#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "active_control.hpp"
#include "export_sheet.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "prism/gui/explanations.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace {

using namespace prism::gui_app;
namespace gui = prism::gui;
using Mode = gui::ExportSize::Mode;

constexpr std::array<Mode, 3> all_modes{Mode::original, Mode::long_side, Mode::percent};

// The sheet in a window like the app's modal (its size and padding), with a headless ImGui and the
// app's real fonts, style and widgets: no backend, the mouse and keys fed through the input queue.
class ExportSheetHeadless : public ::testing::Test {
protected:
    void SetUp() override {
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        load_fonts(io);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        apply_style(gui::Theme::dark, 1.0f);
        state_.focus_field = false;
        file_ = {6000, 4000};
        start_ = file_;
    }
    void TearDown() override {
        ImGui::DestroyContext(context_);
    }

    struct Placed {
        ExportSheetRects rects;
        ImVec2 content_origin;
        ImVec2 window_min, window_max;
        float bottom = 0.0f;  // the cursor under the sheet, screen units
    };

    // One frame. Results are kept until read with take_result().
    Placed frame() {
        hovered_ids_.clear();
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(100, 50));
        ImGui::SetNextWindowSize(export_sheet_size());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(export_sheet_padding(), export_sheet_padding()));
        ImGui::Begin("export", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar();
        ExportSheetCallbacks cb;
        cb.explain = [this](std::string_view id) {
            if (ImGui::IsItemHovered()) {
                hovered_ids_.insert(std::string(id));
            }
        };
        Placed placed;
        placed.content_origin = ImGui::GetCursorScreenPos();
        const ExportSheetResult result =
            draw_export_sheet(state_, ExportSheetContext{file_, start_, exact_, framed_}, cb,
                              &placed.rects);
        if (result != ExportSheetResult::none) {
            results_.push_back(result);
        }
        placed.bottom = ImGui::GetCursorScreenPos().y;
        placed.window_min = ImGui::GetWindowPos();
        placed.window_max = ImVec2(placed.window_min.x + ImGui::GetWindowSize().x,
                                   placed.window_min.y + ImGui::GetWindowSize().y);
        ImGui::End();
        ImGui::EndFrame();
        return placed;
    }
    // Twice: what a change shows comes through on the frame after it.
    Placed settled() {
        frame();
        return frame();
    }

    ExportSheetResult take_result() {
        const ExportSheetResult r = results_.empty() ? ExportSheetResult::none : results_.back();
        results_.clear();
        return r;
    }

    static void expect_same(const Placed& a, const Placed& b) {
        const auto same = [](ImVec2 p, ImVec2 q, const char* what) {
            EXPECT_FLOAT_EQ(p.x, q.x) << what;
            EXPECT_FLOAT_EQ(p.y, q.y) << what;
        };
        EXPECT_FLOAT_EQ(a.bottom, b.bottom);
        EXPECT_FLOAT_EQ(a.rects.height, b.rects.height);
        same(a.rects.title_min, b.rects.title_min, "title");
        same(a.rects.title_max, b.rects.title_max, "title");
        same(a.rects.label_min, b.rects.label_min, "label");
        same(a.rects.tabs_min, b.rects.tabs_min, "tabs");
        same(a.rects.tabs_max, b.rects.tabs_max, "tabs");
        same(a.rects.row_min, b.rects.row_min, "row");
        same(a.rects.row_max, b.rects.row_max, "row");
        same(a.rects.result_min, b.rects.result_min, "result");
        same(a.rects.result_max, b.rects.result_max, "result");
        same(a.rects.cancel_min, b.rects.cancel_min, "cancel");
        same(a.rects.cancel_max, b.rects.cancel_max, "cancel");
        same(a.rects.save_min, b.rects.save_min, "save");
        same(a.rects.save_max, b.rects.save_max, "save");
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
    void click(ImVec2 at) {
        mouse_to(at);
        frame();
        mouse(true);
        frame();
        mouse(false);
        frame();
    }
    void press_key(ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        frame();
    }
    void type(const char* text) {
        for (const char* c = text; *c != '\0'; ++c) {
            ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(*c));
            frame();
        }
    }
    // Chooses `mode` by clicking its tab.
    void choose(Mode mode) {
        const Placed p = settled();
        const float third = (p.rects.tabs_max.x - p.rects.tabs_min.x) / 3.0f;
        std::size_t index = 0;
        for (std::size_t i = 0; i < all_modes.size(); ++i) {
            if (all_modes[i] == mode) {
                index = i;
            }
        }
        click(ImVec2(p.rects.tabs_min.x + third * (static_cast<float>(index) + 0.5f),
                     (p.rects.tabs_min.y + p.rects.tabs_max.y) * 0.5f));
        EXPECT_EQ(state_.size.mode, mode);
    }
    // Clicks into the number field (which selects its text), ready to type.
    void focus_field() {
        const Placed p = settled();
        click(middle(p.rects.field_min, p.rects.field_max));
        EXPECT_NE(ImGui::GetActiveID(), 0u);
    }
    // Leaves the field by clicking the sheet's title.
    void leave_field() {
        const Placed p = settled();
        click(middle(p.rects.title_min, p.rects.title_max));
        settled();
    }

    ImGuiContext* context_ = nullptr;
    ExportSheetState state_;
    prism::PixelSize file_;
    prism::PixelSize start_;
    bool exact_ = true;
    bool framed_ = false;
    std::vector<ExportSheetResult> results_;
    std::set<std::string> hovered_ids_;
};

// The sizes a sheet may be asked to show: an ordinary photo, a choice that is not smaller than it, a
// very long file size, a portrait and a tiny one.
struct Case {
    prism::PixelSize file;
    prism::PixelSize start;
    int long_side;
    int percent;
};
const std::array<Case, 5> cases{
    Case{{6000, 4000}, {6000, 4000}, 2048, 50},
    Case{{6000, 4000}, {6000, 4000}, 9000, 99},                // not smaller than the photo
    Case{{123456789, 987654321}, {123456789, 987654321}, 5, 1},  // a very long file size
    Case{{4000, 6000}, {2000, 3000}, 2048, 50},                // portrait, RAW at half size
    Case{{1, 1}, {1, 1}, 1, 1},
};

// --- #38: nothing that is shown moves anything ----------------------------------------------------

TEST_F(ExportSheetHeadless, TheSheetIsTheSameHeightInAllThreeModesAndForEverySize) {
    std::optional<Placed> first;
    for (const Case& c : cases) {
        file_ = c.file;
        start_ = c.start;
        state_.size.long_side = c.long_side;
        state_.size.percent = c.percent;
        for (const Mode mode : all_modes) {
            state_.size.mode = mode;
            const Placed p = settled();
            if (!first) {
                first = p;
            }
            expect_same(p, *first);
        }
    }
    EXPECT_FLOAT_EQ(first->rects.height, first->bottom - first->content_origin.y);
    // And it is what the window was sized for: the content plus the padding, both ways.
    EXPECT_FLOAT_EQ(export_sheet_size().y, first->rects.height + 2 * export_sheet_padding());
    EXPECT_FLOAT_EQ(first->window_max.y - first->window_min.y, export_sheet_size().y);
}

// A framed photo (#58 §7): the sheet measures the frame and says so, in all three modes, with
// "about" for a RAW file's estimate, and keeps its height.
TEST_F(ExportSheetHeadless, AFramedPhotoSaysFullSizeOfTheFrame) {
    file_ = {5120, 3413};
    start_ = file_;
    exact_ = false;
    state_.size.mode = Mode::original;
    const Placed unframed = settled();
    EXPECT_EQ(unframed.rects.row_text, "Full size, about 5120 \xC3\x97 3413");
    framed_ = true;
    const Placed original = settled();
    EXPECT_EQ(original.rects.row_text, "Full size of the frame, about 5120 \xC3\x97 3413");
    EXPECT_EQ(original.rects.result_text, "Exports at full size, about 5120 \xC3\x97 3413");
    expect_same(original, unframed);
    state_.size.mode = Mode::long_side;
    state_.size.long_side = 2048;
    const Placed reduced = settled();
    EXPECT_EQ(reduced.rects.result_text, "Exports at about 2048 \xC3\x97 1365");
    expect_same(reduced, unframed);
    state_.size.long_side = 9000;
    const Placed bigger = settled();
    EXPECT_EQ(bigger.rects.result_text,
              "Not smaller than the frame: exports at full size, about 5120 \xC3\x97 3413");
    expect_same(bigger, unframed);
    state_.size.mode = Mode::percent;
    state_.size.percent = 50;
    const Placed half = settled();
    EXPECT_EQ(half.rects.result_text, "Exports at about 2560 \xC3\x97 1707");
    // An exact size has no "about".
    exact_ = true;
    EXPECT_EQ(settled().rects.result_text, "Exports at 2560 \xC3\x97 1707");
    state_.size.mode = Mode::original;
    EXPECT_EQ(settled().rects.row_text, "Full size of the frame, 5120 \xC3\x97 3413");
}

TEST_F(ExportSheetHeadless, EveryItemStaysInsideTheSheet) {
    for (const Case& c : cases) {
        file_ = c.file;
        start_ = c.start;
        state_.size.long_side = c.long_side;
        state_.size.percent = c.percent;
        for (const Mode mode : all_modes) {
            state_.size.mode = mode;
            const Placed p = settled();
            const ImVec2 inner_min(p.window_min.x + export_sheet_padding(),
                                   p.window_min.y + export_sheet_padding());
            const ImVec2 inner_max(p.window_max.x - export_sheet_padding(),
                                   p.window_max.y - export_sheet_padding());
            const auto inside = [&](ImVec2 min, ImVec2 max, const char* what) {
                EXPECT_GE(min.x, inner_min.x - 0.01f) << what;
                EXPECT_GE(min.y, inner_min.y - 0.01f) << what;
                EXPECT_LE(max.x, inner_max.x + 0.01f) << what;
                EXPECT_LE(max.y, inner_max.y + 0.01f) << what;
            };
            inside(p.rects.title_min, p.rects.title_max, "title");
            inside(p.rects.label_min, p.rects.label_max, "label");
            inside(p.rects.tabs_min, p.rects.tabs_max, "tabs");
            inside(p.rects.row_min, p.rects.row_max, "row");
            if (mode != Mode::original) {
                inside(p.rects.field_min, p.rects.field_max, "field");
                inside(p.rects.field_min, p.rects.field_max, "field");
                EXPECT_GE(p.rects.field_min.y, p.rects.row_min.y);
                EXPECT_LE(p.rects.field_max.y, p.rects.row_max.y);
            }
            inside(p.rects.result_min, p.rects.result_max, "result");
            inside(p.rects.cancel_min, p.rects.cancel_max, "cancel");
            inside(p.rects.save_min, p.rects.save_max, "save");
            // In order, none over another.
            EXPECT_LE(p.rects.title_max.y, p.rects.label_min.y);
            EXPECT_LE(p.rects.label_max.y, p.rects.tabs_min.y);
            EXPECT_LE(p.rects.tabs_max.y, p.rects.row_min.y);
            EXPECT_LE(p.rects.row_max.y, p.rects.result_min.y);
            EXPECT_LE(p.rects.result_max.y, p.rects.cancel_min.y);
            EXPECT_LE(p.rects.cancel_max.x, p.rects.save_min.x);
        }
    }
}

TEST_F(ExportSheetHeadless, TheHeightIsTheSameAtEveryInterfaceSizeAndTheme) {
    for (const float scale : {0.75f, 1.0f, 1.5f, 2.0f}) {
        for (const gui::Theme theme : {gui::Theme::dark, gui::Theme::light}) {
            apply_style(theme, scale);
            state_.size.mode = Mode::original;
            const Placed first = settled();
            for (const Mode mode : all_modes) {
                state_.size.mode = mode;
                expect_same(settled(), first);
            }
            EXPECT_FLOAT_EQ(first.rects.height, first.bottom - first.content_origin.y);
        }
    }
}

TEST_F(ExportSheetHeadless, HoveringAnythingMovesNothing) {
    for (const Mode mode : all_modes) {
        state_.size.mode = mode;
        mouse_to(ImVec2(-100, -100));
        const Placed first = settled();
        std::vector<ImVec2> places{
            middle(first.rects.tabs_min, first.rects.tabs_max),
            middle(first.rects.row_min, first.rects.row_max),
            middle(first.rects.result_min, first.rects.result_max),
            middle(first.rects.cancel_min, first.rects.cancel_max),
            middle(first.rects.save_min, first.rects.save_max),
            first.rects.title_min,
        };
        if (mode != Mode::original) {
            places.push_back(middle(first.rects.field_min, first.rects.field_max));
        }
        for (const ImVec2 at : places) {
            mouse_to(at);
            for (int i = 0; i < 40; ++i) {  // past any tooltip delay
                expect_same(frame(), first);
            }
        }
    }
}

// --- What the sheet edits ----------------------------------------------------------------------

TEST_F(ExportSheetHeadless, PressingEachSegmentChangesTheMode) {
    state_.size.mode = Mode::original;
    choose(Mode::percent);
    choose(Mode::long_side);
    choose(Mode::original);
    choose(Mode::long_side);
    choose(Mode::percent);
    EXPECT_EQ(take_result(), ExportSheetResult::none);
}

TEST_F(ExportSheetHeadless, ChangingTheModeKeepsTheOtherModesNumber) {
    state_.size = {Mode::long_side, 3000, 40};
    choose(Mode::percent);
    EXPECT_EQ(state_.size.percent, 40);
    EXPECT_EQ(state_.size.long_side, 3000);
    EXPECT_STREQ(state_.text.data(), "40");
    choose(Mode::long_side);
    EXPECT_STREQ(state_.text.data(), "3000");
}

TEST_F(ExportSheetHeadless, TypingIntoTheFieldChangesTheLongSide) {
    state_.size = {Mode::long_side, 2048, 50};
    focus_field();
    type("1600");
    EXPECT_EQ(state_.size.long_side, 1600) << "the typed number replaced the selected one";
    EXPECT_STREQ(state_.text.data(), "1600");
    type("0");
    EXPECT_EQ(state_.size.long_side, 16000) << "no upper limit";
}

TEST_F(ExportSheetHeadless, ALongSideBelowOneBecomesOne) {
    state_.size = {Mode::long_side, 2048, 50};
    focus_field();
    type("0");
    EXPECT_EQ(state_.size.long_side, 1) << "kept in range while typing";
    EXPECT_STREQ(state_.text.data(), "0") << "but typing is never blocked or rewritten under the cursor";
    leave_field();
    EXPECT_STREQ(state_.text.data(), "1") << "the field shows what was kept once it is left";
    EXPECT_EQ(state_.size.long_side, 1);
}

TEST_F(ExportSheetHeadless, APercentIsKeptFromOneTo99) {
    state_.size = {Mode::percent, 2048, 50};
    focus_field();
    type("0");
    EXPECT_EQ(state_.size.percent, 1);
    leave_field();
    EXPECT_STREQ(state_.text.data(), "1");

    focus_field();
    type("150");
    EXPECT_EQ(state_.size.percent, 99);
    leave_field();
    EXPECT_STREQ(state_.text.data(), "99");

    focus_field();
    type("25");
    EXPECT_EQ(state_.size.percent, 25);
    leave_field();
    EXPECT_STREQ(state_.text.data(), "25");
}

TEST_F(ExportSheetHeadless, TheFieldTakesDigitsOnlyAndEmptyKeepsTheNumber) {
    state_.size = {Mode::long_side, 2048, 50};
    focus_field();
    type("1a2-.5");
    EXPECT_EQ(state_.size.long_side, 125);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Backspace, true);
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Backspace, false);
    frame();
    EXPECT_EQ(state_.size.long_side, 12);
    press_key(ImGuiKey_Backspace);
    EXPECT_EQ(state_.size.long_side, 1);
    press_key(ImGuiKey_Backspace);
    EXPECT_STREQ(state_.text.data(), "");
    EXPECT_EQ(state_.size.long_side, 1) << "an empty field changes nothing";
    leave_field();
    EXPECT_STREQ(state_.text.data(), "1");
}

TEST_F(ExportSheetHeadless, TheFieldValueRule) {
    EXPECT_EQ(export_field_value(Mode::long_side, "2048", 7), 2048);
    EXPECT_EQ(export_field_value(Mode::long_side, "0", 7), 1);
    EXPECT_EQ(export_field_value(Mode::long_side, "", 7), 7);
    EXPECT_EQ(export_field_value(Mode::long_side, "99999999", 7), 99999999);
    EXPECT_EQ(export_field_value(Mode::long_side, "999999999", 7), 7) << "too long for the field";
    EXPECT_EQ(export_field_value(Mode::long_side, "12x", 7), 7);
    EXPECT_EQ(export_field_value(Mode::percent, "0", 7), 1);
    EXPECT_EQ(export_field_value(Mode::percent, "100", 7), 99);
    EXPECT_EQ(export_field_value(Mode::percent, "150", 7), 99);
    EXPECT_EQ(export_field_value(Mode::percent, "99", 7), 99);
    EXPECT_EQ(export_field_value(Mode::percent, "1", 7), 1);
}

// --- The buttons -------------------------------------------------------------------------------

TEST_F(ExportSheetHeadless, TheButtonsReportCancelAndSaveAs) {
    for (const Mode mode : all_modes) {
        state_.size.mode = mode;
        Placed p = settled();
        EXPECT_EQ(take_result(), ExportSheetResult::none);
        click(middle(p.rects.cancel_min, p.rects.cancel_max));
        EXPECT_EQ(take_result(), ExportSheetResult::cancel);
        p = settled();
        click(middle(p.rects.save_min, p.rects.save_max));
        EXPECT_EQ(take_result(), ExportSheetResult::save_as);
        EXPECT_EQ(state_.size.mode, mode) << "a button changes no setting";
    }
}

TEST_F(ExportSheetHeadless, EnterIsSaveAsAndEscIsCancel) {
    state_.size.mode = Mode::original;
    settled();
    press_key(ImGuiKey_Enter);
    EXPECT_EQ(take_result(), ExportSheetResult::save_as);
    press_key(ImGuiKey_Escape);
    EXPECT_EQ(take_result(), ExportSheetResult::cancel);
}

TEST_F(ExportSheetHeadless, EnterAndEscWorkInsideTheField) {
    state_.size = {Mode::long_side, 2048, 50};
    focus_field();
    type("800");
    take_result();
    press_key(ImGuiKey_Enter);
    EXPECT_EQ(take_result(), ExportSheetResult::save_as);
    EXPECT_EQ(state_.size.long_side, 800) << "Save As… keeps what was typed";
    focus_field();
    press_key(ImGuiKey_Escape);
    EXPECT_EQ(take_result(), ExportSheetResult::cancel);
}

TEST_F(ExportSheetHeadless, TheButtonsHoldNoActiveItem) {
    // A press on Save As… is not a control being held: it must not look like a drag or a field.
    state_.size.mode = Mode::original;
    const Placed p = settled();
    mouse_to(middle(p.rects.save_min, p.rects.save_max));
    frame();
    mouse(true);
    frame();
    EXPECT_FALSE(control_is_active(false));
    mouse(false);
    frame();
}

TEST_F(ExportSheetHeadless, TheSheetOpensWithTheKeyboardInTheField) {
    state_.size = {Mode::long_side, 2048, 50};
    state_.focus_field = true;
    frame();
    frame();
    EXPECT_NE(ImGui::GetActiveID(), 0u) << "the number field has the keyboard";
    EXPECT_FALSE(state_.focus_field);
}

// --- Explanations ------------------------------------------------------------------------------

TEST_F(ExportSheetHeadless, TheControlsExplainThemselves) {
    state_.size = {Mode::long_side, 2048, 50};
    Placed p = settled();
    mouse_to(middle(p.rects.tabs_min, p.rects.tabs_max));
    frame();
    frame();
    EXPECT_TRUE(hovered_ids_.count(std::string(gui::explain::export_size)));
    mouse_to(middle(p.rects.field_min, p.rects.field_max));
    frame();
    frame();
    EXPECT_TRUE(hovered_ids_.count(std::string(gui::explain::export_value)));
    EXPECT_NE(gui::explanation_for(gui::explain::export_size), nullptr);
    EXPECT_NE(gui::explanation_for(gui::explain::export_value), nullptr);
}

}  // namespace

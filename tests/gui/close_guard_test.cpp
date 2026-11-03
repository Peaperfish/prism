#include "prism/gui/close_guard.hpp"

#include <gtest/gtest.h>

#include "prism/gui/history.hpp"
#include "prism/gui/presets.hpp"

namespace prism::gui {
namespace {

TEST(ExportBaseline, AFreshlyOpenedPhotoIsClean) {
    ExportBaseline b;
    EditSettings s;
    apply_preset(s, default_preset(InputKind::raw));
    b.opened(s);
    EXPECT_FALSE(b.dirty(s));
}

TEST(ExportBaseline, AnEditMakesItDirtyAndUndoingItMakesItCleanAgain) {
    EditSettings start;
    History history(start);
    ExportBaseline b;
    b.opened(history.current());
    EditSettings edited = start;
    edited.exposure_stops = 0.5f;
    history.set(edited);
    EXPECT_TRUE(b.dirty(history.current()));
    history.undo();
    EXPECT_FALSE(b.dirty(history.current()));
    history.redo();
    EXPECT_TRUE(b.dirty(history.current()));
}

TEST(ExportBaseline, ASuccessfulExportMovesTheBaselineToWhatWasExported) {
    EditSettings start;
    ExportBaseline b;
    b.opened(start);
    EditSettings a = start;
    a.exposure_stops = 1.0f;
    b.exported(a);  // the job's copy
    EXPECT_FALSE(b.dirty(a));
    EXPECT_TRUE(b.dirty(start));  // back to the opened state is now an unexported change
    EditSettings later = a;
    later.contrast = 1.2f;  // edited while it was exporting
    EXPECT_TRUE(b.dirty(later));
}

TEST(ExportBaseline, EveryKindOfSettingCounts) {
    EditSettings s;
    ExportBaseline b;
    b.opened(s);
    EditSettings t = s;
    t.curve = CurveKind::filmic;
    EXPECT_TRUE(b.dirty(t));
    t = s;
    t.saturation_on = false;
    EXPECT_TRUE(b.dirty(t));
    t = s;
    t.raw_half_size = true;
    EXPECT_TRUE(b.dirty(t));
}

TEST(CloseGuard, NothingOpenOrNothingToLoseProceedsAtOnce) {
    EXPECT_EQ(close_requested({false, false, false}), CloseStep::proceed);
    EXPECT_EQ(close_requested({true, false, false}), CloseStep::proceed);
}

TEST(CloseGuard, UnexportedEditsAsk) {
    EXPECT_EQ(close_requested({true, true, false}), CloseStep::ask);
}

TEST(CloseGuard, ARunningExportIsWaitedForWhateverElseIsTrue) {
    EXPECT_EQ(close_requested({true, true, true}), CloseStep::wait_for_export);
    EXPECT_EQ(close_requested({true, false, true}), CloseStep::wait_for_export);
    EXPECT_EQ(close_requested({false, false, true}), CloseStep::wait_for_export);
}

TEST(CloseGuard, AfterWaitingTheRequestIsDecidedAgainFromTheNewState) {
    // The export saved what was on screen: clean, so go ahead.
    EXPECT_EQ(close_requested({true, false, false}), CloseStep::proceed);
    // It failed or was cancelled, or more edits came: ask.
    EXPECT_EQ(close_requested({true, true, false}), CloseStep::ask);
}

TEST(CloseGuard, EachButtonDoesWhatItSays) {
    EXPECT_EQ(prompt_choice(PromptChoice::export_first), CloseStep::choose_file);
    EXPECT_EQ(prompt_choice(PromptChoice::discard), CloseStep::proceed);
    EXPECT_EQ(prompt_choice(PromptChoice::cancel), CloseStep::cancel);
}

TEST(CloseGuard, CancellingTheSaveDialogCancelsTheRequest) {
    EXPECT_EQ(save_dialog_closed(false), CloseStep::cancel);
    EXPECT_EQ(save_dialog_closed(true), CloseStep::await_export);
}

TEST(CloseGuard, OnlyASuccessfulExportGoesOn) {
    EXPECT_EQ(export_finished(ExportEnd::saved), CloseStep::proceed);
    EXPECT_EQ(export_finished(ExportEnd::cancelled), CloseStep::cancel);
    EXPECT_EQ(export_finished(ExportEnd::failed), CloseStep::cancel);
}

TEST(CloseGuard, TheTextsFollowWhatIsBeingAskedFor) {
    const PromptText quit = prompt_text(Pending::quit);
    EXPECT_EQ(quit.message, "You have edits that haven't been exported.");
    EXPECT_EQ(quit.export_button, "Export...");
    EXPECT_EQ(quit.discard_button, "Quit Without Exporting");
    EXPECT_EQ(quit.cancel_button, "Cancel");
    EXPECT_EQ(prompt_text(Pending::open_photo).discard_button, "Open Without Exporting");
    EXPECT_NE(waiting_text(Pending::quit), waiting_text(Pending::open_photo));
}

}  // namespace
}  // namespace prism::gui

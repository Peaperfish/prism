#include "prism/gui/close_guard.hpp"

#include <gtest/gtest.h>

#include "prism/gui/history.hpp"
#include "prism/gui/presets.hpp"

namespace prism::gui {
namespace {

TEST(SavedBaseline, AFreshlyOpenedPhotoIsCleanAndHasNoSave) {
    SavedBaseline b;
    EditSettings s;
    apply_preset(s, default_preset(InputKind::raw));
    b.opened(s);
    EXPECT_FALSE(b.dirty(s));
    EXPECT_FALSE(b.last_save().has_value());
}

TEST(SavedBaseline, AnEditMakesItDirtyAndUndoingItMakesItCleanAgain) {
    EditSettings start;
    History history(start);
    SavedBaseline b;
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

TEST(SavedBaseline, ASuccessfulExportMovesTheBaselineToWhatWasExported) {
    EditSettings start;
    SavedBaseline b;
    b.opened(start);
    EditSettings a = start;
    a.exposure_stops = 1.0f;
    b.exported(a, "photo_prism.jpg");  // the job's copy
    EXPECT_FALSE(b.dirty(a));
    EXPECT_TRUE(b.dirty(start));  // back to the opened state is now an unsaved change
    EditSettings later = a;
    later.contrast = 1.2f;  // edited while it was exporting
    EXPECT_TRUE(b.dirty(later));
    EXPECT_EQ(b.last_save(), (LastSave{SaveKind::export_image, "photo_prism.jpg"}));
}

TEST(SavedBaseline, ASavedRecipeMakesItsSettingsClean) {
    EditSettings start;
    SavedBaseline b;
    b.opened(start);
    EditSettings r = start;
    r.saturation = 1.3f;
    b.recipe_saved(r, "warm.prism");
    EXPECT_FALSE(b.dirty(r));
    EXPECT_TRUE(b.dirty(start));
    EXPECT_EQ(b.last_save(), (LastSave{SaveKind::recipe, "warm.prism"}));
}

TEST(SavedBaseline, TheLastExportAndTheLastRecipeAreBothClean) {
    EditSettings start;
    SavedBaseline b;
    b.opened(start);
    EditSettings a = start;
    a.exposure_stops = 1.0f;
    EditSettings r = start;
    r.contrast = 0.9f;
    b.exported(a, "a.jpg");
    b.recipe_saved(r, "r.prism");
    EXPECT_FALSE(b.dirty(a));
    EXPECT_FALSE(b.dirty(r));
    EXPECT_TRUE(b.dirty(start));
    EXPECT_EQ(b.last_save()->kind, SaveKind::recipe);
    // A second export replaces the first one; the recipe stays clean.
    EditSettings a2 = a;
    a2.exposure_stops = 2.0f;
    b.exported(a2, "a2.jpg");
    EXPECT_TRUE(b.dirty(a));
    EXPECT_FALSE(b.dirty(a2));
    EXPECT_FALSE(b.dirty(r));
}

TEST(SavedBaseline, AnExportFinishingAfterARecipeSaveKeepsBothClean) {
    // The race: an export of A is still running while a recipe of B is saved, and finishes
    // afterwards.
    EditSettings o;
    EditSettings a = o;
    a.exposure_stops = 0.7f;
    EditSettings b_settings = o;
    b_settings.saturation = 0.8f;
    EditSettings other = o;
    other.curve = CurveKind::filmic;
    SavedBaseline b;
    b.opened(o);
    b.recipe_saved(b_settings, "b.prism");
    b.exported(a, "a.jpg");  // late
    EXPECT_FALSE(b.dirty(a));
    EXPECT_FALSE(b.dirty(b_settings));
    EXPECT_TRUE(b.dirty(o));
    EXPECT_TRUE(b.dirty(other));
    EXPECT_EQ(b.last_save(), (LastSave{SaveKind::export_image, "a.jpg"}));
}

TEST(SavedBaseline, OpeningAgainForgetsEverySave) {
    EditSettings start;
    SavedBaseline b;
    b.opened(start);
    EditSettings a = start;
    a.exposure_stops = 1.0f;
    b.exported(a, "a.jpg");
    b.recipe_saved(a, "a.prism");
    EditSettings next = start;
    next.contrast = 1.1f;
    b.opened(next);
    EXPECT_FALSE(b.dirty(next));
    EXPECT_TRUE(b.dirty(a));
    EXPECT_FALSE(b.last_save().has_value());
}

TEST(SavedBaseline, EveryKindOfSettingCounts) {
    EditSettings s;
    SavedBaseline b;
    b.opened(s);
    b.recipe_saved(s, "s.prism");
    EditSettings t = s;
    t.curve = CurveKind::filmic;
    EXPECT_TRUE(b.dirty(t));
    t = s;
    t.saturation_on = false;
    EXPECT_TRUE(b.dirty(t));
    t = s;
    t.raw_half_size = true;  // not in a recipe, but still an edit of the photo
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
    EXPECT_EQ(prompt_choice(PromptChoice::save_recipe), CloseStep::name_recipe);
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

TEST(CloseGuard, SaveRecipeGoesFromTheSheetToTheDialog) {
    EXPECT_EQ(recipe_sheet_closed(true), CloseStep::choose_recipe_file);
}

TEST(CloseGuard, CancellingTheRecipeSheetCancelsTheRequest) {
    EXPECT_EQ(recipe_sheet_closed(false), CloseStep::cancel);
}

TEST(CloseGuard, OnlyAWrittenRecipeGoesOn) {
    EXPECT_EQ(recipe_save_finished(RecipeEnd::saved), CloseStep::proceed);
    EXPECT_EQ(recipe_save_finished(RecipeEnd::cancelled), CloseStep::cancel);
    EXPECT_EQ(recipe_save_finished(RecipeEnd::failed), CloseStep::cancel);
}

TEST(CloseGuard, AFailedRecipeWriteLeavesTheBaselineAlone) {
    // The window calls recipe_saved only after a successful write; a failure goes straight to
    // recipe_save_finished(failed), so the edits are still unsaved when the prompt comes back.
    EditSettings start;
    SavedBaseline b;
    b.opened(start);
    EditSettings edited = start;
    edited.exposure_stops = 0.4f;
    EXPECT_EQ(recipe_save_finished(RecipeEnd::failed), CloseStep::cancel);
    EXPECT_TRUE(b.dirty(edited));
    EXPECT_EQ(close_requested({true, b.dirty(edited), false}), CloseStep::ask);
}

TEST(CloseGuard, TheTextsFollowWhatIsBeingAskedFor) {
    const PromptText quit = prompt_text(Pending::quit);
    EXPECT_EQ(quit.message, "You have edits that haven't been exported or saved as a recipe.");
    EXPECT_EQ(quit.export_button, "Export...");
    EXPECT_EQ(quit.recipe_button, "Save Recipe...");
    EXPECT_EQ(quit.cancel_button, "Cancel");
    EXPECT_NE(waiting_text(Pending::quit), waiting_text(Pending::open_photo));
}

TEST(CloseGuard, TheDiscardButtonSaysSavingNotExporting) {
    EXPECT_EQ(prompt_text(Pending::quit).discard_button, "Quit Without Saving");
    EXPECT_EQ(prompt_text(Pending::open_photo).discard_button, "Open Without Saving");
}

TEST(CloseGuard, TheDetailLineNamesTheLastSave) {
    EXPECT_EQ(prompt_detail(std::nullopt), "");
    EXPECT_EQ(prompt_detail(LastSave{SaveKind::export_image, "IMG_1_prism.jpg"}),
              "Last saved: exported as IMG_1_prism.jpg");
    EXPECT_EQ(prompt_detail(LastSave{SaveKind::recipe, "warm.prism"}),
              "Last saved: recipe warm.prism");
}

TEST(CloseGuard, ButtonsOnMacPutDiscardAloneOnTheLeftAndExportLast) {
    const PromptButtons b = prompt_buttons(true);
    EXPECT_EQ(b.order[0], PromptChoice::discard);
    EXPECT_EQ(b.order[1], PromptChoice::cancel);
    EXPECT_EQ(b.order[2], PromptChoice::save_recipe);
    EXPECT_EQ(b.order[3], PromptChoice::export_first);
    EXPECT_EQ(b.gap_after, 1);
}

TEST(CloseGuard, ButtonsOnWindowsAndLinuxPutExportFirst) {
    const PromptButtons b = prompt_buttons(false);
    EXPECT_EQ(b.order[0], PromptChoice::export_first);
    EXPECT_EQ(b.order[1], PromptChoice::save_recipe);
    EXPECT_EQ(b.order[2], PromptChoice::discard);
    EXPECT_EQ(b.order[3], PromptChoice::cancel);
    EXPECT_EQ(b.gap_after, 0);
}

}  // namespace
}  // namespace prism::gui

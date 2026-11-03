#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "prism/gui/settings.hpp"

namespace prism::gui {

// Asking before edits are thrown away (#32). All the decisions are here, as pure functions, so
// the window only has to show a modal and report what happened.
//
// "Edits that haven't been saved" means the settings differ from those of the last successful
// export and from those of the last recipe saved (#18), or, if neither has happened yet, from
// those at open. So undoing back to a saved state is clean again, and a photo that was only
// opened is clean.

// What the user asked for that would leave the photo.
enum class Pending { quit, open_photo };

// The two ways of saving an edit: an exported picture, or a recipe of its settings.
enum class SaveKind { export_image, recipe };

// The most recent successful save: its kind and the file's name (without folders).
struct LastSave {
    SaveKind kind = SaveKind::export_image;
    std::string file;
    bool operator==(const LastSave&) const = default;
};

// The settings the photo was last known to be saved with, by export and by recipe.
class SavedBaseline {
public:
    // A photo was opened: its starting settings are the baseline, and nothing is saved yet.
    void opened(const EditSettings& settings);
    // An export of a job with `settings` finished successfully (use the job's copy, not what is
    // on screen by then). `file` is the name it was saved as.
    void exported(const EditSettings& settings, std::string file);
    // A recipe of `settings` was written to `file`.
    void recipe_saved(const EditSettings& settings, std::string file);

    // Clean if `current` equals the last export or the last recipe save, or, while nothing has
    // been saved, the settings at open. Each kind keeps its own state, so an export that
    // finishes after a recipe was saved leaves both clean.
    [[nodiscard]] bool dirty(const EditSettings& current) const;
    // The save that happened last, of either kind; nothing since the photo was opened.
    [[nodiscard]] const std::optional<LastSave>& last_save() const noexcept { return last_; }

private:
    EditSettings opened_;
    std::optional<EditSettings> exported_;
    std::optional<EditSettings> recipe_;
    std::optional<LastSave> last_;
};

struct CloseState {
    bool has_file = false;
    bool dirty = false;           // unsaved edits (SavedBaseline::dirty)
    bool export_running = false;  // an export is in progress right now
};

// What the window does next.
enum class CloseStep {
    proceed,          // go ahead: quit, or open the other photo
    ask,              // show the prompt
    wait_for_export,  // show "waiting for the export", and ask again when it has finished
    choose_file,      // the prompt's Export...: open the save dialog
    await_export,     // the export has started: wait for its result
    cancel,           // drop the request; nothing quits or opens
};

enum class PromptChoice { export_first, discard, cancel };  // Export..., Quit/Open Without, Cancel

enum class ExportEnd { saved, cancelled, failed };

// A request to quit or to open another photo arrived.
//  - no photo, or nothing unexported: proceed (a running export of a clean photo still finishes
//    first, so a quit never kills a write half-way);
//  - an export is running: wait for it, then ask again with the new state;
//  - otherwise: ask.
[[nodiscard]] CloseStep close_requested(const CloseState& state) noexcept;

// What a prompt button does.
[[nodiscard]] CloseStep prompt_choice(PromptChoice choice) noexcept;

// The save dialog opened by the prompt's Export... closed: with a file chosen the export starts
// (await_export); cancelling the dialog cancels the whole request.
[[nodiscard]] CloseStep save_dialog_closed(bool file_chosen) noexcept;

// The export that the prompt started (or that was awaited) ended. Only a successful export goes
// on; cancelling or a failure cancels the request.
[[nodiscard]] CloseStep export_finished(ExportEnd end) noexcept;

// The text of the prompt for `pending`.
struct PromptText {
    std::string_view message;
    std::string_view export_button;
    std::string_view discard_button;
    std::string_view cancel_button;
};
[[nodiscard]] PromptText prompt_text(Pending pending) noexcept;

// The text of the waiting dialog and its button.
[[nodiscard]] std::string_view waiting_text(Pending pending) noexcept;

}  // namespace prism::gui

#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>

#include "prism/gui/file_types.hpp"
#include "prism/gui/main_thread_queue.hpp"

namespace prism::gui_app {

// The answer of an Open or Save dialog, as it arrives on the main thread.
struct DialogResult {
    enum class Kind { open, save };
    Kind kind = Kind::open;
    std::optional<std::filesystem::path> path;  // nothing: cancelled, or the dialog failed
    std::string error;                          // why it failed (SDL's message), if it did
    gui::ExportFormat format = gui::ExportFormat::jpeg;  // save only: the format chosen
};

using DialogQueue = gui::MainThreadQueue<DialogResult>;

// Show the system's Open or Save dialog and return at once. SDL's dialogs are asynchronous and may
// answer on another thread, so the answer is pushed to `queue` (with an SDL event to wake the
// main loop) for the main thread to take. `queue` must outlive the dialog.
void show_open_dialog(SDL_Window* window, DialogQueue& queue,
                      const std::filesystem::path& start_folder);
// Offers PNG and JPEG, starting at `suggested` (a full file name) in `format`.
void show_save_dialog(SDL_Window* window, DialogQueue& queue,
                      const std::filesystem::path& suggested, gui::ExportFormat format);

}  // namespace prism::gui_app

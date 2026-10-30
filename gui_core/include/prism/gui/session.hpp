#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

#include "prism/gui/history.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"

namespace prism::gui {

enum class ErrorKind {
    unsupported_format,  // not a PNG, JPEG or RAW name (open), or not .png/.jpg/.jpeg (export)
    not_found,           // the file does not exist
    unreadable,          // the decoder rejected it: damaged, or not what its name says
    no_raw_support,      // a RAW file, but this build has no LibRaw
    out_of_memory,
    invalid_settings,  // see validate()
    save_failed,       // the output could not be written
    cancelled,         // export only: the stop token was triggered (nothing was written)
};

// Something that went wrong, for the UI's banner: `message` is one sentence for the user,
// `details` the library's own text (for "Details" and bug reports), possibly empty.
struct Error {
    ErrorKind kind;
    std::string message;
    std::string details;
};

// Nothing on success, or what went wrong. (C++23's std::expected would say this more directly.)
using MaybeError = std::optional<Error>;

struct SessionOptions {
    int preview_long_side = 2560;  // the preview base's longer side, in pixels
    int threads = 0;               // as for the pipeline: 0 = one per hardware thread
};

// Everything an export needs, copied out of a Session: export_image() can then run on another
// thread while the user keeps editing, without sharing anything with the Session.
struct ExportJob {
    std::filesystem::path source;
    InputKind kind = InputKind::image;
    EditSettings settings;
    int threads = 0;
};

// The photo being edited: where it came from, the small image the preview is rendered from, and
// the settings with their undo history. Not thread-safe; it belongs to the UI thread. The full
// image is not kept: after opening, only the preview base stays in memory (~52 MB at 2560 px
// instead of ~730 MB for 60 MP), and export decodes the file again at full size.
class Session {
public:
    explicit Session(SessionOptions options = {});

    // Opens a PNG, JPEG or camera RAW file. RAW files are decoded at half size, which is still
    // larger than the preview and twice as fast. The result is shrunk to the preview base, and the
    // settings start over at the default preset for the kind of file (with no undo steps). On
    // failure nothing changes: the previous file, if any, stays open.
    [[nodiscard]] MaybeError open_file(const std::filesystem::path& path);

    [[nodiscard]] bool has_file() const noexcept { return preview_base_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] InputKind kind() const noexcept { return kind_; }
    // The size the file was decoded at when opened (half the sensor size for RAW).
    [[nodiscard]] int decoded_width() const noexcept { return decoded_width_; }
    [[nodiscard]] int decoded_height() const noexcept { return decoded_height_; }

    // The image previews are rendered from: never changed once made, so it can be shared with the
    // preview thread. A new one replaces it when another file is opened or the RAW camera matrix
    // changes. Null before the first successful open_file().
    [[nodiscard]] std::shared_ptr<const Image> preview_base() const noexcept {
        return preview_base_;
    }

    [[nodiscard]] const EditSettings& settings() const noexcept { return history_.current(); }
    [[nodiscard]] const History& history() const noexcept { return history_; }

    // Changes the settings through the undo history (see History::set). Invalid settings are
    // refused with ErrorKind::invalid_settings and change nothing. If the RAW camera matrix
    // changed, the file is decoded again for a new preview base, here on the calling thread (about
    // a second for a 60 MP RAW file); if that fails, the settings stay changed but the preview base
    // does not.
    [[nodiscard]] MaybeError set_settings(const EditSettings& s);
    void begin_gesture() { history_.begin_gesture(); }
    void end_gesture() { history_.end_gesture(); }
    // Undo and redo, which may also re-decode, as set_settings does. Use history().can_undo() and
    // can_redo() to see whether there is anything to do.
    [[nodiscard]] MaybeError undo();
    [[nodiscard]] MaybeError redo();

    // A copy of what export_image() needs for the current file and settings.
    [[nodiscard]] ExportJob export_job() const;

private:
    // Decodes the file again if the preview base was made with another RAW camera matrix.
    MaybeError update_preview_base();

    SessionOptions options_;
    std::filesystem::path path_;
    InputKind kind_ = InputKind::image;
    int decoded_width_ = 0;
    int decoded_height_ = 0;
    std::shared_ptr<const Image> preview_base_;
    std::optional<Matrix3> preview_camera_matrix_;  // what preview_base_ was decoded with
    History history_;
};

// Renders `job` at full resolution and saves it as PNG or JPEG (by the extension of `output`;
// the library's fixed JPEG quality), on the calling thread. The source file is decoded again:
// RAW at full size unless job.settings.raw_half_size, with its camera matrix. `stop` is checked
// between decoding, each stage and saving; once it is triggered the export returns
// ErrorKind::cancelled and writes nothing. on_stage reports each pipeline stage, for progress.
// Refuses to overwrite the source file itself.
[[nodiscard]] MaybeError export_image(const ExportJob& job, const std::filesystem::path& output,
                                      std::stop_token stop = {},
                                      const StageCallback& on_stage = {});

}  // namespace prism::gui

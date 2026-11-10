#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

#include "prism/gui/export_size.hpp"
#include "prism/gui/history.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/photo_info.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"

namespace prism::gui {

enum class ErrorKind {
    unsupported_format,  // not a PNG, JPEG, HEIC or RAW name (open), or HEIC off macOS, or not
                         // .png/.jpg/.jpeg (export)
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

// The light the camera balanced for when it took the photo (PhotoInfo::as_shot_white_xy), as a
// chromaticity. None for anything but a RAW file, and for a RAW file without a usable one.
[[nodiscard]] std::optional<Chromaticity> as_shot_white_of(const PhotoInfo& info) noexcept;

// Everything an export needs, copied out of a Session: export_image() can then run on another
// thread while the user keeps editing, without sharing anything with the Session.
struct ExportJob {
    std::filesystem::path source;
    InputKind kind = InputKind::image;
    EditSettings settings;
    int threads = 0;
    // Reduce the decoded image so its longer side is at most this many pixels, right after
    // decoding and before any editing (area averaging on linear light, as downscale does). Nothing
    // means the full decoded size; an image already this small is left as it is. With RAW
    // half-size on, this applies to the half-size decode.
    std::optional<int> long_side;
};

// The photo being edited: where it came from, what is known about the shot, the small image the
// preview is rendered from, and the settings with their undo history. Not thread-safe; it belongs
// to the UI thread. The full image is not kept: after opening, only the preview base stays in
// memory (~52 MB at 2560 px instead of ~730 MB for 60 MP), and export decodes the file again at
// full size.
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
    // The size of the file itself: what an export would start from. For a RAW file it comes from
    // the file's metadata; if that can't be read, or the decode may differ from it, it is an
    // estimate and file_size_exact() is false. For a PNG or JPEG it is the decoded size.
    [[nodiscard]] int file_width() const noexcept { return file_width_; }
    [[nodiscard]] int file_height() const noexcept { return file_height_; }
    [[nodiscard]] bool file_size_exact() const noexcept { return file_size_exact_; }

    // What the open photo says about how it was taken (see PhotoInfo): empty before the first
    // successful open_file() and for a PNG. It belongs to the open photo, so it is replaced when
    // another file is opened and left alone by an open that fails, like the rest of the session.
    [[nodiscard]] const PhotoInfo& photo_info() const noexcept { return photo_info_; }
    // The open photo's as-shot light (as_shot_white_of its PhotoInfo): what to_process_settings
    // measures a chosen white balance against.
    [[nodiscard]] std::optional<Chromaticity> as_shot_white() const noexcept {
        return as_shot_white_of(photo_info_);
    }

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
    // Applies a recipe to the current settings (apply_recipe in recipe_settings.hpp) through
    // set_settings: one undo step, or none if it changes nothing. A recipe never changes the RAW
    // options, so this never decodes the file again.
    [[nodiscard]] MaybeError apply_recipe(const Recipe& recipe);
    // Takes every Dodge & burn stroke off the photo (the Clear button): one undo step, or none if
    // there are no strokes. Undo brings them back (the strokes are shared, not copied).
    [[nodiscard]] MaybeError clear_strokes();
    void begin_gesture() { history_.begin_gesture(); }
    void end_gesture() { history_.end_gesture(); }
    // Undo and redo, which may also re-decode, as set_settings does. Use history().can_undo() and
    // can_redo() to see whether there is anything to do.
    [[nodiscard]] MaybeError undo();
    [[nodiscard]] MaybeError redo();

    // The size exports are made at. Not part of the settings (so not undoable): it is how the
    // result is delivered, not how it looks. Starts as the file's own size and is kept when
    // another file is opened.
    void set_export_size(const ExportSize& size) { export_size_ = size; }
    [[nodiscard]] const ExportSize& export_size() const noexcept { return export_size_; }
    // The longer side the current export size gives for the open file, or nothing for its own size
    // (see export_long_side; a percentage is of file_width() x file_height()).
    [[nodiscard]] std::optional<int> export_long_side() const {
        return prism::gui::export_long_side(export_size_, file_width_, file_height_);
    }

    // The size an export decodes at before any reduction: the file's own size, or for a RAW file
    // with raw_half_size on, the half-size decode (the size the file was opened at). Zero before
    // a file is open.
    [[nodiscard]] PixelSize export_start_size() const {
        if (kind_ == InputKind::raw && settings().raw_half_size) {
            return {decoded_width_, decoded_height_};
        }
        return {file_width_, file_height_};
    }
    // What the current export size will write, for the open file and settings (see plan_export).
    [[nodiscard]] ExportPlan export_plan() const {
        return plan_export(export_size_, {file_width_, file_height_}, export_start_size());
    }

    // A copy of what export_image() needs for the current file, settings and export size.
    [[nodiscard]] ExportJob export_job() const;

private:
    // Decodes the file again if the preview base was made with another RAW camera matrix.
    MaybeError update_preview_base();

    SessionOptions options_;
    std::filesystem::path path_;
    InputKind kind_ = InputKind::image;
    int decoded_width_ = 0;
    int decoded_height_ = 0;
    int file_width_ = 0;
    int file_height_ = 0;
    bool file_size_exact_ = true;
    PhotoInfo photo_info_;
    std::shared_ptr<const Image> preview_base_;
    std::optional<Matrix3> preview_camera_matrix_;  // what preview_base_ was decoded with
    History history_;
    ExportSize export_size_;
};

// Renders `job` at full resolution and saves it as PNG or JPEG (by the extension of `output`;
// the library's fixed JPEG quality), on the calling thread. The source file is decoded again:
// RAW at full size unless job.settings.raw_half_size, with its camera matrix, then reduced if
// job.long_side asks for it. A chosen white balance is measured against the as-shot light that
// decode reads from the file, the same one the session read when it opened it. `stop` is checked between decoding, reducing, each stage and saving; once it is triggered the export returns
// ErrorKind::cancelled and writes nothing. on_stage reports each pipeline stage, for progress.
// Refuses to overwrite the source file itself.
[[nodiscard]] MaybeError export_image(const ExportJob& job, const std::filesystem::path& output,
                                      std::stop_token stop = {},
                                      const StageCallback& on_stage = {});

}  // namespace prism::gui

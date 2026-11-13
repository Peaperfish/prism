#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <variant>

#include "prism/frame.hpp"
#include "prism/gui/detail_base.hpp"
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

// The easel's aspect lock (prism/frame.hpp's Frame::aspect and portrait) as framing last left it.
// A frame that is the whole photo is stored in the settings as none (normalised_frame), which
// would drop a lock chosen on an uncropped photo (Free, or Original turned), so the session keeps
// it beside the settings: not part of undo or of a recipe, and back to Original when another photo
// is opened.
struct FrameLock {
    Aspect aspect = Aspect::original;
    bool portrait = false;

    bool operator==(const FrameLock&) const = default;
};

class DetailReader;
class ReadEnd;

// How the detail base is read: decode_for_export's signature (the default). Tests pass their own.
using DetailReadFunction = std::function<std::variant<Photo, Error>(
    const std::filesystem::path& source, InputKind kind, const EditSettings& settings, int threads,
    std::stop_token stop)>;

struct SessionOptions {
    int preview_long_side = 2560;  // the preview base's longer side, in pixels
    int threads = 0;               // as for the pipeline: 0 = one per hardware thread
    // Keep a detail base (Session::detail_base): read the full-size photo in the background after
    // opening. Off until the app shows real pixels, which also polls for it and exports through
    // begin_export.
    bool detail_base = false;
    // The physical memory the detail base is measured against (plan_detail_base); nothing means
    // the computer's own (physical_memory_bytes).
    std::optional<std::uint64_t> physical_memory;
    // How the detail base is read; empty means decode_for_export.
    DetailReadFunction read_detail_base;
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
    // Reduce the framed image so its longer side is at most this many pixels, right after
    // decoding and framing and before any editing (area averaging on linear light, as downscale
    // does). Nothing means the full framed size; an image already this small is left as it is.
    // With RAW half-size on, this applies to the half-size decode, framed.
    std::optional<int> long_side;
    // If set, export_image waits for it before decoding (Session::begin_export): the session's
    // background reads of the full-size photo, stopped, have ended and freed what they read, so
    // the photo is never decoded at full size twice at once. Stopping the export ends the wait.
    std::shared_ptr<const ReadEnd> read_ended = nullptr;
};

// The photo being edited: where it came from, what is known about the shot, the small image the
// preview is rendered from, and the settings with their undo history. Not thread-safe; it belongs
// to the UI thread. After opening, only the preview base stays in memory (~52 MB at 2560 px instead
// of ~730 MB for 60 MP), and export decodes the file again at full size; with
// SessionOptions::detail_base the full-size photo is also read in the background (detail_base()).
class Session {
public:
    explicit Session(SessionOptions options = {});
    // Stops a background read of the detail base and waits for it to end.
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Opens a PNG, JPEG or camera RAW file. RAW files are decoded at half size, which is still
    // larger than the preview and twice as fast. The result is shrunk to the preview base, or kept
    // as the preview base if it already fits, and the settings start over at the default preset
    // for the kind of file (with no undo steps). With SessionOptions::detail_base, the detail base
    // is planned and its read started. On failure nothing changes: the previous file, if any,
    // stays open.
    [[nodiscard]] MaybeError open_file(const std::filesystem::path& path);

    [[nodiscard]] bool has_file() const noexcept { return whole_base_ != nullptr; }
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
    // preview thread. With a frame (EditSettings::frame) it is the framed base, apply_frame of the
    // whole base with the fitted frame (preview_framing), and otherwise the whole base itself. So
    // every stage, the Decode view, Sample, Pick neutral and the clip report see only the framed
    // picture. A new one replaces it when another file is opened, the RAW camera matrix changes or
    // the frame changes (it is built here, on the calling thread). Null before the first successful
    // open_file().
    [[nodiscard]] std::shared_ptr<const Image> preview_base() const noexcept {
        return framed_base_;
    }
    // The whole photo at the preview's size, before any frame (for the framing view, #58). The
    // same object as preview_base() when there is no frame.
    [[nodiscard]] std::shared_ptr<const Image> whole_base() const noexcept { return whole_base_; }
    // Where preview_base() sits on the photo: the fitted frame on whole_base()'s size, at scale 1,
    // or nothing when preview_base() is the whole photo. What PreviewOptions::framing, sample_trace
    // and compute_clip_report take with preview_base().
    [[nodiscard]] const std::optional<PictureFrame>& preview_framing() const noexcept {
        return framing_;
    }

    [[nodiscard]] const EditSettings& settings() const noexcept { return history_.current(); }
    [[nodiscard]] const History& history() const noexcept { return history_; }
    // history().generation(): changes whenever settings() may have, including when another file
    // is opened. What a render made for the current settings is keyed by (DetailKey::settings).
    [[nodiscard]] std::uint64_t settings_generation() const noexcept {
        return history_.generation();
    }
    // The easel's aspect lock (FrameLock): set by framing, Original for a newly opened photo.
    [[nodiscard]] const FrameLock& frame_lock() const noexcept { return frame_lock_; }
    void set_frame_lock(const FrameLock& lock) noexcept { frame_lock_ = lock; }

    // Changes the settings through the undo history (see History::set). Invalid settings are
    // refused with ErrorKind::invalid_settings and change nothing. A frame that is the whole photo
    // is stored as none (normalised_frame). If the RAW camera matrix changed, the file is decoded
    // again for a new preview base, here on the calling thread (about a second for a 60 MP RAW
    // file); if that fails, the settings stay changed but the preview base does not. If the frame
    // changed, the framed base is built again, also here, before the settings change: if it can't
    // be made, nothing changes and the error says why.
    [[nodiscard]] MaybeError set_settings(const EditSettings& s);
    // Applies a recipe to the current settings (apply_recipe in recipe_settings.hpp) through
    // set_settings: one undo step, or none if it changes nothing. A recipe never changes the RAW
    // options or the frame, so this never decodes the file again or frames it anew.
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
    // (see export_long_side; a percentage is of framed_file_size(), the file's size framed).
    [[nodiscard]] std::optional<int> export_long_side() const {
        const PixelSize file = framed_file_size();
        return prism::gui::export_long_side(export_size_, file.width, file.height);
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
    // The size of the framed picture on the file's own size: plan_export_frame(frame, file_width(),
    // file_height()).size, the file's size without a frame. What Original, Long side and Percent
    // measure; an estimate whenever file_size_exact() is false. Zero before a file is open.
    [[nodiscard]] PixelSize framed_file_size() const;
    // framed_file_size()'s longer side: what one pixel of the preview base is compared with (the
    // zoom's and Sample's "file pixels across"), since the base is the framed picture too.
    [[nodiscard]] int framed_file_long_side() const {
        const PixelSize size = framed_file_size();
        return std::max(size.width, size.height);
    }
    // The size an export has after framing and before any reduction: the frame on
    // export_start_size(), which is what export_image frames. export_start_size() without a frame.
    [[nodiscard]] PixelSize framed_start_size() const;
    // What the current export size will write, for the open file and settings (see plan_export):
    // the framed picture, reduced if the export size asks for it.
    [[nodiscard]] ExportPlan export_plan() const {
        return plan_export(export_size_, framed_file_size(), framed_start_size());
    }

    // A copy of what export_image() needs for the current file, settings and export size.
    [[nodiscard]] ExportJob export_job() const;

    // --- The detail base (#37), with SessionOptions::detail_base ---------------------------------
    //
    // The full-size photo real-pixel zoom renders from: decode_for_export's photo for the current
    // file and RAW options, the same pixels an export starts from. After a file is opened, and
    // whenever what decode_for_export would read changes (another file, raw_half_size or
    // raw_camera_matrix on a RAW file), the session plans it (plan_detail_base) and, if it must be
    // read, reads it on a thread of its own. Other settings never restart the read. A result for a
    // file or RAW options no longer current is thrown away when it arrives.

    // Where the detail base stands (off without SessionOptions::detail_base or a file).
    [[nodiscard]] DetailBaseState detail_base_state() const noexcept;
    // The full-size photo to render from (render_region), for the open file and current RAW
    // options: the read photo once ready, or with nothing to read (DetailBaseState::not_needed)
    // the decode the preview base was made from, which is the preview base's own pixels (no copy).
    // Null otherwise. Never changed once made: a render thread can keep it while the session
    // drops or replaces it.
    [[nodiscard]] std::shared_ptr<const Photo> detail_base() const noexcept;
    // Why the read failed (DetailBaseState::failed), or nothing.
    [[nodiscard]] const std::optional<Error>& detail_base_error() const noexcept {
        return detail_error_;
    }
    // UI thread, once per frame: takes a finished read's result, if any, keeping it if it is for
    // the current file and RAW options. True if detail_base_state() changed.
    bool poll_detail_base();
    // Waits until the running read has a result or has ended, for at most `timeout` (false if
    // not by then), then poll_detail_base(). For tests and headless use; the UI just polls.
    bool wait_for_detail_base(std::chrono::milliseconds timeout);
    // As wait_for_detail_base, but leaves a result where it is, for poll_detail_base (or for
    // whatever replaces the read first) to take. For tests.
    bool wait_for_detail_read(std::chrono::milliseconds timeout);

    // export_job() for an export about to start, which must end with end_export(). A read of the
    // detail base still running is stopped, and the job's read_ended makes export_image wait for
    // it to end before decoding: the full-size photo is never decoded twice at once. A ready
    // detail base stays (export still decodes the file itself). No read starts until end_export().
    [[nodiscard]] ExportJob begin_export();
    // The export from begin_export() has finished (or failed, or was cancelled): a read that was
    // stopped for it, or is now wanted, starts again.
    void end_export();

private:
    // Decodes the file again if the preview base was made with another RAW camera matrix, then
    // frames it (update_framed_base).
    MaybeError update_preview_base();
    // Makes the framed base for `frame` on the whole base, unless it is already that. On failure
    // (out of memory) nothing changes, and the base keeps the framing it was made with.
    MaybeError update_framed_base(const std::optional<Frame>& frame);
    // Replaces whole_base_ with `decoded` shrunk to the preview's size, or with `decoded` itself
    // (kept as preview_photo_, no copy) when it already fits. Throws std::bad_alloc.
    void set_whole_base(Photo&& decoded);

    // What decode_for_export reads for a file: the RAW options only matter for a RAW file.
    struct DetailKey {
        std::filesystem::path path;
        bool raw_half_size = false;
        std::optional<Matrix3> camera_matrix;

        bool operator==(const DetailKey&) const = default;
    };
    // Plans the detail base again, and stops, drops and starts its read, if what
    // decode_for_export would read changed (or always, after opening a file).
    void update_detail_base(bool opened);
    // Stops the read and lets go of the detail base and of any result waiting, before a decode
    // that would otherwise hold memory beside them; the next update_detail_base plans it anew.
    void drop_detail_base();
    // Starts the read if the plan is to read and nothing is running, ready or failed, unless an
    // export is running.
    void start_detail_read();

    SessionOptions options_;
    std::filesystem::path path_;
    InputKind kind_ = InputKind::image;
    int decoded_width_ = 0;
    int decoded_height_ = 0;
    int file_width_ = 0;
    int file_height_ = 0;
    bool file_size_exact_ = true;
    PhotoInfo photo_info_;
    std::shared_ptr<const Image> whole_base_;
    // The decode whole_base_ points into, when it fits the preview without shrinking; else null.
    std::shared_ptr<const Photo> preview_photo_;
    std::optional<Matrix3> preview_camera_matrix_;  // what whole_base_ was decoded with
    std::shared_ptr<const Image> framed_base_;      // whole_base_ framed by framing_
    std::shared_ptr<const Image> framed_from_;      // the whole base framed_base_ was made from
    std::optional<PictureFrame> framing_;
    History history_;
    FrameLock frame_lock_;
    ExportSize export_size_;

    std::optional<DetailKey> detail_key_;  // what detail_plan_ and the read are for
    DetailPlan detail_plan_ = DetailPlan::not_needed;
    std::shared_ptr<const Photo> detail_base_;  // the read's photo, once ready
    std::optional<Error> detail_error_;         // the read's failure
    bool detail_reading_ = false;               // a read for detail_key_ is running
    bool exporting_ = false;                    // between begin_export and end_export
    std::uint64_t detail_generation_ = 0;       // the running read's; results of others are stale
    // Created on first use (with SessionOptions::detail_base). Declared last: destroyed (its reads stopped and joined) first.
    std::unique_ptr<DetailReader> detail_reader_;
};

// Renders `job` at full resolution and saves it as PNG or JPEG (by the extension of `output`; the
// library's fixed JPEG quality), on the calling thread. The source file is decoded again: RAW at
// full size unless job.settings.raw_half_size, with its camera matrix, then framed and reduced if
// job.long_side asks for it (frame_and_reduce), and the stages run on the result, with the strokes
// on PixelGrid::whole without a frame (as in 0.5.0) and on the framed grid with one. A chosen white
// balance is measured against the as-shot light that decode reads from the file, the same one the
// session read when it opened it. `stop` is checked between decoding, reducing, each stage and
// saving; once it is triggered the export returns ErrorKind::cancelled and writes nothing. on_stage
// reports each pipeline stage, for progress. Refuses to overwrite the source file itself. With
// job.read_ended, it first waits for the session's background read to end (a stop during that wait
// also returns ErrorKind::cancelled).
[[nodiscard]] MaybeError export_image(const ExportJob& job, const std::filesystem::path& output,
                                      std::stop_token stop = {},
                                      const StageCallback& on_stage = {});

// After checking the output and the settings, export_image runs decode_for_export, then
// finish_export, then save_image (the encode).

// The decode an export starts from, exactly as export_image makes it: `source` checked as open_file
// checks it, then read at full size, or for a RAW file at half size when settings.raw_half_size,
// with settings.raw_camera_matrix, upright (orientation applied). Unframed and unedited: the
// photo's info holds the as-shot light the stages measure white balance against. Fails with the
// same Error as export_image would for the same file (missing, unreadable, no RAW support, out of
// memory), or ErrorKind::cancelled if `stop` was triggered before decoding. `kind` is the file's
// kind, as in ExportJob.
[[nodiscard]] std::variant<Photo, Error> decode_for_export(const std::filesystem::path& source,
                                                           InputKind kind,
                                                           const EditSettings& settings,
                                                           int threads,
                                                           std::stop_token stop = {});

// What export_image does with `decoded` before encoding it: frame and reduce to `long_side`
// (frame_and_reduce), then run the stages for `settings` on the result, with the strokes placed on
// the framed picture. The finished picture, linear, not yet encoded. `decoded` is moved in, never
// copied: framing or reducing frees it, and otherwise its pixels become the result. `settings`
// must pass validate (export_image checks them first); an invalid frame throws
// std::invalid_argument. Fails with ErrorKind::out_of_memory, or ErrorKind::cancelled once `stop`
// is triggered (checked after the frame and between the stages).
[[nodiscard]] std::variant<Image, Error> finish_export(Photo decoded, const EditSettings& settings,
                                                      std::optional<int> long_side, int threads,
                                                      std::stop_token stop = {},
                                                      const StageCallback& on_stage = {});

// `part` of the finished picture an export at the full framed size (no long_side) would make from
// `base`, a decode_for_export result: bit-identical to those pixels of finish_export(base,
// settings, nullopt), for any `threads`. `part` is in pixels of the whole framed picture
// (plan_export_frame(settings.frame, base's size).size): columns [part.x0, part.x0 + part.width)
// and rows [part.y0, part.y0 + part.height). It runs export's own steps, but only on the part's
// pixels: the frame reads the whole `base` (apply_frame with a part), and each stage places the
// part where it is in the whole picture. `base` is only read; the result is part.width x
// part.height. Throws std::invalid_argument if `part` is empty or not inside the framed picture,
// or if the settings are invalid (validate), and std::bad_alloc when out of memory.
[[nodiscard]] Image render_region(const Photo& base, const EditSettings& settings,
                                  const FrameBox& part, int threads = 0);

// render_region that can be stopped, for a worker thread (DetailRenderer): the same image, or
// nothing (std::nullopt) once `stop` is triggered. `stop` is checked where export checks it, after
// the frame and between the stages; a stage under way runs to its end first. A stopped render
// never hands out what it made so far. Throws as render_region does.
[[nodiscard]] std::optional<Image> render_region(const Photo& base, const EditSettings& settings,
                                                 const FrameBox& part, int threads,
                                                 std::stop_token stop);

}  // namespace prism::gui

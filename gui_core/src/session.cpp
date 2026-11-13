#include "prism/gui/session.hpp"

#include <algorithm>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "prism/frame.hpp"
#include "prism/gui/detail_reader.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/system_memory.hpp"
#include "prism/heic.hpp"
#include "prism/raw.hpp"
#include "prism/resize.hpp"
#include "prism/utf8_path.hpp"

namespace prism::gui {

namespace fs = std::filesystem;

namespace {

// The extension, lower-cased (ASCII only, which is all the supported extensions use).
std::string extension_of(const fs::path& path) {
    std::string ext = utf8(path.extension());
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext;
}

bool is_png_or_jpeg(const fs::path& path) {
    const std::string ext = extension_of(path);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

// 'name.jpg', for messages: the file name alone reads better than a long folder path, and the
// details hold the library's message with the full path.
std::string quoted_name(const fs::path& path) {
    return "'" + utf8(path.filename()) + "'";
}

std::optional<Matrix3> camera_matrix_of(const EditSettings& s) {
    if (s.raw_camera_matrix) {
        return s.raw_camera_matrix->values;
    }
    return std::nullopt;
}

// Decodes `path` (already known to exist and to be a supported kind), turning every failure into
// an Error. `raw` only matters for RAW files.
std::variant<Photo, Error> decode(const fs::path& path, InputKind kind, const RawOptions& raw,
                                  int threads) {
    try {
        return load_photo(path, raw, threads);
    } catch (const std::bad_alloc&) {
        return Error{ErrorKind::out_of_memory, "Not enough memory to open this photo.", ""};
    } catch (const std::exception& e) {
        std::string message;
        if (kind == InputKind::raw) {
            message = "Couldn't read the RAW file " + quoted_name(path) +
                      ". The camera may be newer than this version of Prism's RAW decoder.";
        } else if (is_heic_file(path)) {
            message = "Couldn't open " + quoted_name(path) +
                      ". The file may be damaged, or not really a HEIC photo.";
        } else {
            const bool png = extension_of(path) == ".png";
            message = "Couldn't open " + quoted_name(path) + ". The file may be damaged, or not " +
                      (png ? "really a PNG." : "really a JPEG.");
        }
        return Error{ErrorKind::unreadable, message, e.what()};
    }
}

// The checks before decoding a file: its kind, that it exists, and RAW support.
std::variant<InputKind, Error> check_input(const fs::path& path) {
    InputKind kind = InputKind::image;
    if (is_raw_file(path)) {
        kind = InputKind::raw;
    } else if (is_heic_file(path)) {
        // An ordinary photo as far as editing goes; only decoding it needs macOS.
        if (!heic_support_available()) {
            return Error{ErrorKind::unsupported_format,
                         "HEIC photos can only be opened on macOS in this version of Prism.", ""};
        }
    } else if (!is_png_or_jpeg(path)) {
        return Error{
            ErrorKind::unsupported_format,
            heic_support_available()
                ? "Prism opens PNG, JPEG, HEIC and camera RAW files (.heic .heif .arw .dng .cr2 "
                  ".cr3 .nef .raf). " + quoted_name(path) + " isn't one of them."
                : "Prism opens PNG, JPEG and camera RAW files (.arw .dng .cr2 .cr3 .nef .raf). " +
                      quoted_name(path) + " isn't one of them.",
            ""};
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return Error{ErrorKind::not_found,
                     "Couldn't find " + quoted_name(path) + ". It may have been moved or deleted.",
                     utf8(path)};
    }
    if (kind == InputKind::raw && !raw_support_available()) {
        return Error{ErrorKind::no_raw_support,
                     "This copy of Prism was built without RAW support, so it can't open " +
                         quoted_name(path) + ".",
                     ""};
    }
    return kind;
}

Error invalid_settings_error(const std::vector<std::string>& problems) {
    std::string details;
    for (const std::string& p : problems) {
        details += (details.empty() ? "" : "\n") + p;
    }
    return {ErrorKind::invalid_settings, problems.front() + ".", details};
}

Error cancelled() {
    return {ErrorKind::cancelled, "Export cancelled.", ""};
}

}  // namespace

Session::Session(SessionOptions options) : options_(std::move(options)) {}

Session::~Session() = default;  // detail_reader_: stops its reads and waits for them

void Session::set_whole_base(Photo&& decoded) {
    if (std::max(decoded.image.width, decoded.image.height) <= options_.preview_long_side) {
        // The preview base is the decode itself (downscale would only copy it): keep the decode,
        // which can then also be the detail base, and point the base at its pixels.
        auto photo = std::make_shared<const Photo>(std::move(decoded));
        whole_base_ = std::shared_ptr<const Image>(photo, &photo->image);
        preview_photo_ = std::move(photo);
        return;
    }
    whole_base_ = std::make_shared<const Image>(
        downscale(decoded.image, options_.preview_long_side, options_.threads));
    preview_photo_.reset();
}

MaybeError Session::open_file(const fs::path& path) {
    const std::variant<InputKind, Error> checked = check_input(path);
    if (const Error* e = std::get_if<Error>(&checked)) {
        return *e;
    }
    const InputKind kind = std::get<InputKind>(checked);

    EditSettings settings;
    apply_preset(settings, default_preset(kind));
    RawDimensions full;
    RawOptions raw;
    raw.half_size = true;
    raw.full_size = &full;
    // The old photo's detail base goes before this decode, which for a PNG, JPEG or HEIC file is
    // at full size: two full-size photos are never held at once. If the open fails, the old
    // photo's base is read again.
    drop_detail_base();
    std::variant<Photo, Error> decoded = decode(path, kind, raw, options_.threads);
    if (Error* e = std::get_if<Error>(&decoded)) {
        update_detail_base(true);
        return std::move(*e);
    }

    // Only now, with everything loaded, replace the previous file.
    Photo& photo = std::get<Photo>(decoded);
    const PixelSize size{photo.image.width, photo.image.height};
    PhotoInfo info = photo.info;
    try {
        set_whole_base(std::move(photo));
    } catch (const std::bad_alloc&) {
        update_detail_base(true);
        return Error{ErrorKind::out_of_memory, "Not enough memory to open this photo.", ""};
    }
    path_ = path;
    kind_ = kind;
    photo_info_ = std::move(info);
    decoded_width_ = size.width;
    decoded_height_ = size.height;
    file_width_ = size.width;
    file_height_ = size.height;
    file_size_exact_ = true;
    if (kind == InputKind::raw) {
        // Half size is about half the sensor's width and height: the estimate if the file won't
        // say.
        const bool known = full.width > 0;
        file_width_ = known ? full.width : size.width * 2;
        file_height_ = known ? full.height : size.height * 2;
        file_size_exact_ = known && full.exact;
    }
    preview_camera_matrix_.reset();
    history_.reset(settings);
    // A new file's settings have no frame: the framed base is the whole base.
    framed_base_ = whole_base_;
    framed_from_ = whole_base_;
    framing_.reset();
    frame_lock_ = FrameLock{};
    update_detail_base(true);
    return std::nullopt;
}

MaybeError Session::set_settings(const EditSettings& s) {
    if (const std::vector<std::string> problems = validate(s); !problems.empty()) {
        return invalid_settings_error(problems);
    }
    EditSettings stored = s;
    stored.frame = normalised_frame(s.frame);
    const bool decodes = has_file() && kind_ == InputKind::raw &&
                         camera_matrix_of(stored) != preview_camera_matrix_;
    if (!decodes) {
        // The framed base first: if it can't be made, the settings stay as they were, so the base
        // and the settings never disagree.
        if (MaybeError error = update_framed_base(stored.frame)) {
            return error;
        }
        history_.set(stored);
        update_detail_base(false);
        return std::nullopt;
    }
    history_.set(stored);
    MaybeError error = update_preview_base();
    update_detail_base(false);
    return error;
}

MaybeError Session::apply_recipe(const Recipe& recipe) {
    EditSettings s = settings();
    gui::apply_recipe(s, recipe);
    return set_settings(s);
}

MaybeError Session::clear_strokes() {
    if (settings().strokes.empty()) {
        return std::nullopt;
    }
    EditSettings s = settings();
    s.strokes = Strokes{};
    return set_settings(s);
}

MaybeError Session::undo() {
    history_.undo();
    MaybeError error = update_preview_base();
    update_detail_base(false);
    return error;
}

MaybeError Session::redo() {
    history_.redo();
    MaybeError error = update_preview_base();
    update_detail_base(false);
    return error;
}

MaybeError Session::update_preview_base() {
    const std::optional<Matrix3> wanted = camera_matrix_of(settings());
    if (!has_file() || kind_ != InputKind::raw || wanted == preview_camera_matrix_) {
        return update_framed_base(settings().frame);
    }
    // A new camera matrix is a new detail base: the old one goes before this decode (the callers
    // plan and read the new one afterwards).
    drop_detail_base();
    RawOptions raw;
    raw.half_size = true;
    raw.camera_matrix = wanted;
    std::variant<Photo, Error> decoded = decode(path_, kind_, raw, options_.threads);
    if (Error* e = std::get_if<Error>(&decoded)) {
        // The old whole base stays, and is framed as the settings now say; the decode's error is
        // the one to report.
        (void)update_framed_base(settings().frame);
        return std::move(*e);
    }
    try {
        set_whole_base(std::move(std::get<Photo>(decoded)));
    } catch (const std::bad_alloc&) {
        (void)update_framed_base(settings().frame);
        return Error{ErrorKind::out_of_memory, "Not enough memory to open this photo.", ""};
    }
    preview_camera_matrix_ = wanted;
    return update_framed_base(settings().frame);
}

MaybeError Session::update_framed_base(const std::optional<Frame>& frame) {
    if (!has_file()) {
        return std::nullopt;
    }
    std::optional<PictureFrame> wanted;
    try {
        wanted = picture_frame_of(frame, whole_base_->width, whole_base_->height);
    } catch (const std::invalid_argument& e) {
        // validate() let no invalid frame into the history, so this does not happen.
        return Error{ErrorKind::invalid_settings, "The frame isn't valid.", e.what()};
    }
    if (framed_from_ == whole_base_ && wanted == framing_) {
        return std::nullopt;
    }
    try {
        framed_base_ = wanted ? std::make_shared<const Image>(
                                    apply_frame(*whole_base_, wanted->fitted, options_.threads))
                              : whole_base_;
    } catch (const std::bad_alloc&) {
        // Nothing changes: the base keeps the framing it was made with.
        return Error{ErrorKind::out_of_memory, "Not enough memory to frame this photo.", ""};
    }
    framed_from_ = whole_base_;
    framing_ = wanted;
    return std::nullopt;
}

PixelSize Session::framed_file_size() const {
    if (!has_file()) {
        return {};
    }
    return framed_export_size(settings().frame, {file_width_, file_height_});
}

PixelSize Session::framed_start_size() const {
    if (!has_file()) {
        return export_start_size();
    }
    return framed_export_size(settings().frame, export_start_size());
}

std::optional<Chromaticity> as_shot_white_of(const PhotoInfo& info) noexcept {
    if (!info.as_shot_white_xy) {
        return std::nullopt;
    }
    return Chromaticity{(*info.as_shot_white_xy)[0], (*info.as_shot_white_xy)[1]};
}

ExportJob Session::export_job() const {
    return {path_, kind_, settings(), options_.threads, export_long_side(), nullptr};
}

void Session::update_detail_base(bool opened) {
    if (!options_.detail_base || !has_file()) {
        return;
    }
    const EditSettings& s = settings();
    const bool raw = kind_ == InputKind::raw;
    // decode_for_export ignores the RAW options for anything but a RAW file.
    DetailKey key{path_, raw && s.raw_half_size, raw ? camera_matrix_of(s) : std::nullopt};
    if (!opened && detail_key_ == key) {
        return;
    }
    if (!detail_reader_) {
        detail_reader_ = std::make_unique<DetailReader>(options_.read_detail_base);
    }
    drop_detail_base();

    // The preview base is the export's decode when it was made by the same decode (a RAW file's
    // is half size, with the preview's camera matrix) and not shrunk (preview_photo_).
    const bool same_decode =
        preview_photo_ != nullptr &&
        (!raw || (key.raw_half_size && key.camera_matrix == preview_camera_matrix_));
    detail_plan_ = plan_detail_base(
        export_start_size(), {whole_base_->width, whole_base_->height}, same_decode,
        options_.physical_memory ? *options_.physical_memory : physical_memory_bytes());
    detail_key_ = std::move(key);
    start_detail_read();
}

void Session::drop_detail_base() {
    if (detail_reader_) {
        // The base, the read and anything it posted belong to the old file or options: stop the
        // read and drop its result now, so the session no longer holds it when the next decode
        // runs (a render thread still holding the old base keeps it until it lets go).
        detail_reader_->stop();
        (void)detail_reader_->take_all();
    }
    detail_base_.reset();
    detail_error_.reset();
    detail_reading_ = false;
    detail_key_.reset();
    ++detail_generation_;
}

void Session::start_detail_read() {
    if (detail_plan_ != DetailPlan::read || exporting_ || detail_base_ || detail_error_ ||
        detail_reading_) {
        return;
    }
    ++detail_generation_;
    detail_reader_->start(detail_generation_, path_, kind_, settings(), options_.threads);
    detail_reading_ = true;
}

DetailBaseState Session::detail_base_state() const noexcept {
    return prism::gui::detail_base_state(options_.detail_base && has_file(), detail_plan_,
                                         detail_base_ != nullptr, detail_error_.has_value());
}

std::shared_ptr<const Photo> Session::detail_base() const noexcept {
    switch (detail_base_state()) {
        case DetailBaseState::ready:
            return detail_base_;
        case DetailBaseState::not_needed:
            return preview_photo_;
        default:
            return nullptr;
    }
}

bool Session::poll_detail_base() {
    if (!detail_reader_) {
        return false;
    }
    bool changed = false;
    for (DetailReader::Result& result : detail_reader_->take_all()) {
        if (!detail_reading_ || result.generation != detail_generation_) {
            continue;  // for a file or options no longer current: freed here
        }
        detail_reading_ = false;
        changed = true;
        if (auto* photo = std::get_if<std::shared_ptr<const Photo>>(&result.base)) {
            detail_base_ = std::move(*photo);
        } else {
            detail_error_ = std::move(std::get<Error>(result.base));
        }
    }
    return changed;
}

bool Session::wait_for_detail_base(std::chrono::milliseconds timeout) {
    const bool done = wait_for_detail_read(timeout);
    (void)poll_detail_base();
    return done;
}

bool Session::wait_for_detail_read(std::chrono::milliseconds timeout) {
    return !detail_reader_ || detail_reader_->wait(timeout);
}

ExportJob Session::begin_export() {
    ExportJob job = export_job();
    if (detail_reader_) {
        // Stop first: after this nothing more is posted, so the poll below sees every result
        // there will be. A read that finished in time is kept as the ready base; anything else
        // still running is not wanted.
        detail_reader_->stop();
        (void)poll_detail_base();
        if (detail_reading_) {
            detail_reading_ = false;
            ++detail_generation_;  // stopped, it posts nothing; retired all the same
        }
        job.read_ended = detail_reader_->ended();
    }
    exporting_ = true;
    return job;
}

void Session::end_export() {
    exporting_ = false;
    if (detail_reader_) {
        (void)poll_detail_base();
        start_detail_read();
    }
}

namespace {

// The decode handed to run_after_decode: export's own, moved in so the frame can free it, or a
// base kept by the caller (render_region), only read.
using DecodedImage = std::variant<Image, const Image*>;

const Image& image_of(const DecodedImage& decoded) noexcept {
    if (const Image* owned = std::get_if<Image>(&decoded)) {
        return *owned;
    }
    return *std::get<const Image*>(decoded);
}

// `part` of `img` as a new image: the framed picture's pixels when nothing is framed, since the
// picture is then the decode itself.
Image copy_part(const Image& img, const FrameBox& part) {
    Image out;
    out.width = part.width;
    out.height = part.height;
    out.pixels.resize(static_cast<std::size_t>(part.width) * static_cast<std::size_t>(part.height) *
                      3);
    const std::size_t row = static_cast<std::size_t>(part.width) * 3;
    for (int y = 0; y < part.height; ++y) {
        const std::size_t from =
            (static_cast<std::size_t>(part.y0 + y) * static_cast<std::size_t>(img.width) +
             static_cast<std::size_t>(part.x0)) *
            3;
        std::copy_n(img.pixels.begin() + static_cast<std::ptrdiff_t>(from), row,
                    out.pixels.begin() + static_cast<std::ptrdiff_t>(y * row));
    }
    return out;
}

// Everything export does between the decode and the encode, in export's order: frame (and reduce
// to `long_side`), then the stages on the framed picture's grid. The one list of these steps:
// finish_export and render_region both run it.
//
// Without `part`, `decoded` must be export's own decode (an Image), which the frame or the
// reduction frees, or which becomes the result. With `part`, only that part of the full-size
// framed picture is made (no reduction) and `decoded` is only read: the frame
// works out just those pixels (apply_frame's part) and every stage then runs on them with the
// part's place in the whole framed picture, so the result is bit-identical to the same pixels of
// the whole. `part` must already be checked against the framed size.
std::variant<Image, Error> run_after_decode(DecodedImage decoded,
                                            const std::optional<Chromaticity>& as_shot,
                                            const EditSettings& settings,
                                            std::optional<int> long_side,
                                            const std::optional<FrameBox>& part, int threads,
                                            std::stop_token stop, const StageCallback& on_stage) {
    const int decoded_width = image_of(decoded).width;
    const int decoded_height = image_of(decoded).height;

    // Frame, then reduce, before editing: the turn works on the full decode and every stage then
    // works on the small image. The full decode (which on a 60 MP photo is ~730 MB) is moved in and
    // freed before the pipeline allocates its own buffers. Without a frame this is the reduction
    // alone, exactly as before frames existed.
    FramedExport framed;
    try {
        if (part) {
            framed.frame = plan_export_frame(settings.frame, decoded_width, decoded_height);
            framed.image = framed.frame.fitted
                               ? apply_frame(image_of(decoded), *framed.frame.fitted, threads, part)
                               : copy_part(image_of(decoded), *part);
        } else {
            // Only export's own decode comes here (render_region always has a part): moved, never
            // copied.
            framed = frame_and_reduce(std::move(std::get<Image>(decoded)), settings.frame,
                                      long_side, threads);
        }
    } catch (const std::bad_alloc&) {
        return Error{ErrorKind::out_of_memory,
                     normalised_frame(settings.frame)
                         ? "Not enough memory to frame or shrink this photo."
                         : "Not enough memory to shrink this photo.",
                     ""};
    }
    if (stop.stop_requested()) {
        return cancelled();
    }
    Image& img = framed.image;

    // The picture the stages see the image as part of: the framed picture, reduced or not, or for
    // a part the whole full-size framed picture.
    const PixelSize whole = part ? framed.frame.size : PixelSize{img.width, img.height};

    // The strokes sit on the image as framed and reduced, not on the original: they cover the same
    // part of the photo at any export size. Without a frame the grid is the whole image (as in
    // 0.5.0); with one it is the framed grid of the decode, with the reduction as its scale. A part
    // is placed at its own corner of the whole picture.
    std::optional<PictureFrame> framing;
    if (framed.frame.fitted) {
        const PixelSize size = framed.frame.size;
        const float scale = static_cast<float>(std::max(size.width, size.height)) /
                            static_cast<float>(std::max(whole.width, whole.height));
        framing = PictureFrame{*framed.frame.fitted, decoded_width, decoded_height, scale};
    }
    const PixelGrid grid =
        part ? stage_grid(framing, whole.width, whole.height, part->x0, part->y0, 1, part->width,
                          part->height)
             : stage_grid(framing, img.width, img.height);
    if (process(img, to_process_settings(settings, final_stage, as_shot, grid), threads, stop,
                on_stage) == ProcessResult::cancelled ||
        stop.stop_requested()) {
        return cancelled();
    }
    return std::move(img);
}

}  // namespace

std::variant<Photo, Error> decode_for_export(const fs::path& source, InputKind kind,
                                             const EditSettings& settings, int threads,
                                             std::stop_token stop) {
    const std::variant<InputKind, Error> checked = check_input(source);
    if (const Error* e = std::get_if<Error>(&checked)) {
        return *e;
    }
    RawOptions raw;
    raw.half_size = settings.raw_half_size;
    raw.camera_matrix = camera_matrix_of(settings);
    if (stop.stop_requested()) {
        return cancelled();
    }
    return decode(source, kind, raw, threads);
}

std::variant<Image, Error> finish_export(Photo decoded, const EditSettings& settings,
                                         std::optional<int> long_side, int threads,
                                         std::stop_token stop, const StageCallback& on_stage) {
    const std::optional<Chromaticity> as_shot = as_shot_white_of(decoded.info);
    return run_after_decode(std::move(decoded.image), as_shot, settings, long_side, std::nullopt,
                            threads, stop, on_stage);
}

Image render_region(const Photo& base, const EditSettings& settings, const FrameBox& part,
                    int threads) {
    // Not stopped: with no stop token there is always an image.
    return *render_region(base, settings, part, threads, std::stop_token{});
}

std::optional<Image> render_region(const Photo& base, const EditSettings& settings,
                                   const FrameBox& part, int threads, std::stop_token stop) {
    if (const std::vector<std::string> problems = validate(settings); !problems.empty()) {
        throw std::invalid_argument("render_region: " + problems.front());
    }
    const ExportFrame plan =
        plan_export_frame(settings.frame, base.image.width, base.image.height);
    const auto inside = [](int first, int count, int size) {
        return first >= 0 && count >= 1 &&
               static_cast<long long>(first) + static_cast<long long>(count) <=
                   static_cast<long long>(size);
    };
    if (!inside(part.x0, part.width, plan.size.width) ||
        !inside(part.y0, part.height, plan.size.height)) {
        throw std::invalid_argument(
            "render_region: the part (" + std::to_string(part.x0) + ", " +
            std::to_string(part.y0) + ") " + std::to_string(part.width) + " × " +
            std::to_string(part.height) + " is empty or not inside the " +
            std::to_string(plan.size.width) + " × " + std::to_string(plan.size.height) +
            " framed picture");
    }
    std::variant<Image, Error> result = run_after_decode(
        &base.image, as_shot_white_of(base.info), settings, std::nullopt, part, threads, stop, {});
    if (Error* e = std::get_if<Error>(&result)) {
        if (e->kind == ErrorKind::cancelled) {
            return std::nullopt;  // stopped: what was made so far is freed, never handed out
        }
        if (e->kind == ErrorKind::out_of_memory) {
            throw std::bad_alloc();
        }
        // Not reached: run_after_decode fails only when out of memory or stopped.
        throw std::runtime_error("render_region: " + e->message);
    }
    return std::move(std::get<Image>(result));
}

MaybeError export_image(const ExportJob& job, const fs::path& output, std::stop_token stop,
                        const StageCallback& on_stage) {
    if (!is_png_or_jpeg(output)) {
        return Error{ErrorKind::unsupported_format,
                     "Prism saves PNG and JPEG files: give " + quoted_name(output) +
                         " the extension .png, .jpg or .jpeg.",
                     ""};
    }
    std::error_code ec;
    if (fs::equivalent(job.source, output, ec)) {
        return Error{ErrorKind::save_failed,
                     "Prism won't overwrite the photo you opened: choose another file name.", ""};
    }
    if (const std::vector<std::string> problems = validate(job.settings); !problems.empty()) {
        return invalid_settings_error(problems);
    }

    // The session's background read of this photo, if stopped for this export, has ended first.
    if (job.read_ended && !job.read_ended->wait(stop)) {
        return cancelled();
    }
    std::variant<Photo, Error> decoded =
        decode_for_export(job.source, job.kind, job.settings, job.threads, stop);
    if (Error* e = std::get_if<Error>(&decoded)) {
        return std::move(*e);
    }
    std::variant<Image, Error> finished = finish_export(
        std::move(std::get<Photo>(decoded)), job.settings, job.long_side, job.threads, stop,
        on_stage);
    if (Error* e = std::get_if<Error>(&finished)) {
        return std::move(*e);
    }
    try {
        save_image(output, std::get<Image>(finished), job.threads);
    } catch (const std::bad_alloc&) {
        return Error{ErrorKind::out_of_memory, "Not enough memory to save this photo.", ""};
    } catch (const std::exception& e) {
        const fs::path folder = output.has_parent_path() ? output.parent_path() : fs::path(".");
        return Error{ErrorKind::save_failed,
                     "Couldn't save to " + quoted_name(folder) +
                         ". Check that the folder exists and that you can write to it.",
                     e.what()};
    }
    return std::nullopt;
}

}  // namespace prism::gui

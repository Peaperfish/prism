#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "prism/pipeline.hpp"
#include "prism/process.hpp"
#include "prism/ranges.hpp"
#include "prism/stroke_field.hpp"
#include "prism/white_balance.hpp"

namespace prism::gui {

// Every stage of the pipeline, in the order light moves through it. `decode` and `encode` are the
// bookends (turning a file into linear float, and turning the stage's output into 8-bit sRGB);
// the stages in between are the edits, each backed by an `*_on` flag on EditSettings.
//
// The numbers are only an order: nothing saves one to a file (recipes and preferences use names),
// so a stage can be added in the middle.
enum class StageId {
    decode,
    exposure,
    dodge_burn,
    white_balance,
    matrix,
    saturation,
    contrast,
    curve,
    encode,
};

inline constexpr StageId final_stage = StageId::encode;

// How many stages there are: the size of anything indexed by StageId.
inline constexpr std::size_t stage_count = static_cast<std::size_t>(final_stage) + 1;

// One row of the UI's pipeline list: its stable id, its display name, a one-sentence explanation
// in plain words of what the stage does to the light, and whether it has an on/off switch.
struct StageInfo {
    StageId id = StageId::decode;
    std::string_view slug;
    std::string_view name;
    std::string_view description;
    bool can_toggle = false;
    // Kept out of the Light path, the compare popup and the width loop. No stage is hidden now
    // (Dodge & burn was until its row existed); the flag stays for a stage that runs before its row
    // does. A hidden stage still runs, and its slug still names it in the pipeline.
    bool hidden = false;
};

// All pipeline stages in execution order, from Decode to Output encode.
[[nodiscard]] std::span<const StageInfo> pipeline_stages() noexcept;

// The entry for `id` in pipeline_stages().
[[nodiscard]] const StageInfo& stage_info(StageId id) noexcept;

// The grid on the photo that an image handed to the pipeline sits on, for the Dodge & burn stage:
// the one place a grid is built. `full_w` x `full_h` is the picture the strokes were drawn on as
// the render sees it (the whole preview base, or the reduced export image); the image is the
// part of it starting at pixel (x0, y0), every `stride` pixels, `cols` x `rows` of them (0 = to the
// right / bottom edge). Whole picture: stage_grid(w, h). A test strip band: its rectangle. The
// clip report's sample grid: ClipRecorder::grid. One sampled pixel: x, y with cols = rows = 1.
[[nodiscard]] PixelGrid stage_grid(int full_w, int full_h, int x0 = 0, int y0 = 0, int stride = 1,
                                   int cols = 0, int rows = 0) noexcept;

// The brush strokes on a photo, shared and never changed once made: copying it (an undo step, the
// settings handed to the render thread) copies a pointer. Empty by default.
class Strokes {
public:
    Strokes() = default;
    explicit Strokes(std::shared_ptr<const StrokeField> field) : field_(std::move(field)) {}

    // No strokes: a null handle, or a field with none.
    [[nodiscard]] bool empty() const noexcept { return !field_ || field_->strokes.empty(); }
    // The strokes, or nullptr for a null handle.
    [[nodiscard]] const std::shared_ptr<const StrokeField>& field() const noexcept { return field_; }

    // Equal if both are empty, or the same object, or their strokes are equal.
    friend bool operator==(const Strokes& a, const Strokes& b) noexcept;

private:
    std::shared_ptr<const StrokeField> field_;
};

// A 3x3 matrix loaded from a file, with the file it came from (shown in the UI).
struct MatrixFile {
    std::filesystem::path path;
    Matrix3 values = identity_matrix;

    // Reads `path` with prism::load_matrix. Throws std::runtime_error with the library's message
    // (which names the file and line) if the file is missing or malformed.
    [[nodiscard]] static MatrixFile load(const std::filesystem::path& path);

    bool operator==(const MatrixFile&) const = default;
};

enum class CurveKind { none, gamma, reinhard, filmic };

// Everything the GUI edits, as plain values. The controls all edit this one object (the three
// stage sliders are a view of three of its fields, see below), and it is what undo/redo
// stores and what the preview worker gets a copy of. A plain value type: copying it to another
// thread shares nothing, so no lock is needed for the settings themselves.
//
// The default-constructed value is neutral: it leaves an image exactly as it was.
struct EditSettings {
    // Each stage has a value and an on/off switch (on its Light path row). Turning a stage off keeps
    // its value, so turning it back on restores it; an off stage is skipped.
    float exposure_stops = 0.0f;  // any finite number, as the CLI's --exposure
    bool exposure_on = true;

    // Dodge & burn: brush strokes that lighten or darken parts of the image (stroke_field.hpp),
    // applied right after Exposure. Not saved in a recipe; a look keeps them.
    Strokes strokes;
    bool dodge_burn_on = true;

    // The light the photo was lit by (--kelvin, --tint). None is "As shot": the light the camera
    // balanced for, which changes nothing. kelvin_range and tint_range.
    std::optional<WhiteBalance> white_balance;
    bool white_balance_on = true;

    std::optional<MatrixFile> matrix;  // the colour matrix (--matrix); none = skipped
    bool matrix_on = true;

    float saturation = 1.0f;  // saturation_range (0 to 10), as --saturation
    bool saturation_on = true;

    float contrast = 1.0f;  // contrast_range (0.1 to 10), as --contrast
    bool contrast_on = true;

    CurveKind curve = CurveKind::none;
    float gamma = gamma_default;                    // used when curve == gamma; > 0
    float reinhard_white = reinhard_white_default;  // used when curve == reinhard; > 0
    bool curve_on = true;

    // RAW input only. The preview is always decoded at half size; `raw_half_size` makes the
    // export half size too. `raw_camera_matrix` replaces LibRaw's camera -> sRGB matrix
    // (--raw-camera-matrix); it is applied while decoding, so changing it re-decodes the file.
    bool raw_half_size = false;
    std::optional<MatrixFile> raw_camera_matrix;

    // Member-by-member comparison, generated by the compiler. Floats compare by value.
    bool operator==(const EditSettings&) const = default;
};

// Back to the neutral defaults: everything, including the matrix files and RAW options.
void reset_to_defaults(EditSettings& s);

// Problems that would make the CLI reject these values, as messages for the user, e.g.
// "Saturation must be from 0 to 10 (it is 12)". Empty if the settings are valid. A stage that is
// off is still checked: turning it on must not produce an invalid state.
[[nodiscard]] std::vector<std::string> validate(const EditSettings& s);

// The tone curve's CLI spec ("gamma:2.2", "reinhard:4", "filmic"), or nothing for no curve.
// Numbers are written in the shortest form that reads back as exactly the same float.
[[nodiscard]] std::optional<std::string> curve_spec(const EditSettings& s);

// Whether `id` is turned on in `s`. Decode and Output encode have no switch and are always on.
[[nodiscard]] bool stage_enabled(const EditSettings& s, StageId id) noexcept;

// Sets the `*_on` switch for `id` in `s`. Does nothing for Decode and Output encode.
void set_stage_enabled(EditSettings& s, StageId id, bool enabled) noexcept;

// What prism::process should do up to and including `stop_after`: stages that are off, have
// nothing to apply, or come after `stop_after` are left empty. The curve is built with
// prism::parse_curve(curve_spec(s)), so it is exactly the curve the CLI builds from the same
// spec. Throws std::invalid_argument with validate()'s messages if the settings are invalid.
//
// With strokes, Dodge & burn on and the stage reached, the result carries them with `grid`: where
// the image this will run on sits on the photo (stage_grid). Strokes and no grid is an error
// (std::invalid_argument): the stage would not know which pixels it has.
// The RAW options are not part of it: they apply when decoding.
//
// A chosen white balance becomes the matrix prism::white_balance_matrix builds against
// `as_shot_white`, the photo's as-shot light (Session::as_shot_white); without one the photo is
// taken to have been lit by prism::default_as_shot, as the CLI does. "As shot" (no white balance)
// and a white balance that is off give no white balance stage at all.
[[nodiscard]] ProcessSettings to_process_settings(
    const EditSettings& s, StageId stop_after = final_stage,
    const std::optional<Chromaticity>& as_shot_white = std::nullopt,
    const std::optional<PixelGrid>& grid = std::nullopt);

// --- The stage sliders ---------------------------------------------------------------------------
//
// Exposure and Saturation have sliders from -100 to +100, each an invertible map onto one field of
// EditSettings (plan section 7):
//
//   brightness b -> exposure_stops = b / 50          (-2 to +2 stops; 50 = +1 stop)
//   colour     k -> saturation     = 1 + k / 100     (-100 = black and white, +100 = 2.0)
//
// Contrast's slider is not one of these: it is in half grades (paper_grade.hpp) and has no position
// in this scale.

// A field outside what its slider can show (e.g. saturation 3, from a recipe) is shown at
// the slider's end, and outside_simple_range says so (the row says "beyond the slider"); moving the
// slider then takes it over. A stage that is off shows as 0, its neutral position.

inline constexpr float simple_min = -100.0f;
inline constexpr float simple_max = 100.0f;

struct SimpleControls {
    float brightness = 0.0f;
    float colour = 0.0f;

    bool operator==(const SimpleControls&) const = default;
};

// Where the Exposure and Saturation sliders sit for these settings, each clamped to [-100, +100].
[[nodiscard]] SimpleControls simple_from(const EditSettings& s);

// Which fields the sliders cannot show exactly (they are at the slider's end instead).
struct SimpleOutOfRange {
    bool brightness = false;
    bool colour = false;
};
[[nodiscard]] SimpleOutOfRange outside_simple_range(const EditSettings& s);

// Sets exposure and saturation from the sliders (each clamped to [-100, +100] first) and turns
// those two stages on. Nothing else changes: contrast, the matrix, curve and RAW options are not
// these sliders' and stay as they are.
void apply_simple(EditSettings& s, const SimpleControls& c);

// One of the sliders, for when the user moves just that one.
enum class SimpleControl { brightness, contrast, colour };

// Sets only the field behind `control` from its slider position (clamped to [-100, +100]) and
// turns only that stage on. Unlike apply_simple, the other fields stay exactly as they are, even
// when they lie beyond their own sliders' ends or their stages are off: moving Exposure's slider
// must not quietly clamp a saturation of 3 that a recipe set. Contrast has no position in this
// scale: for it this changes nothing (apply_contrast and reset_simple_control are its ways in).
void apply_simple_control(EditSettings& s, SimpleControl control, float position);

// The slider position (clamped to [-100, +100]) that gives `value` in the field behind `control`,
// in that field's own unit: stops for brightness (exposure_stops), the multiplier for colour
// (saturation). The inverse of apply_simple_control, for a value typed in the unit a row's value
// line shows. NaN gives 0. Contrast has no position in this scale and always gives 0.
[[nodiscard]] float simple_position_for(SimpleControl control, float value);

// Puts the field behind `control` back to neutral (exposure 0 stops, saturation 1, contrast exactly
// 1.0) and turns only that stage on.
void reset_simple_control(EditSettings& s, SimpleControl control);

}  // namespace prism::gui

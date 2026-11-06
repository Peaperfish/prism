#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prism/gui/clipping.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/stage_controls.hpp"

namespace prism::gui {

// The Light path panel as data: what each stage's row says and holds, and how tall it is. No ImGui
// here, so the wording and the heights are tested without a window; gui_app/light_path.cpp draws it.

// --- Clip marks ----------------------------------------------------------------------------------

enum class ClipMarkKind { below_black, above_white, out_of_gamut };

// One clip mark of a row: a dot (hollow when recoverable, filled when lost) and a short text.
struct ClipMark {
    ClipMarkKind kind = ClipMarkKind::above_white;
    ClipFate fate = ClipFate::recoverable;  // never none: such a mark isn't made
    std::string percent;                    // "7.0%": all a narrow row has room for
    std::string text;                       // "7.0% above white here", "7.0% above white"
    std::string what;                       // the tooltip's heading: "Highlights above white"
    std::string note;                       // the tooltip's sentence on what the fate means

    bool operator==(const ClipMark&) const = default;
};

// The marks of a stage that clips, in the order below black, above white, out of gamut, each only
// if it has something to say. None for a stage that didn't run. `total` is the report's
// sample_count.
[[nodiscard]] std::vector<ClipMark> clip_marks(const StageClip& clip, std::uint64_t total);

// --- A stage's row -------------------------------------------------------------------------------

struct LightPathRow {
    StageId stage = StageId::decode;
    std::string name;
    std::string subtitle;  // the line under the name: Decode's and Output encode's
    // On the first line, at the right: "+1.00 stops", "×1.20", "black and white", the matrix's name.
    // A stage that is off gives the value it comes back with ("+1.00 stops when on").
    std::string value;
    bool value_muted = false;
    std::string value_tooltip;  // for a value that was shortened: the matrix's whole path
    bool can_toggle = false;
    bool enabled = true;
    bool viewed = false;
    bool dimmed = false;  // after the furthest stage on screen: see stage_after_view
    std::string tooltip;  // the stage's summary, or for a dimmed row why it is dimmed

    // The slider of Exposure, Saturation and Contrast. Off, it sits at its neutral position.
    std::optional<SimpleControl> slider;
    float slider_position = 0.0f;
    bool beyond_slider = false;  // the value lies past the slider's end
    std::string slider_reset;    // "Reset to +0.00 stops"

    // The Tone curve's tabs, and the number of the curves that take one.
    bool curve_tabs = false;
    CurveKind curve = CurveKind::none;
    std::optional<CurveParameter> parameter;
    float parameter_value = 0.0f;
    std::string parameter_text;   // "4.00"
    std::string parameter_reset;  // "Reset to 4"

    bool test_strip = false;  // the Test strip button shares the clip line
    std::vector<ClipMark> marks;
};

// What the panel is made from.
struct LightPathInputs {
    const EditSettings& settings;
    InputKind kind = InputKind::image;
    std::string_view file_kind;  // file_kind_label: "RAW", "JPEG", "PNG", "HEIC"
    StageId viewed = final_stage;
    std::optional<StageId> compare;
    const ClipReport* clips = nullptr;  // none until a frame with a report has arrived
};

// "RAW, camera colour", "RAW, your camera matrix", "JPEG, read as sRGB", "PNG, read as sRGB" or
// "HEIC, converted from its own colours".
[[nodiscard]] std::string decode_subtitle(const EditSettings& s, std::string_view file_kind);

// The Colour matrix row's value: the matrix file's name, "From recipe" for a matrix from a recipe
// that names no file, and with no matrix "Camera default" for a RAW file (its colour comes from
// the camera's own matrix) and "None" for any other.
[[nodiscard]] std::string matrix_value(const EditSettings& s, InputKind kind);

// "Compare: Off", "Compare: Original", "Compare: after Exposure".
[[nodiscard]] std::string compare_action_text(StageId viewed, std::optional<StageId> compare);

[[nodiscard]] LightPathRow light_path_row(const LightPathInputs& in, StageId stage);

struct LightPathView {
    std::vector<LightPathRow> rows;  // in pipeline order
    std::string compare_text;
    std::optional<StageId> compare;  // as effective_compare
    StageId viewed = final_stage;
};

[[nodiscard]] LightPathView light_path_view(const LightPathInputs& in);

// --- A row's height ------------------------------------------------------------------------------

// A row's sizes, in pixels at the current interface size.
struct LightPathMetrics {
    float pad_top = 0;
    float pad_bottom = 0;
    float line = 0;          // the first line: the name, the value, the switch
    float secondary = 0;     // a line of small text
    float slider = 0;        // a slider
    float tabs = 0;          // the Tone curve's tabs, with their well
    float strip_button = 0;  // the Test strip button
    float gap = 0;           // between two lines
};

// Where a row's parts go, from the row's top. A part the row doesn't have is empty.
struct LightPathRowLayout {
    float height = 0;
    float line_y = 0;
    std::optional<float> subtitle_y;
    std::optional<float> slider_y;
    std::optional<float> tabs_y;
    std::optional<float> parameter_y;
    std::optional<float> parameter_slider_y;
    // The clip line is always there, whether or not it has a mark to show (#38).
    float clip_y = 0;
    float clip_height = 0;

    bool operator==(const LightPathRowLayout&) const = default;
};

// Whether a row shows its extra lines: the Tone curve, while its curve takes a number. Only a
// choice of the user's opens or closes a row, so nothing below it moves on its own.
[[nodiscard]] bool row_expanded(const LightPathRow& row) noexcept;

// A row's layout, from its stage and row_expanded alone: never from the values it shows, its clip
// marks, whether it is viewed, dimmed or off, or the mouse. A height that followed any of those
// would move the rows below under a still cursor (#38).
[[nodiscard]] LightPathRowLayout light_path_row_layout(const LightPathRow& row,
                                                       const LightPathMetrics& metrics) noexcept;

}  // namespace prism::gui

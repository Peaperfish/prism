#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prism/gui/canvas_input.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/stage_controls.hpp"

namespace prism::gui {

// The Light path panel as data: what each stage's row says and holds, and how tall it is. No ImGui
// here, so the wording and the heights are tested without a window; gui_app/light_path.cpp draws it.

// --- Texts both the drawing and the panel's minimum width use ------------------------------------
// One source for each: the drawing code draws these, and light_path_min_width measures these.

namespace light_path_text {
inline constexpr std::string_view title = "Light path";
inline constexpr std::string_view beyond_slider = "(beyond the slider)";
inline constexpr std::string_view pick_neutral = "Pick neutral";
inline constexpr std::string_view test_strip = "Test strip";
inline constexpr std::string_view brush = "Brush";
inline constexpr std::string_view clear_strokes = "Clear";
// The widest Strength reads "2.0 stops" (brush_strength_text); the controls line measures it.
inline constexpr std::string_view widest_strength = "2.0 stops";
}  // namespace light_path_text

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
    std::string subtitle;  // the line under the name: Decode's, White balance's, Output encode's
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

    // The slider of Exposure, Saturation and Contrast. While the stage is off it is greyed out and
    // sits at its neutral position (never beyond the slider), though slider_typed and slider_reset
    // still go with the stored value, the one it comes back with.
    std::optional<SimpleControl> slider;
    // Exposure's and Saturation's position is in settings.hpp's -100 to +100. Contrast's is in half
    // grades, a whole number from 0 to 10 (paper_grade.hpp), which is where the slider snaps to.
    float slider_position = 0.0f;
    bool beyond_slider = false;  // the value lies past the slider's end (never while the stage is off)
    std::string slider_reset;    // "Reset to +0.00 stops"
    // What Ctrl/Cmd-click types into: the value as the line above shows it, in its own unit
    // ("+0.70 stops", "1.20", a grade "3½"), see typed_field_text.
    std::string slider_typed;

    // White balance's two sliders, Kelvin over Tint. While the stage is off, or nothing is chosen
    // (As shot), both sit at the as-shot light. `slider_reset` is their reset's words.
    struct WhiteBalanceSliders {
        float kelvin_position = 0.0f;  // 0 to 1, even in mireds: kelvin_slider_position
        float tint_position = 0.0f;    // tint_range, -100 to +100
        // Where As shot is on each slider: where its fill starts from.
        float kelvin_as_shot = 0.0f;
        float tint_as_shot = 0.0f;
        // The numbers beside the sliders: "5810 K" and "+34" ("−8", "0"), where the sliders sit.
        std::string kelvin_text;
        std::string tint_text;
        // What Ctrl/Cmd-click types into: "5330 K" and "+27" (typed_field_text).
        std::string kelvin_typed;
        std::string tint_typed;

        bool operator==(const WhiteBalanceSliders&) const = default;
    };
    std::optional<WhiteBalanceSliders> white_balance;

    // Dodge & burn's controls line: the Brush button, the Strength slider and Clear. The line is
    // always there, so its height never depends on what is shown in it.
    struct BrushControls {
        float strength = 0.0f;         // the brush's, in stops (brush_strength_min to _max)
        std::string strength_text;     // "0.5 stop", "1.5 stops"
        std::string strength_reset;    // "Reset to 0.5 stop"
        bool can_clear = false;        // there are strokes to clear

        bool operator==(const BrushControls&) const = default;
    };
    std::optional<BrushControls> brush_controls;

    // The Tone curve's tabs, and the number of the curves that take one.
    bool curve_tabs = false;
    CurveKind curve = CurveKind::none;
    std::optional<CurveParameter> parameter;
    float parameter_value = 0.0f;
    std::string parameter_text;   // "4.00"
    std::string parameter_reset;  // "Reset to 4"

    // The Test strip button shares the clip line. Like Pick neutral it takes no room of its own: it
    // is as tall as the line and its gap, so it never changes the row's height.
    bool test_strip = false;
    // White balance's Pick neutral button shares the subtitle line. It takes no room of its own, so
    // it never changes the row's height (light_path_row_layout does not look at it).
    bool pick_neutral = false;
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
    // The photo's as-shot light (Session::as_shot_white): what As shot reads, and where the White
    // balance sliders sit until a light is chosen.
    std::optional<Chromaticity> as_shot_white;
    // The Brush's strength (BrushMode::strength): a tool setting, not part of the edit.
    float brush_strength = brush_strength_default;
};

// What Dodge & burn's row says it holds: "None"; "1 dodge", "3 dodges"; "1 burn", "3 burns"; both
// kinds only the count, "3 strokes" ("2 burns · 1 dodge" at its longest made the panel wider than
// the other rows need). A stroke that dodges has a positive weight, one that burns a
// negative weight. With the stage off and strokes on the photo the caller appends " when on".
// `burns` and `dodges` are counts.
[[nodiscard]] std::string strokes_text(std::size_t burns, std::size_t dodges);

// "1 dodge", "3 burns", ...: the count of each kind in `strokes`, as strokes_text.
[[nodiscard]] std::string strokes_text(const Strokes& strokes);

// The longest the value can read in practice, which the panel's minimum width allows for
// (light_path_min_width): a thousand strokes of each kind. More digits are shortened, as a long
// matrix name is.
inline constexpr std::string_view widest_strokes_value = "1998 strokes when on";

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
    float gap = 0;           // between two lines
};

// Where a row's parts go, from the row's top. A part the row doesn't have is empty.
struct LightPathRowLayout {
    float height = 0;
    float line_y = 0;
    std::optional<float> subtitle_y;
    std::optional<float> slider_y;
    std::optional<float> second_slider_y;  // White balance's Tint, under its Kelvin
    std::optional<float> tabs_y;
    std::optional<float> parameter_y;
    std::optional<float> parameter_slider_y;
    std::optional<float> controls_y;  // Dodge & burn's Brush, Strength and Clear
    // The clip line is always there, whether or not it has a mark to show (#38).
    float clip_y = 0;
    float clip_height = 0;

    bool operator==(const LightPathRowLayout&) const = default;
};

// Whether a row shows its extra lines: the Tone curve, while its curve takes a number. Only a
// choice of the user's opens or closes a row, so nothing below it moves on its own.
[[nodiscard]] bool row_expanded(const LightPathRow& row) noexcept;

// White balance has a subtitle and two sliders, whatever is chosen. Dodge & burn has one controls
// line, as tall as a slider.
//
// A row's layout, from its stage and row_expanded alone: never from the values it shows, its clip
// marks, whether it is viewed, dimmed or off, or the mouse. A height that followed any of those
// would move the rows below under a still cursor (#38).
[[nodiscard]] LightPathRowLayout light_path_row_layout(const LightPathRow& row,
                                                       const LightPathMetrics& metrics) noexcept;

}  // namespace prism::gui

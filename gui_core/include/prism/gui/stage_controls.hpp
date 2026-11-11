#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "prism/gui/settings.hpp"

namespace prism::gui {

// What the Light path shows and edits in each stage's row. Everything the window needs to know
// about those controls is here, so it can be tested without one.
//
// Exposure, Saturation and Contrast are edited with one slider each (the ranges and conversions are
// in settings.hpp); apply_simple_control changes only that slider's field.

// The slider that edits `stage`, or nothing for the stages without one.
[[nodiscard]] std::optional<SimpleControl> stage_slider(StageId stage) noexcept;

// The stage that `control` edits.
[[nodiscard]] StageId stage_of(SimpleControl control) noexcept;

// --- Units ---------------------------------------------------------------------------------------

// "+0.70 stops", "-1.00 stops", "+0.00 stops": the sign is always shown, and a value that rounds
// to zero is "+0.00" rather than "-0.00".
[[nodiscard]] std::string format_exposure(float stops);

// "×1.20": a multiplier, to two places, with the multiplication sign.
[[nodiscard]] std::string format_multiplier(float value);

// Saturation as a multiplier ("×1.20"), except 0, which is "black and white".
[[nodiscard]] std::string format_saturation(float value);

// A colour temperature as a row shows it, rounded to 10 K: "5330 K".
[[nodiscard]] std::string format_kelvin(double kelvin);

// A tint as a row shows it, rounded to a whole number, always with its sign except at zero, and
// with the real minus sign (U+2212): "+27", "−10", "0".
[[nodiscard]] std::string format_tint(double tint);

// What a matrix from a recipe that names no file is called, in the Colour matrix row and in text.
inline constexpr std::string_view recipe_matrix_label = "From recipe";

// The value to show in `stage`'s row: format_exposure / format_saturation / format_multiplier for
// the three sliders, the matrix file's name ("none" without a matrix, "From recipe" (recipe_matrix_label) for one a
// recipe gave without naming a file: only a label, the path stays empty), and "none", "gamma 2.2",
// "Reinhard, white 4" or "filmic" for the curve. White balance is "As shot" or the chosen light,
// "5200 K · tint +10". Empty for Decode and Output encode, which have nothing to edit.
// Shows the stored value even when the stage is off (the row greys it out).
[[nodiscard]] std::string stage_value_text(const EditSettings& s, StageId stage);

// --- White balance -------------------------------------------------------------------------------
//
// The White balance row names the light the photo was lit by with two sliders, Kelvin and Tint.
// With nothing chosen the photo is "As shot": the sliders sit at the as-shot light and nothing is
// applied.

// The row's two sliders.
enum class WhiteBalanceControl { kelvin, tint };

// The photo's as-shot light as Kelvin and tint: prism::white_balance_of its as-shot white, or
// prism::default_as_shot (6500 K, tint 0) for a photo without one. Not rounded, not clamped.
[[nodiscard]] WhiteBalance as_shot_white_balance(
    const std::optional<Chromaticity>& as_shot_white) noexcept;

// What the sliders set: Kelvin rounded to 10 K within kelvin_range, tint rounded to a whole number
// within tint_range. A NaN gives the as-shot default's number (6500 K, tint 0).
[[nodiscard]] double snap_kelvin(double kelvin) noexcept;
[[nodiscard]] double snap_tint(double tint) noexcept;

// Where the two sliders sit: at the chosen light (clamped to the ranges), or with none chosen at
// the as-shot light, snapped as above.
[[nodiscard]] WhiteBalance slider_white_balance(const EditSettings& s,
                                                const std::optional<Chromaticity>& as_shot_white);

// The Kelvin slider is even in mireds (a million over the Kelvin), the scale on which equal steps
// look about equally big: position 0 is kelvin_range.min (2000 K), 1 is kelvin_range.max
// (15000 K), and the middle is their mired midpoint, about 3529 K. Both clamp; the ends are exact.
[[nodiscard]] double kelvin_slider_position(double kelvin) noexcept;
[[nodiscard]] double kelvin_at_slider_position(double position) noexcept;

// Sets one of the two numbers, snapped (snap_kelvin, snap_tint), and turns the stage on. The other
// number stays as chosen; with nothing chosen yet it is taken from the as-shot light, snapped.
// Setting a slider to where it already sits on an As shot photo leaves it As shot.
void set_white_balance(EditSettings& s, WhiteBalanceControl control, double value,
                       const std::optional<Chromaticity>& as_shot_white);

// Back to As shot exactly (no white balance), with the stage on.
void reset_white_balance(EditSettings& s);

// The White balance row's value, on the heading line: "As shot" until a light is chosen, then
// "Chosen". The numbers are beside the two sliders (format_kelvin, format_tint), not here.
[[nodiscard]] std::string white_balance_value_text(const EditSettings& s);

// The line under the heading: "The light the photo was lit by", and for a file that records no
// light (JPEG, PNG, HEIC, some RAW) "Light not recorded, taken as 6500 K", the light the numbers
// are measured from.
[[nodiscard]] std::string white_balance_subtitle(const std::optional<Chromaticity>& as_shot_white);

// --- Tone curve ----------------------------------------------------------------------------------

inline constexpr CurveKind curve_kinds[] = {CurveKind::none, CurveKind::gamma, CurveKind::reinhard,
                                            CurveKind::filmic};

// "None", "Gamma", "Reinhard", "Filmic".
[[nodiscard]] std::string_view curve_kind_name(CurveKind kind) noexcept;

// The one number a curve takes, with the range its slider offers.
struct CurveParameter {
    std::string_view label;
    float min = 0.0f;
    float max = 0.0f;
    float default_value = 0.0f;
    bool logarithmic = false;  // the slider moves in ratios, not steps
};

// The parameter of `kind`, or nothing for the curves without one (None and Filmic).
[[nodiscard]] std::optional<CurveParameter> curve_parameter(CurveKind kind) noexcept;

// The parameter's current value for s.curve (which has one), clamped to its slider's range.
[[nodiscard]] float curve_parameter_value(const EditSettings& s);

// Where `value` sits on the parameter's slider, from 0 at min to 1 at max (clamped). A logarithmic
// slider moves in ratios: for Reinhard's white point 1..16, 2 sits at 0.25 and 4 at the middle.
[[nodiscard]] float curve_slider_position(const CurveParameter& p, float value) noexcept;

// The value at `position` on the parameter's slider (clamped to [0, 1]): the inverse of
// curve_slider_position, exact at both ends.
[[nodiscard]] float curve_value_at(const CurveParameter& p, float position) noexcept;

// Chooses the curve. Both parameters keep their values, so switching back restores them.
void set_curve_kind(EditSettings& s, CurveKind kind);

// Sets the parameter of s.curve, clamped to its range (NaN gives the default). Does nothing for a
// curve without one.
void set_curve_parameter(EditSettings& s, float value);

// --- Viewing vs. editing -------------------------------------------------------------------------

// What a picture of `stage` is called on screen: "Original" for Decode (the file as it came, before
// any edit), otherwise "after Exposure", "after Colour matrix", and so on.
[[nodiscard]] std::string stage_caption(StageId stage);

// The compare stage that counts: none if there is none or it is the viewed stage itself (comparing
// a stage with itself shows nothing new, so it is the same as Off).
[[nodiscard]] std::optional<StageId> effective_compare(StageId viewed,
                                                       std::optional<StageId> compare) noexcept;

// Where the Light path's view stands once a photo is open: every route that opens one (File > Open,
// the picker, a drop, the command line) starts at the first row, Decode, with no comparison, whatever
// the previous photo was viewing and whether a recipe is loaded with the new one.
struct ViewAfterOpen {
    StageId viewed = StageId::decode;
    std::optional<StageId> compare;

    bool operator==(const ViewAfterOpen&) const = default;
};
[[nodiscard]] ViewAfterOpen view_after_open() noexcept;

// The furthest stage that either picture shows: the later of `viewed` and `compare`.
[[nodiscard]] StageId furthest_shown(StageId viewed, std::optional<StageId> compare = {}) noexcept;

// True when `stage` comes after the furthest stage on screen (`viewed`, or `compare` if that is
// later): no picture shows what it does, whatever it is set to.
[[nodiscard]] bool stage_after_view(StageId stage, StageId viewed,
                                    std::optional<StageId> compare = {}) noexcept;

// Whether the white balance picker can be used: only where White balance is visible in what is
// shown, which is the same rule as the stage's own (not stage_after_view). While viewing Decode it
// is not, since the picture there is before any white balance.
[[nodiscard]] bool pick_available(StageId viewed, std::optional<StageId> compare = {}) noexcept;

// Whether the canvas shows its quiet note about Decode: only while Decode is the viewed stage and
// no comparison is on (a compare against the viewed stage itself is no comparison). A RAW looks dark
// and flat there, so the note says why.
[[nodiscard]] bool decode_chip_shown(StageId viewed, std::optional<StageId> compare = {}) noexcept;

// The note's words: "Showing Decode — the file's light before any edit. Pick a later stage to see
// more."
[[nodiscard]] std::string_view decode_chip_text() noexcept;

// "Not visible here: you're viewing after Exposure." While comparing, the same for the furthest
// stage shown: "Not visible here: the furthest stage shown is after Exposure."
[[nodiscard]] std::string view_hint(StageId viewed, std::optional<StageId> compare = {});

// What Esc does to the picture. All the flags describe the frame before the key arrived: by the time
// the app sees the key, Dear ImGui has already used it to close a popup or end a drag.
struct EscapeContext {
    bool brushing = false;       // Brush mode (Dodge & burn) is on
    bool picking = false;        // Pick mode (the white balance picker) is on
    bool sampling = false;       // Sample mode is on
    bool test_strip = false;     // the exposure test strip is on
    bool comparing = false;      // a compare stage is in effect (see effective_compare)
    bool viewing_earlier = false;  // the viewed stage is not the final picture
    bool text_editing = false;   // a text field (or a slider typed into) has the keyboard
    bool popup_open = false;     // a popup, menu or combo list was open
    bool item_active = false;    // a slider was being dragged
    // Preferences, About, Third-party notices, Recipe not loaded or View recipe has the focus.
    bool window_focused = false;
};

enum class EscapeAction {
    none,           // Esc belongs to something else, or there's nothing to back out of
    close_window,   // close the focused window (see EscapeContext::window_focused)
    leave_brush,    // leave Brush mode
    leave_pick,     // leave Pick mode
    clear_sample,   // leave Sample mode and clear its pin, in one press
    clear_test_strip,  // turn the test strip off, keep the viewed stage
    clear_compare,  // first Esc: drop the comparison, keep the viewed stage
    show_final,     // second Esc: back to the final picture
};

// Esc backs out one level, whatever widget was used last, but never while it is closing a popup
// (the Saving modal is one, so Esc can't dismiss it), ending a text edit or cancelling a drag. A
// focused closable window takes the Esc itself, and then leaves Sample mode, the test strip, the
// comparison and the viewed stage alone. Then, one level per press: Brush mode, Pick mode, Sample
// mode, the test strip, the comparison, the viewed stage.
[[nodiscard]] EscapeAction escape_action(const EscapeContext& c) noexcept;

}  // namespace prism::gui

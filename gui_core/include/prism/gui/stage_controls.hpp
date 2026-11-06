#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "prism/gui/settings.hpp"

namespace prism::gui {

// What the pipeline view shows and edits in each stage's row. Everything the window needs to know
// about those controls is here, so it can be tested without one.
//
// Exposure, Saturation and Contrast are edited with the same three sliders as Simple's Brightness,
// Colour and Contrast (same range, same conversions in settings.hpp), so the two can never
// disagree: a stage's slider is Simple's slider for that field, and apply_simple_control changes
// only that field.

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

// The value to show in `stage`'s row: format_exposure / format_saturation / format_multiplier for
// the three sliders, the matrix file's name ("none" without a matrix, "recipe matrix" for one a
// recipe gave without naming a file: only a label, the path stays empty), and "none", "gamma 2.2",
// "Reinhard, white 4" or "filmic" for the curve. Empty for Decode and Output encode, which have nothing to edit.
// Shows the stored value even when the stage is off (the row greys it out).
[[nodiscard]] std::string stage_value_text(const EditSettings& s, StageId stage);

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

// The furthest stage that either picture shows: the later of `viewed` and `compare`.
[[nodiscard]] StageId furthest_shown(StageId viewed, std::optional<StageId> compare = {}) noexcept;

// True when `stage` comes after the furthest stage on screen (`viewed`, or `compare` if that is
// later): no picture shows what it does, whatever it is set to.
[[nodiscard]] bool stage_after_view(StageId stage, StageId viewed,
                                    std::optional<StageId> compare = {}) noexcept;

// True when `stage` changes the photo but comes after the furthest stage on screen (`viewed`, or
// `compare` if that is later), so no picture shows it: the stage is on and not neutral
// (exposure 0, saturation 1, contrast 1, no matrix or identity matrix, no curve). Stages up to and
// including that one are never hidden, and Decode and Output encode never are.
[[nodiscard]] bool hidden_by_view(const EditSettings& s, StageId stage, StageId viewed,
                                  std::optional<StageId> compare = {});

// "Not visible here: you're viewing after Exposure." While comparing, the same for the furthest
// stage shown: "Not visible here: the furthest stage shown is after Exposure."
[[nodiscard]] std::string view_hint(StageId viewed, std::optional<StageId> compare = {});

// What Esc does to the picture. All the flags describe the frame before the key arrived: by the time
// the app sees the key, Dear ImGui has already used it to close a popup or end a drag.
struct EscapeContext {
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
    clear_sample,   // leave Sample mode and clear its pin, in one press
    clear_test_strip,  // turn the test strip off, keep the viewed stage
    clear_compare,  // first Esc: drop the comparison, keep the viewed stage
    show_final,     // second Esc: back to the final picture
};

// Esc backs out one level, whatever widget was used last, but never while it is closing a popup
// (the Saving modal is one, so Esc can't dismiss it), ending a text edit or cancelling a drag. A
// focused closable window takes the Esc itself, and then leaves Sample mode, the test strip, the
// comparison and the viewed stage alone. Then, one level per press: Sample mode, the test strip, the
// comparison, the viewed stage.
[[nodiscard]] EscapeAction escape_action(const EscapeContext& c) noexcept;

}  // namespace prism::gui

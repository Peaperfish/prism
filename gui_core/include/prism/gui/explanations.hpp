#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "prism/gui/clipping.hpp"
#include "prism/gui/histogram.hpp"
#include "prism/gui/preferences.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/recipe_ui.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/gui/theme_tokens.hpp"

namespace prism::gui {

// What the Explain area says about a control, in plain words (#16). One entry per control: a
// summary (one line, also the tooltip), a detail (what it does to the light in Prism's pipeline)
// and, for some, a live line computed from the current settings with the engine's own maths.
// No ImGui here, so the texts and the numbers are tested without a window.

inline constexpr std::size_t explain_summary_max = 70;
inline constexpr std::size_t explain_detail_max = 220;

// How a live line is made. Which settings it needs is in live_line().
enum class LiveKind {
    none,
    look,         // the current look: preset name, exposure, colour, contrast, curve
    brightness,   // Simple's Brightness: slider position, stops, the factor on the light
    contrast,     // Simple's Contrast
    colour,       // Simple's Colour
    exposure,     // the Exposure row: stops and factor
    matrix,       // the matrix file's name, or none
    saturation,   // the Saturation row
    contrast_row, // the Contrast row
    curve,        // the chosen curve with its number and what it does to 18% grey
    gamma,        // gamma's own number (whichever curve is chosen)
    reinhard,     // Reinhard's own white point
    filmic,       // filmic's fixed grey-card result
    white,        // the white point alone
    viewing,      // which stage the scopes and histogram show
    over,         // a chip: the share of channel values above white (needs the histogram)
    under,        // a chip: the share below black
    clip_note,    // the whole clipping sentence (needs the clip report)
    compare,      // the two halves' captions
    strip,        // how much light one band step is
    recipe,       // which stages the loaded recipe sets (needs the loaded recipe)
    preview_size, // the preview's size against the file's (needs both)
    sample,       // the sampled pixel's readout (needs the trace)
};

struct PixelTrace;

struct Explanation {
    std::string_view id;
    std::string_view summary;
    std::string_view detail;
    LiveKind live = LiveKind::none;
};

// Every entry, in display order.
[[nodiscard]] std::span<const Explanation> explanations() noexcept;

// The entry for `id` ("simple.brightness", "stage.curve", "look.vivid", ...), or nullptr.
[[nodiscard]] const Explanation* explanation_for(std::string_view id) noexcept;

// The height of the Explain area while explanations are on: room for explain_max_lines lines of
// Small text (`small_line` is one line's height) plus the padding, border and gaps, all already
// scaled to the interface size, and never more than 45% of a very short panel (`panel_height`).
// It does not depend on which entry is shown: a height that followed the text made the scrolling
// panel above it grow and shrink as the cursor crossed the controls, and at the bottom of the
// panel that moved the content under a still cursor (#38). A longer text scrolls inside the area.
[[nodiscard]] float explain_area_height(const TokenMetrics& metrics, float small_line,
                                        float panel_height) noexcept;

// The ids of the controls that have their own enum or list...
[[nodiscard]] std::string_view explain_id(StageId stage) noexcept;           // "stage.exposure"
[[nodiscard]] std::string_view explain_id(SimpleControl control) noexcept;   // "simple.brightness"
[[nodiscard]] std::string_view explain_id(CurveKind kind) noexcept;          // "curve.filmic"
[[nodiscard]] std::string_view explain_id(ScopeChoice scope) noexcept;       // "scope.parade"
[[nodiscard]] std::string explain_id(const Preset& preset);                  // "look.vivid"

// ... and the others (the look menu itself, recipes, chips, clipping, compare, the strip,
// Preferences).
[[nodiscard]] std::span<const std::string_view> other_explain_ids() noexcept;

// Named ids the window uses, so a typo is a compile error rather than a missing explanation.
namespace explain {
inline constexpr std::string_view look = "look";
inline constexpr std::string_view look_reset = "look.reset";
inline constexpr std::string_view look_view_recipe = "look.view_recipe";
inline constexpr std::string_view recipe_load = "recipe.load";
inline constexpr std::string_view recipe_save = "recipe.save";
inline constexpr std::string_view recipe_loaded = "recipe.loaded";
inline constexpr std::string_view recipe_name = "recipe.name";
inline constexpr std::string_view recipe_description = "recipe.description";
inline constexpr std::string_view curve_white = "curve.white";
inline constexpr std::string_view curve_gamma_value = "curve.gamma_value";
inline constexpr std::string_view scope_skin = "scope.skin";
inline constexpr std::string_view chip_over = "chip.over";
inline constexpr std::string_view chip_under = "chip.under";
inline constexpr std::string_view clip_overlay = "clip.overlay";
inline constexpr std::string_view clip_dot = "clip.dot";
inline constexpr std::string_view clip_summary = "clip.summary";
inline constexpr std::string_view clip_note = "clip.note";
inline constexpr std::string_view compare = "compare";
inline constexpr std::string_view compare_swap = "compare.swap";
inline constexpr std::string_view viewing = "viewing";
inline constexpr std::string_view strip = "strip";
inline constexpr std::string_view strip_line = "strip.line";
inline constexpr std::string_view view_zoom = "view.zoom";
inline constexpr std::string_view view_fit = "view.fit";
inline constexpr std::string_view view_actual = "view.actual";
inline constexpr std::string_view sample = "sample";
inline constexpr std::string_view sample_values = "sample.values";
inline constexpr std::string_view sample_light = "sample.light";
inline constexpr std::string_view sample_range = "sample.range";
inline constexpr std::string_view pref_theme = "pref.theme";
inline constexpr std::string_view pref_size = "pref.size";
inline constexpr std::string_view pref_panel = "pref.panel";
inline constexpr std::string_view pref_explain = "pref.explain";
}  // namespace explain

// What a live line may need besides the settings. Anything missing gives an empty line for the
// entries that need it.
struct LiveContext {
    InputKind kind = InputKind::image;
    StageId viewed = final_stage;
    std::optional<StageId> compare;  // as effective_compare: none if there is no comparison
    const StageHistogram* histogram = nullptr;
    const ClipReport* clips = nullptr;
    StripStep strip_step = strip_default_step;
    const LoadedRecipe* recipe = nullptr;  // the recipe loaded onto the photo, if any
    int preview_long_side = 0;             // the preview's longer side, in pixels; 0 if no photo
    int file_long_side = 0;                // the file's (see Session::file_width)
    bool file_size_exact = true;           // false: file_long_side is an estimate
    const PixelTrace* sample = nullptr;    // the pixel the sampler is reading, if any
};

// The live line for `id` with these settings: numbers worked out with the engine's own maths
// (exposure's 2^stops, contrast's curve, saturation's scale, the tone curves), e.g.
//   simple.brightness at +35:  "+35: +0.70 stops, 1.62× the light"
//   simple.contrast at +50:    "+50: ×1.22, each stop near middle grey is 22% wider"
//   curve.gamma at 2.2:        "gamma 2.2: 18% grey becomes 46%"
//   view.zoom on a 61 MP RAW:  "Preview 2560 px, file 9566 px on the long side: each preview pixel
//                               stands for about 3.7 file pixels across"
// Empty for an unknown id and for entries without one. The slider positions are derived from the
// settings (simple_from), so a stage that is off reads as 0 here exactly as on its slider.
[[nodiscard]] std::string live_line(std::string_view id, const EditSettings& settings,
                                    const LiveContext& context = {});

// What a look does, in the words of the line under the Look menu. Only what changes the picture is
// named, in pipeline order; a stage that is off counts as not changing it:
//   Vivid:              "+1.00 stops, colour ×1.25, contrast ×1.15, filmic"
//   saturation 0:       "+1.00 stops, black and white, contrast ×1.20, filmic"
//   Reinhard:           "+1.00 stops, colour ×0.90, contrast ×0.85, Reinhard (white 4)"
//   exposure 0, no curve: "colour ×1.20, contrast ×1.10"
//   nothing at all:     "no adjustments"
[[nodiscard]] std::string look_values_line(const EditSettings& settings);

// `text` with each {kind.id.field} token replaced by that look's number, formatted as the live
// lines do: {raw.vivid.contrast} -> "×1.15", {raw.vivid.exposure} -> "+1.00 stops". The fields are
// exposure, saturation and contrast. Throws std::logic_error for a token that names nothing.
[[nodiscard]] std::string expand_look_text(std::string_view text, std::span<const Preset> raw,
                                           std::span<const Preset> image);

// "×1.20": a multiplier to two places, with the multiplication sign (new text uses × not x).
[[nodiscard]] std::string format_times(float value);

// The fraction of middle grey 0.18 a tone curve gives, as whole percent: "46%".
[[nodiscard]] std::string grey_card_percent(CurveKind kind, const EditSettings& settings);

}  // namespace prism::gui

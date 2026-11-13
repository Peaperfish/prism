#include "prism/gui/explanations.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "prism/gui/export_size.hpp"
#include "prism/gui/framing.hpp"
#include "prism/gui/paper_grade.hpp"
#include "prism/gui/pixel_sample.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/pipeline.hpp"

namespace prism::gui {

namespace {

// The texts are in the order they appear in the window. The draft they come from is the notes for
// #16; numbers in a text are checked against the code by explanations_test.cpp, and live numbers
// are never typed here. A look's numbers in a detail are tokens, such as {raw.vivid.contrast}:
// expanded() below fills them in from the recipes, with expand_look_text.
constexpr Explanation templates[] = {
    // --- Look ------------------------------------------------------------------------------
    {"look", "A look: three numbers and a curve. Keeps your dodging and burning.",
     "Picking one sets Exposure, Saturation, Contrast and the Tone curve in the pipeline below; "
     "nothing is hidden. Change any of them and the name becomes Custom. Dodging and burning stay "
     "as they are.",
     LiveKind::look},
    {"look.natural", "A stop brighter with a filmic curve.",
     "RAW files are linear, so they look dark and flat as decoded. {raw.natural.exposure} and the "
     "filmic curve give a neutral starting point."},
    {"look.original", "Nothing applied.",
     "The image as decoded: for RAW, linear light as the sensor recorded it (dark and flat); for "
     "JPEG and PNG, the photo as it is."},
    {"look.vivid", "More colour and contrast.",
     "Saturation {raw.vivid.saturation} and contrast {raw.vivid.contrast} for RAW "
     "({image.vivid.saturation} and {image.vivid.contrast} for JPEG and PNG). RAW also gets "
     "{raw.vivid.exposure} and the filmic curve."},
    {"look.soft", "Gentler contrast, muted colour.",
     "Saturation {raw.soft.saturation} and contrast {raw.soft.contrast} for RAW "
     "({image.soft.saturation} and {image.soft.contrast} for JPEG and PNG). For RAW the Reinhard "
     "curve rolls highlights off more softly than filmic."},
    {"look.bw", "Greyscale with a little extra contrast.",
     "Saturation 0 removes colour but keeps each pixel's luminance. Contrast {raw.bw.contrast} "
     "(RAW) or {image.bw.contrast} (JPEG, PNG) stops the grey looking flat."},
    {"look.reset", "Go back to the starting look.",
     "Sets the same four values as the first look in the list. One undo step."},
    {"look.view_recipe", "Read this look as the recipe file it is.",
     "Opens the file built into Prism, the same format as a recipe you save. Select or copy any "
     "of it. Reading it changes nothing, and it stays open if you change the photo's settings."},

    // --- Recipes -----------------------------------------------------------------------------
    {"recipe.load", "Apply a .prism recipe to this photo.",
     "Sets each stage the recipe has, with its switch, and leaves the others as they are. One "
     "undo step. The photo is not decoded again."},
    {"recipe.loaded", "The recipe last loaded onto this photo.",
     "Reads (modified) once you change a setting, and goes away if you undo past loading it. The "
     "Look menu names a built-in look only when the numbers match one.",
     LiveKind::recipe},
    {"recipe.save", "Write these settings to a recipe you can read and reuse.",
     "Saves all five stages with their switches, and a white balance you chose, as a small .prism "
     "text file. The photo and the RAW decoding options are not in it. Load it on any photo, or "
     "read it in a text editor."},
    {"recipe.name", "The recipe's name, shown when it is loaded.",
     "Written as name in the file, and used for the file name. Prism suggests the recipe you "
     "loaded or the look you picked."},
    {"recipe.description", "One optional line on what the recipe is for.",
     "Written as description in the file, and shown when you point at the recipe's name after "
     "loading it. Prism suggests the one the recipe or look has. Leave it empty to write none."},
    {"export.size", "Export at the photo's size, or smaller.",
     "Long side sets the longer side in pixels, Percent a share of the photo's own size. The "
     "photo is reduced by averaging the light in each area, and never enlarged. Prism remembers "
     "your choice."},
    {"export.value", "The number of pixels or percent to export at.",
     "Long side takes any whole number from 1 up; Percent takes 1 to 99. A number outside that "
     "range is moved to the nearest one when you leave the field."},

    // --- Pipeline stages ---------------------------------------------------------------------
    {"stage.decode", "The file turned into linear light.",
     "JPEG and PNG have their sRGB curve undone. RAW is demosaiced with the camera's white "
     "balance as shot. After this, numbers are proportional to light."},
    {"stage.exposure", "Same-sized change to all light, in stops.",
     "Multiplies every value by 2 to the power of the stops. Because it works on linear light, +1 "
     "stop is exactly twice as bright.",
     LiveKind::exposure},
    {"stage.dodge_burn", "Hold back or add light in parts of the picture.",
     "Dodging holds light back from a part, as a card under the enlarger does; burning gives it "
     "more time. Each stroke moves the light there by its strength, in stops, right after "
     "Exposure. Recipes don't keep strokes."},
    {"stage.white_balance", "The light the photo was lit by, in Kelvin and tint.",
     "Kelvin is the colour of the light: higher means bluer, so the photo is warmed. Tint is "
     "green–magenta: higher means a greener light, so more magenta. The heading reads As shot or "
     "Chosen.",
     LiveKind::white_balance},
    {"stage.matrix", "Mixes red, green and blue, before saturation and tone.",
     "A 3×3 table: each output channel is a weighted sum of the input red, green and blue, in "
     "linear light. Used to calibrate camera colour or apply a grade. Load one with a recipe.",
     LiveKind::matrix},
    {"stage.saturation", "Colourfulness, the pixel's luminance unchanged.",
     "Each channel moves away from the pixel's luminance (0.2126 R + 0.7152 G + 0.0722 B). A "
     "colour that would go below zero stops at the most saturated version sRGB can show, so hues "
     "do not shift.",
     LiveKind::saturation},
    {"stage.contrast", "A paper grade: 2 is normal, lower is softer, higher is harder.",
     "Each grade is a fixed contrast step, modelled on a real darkroom paper. It is an S-curve "
     "around middle grey, in stops. Recipes and --contrast store its multiplier.",
     LiveKind::contrast_row},
    {"stage.curve", "Squeezes scene light to fit the screen.",
     "Linear light has more range than a screen shows. The curve compresses it: bright values are "
     "pulled in, so highlights have room. Pick a kind below.",
     LiveKind::curve},
    {"stage.encode", "Light turned into the 8-bit sRGB file.",
     "Applies the sRGB curve and rounds to 8 bits. Anything still above white or below black is "
     "cut off here, and cannot come back."},

    // --- Tone curve kinds --------------------------------------------------------------------
    {"curve.none", "No curve.",
     "Values go straight to Output encode. Right for JPEG and PNG, which already have one. For "
     "RAW the picture is dark and flat."},
    {"curve.gamma", "Raises midtones with a power curve.",
     "out = in^(1/gamma). 0 and 1 stay fixed. Above 1 brightens midtones, below 1 darkens. "
     "Highlights above 1 are not compressed; they clip.",
     LiveKind::gamma},
    {"curve.reinhard", "Rolls highlights off smoothly.",
     "out = in × (1 + in/white²) / (1 + in). The white point is the linear value that lands "
     "exactly on white; brighter values clip. A larger white point keeps more highlight detail "
     "but looks flatter.",
     LiveKind::reinhard},
    {"curve.filmic", "Soft toe, soft shoulder (Hable).",
     "The “Uncharted 2” curve with fixed constants and a built-in 2× boost to its input. Darkens "
     "deep shadows gently and compresses highlights; scene value 5.6 maps to white. No setting.",
     LiveKind::filmic},
    {"curve.white", "Where highlights stop compressing.",
     "Reinhard only. The linear value that becomes pure white. Range 1 to 16, default 4.",
     LiveKind::white},
    {"curve.gamma_value", "The exponent of the gamma curve.", "Range 0.5 to 4, default 2.2.",
     LiveKind::gamma},

    // --- Histogram and scopes ----------------------------------------------------------------
    {"scope.histogram", "How many pixels sit at each level, from black to white.",
     "Black on the left, white on the right, at the stage you are viewing. Grey is luminance; red, "
     "green and blue overlap towards white. A corner triangle lights when any pixel is out of "
     "range at that end.",
     LiveKind::viewing},
    {"scope.parade", "Each colour channel's level, side by side.",
     "Left to right across the photo, red, green and blue panels show the level of that channel "
     "at each column. Equal heights mean neutral. The lines mark 5%, 18% and 50% of white's light.",
     LiveKind::viewing},
    {"scope.waveform", "How much light each part of the photo holds.",
     "Horizontal position matches the photo; the vertical axis is Rec. 709 luma, 0 at the bottom "
     "and 255 at the top. A stronger trace means more pixels there. The lines mark 5%, 18% and 50% "
     "of white's light.",
     LiveKind::viewing},
    {"scope.vectorscope", "Which colours are present and how strong.",
     "Angle is hue and distance from the centre is strength (Rec. 709 Cb and Cr). Centre is "
     "neutral grey, and each dot is drawn in its own hue. The line marks where skin tones usually "
     "sit.",
     LiveKind::viewing},
    {"scope.skin", "Skin tones usually sit near this line.",
     "The skin-tone line, 123° from the Cb axis: skin of any complexion lands close to it. Skin "
     "far from the line suggests a colour cast."},
    {"chip.over", "Pixels brighter than white.",
     "Share of pixels with any channel above 1 at this stage (at Decode, already at the file's "
     "ceiling). The red, green and blue figures count each channel alone.",
     LiveKind::over},
    {"chip.under", "Pixels darker than black.",
     "Share of pixels with no light left at this stage: a channel is below 0 and the pixel is black "
     "or darker. A pixel with light and a channel below 0 is outside sRGB, counted apart.",
     LiveKind::under},

    // --- Clipping ----------------------------------------------------------------------------
    {"clip.overlay", "Mark clipped pixels on the photo (J).",
     "Orange-red: brighter than white. Blue: no light left. Violet stripes: too saturated for "
     "sRGB. A pixel an earlier stage already cut off stays marked in later stages."},
    {"clip.dot", "Filled dot: detail lost. Hollow: recoverable.",
     "Out-of-range values are kept as numbers until a stage cuts them off at 0 or 1. Hollow "
     "means a later stage can still pull them back."},
    {"clip.summary", "Where clipping starts and where it costs detail.",
     "Detail is lost only where a stage cuts values off at white or black. Until then the values "
     "are kept, so a later stage can still bring them back.",
     LiveKind::clip_note},
    {"clip.note", "Clipping in the photo, in one line.",
     "Dot colours as in the pipeline: orange above white, blue below black, violet too saturated "
     "for sRGB. It follows your edits and settles a moment after you stop dragging.",
     LiveKind::clip_note},

    // --- Compare and the viewed stage --------------------------------------------------------
    {"light_path", "Every stage the light goes through, in order.",
     "Click a row to see the picture after that stage. Right-click a slider to reset it. "
     "Ctrl-click (Cmd-click on Mac) to type a value in its line's unit: stops, or a factor "
     "such as 1.2.",
     LiveKind::none},
    {"compare", "Show two points in the pipeline side by side.",
     "The stage you click in the Light path is the right half; Compare picks the left half. "
     "Original is the file before any edit.",
     LiveKind::compare},
    {"compare.swap", "Swap the two halves.",
     "The stage on the left becomes the one you view on the right, and the other way round. Only "
     "the view changes, not the photo."},
    {"viewing", "The photo shows the pipeline up to this stage.",
     "Edits made after it are real, but not visible here. Click Output encode (or press Esc) to "
     "see the final result."},

    // --- Test strip --------------------------------------------------------------------------
    {"strip", "Try several exposures at once (T).",
     "Splits the photo into bands, each a fixed number of stops from now. Click a band to take "
     "its exposure. Steps of 1/3, 1/2 or 1 stop.",
     LiveKind::strip},
    {"strip.line", "A full stop doubles the light.",
     "Each band is one step more or less light than its neighbour. A third of a stop is about "
     "1.3×, half a stop about 1.4×, a whole stop 2×."},

    // --- Zoom --------------------------------------------------------------------------------
    {"view.zoom", "How large the photo is on the screen, in % of its own pixels.",
     "Pinch or use the wheel to zoom; scroll or drag to move. 100% is one pixel of the photo on "
     "each pixel of your screen, up to 800%. Zoomed in, the photo's own pixels replace the preview "
     "where they can.",
     LiveKind::preview_size},
    {"view.fit", "Show the whole photo (F).",
     "The picture fills the canvas as far as it can and follows the window as it changes size. "
     "Only the view changes, not the photo."},
    // The detail starts with real_pixels_explain() (detail_view.hpp), word for word.
    {"view.actual", "One pixel of the photo on each pixel of your screen (1).",
     "100%: one pixel of the photo on each pixel of your screen, the photo at its full size. On a "
     "Retina screen a photo pixel is half a point across. Greyed out when the whole photo already "
     "fits above 100%."},

    // --- Sample colour -------------------------------------------------------------------------
    {"sample", "Read one pixel's values at the viewed stage (I).",
     "Point at the photo to read a pixel, click to pin it. Dragging still moves the picture, and "
     "a click on the strip still sets the exposure. Esc leaves. Values are the linear numbers, "
     "not the 8-bit picture.",
     LiveKind::sample},
    {"sample.values", "The pixel's red, green and blue, in linear light.",
     "1.00 is white and 0 is black. Values above 1 or below 0 stay as numbers until a stage cuts "
     "them off. Output encode also shows the 8-bit sRGB codes the file gets."},
    {"sample.light", "How much light the pixel holds, and how far from middle grey.",
     "A share of white's light (0.2126 R + 0.7152 G + 0.0722 B) and stops from middle grey (18%). "
     "After the Tone curve 18% is no longer a grey card, so the stops are the scene's, from "
     "Contrast."},
    {"sample.range", "Whether the pixel is out of range, and what becomes of it.",
     "Over white, no light, or too saturated for sRGB: the same test as the clipping marks. It "
     "names the later stage that brings a value back or clips it. A preview pixel is an average, "
     "so a small clip can hide."},

    // --- Pick neutral --------------------------------------------------------------------------
    {"pick", "Click something grey to set the white balance (W).",
     "Click a patch that should be neutral. Prism averages 5 by 5 pixels there and sets Kelvin and "
     "tint so it comes out grey. A spot that is too dark or clipped is refused. Esc leaves."},

    // --- Dodge & burn --------------------------------------------------------------------------
    {"dodge_burn.switch", "Turn the dodging and burning off and on.",
     "Off, the strokes stay on the photo but change no light, so you can judge the picture without "
     "them. On brings them back."},
    {"dodge_burn.brush", "Paint where to hold back or add light (B).",
     "While it is on, a drag on the photo paints a stroke with the strength set here; Space and a "
     "drag moves the picture. Only from Dodge & burn on. Esc leaves; so do B, I and T, and W from "
     "White balance on."},
    {"dodge_burn.tone", "Dodge holds light back; burn gives more (X).",
     "Alt (Option) at the press paints that one stroke the other way. ] and [ make the brush "
     "bigger and smaller, Shift+[ and Shift+] its edge softer and harder. Hold M to see the "
     "strokes."},
    {"dodge_burn.strength", "How many stops one stroke moves the light.",
     "From 0.1 to 2 stops, in steps of 0.1. A stroke of 1 stop moves the light as much as doubling or "
     "halving the time. It belongs to the brush: undo leaves it, and it isn't saved."},
    {"dodge_burn.clear", "Take every stroke off the photo.",
     "Removes all the dodging and burning in one undo step. Greyed out when there is none."},

    // --- The easel (#58) ------------------------------------------------------------------------
    {"easel", "Easel: frames and straightens the print (C).",
     "It moves no light, so every stage sees only the framed part. Click the line or press C to "
     "frame; Return, Done, C or Esc leaves and keeps the frame. Not saved in recipes.",
     LiveKind::frame},
    {"framing.straighten", "Turns the picture under the blades, −45° to +45°.",
     "Turns the easel under the enlarger: the picture turns about the frame's centre, and the "
     "blades close in so no corner runs past the photo's edge.",
     LiveKind::frame},
    {"framing.angle", "How far the picture is turned, in words.",
     "Turning makes each pixel from the 16 around it, in linear light; at 0° no pixel moves. "
     "Ctrl-click the slider (Cmd-click on Mac) to type one, such as 1.2° anticlockwise."},
    {"framing.aspect", "The shape the blades keep.",
     "Corners keep the shape; dragging an edge makes it Free. Each shape follows the photo's "
     "orientation, and X turns it between landscape and portrait."},
    {"framing.turn_aspect", "Turns the shape between landscape and portrait (X).",
     "4:5 becomes 5:4 about the frame's centre, as far as the photo allows."},
    {"framing.reset", "Upright again, with the largest frame of the shape.",
     "Back to 0°, and the blades open as far as the chosen shape allows: the whole photo for "
     "Original and Free. One undo step."},
    {"framing.done", "Leaves the easel and keeps the frame (Return).",
     "The framed picture is rendered once, and every stage, the scopes and Sample see only it. "
     "Esc during a drag cancels just that drag.",
     LiveKind::frame},

    // --- Preferences -------------------------------------------------------------------------
    {"pref.theme", "Light, dark, or follow the system.",
     "The photo canvas stays neutral grey in both so your eyes judge the photo, not the frame."},
    {"pref.size", "Scales text and controls.",
     "Auto follows the display. Also Cmd + and Cmd − to step, Cmd 0 for Auto (Ctrl on Windows and "
     "Linux)."},
    {"pref.panel", "Width of the side panel.",
     "Drag the panel's left edge to change it too. It never gets narrower than the panel's texts "
     "need."},
    {"pref.explain", "Show a plain-language note for the control you point at.",
     "Off removes the Explain area at the foot of the panel. Tooltips still show the one-line "
     "summary."},
};

// Ids that have no enum of their own: the controls the window shows beyond the stages, the curve
// kinds, the scopes and the presets.
constexpr std::array<std::string_view, 49> other_ids{
    explain::look,         explain::look_reset,   explain::look_view_recipe,
    explain::recipe_load,  explain::recipe_save,
    explain::recipe_loaded, explain::recipe_name, explain::recipe_description,
    explain::export_size,  explain::export_value,
    explain::curve_white,  explain::curve_gamma_value,
    explain::scope_skin,   explain::chip_over,    explain::chip_under,  explain::clip_overlay,
    explain::clip_dot,     explain::clip_summary, explain::clip_note,   explain::compare,
    explain::compare_swap, explain::viewing,      explain::light_path,  explain::strip,
    explain::strip_line,
    explain::view_zoom,    explain::view_fit,     explain::view_actual,
    explain::sample,       explain::sample_values, explain::sample_light, explain::sample_range,
    explain::pick,
    explain::dodge_burn_switch, explain::dodge_burn_brush, explain::dodge_burn_tone,
    explain::dodge_burn_strength, explain::dodge_burn_clear,
    explain::easel,        explain::framing_straighten, explain::framing_angle,
    explain::framing_aspect, explain::framing_turn_aspect, explain::framing_reset,
    explain::framing_done,
    explain::pref_theme,   explain::pref_size,    explain::pref_panel,  explain::pref_explain,
};

std::string printf_string(const char* format, double a) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, a);
    return buffer;
}

// 18% grey through one of the engine's own tone curves, as a whole percent.
int grey_card(const EditSettings& s, CurveKind kind) {
    EditSettings curve = s;
    curve.curve = kind;
    curve.curve_on = true;
    const std::optional<std::string> spec = curve_spec(curve);
    if (!spec) {
        return 18;
    }
    const ToneCurve tone = parse_curve(*spec);
    return static_cast<int>(std::lround(static_cast<double>(tone.map(contrast_pivot)) * 100.0));
}

std::string number_text(float v) {
    return printf_string("%g", static_cast<double>(v));
}

// An off stage does nothing: say so, and what it would say once on.
std::string if_off(const EditSettings& s, StageId stage, const std::string& when_on) {
    if (stage_enabled(s, stage)) {
        return when_on;
    }
    return "Off, so no change. When on: " + when_on;
}

// "5330 K, tint +27": a light, rounded as the White balance row rounds it.
std::string white_balance_light_text(const WhiteBalance& wb) {
    return format_kelvin(wb.kelvin) + ", tint " + format_tint(wb.tint);
}

std::string stops_and_factor(float stops) {
    return format_exposure(stops) + ", " +
           printf_string("%.2f", static_cast<double>(std::exp2(stops))) + "× the light";
}

std::string contrast_text(float c) {
    const long pct = std::lround(std::fabs(static_cast<double>(c) - 1.0) * 100.0);
    if (pct == 0) {
        return format_paper_grade(c) + ", no change";
    }
    return format_paper_grade(c) + ", each stop near middle grey is " + std::to_string(pct) + "% " +
           (c > 1.0f ? "wider" : "narrower");
}

std::string colour_text(float s) {
    return s == 0.0f ? "black and white" : format_times(s) + " colour";
}

std::string grey_text(int percent) {
    return "18% grey becomes " + std::to_string(percent) + "%";
}

// {kind.id.field}: the value of one stage of one look, as the live lines format it.
std::string look_token_value(std::string_view token, std::span<const Preset> raw,
                             std::span<const Preset> image) {
    const auto unknown = [&] { return std::logic_error("unknown token: " + std::string(token)); };
    const std::size_t first = token.find('.');
    const std::size_t last = token.rfind('.');
    if (first == std::string_view::npos || first == last) {
        throw unknown();
    }
    const std::string_view kind = token.substr(0, first);
    const std::string_view id = token.substr(first + 1, last - first - 1);
    const std::string_view field = token.substr(last + 1);
    if (kind != "raw" && kind != "image") {
        throw unknown();
    }
    for (const Preset& preset : kind == "raw" ? raw : image) {
        if (preset.id != id) {
            continue;
        }
        if (field == "exposure") {
            return format_exposure(preset.settings.exposure_stops);
        }
        if (field == "saturation") {
            return format_times(preset.settings.saturation);
        }
        if (field == "contrast") {
            return format_times(preset.settings.contrast);
        }
        break;
    }
    throw unknown();
}

// The table with its tokens filled in, once. The details are all made before any view of them.
const std::vector<Explanation>& expanded() {
    static const std::vector<std::string> details = [] {
        std::vector<std::string> out;
        out.reserve(std::size(templates));
        for (const Explanation& e : templates) {
            out.push_back(expand_look_text(e.detail, presets_for(InputKind::raw),
                                           presets_for(InputKind::image)));
        }
        return out;
    }();
    static const std::vector<Explanation> entries = [] {
        std::vector<Explanation> out(std::begin(templates), std::end(templates));
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i].detail = details[i];
        }
        return out;
    }();
    return entries;
}

}  // namespace

std::span<const Explanation> explanations() noexcept {
    return expanded();
}

const Explanation* explanation_for(std::string_view id) noexcept {
    for (const Explanation& e : expanded()) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

std::string_view explain_id(StageId stage) noexcept {
    switch (stage) {
        case StageId::decode: return "stage.decode";
        case StageId::exposure: return "stage.exposure";
        case StageId::dodge_burn: return "stage.dodge_burn";
        case StageId::white_balance: return "stage.white_balance";
        case StageId::matrix: return "stage.matrix";
        case StageId::saturation: return "stage.saturation";
        case StageId::contrast: return "stage.contrast";
        case StageId::curve: return "stage.curve";
        case StageId::encode: return "stage.encode";
    }
    return {};
}

std::string_view explain_id(CurveKind kind) noexcept {
    switch (kind) {
        case CurveKind::none: return "curve.none";
        case CurveKind::gamma: return "curve.gamma";
        case CurveKind::reinhard: return "curve.reinhard";
        case CurveKind::filmic: return "curve.filmic";
    }
    return {};
}

std::string_view explain_id(ScopeChoice scope) noexcept {
    switch (scope) {
        case ScopeChoice::histogram: return "scope.histogram";
        case ScopeChoice::parade: return "scope.parade";
        case ScopeChoice::waveform: return "scope.waveform";
        case ScopeChoice::vectorscope: return "scope.vectorscope";
    }
    return {};
}

std::string explain_id(const Preset& preset) {
    return "look." + std::string(preset.id);
}

float explain_area_height(const TokenMetrics& metrics, float small_line,
                         float panel_height) noexcept {
    const float five_lines = 2 * metrics.explain_pad.y + 2 * metrics.window_border +
                             explain_max_lines * small_line +
                             (explain_max_lines - 1) * metrics.explain_gap;
    return std::min(panel_height * 0.45f, five_lines);
}

std::string_view explain_to_show(std::string_view hovered, std::string_view focused,
                                 std::string_view last) noexcept {
    if (!hovered.empty()) {
        return hovered;
    }
    return !focused.empty() ? focused : last;
}

std::span<const std::string_view> other_explain_ids() noexcept {
    return other_ids;
}

std::string expand_look_text(std::string_view text, std::span<const Preset> raw,
                             std::span<const Preset> image) {
    std::string out;
    std::size_t at = 0;
    while (true) {
        const std::size_t open = text.find('{', at);
        out += text.substr(at, open == std::string_view::npos ? open : open - at);
        if (open == std::string_view::npos) {
            return out;
        }
        const std::size_t close = text.find('}', open);
        if (close == std::string_view::npos) {
            throw std::logic_error("unclosed token in: " + std::string(text));
        }
        out += look_token_value(text.substr(open + 1, close - open - 1), raw, image);
        at = close + 1;
    }
}

std::string format_times(float value) {
    return "×" + printf_string("%.2f", static_cast<double>(value));
}

std::string grey_card_percent(CurveKind kind, const EditSettings& settings) {
    return std::to_string(grey_card(settings, kind)) + "%";
}

std::string look_values_line(const EditSettings& s) {
    std::string line;
    const auto add = [&](const std::string& part) {
        line += (line.empty() ? "" : ", ") + part;
    };
    if (s.exposure_on && s.exposure_stops != 0.0f) {
        add(format_exposure(s.exposure_stops));
    }
    if (s.saturation_on && s.saturation != 1.0f) {
        add(s.saturation == 0.0f ? colour_text(s.saturation)
                                 : "colour " + format_times(s.saturation));
    }
    if (s.contrast_on && s.contrast != 1.0f) {
        add("contrast " + format_times(s.contrast));
    }
    if (s.curve_on && s.curve != CurveKind::none) {
        add(s.curve == CurveKind::reinhard ? "Reinhard (white " + number_text(s.reinhard_white) + ")"
                                           : stage_value_text(s, StageId::curve));
    }
    return line.empty() ? "no adjustments" : line;
}

std::string live_line(std::string_view id, const EditSettings& s, const LiveContext& context) {
    const Explanation* entry = explanation_for(id);
    if (entry == nullptr) {
        return {};
    }
    switch (entry->live) {
        case LiveKind::none:
            return {};
        case LiveKind::look: {
            const Preset* preset = matching_preset(context.kind, s);
            return (preset != nullptr ? std::string(preset->name) : std::string("Custom")) + ": " +
                   look_values_line(s);
        }
        case LiveKind::exposure:
            return if_off(s, StageId::exposure, stops_and_factor(s.exposure_stops));
        case LiveKind::white_balance: {
            // The light as the row reads it, and for a choice what it is measured against.
            const std::string as_shot =
                context.as_shot_white
                    ? white_balance_light_text(as_shot_white_balance(context.as_shot_white))
                    : "taken as " + format_kelvin(default_as_shot.kelvin);
            if (!s.white_balance) {
                // As shot changes nothing, on or off.
                return "As shot: " + as_shot +
                       (context.as_shot_white ? ", the light the camera balanced for"
                                              : ", as this file names no light");
            }
            return if_off(s, StageId::white_balance,
                          "Chosen: " + white_balance_light_text(*s.white_balance) +
                              ". As shot is " + as_shot);
        }
        case LiveKind::matrix:
            return if_off(s, StageId::matrix, stage_value_text(s, StageId::matrix));
        case LiveKind::saturation:
            return if_off(s, StageId::saturation, colour_text(s.saturation));
        case LiveKind::contrast_row:
            return if_off(s, StageId::contrast, contrast_text(s.contrast));
        case LiveKind::curve: {
            std::string text = stage_value_text(s, StageId::curve);
            if (s.curve != CurveKind::none) {
                text += ": " + grey_text(grey_card(s, s.curve));
            }
            return if_off(s, StageId::curve, text);
        }
        case LiveKind::gamma:
            return "gamma " + number_text(s.gamma) + ": " + grey_text(grey_card(s, CurveKind::gamma));
        case LiveKind::reinhard:
            return "white " + number_text(s.reinhard_white) + ": a scene value of " +
                   number_text(s.reinhard_white) + " maps to white; " +
                   grey_text(grey_card(s, CurveKind::reinhard));
        case LiveKind::filmic:
            return grey_text(grey_card(s, CurveKind::filmic));
        case LiveKind::white:
            return "white = " + number_text(s.reinhard_white);
        case LiveKind::viewing:
            return "Viewing: " + stage_caption(context.viewed);
        case LiveKind::over:
        case LiveKind::under: {
            if (context.histogram == nullptr || context.histogram->pixel_count == 0) {
                return {};
            }
            const StageHistogram& h = *context.histogram;
            const bool over = entry->live == LiveKind::over;
            const auto percent = [&](const ChannelClips& c) {
                return printf_string("%.2f", pixel_percent(over ? c.high() : c.under_black,
                                                           h.pixel_count)) +
                       "%";
            };
            return "R " + percent(h.red_clips) + "   G " + percent(h.green_clips) + "   B " +
                   percent(h.blue_clips);
        }
        case LiveKind::clip_note: {
            if (context.clips == nullptr) {
                return {};
            }
            std::string text = describe_clipping(*context.clips);
            const std::string gamut = describe_gamut(*context.clips);
            return gamut.empty() ? text : text + " " + gamut;
        }
        case LiveKind::compare:
            if (!context.compare) {
                return "No comparison: the photo is shown on its own.";
            }
            return "Left: " + stage_caption(*context.compare) +
                   ". Right: " + stage_caption(context.viewed) + ".";
        case LiveKind::recipe:
            return context.recipe == nullptr ? std::string() : stages_line(context.recipe->stages);
        case LiveKind::strip: {
            const int divisor = strip_step_divisor(context.strip_step);
            const char* name = divisor == 3 ? "1/3 stop" : divisor == 2 ? "1/2 stop" : "1 stop";
            return std::string("Steps of ") + name + ": each band is " +
                   printf_string("%.2f", std::exp2(1.0 / divisor)) + "× the light of the one before";
        }
        case LiveKind::preview_size: {
            if (context.preview_long_side <= 0 || context.file_long_side <= 0) {
                return {};
            }
            const std::string preview = std::to_string(context.preview_long_side);
            const std::string file = std::to_string(context.file_long_side);
            // With a frame the preview is the framed picture, so it is measured against the frame
            // in the file, not the whole file.
            const std::string what = context.measures_frame ? "frame" : "file";
            // What the canvas shows zoomed in, as its own sentence after the preview's.
            const std::string view_state =
                context.view_state.empty() ? std::string() : ". " + std::string(context.view_state);
            if (context.preview_long_side >= context.file_long_side && context.file_size_exact) {
                return "Preview and " + what + " are both " + file +
                       " px on the long side: each preview pixel is one " + what + " pixel" +
                       view_state;
            }
            const double across =
                file_pixels_across(context.preview_long_side, context.file_long_side);
            return "Preview " + preview + " px, " + what + " " +
                   std::string(size_estimate_prefix(context.file_size_exact)) + file +
                   " px on the long side: each preview pixel stands for about " +
                   printf_string("%.1f", across) + " " + what + " pixels across" +
                   // Still true wherever the canvas shows the preview; zoomed in, view_state says
                   // what it shows instead.
                   (context.measures_frame && view_state.empty()
                        ? ", so a tight frame looks softer here than it exports"
                        : view_state);
        }
        case LiveKind::sample:
            if (context.sample == nullptr) {
                return {};
            }
            return describe_sample(*context.sample, context.viewed, context.compare,
                                   {context.preview_long_side, context.file_long_side,
                                    context.measures_frame});
        case LiveKind::frame: {
            if (context.file_width <= 0 || context.file_height <= 0) {
                return {};
            }
            const PixelSize file{context.file_width, context.file_height};
            if (!s.frame) {
                return "The whole photo, upright: no frame.";
            }
            return framing_live_line(framed_export_size(s.frame, file), file, s.frame->angle,
                                     context.file_size_exact);
        }
    }
    return {};
}

}  // namespace prism::gui

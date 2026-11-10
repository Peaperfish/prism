#include "prism/gui/pixel_sample.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "prism/gui/stage_controls.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"

namespace prism::gui {

namespace {

constexpr int channel_count = 3;
constexpr std::string_view channel_names[channel_count] = {"red", "green", "blue"};
// The last stage that changes the values: Output encode only clamps what it receives.
constexpr StageId last_edit = StageId::curve;

bool has_channel(std::uint8_t channels, int c) noexcept {
    return (channels & (1u << c)) != 0;
}

int channels_in(std::uint8_t channels) noexcept {
    return static_cast<int>(has_channel(channels, 0)) + static_cast<int>(has_channel(channels, 1)) +
           static_cast<int>(has_channel(channels, 2));
}

// "red", "red and green", "red, green and blue".
std::string names_of(std::uint8_t channels) {
    std::string text;
    int left = channels_in(channels);
    for (int c = 0; c < channel_count; ++c) {
        if (!has_channel(channels, c)) {
            continue;
        }
        text += channel_names[c];
        --left;
        text += left > 1 ? ", " : left == 1 ? " and " : "";
    }
    return text;
}

std::string capitalised(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

const char* is_or_are(std::uint8_t channels) noexcept {
    return channels_in(channels) == 1 ? "is" : "are";
}

const char* it_or_them(std::uint8_t channels) noexcept {
    return channels_in(channels) == 1 ? "it" : "them";
}

// A stage as a sentence names it: "the Tone curve (filmic)", "the Colour matrix", "Saturation".
std::string stage_words(const PixelTrace& trace, StageId stage) {
    const std::string name(stage_info(stage).name);
    switch (stage) {
        case StageId::curve:
            return trace.curve.empty() ? "the " + name : "the " + name + " (" + trace.curve + ")";
        case StageId::matrix: return "the " + name;
        default: return name;
    }
}

StageId next_stage(StageId stage) noexcept {
    return static_cast<StageId>(static_cast<int>(stage) + 1);
}

bool before(StageId a, StageId b) noexcept {
    return static_cast<int>(a) < static_cast<int>(b);
}

// The stage after which channel `c` stays under white for good, for a channel over white as `from`
// leaves it. Nothing if it reaches Output encode still over white (or as NaN), which then clips it.
std::optional<StageId> brought_under_white_by(const PixelTrace& trace, StageId from, int c) {
    std::optional<StageId> found;
    for (StageId s = last_edit; before(from, s);
         s = static_cast<StageId>(static_cast<int>(s) - 1)) {
        if (!(trace.at(s).rgb[static_cast<std::size_t>(c)] <= 1.0f)) {
            break;
        }
        found = s;
    }
    return found;
}

std::string over_white_fate(const PixelTrace& trace, StageId stage) {
    const std::uint8_t over = trace.at(stage).range.over;
    if (over == 0) {
        return {};
    }
    std::string text = capitalised(names_of(over)) + " " + is_or_are(over) + " over white";
    if (stage == StageId::encode) {
        return text + ": Output encode clips " + it_or_them(over) + ".";
    }
    text += ".";

    // The channels each stage brings back, in pipeline order, and those nothing does.
    std::array<std::uint8_t, ClipReport::stage_count> back{};
    std::uint8_t clipped = 0;
    for (int c = 0; c < channel_count; ++c) {
        if (has_channel(over, c)) {
            const std::optional<StageId> by = brought_under_white_by(trace, stage, c);
            (by ? back[static_cast<std::size_t>(*by)] : clipped) |=
                static_cast<std::uint8_t>(1u << c);
        }
    }
    if (clipped == over) {
        return text + " Still recoverable until Output encode, which clips " + it_or_them(over) +
               ".";
    }
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < back.size(); ++i) {
        if (back[i] == 0) {
            continue;
        }
        const std::string what = back[i] == over ? it_or_them(over) : names_of(back[i]);
        parts.push_back(stage_words(trace, static_cast<StageId>(i)) + " brings " + what +
                        " back under 1.00");
    }
    if (clipped != 0) {
        parts.push_back("Output encode clips " + names_of(clipped));
    }
    text += " Not lost yet: ";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        text += (i == 0 ? "" : "; ") + parts[i];
    }
    return text + ".";
}

// A pixel whose negative channels make it low or out of gamut as `stage` leaves it.
std::string negative_fate(const PixelTrace& trace, StageId stage) {
    const PixelRange& range = trace.at(stage).range;
    const bool gamut = range.gamut != 0;
    const std::uint8_t negative = gamut ? range.gamut : range.low;
    if (negative == 0) {
        return {};
    }
    const std::string text = std::string(gamut ? "Too saturated for sRGB: " : "No light: ") +
                             names_of(negative) + " " + is_or_are(negative) + " below 0.";
    const std::string hue = gamut ? ", so the hue may shift." : ".";
    const char* pronoun = it_or_them(negative);
    if (stage == StageId::encode) {
        return text + " Output encode clips " + pronoun + hue;
    }
    for (StageId s = next_stage(stage); !before(last_edit, s); s = next_stage(s)) {
        const PixelRange& later = trace.at(s).range;
        if (later.low != 0 || later.gamut != 0) {
            continue;
        }
        if (stage_clamps(s, ClipDirection::shadows)) {
            return text + " Still recoverable until " + stage_words(trace, s) + ", which clips " +
                   pronoun + hue;
        }
        return text + " Not lost yet: " + stage_words(trace, s) + " brings " + pronoun + " back.";
    }
    return text + " Still recoverable until Output encode, which clips " + pronoun + hue;
}

// A pixel a stage up to and including `stage` already clamped. The picture no longer shows it; J
// still marks it.
std::string clamped_fate(const PixelTrace& trace, StageId stage) {
    if (!trace.negatives_clamped_at || before(stage, *trace.negatives_clamped_at)) {
        return {};
    }
    const std::string by = stage_words(trace, *trace.negatives_clamped_at);
    if (trace.before_clamp.gamut != 0) {
        return "It was too saturated for sRGB until " + by + " clipped it, so the hue may have " +
               "shifted.";
    }
    return trace.before_clamp.low != 0 ? "It had no light until " + by + " clipped it."
                                       : std::string();
}

// What a pixel that is fine here turns out to be by Output encode.
std::string ends_up(const PixelTrace& trace) {
    const std::uint8_t mask = trace.at(StageId::encode).mask;
    std::string words;
    const auto add = [&](std::uint8_t bit, const char* word) {
        if ((mask & bit) != 0) {
            words += (words.empty() ? "" : " and ") + std::string(word);
        }
    };
    add(clip_mask_high, "over white");
    add(clip_mask_low, "with no light");
    add(clip_mask_gamut, "too saturated for sRGB");
    return words.empty() ? "Nothing clipped."
                         : "Nothing clipped here, but it ends up " + words + ".";
}

std::string fate_line(const PixelTrace& trace, StageId stage) {
    std::string text;
    const auto add = [&](const std::string& sentence) {
        if (!sentence.empty()) {
            text += (text.empty() ? "" : " ") + sentence;
        }
    };
    // Whatever stage is viewed: the edits can hide a value that the file itself had clipped.
    const std::uint8_t ceiling = trace.at(StageId::decode).range.ceiling;
    if (ceiling != 0) {
        add(capitalised(names_of(ceiling)) + (channels_in(ceiling) == 1 ? " was" : " were") +
            " already clipped in the file; no edit can bring " + it_or_them(ceiling) + " back.");
    }
    add(over_white_fate(trace, stage));
    add(negative_fate(trace, stage));
    add(clamped_fate(trace, stage));
    const std::array<float, 3>& rgb = trace.at(stage).rgb;
    if (std::isnan(rgb[0]) || std::isnan(rgb[1]) || std::isnan(rgb[2])) {
        add("Not a number at this stage, so it counts as neither over nor under.");
    }
    return text.empty() ? ends_up(trace) : text;
}

// `v` with `places` decimals, always with '.', whatever the locale. Negative only if it reads as
// something other than zero.
std::string fixed(float v, int places) {
    char buffer[64];
    const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof buffer, std::fabs(v),
                                                 std::chars_format::fixed, places);
    const std::string digits = r.ec == std::errc{} ? std::string(buffer, r.ptr) : std::string("?");
    const bool zero = digits.find_first_not_of("0.") == std::string::npos;
    return (std::signbit(v) && !zero ? "-" : "") + digits;
}

// A channel: three decimals, or what it is instead of a number.
std::string value_text(float v) {
    if (std::isnan(v)) {
        return "NaN";
    }
    if (std::isinf(v)) {
        return v > 0.0f ? "∞" : "-∞";
    }
    const std::string text = fixed(v, 3);
    // A channel under white never reads as white: 0.99995 would round to 1.000, which is the
    // file's ceiling, where a clipped value sits. A value in the ceiling band, or above, reads as
    // it is.
    constexpr float ceiling = 1.0f - file_ceiling_epsilon;
    return v < ceiling && text == "1.000" ? "0.999" : text;
}

std::string rgb_text(const std::array<float, 3>& rgb) {
    return "R " + value_text(rgb[0]) + " G " + value_text(rgb[1]) + " B " + value_text(rgb[2]);
}

// "107%", "30%", "0.4%", "<0.1%": never rounding what light there is down to nothing. For Y > 0.
std::string light_percent(float y) {
    const double percent = 100.0 * static_cast<double>(y);
    if (percent < 0.1) {
        return "<0.1%";
    }
    return fixed(static_cast<float>(percent), percent < 0.95 ? 1 : 0) + "%";
}

// "107% of white's light", or what there is instead.
std::string light_text(float y) {
    if (std::isnan(y)) {
        return "no reading of the light";
    }
    if (!(y > 0.0f)) {
        return "no light";
    }
    return std::isinf(y) ? "far over white" : light_percent(y) + " of white's light";
}

// "2.6 stops above middle grey", "1.3 stops below middle grey", "at middle grey": a position, in
// words. Signs are for adjustments. Empty when the light says it all (no light, NaN, infinity).
std::string stops_text(const std::optional<float>& stops) {
    if (!stops || std::isinf(*stops)) {
        return {};
    }
    const std::string number = fixed(std::fabs(*stops), 1);
    if (number == "0.0") {
        return "at middle grey";
    }
    return number + (*stops > 0.0f ? " stops above middle grey" : " stops below middle grey");
}

// What the picture can show of a value: as Output encode leaves it, NaN being 0.
float on_screen(float v) noexcept {
    return v > 0.0f ? std::min(v, 1.0f) : 0.0f;
}

// The scene's light, from the Contrast stage: its stops, or what it has instead.
std::string scene_text(const SampleStats& scene) {
    const std::string stops = stops_text(scene.stops);
    if (!stops.empty()) {
        return stops;
    }
    return std::isnan(scene.luminance)   ? "no reading of the light"
           : std::isinf(scene.luminance) ? "far over white"
                                         : "no light";
}

std::string values_row(const PixelTrace& trace, StageId stage) {
    const std::array<float, 3>& rgb = trace.at(stage).rgb;
    if (stage != StageId::encode) {
        return rgb_text(rgb) + " (linear, 1.00 = white)";
    }
    return "Linear " + rgb_text(rgb) + " → sRGB " + std::to_string(trace.srgb8[0]) + " " +
           std::to_string(trace.srgb8[1]) + " " + std::to_string(trace.srgb8[2]);
}

std::string light_row(const PixelTrace& trace, StageId stage, const SampleStats& stats) {
    const std::array<float, 3>& rgb = trace.at(stage).rgb;
    if (before(stage, StageId::curve)) {
        const std::string stops = stops_text(stats.stops);
        return capitalised(light_text(stats.luminance)) + (stops.empty() ? "" : ", " + stops);
    }
    // 0.18 is no longer a grey card here. The scene's figure is from the last stage before it
    // stopped being one.
    const float shown = sample_luminance({on_screen(rgb[0]), on_screen(rgb[1]), on_screen(rgb[2])});
    return "On screen " + light_text(shown) + " · In the scene " +
           scene_text(sample_stats(trace, StageId::contrast));
}

// The light as the rows say it at `stage`, without the position in stops: at the Tone curve and
// after, what the screen can show.
float light_at(const PixelTrace& trace, StageId stage) noexcept {
    const std::array<float, 3>& rgb = trace.at(stage).rgb;
    return before(stage, StageId::curve)
               ? sample_luminance(rgb)
               : sample_luminance({on_screen(rgb[0]), on_screen(rgb[1]), on_screen(rgb[2])});
}

// The line about what one preview pixel averages, or nothing if it stands for one file pixel.
std::string average_text(const SampleScale& scale) {
    const double across = file_pixels_across(scale.preview_long_side, scale.file_long_side);
    const std::string number = fixed(static_cast<float>(across), 1);
    if (across < 1.0 || number == "1.0") {
        return {};
    }
    return "One preview pixel averages about " + number + "×" + number +
           " file pixels, so a small clipped highlight can hide in it.";
}

}  // namespace

std::optional<PixelTrace> sample_trace(const Image& base, const EditSettings& settings, int x,
                                       int y, const std::optional<Chromaticity>& as_shot_white) {
    const ProcessSettings process_settings =
        to_process_settings(settings, final_stage, as_shot_white);
    if (base.width < 0 || base.height < 0 ||
        base.pixels.size() != static_cast<std::size_t>(base.width) *
                                  static_cast<std::size_t>(base.height) * Image::channels) {
        throw std::invalid_argument("sample_trace: pixel buffer size does not match the image");
    }
    if (x < 0 || y < 0 || x >= base.width || y >= base.height) {
        return std::nullopt;
    }
    const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(base.width) +
                        static_cast<std::size_t>(x)) *
                       Image::channels;
    Image one;
    one.width = 1;
    one.height = 1;
    one.pixels.assign(base.pixels.begin() + static_cast<std::ptrdiff_t>(index),
                      base.pixels.begin() + static_cast<std::ptrdiff_t>(index + Image::channels));

    PixelTrace trace;
    trace.x = x;
    trace.y = y;
    trace.curve = settings.curve == CurveKind::none ? std::string()
                                                    : stage_value_text(settings, StageId::curve);
    const auto keep = [&](StageId stage) {
        SampleStage& at = trace.stages[static_cast<std::size_t>(stage)];
        std::copy_n(one.pixels.begin(), Image::channels, at.rgb.begin());
        at.ran = true;
    };
    keep(StageId::decode);
    const StageCallback after_stage = [&](const StageTime& t) {
        if (const std::optional<StageId> stage = stage_from_name(t.name)) {
            keep(*stage);
        }
    };
    const BeforeStageCallback before_stage = [&](std::string_view name, const Image& input) {
        const std::optional<StageId> stage = stage_from_name(name);
        if (stage && !trace.negatives_clamped_at && stage_clamps(*stage, ClipDirection::shadows)) {
            trace.negatives_clamped_at = stage;
            trace.before_clamp = pixel_range(input.pixels.data());
        }
    };
    (void)process(one, process_settings, 1, {}, after_stage, before_stage);
    keep(StageId::encode);
    const std::vector<std::uint8_t> bytes = encode_srgb8(one, 1);
    std::copy_n(bytes.begin(), Image::channels, trace.srgb8.begin());

    // A stage that did not run passes its input on; the mask remembers what was clamped.
    for (std::size_t i = 0; i < trace.stages.size(); ++i) {
        SampleStage& at = trace.stages[i];
        const auto stage = static_cast<StageId>(i);
        if (!at.ran) {
            at.rgb = trace.stages[i - 1].rgb;
        }
        at.range = pixel_range(at.rgb.data());
        at.mask = clip_mask_bits(at.range, stage == StageId::decode);
        if (trace.negatives_clamped_at && !before(stage, *trace.negatives_clamped_at)) {
            at.mask |= clip_mask_lost_bits(trace.before_clamp);
        }
    }
    return trace;
}

EditSettings sample_settings(const EditSettings& s, const std::optional<TestStrip>& strip,
                             StageId viewed, int width, int height, int x, int y) {
    if (!strip || viewed == StageId::decode) {
        return s;
    }
    const std::optional<int> band =
        strip_band_at(width, height, strip->count, static_cast<float>(x), static_cast<float>(y));
    return band ? strip_band_settings(s, strip->step, strip->count, *band) : s;
}

float sample_luminance(const std::array<float, 3>& rgb) noexcept {
    if (rgb[0] == rgb[1] && rgb[1] == rgb[2]) {
        return rgb[0];
    }
    return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

SampleStats sample_stats(const PixelTrace& trace, StageId stage) {
    const SampleStage& at = trace.at(stage);
    SampleStats stats;
    stats.luminance = sample_luminance(at.rgb);
    stats.no_light = stats.luminance <= 0.0f;
    if (stats.luminance > 0.0f) {
        // In double, so that a value exactly a whole number of stops from grey gives that number.
        stats.stops = static_cast<float>(
            std::log2(static_cast<double>(stats.luminance) / static_cast<double>(contrast_pivot)));
    }
    stats.over = (at.mask & clip_mask_high) != 0;
    stats.low = (at.mask & clip_mask_low) != 0;
    stats.gamut = (at.mask & clip_mask_gamut) != 0;
    stats.clipped_in_file = trace.at(StageId::decode).range.ceiling;
    stats.fate = fate_line(trace, stage);
    return stats;
}

double file_pixels_across(int preview_long_side, int file_long_side) noexcept {
    if (preview_long_side <= 0 || file_long_side <= 0) {
        return 0.0;
    }
    return static_cast<double>(file_long_side) / static_cast<double>(preview_long_side);
}

SampleRows sample_rows(const PixelTrace& trace, StageId stage, SampleScale scale) {
    const SampleStats stats = sample_stats(trace, stage);
    return {values_row(trace, stage), light_row(trace, stage, stats), stats.fate,
            average_text(scale)};
}

std::string sample_cursor_line(const PixelTrace& trace, StageId stage) {
    return "Under the cursor: " + rgb_text(trace.at(stage).rgb) + " · " +
           capitalised(light_text(light_at(trace, stage)));
}

std::string sample_stage_title(StageId stage) {
    return capitalised(stage_caption(stage));
}

std::string describe_sample(const PixelTrace& trace, StageId viewed, std::optional<StageId> compare,
                            SampleScale scale) {
    const auto stage_text = [&](const SampleRows& rows) {
        return rows.values + " · " + rows.light + " · " + rows.range;
    };
    const SampleRows first = sample_rows(trace, viewed, scale);
    compare = effective_compare(viewed, compare);
    if (!compare) {
        return stage_text(first) + (first.average.empty() ? "" : " · " + first.average);
    }
    return sample_stage_title(viewed) + ": " + stage_text(first) + "\n" +
           sample_stage_title(*compare) + ": " + stage_text(sample_rows(trace, *compare)) +
           (first.average.empty() ? "" : "\n" + first.average);
}

const PixelTrace* SampleCache::read(std::shared_ptr<const Image> base, const EditSettings& settings,
                                    const std::optional<TestStrip>& strip, StageId viewed,
                                    std::optional<StageId> compare, int x, int y,
                                    const std::optional<Chromaticity>& as_shot_white) {
    Key key{std::move(base), settings, strip, viewed, compare, x, y, as_shot_white};
    if (key_ && *key_ == key) {
        return current();
    }
    ++computes_;
    trace_.reset();
    if (key.base) {
        try {
            trace_ = sample_trace(
                *key.base,
                sample_settings(settings, strip, viewed, key.base->width, key.base->height, x, y),
                x, y, as_shot_white);
        } catch (const std::invalid_argument&) {
            // Remembered too: asking again gives the same answer until the settings change.
        }
    }
    key_ = std::move(key);
    return current();
}

void SampleCache::clear() noexcept {
    key_.reset();
    trace_.reset();
}

}  // namespace prism::gui

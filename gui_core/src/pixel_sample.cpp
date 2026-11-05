#include "prism/gui/pixel_sample.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
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

}  // namespace

std::optional<PixelTrace> sample_trace(const Image& base, const EditSettings& settings, int x,
                                       int y) {
    const ProcessSettings process_settings = to_process_settings(settings, final_stage);
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

}  // namespace prism::gui

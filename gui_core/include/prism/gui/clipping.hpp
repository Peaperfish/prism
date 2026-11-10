#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"

namespace prism::gui {

// Where in the pipeline values leave the range 0..1, and where that costs detail.
//
// Highlights are pixels with some channel above 1.0. Shadows are pixels with no light: luminance
// Y = 0.2126 R + 0.7152 G + 0.0722 B (linear light) at or below 0 because some channel is negative.
// (A pure black pixel is exactly black, not a shadow that was lost.) A negative channel on a
// pixel that still has light (Y > 0) is a colour outside sRGB, not a shadow.
//
// A value above 1 (brighter than white) or below 0 (darker than black) is not lost by itself: the
// pipeline works in float, so a later stage may bring it back (a tone curve rolls highlights
// down, a lower exposure undoes a boost). Detail is lost only where a stage clamps it, because
// the clamped values are all equal afterwards. Which stages do that:
//
//   Decode        clamps above 1: the file's own ceiling (see below)
//   Saturation    clamps below 0 (it pulls a negative channel up to 0 and makes Y <= 0 black)
//   Tone curve    clamps below 0 (all three curves floor their input at 0)
//   Output encode clamps both ends (8-bit sRGB can only hold 0..1)
//
// Exposure, White balance, Colour matrix and Contrast never clamp (the two matrices can create
// values below 0, contrast keeps them below 0).
//
// Decode's ceiling: LibRaw maps the sensor's white level to exactly 1.0 (and clips there), and an
// 8-bit file's 255 is exactly 1.0, so a pixel with a channel at 1.0 after Decode was already
// clipped before any edit, and nothing can bring its detail back. Decode therefore counts any
// channel in [1.0 - file_ceiling_epsilon, 1.0] as lost, for RAW and 8-bit sources alike. (A real,
// unclipped value at exactly 1.0 can't be told apart; that is rare in a photo.) Prism has no float
// decode path: every source is integer (8-bit, or RAW at 16 bits), and those never exceed 1.0. So
// a value above 1.0 at Decode can only come from a float image given to the library directly,
// where it is real highlight data: it counts as out of range and recoverable, like Exposure's.
// (If a pixel set has both, the row counts the ceiling pixels.) Below 0 Decode is
// not changed: both kinds of source give values in 0..1 (an 8-bit 0 is 0.0; LibRaw's 16-bit output
// has the black level subtracted and floored at 0), so black in the file looks like true black.
//
// Gamut (#14). A colour outside the output gamut is a pixel that still has light (Y > 0) but a
// channel below -gamut_tolerance: sRGB cannot show it, and clamping that channel to 0 changes the
// pixel's hue. It is counted the same way at every stage, in the same pass as the rest, and
// independently of highs and lows (a pixel can be in more than one). Negatives between
// -gamut_tolerance and 0 are neither gamut nor lows. The stage that first has a non-zero gamut
// count "pushed the colours out" (usually Colour matrix); the first later stage that clamps
// negatives (Saturation if it runs, else Tone curve, else Output encode) "clips" them, and that
// is where their fate is lost. Saturation's own limiting of a colour to the gamut boundary is not
// a loss: only negatives that arrive at Saturation and are clamped there count. The gamut count
// of a clamping stage is therefore of what came in, like the shadows count.
enum class ClipDirection { highlights, shadows };

// How close to 1.0 counts as at the file's ceiling when counting Decode's clipped pixels. It only
// absorbs float rounding (e.g. an average of 1.0s in the downscaled preview).
inline constexpr float file_ceiling_epsilon = 1e-6f;

// How far below 0 a channel must be, on a pixel with Y > 0, to count as out of gamut: half an
// 8-bit step at the sRGB toe (0.5 / 255 / 12.92), below which the encoded value rounds to 0 anyway.
inline constexpr float gamut_tolerance = 1.5e-4f;

// How one pixel's values sit against 0 and 1. This is the one per-pixel test: the clip mask, the
// clip report and the colour sampler all ask it, so they can never disagree. Each field has one bit
// per channel: bit 0 red, bit 1 green, bit 2 blue.
//   over:    above 1.0.
//   ceiling: in [1.0 - file_ceiling_epsilon, 1.0]: at the file's ceiling, if this is Decode.
//   low:     no light: the negative channels of a pixel with Y <= 0. A pure black pixel (0, 0, 0) has
//            none: it is exactly black, nothing is out of range.
//   gamut:   the channels below -gamut_tolerance of a pixel that has light (Y > 0).
// Comparisons are false for NaN, so NaN counts as none of them; +-inf count.
struct PixelRange {
    std::uint8_t over = 0;
    std::uint8_t ceiling = 0;
    std::uint8_t low = 0;
    std::uint8_t gamut = 0;

    bool operator==(const PixelRange&) const = default;
};

// `p` points at a pixel's three channels. Inline because the mask asks for every pixel of a preview.
[[nodiscard]] inline PixelRange pixel_range(const float* p) noexcept {
    constexpr float ceiling = 1.0f - file_ceiling_epsilon;
    PixelRange r;
    // Only a pixel with a channel below 0 can be low or gamut, so the luminance is worked out for
    // those alone; the rest, nearly every pixel, cost one min.
    const float lowest = std::min(p[0], std::min(p[1], p[2]));
    bool low = false;
    bool gamut = false;
    if (lowest < 0.0f) {
        const float y = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        low = y <= 0.0f;
        gamut = y > 0.0f && lowest < -gamut_tolerance;
    }
    for (int c = 0; c < 3; ++c) {
        const auto bit = static_cast<std::uint8_t>(1u << c);
        r.over |= p[c] > 1.0f ? bit : std::uint8_t{0};
        r.ceiling |= p[c] >= ceiling && p[c] <= 1.0f ? bit : std::uint8_t{0};
        r.low |= low && p[c] < 0.0f ? bit : std::uint8_t{0};
        r.gamut |= gamut && p[c] < -gamut_tolerance ? bit : std::uint8_t{0};
    }
    return r;
}

// Whether `stage`, when it runs, clamps values in `direction`.
[[nodiscard]] bool stage_clamps(StageId stage, ClipDirection direction) noexcept;

// What happens to the pixels counted for a stage in one direction.
enum class ClipFate {
    none,         // no pixel is affected
    recoverable,  // out of range as the stage leaves them; a later stage may bring them back
    lost,         // the stage clamped them: the detail is gone
};

// Pixels (of the sampled grid) with any / each channel out of range in one direction.
struct ClipCounts {
    std::uint64_t any = 0;
    std::uint64_t red = 0;
    std::uint64_t green = 0;
    std::uint64_t blue = 0;

    bool operator==(const ClipCounts&) const = default;
};

// One stage of the report. `over` counts values above 1.0 and `under` the negative channels
// of pixels with Y <= 0
// (at Decode, `over` counts values at the file's ceiling, in [1.0 - file_ceiling_epsilon, 1.0]:
// lost; with none, values above 1.0: recoverable).
// For a direction the stage does not clamp, the count is of its output (still out of range,
// fate recoverable). For a direction it clamps, the count is of the pixels that were out of range
// going in, which the stage just clamped (fate lost); the output has none left.
struct StageClip {
    StageId stage = StageId::decode;
    bool ran = false;  // false: the stage was off or neutral, so it did nothing (and counts are 0)
    ClipCounts over;
    ClipCounts under;
    ClipCounts gamut;  // channels below -gamut_tolerance on pixels with Y > 0 (see above)
    ClipFate over_fate = ClipFate::none;
    ClipFate under_fate = ClipFate::none;
    ClipFate gamut_fate = ClipFate::none;

    bool operator==(const StageClip&) const = default;
};

struct ClipReport {
    static constexpr std::uint64_t max_samples = 250'000;
    static constexpr std::size_t stage_count = gui::stage_count;

    std::array<StageClip, stage_count> stages{};  // in pipeline order, indexed by StageId
    std::uint64_t sample_count = 0;  // pixels counted per stage (a strided grid, see below)
    int stride = 1;                  // distance in x and y between counted pixels

    // The first stage whose pixels are out of range, and the first that clamps some (loses detail),
    // for highlights and for shadows. Nothing if that never happens.
    std::optional<StageId> first_over;
    std::optional<StageId> first_over_lost;
    std::optional<StageId> first_under;
    std::optional<StageId> first_under_lost;
    // The first stage with colours outside the gamut ("pushed out by"), and the first that clamps
    // them (where the hue shift happens).
    std::optional<StageId> first_gamut;
    std::optional<StageId> first_gamut_lost;

    [[nodiscard]] const StageClip& at(StageId id) const noexcept {
        return stages[static_cast<std::size_t>(id)];
    }
    bool operator==(const ClipReport&) const = default;
};

// The stage a StageTime name from process() belongs to ("exposure", "matrix", ...).
[[nodiscard]] std::optional<StageId> stage_from_name(std::string_view name) noexcept;

// `full` without the stages up to and including `stop_after`: what is left to run on an image
// that already went through them. Each stage goes exactly when `stop_after` is it or a later one:
// the white balance matrix from White balance on, the colour matrix from Colour matrix on.
[[nodiscard]] ProcessSettings process_settings_after(ProcessSettings full, StageId stop_after);

// Fills a ClipReport while the pipeline runs. Only a strided grid of at most
// ClipReport::max_samples pixels is read per stage (like the colour scopes), so the cost stays
// small however big the preview is. Not thread-safe: use it from the thread running the pipeline.
//
//   ClipRecorder rec(img.width, img.height);
//   rec.decoded(img);
//   ... after each stage that ran:  rec.stage_done(id, img);
//   rec.finish();                  // Output encode
//   ClipReport report = rec.report();
class ClipRecorder {
public:
    ClipRecorder(int width, int height);

    void decoded(const Image& img);
    void stage_done(StageId stage, const Image& img);

    // Every stage after the viewed one is a per-pixel operation (no pixel reads its neighbours), so
    // they can run on just the sampled pixels. sample_grid() copies the pixels this recorder would
    // read from `img` into a small image; run the remaining stages on that, and report each one
    // with stage_done_samples(). The counts are the same as running them on the whole image.
    [[nodiscard]] Image sample_grid(const Image& img) const;
    void stage_done_samples(StageId stage, const Image& grid);
    // Where the pixels of sample_grid() sit on the photo, for the Dodge & burn stage that runs on
    // it: the recorder's image is `image` on the photo (stage_grid(W, H) for a whole picture, a
    // band's rectangle for a test strip band), and the sample grid takes every stride-th pixel of
    // it from `first`. Empty (0 x 0) if the recorder has no dimensions.
    [[nodiscard]] PixelGrid grid(const PixelGrid& image) const;
    // Output encode, after the last stage: it clamps what the last stage left out of range.
    void finish();

    [[nodiscard]] ClipReport report() const;

private:
    struct Range {
        ClipCounts over;
        ClipCounts under;
        ClipCounts gamut;
        ClipCounts ceiling;  // channels in [1.0 - file_ceiling_epsilon, 1.0] (used for Decode)
    };
    // Counts the pixels of `img` on the grid `stride` apart from `first` (stride 1: all of them).
    [[nodiscard]] Range measure(const Image& img, int stride, int first) const;
    void record(StageId stage, const Range& output);

    ClipReport report_;
    Range previous_;  // the output of the last stage recorded (what the next one received)
    bool have_dimensions_ = false;
    int width_ = 0;
    int height_ = 0;
    int first_ = 0;
};

// Runs the whole pipeline on a copy of `base` with `settings` and reports the clipping at every
// stage. For tests and headless use; the preview renderer fills the same report during a render.
// Throws std::invalid_argument for invalid settings (see to_process_settings, also for
// `as_shot_white`).
[[nodiscard]] ClipReport compute_clip_report(
    const Image& base, const EditSettings& settings, int threads = 0,
    const std::optional<Chromaticity>& as_shot_white = std::nullopt);

// Marks each pixel of `img` whose float values are out of range: bit 1 = some channel above 1.0,
// bit 2 = no light (Y <= 0 with a negative channel), bit 4 = outside the gamut. NaN is none. One
// byte per pixel, row-major, so a UI can tint those pixels on the picture. Reads every pixel, so
// only ask for it while it is shown.
inline constexpr std::uint8_t clip_mask_high = 1;
inline constexpr std::uint8_t clip_mask_low = 2;
inline constexpr std::uint8_t clip_mask_gamut = 4;  // Y > 0 and a channel below -gamut_tolerance

// What a pixel counts as: THE definitions. The clip mask (the J overlay), the clip report (the
// Light path), the histogram's counts, the clip triangles and the scope's readout all ask these, so
// they can't drift. There are three separate things, and a pixel is in at most one of the first two:
//   Below black: the pixel has no light left (Y <= 0) and a channel is negative. The Light path's
//     "under", J's blue.
//   Outside sRGB: the pixel still has light (Y > 0) but a channel is below -gamut_tolerance, a
//     colour sRGB cannot show. The Light path's "gamut", J's violet. A rounding-sized negative
//     (above -gamut_tolerance) on a pixel that still has light is neither: it encodes to 0 anyway.
//   Above white: some channel above 1.0, and with `file_ceiling` (for Decode) also one at the
//     file's ceiling.
// The *_channels functions give the channels (bit 0 red, 1 green, 2 blue) that make a pixel one.
[[nodiscard]] constexpr std::uint8_t below_black_channels(const PixelRange& r) noexcept {
    return r.low;
}
[[nodiscard]] constexpr std::uint8_t outside_srgb_channels(const PixelRange& r) noexcept {
    return r.gamut;
}
[[nodiscard]] constexpr bool pixel_below_black(const PixelRange& r) noexcept {
    return below_black_channels(r) != 0;
}
[[nodiscard]] constexpr bool pixel_outside_srgb(const PixelRange& r) noexcept {
    return outside_srgb_channels(r) != 0;
}
[[nodiscard]] constexpr bool pixel_above_white(const PixelRange& r,
                                               bool file_ceiling = false) noexcept {
    return r.over != 0 || (file_ceiling && r.ceiling != 0);
}

// The bits a stage about to clamp negatives would lose for this pixel (see mark_lost_negatives).
[[nodiscard]] constexpr std::uint8_t clip_mask_lost_bits(const PixelRange& r) noexcept {
    return static_cast<std::uint8_t>((pixel_below_black(r) ? clip_mask_low : 0) |
                                     (pixel_outside_srgb(r) ? clip_mask_gamut : 0));
}

// The mask's byte for a pixel, before any earlier stage's marks. With `file_ceiling` (for Decode),
// "high" means at the file's ceiling or above it.
[[nodiscard]] constexpr std::uint8_t clip_mask_bits(const PixelRange& r,
                                                    bool file_ceiling = false) noexcept {
    return static_cast<std::uint8_t>((pixel_above_white(r, file_ceiling) ? clip_mask_high : 0) |
                                     clip_mask_lost_bits(r));
}

// With `file_ceiling` (for Decode), "high" means at the file's ceiling, as counted at Decode.
// `earlier`, if given and the same size, is OR-ed in: the pixels a previous stage lost (see
// mark_lost_negatives), so they stay marked after the stage that clamped them.
void compute_clip_mask(const Image& img, int threads, std::vector<std::uint8_t>& mask,
                       bool file_ceiling = false,
                       const std::vector<std::uint8_t>* earlier = nullptr);

// One read-only pass over `img`, the input of a stage that clamps negatives: ORs the low and gamut
// bits of the pixels it is about to lose into `lost` (one byte per pixel; sized and zeroed first
// if it does not fit). `img` is not copied or changed.
void mark_lost_negatives(const Image& img, int threads, std::vector<std::uint8_t>& lost);

// The summary line for colours outside the gamut, or "" if there are none. Example:
//   "4.1% of colours are too saturated for sRGB after Colour matrix; Saturation clips them, so
//   hues may shift."
[[nodiscard]] std::string describe_gamut(const ClipReport& report);

// "12%", "0.4%", or "<0.1%" for anything above 0 but under 0.1% (0 is "0%").
[[nodiscard]] std::string format_clip_percent(std::uint64_t count, std::uint64_t total);

// One plain sentence per direction that clips, or "Nothing clips." Examples:
//   "Highlights first clip at Exposure (2.1% of pixels); still recoverable until Output encode."
//   "Shadows clip at Output encode (0.4% of pixels)."
//   "3.2% of pixels were already clipped in the file; no edit can bring them back."
//   "3.2% of pixels were already clipped in the file. Edits push 51% over white at Exposure; lost
//   at
//    Output encode."
[[nodiscard]] std::string describe_clipping(const ClipReport& report);

}  // namespace prism::gui

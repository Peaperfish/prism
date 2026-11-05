#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "prism/gui/clipping.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/image.hpp"

namespace prism::gui {

// The colour sampler (#28): one pixel's values at every stage, from the linear working values
// rather than the 8-bit picture. No ImGui here, so all of it is tested without a window.

// One pixel just after one stage.
struct SampleStage {
    std::array<float, 3> rgb{};  // linear; a stage that did not run passes on what it received
    bool ran = false;            // Decode and Output encode always count as having run
    PixelRange range;            // pixel_range of rgb
    // The byte compute_clip_mask gives this pixel while this stage is viewed: what J marks. Unlike
    // `range` it remembers what an earlier stage clamped.
    std::uint8_t mask = 0;
};

// One pixel's way through the pipeline. Output encode's rgb is the values it receives, before it
// clamps them to 0..1 and rounds them to srgb8.
struct PixelTrace {
    int x = 0;
    int y = 0;
    std::array<SampleStage, ClipReport::stage_count> stages{};  // in pipeline order, by StageId
    std::array<std::uint8_t, 3> srgb8{};                        // the 8-bit sRGB at Output encode
    // The first stage that clamped negatives, if it did any, and how the pixel stood going into it.
    std::optional<StageId> negatives_clamped_at;
    PixelRange before_clamp;
    // The curve's name for the words about the pixel, as the Tone curve's row shows it ("filmic").
    std::string curve;

    [[nodiscard]] const SampleStage& at(StageId id) const noexcept {
        return stages[static_cast<std::size_t>(id)];
    }
};

// Runs the pixel (x, y) of `base` through prism::process as a 1x1 image, on the calling thread: it
// costs about one contrast table. Every stage is per pixel, so the result is bit-identical to the
// full render's (see the thread-count note in prism/parallel.hpp). If a stage ever reads its
// neighbours, this is the one function to change: run a small crop on the worker instead.
//
// Nothing for a pixel off the picture or an empty image. Throws std::invalid_argument for invalid
// settings (see to_process_settings) or a pixel buffer that does not match the image's size.
[[nodiscard]] std::optional<PixelTrace> sample_trace(const Image& base,
                                                     const EditSettings& settings, int x, int y);

// The settings a pixel (x, y) of a width x height picture was rendered with: `s`, or the band's
// (strip_band_settings) while a test strip is on. As in render_preview, no strip shows at Decode.
[[nodiscard]] EditSettings sample_settings(const EditSettings& s,
                                           const std::optional<TestStrip>& strip, StageId viewed,
                                           int width, int height, int x, int y);

// Rec. 709 luminance, the weights Saturation and the clip code use. A grey is exactly its own
// value: the weights add up to 1, and rounding must not move a grey card off its stop.
[[nodiscard]] float sample_luminance(const std::array<float, 3>& rgb) noexcept;

// What one stage's values mean.
struct SampleStats {
    float luminance = 0.0f;      // Y, linear; 1 is white
    std::optional<float> stops;  // log2(Y / 0.18); nothing if there is no light (or for NaN)
    bool no_light = false;       // Y <= 0
    // What J marks at this stage, from SampleStage::mask, so the two can never disagree.
    bool over = false;   // above white (at Decode: at the file's ceiling, or above it)
    bool low = false;    // no light, because a channel is negative; a pure black pixel is not
    bool gamut = false;  // too saturated for sRGB
    // Which channels were at the file's ceiling at Decode (bit 0 red, bit 1 green, bit 2 blue).
    std::uint8_t clipped_in_file = 0;
    // Which later stage brings a value back or clamps it, in the clip report's words. Examples:
    //   "Red and green are over white. Not lost yet: the Tone curve (filmic) brings them back under
    //    1.00."
    //   "Too saturated for sRGB: blue is below 0. Still recoverable until Saturation, which clips
    //    it, so the hue may shift."
    //   "Nothing clipped."
    std::string fate;
};

[[nodiscard]] SampleStats sample_stats(const PixelTrace& trace, StageId stage);

// How many file pixels across one preview pixel stands for: the number in view.zoom's live line. 0
// if either size is unknown.
[[nodiscard]] double file_pixels_across(int preview_long_side, int file_long_side) noexcept;

// The preview against the file, for the line about what one preview pixel averages.
struct SampleScale {
    int preview_long_side = 0;  // 0 if no photo
    int file_long_side = 0;
};

// The readout, in the words a photographer uses, a row at a time so that a window can show each on
// its own. Positions are in words ("2.6 stops above middle grey"); signs are for adjustments.
struct SampleRows {
    // "R 1.210 G 1.050 B 0.860 (linear, 1.00 = white)"; Output encode adds the sRGB codes:
    // "Linear R 0.420 G 0.270 B 0.190 → sRGB 173 142 121"
    std::string values;
    // Before the Tone curve, 18% is a grey card: "107% of white's light, 2.6 stops above middle
    // grey". From the Tone curve on it no longer is, so the light on screen and the stops in the
    // scene (at Contrast) are said apart: "On screen 30% of white's light · In the scene 0.7 stops
    // above middle grey". On screen means after the clamp to 0..1, so it never reads above 100%.
    std::string light;
    // SampleStats::fate.
    std::string range;
    // What one preview pixel averages, if the preview is smaller than the file; otherwise empty.
    std::string average;
};

[[nodiscard]] SampleRows sample_rows(const PixelTrace& trace, StageId stage,
                                     SampleScale scale = {});

// The one line for the pixel under the cursor while another one is pinned:
//   "Under the cursor: R 0.420 G 0.270 B 0.190 · 30% of white's light"
// From the Tone curve on, the light is what the screen can show, as in the light row.
[[nodiscard]] std::string sample_cursor_line(const PixelTrace& trace, StageId stage);

// What a stage's rows are headed with in a comparison: "Original", "After Exposure".
[[nodiscard]] std::string sample_stage_title(StageId stage);

// The rows joined with " · ". With `compare` (see effective_compare), one line per stage, the
// viewed stage first, each with its title, and the average once at the end.
[[nodiscard]] std::string describe_sample(const PixelTrace& trace, StageId viewed,
                                          std::optional<StageId> compare = {},
                                          SampleScale scale = {});

// One pixel's trace, kept until something it depends on changes, so that a still cursor costs
// nothing however many frames go by. One per pixel on show.
class SampleCache {
public:
    // The trace of pixel (x, y), with the settings sample_settings gives. Computes it only if the
    // pixel, the base, the settings, the viewed stage, the compare stage or the strip differ from
    // the last call. Nothing for a pixel off the picture, and for settings that can't be rendered
    // (the preview says why; a readout must not take the window down).
    [[nodiscard]] const PixelTrace* read(std::shared_ptr<const Image> base,
                                         const EditSettings& settings,
                                         const std::optional<TestStrip>& strip, StageId viewed,
                                         std::optional<StageId> compare, int x, int y);

    // The last read's result, without looking at anything.
    [[nodiscard]] const PixelTrace* current() const noexcept { return trace_ ? &*trace_ : nullptr; }

    // Forgets the trace and lets go of the base.
    void clear() noexcept;

    // How many traces it has computed, ever.
    [[nodiscard]] std::uint64_t computes() const noexcept { return computes_; }

private:
    struct Key {
        std::shared_ptr<const Image> base;
        EditSettings settings;
        std::optional<TestStrip> strip;
        StageId viewed = final_stage;
        std::optional<StageId> compare;
        int x = 0;
        int y = 0;

        bool operator==(const Key&) const = default;
    };
    std::optional<Key> key_;
    std::optional<PixelTrace> trace_;
    std::uint64_t computes_ = 0;
};

}  // namespace prism::gui

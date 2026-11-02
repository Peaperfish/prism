#pragma once

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
// Exposure, Colour matrix and Contrast never clamp (a matrix can create values below 0, contrast
// keeps them below 0).
//
// Decode's ceiling: LibRaw maps the sensor's white level to exactly 1.0 (and clips there), and an
// 8-bit file's 255 is exactly 1.0, so a pixel with a channel at 1.0 after Decode was already
// clipped before any edit, and nothing can bring its detail back. Decode therefore counts any
// channel >= 1.0 - file_ceiling_epsilon as lost, for RAW and 8-bit sources alike. (A real,
// unclipped value at exactly 1.0 can't be told apart; that is rare in a photo.) Below 0 Decode is
// not changed: both kinds of source give values in 0..1 (an 8-bit 0 is 0.0; LibRaw's 16-bit output
// has the black level subtracted and floored at 0), so black in the file looks like true black.
//
// Note for a gamut warning (#14): after Colour matrix, a value below 0 is a colour outside sRGB.
// That is the `under` count of that stage's row, so a gamut warning can share this pass.
enum class ClipDirection { highlights, shadows };

// How close to 1.0 counts as at the file's ceiling when counting Decode's clipped pixels. It only
// absorbs float rounding (e.g. an average of 1.0s in the downscaled preview).
inline constexpr float file_ceiling_epsilon = 1e-6f;

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

// One row of the pipeline view. `over` counts values above 1.0 and `under` values below 0.0
// (at Decode, `over` counts values at the file's ceiling, >= 1.0 - file_ceiling_epsilon: lost).
// For a direction the stage does not clamp, the count is of its output (still out of range,
// fate recoverable). For a direction it clamps, the count is of the pixels that were out of range
// going in, which the stage just clamped (fate lost); the output has none left.
struct StageClip {
    StageId stage = StageId::decode;
    bool ran = false;  // false: the stage was off or neutral, so it did nothing (and counts are 0)
    ClipCounts over;
    ClipCounts under;
    ClipFate over_fate = ClipFate::none;
    ClipFate under_fate = ClipFate::none;

    bool operator==(const StageClip&) const = default;
};

struct ClipReport {
    static constexpr std::uint64_t max_samples = 250'000;
    static constexpr std::size_t stage_count = 7;  // pipeline_stages().size()

    std::array<StageClip, stage_count> stages{};  // in pipeline order, indexed by StageId
    std::uint64_t sample_count = 0;  // pixels counted per stage (a strided grid, see below)
    int stride = 1;                  // distance in x and y between counted pixels

    // The first stage whose pixels are out of range, and the first that clamps some (loses detail),
    // for highlights and for shadows. Nothing if that never happens.
    std::optional<StageId> first_over;
    std::optional<StageId> first_over_lost;
    std::optional<StageId> first_under;
    std::optional<StageId> first_under_lost;

    [[nodiscard]] const StageClip& at(StageId id) const noexcept {
        return stages[static_cast<std::size_t>(id)];
    }
    bool operator==(const ClipReport&) const = default;
};

// The stage a StageTime name from process() belongs to ("exposure", "matrix", ...).
[[nodiscard]] std::optional<StageId> stage_from_name(std::string_view name) noexcept;

// `full` without the stages up to and including `stop_after`: what is left to run on an image
// that already went through them.
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
    // Output encode, after the last stage: it clamps what the last stage left out of range.
    void finish();

    [[nodiscard]] ClipReport report() const;

private:
    struct Range {
        ClipCounts over;
        ClipCounts under;
        ClipCounts ceiling;  // channels >= 1.0 - file_ceiling_epsilon (used for Decode)
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
// Throws std::invalid_argument for invalid settings (see to_process_settings).
[[nodiscard]] ClipReport compute_clip_report(const Image& base, const EditSettings& settings,
                                             int threads = 0);

// Marks each pixel of `img` whose float values are out of range: bit 1 = some channel above 1.0,
// bit 2 = some channel below 0.0 (NaN is neither). One byte per pixel, row-major, so a UI can
// tint those pixels on the picture. Reads every pixel, so only ask for it while it is shown.
inline constexpr std::uint8_t clip_mask_high = 1;
inline constexpr std::uint8_t clip_mask_low = 2;
// With `file_ceiling` (for Decode), "high" means at the file's ceiling, as counted at Decode.
void compute_clip_mask(const Image& img, int threads, std::vector<std::uint8_t>& mask,
                       bool file_ceiling = false);

// "12%", "0.4%", or "<0.1%" for anything above 0 but under 0.1% (0 is "0%").
[[nodiscard]] std::string format_clip_percent(std::uint64_t count, std::uint64_t total);

// One plain sentence per direction that clips, or "Nothing clips." Examples:
//   "Highlights first clip at Exposure (2.1% of pixels); still recoverable until Output encode."
//   "Shadows clip at Output encode (0.4% of pixels)."
//   "3.2% of pixels were already clipped in the file; no edit can bring them back."
[[nodiscard]] std::string describe_clipping(const ClipReport& report);

}  // namespace prism::gui

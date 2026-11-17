#pragma once

#include <functional>
#include <optional>
#include <stop_token>
#include <string_view>

#include "prism/dodge_burn.hpp"
#include "prism/image.hpp"
#include "prism/pipeline.hpp"

namespace prism {

// What to do to an image: one field per pipeline stage. An empty field skips its stage, and so
// does a neutral value (exposure 0, the identity matrix, saturation 1, contrast 1), which would
// change nothing anyway. A tone curve always runs when set: even gamma:1 is not bit-exact.
// A plain value type, so it can be copied to another thread and compared.
struct ProcessSettings {
    std::optional<float> exposure_stops;
    // Dodge and burn. No strokes (the default) skips the stage.
    LocalExposure dodge_burn;
    // The matrix prism::white_balance_matrix builds for the photo's as-shot light and the light
    // chosen. "As shot" is no matrix at all: leave it empty.
    std::optional<Matrix3> white_balance;
    std::optional<Matrix3> matrix;
    std::optional<float> saturation;
    std::optional<float> contrast;
    std::optional<ToneCurve> curve;
    // HDR headroom in stops above white (prism/hdr.hpp): empty or 0 is SDR only. process()
    // ignores it; process_hdr() branches after Contrast when it is above 0. Optional like the
    // other fields, so a --headroom 0 on the command line can replace a recipe's [hdr].
    std::optional<float> headroom_stops;
};

// How long one stage took. `name` is one of "exposure", "dodge_burn", "white_balance", "matrix",
// "saturation", "contrast" and "curve" (the names prism --time prints), and "hdr_curve" from
// process_hdr(); it points at a string literal, so it never dangles.
struct StageTime {
    std::string_view name;
    double ms = 0;
};

// Called after each stage that ran, on the thread that called process().
using StageCallback = std::function<void(const StageTime&)>;

// Called just before each stage that will run, with the stage's name and the image it is about to
// receive (read-only), on the thread that called process().
using BeforeStageCallback = std::function<void(std::string_view name, const Image& input)>;

enum class ProcessResult {
    completed,  // every stage that `settings` asks for ran
    cancelled,  // `stop` was requested: the image is valid but only partly processed
};

// Runs the pipeline on `img` in place, in the README's order: exposure -> (dodge and burn) ->
// white balance -> colour matrix -> saturation -> contrast -> tone curve. `threads` means the same
// as for each stage (0 = one per hardware thread), and the result is the same as calling those
// stages by hand.
//
// `stop` is checked before each stage that would run (a stage itself is never interrupted). If a
// stop was requested, process() returns cancelled at once: the stages that already ran are
// applied, the rest are not, so the image holds no garbage but is unfinished. Throws
// std::invalid_argument as the stages do if the pixel buffer does not match the dimensions.
[[nodiscard]] ProcessResult process(Image& img, const ProcessSettings& settings, int threads = 0,
                                    std::stop_token stop = {}, const StageCallback& on_stage = {},
                                    const BeforeStageCallback& before_stage = {});

// process() with HDR. With settings.headroom_stops empty or 0, exactly process(), and `hdr` is left empty
// (0 x 0). Above 0, the light branches after Contrast: `img` gets the SDR branch, bit for bit what
// process() gives, and `hdr` the HDR branch: the image after Contrast through the HDR curve
// (prism/hdr.hpp), anchored on hdr_file_white(settings), linear float cut at 2^headroom. The
// stages are reported as process() reports them, with "hdr_curve" (whose input is the image
// after Contrast) after "curve".
//
// A stop works as in process() for `img`; `hdr` is then left empty (it is only ever the finished
// HDR picture or nothing). `img` and `hdr` must be different images. Throws
// std::invalid_argument, before changing either image, if the headroom is not 0 and not in
// (0, headroom_max_stops], or if the file's white comes out infinite or NaN (e.g. an exposure of
// over +127 stops); and as process() does.
[[nodiscard]] ProcessResult process_hdr(Image& img, Image& hdr, const ProcessSettings& settings,
                                        int threads = 0, std::stop_token stop = {},
                                        const StageCallback& on_stage = {},
                                        const BeforeStageCallback& before_stage = {});

}  // namespace prism

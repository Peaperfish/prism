#pragma once

#include <functional>
#include <optional>
#include <stop_token>
#include <string_view>

#include "prism/image.hpp"
#include "prism/pipeline.hpp"

namespace prism {

// What to do to an image: one field per pipeline stage. An empty field skips its stage, and so
// does a neutral value (exposure 0, the identity matrix, saturation 1, contrast 1), which would
// change nothing anyway. A tone curve always runs when set: even gamma:1 is not bit-exact.
// A plain value type, so it can be copied to another thread and compared.
struct ProcessSettings {
    std::optional<float> exposure_stops;
    std::optional<Matrix3> matrix;
    std::optional<float> saturation;
    std::optional<float> contrast;
    std::optional<ToneCurve> curve;
};

// How long one stage took. `name` is one of "exposure", "matrix", "saturation", "contrast" and
// "curve" (the names prism --time prints); it points at a string literal, so it never dangles.
struct StageTime {
    std::string_view name;
    double ms = 0;
};

// Called after each stage that ran, on the thread that called process().
using StageCallback = std::function<void(const StageTime&)>;

enum class ProcessResult {
    completed,  // every stage that `settings` asks for ran
    cancelled,  // `stop` was requested: the image is valid but only partly processed
};

// Runs the pipeline on `img` in place, in the README's order: exposure -> colour matrix ->
// saturation -> contrast -> tone curve. `threads` means the same as for each stage (0 = one per
// hardware thread), and the result is the same as calling those stages by hand.
//
// `stop` is checked before each stage that would run (a stage itself is never interrupted). If a
// stop was requested, process() returns cancelled at once: the stages that already ran are
// applied, the rest are not, so the image holds no garbage but is unfinished. Throws
// std::invalid_argument as the stages do if the pixel buffer does not match the dimensions.
[[nodiscard]] ProcessResult process(Image& img, const ProcessSettings& settings, int threads = 0,
                                    std::stop_token stop = {}, const StageCallback& on_stage = {});

}  // namespace prism

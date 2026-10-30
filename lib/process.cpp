#include "prism/process.hpp"

#include <chrono>

namespace prism {

ProcessResult process(Image& img, const ProcessSettings& settings, int threads,
                      std::stop_token stop, const StageCallback& on_stage) {
    using Clock = std::chrono::steady_clock;

    // Runs one stage and reports its time, unless a stop was requested; returns false if it was.
    // `auto&&` makes this lambda a template, so each stage's lambda is called directly.
    const auto run = [&](std::string_view name, auto&& stage) {
        if (stop.stop_requested()) {
            return false;
        }
        const auto start = Clock::now();
        stage();
        const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
        if (on_stage) {
            on_stage({name, elapsed.count()});
        }
        return true;
    };

    // The pipeline order lives here and only here: the CLI (src/main.cpp) calls this function.
    const ProcessSettings& s = settings;
    if (s.exposure_stops && *s.exposure_stops != 0.0f &&
        !run("exposure", [&] { exposure(img, *s.exposure_stops, threads); })) {
        return ProcessResult::cancelled;
    }
    if (s.matrix && *s.matrix != identity_matrix &&
        !run("matrix", [&] { apply_matrix(img, *s.matrix, threads); })) {
        return ProcessResult::cancelled;
    }
    if (s.saturation && *s.saturation != 1.0f &&
        !run("saturation", [&] { saturation(img, *s.saturation, threads); })) {
        return ProcessResult::cancelled;
    }
    if (s.contrast && *s.contrast != 1.0f &&
        !run("contrast", [&] { contrast(img, *s.contrast, threads); })) {
        return ProcessResult::cancelled;
    }
    if (s.curve && !run("curve", [&] { tone_curve(img, *s.curve, threads); })) {
        return ProcessResult::cancelled;
    }
    return ProcessResult::completed;
}

}  // namespace prism

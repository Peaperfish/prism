#include "prism/process.hpp"

#include <chrono>
#include <utility>

#include "prism/hdr.hpp"

namespace prism {

namespace {

// Runs one stage at a time on an image, reporting each stage's time, unless a stop was
// requested.
class StageRunner {
public:
    StageRunner(std::stop_token stop, const StageCallback& on_stage,
                const BeforeStageCallback& before_stage)
        : stop_(std::move(stop)), on_stage_(on_stage), before_stage_(before_stage) {}

    // Runs `stage` on `img` and returns true, or returns false at once if a stop was requested.
    // `auto&&` makes this a template, so each stage's lambda is called directly.
    bool operator()(std::string_view name, const Image& img, auto&& stage) const {
        using Clock = std::chrono::steady_clock;
        if (stop_.stop_requested()) {
            return false;
        }
        if (before_stage_) {
            before_stage_(name, img);
        }
        const auto start = Clock::now();
        stage();
        const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
        if (on_stage_) {
            on_stage_({name, elapsed.count()});
        }
        return true;
    }

private:
    std::stop_token stop_;
    const StageCallback& on_stage_;
    const BeforeStageCallback& before_stage_;
};

// Every stage before the branch: exposure through contrast. Returns false if stopped.
bool run_to_branch(Image& img, const ProcessSettings& s, int threads, const StageRunner& run) {
    if (s.exposure_stops && *s.exposure_stops != 0.0f &&
        !run("exposure", img, [&] { exposure(img, *s.exposure_stops, threads); })) {
        return false;
    }
    if (!s.dodge_burn.field.strokes.empty() &&
        !run("dodge_burn", img, [&] { dodge_burn(img, s.dodge_burn, threads); })) {
        return false;
    }
    if (s.white_balance && *s.white_balance != identity_matrix &&
        !run("white_balance", img, [&] { apply_matrix(img, *s.white_balance, threads); })) {
        return false;
    }
    if (s.matrix && *s.matrix != identity_matrix &&
        !run("matrix", img, [&] { apply_matrix(img, *s.matrix, threads); })) {
        return false;
    }
    if (s.saturation && *s.saturation != 1.0f &&
        !run("saturation", img, [&] { saturation(img, *s.saturation, threads); })) {
        return false;
    }
    if (s.contrast && *s.contrast != 1.0f &&
        !run("contrast", img, [&] { contrast(img, *s.contrast, threads); })) {
        return false;
    }
    return true;
}

// The SDR branch's tone curve. Returns false if stopped.
bool run_sdr_curve(Image& img, const ProcessSettings& s, int threads, const StageRunner& run) {
    return !s.curve || run("curve", img, [&] { tone_curve(img, *s.curve, threads); });
}

}  // namespace

// The pipeline order lives here and only here: the CLI (src/main.cpp) calls these functions.

ProcessResult process(Image& img, const ProcessSettings& settings, int threads,
                      std::stop_token stop, const StageCallback& on_stage,
                      const BeforeStageCallback& before_stage) {
    const StageRunner run(std::move(stop), on_stage, before_stage);
    if (!run_to_branch(img, settings, threads, run) ||
        !run_sdr_curve(img, settings, threads, run)) {
        return ProcessResult::cancelled;
    }
    return ProcessResult::completed;
}

ProcessResult process_hdr(Image& img, Image& hdr, const ProcessSettings& settings, int threads,
                          std::stop_token stop, const StageCallback& on_stage,
                          const BeforeStageCallback& before_stage) {
    if (settings.headroom_stops == 0.0f) {
        hdr = {};
        return process(img, settings, threads, std::move(stop), on_stage, before_stage);
    }
    // Built first, so a bad headroom throws before any pixel (of either image) changes.
    const HdrCurve curve =
        make_hdr_curve(settings.curve, hdr_file_white(settings), settings.headroom_stops);
    hdr = {};
    const StageRunner run(std::move(stop), on_stage, before_stage);
    if (!run_to_branch(img, settings, threads, run)) {
        return ProcessResult::cancelled;
    }
    hdr = img;
    if (!run_sdr_curve(img, settings, threads, run) ||
        !run("hdr_curve", hdr, [&] { hdr_curve(hdr, curve, threads); })) {
        hdr = {};  // never a half-made HDR picture
        return ProcessResult::cancelled;
    }
    return ProcessResult::completed;
}

}  // namespace prism

#include "prism/gui/clipping.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>

#include "prism/parallel.hpp"
#include "prism/process.hpp"

namespace prism::gui {

namespace {

ClipFate fate_of(std::uint64_t any, bool clamps) noexcept {
    if (any == 0) {
        return ClipFate::none;
    }
    return clamps ? ClipFate::lost : ClipFate::recoverable;
}

}  // namespace

std::optional<StageId> stage_from_name(std::string_view name) noexcept {
    for (const StageInfo& info : pipeline_stages()) {
        if (info.slug == name) {
            return info.id;
        }
    }
    return std::nullopt;
}

ProcessSettings process_settings_after(ProcessSettings full, StageId stop_after) {
    const auto reached = [&](StageId stage) {
        return static_cast<int>(stage) <= static_cast<int>(stop_after);
    };
    if (reached(StageId::exposure)) {
        full.exposure_stops.reset();
    }
    if (reached(StageId::matrix)) {
        full.matrix.reset();
    }
    if (reached(StageId::saturation)) {
        full.saturation.reset();
    }
    if (reached(StageId::contrast)) {
        full.contrast.reset();
    }
    if (reached(StageId::curve)) {
        full.curve.reset();
    }
    return full;
}

bool stage_clamps(StageId stage, ClipDirection direction) noexcept {
    switch (stage) {
        case StageId::saturation:
        case StageId::curve: return direction == ClipDirection::shadows;
        case StageId::encode: return true;
        case StageId::decode:
        case StageId::exposure:
        case StageId::matrix:
        case StageId::contrast: return false;
    }
    return false;
}

ClipRecorder::ClipRecorder(int width, int height) : width_(width), height_(height) {
    for (std::size_t i = 0; i < report_.stages.size(); ++i) {
        report_.stages[i].stage = static_cast<StageId>(i);
    }
    if (width > 0 && height > 0) {
        have_dimensions_ = true;
        const double pixels = static_cast<double>(width) * height;
        report_.stride = std::max(1, static_cast<int>(std::ceil(std::sqrt(
                                         pixels / static_cast<double>(ClipReport::max_samples)))));
        first_ = report_.stride / 2;  // centre the sample grid in its cells
        // Every x in first_, first_ + stride, ... below width, for every such y.
        const auto per_axis = [&](int n) {
            return static_cast<std::uint64_t>((n - first_ + report_.stride - 1) / report_.stride);
        };
        report_.sample_count = per_axis(width) * per_axis(height);
    }
}

ClipRecorder::Range ClipRecorder::measure(const Image& img) const {
    Range r;
    if (!have_dimensions_ || img.width != width_ || img.height != height_ ||
        img.pixels.size() != static_cast<std::size_t>(width_) * height_ * Image::channels) {
        return r;
    }
    const int stride = report_.stride;
    for (int y = first_; y < height_; y += stride) {
        const float* row = img.pixels.data() + static_cast<std::size_t>(y) * width_ * 3;
        for (int x = first_; x < width_; x += stride) {
            const float* p = row + static_cast<std::size_t>(x) * 3;
            // Comparisons are false for NaN, so NaN counts as neither; +-inf count.
            const bool hi[3] = {p[0] > 1.0f, p[1] > 1.0f, p[2] > 1.0f};
            const bool lo[3] = {p[0] < 0.0f, p[1] < 0.0f, p[2] < 0.0f};
            r.over.red += hi[0];
            r.over.green += hi[1];
            r.over.blue += hi[2];
            r.over.any += hi[0] || hi[1] || hi[2];
            r.under.red += lo[0];
            r.under.green += lo[1];
            r.under.blue += lo[2];
            r.under.any += lo[0] || lo[1] || lo[2];
        }
    }
    return r;
}

void ClipRecorder::record(StageId stage, const Range& output) {
    StageClip& row = report_.stages[static_cast<std::size_t>(stage)];
    row.ran = true;
    const bool clamps_hi = stage_clamps(stage, ClipDirection::highlights);
    const bool clamps_lo = stage_clamps(stage, ClipDirection::shadows);
    // A clamping stage's row counts what it clamped, i.e. what came in out of range.
    row.over = clamps_hi ? previous_.over : output.over;
    row.under = clamps_lo ? previous_.under : output.under;
    row.over_fate = fate_of(row.over.any, clamps_hi);
    row.under_fate = fate_of(row.under.any, clamps_lo);

    const auto note = [&](std::optional<StageId>& first, std::optional<StageId>& first_lost,
                          ClipFate fate) {
        if (fate != ClipFate::none && !first) {
            first = stage;
        }
        if (fate == ClipFate::lost && !first_lost) {
            first_lost = stage;
        }
    };
    note(report_.first_over, report_.first_over_lost, row.over_fate);
    note(report_.first_under, report_.first_under_lost, row.under_fate);
    previous_ = output;
}

void ClipRecorder::decoded(const Image& img) {
    previous_ = Range{};
    record(StageId::decode, measure(img));
}

void ClipRecorder::stage_done(StageId stage, const Image& img) {
    if (stage == StageId::decode || stage == StageId::encode) {
        return;
    }
    record(stage, measure(img));
}

void ClipRecorder::finish() {
    // The encode clamps the last stage's output; it leaves bytes in 0..255, nothing out of range.
    record(StageId::encode, Range{});
}

ClipReport ClipRecorder::report() const {
    return report_;
}

ClipReport compute_clip_report(const Image& base, const EditSettings& settings, int threads) {
    const ProcessSettings process_settings = to_process_settings(settings, final_stage);
    Image img = base;
    ClipRecorder recorder(img.width, img.height);
    recorder.decoded(img);
    const auto record = [&](const StageTime& t) {
        if (const std::optional<StageId> stage = stage_from_name(t.name)) {
            recorder.stage_done(*stage, img);
        }
    };
    (void)process(img, process_settings, threads, {}, record);
    recorder.finish();
    return recorder.report();
}

void compute_clip_mask(const Image& img, int threads, std::vector<std::uint8_t>& mask) {
    const std::size_t pixels = static_cast<std::size_t>(std::max(img.width, 0)) *
                               static_cast<std::size_t>(std::max(img.height, 0));
    if (img.pixels.size() != pixels * Image::channels) {
        throw std::invalid_argument("compute_clip_mask: pixel buffer size does not match");
    }
    mask.assign(pixels, 0);
    if (pixels == 0) {
        return;
    }
    const int n = threads_for_pixels(pixels, threads);
    parallel_for_rows(img.height, n, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const float* row = img.pixels.data() + static_cast<std::size_t>(y) * img.width * 3;
            std::uint8_t* out = mask.data() + static_cast<std::size_t>(y) * img.width;
            for (int x = 0; x < img.width; ++x) {
                const float* p = row + static_cast<std::size_t>(x) * 3;
                const bool hi = p[0] > 1.0f || p[1] > 1.0f || p[2] > 1.0f;
                const bool lo = p[0] < 0.0f || p[1] < 0.0f || p[2] < 0.0f;
                out[x] =
                    static_cast<std::uint8_t>((hi ? clip_mask_high : 0) | (lo ? clip_mask_low : 0));
            }
        }
    });
}

std::string format_clip_percent(std::uint64_t count, std::uint64_t total) {
    if (count == 0 || total == 0) {
        return "0%";
    }
    const double percent = 100.0 * static_cast<double>(count) / static_cast<double>(total);
    if (percent < 0.1) {
        return "<0.1%";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, percent < 10.0 ? "%.1f%%" : "%.0f%%", percent);
    return buffer;
}

namespace {

std::string sentence(const ClipReport& r, std::string_view noun, std::optional<StageId> first,
                     std::optional<StageId> first_lost, bool highlights) {
    if (!first) {
        return {};
    }
    const StageClip& row = r.at(*first);
    const std::uint64_t count = highlights ? row.over.any : row.under.any;
    const std::string_view name = stage_info(*first).name;
    std::string text = std::string(noun);
    const std::string amount = " (" + format_clip_percent(count, r.sample_count) + " of pixels)";
    if (first_lost && *first_lost == *first) {
        return text + " clip at " + std::string(name) + amount + ".";
    }
    text += " first clip at " + std::string(name) + amount + "; ";
    if (first_lost) {
        return text + "still recoverable until " + std::string(stage_info(*first_lost).name) + ".";
    }
    return text + "a later stage brings them back, so nothing is lost.";
}

}  // namespace

std::string describe_clipping(const ClipReport& report) {
    std::string text =
        sentence(report, "Highlights", report.first_over, report.first_over_lost, true);
    const std::string shadows =
        sentence(report, "Shadows", report.first_under, report.first_under_lost, false);
    if (!text.empty() && !shadows.empty()) {
        text += " ";
    }
    text += shadows;
    return text.empty() ? "Nothing clips." : text;
}

}  // namespace prism::gui

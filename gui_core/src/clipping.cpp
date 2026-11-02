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

// What a pixel's negative channels make it. Only a pixel with a channel below 0 can be either, so
// the luminance is worked out for those alone; the rest, nearly every pixel, cost one min.
//   low:   no light: Y <= 0, because some channel is negative. A pure black pixel (0, 0, 0) is not
//          one: it is exactly black, nothing is out of range.
//   gamut: it has light (Y > 0) but a channel is below -gamut_tolerance.
// Comparisons are false for NaN, so a NaN pixel is neither.
struct Negative {
    bool low = false;
    bool gamut = false;
};

Negative negative_kind(const float* p) noexcept {
    const float lowest = std::min(p[0], std::min(p[1], p[2]));
    if (!(lowest < 0.0f)) {
        return {};
    }
    const float y = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
    return {y <= 0.0f, y > 0.0f && lowest < -gamut_tolerance};
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
        case StageId::decode: return direction == ClipDirection::highlights;
        case StageId::saturation:
        case StageId::curve: return direction == ClipDirection::shadows;
        case StageId::encode: return true;
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

ClipRecorder::Range ClipRecorder::measure(const Image& img, int stride, int first) const {
    Range r;
    if (!have_dimensions_ || img.width <= 0 || img.height <= 0 ||
        img.pixels.size() != static_cast<std::size_t>(img.width) *
                                 static_cast<std::size_t>(img.height) * Image::channels) {
        return r;
    }
    constexpr float ceiling = 1.0f - file_ceiling_epsilon;
    for (int y = first; y < img.height; y += stride) {
        const float* row = img.pixels.data() + static_cast<std::size_t>(y) * img.width * 3;
        for (int x = first; x < img.width; x += stride) {
            const float* p = row + static_cast<std::size_t>(x) * 3;
            // Comparisons are false for NaN, so NaN counts as neither; +-inf count.
            const bool hi[3] = {p[0] > 1.0f, p[1] > 1.0f, p[2] > 1.0f};
            const Negative neg = negative_kind(p);
            const bool lo[3] = {neg.low && p[0] < 0.0f, neg.low && p[1] < 0.0f,
                                neg.low && p[2] < 0.0f};
            // The ceiling band is [1 - epsilon, 1]: an integer source never goes above 1.0, so a
            // value above it can only come from a float image, where it is real highlight data.
            const bool ce[3] = {p[0] >= ceiling && p[0] <= 1.0f, p[1] >= ceiling && p[1] <= 1.0f,
                                p[2] >= ceiling && p[2] <= 1.0f};
            r.over.red += hi[0];
            r.over.green += hi[1];
            r.over.blue += hi[2];
            r.over.any += hi[0] || hi[1] || hi[2];
            r.under.red += lo[0];
            r.under.green += lo[1];
            r.under.blue += lo[2];
            r.under.any += lo[0] || lo[1] || lo[2];
            const bool out = neg.gamut;
            const bool go[3] = {out && p[0] < -gamut_tolerance, out && p[1] < -gamut_tolerance,
                                out && p[2] < -gamut_tolerance};
            r.gamut.red += go[0];
            r.gamut.green += go[1];
            r.gamut.blue += go[2];
            r.gamut.any += out;
            r.ceiling.red += ce[0];
            r.ceiling.green += ce[1];
            r.ceiling.blue += ce[2];
            r.ceiling.any += ce[0] || ce[1] || ce[2];
        }
    }
    return r;
}

Image ClipRecorder::sample_grid(const Image& img) const {
    Image grid;
    if (!have_dimensions_ || img.width != width_ || img.height != height_ ||
        img.pixels.size() != static_cast<std::size_t>(width_) * height_ * Image::channels) {
        return grid;
    }
    const int stride = report_.stride;
    const int across = static_cast<int>((width_ - first_ + stride - 1) / stride);
    const int down = static_cast<int>((height_ - first_ + stride - 1) / stride);
    grid.width = across;
    grid.height = down;
    grid.pixels.reserve(static_cast<std::size_t>(across) * down * 3);
    for (int y = first_; y < height_; y += stride) {
        const float* row = img.pixels.data() + static_cast<std::size_t>(y) * width_ * 3;
        for (int x = first_; x < width_; x += stride) {
            const float* p = row + static_cast<std::size_t>(x) * 3;
            grid.pixels.insert(grid.pixels.end(), p, p + 3);
        }
    }
    return grid;
}

void ClipRecorder::stage_done_samples(StageId stage, const Image& grid) {
    if (stage == StageId::decode || stage == StageId::encode) {
        return;
    }
    record(stage, measure(grid, 1, 0));
}

void ClipRecorder::record(StageId stage, const Range& output) {
    StageClip& row = report_.stages[static_cast<std::size_t>(stage)];
    row.ran = true;
    const bool clamps_hi = stage_clamps(stage, ClipDirection::highlights);
    const bool clamps_lo = stage_clamps(stage, ClipDirection::shadows);
    // A clamping stage's row counts what it clamped, i.e. what came in out of range.
    // Decode's "clamp" happened in the file: its row counts the pixels at the file's ceiling
    // (lost). With none, any values above 1.0 (a float image) are out of range but recoverable.
    const bool at_ceiling = stage == StageId::decode && output.ceiling.any > 0;
    row.over = at_ceiling                              ? output.ceiling
               : clamps_hi && stage != StageId::decode ? previous_.over
                                                       : output.over;
    row.under = clamps_lo ? previous_.under : output.under;
    row.over_fate = fate_of(row.over.any, at_ceiling || (clamps_hi && stage != StageId::decode));
    row.gamut = clamps_lo ? previous_.gamut : output.gamut;
    row.under_fate = fate_of(row.under.any, clamps_lo);
    row.gamut_fate = fate_of(row.gamut.any, clamps_lo);

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
    note(report_.first_gamut, report_.first_gamut_lost, row.gamut_fate);
    previous_ = output;
}

void ClipRecorder::decoded(const Image& img) {
    previous_ = Range{};
    record(StageId::decode, measure(img, report_.stride, first_));
}

void ClipRecorder::stage_done(StageId stage, const Image& img) {
    if (stage == StageId::decode || stage == StageId::encode) {
        return;
    }
    record(stage, measure(img, report_.stride, first_));
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

namespace {

// The mask of `img`: highs too if `highs`, then low and gamut, with `earlier` (if it fits) OR-ed
// in.
void fill_mask(const Image& img, int threads, std::vector<std::uint8_t>& mask, bool highs,
               bool file_ceiling, const std::vector<std::uint8_t>* earlier) {
    const std::size_t pixels = static_cast<std::size_t>(std::max(img.width, 0)) *
                               static_cast<std::size_t>(std::max(img.height, 0));
    if (img.pixels.size() != pixels * Image::channels) {
        throw std::invalid_argument("compute_clip_mask: pixel buffer size does not match");
    }
    mask.assign(pixels, 0);
    if (pixels == 0) {
        return;
    }
    const bool merge = earlier != nullptr && earlier->size() == pixels;
    const float high = file_ceiling ? 1.0f - file_ceiling_epsilon : 1.0f;
    const int n = threads_for_pixels(pixels, threads);
    parallel_for_rows(img.height, n, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const std::size_t first = static_cast<std::size_t>(y) * img.width;
            const float* row = img.pixels.data() + first * 3;
            std::uint8_t* out = mask.data() + first;
            for (int x = 0; x < img.width; ++x) {
                const float* p = row + static_cast<std::size_t>(x) * 3;
                std::uint8_t bits = merge ? (*earlier)[first + x] : std::uint8_t{0};
                if (highs) {
                    const bool hi = file_ceiling ? (p[0] >= high || p[1] >= high || p[2] >= high)
                                                 : (p[0] > high || p[1] > high || p[2] > high);
                    bits |= hi ? clip_mask_high : 0;
                }
                const Negative neg = negative_kind(p);
                bits |= (neg.low ? clip_mask_low : 0) | (neg.gamut ? clip_mask_gamut : 0);
                out[x] = bits;
            }
        }
    });
}

}  // namespace

void compute_clip_mask(const Image& img, int threads, std::vector<std::uint8_t>& mask,
                       bool file_ceiling, const std::vector<std::uint8_t>* earlier) {
    fill_mask(img, threads, mask, true, file_ceiling, earlier);
}

void compute_negative_mask(const Image& img, int threads, std::vector<std::uint8_t>& mask) {
    fill_mask(img, threads, mask, false, false, nullptr);
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
    std::string text;
    if (report.first_over_lost == StageId::decode) {
        // Pixels at the file's ceiling were clipped before any edit. If edits push more out of
        // range, say so in the same sentence.
        text = format_clip_percent(report.at(StageId::decode).over.any, report.sample_count) +
               " of pixels were already clipped in the file";
        std::optional<StageId> later;
        for (std::size_t i = static_cast<std::size_t>(StageId::exposure);
             i < static_cast<std::size_t>(StageId::encode) && !later; ++i) {
            if (report.stages[i].over_fate != ClipFate::none) {
                later = report.stages[i].stage;
            }
        }
        if (later) {
            // Where the edits' highlights are lost, if they are (the first stage after Decode).
            std::optional<StageId> lost;
            for (std::size_t i = static_cast<std::size_t>(StageId::exposure);
                 i < report.stages.size() && !lost; ++i) {
                if (report.stages[i].over_fate == ClipFate::lost) {
                    lost = report.stages[i].stage;
                }
            }
            text +=
                ". Edits push " +
                format_clip_percent(report.at(*later).over.any, report.sample_count) +
                " over white at " + std::string(stage_info(*later).name) + "; " +
                (lost ? "lost at " + std::string(stage_info(*lost).name) : "still recoverable") +
                ".";
        } else {
            text += "; no edit can bring them back.";
        }
    } else {
        text = sentence(report, "Highlights", report.first_over, report.first_over_lost, true);
    }
    const std::string shadows =
        sentence(report, "Shadows", report.first_under, report.first_under_lost, false);
    if (!text.empty() && !shadows.empty()) {
        text += " ";
    }
    text += shadows;
    return text.empty() ? "Nothing clips." : text;
}

std::string describe_gamut(const ClipReport& report) {
    if (!report.first_gamut) {
        return {};
    }
    const std::uint64_t count = report.at(*report.first_gamut).gamut.any;
    std::string text = format_clip_percent(count, report.sample_count) +
                       " of colours are too saturated for sRGB after " +
                       std::string(stage_info(*report.first_gamut).name) + "; ";
    if (report.first_gamut_lost) {
        return text + std::string(stage_info(*report.first_gamut_lost).name) +
               " clips them, so hues may shift.";
    }
    return text + "a later stage brings them back, so nothing is lost.";
}

}  // namespace prism::gui

#include "prism/frame.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <sstream>
#include <stdexcept>

#include "prism/parallel.hpp"
#include "prism/ranges.hpp"
#include "prism/resize.hpp"
#include "sample_ranges.hpp"

namespace prism {

namespace {

bool known_aspect(Aspect a) noexcept {
    return std::find(all_aspects.begin(), all_aspects.end(), a) != all_aspects.end();
}

// Throws std::invalid_argument with all of validate_frame's messages, as to_process_settings does
// for EditSettings.
void require_valid(const Frame& f, int width, int height, const char* who) {
    const std::vector<std::string> problems = validate_frame(f);
    if (!problems.empty()) {
        std::string message = std::string(who) + ": invalid frame:";
        for (const std::string& p : problems) {
            message += " " + p + ".";
        }
        throw std::invalid_argument(message);
    }
    if (width < 1 || height < 1) {
        throw std::invalid_argument(std::string(who) + ": width and height must be at least 1");
    }
}

// cos and sin of the frame's angle, worked out once, in double. At exactly 0 they are exactly
// 1 and 0.
struct Turn {
    double c = 1.0;
    double s = 0.0;
};

Turn turn_of(float angle_degrees) noexcept {
    const double a = static_cast<double>(angle_degrees) * (std::numbers::pi / 180.0);
    return {std::cos(a), std::sin(a)};
}

// round(fraction * n), halves up, within [0, n].
int edge_pixel(float fraction, int n) noexcept {
    const double v = std::floor(static_cast<double>(fraction) * n + 0.5);
    return static_cast<int>(std::clamp(v, 0.0, static_cast<double>(n)));
}

// Catmull-Rom weights for a position t in [0, 1) past the second of four samples.
void catmull_rom(float t, float w[4]) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    w[0] = 0.5f * (-t3 + 2.0f * t2 - t);
    w[1] = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
    w[2] = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
    w[3] = 0.5f * (t3 - t2);
}

// The four source indices around floor position `i0` (i0 - 1 .. i0 + 2), each held inside
// [0, n - 1], which repeats the edge pixel past the photo's edge.
void taps(int i0, int n, int out[4]) noexcept {
    for (int k = 0; k < 4; ++k) {
        out[k] = std::clamp(i0 - 1 + k, 0, n - 1);
    }
}

}  // namespace

std::vector<std::string> validate_frame(const Frame& f) {
    std::vector<std::string> problems;
    if (!in_range(f.angle, frame_angle_range)) {
        problems.push_back(range_message("Frame angle", f.angle, frame_angle_range));
    }
    constexpr ValueRange unit{0.0, 1.0};
    const auto edges = [&](const char* low_name, float low, const char* high_name, float high) {
        const bool low_ok = in_range(low, unit);
        const bool high_ok = in_range(high, unit);
        if (!low_ok) {
            problems.push_back(range_message(std::string("Frame ") + low_name, low, unit));
        }
        if (!high_ok) {
            problems.push_back(range_message(std::string("Frame ") + high_name, high, unit));
        }
        if (low_ok && high_ok && !(low < high)) {
            std::ostringstream message;
            message << "Frame " << low_name << " must be less than " << high_name << " ("
                    << low_name << " is " << low << ", " << high_name << " is " << high << ")";
            problems.push_back(message.str());
        }
    };
    edges("left", f.left, "right", f.right);
    edges("top", f.top, "bottom", f.bottom);
    if (!known_aspect(f.aspect)) {
        problems.push_back("Frame aspect is not one of the known shapes");
    }
    return problems;
}

std::optional<double> aspect_ratio(Aspect aspect, bool portrait, int width, int height) {
    if (width < 1 || height < 1) {
        throw std::invalid_argument("aspect_ratio: width and height must be at least 1");
    }
    if (!known_aspect(aspect)) {
        throw std::invalid_argument("aspect_ratio: not one of the known shapes");
    }
    // The long side over the short one, before following the photo's orientation.
    double long_over_short = 0.0;
    switch (aspect) {
        case Aspect::free: return std::nullopt;
        case Aspect::original:
            long_over_short = static_cast<double>(std::max(width, height)) / std::min(width, height);
            break;
        case Aspect::square: long_over_short = 1.0; break;
        case Aspect::four_five: long_over_short = 5.0 / 4.0; break;
        case Aspect::five_seven: long_over_short = 7.0 / 5.0; break;
        case Aspect::three_two: long_over_short = 3.0 / 2.0; break;
        case Aspect::sixteen_nine: long_over_short = 16.0 / 9.0; break;
    }
    const bool landscape = (width >= height) != portrait;
    return landscape ? long_over_short : 1.0 / long_over_short;
}

bool is_whole_photo(const Frame& f) noexcept {
    return f.angle == 0.0f && f.left == 0.0f && f.top == 0.0f && f.right == 1.0f &&
           f.bottom == 1.0f;
}

Frame apply_aspect(const Frame& f, int width, int height) {
    require_valid(f, width, height, "apply_aspect");
    const std::optional<double> ratio = aspect_ratio(f.aspect, f.portrait, width, height);
    if (!ratio) {
        return f;
    }
    const double W = width;
    const double H = height;
    const double cx = (static_cast<double>(f.left) + f.right) / 2.0;
    const double cy = (static_cast<double>(f.top) + f.bottom) / 2.0;
    const double w = (static_cast<double>(f.right) - f.left) * W;
    const double h = (static_cast<double>(f.bottom) - f.top) * H;
    // Keep the whole side that is already short enough and shrink the other one.
    // A rectangle already of the right shape, to within rounding, is kept as it is.
    constexpr double tolerance = 1e-9;
    const double shape = (w / h) / *ratio;
    double new_w = w;
    double new_h = h;
    if (shape > 1.0 + tolerance) {
        new_w = h * *ratio;
    } else if (shape < 1.0 - tolerance) {
        new_h = w / *ratio;
    }
    Frame out = f;
    const auto edge = [](double centre, double size, double n, int sign) {
        return static_cast<float>(std::clamp(centre + sign * size / n / 2.0, 0.0, 1.0));
    };
    if (new_w != w) {
        out.left = edge(cx, new_w, W, -1);
        out.right = edge(cx, new_w, W, 1);
    }
    if (new_h != h) {
        out.top = edge(cy, new_h, H, -1);
        out.bottom = edge(cy, new_h, H, 1);
    }
    return out;
}

Frame fit_frame(const Frame& f, int width, int height) {
    require_valid(f, width, height, "fit_frame");
    const Turn t = turn_of(f.angle);
    const double W = width;
    const double H = height;
    const double half_w = W / 2.0;
    const double half_h = H / 2.0;

    // The rectangle along the straightened axes, in pixels from the photo's centre: centre
    // (cx, cy), half-width ax, half-height ay.
    double cx = ((static_cast<double>(f.left) + f.right) / 2.0 - 0.5) * W;
    double cy = ((static_cast<double>(f.top) + f.bottom) / 2.0 - 0.5) * H;
    const double ax = (static_cast<double>(f.right) - f.left) / 2.0 * W;
    const double ay = (static_cast<double>(f.bottom) - f.top) / 2.0 * H;

    // 1. The centre on the photo, (pu, pv) from the photo's centre. Outside the photo it moves
    // straight towards the photo's centre until it is half a pixel inside (so the shrink below
    // leaves a rectangle of some size; on a side of one pixel, all the way). Moving towards the
    // centre, rather than to the nearest point, keeps it inside the straightened rectangle
    // [0, 1] x [0, 1] as well, where it started, and the rectangle with it. The move scales
    // (pu, pv) and (cx, cy) alike, as R is linear.
    double pu = t.c * cx + t.s * cy;
    double pv = -t.s * cx + t.c * cy;
    if (std::abs(pu) > half_w || std::abs(pv) > half_h) {
        const double reach_u = std::max(half_w - 0.5, 0.0);
        const double reach_v = std::max(half_h - 0.5, 0.0);
        double m = 1.0;
        if (std::abs(pu) > reach_u) {
            m = std::min(m, reach_u / std::abs(pu));
        }
        if (std::abs(pv) > reach_v) {
            m = std::min(m, reach_v / std::abs(pv));
        }
        pu *= m;
        pv *= m;
        cx *= m;
        cy *= m;
    }

    // 2. Scaling the half-sizes by k puts the corner (sx, sy) (each +1 or -1) on the photo at
    // (pu + k * bu, pv + k * bv), linear in k, so the largest k that keeps it inside comes straight
    // from each edge: |pu + k * bu| <= W / 2 holds up to k = (W / 2 - sign(bu) * pu) / |bu|.
    double k = 1.0;
    for (const double sx : {-1.0, 1.0}) {
        for (const double sy : {-1.0, 1.0}) {
            const double bu = t.c * sx * ax + t.s * sy * ay;
            const double bv = -t.s * sx * ax + t.c * sy * ay;
            if (bu != 0.0) {
                k = std::min(k, (half_w - (bu > 0.0 ? pu : -pu)) / std::abs(bu));
            }
            if (bv != 0.0) {
                k = std::min(k, (half_h - (bv > 0.0 ? pv : -pv)) / std::abs(bv));
            }
        }
    }
    k = std::max(k, 0.0);

    // Back to fractions. A frame that already fitted (k = 1, centre not moved) comes back as it
    // was: the double arithmetic is far finer than a float, so each fraction rounds back to itself.
    Frame out = f;
    const auto fraction = [](double pixels_from_centre, double n) {
        return static_cast<float>(std::clamp(pixels_from_centre / n + 0.5, 0.0, 1.0));
    };
    out.left = fraction(cx - k * ax, W);
    out.right = fraction(cx + k * ax, W);
    out.top = fraction(cy - k * ay, H);
    out.bottom = fraction(cy + k * ay, H);
    // A side shrunk below what a float can hold apart: keep it one float step wide.
    const auto keep_apart = [](float& low, float& high) {
        if (low < high) {
            return;
        }
        if (high < 1.0f) {
            high = std::nextafter(low, 2.0f);
        } else {
            low = std::nextafter(1.0f, 0.0f);
        }
    };
    keep_apart(out.left, out.right);
    keep_apart(out.top, out.bottom);
    return out;
}

FrameBox framed_box(const Frame& f, int width, int height) {
    require_valid(f, width, height, "framed_box");
    int x0 = edge_pixel(f.left, width);
    int x1 = edge_pixel(f.right, width);
    int y0 = edge_pixel(f.top, height);
    int y1 = edge_pixel(f.bottom, height);
    // left < right, so x0 <= x1; a frame under half a pixel wide rounds to x0 == x1.
    if (x1 == x0) {
        if (x1 < width) {
            ++x1;
        } else {
            --x0;
        }
    }
    if (y1 == y0) {
        if (y1 < height) {
            ++y1;
        } else {
            --y0;
        }
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

PixelSize framed_size(const Frame& f, int width, int height) {
    const FrameBox box = framed_box(f, width, height);
    return {box.width, box.height};
}

FrameMap::FrameMap(const Frame& f, int width, int height)
    : width_(width), height_(height), box_(framed_box(f, width, height)) {
    const Turn t = turn_of(f.angle);
    cos_ = t.c;
    sin_ = t.s;
}

FramePoint FrameMap::to_photo_pixels(FramePoint p) const noexcept {
    // From the framed image to straightened pixels from the photo's centre, then through R.
    const double dx = (box_.x0 + p.x) - width_ / 2.0;
    const double dy = (box_.y0 + p.y) - height_ / 2.0;
    return {width_ / 2.0 + (cos_ * dx + sin_ * dy), height_ / 2.0 + (-sin_ * dx + cos_ * dy)};
}

PhotoPoint FrameMap::to_photo(FramePoint p) const noexcept {
    const FramePoint q = to_photo_pixels(p);
    return {q.x / width_, q.y / height_};
}

FramePoint FrameMap::to_frame(PhotoPoint q) const noexcept {
    // R's inverse is its transpose.
    const double du = q.u * width_ - width_ / 2.0;
    const double dv = q.v * height_ - height_ / 2.0;
    return {width_ / 2.0 + (cos_ * du - sin_ * dv) - box_.x0,
            height_ / 2.0 + (sin_ * du + cos_ * dv) - box_.y0};
}

PhotoPoint frame_to_photo(const Frame& f, int width, int height, FramePoint p) {
    return FrameMap(f, width, height).to_photo(p);
}

FramePoint photo_to_frame(const Frame& f, int width, int height, PhotoPoint q) {
    return FrameMap(f, width, height).to_frame(q);
}

namespace {

// Throws std::invalid_argument unless `part` is a non-empty part of the framed image `box`
// (columns [0, box.width), rows [0, box.height)).
void require_part(const FrameBox& box, const FrameBox& part, const char* who) {
    if (part.width < 1 || part.height < 1) {
        throw std::invalid_argument(std::string(who) + ": the part must be at least 1 x 1 pixel");
    }
    // In long long, so a part near INT_MAX can't overflow the sums.
    if (part.x0 < 0 || part.y0 < 0 ||
        static_cast<long long>(part.x0) + part.width > box.width ||
        static_cast<long long>(part.y0) + part.height > box.height) {
        std::ostringstream message;
        message << who << ": the part " << part.width << " x " << part.height << " at ("
                << part.x0 << ", " << part.y0 << ") is not inside the framed image, "
                << box.width << " x " << box.height;
        throw std::invalid_argument(message.str());
    }
}

}  // namespace

Image apply_frame(const Image& img, const Frame& f, int threads,
                  const std::optional<FrameBox>& part) {
    detail::require_consistent(img, "apply_frame");
    require_valid(f, img.width, img.height, "apply_frame");
    const FrameMap map(f, img.width, img.height);
    const FrameBox& box = map.box();
    // The framed pixels to compute, in framed-image pixels: all of them unless a part is asked for.
    const FrameBox want = part ? *part : FrameBox{0, 0, box.width, box.height};
    require_part(box, want, "apply_frame");

    Image out;
    out.width = want.width;
    out.height = want.height;
    out.pixels.resize(detail::sample_count(out.width, out.height));

    // The work grows with the pixels computed, so that decides whether to use several threads.
    // Each thread writes only its own output rows and reads only the photo.
    const int n = threads_for_pixels(
        static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), threads);
    const std::size_t src_row = detail::sample_count(img.width, 1);
    const std::size_t dst_row = detail::sample_count(out.width, 1);

    // Output pixel (ii, jj) is framed pixel (i, j) = (want.x0 + ii, want.y0 + jj), and is worked
    // out from that (i, j) alone, so a part holds exactly the same values as the same pixels of
    // the whole framed image.
    if (f.angle == 0.0f) {
        // Straightened axes are the photo's: framed pixel (i, j) is photo pixel (x0 + i, y0 + j).
        parallel_for_rows(out.height, n, [&](int begin, int end) {
            for (int jj = begin; jj < end; ++jj) {
                const int j = want.y0 + jj;
                const float* src = img.pixels.data() +
                                   static_cast<std::size_t>(box.y0 + j) * src_row +
                                   detail::sample_count(box.x0 + want.x0, 1);
                std::memcpy(out.pixels.data() + static_cast<std::size_t>(jj) * dst_row, src,
                            dst_row * sizeof(float));
            }
        });
        return out;
    }

    const int w = img.width;
    const int h = img.height;
    const float* pixels = img.pixels.data();
    parallel_for_rows(out.height, n, [&](int begin, int end) {
        for (int jj = begin; jj < end; ++jj) {
            const int j = want.y0 + jj;
            float* dst = out.pixels.data() + static_cast<std::size_t>(jj) * dst_row;
            for (int ii = 0; ii < out.width; ++ii) {
                const int i = want.x0 + ii;
                // Each pixel from its own (i, j): no position is carried from one pixel to the
                // next, so neither a row range's start nor the part's corner changes a value.
                const FramePoint p = map.to_photo_pixels({i + 0.5, j + 0.5});
                // Photo pixel centres sit at whole numbers in these coordinates.
                const double sx = p.x - 0.5;
                const double sy = p.y - 0.5;
                const double fx = std::floor(sx);
                const double fy = std::floor(sy);
                float wx[4];
                float wy[4];
                catmull_rom(static_cast<float>(sx - fx), wx);
                catmull_rom(static_cast<float>(sy - fy), wy);
                int xs[4];
                int ys[4];
                // Positions stay within a few photo sizes of the photo (the rectangle is inside
                // [0, 1] and turned at most 45 degrees), so they fit an int.
                taps(static_cast<int>(fx), w, xs);
                taps(static_cast<int>(fy), h, ys);

                float sum[3] = {0.0f, 0.0f, 0.0f};
                for (int r = 0; r < 4; ++r) {
                    const float* row = pixels + static_cast<std::size_t>(ys[r]) * src_row;
                    float across[3] = {0.0f, 0.0f, 0.0f};
                    for (int k = 0; k < 4; ++k) {
                        const float* q = row + detail::sample_count(xs[k], 1);
                        across[0] += wx[k] * q[0];
                        across[1] += wx[k] * q[1];
                        across[2] += wx[k] * q[2];
                    }
                    sum[0] += wy[r] * across[0];
                    sum[1] += wy[r] * across[1];
                    sum[2] += wy[r] * across[2];
                }

                // Clamp to the 2 x 2 nearest: taps 1 and 2 on each axis.
                const float* a = pixels + static_cast<std::size_t>(ys[1]) * src_row +
                                 detail::sample_count(xs[1], 1);
                const float* b = pixels + static_cast<std::size_t>(ys[1]) * src_row +
                                 detail::sample_count(xs[2], 1);
                const float* c = pixels + static_cast<std::size_t>(ys[2]) * src_row +
                                 detail::sample_count(xs[1], 1);
                const float* d = pixels + static_cast<std::size_t>(ys[2]) * src_row +
                                 detail::sample_count(xs[2], 1);
                float* o = dst + detail::sample_count(ii, 1);
                for (int ch = 0; ch < 3; ++ch) {
                    const float lo = std::min(std::min(a[ch], b[ch]), std::min(c[ch], d[ch]));
                    const float hi = std::max(std::max(a[ch], b[ch]), std::max(c[ch], d[ch]));
                    o[ch] = std::min(std::max(sum[ch], lo), hi);
                }
            }
        }
    });
    return out;
}

FrameBox frame_source_rect(const Frame& f, int width, int height, const FrameBox& part) {
    require_valid(f, width, height, "frame_source_rect");
    const FrameMap map(f, width, height);
    const FrameBox& box = map.box();
    require_part(box, part, "frame_source_rect");
    if (f.angle == 0.0f) {
        // apply_frame copies: the part's own pixels, moved by the box's corner.
        return {box.x0 + part.x0, box.y0 + part.y0, part.width, part.height};
    }
    // apply_frame samples framed pixel centres. Its photo positions only grow (or only shrink)
    // along a row or a column, rounding included (cos > 0 within 45 degrees, and rounding never
    // reverses an order), so the extremes are at the four corner pixels' centres. Taken exactly as
    // apply_frame takes them, then widened by the taps -1 and +2 and held inside the photo as taps
    // are.
    double lo_x = 0.0;
    double hi_x = 0.0;
    double lo_y = 0.0;
    double hi_y = 0.0;
    bool first = true;
    for (const int i : {part.x0, part.x0 + part.width - 1}) {
        for (const int j : {part.y0, part.y0 + part.height - 1}) {
            const FramePoint p = map.to_photo_pixels({i + 0.5, j + 0.5});
            const double fx = std::floor(p.x - 0.5);
            const double fy = std::floor(p.y - 0.5);
            lo_x = first ? fx : std::min(lo_x, fx);
            hi_x = first ? fx : std::max(hi_x, fx);
            lo_y = first ? fy : std::min(lo_y, fy);
            hi_y = first ? fy : std::max(hi_y, fy);
            first = false;
        }
    }
    const auto held = [](double v, int n) {
        return static_cast<int>(std::clamp(v, 0.0, static_cast<double>(n - 1)));
    };
    const int x0 = held(lo_x - 1.0, width);
    const int x1 = held(hi_x + 2.0, width);
    const int y0 = held(lo_y - 1.0, height);
    const int y1 = held(hi_y + 2.0, height);
    return {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

ExportFrame plan_export_frame(const std::optional<Frame>& frame, int width, int height) {
    if (!frame) {
        if (width < 1 || height < 1) {
            throw std::invalid_argument("plan_export_frame: width and height must be at least 1");
        }
        return {std::nullopt, {width, height}, false};
    }
    require_valid(*frame, width, height, "plan_export_frame");
    if (is_whole_photo(*frame)) {
        return {std::nullopt, {width, height}, false};
    }
    const Frame fitted = fit_frame(*frame, width, height);
    return {fitted, framed_size(fitted, width, height), fitted != *frame};
}

FramedExport frame_and_reduce(Image decoded, const std::optional<Frame>& frame,
                              std::optional<int> long_side, int threads) {
    FramedExport out;
    out.frame = plan_export_frame(frame, decoded.width, decoded.height);
    using Clock = std::chrono::steady_clock;
    const auto millis = [](Clock::time_point from) {
        return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
    };
    if (out.frame.fitted) {
        const auto start = Clock::now();
        // Assigning frees the full decode before the reduction below allocates.
        decoded = apply_frame(decoded, *out.frame.fitted, threads);
        out.frame_ms = millis(start);
        out.frame_threads = threads_for_pixels(
            static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.height),
            threads);
    }
    if (long_side && std::max(decoded.width, decoded.height) > *long_side) {
        const auto start = Clock::now();
        out.reduce_threads = threads_for_pixels(
            static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.height),
            threads);
        decoded = downscale(decoded, *long_side, threads);
        out.reduce_ms = millis(start);
        out.reduced = true;
    }
    out.image = std::move(decoded);
    return out;
}

}  // namespace prism

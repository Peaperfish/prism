#include "prism/frame.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/resize.hpp"

namespace {

using prism::Aspect;
using prism::Frame;
using prism::FrameBox;
using prism::FrameMap;
using prism::FramePoint;
using prism::Image;
using prism::PhotoPoint;

Frame frame_of(float angle, float l, float t, float r, float b) {
    Frame f;
    f.angle = angle;
    f.left = l;
    f.top = t;
    f.right = r;
    f.bottom = b;
    return f;
}

// A W x H image whose every sample is different and not a round number: a smooth part plus a
// little deterministic noise, so interpolation and its rounding are exercised everywhere.
Image patterned(int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    std::uint32_t state = 2463534242u;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                const float noise = static_cast<float>(state % 1000) / 4000.0f;
                img.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                            static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(c)] =
                    0.1f + 0.7f * static_cast<float>(x) / static_cast<float>(w) +
                    0.3f * static_cast<float>(y * (c + 1)) / static_cast<float>(h) + noise;
            }
        }
    }
    return img;
}

const float* pixel(const Image& img, int x, int y) {
    return img.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                                static_cast<std::size_t>(x)) * 3;
}

// Index of the first sample whose bits differ, or -1 (also 0 if the sizes differ).
long long first_bit_difference(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size()) {
        return 0;
    }
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(a.pixels[i]) != std::bit_cast<std::uint32_t>(b.pixels[i])) {
            return static_cast<long long>(i);
        }
    }
    return -1;
}

// --- 0 degrees: a copy ---------------------------------------------------------------------------

TEST(Frame, ZeroAngleCopiesTheBoxPixelForPixel) {
    const Image img = patterned(37, 23);
    const Frame f = frame_of(0.0f, 0.1f, 0.2f, 0.7f, 0.9f);
    // round(3.7) = 4, round(25.9) = 26; round(4.6) = 5, round(20.7) = 21.
    const FrameBox box = prism::framed_box(f, 37, 23);
    EXPECT_EQ(box, (FrameBox{4, 5, 22, 16}));
    const Image out = prism::apply_frame(img, f, 1);
    ASSERT_EQ(out.width, 22);
    ASSERT_EQ(out.height, 16);
    for (int j = 0; j < out.height; ++j) {
        for (int i = 0; i < out.width; ++i) {
            for (int c = 0; c < 3; ++c) {
                ASSERT_EQ(std::bit_cast<std::uint32_t>(pixel(out, i, j)[c]),
                          std::bit_cast<std::uint32_t>(pixel(img, box.x0 + i, box.y0 + j)[c]))
                    << "at " << i << ", " << j;
            }
        }
    }
}

TEST(Frame, WholePhotoAtZeroIsTheInput) {
    Image img = patterned(41, 17);
    // At exactly 0 degrees the Catmull-Rom weights are 0, 1, 0, 0, which would give the same bits
    // for finite values; an infinity tells a copy (inf) from a resample (0 * inf = NaN).
    img.pixels[(8 * 41 + 20) * 3 + 1] = std::numeric_limits<float>::infinity();
    const Image out = prism::apply_frame(img, Frame{}, 1);
    EXPECT_EQ(first_bit_difference(out, img), -1);
    // -0 is 0 as well: still a copy.
    EXPECT_EQ(first_bit_difference(prism::apply_frame(img, frame_of(-0.0f, 0, 0, 1, 1), 1), img), -1);
}

// --- Pixel size ----------------------------------------------------------------------------------

TEST(Frame, FramedSizeRoundsEachEdgeHalfUp) {
    // 0.25 * 10 = 2.5 -> 3 and 0.75 * 10 = 7.5 -> 8 (both exact in float and double).
    EXPECT_EQ(prism::framed_box(frame_of(0, 0.25f, 0.25f, 0.75f, 0.75f), 10, 10),
              (FrameBox{3, 3, 5, 5}));
    // 0.24 * 10 = 2.4 -> 2 and 0.76 * 10 = 7.6 -> 8.
    EXPECT_EQ(prism::framed_box(frame_of(0, 0.24f, 0.0f, 0.76f, 1.0f), 10, 4),
              (FrameBox{2, 0, 6, 4}));
    EXPECT_EQ(prism::framed_size(frame_of(0, 0.24f, 0.0f, 0.76f, 1.0f), 10, 4),
              (prism::PixelSize{6, 4}));
    // The whole photo, at any angle.
    EXPECT_EQ(prism::framed_size(frame_of(12.5f, 0, 0, 1, 1), 9504, 6336),
              (prism::PixelSize{9504, 6336}));
}

TEST(Frame, FramedSizeIsNeverEmptyOnTinyImages) {
    // 1 x 1: every valid frame frames the one pixel.
    for (const Frame& f : {frame_of(0, 0.1f, 0.1f, 0.2f, 0.2f), frame_of(0, 0.8f, 0.8f, 0.9f, 0.9f),
                           frame_of(30, 0.4f, 0.0f, 0.6f, 1.0f), Frame{}}) {
        EXPECT_EQ(prism::framed_box(f, 1, 1), (FrameBox{0, 0, 1, 1}));
    }
    // 2 x 1.
    EXPECT_EQ(prism::framed_box(frame_of(0, 0.0f, 0.3f, 0.2f, 0.4f), 2, 1), (FrameBox{0, 0, 1, 1}));
    EXPECT_EQ(prism::framed_box(frame_of(0, 0.6f, 0.0f, 0.7f, 1.0f), 2, 1), (FrameBox{1, 0, 1, 1}));
    EXPECT_EQ(prism::framed_box(frame_of(0, 0.9f, 0.0f, 1.0f, 1.0f), 2, 1), (FrameBox{1, 0, 1, 1}));
    EXPECT_EQ(prism::framed_box(frame_of(0, 0.0f, 0.0f, 1.0f, 1.0f), 2, 1), (FrameBox{0, 0, 2, 1}));
    // Every frame on a grid of edges, on every size up to 5 x 5: inside the image and not empty.
    const std::vector<float> edges = {0.0f, 0.05f, 0.1f, 0.25f, 0.3f, 0.5f, 0.55f, 0.75f, 0.9f, 1.0f};
    for (int w = 1; w <= 5; ++w) {
        for (int h = 1; h <= 5; ++h) {
            for (const float l : edges) {
                for (const float r : edges) {
                    if (!(l < r)) {
                        continue;
                    }
                    const FrameBox box = prism::framed_box(frame_of(0, l, 1.0f - r, r, 1.0f - l), w, h);
                    ASSERT_GE(box.width, 1) << w << "x" << h << " " << l << ".." << r;
                    ASSERT_GE(box.height, 1);
                    ASSERT_GE(box.x0, 0);
                    ASSERT_GE(box.y0, 0);
                    ASSERT_LE(box.x0 + box.width, w);
                    ASSERT_LE(box.y0 + box.height, h);
                }
            }
        }
    }
}

// The 2560 px preview, a RAW half-size decode and the full decode of a 9504 x 6336 photo frame the
// same part of it: each edge is within half a pixel of the fractions on every decode, so the boxes,
// as fractions of their photo, agree to within one pixel of the coarser decode.
TEST(Frame, PreviewAndFullSizeFrameTheSamePartWithinOnePixel) {
    const prism::PixelSize full{9504, 6336};
    const prism::PixelSize preview = prism::downscaled_size(full.width, full.height, 2560);
    const prism::PixelSize half{full.width / 2, full.height / 2};
    ASSERT_EQ(preview, (prism::PixelSize{2560, 1707}));
    const std::vector<Frame> frames = {
        frame_of(0, 0.1f, 0.2f, 0.7f, 0.9f),       frame_of(3.3f, 0.123f, 0.0f, 0.877f, 0.5f),
        frame_of(-45, 0.3333f, 0.3333f, 0.6667f, 0.6667f), frame_of(0, 0.4999f, 0.25f, 0.5001f, 0.75f),
        frame_of(0, 0.0001f, 0.0001f, 0.9999f, 0.9999f),    Frame{}};
    for (const Frame& f : frames) {
        // On each decode, each edge is within half a pixel of its fraction (a whole pixel for a
        // frame that had to be widened to one pixel).
        for (const prism::PixelSize d : {preview, half, full}) {
            const FrameBox box = prism::framed_box(f, d.width, d.height);
            const double tx = box.width == 1 ? 1.0 : 0.5;
            const double ty = box.height == 1 ? 1.0 : 0.5;
            EXPECT_LE(std::abs(box.x0 - double(f.left) * d.width), tx);
            EXPECT_LE(std::abs(box.x0 + box.width - double(f.right) * d.width), tx);
            EXPECT_LE(std::abs(box.y0 - double(f.top) * d.height), ty);
            EXPECT_LE(std::abs(box.y0 + box.height - double(f.bottom) * d.height), ty);
        }
        for (const auto& [a, b] : {std::pair{preview, full}, std::pair{half, full}, std::pair{preview, half}}) {
            const FrameBox ba = prism::framed_box(f, a.width, a.height);
            const FrameBox bb = prism::framed_box(f, b.width, b.height);
            // One pixel of the coarser decode (a), as a fraction.
            const double px = 1.0 / a.width;
            const double py = 1.0 / a.height;
            EXPECT_LE(std::abs(ba.x0 / double(a.width) - bb.x0 / double(b.width)), px);
            EXPECT_LE(std::abs((ba.x0 + ba.width) / double(a.width) - (bb.x0 + bb.width) / double(b.width)), px);
            EXPECT_LE(std::abs(ba.y0 / double(a.height) - bb.y0 / double(b.height)), py);
            EXPECT_LE(std::abs((ba.y0 + ba.height) / double(a.height) - (bb.y0 + bb.height) / double(b.height)), py);
            // And so the sizes, scaled to the coarser decode, are within a pixel of each other
            // plus the finer decode's share of rounding.
            EXPECT_LE(std::abs(ba.width - bb.width * double(a.width) / b.width),
                      1.0 + double(a.width) / b.width);
        }
    }
}

// --- Threads -------------------------------------------------------------------------------------

TEST(Frame, OneAndEightThreadsAreBitIdentical) {
    constexpr int W = 2400;
    constexpr int H = 1400;
    const Image img = patterned(W, H);
    // The whole photo, not fitted, so the framed image is 2400 x 1400 at every angle and really is
    // split across threads (apply_frame decides by the framed image's size).
    const std::size_t whole_pixels = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    ASSERT_GE(whole_pixels, prism::parallel_min_pixels * 3 / 2);
    ASSERT_EQ(prism::threads_for_pixels(whole_pixels, 8), 8);
    for (const float angle : {-45.0f, -17.3f, -0.1f, 0.1f, 2.5f, 33.3f, 45.0f}) {
        const Frame f = frame_of(angle, 0, 0, 1, 1);
        const Image one = prism::apply_frame(img, f, 1);
        const Image eight = prism::apply_frame(img, f, 8);
        ASSERT_EQ(one.width, W);
        ASSERT_EQ(one.height, H);
        EXPECT_EQ(first_bit_difference(one, eight), -1) << "angle " << angle;
    }
    // A fitted frame (smaller, an offset box), still well above 1.5x parallel_min_pixels after
    // fitting so apply_frame runs multi-threaded on it too.
    Frame f = frame_of(7.0f, 0.1f, 0.05f, 0.95f, 0.9f);
    f = prism::fit_frame(f, W, H);
    const Image fitted_one = prism::apply_frame(img, f, 1);
    const Image fitted_eight = prism::apply_frame(img, f, 8);
    const std::size_t fitted_pixels =
        static_cast<std::size_t>(fitted_one.width) * static_cast<std::size_t>(fitted_one.height);
    ASSERT_GE(fitted_pixels, prism::parallel_min_pixels * 3 / 2);
    ASSERT_EQ(prism::threads_for_pixels(fitted_pixels, 8), 8);
    EXPECT_EQ(first_bit_difference(fitted_one, fitted_eight), -1);
}

// --- Values --------------------------------------------------------------------------------------

TEST(Frame, ConstantImageStaysExactlyConstant) {
    Image img;
    img.width = 64;
    img.height = 48;
    const float value[3] = {0.3712f, 1.0f, 7.25f};
    for (std::size_t i = 0; i < 64 * 48; ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            img.pixels.push_back(value[c]);
        }
    }
    for (const float angle : {-45.0f, -31.7f, -1.0f, 0.1f, 0.0f, 13.13f, 45.0f}) {
        for (const Frame& f : {frame_of(angle, 0, 0, 1, 1),
                               prism::fit_frame(frame_of(angle, 0.2f, 0.1f, 0.9f, 0.8f), 64, 48)}) {
            const Image out = prism::apply_frame(img, f, 1);
            for (std::size_t i = 0; i < out.pixels.size(); ++i) {
                ASSERT_EQ(std::bit_cast<std::uint32_t>(out.pixels[i]),
                          std::bit_cast<std::uint32_t>(value[i % 3]))
                    << "angle " << angle << " sample " << i << ": " << out.pixels[i];
            }
        }
    }
}

// Catmull-Rom reproduces any quadratic exactly (it is third-order accurate), and a quadratic that
// rises across the whole photo always lies between its 2 x 2 nearest samples, so the clamp never
// acts: each turned pixel must match the formula at its position to float rounding. The clamp
// alone cannot hide a wrong filter here, as it does on a flat image; bilinear, for one, is off by
// up to f'' / 8 = 2.5e-5.
TEST(Frame, TurnedPixelsInterpolateAQuadraticExactly) {
    constexpr int W = 120;
    constexpr int H = 90;
    const auto f = [](double x, double y) { return 0.05 + 1e-4 * x * x + 0.5e-4 * y * y + 1e-3 * x; };
    Image img;
    img.width = W;
    img.height = H;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float v = static_cast<float>(f(x, y));
            img.pixels.insert(img.pixels.end(), {v, v, v});
        }
    }
    for (const float angle : {-45.0f, -8.0f, 0.1f, 21.0f, 45.0f}) {
        const Frame fr = frame_of(angle, 0, 0, 1, 1);
        const Image out = prism::apply_frame(img, fr, 1);
        const FrameMap map(fr, W, H);
        int checked = 0;
        double worst = 0.0;
        for (int j = 0; j < out.height; ++j) {
            for (int i = 0; i < out.width; ++i) {
                const FramePoint p = map.to_photo_pixels({i + 0.5, j + 0.5});
                const double sx = p.x - 0.5;  // photo pixel centres at whole numbers
                const double sy = p.y - 0.5;
                // Only where all 4 x 4 taps are on the photo (no edge repeats).
                if (sx < 1.0 || sy < 1.0 || sx >= W - 3.0 || sy >= H - 3.0) {
                    continue;
                }
                worst = std::max(worst, std::abs(pixel(out, i, j)[0] - f(sx, sy)));
                ++checked;
            }
        }
        EXPECT_LT(worst, 3e-6) << "angle " << angle;
        EXPECT_GT(checked, 2000) << "angle " << angle;
    }
}

// A blown highlight next to deep shadow: the hardest case for a sharp filter, which rings on both
// sides of the step. Inside the highlight every pixel must stay at the file's ceiling (Decode's
// "clipped in the file" test, gui/clipping.hpp, uses 1 - 1e-6), and nothing may go below the
// photo's lowest value (0 here) or above its highest.
TEST(Frame, ClippedPlateauStaysClippedAndNothingGoesNegative) {
    constexpr int W = 200;
    constexpr int H = 150;
    constexpr float ceiling = 1.0f - 1e-6f;
    Image img;
    img.width = W;
    img.height = H;
    img.pixels.resize(static_cast<std::size_t>(W) * H * 3);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const bool plateau = (x - 100) * (x - 100) + (y - 75) * (y - 75) < 50 * 50;
            for (int c = 0; c < 3; ++c) {
                // Shadow is 0 in red, a little above in green and blue (with a checkerboard so the
                // shadow side rings too); the plateau is exactly 1 in red and green, 4 in blue.
                const float shadow = c == 0 ? 0.0f : ((x + y) % 2 == 0 ? 0.0f : 0.05f);
                const float high = c == 2 ? 4.0f : 1.0f;
                img.pixels[(static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)) * 3 +
                           static_cast<std::size_t>(c)] = plateau ? high : shadow;
            }
        }
    }
    for (const float angle : {-45.0f, -12.5f, 0.1f, 3.0f, 45.0f}) {
        const Frame f = frame_of(angle, 0, 0, 1, 1);
        const Image out = prism::apply_frame(img, f, 1);
        const FrameMap map(f, W, H);
        int inside = 0;
        for (int j = 0; j < out.height; ++j) {
            for (int i = 0; i < out.width; ++i) {
                const float* o = pixel(out, i, j);
                for (int c = 0; c < 3; ++c) {
                    ASSERT_GE(o[c], 0.0f) << "angle " << angle << " at " << i << ", " << j;
                    ASSERT_LE(o[c], c == 2 ? 4.0f : 1.0f);
                }
                // The 2 x 2 photo pixels nearest this pixel's position.
                const FramePoint p = map.to_photo_pixels({i + 0.5, j + 0.5});
                const int x = static_cast<int>(std::floor(p.x - 0.5));
                const int y = static_cast<int>(std::floor(p.y - 0.5));
                bool all_clipped = true;
                for (const int yy : {y, y + 1}) {
                    for (const int xx : {x, x + 1}) {
                        const int cx = std::clamp(xx, 0, W - 1);
                        const int cy = std::clamp(yy, 0, H - 1);
                        all_clipped = all_clipped && pixel(img, cx, cy)[0] >= ceiling;
                    }
                }
                if (all_clipped) {
                    ++inside;
                    for (int c = 0; c < 3; ++c) {
                        ASSERT_GE(o[c], ceiling) << "angle " << angle << " at " << i << ", " << j;
                    }
                }
            }
        }
        EXPECT_GT(inside, 7000);  // the disc is about 7850 pixels
    }
}

// --- Direction -----------------------------------------------------------------------------------

// A bright pixel 50 px right of the centre: a positive angle turns the picture clockwise on
// screen (y down), so it moves down; a negative one moves it up.
TEST(Frame, PositiveAngleTurnsClockwise) {
    constexpr int W = 201;
    constexpr int H = 101;
    Image img;
    img.width = W;
    img.height = H;
    img.pixels.assign(static_cast<std::size_t>(W) * H * 3, 0.0f);
    for (int c = 0; c < 3; ++c) {
        img.pixels[(50 * static_cast<std::size_t>(W) + 150) * 3 + static_cast<std::size_t>(c)] = 1.0f;
    }
    for (const float angle : {10.0f, -10.0f}) {
        const Image out = prism::apply_frame(img, frame_of(angle, 0, 0, 1, 1), 1);
        int best_x = 0;
        int best_y = 0;
        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                if (pixel(out, x, y)[0] > pixel(out, best_x, best_y)[0]) {
                    best_x = x;
                    best_y = y;
                }
            }
        }
        // The centre is at (100.5, 50.5); the pixel's at (150.5, 50.5), 50 px right. Turned by
        // 10 degrees it lands at about (149.7, 50.5 +- 8.7).
        EXPECT_NEAR(best_x, 149, 1) << "angle " << angle;
        if (angle > 0) {
            EXPECT_NEAR(best_y, 59, 1) << "clockwise: right of centre moves down";
        } else {
            EXPECT_NEAR(best_y, 41, 1) << "anticlockwise: right of centre moves up";
        }
        // The map says the same.
        const PhotoPoint q =
            prism::frame_to_photo(frame_of(angle, 0, 0, 1, 1), W, H, {best_x + 0.5, best_y + 0.5});
        EXPECT_NEAR(q.u * W, 150.5, 1.0);
        EXPECT_NEAR(q.v * H, 50.5, 1.0);
    }
}

// --- fit_frame -----------------------------------------------------------------------------------

// The photo position of each corner of `f`'s rectangle (on a W x H photo).
std::vector<FramePoint> corners_on_photo(const Frame& f, int w, int h) {
    const double a = static_cast<double>(f.angle) * (std::acos(-1.0) / 180.0);
    const double c = std::cos(a);
    const double s = std::sin(a);
    std::vector<FramePoint> out;
    for (const float x : {f.left, f.right}) {
        for (const float y : {f.top, f.bottom}) {
            const double dx = (static_cast<double>(x) - 0.5) * w;
            const double dy = (static_cast<double>(y) - 0.5) * h;
            out.push_back({w / 2.0 + c * dx + s * dy, h / 2.0 - s * dx + c * dy});
        }
    }
    return out;
}

// The largest centred rectangle of `aspect` on a W x H photo (any shape for Free), as fractions,
// optionally shrunk to `scale` and moved by (du, dv).
Frame shaped(Aspect aspect, bool portrait, float angle, int w, int h, double scale, double du,
             double dv) {
    const std::optional<double> ratio = prism::aspect_ratio(aspect, portrait, w, h);
    // Width and height as fractions of W and H.
    double fw = 0.8;
    double fh = 0.6;
    if (ratio) {
        const double photo = static_cast<double>(w) / h;
        fw = *ratio >= photo ? 1.0 : *ratio / photo;
        fh = *ratio >= photo ? photo / *ratio : 1.0;
    }
    fw *= scale;
    fh *= scale;
    const double cu = std::clamp(0.5 + du, fw / 2, 1 - fw / 2);
    const double cv = std::clamp(0.5 + dv, fh / 2, 1 - fh / 2);
    Frame f = frame_of(angle, static_cast<float>(cu - fw / 2), static_cast<float>(cv - fh / 2),
                       static_cast<float>(cu + fw / 2), static_cast<float>(cv + fh / 2));
    f.aspect = aspect;
    f.portrait = portrait;
    return f;
}

// Every aspect, both ways round, from -45 to 45 degrees, on odd and tiny sizes, for a full-size
// frame, a smaller off-centre one and one whose centre lies outside the turned photo: the fitted
// frame is valid, its corners are on the photo, its shape is kept and its centre is on the photo.
//
// Tolerance: fit_frame works in double and puts the binding corner exactly on the edge; storing
// each fraction as a float moves it by at most half a float step, 2^-25 of the side, so a corner
// moves by at most about 2 * 2^-25 * max(W, H) = 6e-8 * max(W, H) px. 1e-6 * max(W, H) px (0.01
// px on a 9504 px photo) is well above that and far below a real miss, which is pixels.
TEST(Frame, FitFramePutsEveryCornerOnTheTurnedPhoto) {
    // Very wide and very tall photos too: turned, much of their straightened rectangle is off the
    // photo, so pulling a centre in has the furthest to go.
    const std::vector<std::pair<int, int>> sizes = {{1, 1},       {7, 3},      {3, 7},
                                                    {5, 5},       {1501, 701}, {701, 1501},
                                                    {9504, 6336}, {2561, 1707}, {1000, 37},
                                                    {37, 1000}};
    std::vector<float> angles = {-0.1f, 0.1f};
    for (int a = -45 * 4; a <= 45 * 4; a += 5) {
        angles.push_back(static_cast<float>(a) / 4.0f);  // -45 to 45 in steps of 1.25
    }
    int checked = 0;
    for (const auto& [w, h] : sizes) {
        const double tol = 1e-6 * std::max(w, h);
        for (const Aspect aspect : prism::all_aspects) {
            for (const bool portrait : {false, true}) {
                for (const float angle : angles) {
                    for (const auto& [scale, du, dv] :
                         {std::tuple{1.0, 0.0, 0.0}, std::tuple{0.4, 0.21, -0.17},
                          std::tuple{0.1, 0.5, -0.5}, std::tuple{0.1, 0.5, 0.5}}) {
                        const Frame before = shaped(aspect, portrait, angle, w, h, scale, du, dv);
                        ASSERT_TRUE(prism::validate_frame(before).empty());
                        const Frame after = prism::fit_frame(before, w, h);
                        SCOPED_TRACE(::testing::Message()
                                     << w << "x" << h << " aspect " << static_cast<int>(aspect)
                                     << (portrait ? " portrait" : "") << " angle " << angle
                                     << " scale " << scale);
                        ASSERT_TRUE(prism::validate_frame(after).empty());
                        EXPECT_EQ(after.angle, before.angle);
                        EXPECT_EQ(after.aspect, before.aspect);
                        EXPECT_EQ(after.portrait, before.portrait);
                        for (const FramePoint& p : corners_on_photo(after, w, h)) {
                            ASSERT_GE(p.x, -tol);
                            ASSERT_LE(p.x, w + tol);
                            ASSERT_GE(p.y, -tol);
                            ASSERT_LE(p.y, h + tol);
                        }
                        // The shape, in pixels.
                        const double shape_before = (double(before.right) - before.left) * w /
                                                    ((double(before.bottom) - before.top) * h);
                        const double shape_after = (double(after.right) - after.left) * w /
                                                   ((double(after.bottom) - after.top) * h);
                        // Each edge is a float, within half a float step (2^-25) of where
                        // fit_frame put it in double, so a side's length is off by at most
                        // 2^-24 and the shape by at most the sum of the sides' relative errors,
                        // before and after: doubled for margin.
                        const double step = std::ldexp(1.0, -24);
                        const double shape_tol =
                            2 * step *
                            (1 / (double(before.right) - before.left) +
                             1 / (double(before.bottom) - before.top) +
                             1 / (double(after.right) - after.left) +
                             1 / (double(after.bottom) - after.top));
                        EXPECT_NEAR(shape_after / shape_before, 1.0, shape_tol);
                        // Never grows.
                        EXPECT_LE(after.right - after.left, before.right - before.left + 1e-6f);
                        // The centre, on the photo.
                        const double a = static_cast<double>(angle) * (std::acos(-1.0) / 180.0);
                        const double dx = ((double(after.left) + after.right) / 2 - 0.5) * w;
                        const double dy = ((double(after.top) + after.bottom) / 2 - 0.5) * h;
                        const double px = w / 2.0 + std::cos(a) * dx + std::sin(a) * dy;
                        const double py = h / 2.0 - std::sin(a) * dx + std::cos(a) * dy;
                        EXPECT_GE(px, -tol);
                        EXPECT_LE(px, w + tol);
                        EXPECT_GE(py, -tol);
                        EXPECT_LE(py, h + tol);
                        ++checked;
                    }
                }
            }
        }
    }
    EXPECT_EQ(checked, 10 * 7 * 2 * 75 * 4);
}

TEST(Frame, FitFrameLeavesAFittingFrameAloneAndPullsTheCentreIn) {
    // At 0 degrees every valid frame fits.
    for (const Frame& f : {Frame{}, frame_of(0, 0.1f, 0.2f, 0.3f, 0.9f), frame_of(0, 0.99f, 0.0f, 1.0f, 0.01f)}) {
        EXPECT_EQ(prism::fit_frame(f, 6000, 4000), f);
    }
    // A small frame near the middle, slightly turned: fits as it is.
    const Frame small = frame_of(5.0f, 0.4f, 0.4f, 0.6f, 0.6f);
    EXPECT_EQ(prism::fit_frame(small, 6000, 4000), small);
    // The top-right corner of the straightened square at 45 degrees is outside the turned photo:
    // the centre comes in, and the frame shrinks about it to something that fits.
    const Frame corner = frame_of(45.0f, 0.9f, 0.0f, 1.0f, 0.1f);
    const Frame fitted = prism::fit_frame(corner, 1000, 1000);
    EXPECT_TRUE(prism::validate_frame(fitted).empty());
    for (const FramePoint& p : corners_on_photo(fitted, 1000, 1000)) {
        EXPECT_GE(p.x, -1e-3);
        EXPECT_LE(p.x, 1000 + 1e-3);
        EXPECT_GE(p.y, -1e-3);
        EXPECT_LE(p.y, 1000 + 1e-3);
    }
    EXPECT_GT(fitted.right - fitted.left, 0.0f);
}

TEST(Frame, FitFrameKeepsASideTooThinForAFloatValid) {
    // One float step tall, as wide as the photo, on a photo 60 times wider than tall and turned:
    // the shrink leaves the height far below what a float can hold apart. It stays one float step
    // instead of becoming invalid, wherever the frame is, the photo's edges included; and the same
    // for one float step wide on a tall photo.
    int collapsed = 0;
    for (const float at : {0.0f, 0.25f, 0.5f, 0.75f, std::nextafter(1.0f, 0.0f)}) {
        for (const float angle : {-45.0f, -10.0f, 10.0f, 45.0f}) {
            const float next = std::nextafter(at, 2.0f);
            for (const bool tall : {false, true}) {
                const Frame thin = tall ? frame_of(angle, at, 0.0f, next, 1.0f)
                                        : frame_of(angle, 0.0f, at, 1.0f, next);
                const int w = tall ? 100 : 6000;
                const int h = tall ? 6000 : 100;
                const Frame fitted = prism::fit_frame(thin, w, h);
                SCOPED_TRACE(::testing::Message() << "at " << at << " angle " << angle
                                                  << (tall ? " tall" : " wide"));
                EXPECT_TRUE(prism::validate_frame(fitted).empty())
                    << fitted.left << " " << fitted.top << " " << fitted.right << " "
                    << fitted.bottom;
                // It did shrink along its length.
                EXPECT_LT(tall ? fitted.bottom - fitted.top : fitted.right - fitted.left, 0.5f);
                const float side = tall ? fitted.right - fitted.left : fitted.bottom - fitted.top;
                collapsed += side <= next - at;
            }
        }
    }
    EXPECT_GT(collapsed, 0);
}

// --- Map -----------------------------------------------------------------------------------------

TEST(Frame, MapAndInverseRoundTrip) {
    for (const float angle : {-45.0f, -12.3f, -0.1f, 0.0f, 0.1f, 7.7f, 45.0f}) {
        const Frame f = frame_of(angle, 0.13f, 0.07f, 0.91f, 0.88f);
        for (const auto& [w, h] : {std::pair{1501, 701}, std::pair{9504, 6336}, std::pair{1, 1}}) {
            const FrameMap map(f, w, h);
            for (const double x : {0.5, 3.25, 100.5, 700.75}) {
                for (const double y : {0.5, 17.5, 333.125}) {
                    const PhotoPoint q = map.to_photo({x, y});
                    const FramePoint back = map.to_frame(q);
                    EXPECT_NEAR(back.x, x, 1e-9) << angle;
                    EXPECT_NEAR(back.y, y, 1e-9) << angle;
                    // The one-off forms agree with the map exactly.
                    const PhotoPoint q2 = prism::frame_to_photo(f, w, h, {x, y});
                    EXPECT_EQ(q2.u, q.u);
                    EXPECT_EQ(q2.v, q.v);
                }
            }
            for (const double u : {0.0, 0.25, 0.5, 0.875, 1.0}) {
                for (const double v : {0.0, 0.3, 1.0}) {
                    const PhotoPoint q = map.to_photo(prism::photo_to_frame(f, w, h, {u, v}));
                    EXPECT_NEAR(q.u, u, 1e-12);
                    EXPECT_NEAR(q.v, v, 1e-12);
                }
            }
        }
    }
    // At 0 degrees a framed pixel's centre is the photo pixel's centre, exactly.
    const Frame crop = frame_of(0, 0.1f, 0.2f, 0.7f, 0.9f);
    const FrameMap map(crop, 37, 23);
    const PhotoPoint q = map.to_photo({2.5, 3.5});
    EXPECT_EQ(q.u, (map.box().x0 + 2.5) / 37.0);
    EXPECT_EQ(q.v, (map.box().y0 + 3.5) / 23.0);
}

// --- Validation ----------------------------------------------------------------------------------

bool mentions(const std::vector<std::string>& problems, const std::string& text) {
    return std::any_of(problems.begin(), problems.end(),
                       [&](const std::string& p) { return p.find(text) != std::string::npos; });
}

TEST(Frame, ValidateFrameRejectsBadValues) {
    EXPECT_TRUE(prism::validate_frame(Frame{}).empty());
    EXPECT_TRUE(prism::validate_frame(frame_of(-45, 0, 0, 1, 1)).empty());
    EXPECT_TRUE(prism::validate_frame(frame_of(45, 0.5f, 0.5f, 0.5000001f, 0.6f)).empty());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const auto problems = [](const Frame& f) { return prism::validate_frame(f); };

    EXPECT_TRUE(mentions(problems(frame_of(nan, 0, 0, 1, 1)), "Frame angle must be from -45 to 45"));
    EXPECT_TRUE(mentions(problems(frame_of(inf, 0, 0, 1, 1)), "Frame angle"));
    EXPECT_TRUE(mentions(problems(frame_of(45.0001f, 0, 0, 1, 1)), "(it is 45.0001)"));
    EXPECT_TRUE(mentions(problems(frame_of(-90, 0, 0, 1, 1)), "Frame angle"));
    EXPECT_TRUE(mentions(problems(frame_of(0, nan, 0, 1, 1)), "Frame left must be from 0 to 1"));
    EXPECT_TRUE(mentions(problems(frame_of(0, 0, -inf, 1, 1)), "Frame top must be from 0 to 1"));
    EXPECT_TRUE(mentions(problems(frame_of(0, 0, 0, inf, 1)), "Frame right must be from 0 to 1"));
    EXPECT_TRUE(mentions(problems(frame_of(0, 0, 0, 1, nan)), "Frame bottom must be from 0 to 1"));
    EXPECT_TRUE(mentions(problems(frame_of(0, -0.1f, 0, 1, 1)), "Frame left must be from 0 to 1"));
    EXPECT_TRUE(mentions(problems(frame_of(0, 0, 0, 1.1f, 1)), "Frame right must be from 0 to 1"));
    EXPECT_TRUE(mentions(problems(frame_of(0, 0.6f, 0, 0.4f, 1)),
                         "Frame left must be less than right (left is 0.6, right is 0.4)"));
    EXPECT_TRUE(mentions(problems(frame_of(0, 0.5f, 0, 0.5f, 1)), "Frame left must be less than right"));
    EXPECT_TRUE(mentions(problems(frame_of(0, 0, 0.9f, 1, 0.2f)), "Frame top must be less than bottom"));
    Frame odd;
    odd.aspect = static_cast<Aspect>(99);
    EXPECT_TRUE(mentions(problems(odd), "Frame aspect"));
    // Every problem is listed, not just the first.
    EXPECT_EQ(problems(frame_of(nan, 2, 0.5f, 0.4f, 0.2f)).size(), 3u);

    // Everything that takes a frame refuses a bad one, and an empty image.
    const Frame bad = frame_of(0, 0.6f, 0, 0.4f, 1);
    const Image img = patterned(4, 4);
    EXPECT_THROW((void)prism::fit_frame(bad, 4, 4), std::invalid_argument);
    EXPECT_THROW((void)prism::framed_box(bad, 4, 4), std::invalid_argument);
    EXPECT_THROW((void)prism::framed_size(bad, 4, 4), std::invalid_argument);
    EXPECT_THROW((void)FrameMap(bad, 4, 4), std::invalid_argument);
    EXPECT_THROW((void)prism::apply_frame(img, bad, 1), std::invalid_argument);
    EXPECT_THROW((void)prism::framed_box(Frame{}, 0, 4), std::invalid_argument);
    EXPECT_THROW((void)prism::fit_frame(Frame{}, 4, 0), std::invalid_argument);
    EXPECT_THROW((void)prism::apply_frame(Image{}, Frame{}, 1), std::invalid_argument);
    Image broken = img;
    broken.pixels.pop_back();
    EXPECT_THROW((void)prism::apply_frame(broken, Frame{}, 1), std::invalid_argument);
}

// --- Aspect --------------------------------------------------------------------------------------

TEST(Frame, AspectRatioFollowsThePhotoAndTurns) {
    using prism::aspect_ratio;
    // Landscape photo.
    EXPECT_EQ(aspect_ratio(Aspect::original, false, 6000, 4000), 1.5);
    EXPECT_EQ(aspect_ratio(Aspect::original, true, 6000, 4000), 4000.0 / 6000.0);
    EXPECT_EQ(aspect_ratio(Aspect::four_five, false, 6000, 4000), 1.25);
    EXPECT_EQ(aspect_ratio(Aspect::four_five, true, 6000, 4000), 0.8);
    EXPECT_EQ(aspect_ratio(Aspect::five_seven, false, 6000, 4000), 1.4);
    EXPECT_EQ(aspect_ratio(Aspect::three_two, false, 6000, 4000), 1.5);
    EXPECT_EQ(aspect_ratio(Aspect::sixteen_nine, true, 6000, 4000), 9.0 / 16.0);
    EXPECT_EQ(aspect_ratio(Aspect::square, true, 6000, 4000), 1.0);
    EXPECT_FALSE(aspect_ratio(Aspect::free, false, 6000, 4000).has_value());
    // Portrait photo: the same shapes stand up, and portrait lays them down.
    EXPECT_EQ(aspect_ratio(Aspect::original, false, 4000, 6000), 4000.0 / 6000.0);
    EXPECT_EQ(aspect_ratio(Aspect::four_five, false, 4000, 6000), 0.8);
    EXPECT_EQ(aspect_ratio(Aspect::four_five, true, 4000, 6000), 1.25);
    // A square photo counts as landscape.
    EXPECT_EQ(aspect_ratio(Aspect::three_two, false, 500, 500), 1.5);
    EXPECT_THROW((void)aspect_ratio(Aspect::square, false, 0, 1), std::invalid_argument);
    EXPECT_THROW((void)aspect_ratio(static_cast<Aspect>(99), false, 1, 1), std::invalid_argument);
}

// --- Whole photo, aspect and the export order ----------------------------------------------------

TEST(Frame, IsWholePhotoNeedsZeroAngleAndAllFourEdges) {
    using prism::is_whole_photo;
    EXPECT_TRUE(is_whole_photo(Frame{}));
    // The aspect lock moves no pixel.
    Frame locked;
    locked.aspect = Aspect::three_two;
    locked.portrait = true;
    EXPECT_TRUE(is_whole_photo(locked));
    EXPECT_FALSE(is_whole_photo(frame_of(0.1f, 0, 0, 1, 1)));
    EXPECT_FALSE(is_whole_photo(frame_of(0, 0.01f, 0, 1, 1)));
    EXPECT_FALSE(is_whole_photo(frame_of(0, 0, 0.01f, 1, 1)));
    EXPECT_FALSE(is_whole_photo(frame_of(0, 0, 0, 0.99f, 1)));
    EXPECT_FALSE(is_whole_photo(frame_of(0, 0, 0, 1, 0.99f)));
}

Frame with_aspect(Frame f, Aspect a, bool portrait = false) {
    f.aspect = a;
    f.portrait = portrait;
    return f;
}

TEST(Frame, ApplyAspectGivesTheLargestCentredRectangleOfTheShape) {
    using prism::apply_aspect;
    // The whole of a 100 x 80 photo, 4:5 turned to stand up: 64 x 80, centred.
    Frame f = apply_aspect(with_aspect(Frame{}, Aspect::four_five, true), 100, 80);
    EXPECT_EQ(prism::framed_box(f, 100, 80), (FrameBox{18, 0, 64, 80}));
    // Not turned it is 5 wide by 4 tall: 100 x 80 already, so the frame stays as it was.
    f = apply_aspect(with_aspect(Frame{}, Aspect::four_five), 100, 80);
    EXPECT_EQ(f, with_aspect(Frame{}, Aspect::four_five));
    // A wide shape on a tall photo (turned against it) loses height: 16:9 on 80 x 100 is 80 x 45.
    f = apply_aspect(with_aspect(Frame{}, Aspect::sixteen_nine, true), 80, 100);
    EXPECT_EQ(prism::framed_size(f, 80, 100), (prism::PixelSize{80, 45}));
    // A crop of 100 x 50 pixels, wider than 3:2: the width shrinks to 75, about the crop's centre.
    f = apply_aspect(with_aspect(frame_of(0, 0.25f, 0.25f, 0.75f, 0.875f), Aspect::three_two), 200,
                     80);
    EXPECT_EQ(prism::framed_box(f, 200, 80), (FrameBox{63, 20, 75, 50}));
    // Free keeps the rectangle as it is.
    const Frame free_frame = with_aspect(frame_of(3, 0.1f, 0.2f, 0.7f, 0.9f), Aspect::free);
    EXPECT_EQ(apply_aspect(free_frame, 100, 80), free_frame);
}

TEST(Frame, ApplyAspectKeepsACorrectShapeNeverGrowsAndKeepsTheRest) {
    using prism::apply_aspect;
    // Original on the whole photo is the photo's own shape: nothing moves, not even by a rounding
    // error (a frame off 0 / 1 by 1e-17 would no longer be the whole photo).
    for (const auto& [w, h] : {std::pair{6000, 4000}, {4000, 6000}, {1501, 701}, {977, 977},
                          {100, 104}, {104, 100}, {100, 106}, {117, 103}}) {
        const Frame whole = with_aspect(Frame{}, Aspect::original);
        EXPECT_EQ(apply_aspect(whole, w, h), whole) << w << "x" << h;
        EXPECT_TRUE(prism::is_whole_photo(apply_aspect(whole, w, h))) << w << "x" << h;
    }
    // Every shape, both ways, on a few photos and crops: inside the crop, same centre, right shape.
    const Frame crops[] = {frame_of(0, 0, 0, 1, 1), frame_of(12.5f, 0.1f, 0.3f, 0.9f, 0.6f),
                           frame_of(-30, 0.4f, 0.0f, 0.6f, 1.0f)};
    for (const auto& [w, h] : {std::pair{6000, 4000}, {4000, 6000}, {500, 500}}) {
        for (const Frame& crop : crops) {
            for (const Aspect a : prism::all_aspects) {
                if (a == Aspect::free) {
                    continue;
                }
                for (const bool portrait : {false, true}) {
                    const Frame in = with_aspect(crop, a, portrait);
                    const Frame out = apply_aspect(in, w, h);
                    EXPECT_GE(out.left, in.left);
                    EXPECT_GE(out.top, in.top);
                    EXPECT_LE(out.right, in.right);
                    EXPECT_LE(out.bottom, in.bottom);
                    EXPECT_NEAR(out.left + out.right, in.left + in.right, 2e-6);
                    EXPECT_NEAR(out.top + out.bottom, in.top + in.bottom, 2e-6);
                    const double shape = (static_cast<double>(out.right) - out.left) * w /
                                         ((static_cast<double>(out.bottom) - out.top) * h);
                    EXPECT_NEAR(shape, *prism::aspect_ratio(a, portrait, w, h), 1e-4);
                    EXPECT_EQ(out.angle, in.angle);
                    EXPECT_EQ(out.aspect, in.aspect);
                    EXPECT_EQ(out.portrait, in.portrait);
                }
            }
        }
    }
    Frame bad;
    bad.left = 2.0f;
    EXPECT_THROW((void)apply_aspect(bad, 10, 10), std::invalid_argument);
    EXPECT_THROW((void)apply_aspect(Frame{}, 0, 10), std::invalid_argument);
}

TEST(Frame, PlanExportFrameTakesTodaysPathForNoFrameAndForTheWholePhoto) {
    using prism::plan_export_frame;
    for (const std::optional<Frame>& none :
         {std::optional<Frame>{}, std::optional<Frame>{Frame{}},
          std::optional<Frame>{with_aspect(Frame{}, Aspect::sixteen_nine, true)}}) {
        const prism::ExportFrame plan = plan_export_frame(none, 120, 80);
        EXPECT_FALSE(plan.fitted.has_value());
        EXPECT_EQ(plan.size, (prism::PixelSize{120, 80}));
        EXPECT_FALSE(plan.changed);
    }
}

TEST(Frame, PlanExportFrameFitsAndSaysWhenFittingChangedTheFrame) {
    using prism::plan_export_frame;
    // Fits: kept, nothing to say.
    const Frame fits = frame_of(2.0f, 0.2f, 0.2f, 0.8f, 0.8f);
    prism::ExportFrame plan = plan_export_frame(fits, 120, 80);
    ASSERT_TRUE(plan.fitted.has_value());
    EXPECT_EQ(*plan.fitted, fits);
    EXPECT_FALSE(plan.changed);
    EXPECT_EQ(plan.size, prism::framed_size(fits, 120, 80));
    // Turned whole photo: the corners show, so it shrinks.
    const Frame turned = frame_of(10.0f, 0, 0, 1, 1);
    plan = plan_export_frame(turned, 120, 80);
    ASSERT_TRUE(plan.fitted.has_value());
    EXPECT_EQ(*plan.fitted, prism::fit_frame(turned, 120, 80));
    EXPECT_TRUE(plan.changed);
    EXPECT_LT(plan.size.width, 120);
    EXPECT_LT(plan.size.height, 80);
    EXPECT_EQ(plan.size, prism::framed_size(*plan.fitted, 120, 80));
    EXPECT_THROW((void)plan_export_frame(frame_of(0, 0.5f, 0, 0.4f, 1), 10, 10),
                 std::invalid_argument);
    EXPECT_THROW((void)plan_export_frame(std::nullopt, 0, 10), std::invalid_argument);
}

TEST(Frame, ExportMeasuresTheFramedSizeNotThePhotos) {
    // A percentage or a long side is worked out from the plan's size, the framed one.
    const Frame crop = frame_of(0, 0.25f, 0.0f, 0.75f, 1.0f);  // 100 x 80 of 200 x 80
    const prism::ExportFrame plan = prism::plan_export_frame(crop, 200, 80);
    ASSERT_EQ(plan.size, (prism::PixelSize{100, 80}));
    const int percent_side = prism::long_side_for_percent(plan.size.width, plan.size.height, 50);
    EXPECT_EQ(percent_side, 50);  // 50% of the framed 100 x 80, not of the photo's 200 x 80
    const Image reduced = prism::frame_and_reduce(patterned(200, 80), crop, percent_side).image;
    EXPECT_EQ(reduced.width, 50);
    EXPECT_EQ(reduced.height, 40);
    // Original: no reduction, the framed size.
    const Image original = prism::frame_and_reduce(patterned(200, 80), crop, std::nullopt).image;
    EXPECT_EQ(original.width, 100);
    EXPECT_EQ(original.height, 80);
}

TEST(Frame, FrameAndReduceWithoutAFramePathIsExactlyTodaysDownscale) {
    const Image img = patterned(150, 90);
    const Frame whole_but_locked = with_aspect(Frame{}, Aspect::square);
    for (const std::optional<Frame>& frame :
         {std::optional<Frame>{}, std::optional<Frame>{Frame{}},
          std::optional<Frame>{whole_but_locked}}) {
        // Original, a long side, and ones that do not reduce.
        for (const std::optional<int> side : {std::optional<int>{}, std::optional<int>{40},
                                              std::optional<int>{150}, std::optional<int>{400}}) {
            const prism::FramedExport out = prism::frame_and_reduce(img, frame, side, 1);
            const Image today = side ? prism::downscale(img, *side, 1) : img;
            ASSERT_EQ(out.image.width, today.width);
            ASSERT_EQ(out.image.height, today.height);
            EXPECT_EQ(first_bit_difference(out.image, today), -1);
            EXPECT_FALSE(out.frame.fitted.has_value());
            EXPECT_EQ(out.frame_ms, 0.0);
        }
    }
    // A whole photo at a reduced size takes no apply_frame: it reports no fitted frame, which is
    // what tells the caller to keep the whole-photo grid.
    const prism::FramedExport reduced = prism::frame_and_reduce(img, Frame{}, 40, 1);
    EXPECT_TRUE(reduced.reduced);
    EXPECT_FALSE(reduced.frame.fitted.has_value());
}

TEST(Frame, FrameAndReduceFramesFirstThenReduces) {
    const Image img = patterned(240, 160);
    const Frame f = frame_of(6.0f, 0.1f, 0.05f, 0.9f, 0.95f);
    const Frame fitted = prism::fit_frame(f, 240, 160);
    const prism::FramedExport out = prism::frame_and_reduce(img, f, 50, 1);
    ASSERT_TRUE(out.frame.fitted.has_value());
    EXPECT_EQ(*out.frame.fitted, fitted);
    EXPECT_TRUE(out.reduced);
    // The long side is exactly what was asked, of the framed picture.
    EXPECT_EQ(std::max(out.image.width, out.image.height), 50);
    const Image expected = prism::downscale(prism::apply_frame(img, fitted, 1), 50, 1);
    ASSERT_EQ(out.image.width, expected.width);
    ASSERT_EQ(out.image.height, expected.height);
    EXPECT_EQ(first_bit_difference(out.image, expected), -1);
    // Reducing first and framing after is another picture altogether.
    const Image other_way = prism::apply_frame(prism::downscale(img, 80, 1), fitted, 1);
    EXPECT_NE(first_bit_difference(out.image, prism::downscale(other_way, 50, 1)), -1);
    // A frame not larger than long_side keeps its framed size.
    const prism::FramedExport kept = prism::frame_and_reduce(img, f, 10000, 1);
    EXPECT_FALSE(kept.reduced);
    EXPECT_EQ(kept.image.width, prism::framed_size(fitted, 240, 160).width);
    EXPECT_EQ(first_bit_difference(kept.image, prism::apply_frame(img, fitted, 1)), -1);
    EXPECT_THROW((void)prism::frame_and_reduce(img, f, 0, 1), std::invalid_argument);
}

TEST(Frame, FrameAndReduceOneAndEightThreadsAreBitIdentical) {
    constexpr int W = 2400;
    constexpr int H = 1600;
    const Image img = patterned(W, H);
    const Frame f = frame_of(-4.0f, 0.02f, 0.03f, 0.97f, 0.98f);
    for (const std::optional<int> side : {std::optional<int>{}, std::optional<int>{1600}}) {
        const prism::FramedExport one = prism::frame_and_reduce(img, f, side, 1);
        const prism::FramedExport eight = prism::frame_and_reduce(img, f, side, 8);
        ASSERT_TRUE(one.frame.fitted.has_value());
        ASSERT_EQ(one.image.width, eight.image.width);
        ASSERT_EQ(one.image.height, eight.image.height);
        const std::size_t out_pixels =
            static_cast<std::size_t>(one.image.width) * static_cast<std::size_t>(one.image.height);
        ASSERT_GE(out_pixels, prism::parallel_min_pixels * 3 / 2);
        ASSERT_EQ(prism::threads_for_pixels(out_pixels, 8), 8);
        ASSERT_EQ(eight.frame_threads, 8);
        if (side) {
            ASSERT_TRUE(one.reduced);
            ASSERT_TRUE(eight.reduced);
            ASSERT_EQ(eight.reduce_threads, 8);
        }
        EXPECT_EQ(first_bit_difference(one.image, eight.image), -1);
    }
}

}  // namespace

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "prism/image.hpp"
#include "prism/resize.hpp"

namespace prism {

// Framing and straightening: which part of the photo is printed, and how far it is turned. In
// darkroom terms this is the easel under the enlarger: its blades decide which light reaches the
// paper and its angle levels the horizon. It moves no light, so it is not a pipeline stage; it runs
// straight after decoding, and every stage then only sees the framed picture.
//
// The photo is the oriented (rotated as displayed), uncropped image, W x H pixels: the same image
// a stroke's (u, v) refers to (prism/stroke_field.hpp). A Frame is resolution-free, so the same
// Frame frames the 2560 px preview, a RAW half-size decode and the full decode alike.
//
// Coordinates
//
// Pixel positions are continuous, with the photo running from (0, 0) at its top-left corner to
// (W, H) at its bottom-right, so pixel (x, y) covers [x, x + 1) x [y, y + 1) and its centre is
// (x + 0.5, y + 0.5). A photo point (u, v) is a position divided by (W, H), as for strokes.
//
// Turning the photo by `angle` degrees about its centre (W / 2, H / 2) gives the "straightened"
// axes, and the frame's rectangle is measured along those: its edges are fractions of W (left,
// right) and of H (top, bottom), with the photo's centre at (0.5, 0.5) whatever the angle. So at 0
// degrees the rectangle is simply the crop. Positive angles turn the picture clockwise on screen
// (y points down): a straightened position p comes from the photo position
//   photo = C + R * (p - C),   R = [ cos a   sin a ]   C = (W / 2, H / 2), a = angle in radians
//                                  [-sin a   cos a ]
// so a feature right of the centre moves down as the angle grows. Each function that maps
// positions works out sin a and cos a once per call (FrameMap: once per map), in double.
//
// The rectangle can reach past the turned photo's edges, where there is no picture; fit_frame
// pulls it back in. apply_frame does not fit: outside the photo it repeats the nearest edge pixel.
//
// Pixel size
//
// On a W x H decode the frame covers straightened pixels [x0, x1) x [y0, y1), with
//   x0 = round(left * W), x1 = round(right * W), y0 = round(top * H), y1 = round(bottom * H)
// (halves up), so the framed image is (x1 - x0) x (y1 - y0) pixels and each edge is within half a
// pixel of where the fractions put it, on any decode. Because the edges are rounded to the nearest
// pixel independently, a corner of the rounded pixel box [x0, x1] x [y0, y1] can reach up to
// ~0.7 px past the turned photo, while every pixel centre (i + 0.5, j + 0.5) (where apply_frame
// samples) stays inside. A frame narrower than half a pixel would
// round to nothing; it is then widened to one pixel: x1 = x0 + 1, or x0 = x1 - 1 at the right edge
// (the same for y). So a valid frame never has an empty framed image, on any image of at least
// 1 x 1 pixel.
//
// frame.cpp is compiled without floating-point contraction (no fused multiply-add) on every
// compiler, and each framed pixel is worked out on its own from its (x, y), so the framed image is
// bit-identical whichever thread or row range computed a pixel.

// The shape the frame keeps while its blades move. Each follows the photo's orientation (a ratio
// of 4:5 is 5 wide by 4 tall on a landscape photo) unless Frame::portrait turns it. A square photo
// counts as landscape.
enum class Aspect {
    original,      // the photo's own W:H
    free,          // any shape: no lock
    square,        // 1:1
    four_five,     // 4:5, an 8x10 inch sheet
    five_seven,    // 5:7, a 5x7 inch sheet
    three_two,     // 3:2, a 35 mm negative
    sixteen_nine,  // 16:9
};

// Every Aspect, in menu order.
inline constexpr std::array<Aspect, 7> all_aspects = {
    Aspect::original,  Aspect::free,      Aspect::square,      Aspect::four_five,
    Aspect::five_seven, Aspect::three_two, Aspect::sixteen_nine};

struct Frame {
    // Degrees, -45 to 45; positive turns the picture clockwise on screen.
    float angle = 0.0f;
    // The rectangle, along the straightened axes: fractions of the photo's width (left, right) and
    // height (top, bottom), 0 <= left < right <= 1 and 0 <= top < bottom <= 1.
    float left = 0.0f;
    float top = 0.0f;
    float right = 1.0f;
    float bottom = 1.0f;
    // The lock that moving the blades keeps. Only recorded here; the rectangle above is what
    // frames the photo.
    Aspect aspect = Aspect::original;
    // The aspect turned against the photo's orientation (4:5 tall on a landscape photo).
    bool portrait = false;

    friend bool operator==(const Frame&, const Frame&) = default;
};
// A default Frame is the whole photo, upright.

// What is wrong with `f`, one sentence each (no full stop), or nothing if it is valid: the angle
// must be finite and within frame_angle_range (prism/ranges.hpp), each edge finite and from 0 to 1,
// left below right and top below bottom, and the aspect one of the Aspect values. For example
// "Frame angle must be from -45 to 45 (it is 50)" or "Frame left must be less than right (left is
// 0.6, right is 0.4)".
[[nodiscard]] std::vector<std::string> validate_frame(const Frame& f);

// The width / height, in pixels, that `aspect` asks for on a `width` x `height` photo, turned if
// `portrait`; nothing for Aspect::free. Throws std::invalid_argument if width or height is below 1
// or the aspect is not one of the Aspect values.
[[nodiscard]] std::optional<double> aspect_ratio(Aspect aspect, bool portrait, int width,
                                                 int height);

// Whether `f` is the whole photo, upright: angle 0 and the edges at 0, 0, 1, 1 exactly. The aspect
// lock is not looked at, as it moves no pixel. Such a frame changes nothing, and the export path
// takes no part of the framing code for it (see frame_and_reduce).
[[nodiscard]] bool is_whole_photo(const Frame& f) noexcept;

// `f` with its rectangle made the largest one of f.aspect (turned if f.portrait) that fits inside
// it, with the same centre: the rectangle shrinks about its centre until its shape is the aspect's
// on a `width` x `height` photo. So a rectangle wider than the aspect loses width, a taller one
// loses height, and one of the right shape (within float rounding) is kept. Aspect::free keeps the
// rectangle as it is. The angle, aspect and portrait are kept; it never grows, so a rectangle
// inside the photo stays inside it. Shapes are measured in pixels of the photo, not in fractions.
// Fit the result with fit_frame to keep corners off the edge of a turned photo. Throws
// std::invalid_argument with validate_frame's messages if `f` is invalid, or if width or height is
// below 1.
[[nodiscard]] Frame apply_aspect(const Frame& f, int width, int height);

// `f` made to fit a `width` x `height` photo turned by f.angle: no corner of the rectangle past the
// photo's edge, so nothing outside the picture is ever printed.
//  1. If the rectangle's centre lies outside the turned photo, it moves straight towards the
//     photo's centre until it is half a pixel inside (all the way on a side of one pixel). Along
//     that line it stays inside the straightened [0, 1] x [0, 1], and so does the rectangle.
//  2. The rectangle then shrinks about its centre, keeping its shape (width / height), until all
//     four corners are inside the photo.
// It never grows, and a frame that already fits comes back unchanged. The angle, aspect and
// portrait are kept. Corners land on the photo's edge to within float rounding of the fractions
// (about 1e-7 of the photo's long side). The result is always valid: in the rare case where a
// shrunken side would round to nothing as a float, it is kept one float step wide.
// Throws std::invalid_argument with validate_frame's messages if `f` is invalid, or if width or
// height is below 1.
[[nodiscard]] Frame fit_frame(const Frame& f, int width, int height);

// Where the framed image sits along the straightened axes of a `width` x `height` photo, in pixels:
// columns [x0, x0 + width), rows [y0, y0 + height). See "Pixel size" above.
struct FrameBox {
    int x0 = 0;
    int y0 = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const FrameBox&, const FrameBox&) = default;
};

// The box `f` frames on a `width` x `height` photo; never empty. Throws std::invalid_argument
// with validate_frame's messages if `f` is invalid, or if width or height is below 1.
[[nodiscard]] FrameBox framed_box(const Frame& f, int width, int height);

// framed_box's size: the size apply_frame gives a `width` x `height` image. Throws as framed_box.
[[nodiscard]] PixelSize framed_size(const Frame& f, int width, int height);

// A point on the photo, in the (u, v) coordinates above, in double.
struct PhotoPoint {
    double u = 0.0;
    double v = 0.0;
};

// A position on the framed image in its own pixels, continuous: framed pixel (i, j) has its centre
// at (i + 0.5, j + 0.5).
struct FramePoint {
    double x = 0.0;
    double y = 0.0;
};

// The mapping between the framed image of a `width` x `height` photo and the photo, with sin and
// cos worked out once, when the map is made. Copyable and cheap to use per pixel.
class FrameMap {
public:
    // Throws as framed_box.
    FrameMap(const Frame& f, int width, int height);

    // The photo point under framed position p.
    [[nodiscard]] PhotoPoint to_photo(FramePoint p) const noexcept;
    // The framed position over photo point q: the inverse of to_photo.
    [[nodiscard]] FramePoint to_frame(PhotoPoint q) const noexcept;
    // As to_photo, in photo pixels rather than fractions: (u * W, v * H).
    [[nodiscard]] FramePoint to_photo_pixels(FramePoint p) const noexcept;

    [[nodiscard]] const FrameBox& box() const noexcept { return box_; }
    [[nodiscard]] double cos_angle() const noexcept { return cos_; }
    [[nodiscard]] double sin_angle() const noexcept { return sin_; }

private:
    double width_ = 0.0;
    double height_ = 0.0;
    double cos_ = 1.0;
    double sin_ = 0.0;
    FrameBox box_;
};

// One-off forms of FrameMap(f, width, height).to_photo(p) and .to_frame(q). Throw as framed_box.
[[nodiscard]] PhotoPoint frame_to_photo(const Frame& f, int width, int height, FramePoint p);
[[nodiscard]] FramePoint photo_to_frame(const Frame& f, int width, int height, PhotoPoint q);

// The framed picture: framed_size(f, img.width, img.height) pixels, framed pixel (i, j) showing
// the photo at FrameMap::to_photo((i + 0.5, j + 0.5)).
//
// At an angle of exactly 0 that is a photo pixel's centre, and the box is copied pixel for pixel:
// nothing is resampled. Otherwise each pixel is interpolated from the 4 x 4 photo pixels around its
// position with Catmull-Rom weights, on the linear values, and each channel is then clamped to the
// lowest and highest value of that channel among the 2 x 2 photo pixels nearest the position. The
// clamp means turning never overshoots: no value below the photo's lowest (so no new negatives),
// none above its highest, a flat area stays exactly flat, and inside an area at the file's ceiling
// every pixel stays at or above it. Pixels past the photo's edge (the frame is not fitted here)
// repeat the nearest edge pixel.
//
// With `part`, only that part of the framed picture is worked out: `part` is in framed-image
// pixels, columns [part.x0, part.x0 + part.width) and rows [part.y0, part.y0 + part.height), all
// inside the framed image (framed_size), and the result is part.width x part.height pixels,
// bit-identical to the same pixels of the whole framed picture. Each pixel is worked out from its
// place in the whole framed picture, by the same code, and reads the whole photo, so a part needs
// no margin around it.
//
// `threads` as for the pipeline stages, decided by the size of what is worked out (the part, or
// the framed image); the result is bit-identical for any value. Throws std::invalid_argument: with
// validate_frame's messages if `f` is invalid; and if `img` is empty, if its pixel buffer does not
// match width * height * 3, or if `part` is empty or not inside the framed image.
[[nodiscard]] Image apply_frame(const Image& img, const Frame& f, int threads = 0,
                                const std::optional<FrameBox>& part = std::nullopt);

// The photo pixels apply_frame reads for `part` (as above) of a `width` x `height` photo framed by
// `f`, as a box of photo pixels: columns [x0, x0 + width), rows [y0, y0 + height). Reading only
// these gives the same part, so a caller holding the photo in pieces knows which to have ready.
//
// At an angle of exactly 0 it is the part's own pixels, moved by the framed_box's corner. Turned,
// it is the box around the photo positions of the part's four corner pixels, widened by the
// Catmull-Rom taps (one pixel before, two after, on each axis) and held inside the photo, as the
// taps are, so it is never empty. It holds exactly the pixels between the lowest and the highest
// tap: no more. Throws as apply_frame for an invalid `f` or `part`, and if width or height is
// below 1.
[[nodiscard]] FrameBox frame_source_rect(const Frame& f, int width, int height,
                                         const FrameBox& part);

// What an export does with the frame, for a decode of `width` x `height`.
struct ExportFrame {
    // The frame to apply, fitted (fit_frame); nothing when the export takes today's path: no frame,
    // or a frame that is the whole photo (is_whole_photo). Nothing means no apply_frame, no framed
    // grid for the dodge & burn stage and no change to the size or the pixels.
    std::optional<Frame> fitted;
    // The size after framing, before any reduction: the decode's own size when `fitted` is empty.
    // Original, Long side and Percent all measure this size.
    PixelSize size;
    // Fitting made the frame smaller or moved it, so it is no longer what was asked for.
    bool changed = false;
};

// The frame an export of a `width` x `height` decode applies. Throws std::invalid_argument with
// validate_frame's messages if `frame` is invalid, or if width or height is below 1.
[[nodiscard]] ExportFrame plan_export_frame(const std::optional<Frame>& frame, int width,
                                            int height);

struct FramedExport {
    Image image;
    ExportFrame frame;  // as plan_export_frame(...) gave it
    bool reduced = false;  // downscale ran (the framed image was larger than long_side)
    // How long apply_frame and downscale took, in milliseconds (0 for a step that did not run).
    double frame_ms = 0.0;
    double reduce_ms = 0.0;
    // The threads each step really used (0 for a step that did not run): apply_frame is sized by
    // the framed image and downscale by the image it reduces, as `threads` (0 = automatic, else a
    // cap) is resolved by threads_for_pixels, so a large decode turns on many threads even when
    // the result is small.
    int frame_threads = 0;
    int reduce_threads = 0;
};

// The order of an export: the decoded image is framed (plan_export_frame, then apply_frame, so the
// turn works on the full-resolution decode), then reduced to `long_side` if the framed image is
// larger (downscale, never an enlargement), and the caller runs the stages on the result. With no
// framing to do (`frame.fitted` empty) the image is only reduced, exactly as before frames existed.
// `decoded` is moved from, so the full decode is freed before the reduction allocates.
//
// A caller with a percentage works out `long_side` from plan_export_frame(...).size. For the dodge
// & burn stage a framed export uses PixelGrid::framed(*frame.fitted, width, height, scale, w, h)
// with the decode's `width` x `height`, the result's w x h and scale = framed size / w; an export
// without a fitted frame uses PixelGrid::whole as it always did.
// Throws as plan_export_frame, and std::invalid_argument (from downscale) if long_side is below 1.
[[nodiscard]] FramedExport frame_and_reduce(Image decoded, const std::optional<Frame>& frame,
                                            std::optional<int> long_side, int threads = 0);

}  // namespace prism

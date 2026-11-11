#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "prism/resize.hpp"

namespace prism::gui {

// How big an export should be: the file's own size, or smaller (Prism never enlarges). It is its
// own small header because both the session (which exports) and the preferences (which remember
// the choice) use it.
struct ExportSize {
    enum class Mode {
        original,   // the file's size
        long_side,  // the longer side is `long_side` pixels
        percent,    // the longer side is `percent` percent of the file's longer side
    };
    Mode mode = Mode::original;
    int long_side = 2048;  // pixels, at least 1; used when mode == long_side
    int percent = 50;      // 1..99; used when mode == percent

    bool operator==(const ExportSize&) const = default;
};

// The longer side to reduce an export to, or nothing for the file's own size: for `original`, and
// when the requested side would not be smaller than the file's longer side (a long side at or above
// it, or a percentage that rounds to it). A percentage is of the FILE's size, `file_width` x
// `file_height` (see Session::file_width), not of whatever size the decode happens to have, so 50%
// of a RAW file's sensor size stays 50% of the sensor size under half-size decoding. A malformed
// ExportSize (long side < 1, percent outside 1..100) also gives nothing.
[[nodiscard]] std::optional<int> export_long_side(const ExportSize& size, int file_width,
                                                  int file_height);

// What an export will really write. `start` is the size it decodes at: the file's own size, or for
// a RAW file with half-size decoding the half-size decode (Session::export_start_size). `size` is
// what is saved: `start`, or smaller. `long_side` is set only when the export reduces the image,
// so it is also the number a reduced export's file name carries (suggested_export_path).
struct ExportPlan {
    PixelSize start;
    PixelSize size;
    std::optional<int> long_side;

    [[nodiscard]] bool reduced() const noexcept { return long_side.has_value(); }
};

// The plan for `size` on a file of `file` pixels that is decoded at `start`. The wanted side comes
// from the FILE's size (export_long_side), but the image only shrinks if the decode is larger than
// it: 50% of a 6000 x 4000 RAW is 3000, and its half-size decode (3000 x 2000) is already that.
// The shorter side is worked out as downscale() does.
[[nodiscard]] ExportPlan plan_export(const ExportSize& size, PixelSize file, PixelSize start);

// The marker for a size that is only an estimate (a RAW file whose real size is not known yet): "about "
// before the number, "" when it is exact. The status bar's preview-size line and the Export sheet
// both use it, so they say it the same way.
[[nodiscard]] std::string_view size_estimate_prefix(bool exact) noexcept;

// The Export sheet's result line, with the real multiplication sign and no digit grouping:
// "Exports at 2048 x 1365" when reduced, "Not smaller than the photo: exports at full size, W x H"
// when a size was asked for that does not reduce it, "Exports at full size, W x H" for Original.
// With `exact` false the size reads "about W x H" (size_estimate_prefix), as it comes from an estimate.
[[nodiscard]] std::string export_result_text(const ExportSize& size, const ExportPlan& plan,
                                             bool exact = true);

// The sheet's value row for Original: "Full size, W x H" with the file's size ("Full size, about
// W x H" while `exact` is false).
[[nodiscard]] std::string export_original_text(PixelSize file, bool exact = true);

}  // namespace prism::gui

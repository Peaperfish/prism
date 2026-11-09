#pragma once

#include <optional>

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

}  // namespace prism::gui

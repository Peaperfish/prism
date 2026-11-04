#pragma once

#include <span>
#include <string_view>

namespace prism::gui {

// The files in recipes/, compiled in by the build (gui_core/CMakeLists.txt), so the app needs no
// recipe folder wherever it is copied to.
struct BuiltinLookFile {
    std::string_view id;      // the file's stem, stable: "natural", "vivid", ...
    std::string_view source;  // names it in error messages: "raw/vivid.prism"
    std::string_view text;    // the file's text: LF line ends, no byte-order mark
};

// In display order; the first is the default.
[[nodiscard]] std::span<const BuiltinLookFile> builtin_raw_look_files();
[[nodiscard]] std::span<const BuiltinLookFile> builtin_image_look_files();

}  // namespace prism::gui

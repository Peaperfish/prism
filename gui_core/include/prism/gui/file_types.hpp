#pragma once

#include <filesystem>
#include <string>

namespace prism::gui {

// What the file dialogs offer, and the names they hand back. Dialog filters are written as
// semicolon-separated extensions without dots ("png;jpg;jpeg"), the form SDL's dialogs take.

// The extensions Prism opens: PNG and JPEG always, the RAW formats only if this build has LibRaw
// (raw_support_available()), so the dialog never offers a file that would fail to open.
[[nodiscard]] std::string open_filter_pattern();

// The formats the save dialog offers, in its filter order.
enum class ExportFormat { png, jpeg };

// What an export of `source` defaults to: PNG for a PNG, JPEG for anything else (a JPEG, or a RAW
// file, whose camera would have made a JPEG).
[[nodiscard]] ExportFormat default_export_format(const std::filesystem::path& source);

// The save dialog's answer as a file name to write: if `chosen` already ends in .png, .jpg or
// .jpeg (any case) it is kept as it is; otherwise the extension of `format` is appended, since
// not every platform's dialog adds it ("photo" -> "photo.jpg"; "photo.v2" -> "photo.v2.jpg").
[[nodiscard]] std::filesystem::path with_export_extension(const std::filesystem::path& chosen,
                                                          ExportFormat format);

// Where the save dialog starts: next to the source, named after it, so exporting never offers the
// source file itself ("DSC0001.ARW" -> "DSC0001_prism.jpg" in the same folder).
[[nodiscard]] std::filesystem::path suggested_export_path(const std::filesystem::path& source,
                                                          ExportFormat format);

// --- Recipes (.prism) -----------------------------------------------------------------------------

// The recipe dialogs' filter: "prism".
[[nodiscard]] std::string recipe_filter_pattern();

// True if `path` ends in .prism, in any case.
[[nodiscard]] bool is_recipe_file(const std::filesystem::path& path);

// The recipe save dialog's answer as a file name to write: kept as it is when it already ends in
// .prism (any case), otherwise with ".prism" added, as with_export_extension does
// ("warm" -> "warm.prism"; "warm.v2" -> "warm.v2.prism").
[[nodiscard]] std::filesystem::path with_recipe_extension(const std::filesystem::path& chosen);

}  // namespace prism::gui

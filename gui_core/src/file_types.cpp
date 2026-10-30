#include "prism/gui/file_types.hpp"

#include "prism/raw.hpp"
#include "prism/utf8_path.hpp"

namespace prism::gui {

namespace fs = std::filesystem;

namespace {

const char* extension_of(ExportFormat format) {
    return format == ExportFormat::png ? ".png" : ".jpg";
}

std::string lowercase_extension(const fs::path& path) {
    std::string ext = utf8(path.extension());
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext;
}

}  // namespace

std::string open_filter_pattern() {
    std::string pattern = "png;jpg;jpeg";
    if (raw_support_available()) {
        pattern += ";arw;dng;cr2;cr3;nef;raf";  // what is_raw_file() accepts
    }
    return pattern;
}

ExportFormat default_export_format(const fs::path& source) {
    return lowercase_extension(source) == ".png" ? ExportFormat::png : ExportFormat::jpeg;
}

fs::path with_export_extension(const fs::path& chosen, ExportFormat format) {
    const std::string ext = lowercase_extension(chosen);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        return chosen;
    }
    fs::path result = chosen;
    result += extension_of(format);
    return result;
}

fs::path suggested_export_path(const fs::path& source, ExportFormat format) {
    fs::path name = source.stem();
    name += "_prism";
    name += extension_of(format);
    return source.parent_path() / name;
}

}  // namespace prism::gui

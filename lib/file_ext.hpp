#pragma once

// Internal helper shared by image.cpp and raw.cpp; not part of the public API.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include "prism/utf8_path.hpp"

namespace prism::detail {

// The extension including the dot, lower-cased: "photo.ARW" -> ".arw". As UTF-8, so a
// non-ASCII extension can't make it throw on Windows; in the C locale tolower only changes
// ASCII letters.
inline std::string lowercase_extension(const std::filesystem::path& path) {
    std::string ext = utf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

}  // namespace prism::detail

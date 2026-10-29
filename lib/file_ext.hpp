#pragma once

// Internal helper shared by image.cpp and raw.cpp; not part of the public API.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace prism::detail {

// The extension including the dot, lower-cased: "photo.ARW" -> ".arw".
inline std::string lowercase_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

}  // namespace prism::detail

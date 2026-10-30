#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace prism {

// Converting between std::filesystem::path and UTF-8 text.
//
// On macOS and Linux a path is a string of bytes (UTF-8 by convention) and path::string()
// returns them unchanged. On Windows a path is UTF-16, and path::string() converts it to the
// "ANSI" code page (e.g. Windows-1252), which holds only a small part of Unicode: for a name in
// Chinese or Japanese, or with an emoji, it throws. These functions use UTF-8 on Windows and keep
// the bytes exactly as they were on other systems, so nothing changes there.

// The path as UTF-8 text, for messages and anything else printed or stored as a std::string.
inline std::string utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    // In C++20 u8string() returns std::u8string, whose elements are char8_t; copying them one by
    // one into a std::string keeps the same byte values.
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
#else
    return path.string();
#endif
}

// A path from UTF-8 text, such as a command-line argument (UTF-8 on Windows because prism.exe
// and prism_profile.exe ask for it in their manifest) or a file name from a UI toolkit.
inline std::filesystem::path path_from_utf8(std::string_view text) {
#ifdef _WIN32
    // Constructing from char8_t tells path the text is UTF-8, not the ANSI code page.
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
#else
    return std::filesystem::path(text);
#endif
}

}  // namespace prism

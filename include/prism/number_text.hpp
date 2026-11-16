#pragma once

#include <string>
#include <string_view>

namespace prism {

// `text` with a leading real minus sign (U+2212, "−") written as "-". The docs and messages show
// negative numbers with "−", so a number copied from them still reads.
inline std::string minus_as_hyphen(std::string_view text) {
    constexpr std::string_view minus = "\xE2\x88\x92";  // U+2212 in UTF-8
    if (text.substr(0, minus.size()) == minus) {
        text.remove_prefix(minus.size());
        return "-" + std::string(text);
    }
    return std::string(text);
}

}  // namespace prism

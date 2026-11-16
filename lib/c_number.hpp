#pragma once

// strtod and strtof that always read numbers as the C locale does ("1.5", never "1,5"), whatever
// LC_NUMERIC is at the moment. SDL's X11 code on Linux sets LC_ALL from the environment for a
// moment (keyboard setup, message boxes), and a plain strtod would then stop at the dot in a
// German or French locale. Same contract as std::strtod / std::strtof, including errno = ERANGE.
// (std::from_chars for float would do, but needs macOS 26; Prism supports 13.3.)

namespace prism {

double c_strtod(const char* text, char** end);
float c_strtof(const char* text, char** end);

}  // namespace prism

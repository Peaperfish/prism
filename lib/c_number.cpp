#include "c_number.hpp"

#include <locale.h>

#include <cstdlib>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace prism {

namespace {

#ifdef _WIN32
_locale_t c_locale() {
    static const _locale_t locale = _create_locale(LC_ALL, "C");
    return locale;
}
#else
locale_t c_locale() {
    static const locale_t locale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
    return locale;
}
#endif

}  // namespace

double c_strtod(const char* text, char** end) {
#ifdef _WIN32
    return _strtod_l(text, end, c_locale());
#else
    return strtod_l(text, end, c_locale());
#endif
}

float c_strtof(const char* text, char** end) {
#ifdef _WIN32
    return _strtof_l(text, end, c_locale());
#else
    return strtof_l(text, end, c_locale());
#endif
}

}  // namespace prism

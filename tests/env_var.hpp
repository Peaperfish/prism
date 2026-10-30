#pragma once

// A test helper for the tests that read PRISM_TEST_* variables; not part of Prism.

#include <cstddef>
#include <cstdlib>
#include <string>

namespace prism_test {

// The value of environment variable `name`, or "" if it is not set. MSVC deprecates
// std::getenv (C4996) because the pointer it returns can be invalidated by a later change to
// the environment; _dupenv_s returns a copy instead, which we free once it is in a std::string.
inline std::string env_var(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

}  // namespace prism_test

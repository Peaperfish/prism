#pragma once

// A test helper shared by the tests that run prism and prism_profile; not part of Prism.

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace prism_test {

// Runs `command` (UTF-8) through the shell, like std::system, and returns the program's exit
// code (-1 if it did not exit normally, e.g. it crashed).
//
// On Windows std::system runs `cmd.exe /c <command>`. If that line starts with a quote and has
// more than two, cmd removes its first and last quote, so `"C:\prism.exe" "in.png" > "log"`
// becomes `C:\prism.exe" "in.png" > "log` and fails. One more pair of quotes around the whole
// line gives cmd a pair to remove and leaves the command's own quotes as they were.
//
// std::system would also pass the line to cmd in the ANSI code page, losing any character it
// can't represent (CJK, emoji, ...), so it goes to _wsystem as UTF-16 instead.
// std::filesystem::path already converts UTF-8 (char8_t) to UTF-16, which saves including
// <windows.h> for MultiByteToWideChar.
inline int run_command(const std::string& command) {
#ifdef _WIN32
    const std::string line = "\"" + command + "\"";
    const std::filesystem::path wide(std::u8string(line.begin(), line.end()));
    return _wsystem(wide.c_str());
#else
    // On POSIX, std::system returns a wait status; WEXITSTATUS extracts the exit code.
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

}  // namespace prism_test

#pragma once

// A test helper shared by the tests that run prism and prism_profile; not part of Prism.

#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace prism_test {

// Runs `command` through the shell, like std::system, and returns the program's exit code
// (-1 if it did not exit normally, e.g. it crashed).
//
// On Windows std::system runs `cmd.exe /c <command>`. If that line starts with a quote and has
// more than two, cmd removes its first and last quote, so `"C:\prism.exe" "in.png" > "log"`
// becomes `C:\prism.exe" "in.png" > "log` and fails. One more pair of quotes around the whole
// line gives cmd a pair to remove and leaves the command's own quotes as they were.
inline int run_command(const std::string& command) {
#ifdef _WIN32
    return std::system(("\"" + command + "\"").c_str());
#else
    // On POSIX, std::system returns a wait status; WEXITSTATUS extracts the exit code.
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

}  // namespace prism_test

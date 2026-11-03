#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace prism {

// Why write_file_atomically failed.
struct AtomicWriteError {
    enum class Step {
        write,   // the temporary file next to `path` could not be written
        rename,  // it was written, but could not be renamed over `path`
    };
    Step step = Step::write;
    std::filesystem::path file;  // the temporary file (write) or the target (rename)
    std::string reason;          // the system's words for a failed rename; empty for a write
};

// Writes `contents` to `path` atomically: to a temporary file next to it (`path` + ".tmp"), then
// renamed over it, so a crash or a full disk never leaves a half-written file behind and readers
// see either the old file or the new one. The bytes are written exactly as given. On failure the
// temporary file is removed and `path` is left as it was. Nothing on success.
[[nodiscard]] std::optional<AtomicWriteError> write_file_atomically(
    const std::filesystem::path& path, std::string_view contents);

}  // namespace prism

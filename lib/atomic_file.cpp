#include "prism/atomic_file.hpp"

#include <fstream>
#include <system_error>

namespace prism {

namespace fs = std::filesystem;

std::optional<AtomicWriteError> write_file_atomically(const fs::path& path,
                                                      std::string_view contents) {
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            fs::remove(temp, ignored);
            return AtomicWriteError{AtomicWriteError::Step::write, temp, {}};
        }
    }
    // rename replaces an existing file in one step (on Windows too: MoveFileEx with
    // MOVEFILE_REPLACE_EXISTING), so readers see either the old file or the new one.
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return AtomicWriteError{AtomicWriteError::Step::rename, path, ec.message()};
    }
    return std::nullopt;
}

}  // namespace prism

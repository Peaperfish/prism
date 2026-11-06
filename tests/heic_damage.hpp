#pragma once

// Test helper: damaged copies of a HEIC file, made at test time from the committed fixture so no
// broken binary has to be checked in. Not part of Prism.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace prism_test {

inline std::vector<std::uint8_t> read_all_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

inline void write_all_bytes(const std::filesystem::path& path,
                            const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

// Offset of the first byte after the header of the first 'mdat' box (the picture data), or the
// size of `bytes` if there is no such box.
inline std::size_t mdat_payload_offset(const std::vector<std::uint8_t>& bytes) {
    const std::string tag = "mdat";
    const auto it = std::search(bytes.begin(), bytes.end(), tag.begin(), tag.end());
    if (it == bytes.end() || it - bytes.begin() < 4) {
        return bytes.size();
    }
    const std::size_t at = static_cast<std::size_t>(it - bytes.begin());
    // A box starts with its 32-bit size; size 1 means a 64-bit size follows the type.
    const bool large =
        bytes[at - 4] == 0 && bytes[at - 3] == 0 && bytes[at - 2] == 0 && bytes[at - 1] == 1;
    return std::min(bytes.size(), at + 4 + (large ? 8 : 0));
}

// Writes into `dir` four damaged copies of the HEIC file `source`: cut off after 600, 545 and 520
// bytes, and one with every byte after the 'mdat' box header inverted. Returns their paths.
inline std::vector<std::filesystem::path> write_damaged_heic_copies(
    const std::filesystem::path& source, const std::filesystem::path& dir) {
    const std::vector<std::uint8_t> whole = read_all_bytes(source);
    std::vector<std::filesystem::path> paths;
    for (const std::size_t keep : {std::size_t{600}, std::size_t{545}, std::size_t{520}}) {
        const std::vector<std::uint8_t> cut(
            whole.begin(),
            whole.begin() + static_cast<std::ptrdiff_t>(std::min(keep, whole.size())));
        paths.push_back(dir / ("cut_at_" + std::to_string(keep) + ".heic"));
        write_all_bytes(paths.back(), cut);
    }
    std::vector<std::uint8_t> inverted = whole;
    for (std::size_t i = mdat_payload_offset(whole); i < inverted.size(); ++i) {
        inverted[i] = static_cast<std::uint8_t>(~inverted[i]);
    }
    paths.push_back(dir / "inverted_mdat.heic");
    write_all_bytes(paths.back(), inverted);
    return paths;
}

}  // namespace prism_test

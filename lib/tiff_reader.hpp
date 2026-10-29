#pragma once

// Internal: a minimal reader for the first IFD of a TIFF file, enough for the colour tags of a
// DNG file or a DCP camera profile; not part of the public API.
//
// TIFF 6.0, section 2: a file starts with "II" (little-endian) or "MM" (big-endian), a 16-bit
// magic number and the 32-bit offset of IFD 0, the first image file directory. An IFD is a
// 16-bit entry count, that many 12-byte entries (tag, type, count, value or offset) and the
// 32-bit offset of the next IFD. A value that fits in 4 bytes is stored in the entry itself; a
// longer one is stored elsewhere and the entry holds its offset from the start of the file.
//
// A DNG is a TIFF with magic number 42. A DCP camera profile has the same layout with magic
// number 0x4352 ("CR"); its offsets are relative to the start of its byte order mark, which for
// a stand-alone .dcp file is also the start of the file (DNG 1.7.1.0, ExtraCameraProfiles).
//
// Only IFD 0 is read: no next-IFD chain and no SubIFDs, so a malicious file can't make the reader
// loop. Everything taken from the file is bounds-checked: parse_tiff either returns entries whose
// data lies inside the buffer, or throws std::runtime_error.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace prism::detail {

// The TIFF field types this reader understands. Entries of any other type (SBYTE, SSHORT, SLONG,
// DOUBLE, or types added after TIFF 6.0) are skipped, as TIFF 6.0 tells readers to do.
enum class TiffType : std::uint16_t {
    byte = 1,        // BYTE: 8-bit unsigned integer
    ascii = 2,       // ASCII: 8-bit characters, NUL-terminated
    uint16 = 3,      // SHORT
    uint32 = 4,      // LONG
    rational = 5,    // RATIONAL: two LONGs, numerator and denominator
    undefined = 7,   // UNDEFINED: bytes whose meaning depends on the tag
    srational = 10,  // SRATIONAL: two SLONGs (signed)
    float32 = 11,    // FLOAT: IEEE single precision
};

enum class TiffMagic {
    tiff,            // 42: TIFF, DNG
    camera_profile,  // 0x4352: DCP
};

struct TiffEntry {
    std::uint16_t tag = 0;
    TiffType type = TiffType::byte;
    std::uint32_t count = 0;          // number of values, not bytes
    std::span<const std::byte> data;  // count * (size of one value) bytes, inside the file
};

// IFD 0 of a TIFF file. The entries point into the buffer given to parse_tiff, so that buffer
// must outlive this.
struct TiffIfd {
    TiffMagic magic = TiffMagic::tiff;
    bool big_endian = false;
    std::vector<TiffEntry> entries;  // in file order, skipped types left out

    // The entry with this tag, or nullptr.
    [[nodiscard]] const TiffEntry* find(std::uint16_t tag) const noexcept;

    // Value i of an entry. Each throws std::out_of_range if i >= count, and std::runtime_error if
    // the entry's type doesn't fit:
    //   integer: BYTE, SHORT or LONG (TIFF 6.0: readers should accept any of the three for an
    //            unsigned integer field).
    //   number:  any numeric type, as a double. Throws std::runtime_error for a RATIONAL or
    //            SRATIONAL with denominator 0. A FLOAT may be NaN or infinite: callers check.
    [[nodiscard]] std::uint32_t integer(const TiffEntry& entry, std::uint32_t i) const;
    [[nodiscard]] double number(const TiffEntry& entry, std::uint32_t i) const;

    // An ASCII or BYTE entry as text, up to its first NUL (or all of it, if it has none).
    [[nodiscard]] std::string string(const TiffEntry& entry) const;
};

// Parses the header and IFD 0. Throws std::runtime_error (message starting "TIFF: ") if the file
// is not a TIFF with magic 42 or 0x4352, if any part of the header, IFD or an entry's data lies
// outside the file, or if IFD 0 has no entries or the same tag twice.
[[nodiscard]] TiffIfd parse_tiff(std::span<const std::byte> file);

}  // namespace prism::detail

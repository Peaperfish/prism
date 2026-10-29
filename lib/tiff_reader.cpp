#include "tiff_reader.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace prism::detail {

namespace {

[[noreturn]] void fail(const std::string& why) {
    throw std::runtime_error("TIFF: " + why);
}

std::string hex(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%04X", static_cast<unsigned>(v));
    return buf;
}

std::string tag_name(std::uint16_t tag) {
    return "tag " + std::to_string(tag);
}

// Bytes per value, or 0 for a type this reader skips.
std::uint64_t type_size(std::uint16_t type) noexcept {
    switch (type) {
        case 1:  // BYTE
        case 2:  // ASCII
        case 7:  // UNDEFINED
            return 1;
        case 3:  // SHORT
            return 2;
        case 4:   // LONG
        case 11:  // FLOAT
            return 4;
        case 5:   // RATIONAL
        case 10:  // SRATIONAL
            return 8;
        default: return 0;
    }
}

const char* type_name(TiffType type) noexcept {
    switch (type) {
        case TiffType::byte: return "BYTE";
        case TiffType::ascii: return "ASCII";
        case TiffType::uint16: return "SHORT";
        case TiffType::uint32: return "LONG";
        case TiffType::rational: return "RATIONAL";
        case TiffType::undefined: return "UNDEFINED";
        case TiffType::srational: return "SRATIONAL";
        case TiffType::float32: return "FLOAT";
    }
    return "?";
}

// A 2- or 4-byte unsigned integer at bytes[pos], in the file's byte order. It is put together
// byte by byte, so it works whatever the computer's own byte order is, and never reads through a
// cast pointer (which could be misaligned). The bounds check is written so that it can't overflow.
std::uint32_t read_uint(std::span<const std::byte> bytes, std::uint64_t pos, int size,
                        bool big_endian) {
    if (pos > bytes.size() || static_cast<std::uint64_t>(size) > bytes.size() - pos) {
        fail("read of " + std::to_string(size) + " bytes at offset " + std::to_string(pos) +
             " is past the end (" + std::to_string(bytes.size()) + " bytes)");
    }
    std::uint32_t v = 0;
    for (int k = 0; k < size; ++k) {
        const int byte = big_endian ? k : size - 1 - k;  // most significant first
        v = (v << 8) | std::to_integer<std::uint32_t>(bytes[static_cast<std::size_t>(pos) + byte]);
    }
    return v;
}

void check_index(const TiffEntry& entry, std::uint32_t i) {
    if (i >= entry.count) {
        throw std::out_of_range("TIFF: value " + std::to_string(i) + " of " + tag_name(entry.tag) +
                                ", which has only " + std::to_string(entry.count));
    }
}

}  // namespace

const TiffEntry* TiffIfd::find(std::uint16_t tag) const noexcept {
    for (const TiffEntry& e : entries) {
        if (e.tag == tag) {
            return &e;
        }
    }
    return nullptr;
}

std::uint32_t TiffIfd::integer(const TiffEntry& entry, std::uint32_t i) const {
    check_index(entry, i);
    switch (entry.type) {
        case TiffType::byte: return read_uint(entry.data, i, 1, big_endian);
        case TiffType::uint16: return read_uint(entry.data, 2ull * i, 2, big_endian);
        case TiffType::uint32: return read_uint(entry.data, 4ull * i, 4, big_endian);
        default:
            fail(tag_name(entry.tag) + " has type " + type_name(entry.type) +
                 ", expected an integer (BYTE, SHORT or LONG)");
    }
}

double TiffIfd::number(const TiffEntry& entry, std::uint32_t i) const {
    check_index(entry, i);
    switch (entry.type) {
        case TiffType::byte:
        case TiffType::uint16:
        case TiffType::uint32: return integer(entry, i);
        case TiffType::rational:
        case TiffType::srational: {
            const std::uint32_t num = read_uint(entry.data, 8ull * i, 4, big_endian);
            const std::uint32_t den = read_uint(entry.data, 8ull * i + 4, 4, big_endian);
            if (entry.type == TiffType::rational) {
                if (den == 0) {
                    fail(tag_name(entry.tag) + " value " + std::to_string(i) +
                         " is a RATIONAL with denominator 0");
                }
                return static_cast<double>(num) / den;
            }
            // SLONGs are two's complement; converting uint32_t to int32_t wraps (defined since
            // C++20), which is exactly that.
            const auto snum = static_cast<std::int32_t>(num);
            const auto sden = static_cast<std::int32_t>(den);
            if (sden == 0) {
                fail(tag_name(entry.tag) + " value " + std::to_string(i) +
                     " is an SRATIONAL with denominator 0");
            }
            return static_cast<double>(snum) / sden;
        }
        case TiffType::float32:
            return std::bit_cast<float>(read_uint(entry.data, 4ull * i, 4, big_endian));
        default:
            fail(tag_name(entry.tag) + " has type " + type_name(entry.type) +
                 ", expected a number");
    }
}

std::string TiffIfd::string(const TiffEntry& entry) const {
    if (entry.type != TiffType::ascii && entry.type != TiffType::byte) {
        fail(tag_name(entry.tag) + " has type " + type_name(entry.type) +
             ", expected ASCII or BYTE text");
    }
    std::string out;
    for (const std::byte b : entry.data) {
        if (b == std::byte{0}) {
            break;
        }
        out += static_cast<char>(b);
    }
    return out;
}

TiffIfd parse_tiff(std::span<const std::byte> file) {
    const std::uint64_t size = file.size();
    if (size < 8) {
        fail("file is too short for a TIFF header (" + std::to_string(size) + " bytes, need 8)");
    }

    TiffIfd ifd;
    if (file[0] == std::byte{'I'} && file[1] == std::byte{'I'}) {
        ifd.big_endian = false;
    } else if (file[0] == std::byte{'M'} && file[1] == std::byte{'M'}) {
        ifd.big_endian = true;
    } else {
        fail("not a TIFF file (no II or MM byte order mark)");
    }
    const bool big = ifd.big_endian;

    const std::uint32_t magic = read_uint(file, 2, 2, big);
    if (magic == 42) {
        ifd.magic = TiffMagic::tiff;
    } else if (magic == 0x4352) {
        ifd.magic = TiffMagic::camera_profile;
    } else if (magic == 43) {
        fail("BigTIFF files (magic number 43) are not supported");
    } else {
        fail("unknown magic number " + hex(magic) +
             " (expected 42 for TIFF or DNG, or 0x4352 for a DCP camera profile)");
    }

    // All offset arithmetic is in 64 bits: offsets and counts from the file are at most 2^32 - 1,
    // so sums and products like these can't wrap around.
    const std::uint64_t ifd_offset = read_uint(file, 4, 4, big);
    if (ifd_offset < 8) {
        fail("IFD 0 offset " + std::to_string(ifd_offset) +
             (ifd_offset == 0 ? " means the file has no image file directory"
                              : " points into the 8-byte header"));
    }
    if (ifd_offset + 2 > size) {
        fail("IFD 0 offset " + std::to_string(ifd_offset) + " is past the end of the file (" +
             std::to_string(size) + " bytes)");
    }
    const std::uint64_t n = read_uint(file, ifd_offset, 2, big);
    if (n == 0) {
        fail("IFD 0 has no entries");
    }
    // Entry count, n entries of 12 bytes, next-IFD offset (not followed, but it must be there).
    const std::uint64_t ifd_end = ifd_offset + 2 + 12 * n + 4;
    if (ifd_end > size) {
        fail("IFD 0 at offset " + std::to_string(ifd_offset) + " has " + std::to_string(n) +
             " entries, which need " + std::to_string(ifd_end) + " bytes, but the file has " +
             std::to_string(size));
    }

    // n is at most 65535 and the whole IFD is known to fit in the file, so these are bounded.
    std::vector<std::uint16_t> tags;
    tags.reserve(n);
    ifd.entries.reserve(n);
    for (std::uint64_t i = 0; i < n; ++i) {
        const std::uint64_t at = ifd_offset + 2 + 12 * i;
        const auto tag = static_cast<std::uint16_t>(read_uint(file, at, 2, big));
        const auto type = static_cast<std::uint16_t>(read_uint(file, at + 2, 2, big));
        const std::uint32_t count = read_uint(file, at + 4, 4, big);
        tags.push_back(tag);

        const std::uint64_t one = type_size(type);
        if (one == 0) {
            continue;  // a type we don't read: skip it, as TIFF 6.0 says
        }
        // At most 8 * (2^32 - 1): can't wrap in 64 bits. (In 32 bits, e.g. 0x20000000 SRATIONALs
        // would wrap to 0 bytes and look like an inline value.)
        const std::uint64_t bytes = one * count;
        std::uint64_t value_at = at + 8;  // inline: in the entry's last 4 bytes
        if (bytes > 4) {
            value_at = read_uint(file, at + 8, 4, big);
            if (value_at > size || bytes > size - value_at) {
                fail(tag_name(tag) + " (" + type_name(static_cast<TiffType>(type)) + ", count " +
                     std::to_string(count) + ", " + std::to_string(bytes) +
                     " bytes) points to offset " + std::to_string(value_at) +
                     ", past the end of the file (" + std::to_string(size) + " bytes)");
            }
        }
        ifd.entries.push_back(
            {tag, static_cast<TiffType>(type), count,
             file.subspan(static_cast<std::size_t>(value_at), static_cast<std::size_t>(bytes))});
    }

    // Two entries with the same tag would make find() return whichever came first; a valid file
    // never has them, so treat them as damage rather than guess.
    std::sort(tags.begin(), tags.end());
    if (const auto dup = std::adjacent_find(tags.begin(), tags.end()); dup != tags.end()) {
        fail(tag_name(*dup) + " appears more than once in IFD 0");
    }
    return ifd;
}

}  // namespace prism::detail

#pragma once

// A test helper shared by the TIFF, DNG and DCP tests; not part of Prism.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tiff_reader.hpp"

namespace prism_test {

using prism::detail::TiffType;

// Builds small TIFF files in memory: an 8-byte header, IFD 0 and the values that don't fit in
// their entry. Entries are written in the order they were added (not sorted), so tests can also
// build files a real writer never would: duplicate tags, raw value fields that point anywhere,
// a wrong entry count or IFD offset.
class TiffBuilder {
public:
    explicit TiffBuilder(bool big_endian, std::uint16_t magic = 42)
        : big_(big_endian), magic_(magic) {}

    TiffBuilder& bytes(std::uint16_t tag, TiffType type, const std::vector<std::uint8_t>& v) {
        std::vector<std::uint8_t> data(v.begin(), v.end());
        return add(tag, static_cast<std::uint16_t>(type), static_cast<std::uint32_t>(v.size()),
                   std::move(data));
    }
    TiffBuilder& ascii(std::uint16_t tag, const std::string& text) {
        std::vector<std::uint8_t> data(text.begin(), text.end());
        data.push_back(0);  // the count includes the NUL
        const auto count = static_cast<std::uint32_t>(data.size());
        return add(tag, 2, count, std::move(data));
    }
    TiffBuilder& shorts(std::uint16_t tag, const std::vector<std::uint16_t>& v) {
        std::vector<std::uint8_t> data;
        for (std::uint16_t x : v) {
            put(data, x, 2);
        }
        return add(tag, 3, static_cast<std::uint32_t>(v.size()), std::move(data));
    }
    TiffBuilder& longs(std::uint16_t tag, const std::vector<std::uint32_t>& v) {
        std::vector<std::uint8_t> data;
        for (std::uint32_t x : v) {
            put(data, x, 4);
        }
        return add(tag, 4, static_cast<std::uint32_t>(v.size()), std::move(data));
    }
    TiffBuilder& rationals(std::uint16_t tag,
                           const std::vector<std::pair<std::uint32_t, std::uint32_t>>& v) {
        std::vector<std::uint8_t> data;
        for (const auto& [num, den] : v) {
            put(data, num, 4);
            put(data, den, 4);
        }
        return add(tag, 5, static_cast<std::uint32_t>(v.size()), std::move(data));
    }
    TiffBuilder& srationals(std::uint16_t tag,
                            const std::vector<std::pair<std::int32_t, std::int32_t>>& v) {
        std::vector<std::uint8_t> data;
        for (const auto& [num, den] : v) {
            put(data, static_cast<std::uint32_t>(num), 4);
            put(data, static_cast<std::uint32_t>(den), 4);
        }
        return add(tag, 10, static_cast<std::uint32_t>(v.size()), std::move(data));
    }
    TiffBuilder& floats(std::uint16_t tag, const std::vector<float>& v) {
        std::vector<std::uint8_t> data;
        for (float x : v) {
            put(data, std::bit_cast<std::uint32_t>(x), 4);
        }
        return add(tag, 11, static_cast<std::uint32_t>(v.size()), std::move(data));
    }
    // An entry with any type and count, whose 4-byte value field is written exactly as given
    // (no data is stored for it).
    TiffBuilder& raw(std::uint16_t tag, std::uint16_t type, std::uint32_t count,
                     std::uint32_t value_field) {
        entries_.push_back({tag, type, count, {}, value_field});
        return *this;
    }

    TiffBuilder& ifd_at_end() {
        ifd_at_end_ = true;
        return *this;
    }
    TiffBuilder& ifd_offset(std::uint32_t offset) {
        ifd_offset_override_ = offset;
        return *this;
    }
    TiffBuilder& entry_count(std::uint16_t n) {
        entry_count_override_ = n;
        return *this;
    }

    std::vector<std::byte> build() const {
        const std::size_t ifd_size = 2 + 12 * entries_.size() + 4;
        // Out-of-line values, each starting on a word (2-byte) boundary as TIFF 6.0 asks.
        std::vector<std::uint8_t> blob;
        std::vector<std::uint32_t> blob_at(entries_.size(), 0);
        const std::size_t blob_start = ifd_at_end_ ? 8 : 8 + ifd_size;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            if (!e.raw_field && e.data.size() > 4) {
                blob_at[i] = static_cast<std::uint32_t>(blob_start + blob.size());
                blob.insert(blob.end(), e.data.begin(), e.data.end());
                if (blob.size() % 2) {
                    blob.push_back(0);
                }
            }
        }
        const std::size_t ifd_at = ifd_at_end_ ? 8 + blob.size() : 8;

        std::vector<std::uint8_t> out;
        out.push_back(big_ ? 'M' : 'I');
        out.push_back(big_ ? 'M' : 'I');
        put(out, magic_, 2);
        put(out, ifd_offset_override_.value_or(static_cast<std::uint32_t>(ifd_at)), 4);
        std::vector<std::uint8_t> ifd;
        put(ifd, entry_count_override_.value_or(static_cast<std::uint16_t>(entries_.size())), 2);
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            put(ifd, e.tag, 2);
            put(ifd, e.type, 2);
            put(ifd, e.count, 4);
            if (e.raw_field) {
                put(ifd, *e.raw_field, 4);
            } else if (e.data.size() > 4) {
                put(ifd, blob_at[i], 4);
            } else {
                // Inline values are left-justified: in the first bytes of the field.
                for (std::size_t k = 0; k < 4; ++k) {
                    ifd.push_back(k < e.data.size() ? e.data[k] : 0);
                }
            }
        }
        put(ifd, 0, 4);  // no next IFD
        if (ifd_at_end_) {
            out.insert(out.end(), blob.begin(), blob.end());
            out.insert(out.end(), ifd.begin(), ifd.end());
        } else {
            out.insert(out.end(), ifd.begin(), ifd.end());
            out.insert(out.end(), blob.begin(), blob.end());
        }
        std::vector<std::byte> file(out.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            file[i] = static_cast<std::byte>(out[i]);
        }
        return file;
    }

private:
    struct Entry {
        std::uint16_t tag;
        std::uint16_t type;
        std::uint32_t count;
        std::vector<std::uint8_t> data;  // already in the file's byte order
        std::optional<std::uint32_t> raw_field;
    };

    TiffBuilder& add(std::uint16_t tag, std::uint16_t type, std::uint32_t count,
                     std::vector<std::uint8_t> data) {
        entries_.push_back({tag, type, count, std::move(data), std::nullopt});
        return *this;
    }

    void put(std::vector<std::uint8_t>& out, std::uint32_t v, int size) const {
        for (int k = 0; k < size; ++k) {
            const int shift = 8 * (big_ ? size - 1 - k : k);
            out.push_back(static_cast<std::uint8_t>(v >> shift));
        }
    }

    bool big_;
    std::uint16_t magic_;
    std::vector<Entry> entries_;
    bool ifd_at_end_ = false;
    std::optional<std::uint32_t> ifd_offset_override_;
    std::optional<std::uint16_t> entry_count_override_;
};

}  // namespace prism_test

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiff_reader.hpp"

using prism::detail::parse_tiff;
using prism::detail::TiffEntry;
using prism::detail::TiffIfd;
using prism::detail::TiffMagic;
using prism::detail::TiffType;

namespace {

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

// One of each supported type, inline and out of line, with awkward values.
TiffBuilder sample_file(bool big_endian, std::uint16_t magic) {
    TiffBuilder b(big_endian, magic);
    b.longs(254, {1})                                // LONG, inline
        .shorts(258, {8, 16})                        // 2 SHORTs: exactly 4 bytes, inline
        .shorts(259, {1, 0xFFFF, 0x1234})            // 3 SHORTs: 6 bytes, out of line
        .bytes(50706, TiffType::byte, {1, 7, 1, 0})  // BYTE, inline
        .ascii(50708, "TEST-CAM")                    // ASCII, out of line
        .ascii(50936, "abc")                         // ASCII with NUL: 4 bytes, inline
        .srationals(50721, {{-27, 100},
                            {12345, 10000},
                            {std::numeric_limits<std::int32_t>::min(), 1},
                            {1, -3},
                            {0, 7},
                            {-1, -2},
                            {44, 100},
                            {-8, 100},
                            {62, 100}})
        .rationals(50727, {{1, 1}, {3, 2}, {0xFFFFFFFF, 1}})
        .floats(50938, {5.0f, -1.25f, 1e-30f})
        .floats(50999, {0.5f})  // FLOAT, inline
        .bytes(37500, TiffType::undefined, {0x00, 0x01, 0xFE, 0xFF, 0x80})
        .longs(50941, {0xDEADBEEF});
    return b;
}

// Parses, expecting std::runtime_error whose message contains `expected`.
void expect_error(const std::vector<std::byte>& file, const std::string& expected) {
    try {
        (void)parse_tiff(file);
        ADD_FAILURE() << "parsed without error; expected \"" << expected << "\"";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find(expected), std::string::npos) << e.what();
    }
}

}  // namespace

// --- Round trips ---------------------------------------------------------------------------------

TEST(TiffReader, RoundTripsEveryTypeInBothByteOrdersAndMagics) {
    for (const bool big : {false, true}) {
        for (const std::uint16_t magic : {std::uint16_t{42}, std::uint16_t{0x4352}}) {
            SCOPED_TRACE(std::string(big ? "MM" : "II") + " magic " + std::to_string(magic));
            const std::vector<std::byte> file = sample_file(big, magic).build();
            const TiffIfd ifd = parse_tiff(file);
            EXPECT_EQ(ifd.big_endian, big);
            EXPECT_EQ(ifd.magic, magic == 42 ? TiffMagic::tiff : TiffMagic::camera_profile);
            ASSERT_EQ(ifd.entries.size(), 12u);

            const TiffEntry* e = ifd.find(254);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->type, TiffType::uint32);
            EXPECT_EQ(ifd.integer(*e, 0), 1u);

            e = ifd.find(258);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->count, 2u);
            EXPECT_EQ(ifd.integer(*e, 0), 8u);
            EXPECT_EQ(ifd.integer(*e, 1), 16u);

            e = ifd.find(259);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->type, TiffType::uint16);
            EXPECT_EQ(ifd.integer(*e, 0), 1u);
            EXPECT_EQ(ifd.integer(*e, 1), 0xFFFFu);
            EXPECT_EQ(ifd.integer(*e, 2), 0x1234u);

            e = ifd.find(50706);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->type, TiffType::byte);
            EXPECT_EQ(ifd.integer(*e, 1), 7u);
            EXPECT_EQ(ifd.number(*e, 1), 7.0);

            e = ifd.find(50708);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->type, TiffType::ascii);
            EXPECT_EQ(e->count, 9u);
            EXPECT_EQ(ifd.string(*e), "TEST-CAM");
            EXPECT_EQ(ifd.string(*ifd.find(50936)), "abc");

            e = ifd.find(50721);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->type, TiffType::srational);
            ASSERT_EQ(e->count, 9u);
            // Computed with the same division, so these are exactly equal.
            EXPECT_EQ(ifd.number(*e, 0), -27.0 / 100.0);
            EXPECT_EQ(ifd.number(*e, 1), 12345.0 / 10000.0);
            EXPECT_EQ(ifd.number(*e, 2), -2147483648.0);
            EXPECT_EQ(ifd.number(*e, 3), 1.0 / -3.0);
            EXPECT_EQ(ifd.number(*e, 4), 0.0);
            EXPECT_EQ(ifd.number(*e, 5), 0.5);
            EXPECT_EQ(ifd.number(*e, 8), 62.0 / 100.0);

            e = ifd.find(50727);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->type, TiffType::rational);
            EXPECT_EQ(ifd.number(*e, 1), 1.5);
            EXPECT_EQ(ifd.number(*e, 2), 4294967295.0);  // unsigned: not -1

            e = ifd.find(50938);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(ifd.number(*e, 0), 5.0);
            EXPECT_EQ(ifd.number(*e, 1), -1.25);
            EXPECT_EQ(ifd.number(*e, 2), static_cast<double>(1e-30f));
            EXPECT_EQ(ifd.number(*ifd.find(50999), 0), 0.5);

            e = ifd.find(37500);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->type, TiffType::undefined);
            ASSERT_EQ(e->data.size(), 5u);
            EXPECT_EQ(e->data[2], std::byte{0xFE});
            EXPECT_EQ(e->data[4], std::byte{0x80});

            EXPECT_EQ(ifd.integer(*ifd.find(50941), 0), 0xDEADBEEFu);
            EXPECT_EQ(ifd.find(1), nullptr);
        }
    }
}

TEST(TiffReader, IfdMayComeAfterTheData) {
    // TIFF 6.0: "Readers must follow the pointers wherever they may lead." (Some DNGs put IFD 0
    // at the end of the file.)
    const std::vector<std::byte> file = sample_file(false, 42).ifd_at_end().build();
    const TiffIfd ifd = parse_tiff(file);
    EXPECT_EQ(ifd.string(*ifd.find(50708)), "TEST-CAM");
    EXPECT_EQ(ifd.number(*ifd.find(50721), 1), 12345.0 / 10000.0);
}

TEST(TiffReader, SkipsUnexpectedTypes) {
    // SSHORT, DOUBLE and an unknown type 99. The DOUBLE's offset points far outside the file:
    // skipped entries are never followed, so that is not an error.
    const std::vector<std::byte> file = TiffBuilder(true)
                                            .shorts(256, {640})
                                            .raw(300, 8, 1, 0)
                                            .raw(301, 12, 1, 0xFFFFFFF0)
                                            .raw(302, 99, 1000, 0xFFFFFFF0)
                                            .build();
    const TiffIfd ifd = parse_tiff(file);
    ASSERT_EQ(ifd.entries.size(), 1u);
    EXPECT_EQ(ifd.integer(*ifd.find(256), 0), 640u);
    EXPECT_EQ(ifd.find(300), nullptr);
    EXPECT_EQ(ifd.find(301), nullptr);
    EXPECT_EQ(ifd.find(302), nullptr);
}

TEST(TiffReader, ZeroCountIsAnEmptyValue) {
    const std::vector<std::byte> file = TiffBuilder(false).raw(256, 10, 0, 0xFFFFFFFF).build();
    const TiffIfd ifd = parse_tiff(file);
    ASSERT_NE(ifd.find(256), nullptr);
    EXPECT_TRUE(ifd.find(256)->data.empty());
    EXPECT_THROW((void)ifd.number(*ifd.find(256), 0), std::out_of_range);
}

// --- Bad files -----------------------------------------------------------------------------------

TEST(TiffReader, RejectsEmptyAndShortFiles) {
    expect_error({}, "too short for a TIFF header (0 bytes");
    const std::vector<std::byte> file = sample_file(false, 42).build();
    expect_error({file.begin(), file.begin() + 1}, "too short");
    expect_error({file.begin(), file.begin() + 7}, "too short");
}

TEST(TiffReader, RejectsBadByteOrderMark) {
    for (const char* mark : {"IM", "ii", "mm", "\0\0"}) {
        std::vector<std::byte> file = sample_file(false, 42).build();
        file[0] = static_cast<std::byte>(mark[0]);
        file[1] = static_cast<std::byte>(mark[1]);
        expect_error(file, "no II or MM byte order mark");
    }
}

TEST(TiffReader, RejectsBadMagic) {
    expect_error(TiffBuilder(false, 0).shorts(256, {1}).build(), "unknown magic number 0x0000");
    expect_error(TiffBuilder(true, 0x2A00).shorts(256, {1}).build(), "unknown magic number 0x2A00");
    expect_error(TiffBuilder(false, 0x5243).shorts(256, {1}).build(), "unknown magic number");
    expect_error(TiffBuilder(false, 43).shorts(256, {1}).build(), "BigTIFF");
}

TEST(TiffReader, RejectsBadIfdOffsets) {
    const auto with_offset = [](std::uint32_t offset) {
        return TiffBuilder(false).shorts(256, {1}).ifd_offset(offset).build();
    };
    expect_error(with_offset(0), "no image file directory");
    expect_error(with_offset(4), "points into the 8-byte header");
    const std::uint32_t size = static_cast<std::uint32_t>(with_offset(8).size());
    expect_error(with_offset(size), "past the end of the file");
    expect_error(with_offset(size - 1), "past the end of the file");  // no room for the count
    expect_error(with_offset(0xFFFFFFFF), "past the end of the file");
}

TEST(TiffReader, RejectsBadEntryCounts) {
    expect_error(TiffBuilder(false).shorts(256, {1}).entry_count(0).build(),
                 "IFD 0 has no entries");
    expect_error(TiffBuilder(false).shorts(256, {1}).entry_count(0xFFFF).build(),
                 "has 65535 entries, which need 786434 bytes");
    // One entry too many: the last one (or the next-IFD offset) would run off the end.
    expect_error(TiffBuilder(false).shorts(256, {1}).entry_count(2).build(), "entries");
}

TEST(TiffReader, CountTimesSizeCannotWrapAround) {
    // 0x20000000 SRATIONALs are 2^32 bytes: 0 in 32-bit arithmetic, which would look like a value
    // stored inline in the entry. The same for 0x40000000 LONGs and 0x80000000 SHORTs.
    expect_error(TiffBuilder(false).raw(50721, 10, 0x20000000, 8).build(),
                 "4294967296 bytes) points to offset 8, past the end");
    expect_error(TiffBuilder(true).raw(256, 4, 0x40000000, 8).build(), "past the end");
    expect_error(TiffBuilder(false).raw(256, 3, 0x80000000, 8).build(), "past the end");
    expect_error(TiffBuilder(false).raw(256, 10, 0xFFFFFFFF, 8).build(), "past the end");
}

TEST(TiffReader, RejectsValueOffsetsPastTheEnd) {
    const std::uint32_t size =
        static_cast<std::uint32_t>(TiffBuilder(false).raw(50721, 10, 9, 0).build().size());
    // 9 SRATIONALs = 72 bytes, more than the whole file: starting at its end, one byte before it,
    // inside it, or so far out that offset + size would wrap in 32 bits.
    for (const std::uint32_t offset : {size, size - 1, 8u, 0xFFFFFFF0u, 0xFFFFFFFFu}) {
        SCOPED_TRACE(offset);
        expect_error(TiffBuilder(false).raw(50721, 10, 9, offset).build(),
                     "points to offset " + std::to_string(offset) + ", past the end");
    }
    // Data that ends exactly at the end of the file is fine (2 SRATIONALs = the last 16 bytes).
    EXPECT_NO_THROW((void)parse_tiff(TiffBuilder(false).raw(50721, 10, 2, size - 16).build()));
}

TEST(TiffReader, ZeroDenominatorIsAnErrorWhenRead) {
    const std::vector<std::byte> file = TiffBuilder(true)
                                            .rationals(50727, {{1, 1}, {1, 0}})
                                            .srationals(50721, {{-1, 0}, {0, 0}})
                                            .build();
    const TiffIfd ifd = parse_tiff(file);
    EXPECT_EQ(ifd.number(*ifd.find(50727), 0), 1.0);
    try {
        (void)ifd.number(*ifd.find(50727), 1);
        ADD_FAILURE() << "no error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("tag 50727 value 1 is a RATIONAL with denominator 0"),
                  std::string::npos)
            << e.what();
    }
    EXPECT_THROW((void)ifd.number(*ifd.find(50721), 0), std::runtime_error);
    EXPECT_THROW((void)ifd.number(*ifd.find(50721), 1), std::runtime_error);
}

TEST(TiffReader, RejectsDuplicateTags) {
    expect_error(TiffBuilder(false).shorts(256, {1}).longs(257, {2}).shorts(256, {3}).build(),
                 "tag 256 appears more than once");
    // Even when one of the two has a type that would be skipped.
    expect_error(TiffBuilder(true).raw(50721, 12, 1, 0).srationals(50721, {{1, 1}}).build(),
                 "tag 50721 appears more than once");
}

TEST(TiffReader, AccessorsCheckTypeAndIndex) {
    const std::vector<std::byte> file = sample_file(false, 42).build();
    const TiffIfd ifd = parse_tiff(file);
    EXPECT_THROW((void)ifd.integer(*ifd.find(50721), 0), std::runtime_error);  // SRATIONAL
    EXPECT_THROW((void)ifd.integer(*ifd.find(50938), 0), std::runtime_error);  // FLOAT
    EXPECT_THROW((void)ifd.number(*ifd.find(50708), 0), std::runtime_error);   // ASCII
    EXPECT_THROW((void)ifd.number(*ifd.find(37500), 0), std::runtime_error);   // UNDEFINED
    EXPECT_THROW((void)ifd.string(*ifd.find(259)), std::runtime_error);        // SHORT
    EXPECT_EQ(ifd.string(*ifd.find(50706)), "\x01\x07\x01");  // BYTE text stops at the NUL
    EXPECT_THROW((void)ifd.integer(*ifd.find(259), 3), std::out_of_range);
    EXPECT_THROW((void)ifd.number(*ifd.find(50721), 9), std::out_of_range);
}

// --- Damaged files: every truncation and every byte flipped --------------------------------------

namespace {

std::size_t value_size(TiffType type) {
    switch (type) {
        case TiffType::uint16: return 2;
        case TiffType::uint32:
        case TiffType::float32: return 4;
        case TiffType::rational:
        case TiffType::srational: return 8;
        default: return 1;
    }
}

// Parsing (and then reading every value of every entry) must either work or throw
// std::runtime_error: never another exception, and never a crash or out-of-bounds read (which the
// ASan build would report). A parsed entry must hold exactly count values, all inside the file.
void expect_parse_or_runtime_error(std::span<const std::byte> file, const std::string& what) {
    try {
        const TiffIfd ifd = parse_tiff(file);
        for (const TiffEntry& e : ifd.entries) {
            EXPECT_LE(e.data.data() + e.data.size(), file.data() + file.size()) << what;
            EXPECT_GE(e.data.data(), file.data()) << what;
            if (e.data.size() != std::uint64_t{e.count} * value_size(e.type)) {
                ADD_FAILURE() << what << ": tag " << e.tag << " has count " << e.count << " but "
                              << e.data.size() << " bytes of data";
                continue;  // don't read billions of missing values
            }
            for (std::uint32_t i = 0; i < e.count; ++i) {
                try {
                    (void)ifd.integer(e, i);
                } catch (const std::runtime_error&) {
                }
                try {
                    (void)ifd.number(e, i);
                } catch (const std::runtime_error&) {
                }
            }
            try {
                (void)ifd.string(e);
            } catch (const std::runtime_error&) {
            }
        }
    } catch (const std::runtime_error&) {
        // Expected for most damaged files.
    } catch (const std::exception& e) {
        ADD_FAILURE() << what << ": unexpected exception: " << e.what();
    } catch (...) {
        ADD_FAILURE() << what << ": unexpected non-standard exception";
    }
}

}  // namespace

TEST(TiffReader, SurvivesEveryTruncationAndEveryFlippedByte) {
    for (const bool big : {false, true}) {
        for (const std::uint16_t magic : {std::uint16_t{42}, std::uint16_t{0x4352}}) {
            for (const bool at_end : {false, true}) {
                TiffBuilder builder = sample_file(big, magic);
                if (at_end) {
                    builder.ifd_at_end();
                }
                const std::vector<std::byte> file = builder.build();
                const std::string name = std::string(big ? "MM" : "II") + "/" +
                                         std::to_string(magic) + (at_end ? "/ifd at end" : "");
                for (std::size_t len = 0; len <= file.size(); ++len) {
                    // A copy of just the prefix, so ASan sees reading past it as an overflow.
                    const std::vector<std::byte> prefix(
                        file.begin(), file.begin() + static_cast<std::ptrdiff_t>(len));
                    expect_parse_or_runtime_error(prefix,
                                                  name + " truncated to " + std::to_string(len));
                }
                for (std::size_t i = 0; i < file.size(); ++i) {
                    for (const std::uint8_t mask : {0xFF, 0x80, 0x01}) {
                        std::vector<std::byte> damaged = file;
                        damaged[i] ^= static_cast<std::byte>(mask);
                        expect_parse_or_runtime_error(damaged, name + " byte " + std::to_string(i) +
                                                                   " ^ " + std::to_string(mask));
                    }
                }
            }
        }
    }
}

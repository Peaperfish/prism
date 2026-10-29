#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "prism/histogram.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/pipeline.hpp"

namespace {

// Just above parallel_min_pixels, so stages really do split across threads. The height is prime,
// so it doesn't divide evenly by any thread count we test and chunks come out unequal.
constexpr int big_width = 1031;
constexpr int big_height = 1021;
static_assert(static_cast<std::size_t>(big_width) * big_height >= prism::parallel_min_pixels);

// Thread counts to compare against single-threaded. 1021 is prime, so none divide it evenly.
std::vector<int> thread_counts() {
    return {2, 3, 4, 7, 8, 16, prism::hardware_threads()};
}

// Deterministic pseudo-random bytes.
std::vector<std::uint8_t> make_bytes(std::size_t count) {
    std::vector<std::uint8_t> bytes(count);
    std::uint32_t state = 1;
    for (std::uint8_t& b : bytes) {
        state = state * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>(state >> 24);
    }
    return bytes;
}

// A linear image with values from -0.25 to 2.0 (as after exposure or a matrix), plus a few
// special values (NaN, infinity) scattered around, including in the last row.
prism::Image make_linear_image() {
    prism::Image img;
    img.width = big_width;
    img.height = big_height;
    const std::vector<std::uint8_t> bytes =
        make_bytes(static_cast<std::size_t>(big_width) * big_height * prism::Image::channels);
    img.pixels.reserve(bytes.size());
    for (std::uint8_t b : bytes) {
        img.pixels.push_back(-0.25f + 2.25f * static_cast<float>(b) / 255.0f);
    }
    for (std::size_t i = 0; i < img.pixels.size(); i += 99'991) {
        img.pixels[i] = std::numeric_limits<float>::quiet_NaN();
        img.pixels[img.pixels.size() - 1 - i] = std::numeric_limits<float>::infinity();
    }
    return img;
}

// True if both buffers hold exactly the same bits. Comparing floats with == would not do:
// NaN != NaN, and it would treat -0.0f and +0.0f as equal.
template <typename T>
bool same_bits(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
}

}  // namespace

// --- chunk_rows / parallel_for_rows ------------------------------------------------------------

TEST(ChunkRows, CoverEveryRowOnceWithNearEqualSizes) {
    for (int height : {0, 1, 2, 7, 10, 1021, 6336}) {
        for (int chunks = 1; chunks <= 17; ++chunks) {
            int expected_begin = 0;
            for (int c = 0; c < chunks; ++c) {
                const prism::RowRange r = prism::chunk_rows(height, chunks, c);
                ASSERT_EQ(r.begin, expected_begin) << height << " rows, chunk " << c;
                const int size = r.end - r.begin;
                EXPECT_GE(size, height / chunks);
                EXPECT_LE(size, height / chunks + 1);
                expected_begin = r.end;
            }
            EXPECT_EQ(expected_begin, height) << height << " rows in " << chunks << " chunks";
        }
    }
}

TEST(ParallelForRows, VisitsEveryRowExactlyOnce) {
    // Includes more threads than rows, and heights that don't divide evenly.
    for (int height : {1, 3, 10, 1021}) {
        for (int threads : {1, 2, 3, 8, 64}) {
            std::vector<int> visits(static_cast<std::size_t>(height), 0);
            // Each call writes only visits[begin..end), so no two threads touch the same element.
            prism::parallel_for_rows(height, threads, [&](int begin, int end) {
                for (int y = begin; y < end; ++y) {
                    ++visits[static_cast<std::size_t>(y)];
                }
            });
            EXPECT_EQ(visits, std::vector<int>(visits.size(), 1))
                << height << " rows, " << threads << " threads";
        }
    }
}

TEST(ParallelForRows, ZeroHeightCallsNothing) {
    bool called = false;
    prism::parallel_for_rows(0, 4, [&](int, int) { called = true; });
    EXPECT_FALSE(called);
}

TEST(ThreadsForPixels, SmallImagesStaySingleThreaded) {
    EXPECT_EQ(prism::threads_for_pixels(0, 8), 1);
    EXPECT_EQ(prism::threads_for_pixels(prism::parallel_min_pixels - 1, 8), 1);
    EXPECT_EQ(prism::threads_for_pixels(prism::parallel_min_pixels, 8), 8);
    EXPECT_EQ(prism::threads_for_pixels(prism::parallel_min_pixels, 1), 1);
    EXPECT_EQ(prism::threads_for_pixels(prism::parallel_min_pixels, 0), prism::hardware_threads());
    EXPECT_GE(prism::hardware_threads(), 1);
}

// --- every stage: multithreaded == single-threaded, bit for bit ---------------------------------

TEST(ParallelStages, DecodeMatchesSingleThreaded) {
    const std::vector<std::uint8_t> bytes =
        make_bytes(static_cast<std::size_t>(big_width) * big_height * prism::Image::channels);
    const prism::Image expected = prism::decode_srgb8(bytes, big_width, big_height, 1);
    for (int threads : thread_counts()) {
        const prism::Image img = prism::decode_srgb8(bytes, big_width, big_height, threads);
        EXPECT_TRUE(same_bits(img.pixels, expected.pixels)) << threads << " threads";
    }
}

TEST(ParallelStages, ExposureMatchesSingleThreaded) {
    prism::Image expected = make_linear_image();
    prism::exposure(expected, 0.7f, 1);
    for (int threads : thread_counts()) {
        prism::Image img = make_linear_image();
        prism::exposure(img, 0.7f, threads);
        EXPECT_TRUE(same_bits(img.pixels, expected.pixels)) << threads << " threads";
    }
}

TEST(ParallelStages, MatrixMatchesSingleThreaded) {
    const prism::Matrix3 m = {1.05f, 0.03f, -0.01f, 0.01f, 1.0f, 0.0f, -0.02f, -0.02f, 0.94f};
    prism::Image expected = make_linear_image();
    prism::apply_matrix(expected, m, 1);
    for (int threads : thread_counts()) {
        prism::Image img = make_linear_image();
        prism::apply_matrix(img, m, threads);
        EXPECT_TRUE(same_bits(img.pixels, expected.pixels)) << threads << " threads";
    }
}

TEST(ParallelStages, ToneCurvesMatchSingleThreaded) {
    for (const char* spec : {"gamma:2.2", "reinhard", "filmic"}) {
        const prism::ToneCurve curve = prism::parse_curve(spec);
        prism::Image expected = make_linear_image();
        prism::tone_curve(expected, curve, 1);
        for (int threads : thread_counts()) {
            prism::Image img = make_linear_image();
            prism::tone_curve(img, curve, threads);
            EXPECT_TRUE(same_bits(img.pixels, expected.pixels)) << spec << ", " << threads;
        }
    }
}

TEST(ParallelStages, EncodeMatchesSingleThreaded) {
    const prism::Image img = make_linear_image();
    const std::vector<std::uint8_t> expected = prism::encode_srgb8(img, 1);
    for (int threads : thread_counts()) {
        EXPECT_TRUE(same_bits(prism::encode_srgb8(img, threads), expected)) << threads;
    }
}

TEST(ParallelStages, HistogramMatchesSingleThreaded) {
    const prism::Image img = make_linear_image();
    const prism::Histogram expected = prism::compute_histogram(img, 1);
    ASSERT_EQ(expected.pixel_count, static_cast<std::uint64_t>(big_width) * big_height);
    for (int threads : thread_counts()) {
        const prism::Histogram h = prism::compute_histogram(img, threads);
        EXPECT_EQ(h.pixel_count, expected.pixel_count) << threads << " threads";
        EXPECT_EQ(h.red, expected.red) << threads << " threads";
        EXPECT_EQ(h.green, expected.green) << threads << " threads";
        EXPECT_EQ(h.blue, expected.blue) << threads << " threads";
        EXPECT_EQ(h.luma, expected.luma) << threads << " threads";
    }
}

TEST(ParallelStages, MismatchedBufferThrows) {
    // Threads index the buffer by row, so a wrong size must be refused, not read out of bounds.
    prism::Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.assign(10, 0.5f);
    EXPECT_THROW(prism::exposure(img, 1.0f), std::invalid_argument);
    EXPECT_THROW(prism::apply_matrix(img, prism::identity_matrix), std::invalid_argument);
    EXPECT_THROW(prism::tone_curve(img, prism::parse_curve("filmic")), std::invalid_argument);
    EXPECT_THROW((void)prism::encode_srgb8(img), std::invalid_argument);
    EXPECT_THROW((void)prism::compute_histogram(img), std::invalid_argument);
    EXPECT_THROW((void)prism::decode_srgb8(std::vector<std::uint8_t>(10), 4, 4),
                 std::invalid_argument);
}

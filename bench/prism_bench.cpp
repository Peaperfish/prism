// Times each pipeline stage on a large synthetic image held in memory (no file I/O, so we
// measure our own code, not PNG/JPEG compression or the disk).
//
// Usage: prism_bench [--size <width>x<height>] [--runs <n>] [--threads <n>]
//        prism_bench --exhaustive [--threads <n>]
// Defaults: 9504x6336 (a 61 MP Sony a7R V frame), 5 runs per stage, one thread per hardware
// thread. Every stage is timed single-threaded and with --threads threads; medians are reported.
//
// --exhaustive instead checks the fast linear_to_srgb8 against linear_to_srgb8_reference for every
// one of the 2^32 float bit patterns (NaNs, infinities, negatives and all), then exits.

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "prism/color.hpp"
#include "prism/histogram.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/pipeline.hpp"

namespace {

using Clock = std::chrono::steady_clock;  // monotonic: never jumps if the system clock is changed

struct Settings {
    int width = 9504;
    int height = 6336;
    int runs = 5;
    int threads = prism::hardware_threads();
    bool exhaustive = false;
};

[[noreturn]] void usage_error(const std::string& message) {
    std::cerr << "error: " << message << "\n"
              << "usage: prism_bench [--size <width>x<height>] [--runs <n>] [--threads <n>]\n"
              << "       prism_bench --exhaustive [--threads <n>]\n";
    std::exit(2);
}

int parse_positive(const std::string& text, const std::string& what) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || *end != '\0' || value <= 0 || value > 1'000'000) {
        usage_error(what + " needs a positive integer, got '" + text + "'");
    }
    return static_cast<int>(value);
}

Settings parse_args(int argc, char* argv[]) {
    Settings s;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--exhaustive") {
            s.exhaustive = true;
            continue;
        }
        if (i + 1 >= argc) {
            usage_error(std::string(arg) + " needs a value");
        }
        const std::string value = argv[++i];
        if (arg == "--runs") {
            s.runs = parse_positive(value, "--runs");
        } else if (arg == "--threads") {
            s.threads = parse_positive(value, "--threads");
        } else if (arg == "--size") {
            const std::size_t x = value.find('x');
            if (x == std::string::npos) {
                usage_error("--size needs <width>x<height>, got '" + value + "'");
            }
            s.width = parse_positive(value.substr(0, x), "--size width");
            s.height = parse_positive(value.substr(x + 1), "--size height");
        } else {
            usage_error("unexpected argument '" + std::string(arg) + "'");
        }
    }
    return s;
}

// Photo-like 8-bit sRGB content: smooth gradients plus a little deterministic noise. Pure random
// bytes would make every branch in the colour code unpredictable, which real photos are not.
std::vector<std::uint8_t> make_synthetic_srgb8(int width, int height) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 3);
    std::uint32_t state = 12345;
    std::size_t i = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int gradients[3] = {x * 255 / width, y * 255 / height,
                                      (x + y) * 255 / (width + height)};
            for (int g : gradients) {
                state = state * 1664525u + 1013904223u;  // tiny LCG, plenty for test noise
                const int noise = static_cast<int>(state >> 28) - 8;  // -8 .. +7
                bytes[i++] = static_cast<std::uint8_t>(std::clamp(g + noise, 0, 255));
            }
        }
    }
    return bytes;
}

// Runs `setup` (untimed) then `stage(threads)` (timed) `runs` times; returns the median in ms.
// The median ignores one-off outliers, such as the first run paying for page faults.
double median_ms(int runs, int threads, const std::function<void()>& setup,
                 const std::function<void(int threads)>& stage) {
    std::vector<double> times;
    for (int r = 0; r < runs; ++r) {
        setup();
        const auto start = Clock::now();
        stage(threads);
        times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

// Times one stage single-threaded and with s.threads threads, and prints a table row.
void bench_stage(const Settings& s, std::string_view name, double bytes_touched,
                 const std::function<void()>& setup,
                 const std::function<void(int threads)>& stage) {
    const double one = median_ms(s.runs, 1, setup, stage);
    const double many = median_ms(s.runs, s.threads, setup, stage);
    // Effective memory traffic: bytes read + written, divided by time. Comparing this with the
    // machine's memory bandwidth tells us whether a stage is limited by maths or by memory.
    const auto gb_per_s = [&](double ms) { return bytes_touched / (ms / 1000.0) / 1e9; };
    std::cout << "  " << std::left << std::setw(14) << name << std::right << std::fixed
              << std::setprecision(1) << std::setw(10) << one << std::setw(10) << many
              << std::setw(9) << one / many << "x" << std::setw(10) << gb_per_s(one)
              << std::setw(10) << gb_per_s(many) << '\n';
}

// Compares linear_to_srgb8 (single value and batch forms) with the pow-based reference for every
// float bit pattern. Returns true if there is no difference at all.
bool check_encode_exhaustively(int threads) {
    constexpr std::uint64_t total = std::uint64_t{1} << 32;
    constexpr int chunks = 1024;
    constexpr std::uint64_t per_chunk = total / chunks;
    constexpr std::size_t block = 4096;
    std::vector<std::uint64_t> mismatches(chunks, 0);
    std::vector<std::uint32_t> first_bad(chunks, 0);

    const auto start = Clock::now();
    std::atomic<int> next{0};
    prism::parallel_for(threads, [&](int) {
        std::array<float, block> in;
        std::array<std::uint8_t, block> out;
        for (int c = next++; c < chunks; c = next++) {
            for (std::uint64_t base = c * per_chunk; base < (c + 1) * per_chunk; base += block) {
                for (std::size_t i = 0; i < block; ++i) {
                    in[i] = std::bit_cast<float>(static_cast<std::uint32_t>(base + i));
                }
                prism::linear_to_srgb8(in.data(), out.data(), block);
                for (std::size_t i = 0; i < block; ++i) {
                    const std::uint8_t expected = prism::linear_to_srgb8_reference(in[i]);
                    if (out[i] != expected || prism::linear_to_srgb8(in[i]) != expected) {
                        if (mismatches[c]++ == 0) {
                            first_bad[c] = static_cast<std::uint32_t>(base + i);
                        }
                    }
                }
            }
        }
    });
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();

    std::uint64_t bad = 0;
    for (int c = 0; c < chunks; ++c) {
        bad += mismatches[c];
        if (mismatches[c] != 0) {
            std::cout << "  mismatch at bits 0x" << std::hex << first_bad[c] << std::dec << '\n';
        }
    }
    std::cout << "linear_to_srgb8 vs reference: " << total << " floats checked in " << std::fixed
              << std::setprecision(1) << seconds << " s, " << bad << " mismatches\n";
    return bad == 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    const Settings s = parse_args(argc, argv);
    if (s.exhaustive) {
        return check_encode_exhaustively(s.threads) ? 0 : 1;
    }
    const std::size_t samples = static_cast<std::size_t>(s.width) * s.height * 3;
    const double f32 = static_cast<double>(samples) * sizeof(float);  // one float image, in bytes
    const double u8 = static_cast<double>(samples);                    // one 8-bit image, in bytes

    std::cout << "prism_bench: " << s.width << "x" << s.height << " ("
              << std::setprecision(1) << std::fixed << s.width * static_cast<double>(s.height) / 1e6
              << " MP), median of " << s.runs << " runs\n\n"
              << "  " << std::left << std::setw(14) << "stage" << std::right << std::setw(10)
              << "1 thr ms" << std::setw(10) << (std::to_string(s.threads) + " thr ms")
              << std::setw(10) << "speedup" << std::setw(10) << "1 GB/s" << std::setw(10)
              << (std::to_string(s.threads) + " GB/s") << '\n';

    const std::vector<std::uint8_t> srgb = make_synthetic_srgb8(s.width, s.height);

    // Each stage runs on the previous stage's output, as in the real pipeline. In-place stages
    // start every run from a fresh copy of their input (copied outside the timed region), so
    // each run does exactly the same work.
    prism::Image input;
    prism::Image work;
    const auto reset = [&] { work = input; };
    const auto advance = [&] { input = work; };

    const auto none = [] {};

    bench_stage(s, "sRGB decode", u8 + f32, none,
                [&](int t) { work = prism::decode_srgb8(srgb, s.width, s.height, t); });
    advance();

    bench_stage(s, "exposure", 2 * f32, reset, [&](int t) { prism::exposure(work, 0.5f, t); });
    advance();

    // samples/grade.txt, inlined so the benchmark does not depend on the working directory.
    const prism::Matrix3 grade = {1.05f,  0.03f,  -0.01f,  //
                                  0.01f,  1.00f,  0.00f,   //
                                  -0.02f, -0.02f, 0.94f};
    bench_stage(s, "matrix", 2 * f32, reset, [&](int t) { prism::apply_matrix(work, grade, t); });
    advance();

    const prism::ToneCurve filmic = prism::parse_curve("filmic");
    bench_stage(s, "curve (filmic)", 2 * f32, reset,
                [&](int t) { prism::tone_curve(work, filmic, t); });
    advance();

    std::vector<std::uint8_t> encoded;
    bench_stage(s, "sRGB encode", f32 + u8, none,
                [&](int t) { encoded = prism::encode_srgb8(input, t); });

    prism::Histogram hist;
    bench_stage(s, "histogram", f32, none,
                [&](int t) { hist = prism::compute_histogram(input, t); });

    // Use the results so the optimiser cannot decide the work is pointless and delete it.
    if (encoded.empty() || hist.pixel_count != samples / 3) {
        std::cerr << "unexpected result\n";
        return 1;
    }
    return 0;
}

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "prism/histogram.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"
#include "prism/raw.hpp"
#include "prism/utf8_path.hpp"
#include "prism/version.hpp"

namespace {

void print_usage(const char* program) {
    std::cerr << "prism " << prism::version() << " - command-line photo tool\n"
              << "\n"
              << "Usage: " << program << " <input> [-o <output>] [--histogram <file>] [options]\n"
              << "\n"
              << "Loads <input> (PNG, JPEG or camera RAW: .arw .dng .cr2 .cr3 .nef .raf) and\n"
              << "writes <output> (.png, .jpg or .jpeg).\n"
              << "At least one of -o and --histogram is required.\n"
              << "\n"
              << "RAW input:\n"
              << "  --raw-half                  decode at half size (much faster; for previews)\n"
              << "  --raw-camera-matrix <file>  convert camera RGB to linear sRGB with this 3x3\n"
              << "                              matrix instead of LibRaw's built-in one\n"
              << "\n"
              << "Options (always applied in this order, whatever order you type them):\n"
              << "  --exposure <stops>  brighten (+) or darken (-) by this many stops\n"
              << "  --matrix <file>     apply a 3x3 colour matrix (9 numbers, row-major)\n"
              << "  --saturation <s>    scale colourfulness around each pixel's luminance\n"
              << "                      (1 = unchanged, 0 = greyscale; 0 to 10)\n"
              << "  --contrast <c>      S-curve around middle grey, in stops (1 = unchanged;\n"
              << "                      above 1 more contrast, below 1 less; 0.1 to 10)\n"
              << "  --curve <spec>      apply a tone curve:";
    for (std::string_view spec : prism::curve_specs()) {
        std::cerr << ' ' << spec;
    }
    std::cerr << "\n  --histogram <file>  save a histogram image of the result and print\n"
              << "                      the percentage of clipped pixels per channel\n"
              << "  --threads <n>       use up to n threads (default: " << prism::hardware_threads()
              << ", one per hardware thread;\n"
              << "                      1 = single-threaded; images under ~1 MP always use 1)\n"
              << "  --time              print how long each stage took\n"
              << "  -h, --help          show this help\n"
              << "  --version           print the version and exit\n";
}

// Raised for bad command-line input, so main() can also print the usage text.
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// File names stay as the text from argv (UTF-8: on Windows the manifest asks for it, see
// src/CMakeLists.txt) and become paths with prism::path_from_utf8 where they are opened.
struct Options {
    std::string input;
    std::optional<std::string> output;
    std::optional<float> exposure_stops;
    std::optional<std::string> matrix_path;
    std::optional<float> saturation;
    std::optional<float> contrast;
    std::optional<std::string> curve_spec;
    std::optional<std::string> histogram_path;
    bool raw_half = false;
    std::optional<std::string> raw_camera_matrix_path;
    std::optional<int> threads;  // unset = automatic (one per hardware thread)
    bool time = false;
};

float parse_stops(const std::string& text) {
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || errno == ERANGE ||
        !std::isfinite(value)) {
        throw UsageError("--exposure needs a number, got '" + text + "'");
    }
    return static_cast<float>(value);
}

// A number for `flag` within [min, max]. The upper limits only catch typos such as 125 for 1.25.
float parse_ranged(const std::string& text, std::string_view flag, double min, double max) {
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || errno == ERANGE ||
        !std::isfinite(value) || value < min || value > max) {
        std::ostringstream message;
        message << flag << " needs a number from " << min << " to " << max << ", got '" << text
                << "'";
        throw UsageError(message.str());
    }
    return static_cast<float>(value);
}

int parse_threads(const std::string& text) {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text.c_str(), &end, 10);
    // The upper limit only catches typos; far more threads than cores would just be slower.
    if (text.empty() || *end != '\0' || errno == ERANGE || value < 1 || value > 1024) {
        throw UsageError("--threads needs a whole number from 1 to 1024, got '" + text + "'");
    }
    return static_cast<int>(value);
}

// Stores `value` into `slot`, rejecting a flag given twice (which is almost always a mistake).
template <typename T>
void set_once(std::optional<T>& slot, std::string_view flag, T value) {
    if (slot) {
        throw UsageError(std::string(flag) + " given more than once");
    }
    slot = std::move(value);
}

Options parse_args(int argc, char* argv[]) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        // Every flag except --help, --version and --time takes the next argument as its value. We
        // take it as-is, even if it starts with '-', so "--exposure -1" works.
        auto next_value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw UsageError(std::string(arg) + " needs a value");
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--version") {
            std::cout << "prism " << prism::version() << '\n';
            std::exit(0);
        } else if (arg == "-o") {
            set_once(opts.output, arg, next_value());
        } else if (arg == "--exposure") {
            set_once(opts.exposure_stops, arg, parse_stops(next_value()));
        } else if (arg == "--matrix") {
            set_once(opts.matrix_path, arg, next_value());
        } else if (arg == "--saturation") {
            set_once(opts.saturation, arg, parse_ranged(next_value(), arg, 0.0, 10.0));
        } else if (arg == "--contrast") {
            set_once(opts.contrast, arg, parse_ranged(next_value(), arg, 0.1, 10.0));
        } else if (arg == "--curve") {
            set_once(opts.curve_spec, arg, next_value());
        } else if (arg == "--histogram") {
            set_once(opts.histogram_path, arg, next_value());
        } else if (arg == "--threads") {
            set_once(opts.threads, arg, parse_threads(next_value()));
        } else if (arg == "--time") {
            opts.time = true;
        } else if (arg == "--raw-half") {
            opts.raw_half = true;
        } else if (arg == "--raw-camera-matrix") {
            set_once(opts.raw_camera_matrix_path, arg, next_value());
        } else if (arg.starts_with('-') || !opts.input.empty()) {
            throw UsageError("unexpected argument '" + std::string(arg) + "'");
        } else {
            opts.input = arg;
        }
    }
    if (opts.input.empty()) {
        throw UsageError("no input file given");
    }
    // A histogram on its own is a useful result (e.g. checking clipping before exporting),
    // but with neither there would be nothing to show for the work.
    if (!opts.output && !opts.histogram_path) {
        throw UsageError("no output given (use -o <output>, --histogram <file>, or both)");
    }
    // Silently ignoring these on a PNG or JPEG would hide a mistake such as the wrong input file.
    if ((opts.raw_half || opts.raw_camera_matrix_path) &&
        !prism::is_raw_file(prism::path_from_utf8(opts.input))) {
        throw UsageError(std::string(opts.raw_half ? "--raw-half" : "--raw-camera-matrix") +
                         " only applies to RAW input (.arw, .dng, .cr2, .cr3, .nef, .raf)");
    }
    return opts;
}

// Prints e.g. "clipped (0 / 255):  R 0.00% / 1.25%  G 0.00% / 0.80%  B 0.10% / 0.00%".
void print_clipping(const prism::Histogram& h) {
    const struct {
        char name;
        const prism::Histogram::Counts& counts;
    } channels[] = {{'R', h.red}, {'G', h.green}, {'B', h.blue}};

    std::cout << "clipped (0 / 255):" << std::fixed << std::setprecision(2);
    for (const auto& c : channels) {
        std::cout << "  " << c.name << ' ' << prism::bin_percent(h, c.counts, 0) << "% / "
                  << prism::bin_percent(h, c.counts, 255) << '%';
    }
    std::cout << '\n';
}

// Records how long each stage of a run took, for --time.
class StageTimer {
public:
    // Times fn() and records it under `name`.
    template <typename Fn>
    void run(std::string name, Fn&& fn) {
        const auto start = Clock::now();
        std::forward<Fn>(fn)();
        const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
        add(std::move(name), elapsed.count());
    }

    // Records a stage that was timed elsewhere (by prism::process).
    void add(std::string name, double ms) { stages_.push_back({std::move(name), ms}); }

    void print(int threads) const {
        double total = 0;
        std::cout << "time (" << threads << (threads == 1 ? " thread" : " threads") << "):\n"
                  << std::fixed << std::setprecision(1);
        for (const Stage& s : stages_) {
            std::cout << "  " << std::left << std::setw(10) << s.name << std::right << std::setw(9)
                      << s.ms << " ms\n";
            total += s.ms;
        }
        std::cout << "  " << std::left << std::setw(10) << "total" << std::right << std::setw(9)
                  << total << " ms\n";
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Stage {
        std::string name;
        double ms;
    };
    std::vector<Stage> stages_;
};

}  // namespace

int main(int argc, char* argv[]) {
    const char* program = (argc > 0 && argv[0] != nullptr) ? argv[0] : "prism";

    try {
        const Options opts = parse_args(argc, argv);

        // Read the matrix and curve before the image: they are quick to check, so a typo
        // fails immediately instead of after decoding a large photo.
        prism::ProcessSettings settings;
        settings.exposure_stops = opts.exposure_stops;
        if (opts.matrix_path) {
            settings.matrix = prism::load_matrix(prism::path_from_utf8(*opts.matrix_path));
        }
        settings.saturation = opts.saturation;
        settings.contrast = opts.contrast;
        prism::RawOptions raw;
        raw.half_size = opts.raw_half;
        if (opts.raw_camera_matrix_path) {
            raw.camera_matrix =
                prism::load_matrix(prism::path_from_utf8(*opts.raw_camera_matrix_path));
        }
        if (opts.curve_spec) {
            settings.curve = prism::parse_curve(*opts.curve_spec);
        }

        // 0 asks the library for one thread per hardware thread.
        const int threads = opts.threads.value_or(0);
        StageTimer timer;

        prism::Image img;
        timer.run("load", [&] {
            img = prism::load_image(prism::path_from_utf8(opts.input), raw, threads);
        });
        // process() applies the stages in the README's order and skips those left at their
        // neutral value. Without a stop token it always runs to the end, so the result is always
        // ProcessResult::completed.
        (void)prism::process(img, settings, threads, {}, [&](const prism::StageTime& t) {
            timer.add(std::string(t.name), t.ms);
        });
        // With both -o and --histogram the image is converted to 8-bit sRGB once: the saved bytes
        // are also what the histogram counts. (Without -o the histogram encodes block by block.)
        std::vector<std::uint8_t> bytes;
        if (opts.output) {
            timer.run("save", [&] {
                if (opts.histogram_path) {
                    bytes = prism::encode_srgb8(img, threads);
                    prism::save_srgb8(prism::path_from_utf8(*opts.output), bytes, img.width,
                                      img.height);
                } else {
                    prism::save_image(prism::path_from_utf8(*opts.output), img, threads);
                }
            });
            std::cout << "wrote " << *opts.output << " (" << img.width << "x" << img.height
                      << ")\n";
        }

        if (opts.histogram_path) {
            // Measured after the whole pipeline, on the same 8-bit values save_image writes,
            // whether or not -o was given.
            prism::Histogram hist;
            timer.run("histogram", [&] {
                hist = bytes.empty()
                           ? prism::compute_histogram(img, threads)
                           : prism::compute_histogram(bytes, img.width, img.height, threads);
                prism::save_image(prism::path_from_utf8(*opts.histogram_path),
                                  prism::render_histogram(hist), threads);
            });
            std::cout << "wrote " << *opts.histogram_path << " (histogram)\n";
            print_clipping(hist);
        }

        if (opts.time) {
            // Report what was actually used: small images run single-threaded whatever was asked.
            timer.print(prism::threads_for_pixels(
                static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height),
                threads));
        }
    } catch (const UsageError& e) {
        std::cerr << "error: " << e.what() << "\n\n";
        print_usage(program);
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

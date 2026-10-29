#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "prism/histogram.hpp"
#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/version.hpp"

namespace {

void print_usage(const char* program) {
    std::cerr << "prism " << prism::version() << " - command-line photo tool\n"
              << "\n"
              << "Usage: " << program << " <input> [-o <output>] [--histogram <file>] [options]\n"
              << "\n"
              << "Loads <input> (PNG or JPEG) and writes <output> (.png, .jpg or .jpeg).\n"
              << "At least one of -o and --histogram is required.\n"
              << "\n"
              << "Options (always applied in this order, whatever order you type them):\n"
              << "  --exposure <stops>  brighten (+) or darken (-) by this many stops\n"
              << "  --matrix <file>     apply a 3x3 colour matrix (9 numbers, row-major)\n"
              << "  --curve <spec>      apply a tone curve:";
    for (std::string_view spec : prism::curve_specs()) {
        std::cerr << ' ' << spec;
    }
    std::cerr << "\n  --histogram <file>  save a histogram image of the result and print\n"
              << "                      the percentage of clipped pixels per channel\n"
              << "  -h, --help          show this help\n";
}

// Raised for bad command-line input, so main() can also print the usage text.
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Options {
    std::string input;
    std::optional<std::string> output;
    std::optional<float> exposure_stops;
    std::optional<std::string> matrix_path;
    std::optional<std::string> curve_spec;
    std::optional<std::string> histogram_path;
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

        // Every flag except --help takes the next argument as its value. We take it as-is,
        // even if it starts with '-', so "--exposure -1" works.
        auto next_value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw UsageError(std::string(arg) + " needs a value");
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "-o") {
            set_once(opts.output, arg, next_value());
        } else if (arg == "--exposure") {
            set_once(opts.exposure_stops, arg, parse_stops(next_value()));
        } else if (arg == "--matrix") {
            set_once(opts.matrix_path, arg, next_value());
        } else if (arg == "--curve") {
            set_once(opts.curve_spec, arg, next_value());
        } else if (arg == "--histogram") {
            set_once(opts.histogram_path, arg, next_value());
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

}  // namespace

int main(int argc, char* argv[]) {
    const char* program = (argc > 0 && argv[0] != nullptr) ? argv[0] : "prism";

    try {
        const Options opts = parse_args(argc, argv);

        // Read the matrix and curve before the image: they are quick to check, so a typo
        // fails immediately instead of after decoding a large photo.
        std::optional<prism::Matrix3> matrix;
        if (opts.matrix_path) {
            matrix = prism::load_matrix(*opts.matrix_path);
        }
        std::optional<prism::ToneCurve> curve;
        if (opts.curve_spec) {
            curve = prism::parse_curve(*opts.curve_spec);
        }

        // Pipeline order from the README: exposure -> colour matrix -> tone curve.
        prism::Image img = prism::load_image(opts.input);
        if (opts.exposure_stops) {
            prism::exposure(img, *opts.exposure_stops);
        }
        if (matrix) {
            prism::apply_matrix(img, *matrix);
        }
        if (curve) {
            prism::tone_curve(img, *curve);
        }
        if (opts.output) {
            prism::save_image(*opts.output, img);
            std::cout << "wrote " << *opts.output << " (" << img.width << "x" << img.height
                      << ")\n";
        }

        if (opts.histogram_path) {
            // Measured after the whole pipeline, on the same 8-bit values save_image writes,
            // whether or not -o was given.
            const prism::Histogram hist = prism::compute_histogram(img);
            prism::save_image(*opts.histogram_path, prism::render_histogram(hist));
            std::cout << "wrote " << *opts.histogram_path << " (histogram)\n";
            print_clipping(hist);
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

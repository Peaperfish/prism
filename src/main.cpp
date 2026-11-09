#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
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
#include "prism/ranges.hpp"
#include "prism/raw.hpp"
#include "prism/recipe.hpp"
#include "prism/resize.hpp"
#include "prism/utf8_path.hpp"
#include "prism/version.hpp"
#include "prism/white_balance.hpp"

namespace {

void print_usage(const char* program) {
    std::cerr << "prism " << prism::version() << " - command-line photo tool\n"
              << "\n"
              << "Usage: " << program
              << " <input>... [-o <output> | --out-dir <dir>] [--histogram <file>] [options]\n"
              << "\n"
              << "Loads <input> (PNG, JPEG, camera RAW: .arw .dng .cr2 .cr3 .nef .raf, or on\n"
              << "macOS HEIC: .heic .heif) and writes <output> (.png, .jpg or .jpeg).\n"
              << "At least one of -o, --out-dir and --histogram is required.\n"
              << "\n"
              << "Several photos:\n"
              << "  --out-dir <dir>     write each result into this existing folder, as\n"
              << "                      <input name>_prism.jpg (<input name>_prism_<n>.jpg with\n"
              << "                      --long-side <n>); needed for more than one input\n"
              << "  --format jpg|png    what --out-dir writes: JPEG (the default) or PNG\n"
              << "\n"
              << "Size:\n"
              << "  --long-side <pixels>  shrink the photo right after loading so its longer side\n"
              << "                        is this many pixels (smaller only: a photo that is not\n"
              << "                        larger keeps its size); works for RAW too\n"
              << "\n"
              << "RAW input:\n"
              << "  --raw-half                  decode at half size (much faster; for previews)\n"
              << "  --raw-camera-matrix <file>  convert camera RGB to linear sRGB with this 3x3\n"
              << "                              matrix instead of LibRaw's built-in one\n"
              << "\n"
              << "Options (always applied in this order, whatever order you type them):\n"
              << "  --recipe <file>     apply a recipe (.prism file); each option below that you\n"
              << "                      also give replaces that stage of the recipe\n"
              << "  --exposure <stops>  brighten (+) or darken (-) by this many stops\n"
              << "  --kelvin <K>        the light the photo was lit by, 2000 to 15000: higher is\n"
              << "                      warmer. For RAW the edit is relative to the camera's as-shot\n"
              << "                      light; for other photos 6500 changes nothing\n"
              << "  --tint <n>          green-magenta tint of that light, -100 to 100: positive (a\n"
              << "                      greener light) is more magenta; for photos without an as-shot\n"
              << "                      light 0 changes nothing. Either of --kelvin and --tint alone\n"
              << "                      keeps the other as shot\n"
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
    std::vector<std::string> inputs;
    std::optional<std::string> output;
    std::optional<std::string> out_dir;
    std::optional<std::string> format;  // "jpg" or "png", for --out-dir
    std::optional<std::string> recipe_path;
    std::optional<float> exposure_stops;
    std::optional<float> kelvin;
    std::optional<float> tint;
    std::optional<std::string> matrix_path;
    std::optional<float> saturation;
    std::optional<float> contrast;
    std::optional<std::string> curve_spec;
    std::optional<std::string> histogram_path;
    bool raw_half = false;
    std::optional<std::string> raw_camera_matrix_path;
    std::optional<int> long_side;  // pixels, at least 1; unset = keep the size
    std::optional<int> threads;  // unset = automatic (one per hardware thread)
    bool time = false;

    // Worked out by parse_args from the above: what to write for each input, in the same order.
    // With one input and no --out-dir, `output` as given.
    std::vector<std::optional<std::string>> outputs;
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

// --long-side: a whole number of pixels, at least 1, written in plain digits.
int parse_long_side(const std::string& text) {
    const bool digits = !text.empty() && text.find_first_not_of("0123456789") == std::string::npos;
    char* end = nullptr;
    errno = 0;
    const long long value = digits ? std::strtoll(text.c_str(), &end, 10) : 0;
    if (!digits || errno == ERANGE || value < 1 || value > std::numeric_limits<int>::max()) {
        throw UsageError("--long-side needs a whole number of pixels, at least 1, got '" + text +
                         "'");
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

// Lower-case ASCII letters, for comparing file names the way macOS and Windows file systems do.
std::string ascii_lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

// The files --out-dir writes, one per input: <dir>/<input name>_prism<extension>, the name the
// app's Export suggests; with --long-side <n> it is <input name>_prism_<n><extension> for every
// input, including those too small to be reduced, so the names can be known in advance. Checks the whole batch before anything runs: the folder must exist, and
// no two inputs may give the same name (they would overwrite each other).
std::vector<std::optional<std::string>> out_dir_outputs(const std::string& dir,
                                                        const std::vector<std::string>& inputs,
                                                        const std::string& extension,
                                                        std::optional<int> long_side) {
    const std::filesystem::path folder = prism::path_from_utf8(dir);
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) {
        throw UsageError("--out-dir '" + dir + "' does not exist (create the folder first)");
    }
    if (!std::filesystem::is_directory(folder, ec)) {
        throw UsageError("--out-dir '" + dir + "' is not a folder");
    }
    std::vector<std::optional<std::string>> outputs;
    std::map<std::string, std::size_t> by_name;  // lower-case output name -> index of its input
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        std::filesystem::path name = prism::path_from_utf8(inputs[i]).stem();
        name += "_prism";
        if (long_side) {
            name += "_" + std::to_string(*long_side);
        }
        name += extension;
        const auto [it, added] = by_name.emplace(ascii_lower(prism::utf8(name)), i);
        if (!added) {
            throw UsageError("'" + inputs[it->second] + "' and '" + inputs[i] +
                             "' would both be written as '" + prism::utf8(name) + "' in '" + dir +
                             "'");
        }
        outputs.emplace_back(prism::utf8(folder / name));
    }
    return outputs;
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
        } else if (arg == "--out-dir") {
            set_once(opts.out_dir, arg, next_value());
        } else if (arg == "--format") {
            std::string format = next_value();
            if (format != "jpg" && format != "png") {
                throw UsageError("--format needs jpg or png, got '" + format + "'");
            }
            set_once(opts.format, arg, std::move(format));
        } else if (arg == "--recipe") {
            set_once(opts.recipe_path, arg, next_value());
        } else if (arg == "--exposure") {
            set_once(opts.exposure_stops, arg, parse_stops(next_value()));
        } else if (arg == "--kelvin") {
            set_once(opts.kelvin, arg,
                     parse_ranged(next_value(), arg, prism::kelvin_range.min,
                                  prism::kelvin_range.max));
        } else if (arg == "--tint") {
            set_once(opts.tint, arg,
                     parse_ranged(next_value(), arg, prism::tint_range.min, prism::tint_range.max));
        } else if (arg == "--matrix") {
            set_once(opts.matrix_path, arg, next_value());
        } else if (arg == "--saturation") {
            set_once(opts.saturation, arg,
                     parse_ranged(next_value(), arg, prism::saturation_range.min,
                                  prism::saturation_range.max));
        } else if (arg == "--contrast") {
            set_once(opts.contrast, arg,
                     parse_ranged(next_value(), arg, prism::contrast_range.min,
                                  prism::contrast_range.max));
        } else if (arg == "--curve") {
            set_once(opts.curve_spec, arg, next_value());
        } else if (arg == "--histogram") {
            set_once(opts.histogram_path, arg, next_value());
        } else if (arg == "--long-side") {
            set_once(opts.long_side, arg, parse_long_side(next_value()));
        } else if (arg == "--threads") {
            set_once(opts.threads, arg, parse_threads(next_value()));
        } else if (arg == "--time") {
            opts.time = true;
        } else if (arg == "--raw-half") {
            opts.raw_half = true;
        } else if (arg == "--raw-camera-matrix") {
            set_once(opts.raw_camera_matrix_path, arg, next_value());
        } else if (arg.starts_with('-')) {
            throw UsageError("unexpected argument '" + std::string(arg) + "'");
        } else {
            opts.inputs.emplace_back(arg);
        }
    }
    if (opts.inputs.empty()) {
        throw UsageError("no input file given");
    }
    const bool several = opts.inputs.size() > 1;
    if (several && opts.output) {
        throw UsageError("-o names one output file, so it takes a single input; for " +
                         std::to_string(opts.inputs.size()) +
                         " inputs, use --out-dir <dir> instead");
    }
    if (several && opts.histogram_path) {
        throw UsageError("--histogram writes one histogram file, so it takes a single input");
    }
    if (several && !opts.out_dir) {
        throw UsageError(std::to_string(opts.inputs.size()) +
                         " inputs need --out-dir <dir>, the folder to write the results to");
    }
    if (opts.output && opts.out_dir) {
        throw UsageError("-o and --out-dir can't be used together: give one or the other");
    }
    // A histogram on its own is a useful result (e.g. checking clipping before exporting),
    // but with neither there would be nothing to show for the work.
    if (!opts.output && !opts.histogram_path && !opts.out_dir) {
        throw UsageError("no output given (use -o <output>, --histogram <file>, or both)");
    }
    // Silently ignoring these on a PNG or JPEG would hide a mistake such as the wrong input file.
    if (opts.raw_half || opts.raw_camera_matrix_path) {
        for (const std::string& input : opts.inputs) {
            if (!prism::is_raw_file(prism::path_from_utf8(input))) {
                throw UsageError(
                    std::string(opts.raw_half ? "--raw-half" : "--raw-camera-matrix") +
                    " only applies to RAW input (.arw, .dng, .cr2, .cr3, .nef, .raf)" +
                    (several ? ", and '" + input + "' is not a RAW file" : std::string()));
            }
        }
    }
    // With -o the file name's extension already says the format; a second say would only
    // contradict it.
    if (opts.format && !opts.out_dir) {
        throw UsageError(
            "--format only applies to --out-dir (with -o, the extension of the file "
            "name chooses the format)");
    }
    if (opts.out_dir) {
        opts.outputs = out_dir_outputs(*opts.out_dir, opts.inputs,
                                       "." + opts.format.value_or("jpg"), opts.long_side);
    } else {
        opts.outputs = {opts.output};
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
            std::cout << "  " << std::left << std::setw(14) << s.name << std::right << std::setw(9)
                      << s.ms << " ms\n";
            total += s.ms;
        }
        std::cout << "  " << std::left << std::setw(14) << "total" << std::right << std::setw(9)
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

// --kelvin and --tint, as given. The matrix they make depends on the photo's as-shot light, so it
// is built once the photo is loaded.
struct WhiteBalanceFlags {
    std::optional<float> kelvin;
    std::optional<float> tint;
};

// `flags` plus what --kelvin and --tint ask of a photo whose as-shot light is `as_shot_white`
// (none: 6500 K, tint 0). Either of the two given alone takes the other from the photo's as-shot
// value. Like the other flags, they replace the recipe's stage, so the matrix goes into the flags.
prism::ProcessSettings with_white_balance(prism::ProcessSettings flags,
                                          const WhiteBalanceFlags& wb,
                                          const std::optional<prism::Chromaticity>& as_shot_white) {
    if (!wb.kelvin && !wb.tint) {
        return flags;
    }
    const prism::Chromaticity shot_white =
        as_shot_white.value_or(prism::white_of(prism::default_as_shot));
    prism::WhiteBalance shot = prism::default_as_shot;
    if (as_shot_white) {
        // The editing ranges are narrower than the locus; the matrix still uses the true white.
        shot = prism::white_balance_of(shot_white);
        shot.kelvin = std::clamp(shot.kelvin, prism::kelvin_range.min, prism::kelvin_range.max);
        shot.tint = std::clamp(shot.tint, prism::tint_range.min, prism::tint_range.max);
    }
    const prism::WhiteBalance chosen{wb.kelvin ? static_cast<double>(*wb.kelvin) : shot.kelvin,
                                     wb.tint ? static_cast<double>(*wb.tint) : shot.tint};
    flags.white_balance = prism::white_balance_matrix(chosen, shot_white);
    return flags;
}

// Loads one photo, runs the recipe and flags on it and writes what was asked for: the image to
// `output` and the histogram to `histogram_path`, each when given. File names are UTF-8 text,
// printed as they are.
void run_one(const std::string& input, const std::optional<std::string>& output,
             const std::optional<std::string>& histogram_path, bool time,
             const prism::Recipe& recipe, const prism::ProcessSettings& flags,
             const WhiteBalanceFlags& wb_flags, const prism::RawOptions& raw,
             std::optional<int> long_side, int threads) {
    StageTimer timer;

    prism::Image img;
    prism::PhotoInfo info;
    timer.run("load", [&] {
        prism::Photo photo = prism::load_photo(prism::path_from_utf8(input), raw, threads);
        img = std::move(photo.image);
        info = std::move(photo.info);
    });
    std::optional<prism::Chromaticity> as_shot_white;
    if (info.as_shot_white_xy) {
        as_shot_white =
            prism::Chromaticity{(*info.as_shot_white_xy)[0], (*info.as_shot_white_xy)[1]};
    }
    const prism::ProcessSettings settings = prism::merge_recipe_and_flags(
        recipe, with_white_balance(flags, wb_flags, as_shot_white), as_shot_white);
    // Reduce before any editing, so every stage works on the small image. A photo that is not
    // larger keeps its size (downscale never enlarges), and that is said, since the user asked for
    // a size and gets another.
    if (long_side) {
        const int width = img.width;
        const int height = img.height;
        const bool larger = std::max(width, height) > *long_side;
        timer.run("resize", [&] {
            if (larger) {
                img = prism::downscale(img, *long_side, threads);
            }
        });
        if (!larger) {
            std::cerr << "note: " << input << " is " << width << 'x' << height
                      << ", not larger than --long-side " << *long_side << "; kept its size\n";
        }
    }
    // process() applies the stages in the README's order and skips those left at their
    // neutral value. Without a stop token it always runs to the end, so the result is always
    // ProcessResult::completed.
    (void)prism::process(img, settings, threads, {},
                         [&](const prism::StageTime& t) { timer.add(std::string(t.name), t.ms); });
    // With both -o and --histogram the image is converted to 8-bit sRGB once: the saved bytes
    // are also what the histogram counts. (Without -o the histogram encodes block by block.)
    std::vector<std::uint8_t> bytes;
    if (output) {
        timer.run("save", [&] {
            if (histogram_path) {
                bytes = prism::encode_srgb8(img, threads);
                prism::save_srgb8(prism::path_from_utf8(*output), bytes, img.width, img.height);
            } else {
                prism::save_image(prism::path_from_utf8(*output), img, threads);
            }
        });
        std::cout << "wrote " << *output << " (" << img.width << "x" << img.height << ")\n";
    }

    if (histogram_path) {
        // Measured after the whole pipeline, on the same 8-bit values save_image writes,
        // whether or not -o was given.
        prism::Histogram hist;
        timer.run("histogram", [&] {
            hist = bytes.empty() ? prism::compute_histogram(img, threads)
                                 : prism::compute_histogram(bytes, img.width, img.height, threads);
            prism::save_image(prism::path_from_utf8(*histogram_path), prism::render_histogram(hist),
                              threads);
        });
        std::cout << "wrote " << *histogram_path << " (histogram)\n";
        print_clipping(hist);
    }

    if (time) {
        // Report what was actually used: small images run single-threaded whatever was asked.
        timer.print(prism::threads_for_pixels(
            static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height), threads));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* program = (argc > 0 && argv[0] != nullptr) ? argv[0] : "prism";

    try {
        const Options opts = parse_args(argc, argv);

        // Read the recipe, matrix and curve before the image: they are quick to check, so a typo
        // fails immediately instead of after decoding a large photo.
        prism::Recipe recipe;
        if (opts.recipe_path) {
            recipe = prism::load_recipe(prism::path_from_utf8(*opts.recipe_path));
        }
        prism::ProcessSettings flags;
        flags.exposure_stops = opts.exposure_stops;
        if (opts.matrix_path) {
            flags.matrix = prism::load_matrix(prism::path_from_utf8(*opts.matrix_path));
        }
        flags.saturation = opts.saturation;
        flags.contrast = opts.contrast;
        prism::RawOptions raw;
        raw.half_size = opts.raw_half;
        if (opts.raw_camera_matrix_path) {
            raw.camera_matrix =
                prism::load_matrix(prism::path_from_utf8(*opts.raw_camera_matrix_path));
        }
        if (opts.curve_spec) {
            flags.curve = prism::parse_curve(*opts.curve_spec);
        }
        // Without --recipe the recipe is empty and the result is exactly `flags`. Merged here once
        // to refuse a bad recipe before any photo is opened; run_one merges again per photo, since
        // white balance depends on the photo's as-shot light.
        (void)prism::merge_recipe_and_flags(recipe, flags);
        const WhiteBalanceFlags wb_flags{opts.kelvin, opts.tint};

        // 0 asks the library for one thread per hardware thread.
        const int threads = opts.threads.value_or(0);
        if (opts.inputs.size() == 1) {
            // A failure is reported by the catch blocks below, as it always was.
            run_one(opts.inputs[0], opts.outputs[0], opts.histogram_path, opts.time, recipe, flags,
                    wb_flags, raw, opts.long_side, threads);
            return 0;
        }

        // A batch: one photo after another (each one already uses every core). A photo that fails
        // is reported and skipped, and the rest still run.
        std::size_t written = 0;
        for (std::size_t i = 0; i < opts.inputs.size(); ++i) {
            const std::string& input = opts.inputs[i];
            try {
                run_one(input, opts.outputs[i], std::nullopt, opts.time, recipe, flags, wb_flags,
                        raw, opts.long_side, threads);
                ++written;
            } catch (const std::exception& e) {
                // Keep the "wrote" lines before this one when both go to the same place.
                std::cout << std::flush;
                // Loading errors name the input already; others (such as a failed save) name the
                // output, so the input is added.
                const std::string what = e.what();
                if (what.find("'" + input + "'") != std::string::npos) {
                    std::cerr << "error: " << what << '\n';
                } else {
                    std::cerr << "error: '" << input << "': " << what << '\n';
                }
            }
        }
        const std::size_t failed = opts.inputs.size() - written;
        std::cout << written << " of " << opts.inputs.size() << " photos written";
        if (failed > 0) {
            std::cout << "; " << failed << " failed";
        }
        std::cout << ".\n";
        return failed > 0 ? 1 : 0;
    } catch (const UsageError& e) {
        std::cerr << "error: " << e.what() << "\n\n";
        print_usage(program);
        return 2;
    } catch (const prism::RecipeError& e) {
        // One line per mistake, each with the same "error: " as any other error.
        for (const prism::RecipeIssue& issue : e.issues()) {
            std::cerr << "error: " << prism::format_recipe_issue(e.source(), issue) << '\n';
        }
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

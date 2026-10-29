#include <exception>
#include <iostream>
#include <string_view>

#include "prism/image.hpp"
#include "prism/version.hpp"

namespace {

void print_usage(const char* program) {
    std::cerr << "prism " << prism::version() << " - command-line photo tool\n"
              << "\n"
              << "Usage: " << program << " <input> -o <output>\n"
              << "\n"
              << "Loads <input> (PNG or JPEG) and writes it to <output> (.png, .jpg or .jpeg).\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* program = (argc > 0 && argv[0] != nullptr) ? argv[0] : "prism";

    // Accept "<input> -o <output>" with the input before or after "-o <output>".
    const char* input = nullptr;
    const char* output = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            print_usage(program);
            return 0;
        } else if (input == nullptr && !arg.starts_with('-')) {
            input = argv[i];
        } else {
            std::cerr << "error: unexpected argument '" << arg << "'\n\n";
            print_usage(program);
            return 2;
        }
    }
    if (input == nullptr || output == nullptr) {
        print_usage(program);
        return 2;
    }

    try {
        const prism::Image img = prism::load_image(input);
        prism::save_image(output, img);
        std::cout << "wrote " << output << " (" << img.width << "x" << img.height << ")\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

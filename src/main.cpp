#include <iostream>

#include "prism/version.hpp"

int main(int argc, char* argv[]) {
    (void)argc;
    const char* program = (argv[0] != nullptr) ? argv[0] : "prism";

    std::cout << "prism " << prism::version() << " - command-line photo tool\n"
              << "\n"
              << "Usage: " << program << " <command> [options]\n"
              << "\n"
              << "No commands are implemented yet.\n";
    return 0;
}

cmake_minimum_required(VERSION 3.20)

project(prism
  VERSION 0.1.0
  DESCRIPTION "Command-line photo tool"
  LANGUAGES CXX)

# Write build/compile_commands.json so clangd / clang-tidy / IDEs see the exact flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PRISM_BUILD_TESTS "Build the Prism unit tests" ON)

# Warnings live on an INTERFACE target instead of global CMAKE_CXX_FLAGS so they apply
# only to our code and not to third-party code (stb, GoogleTest).
add_library(prism_warnings INTERFACE)
if(MSVC)
  target_compile_options(prism_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(prism_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(third_party/stb)
add_subdirectory(lib)
add_subdirectory(src)

if(PRISM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

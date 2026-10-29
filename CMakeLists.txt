cmake_minimum_required(VERSION 3.20)

project(prism
  VERSION 0.1.0
  DESCRIPTION "Command-line photo tool"
  LANGUAGES CXX)

# Write build/compile_commands.json so clangd / clang-tidy / IDEs see the exact flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PRISM_BUILD_TESTS "Build the Prism unit tests" ON)
option(PRISM_SANITIZE_THREAD "Build everything with ThreadSanitizer to detect data races" OFF)

# ThreadSanitizer instruments every memory access and reports two threads touching the same
# location without synchronisation. It must be applied to all code, GoogleTest included, so it
# is set globally here (before any add_subdirectory) rather than on one target. Use a separate
# build directory: TSan builds run 5-15x slower and can't be mixed with normal object files.
if(PRISM_SANITIZE_THREAD)
  if(MSVC)
    message(FATAL_ERROR "PRISM_SANITIZE_THREAD needs GCC or Clang")
  endif()
  # -O1 keeps it reasonably fast; -g and frame pointers give readable stack traces in reports.
  add_compile_options(-fsanitize=thread -g -O1 -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

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
add_subdirectory(bench)

if(PRISM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

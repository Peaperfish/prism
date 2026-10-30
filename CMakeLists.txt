cmake_minimum_required(VERSION 3.20)

project(prism
  VERSION 0.1.0
  DESCRIPTION "Command-line photo tool"
  LANGUAGES CXX)

# Prism's copyright line, from the "Required Notice:" in LICENSE.md (PolyForm asks that it go with
# every copy): for the About window, the macOS Info.plist and the Windows version resource.
file(STRINGS ${PROJECT_SOURCE_DIR}/LICENSE.md PRISM_REQUIRED_NOTICE REGEX "^Required Notice:" LIMIT_COUNT 1)
if(NOT PRISM_REQUIRED_NOTICE)
  message(FATAL_ERROR "LICENSE.md has no 'Required Notice:' line")
endif()
string(REGEX REPLACE "^Required Notice: *" "" PRISM_COPYRIGHT "${PRISM_REQUIRED_NOTICE}")
if(WIN32)
  include(cmake/WindowsResources.cmake)
endif()

# Write build/compile_commands.json so clangd / clang-tidy / IDEs see the exact flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PRISM_BUILD_TESTS "Build the Prism unit tests" ON)
# The desktop app (SDL3 + Dear ImGui, downloaded at configure time). Off by default: with it off
# nothing is downloaded and no GUI target exists. prism_gui_core, its logic without a window, is
# built either way.
option(PRISM_BUILD_GUI "Build the Prism desktop app (prism_gui, with SDL3 and Dear ImGui)" OFF)
option(PRISM_SANITIZE_THREAD "Build everything with ThreadSanitizer to detect data races" OFF)
option(PRISM_SANITIZE_ADDRESS
       "Build everything with AddressSanitizer and UndefinedBehaviorSanitizer to detect memory errors and undefined behaviour"
       OFF)

# PRISM_RELEASE=ON is the configuration the downloadable apps are built with
# (.github/workflows/release.yml). It builds LibRaw from its pinned source tarball and links it
# statically and unmodified (Prism uses LibRaw under CDDL 1.0), refuses anything that would use a
# system or Homebrew LibRaw instead, and on Windows links the C runtime statically (/MT), so no
# Visual C++ redistributable is needed. Off by default: normal builds don't change.
option(PRISM_RELEASE "Configure the build for a downloadable release (static LibRaw from source)" OFF)
if(PRISM_RELEASE)
  # The cache of a build directory configured without PRISM_RELEASE may hold these as OFF.
  if(DEFINED PRISM_LIBRAW_FROM_SOURCE AND NOT PRISM_LIBRAW_FROM_SOURCE)
    message(FATAL_ERROR "PRISM_RELEASE needs PRISM_LIBRAW_FROM_SOURCE=ON: a release never uses a "
                        "system LibRaw (use a fresh build directory)")
  endif()
  if(DEFINED PRISM_WITH_LIBRAW AND NOT PRISM_WITH_LIBRAW)
    message(FATAL_ERROR "PRISM_RELEASE needs PRISM_WITH_LIBRAW=ON: releases have RAW support "
                        "(use a fresh build directory)")
  endif()
  # FetchContent would use this local tree as-is, skipping the download and its SHA-256 check.
  if(FETCHCONTENT_SOURCE_DIR_LIBRAW)
    message(FATAL_ERROR "PRISM_RELEASE builds LibRaw from the pinned tarball: unset "
                        "FETCHCONTENT_SOURCE_DIR_LIBRAW")
  endif()
  # Without it, the binaries only run on the macOS version they were built on. 13.3 is the oldest
  # whose libc++ has std::to_chars for floats (gui_core/src/settings.cpp).
  if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "PRISM_RELEASE needs -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3")
  endif()
  # Older Xcodes (16.4 and before) ship a libc++ without std::jthread and std::stop_token
  # (lib/parallel.cpp, include/prism/process.hpp), and the build would fail halfway. Check before
  # building, for the architectures and minimum macOS this release is configured for.
  if(APPLE)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS -std=c++20)
    check_cxx_source_compiles([[
      #include <stop_token>
      #include <thread>
      int main() {
        std::jthread worker([](std::stop_token stop) { (void)stop.stop_requested(); });
        worker.request_stop();
      }]] PRISM_HAVE_JTHREAD)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT PRISM_HAVE_JTHREAD)
      message(FATAL_ERROR "This compiler's C++ library has no std::jthread or std::stop_token, "
                          "which Prism uses. Build with a newer Xcode (the one CI uses on "
                          "macos-latest) in a fresh build directory.")
    endif()
  endif()
  set(PRISM_LIBRAW_FROM_SOURCE ON CACHE BOOL "" FORCE)
  set(PRISM_WITH_LIBRAW ON CACHE BOOL "" FORCE)
  # For every target, SDL and GoogleTest included (they follow CMAKE_MSVC_RUNTIME_LIBRARY too).
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# LibRaw (camera RAW decoding) is optional. pkg-config finds a system install, e.g. from
# `brew install libraw`; we use libraw_r, the thread-safe build. The option defaults to ON
# when LibRaw is found, so a plain configure picks it up. Without it Prism still builds and
# RAW files fail with a clear error.
#
# PRISM_LIBRAW_FROM_SOURCE=ON skips pkg-config and builds the pinned LibRaw release from source
# instead, as a shared library next to Prism (cmake/LibRaw.cmake; static with PRISM_RELEASE).
# Windows has no system LibRaw, so that is the way to get RAW support there.
option(PRISM_LIBRAW_FROM_SOURCE
       "Build LibRaw from its pinned release tarball as a shared library instead of using a system install"
       OFF)
if(PRISM_LIBRAW_FROM_SOURCE)
  set(prism_libraw_default ON)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBRAW QUIET IMPORTED_TARGET libraw_r)
  endif()
  # LibRaw's .pc file asks for -lstdc++, which on macOS is an alias for libc++, already linked by
  # the compiler: drop it to avoid ld's "ignoring duplicate libraries" warning.
  if(APPLE AND TARGET PkgConfig::LIBRAW)
    get_target_property(prism_libraw_libs PkgConfig::LIBRAW INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM prism_libraw_libs stdc++ -lstdc++)
    set_target_properties(PkgConfig::LIBRAW PROPERTIES INTERFACE_LINK_LIBRARIES "${prism_libraw_libs}")
  endif()
  if(LIBRAW_FOUND)
    set(prism_libraw_default ON)
  else()
    set(prism_libraw_default OFF)
  endif()
endif()
option(PRISM_WITH_LIBRAW "Read camera RAW files (.arw, .dng, ...) with LibRaw" ${prism_libraw_default})
if(PRISM_WITH_LIBRAW AND PRISM_LIBRAW_FROM_SOURCE)
  include(cmake/LibRaw.cmake)
  set(prism_libraw_target libraw_r)
  get_target_property(prism_libraw_type libraw_r TYPE)
  if(prism_libraw_type STREQUAL "STATIC_LIBRARY")
    message(STATUS "RAW support: LibRaw ${PRISM_LIBRAW_VERSION} (built from source, static)")
  else()
    message(STATUS "RAW support: LibRaw ${PRISM_LIBRAW_VERSION} (built from source, shared)")
  endif()
elseif(PRISM_WITH_LIBRAW)
  if(NOT LIBRAW_FOUND)
    message(FATAL_ERROR "PRISM_WITH_LIBRAW is ON but LibRaw was not found via pkg-config "
                        "(install it, e.g. `brew install libraw pkg-config`, pass "
                        "-DPRISM_LIBRAW_FROM_SOURCE=ON to build it, or pass -DPRISM_WITH_LIBRAW=OFF)")
  endif()
  set(prism_libraw_target PkgConfig::LIBRAW)
  message(STATUS "RAW support: LibRaw ${LIBRAW_VERSION}")
else()
  message(STATUS "RAW support: off (LibRaw not found or PRISM_WITH_LIBRAW=OFF)")
endif()
# Only a source build has a DLL of ours to copy next to the executables (see cmake/LibRaw.cmake).
if(NOT COMMAND prism_copy_libraw_dll)
  function(prism_copy_libraw_dll target)
  endfunction()
endif()

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

# AddressSanitizer reports out-of-bounds reads and writes, use-after-free and leaks;
# UndefinedBehaviorSanitizer reports signed overflow, bad shifts, misaligned loads and the like.
# Like TSan, they are set globally so GoogleTest is instrumented too. TSan and ASan both reserve
# the same shadow memory, so they can't be combined: use one build directory per sanitizer.
# UBSan only prints a report by default; run with UBSAN_OPTIONS=halt_on_error=1 to fail the test.
if(PRISM_SANITIZE_ADDRESS)
  if(MSVC)
    message(FATAL_ERROR "PRISM_SANITIZE_ADDRESS needs GCC or Clang")
  endif()
  if(PRISM_SANITIZE_THREAD)
    message(FATAL_ERROR "PRISM_SANITIZE_ADDRESS and PRISM_SANITIZE_THREAD can't be combined")
  endif()
  add_compile_options(-fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# MSVC reads a source file without a byte-order mark in the system code page, not as UTF-8.
# Our sources are UTF-8 (a few comments hold non-ASCII text): on a Chinese or Japanese system
# that gives warning C4819, a UTF-8 sequence can swallow the newline ending a // comment, and a
# u8"..." literal with non-ASCII characters would hold the wrong bytes. /utf-8 makes MSVC read
# the sources as UTF-8 and store narrow string literals as UTF-8, as GCC and Clang do. Global,
# so GoogleTest and stb are read the same way.
if(MSVC)
  add_compile_options(/utf-8)
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
add_subdirectory(gui_core)
add_subdirectory(src)
add_subdirectory(bench)
if(PRISM_BUILD_GUI)
  # On macOS, gui_app has a little Objective-C++ (macos_menu.mm) and SDL has Objective-C (.m).
  # Both languages are enabled here, in the top-level directory and before SDL is added: CMake
  # maps file extensions to languages globally but keeps a language's setup per directory, so
  # OBJCXX enabled only in gui_app, or without OBJC next to it, makes SDL's .m files OBJCXX
  # sources in a directory with no rules to compile them ("Missing variable is:
  # CMAKE_OBJCXX_COMPILE_OBJECT"). OBJC goes first so that .m stays Objective-C.
  if(APPLE)
    enable_language(OBJC OBJCXX)
  endif()
  include(cmake/GuiDependencies.cmake)
endif()
# THIRD_PARTY_NOTICES.txt, for the About window and the release downloads.
if(PRISM_BUILD_GUI OR PRISM_RELEASE)
  include(cmake/ThirdPartyNotices.cmake)
endif()
if(PRISM_BUILD_GUI)
  add_subdirectory(gui_app)
endif()

if(PRISM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(PRISM_RELEASE)
  include(cmake/Package.cmake)
endif()

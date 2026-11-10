add_library(prism_core STATIC
  atomic_file.cpp
  camera_profile.cpp
  color.cpp
  dng_profile.cpp
  dodge_burn.cpp
  exif.cpp
  heic.cpp
  histogram.cpp
  image.cpp
  parallel.cpp
  pipeline.cpp
  process.cpp
  ranges.cpp
  recipe.cpp
  resize.cpp
  raw.cpp
  stroke_field.cpp
  tiff_reader.cpp
  version.cpp
  white_balance.cpp)

# The stroke field promises bit-identical values whichever grid, row range or thread computes a
# pixel, and that the incremental API matches a fresh rasterisation exactly. That holds only if no
# a * b + c is fused into one multiply-add, so contraction is off for this file on Clang too (GCC
# already has it off everywhere via prism_warnings; MSVC does not contract by default). The dodge &
# burn stage applies that field through fast_exp2, a polynomial, so it is compiled the same way.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set_source_files_properties(stroke_field.cpp dodge_burn.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()

# PUBLIC: anything linking prism_core also gets include/ on its include path, so it can
# write #include "prism/version.hpp". The "prism/" prefix avoids name clashes with other libraries.
target_include_directories(prism_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

# PUBLIC so users of prism_core are also compiled as C++20 (our headers may need it).
target_compile_features(prism_core PUBLIC cxx_std_20)

# The version comes from project(VERSION ...), so it is set in one place.
target_compile_definitions(prism_core PRIVATE PRISM_VERSION="${PROJECT_VERSION}")

# PRIVATE: stb is an implementation detail; callers of prism_core don't see stb headers.
target_link_libraries(prism_core PRIVATE stb prism_warnings)

# Also PRIVATE, for the same reason: only raw.cpp includes LibRaw's headers. (prism_core is a
# static library, so CMake still passes LibRaw on to whatever finally links it.) The target is
# PkgConfig::LIBRAW for a system LibRaw or libraw_r for a source build (top-level CMakeLists.txt).
if(PRISM_WITH_LIBRAW)
  target_link_libraries(prism_core PRIVATE ${prism_libraw_target})
  target_compile_definitions(prism_core PRIVATE PRISM_HAVE_LIBRAW)
endif()
# A release checks that it compiles against the pinned LibRaw's own header (cmake/LibRaw.cmake).
if(PRISM_RELEASE)
  target_compile_definitions(prism_core PRIVATE
    PRISM_EXPECT_LIBRAW_MAJOR=${PRISM_LIBRAW_VERSION_MAJOR}
    PRISM_EXPECT_LIBRAW_MINOR=${PRISM_LIBRAW_VERSION_MINOR}
    PRISM_EXPECT_LIBRAW_PATCH=${PRISM_LIBRAW_VERSION_PATCH})
endif()

# HEIC photos are decoded by Apple's ImageIO, so only macOS can open them; heic.cpp has a stub that
# says so everywhere else. Private for the same reason as LibRaw: callers never see these headers.
if(APPLE)
  target_link_libraries(prism_core PRIVATE "-framework ImageIO" "-framework CoreGraphics"
                                           "-framework CoreFoundation")
endif()

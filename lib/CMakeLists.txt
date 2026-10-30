add_library(prism_core STATIC
  camera_profile.cpp
  color.cpp
  dng_profile.cpp
  histogram.cpp
  image.cpp
  parallel.cpp
  pipeline.cpp
  process.cpp
  resize.cpp
  raw.cpp
  tiff_reader.cpp
  version.cpp)

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

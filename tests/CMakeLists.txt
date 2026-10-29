include(FetchContent)

# Pinned to a release tag so every build gets the same GoogleTest.
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# On Windows, use the same C runtime as the rest of the project (avoids link errors).
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# We only need GoogleTest itself: no GoogleMock, and no installing it alongside Prism.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(prism_tests
  camera_profile_test.cpp
  cli_test.cpp
  color_test.cpp
  dng_profile_test.cpp
  histogram_test.cpp
  image_test.cpp
  parallel_test.cpp
  pipeline_test.cpp
  raw_test.cpp
  tiff_reader_test.cpp
  version_test.cpp)
target_link_libraries(prism_tests PRIVATE prism_core prism_warnings GTest::gtest_main)
# lib/ holds internal headers (e.g. tiff_reader.hpp) that are not part of the public API; only
# the tests may include them directly.
target_include_directories(prism_tests PRIVATE ${PROJECT_SOURCE_DIR}/lib)

# cli_test.cpp runs the real prism executable: tell it where the build put it, and make sure
# building the tests also (re)builds prism so the tests never run a stale binary.
target_compile_definitions(prism_tests PRIVATE PRISM_EXE="$<TARGET_FILE:prism>"
                                              PRISM_PROFILE_EXE="$<TARGET_FILE:prism_profile>")
add_dependencies(prism_tests prism prism_profile)

# Registers each TEST() as its own ctest entry by asking the test binary for its list.
include(GoogleTest)
gtest_discover_tests(prism_tests)

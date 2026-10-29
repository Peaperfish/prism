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
  color_test.cpp
  image_test.cpp
  version_test.cpp)
target_link_libraries(prism_tests PRIVATE prism_core prism_warnings GTest::gtest_main)

# Registers each TEST() as its own ctest entry by asking the test binary for its list.
include(GoogleTest)
gtest_discover_tests(prism_tests)

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
  process_test.cpp
  prism_profile_test.cpp
  raw_test.cpp
  resize_test.cpp
  tiff_reader_test.cpp
  unicode_path_test.cpp
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

# The test executable needs libraw_r.dll next to it too (Windows source build), and already when
# gtest_discover_tests runs it after the build, so this comes first.
prism_copy_libraw_dll(prism_tests)

# Registers each TEST() as its own ctest entry by asking the test binary for its list.
include(GoogleTest)
gtest_discover_tests(prism_tests)

# The GUI core's tests, in their own executable so that they show up under their own name.
add_executable(prism_gui_core_tests
  gui/background_export_test.cpp
  gui/file_types_test.cpp
  gui/history_test.cpp
  gui/live_preview_test.cpp
  gui/main_thread_queue_test.cpp
  gui/presets_test.cpp
  gui/preview_renderer_test.cpp
  gui/session_test.cpp
  gui/settings_test.cpp
  gui/slider_gesture_test.cpp)
target_link_libraries(prism_gui_core_tests PRIVATE prism_gui_core prism_warnings GTest::gtest_main)
# The RAW tests use the synthetic DNG writer (synthetic_dng.hpp, which needs lib/tiff_reader.hpp).
target_include_directories(prism_gui_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                        ${PROJECT_SOURCE_DIR}/lib)
prism_copy_libraw_dll(prism_gui_core_tests)
gtest_discover_tests(prism_gui_core_tests)

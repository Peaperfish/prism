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
  cli_batch_test.cpp
  cli_docs_test.cpp
  cli_test.cpp
  color_test.cpp
  dng_profile_test.cpp
  dodge_burn_test.cpp
  exif_test.cpp
  frame_test.cpp
  framed_field_test.cpp
  heic_test.cpp
  histogram_test.cpp
  image_test.cpp
  orientation_test.cpp
  parallel_test.cpp
  pipeline_test.cpp
  process_test.cpp
  ranges_test.cpp
  recipe_docs_test.cpp
  recipe_flags_test.cpp
  recipe_process_test.cpp
  recipe_test.cpp
  prism_profile_test.cpp
  raw_test.cpp
  resize_test.cpp
  stroke_field_test.cpp
  tiff_reader_test.cpp
  unicode_path_test.cpp
  version_test.cpp
  white_balance_test.cpp)
target_link_libraries(prism_tests PRIVATE prism_core prism_warnings GTest::gtest_main)
# lib/ holds internal headers (e.g. tiff_reader.hpp) that are not part of the public API; only
# the tests may include them directly.
target_include_directories(prism_tests PRIVATE ${PROJECT_SOURCE_DIR}/lib)

# cli_test.cpp runs the real prism executable: tell it where the build put it, and make sure
# building the tests also (re)builds prism so the tests never run a stale binary.
target_compile_definitions(prism_tests PRIVATE PRISM_EXE="$<TARGET_FILE:prism>"
                                              PRISM_PROFILE_EXE="$<TARGET_FILE:prism_profile>"
                                              PRISM_CLI_DOC="${PROJECT_SOURCE_DIR}/docs/CLI.md"
                                              PRISM_RECIPE_DOC="${PROJECT_SOURCE_DIR}/docs/RECIPES.md"
                                              PRISM_RECIPES_DIR="${PROJECT_SOURCE_DIR}/recipes"
                                              PRISM_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
                                              PRISM_GRADE_SAMPLE="${PROJECT_SOURCE_DIR}/samples/grade.txt")
add_dependencies(prism_tests prism prism_profile)
# heic_test.cpp compares against ImageIO's own rendering.
if(APPLE)
  target_link_libraries(prism_tests PRIVATE "-framework ImageIO" "-framework CoreGraphics"
                                            "-framework CoreFoundation")
endif()

# The test executable needs libraw_r.dll next to it too (Windows source build), and already when
# gtest_discover_tests runs it after the build, so this comes first.
prism_copy_libraw_dll(prism_tests)

# Registers each TEST() as its own ctest entry by asking the test binary for its list.
include(GoogleTest)
gtest_discover_tests(prism_tests)

# The GUI core's tests, in their own executable so that they show up under their own name.
add_executable(prism_gui_core_tests
  gui/background_export_test.cpp
  gui/brush_test.cpp
  gui/builtin_look_files_test.cpp
  gui/builtin_looks_test.cpp
  gui/canvas_input_test.cpp
  gui/clip_note_test.cpp
  gui/clipping_test.cpp
  gui/close_guard_test.cpp
  gui/detail_base_test.cpp
  gui/explanations_test.cpp
  gui/export_size_test.cpp
  gui/field_cache_test.cpp
  gui/file_types_test.cpp
  gui/frame_wiring_test.cpp
  gui/framing_test.cpp
  gui/histogram_paint_test.cpp
  gui/histogram_test.cpp
  gui/history_test.cpp
  gui/layout_test.cpp
  gui/light_path_test.cpp
  gui/live_preview_test.cpp
  gui/main_thread_queue_test.cpp
  gui/neutral_pick_test.cpp
  gui/paper_grade_test.cpp
  gui/preferences_test.cpp
  gui/presets_test.cpp
  gui/photo_format_test.cpp
  gui/pixel_sample_test.cpp
  gui/dodge_burn_stage_test.cpp
  gui/preview_renderer_test.cpp
  gui/recipe_settings_test.cpp
  gui/render_region_test.cpp
  gui/recipe_ui_test.cpp
  gui/scopes_test.cpp
  gui/session_test.cpp
  gui/settings_test.cpp
  gui/slider_gesture_test.cpp
  gui/stage_controls_test.cpp
  gui/test_strip_test.cpp
  gui/theme_tokens_test.cpp
  gui/typed_value_test.cpp
  gui/view_transform_test.cpp
  gui/wheel_scaling_test.cpp)
target_link_libraries(prism_gui_core_tests PRIVATE prism_gui_core prism_warnings GTest::gtest_main)
# The RAW tests use the synthetic DNG writer (synthetic_dng.hpp, which needs lib/tiff_reader.hpp).
target_include_directories(prism_gui_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                        ${PROJECT_SOURCE_DIR}/lib)
target_compile_definitions(prism_gui_core_tests PRIVATE
                           PRISM_RECIPES_DIR="${PROJECT_SOURCE_DIR}/recipes"
                           PRISM_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
prism_copy_libraw_dll(prism_gui_core_tests)
gtest_discover_tests(prism_gui_core_tests)

# Tests of the desktop app's widget code against a headless Dear ImGui (no window, no backend). Only
# with the app on, since they need ImGui.
if(PRISM_BUILD_GUI)
  add_executable(prism_gui_app_tests
    gui_app/active_control_test.cpp
    gui_app/export_sheet_test.cpp
    gui_app/framing_bar_test.cpp
    gui_app/light_path_test.cpp
    gui_app/scopes_panel_test.cpp)
  target_link_libraries(prism_gui_app_tests PRIVATE prism_gui_panels prism_warnings
                        GTest::gtest_main)
  prism_copy_libraw_dll(prism_gui_app_tests)
  gtest_discover_tests(prism_gui_app_tests)
endif()

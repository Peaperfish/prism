# The GUI's logic without any window: settings, presets, undo, the open/export session and the
# preview worker thread. It needs nothing beyond prism_core and the standard library, so it is
# always built and every CI job (the sanitizer ones included) tests it. The window itself
# (SDL + ImGui) will be a separate target on top of this one.
add_library(prism_gui_core STATIC
  src/file_types.cpp
  src/history.cpp
  src/live_preview.cpp
  src/presets.cpp
  src/preview_renderer.cpp
  src/session.cpp
  src/settings.cpp
  src/slider_gesture.cpp)

target_include_directories(prism_gui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# PUBLIC: the GUI core's headers include prism_core's (EditSettings uses prism::Matrix3).
target_link_libraries(prism_gui_core PUBLIC prism_core PRIVATE prism_warnings)

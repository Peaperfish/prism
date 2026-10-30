# The desktop app: an SDL3 window drawn with Dear ImGui. It stays thin: it draws, and forwards
# clicks and keys to prism_gui_core's Session, which does the work. It never calls a pipeline
# stage itself.
add_executable(prism_gui
  app.cpp
  file_dialogs.cpp
  main.cpp
  preview_texture.cpp
  theme.cpp)

target_link_libraries(prism_gui PRIVATE prism_gui_core imgui SDL3::SDL3 prism_warnings)

# windows.h (pulled in by SDL on Windows) defines min and max as macros, which break std::min and
# std::max; and most of it isn't needed.
if(WIN32)
  target_compile_definitions(prism_gui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

prism_copy_libraw_dll(prism_gui)

# The GUI's logic without any window: settings, presets, undo, the open/export session and the
# preview worker thread. It needs nothing beyond prism_core and the standard library, so it is
# always built and every CI job (the sanitizer ones included) tests it. The window itself
# (SDL + ImGui) will be a separate target on top of this one.

# The built-in looks (recipes/), compiled in as byte arrays (builtin_looks.hpp). These lists are the
# order of the Look menu, and the first of each is the default; a test fails if a file in recipes/
# is in neither. Re-run when a recipe changes.
set(prism_looks_raw natural original vivid soft bw)
set(prism_looks_image original vivid soft bw)
set(PRISM_BUILTIN_LOOK_ARRAYS "")
foreach(kind raw image)
  string(TOUPPER ${kind} kind_upper)
  set(PRISM_BUILTIN_LOOKS_${kind_upper} "")
  foreach(look IN LISTS prism_looks_${kind})
    set(file ${PROJECT_SOURCE_DIR}/recipes/${kind}/${look}.prism)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file})
    file(READ ${file} bytes HEX)
    string(REGEX REPLACE "(..)" "0x\\1," bytes "${bytes}")
    # Only on the formatted bytes: in the bare hex, "0d0a" also matches across two bytes (30 d0 a1).
    string(REPLACE "0x0d,0x0a," "0x0a," bytes "${bytes}")
    string(APPEND PRISM_BUILTIN_LOOK_ARRAYS
           "constexpr unsigned char ${kind}_${look}[] = {${bytes} 0};\n")
    string(APPEND PRISM_BUILTIN_LOOKS_${kind_upper}
           "        {\"${look}\", \"${kind}/${look}.prism\", text_of(${kind}_${look})},\n")
  endforeach()
endforeach()
configure_file(builtin_looks.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/builtin_looks.cpp @ONLY)

add_library(prism_gui_core STATIC
  src/background_export.cpp
  src/brush.cpp
  src/canvas_input.cpp
  src/clip_note.cpp
  src/clipping.cpp
  src/close_guard.cpp
  src/explanations.cpp
  src/export_size.cpp
  src/field_cache.cpp
  src/file_types.cpp
  src/framing.cpp
  src/histogram.cpp
  src/histogram_paint.cpp
  src/history.cpp
  src/layout.cpp
  src/light_path.cpp
  src/live_preview.cpp
  src/neutral_pick.cpp
  src/paper_grade.cpp
  src/photo_format.cpp
  src/preferences.cpp
  src/presets.cpp
  src/pixel_sample.cpp
  src/preview_renderer.cpp
  src/recipe_settings.cpp
  src/recipe_ui.cpp
  src/scopes.cpp
  src/session.cpp
  src/settings.cpp
  src/slider_gesture.cpp
  src/stage_controls.cpp
  src/test_strip.cpp
  src/theme_tokens.cpp
  src/typed_value.cpp
  src/view_transform.cpp
  src/wheel_scaling.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/builtin_looks.cpp)

target_include_directories(prism_gui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# PUBLIC: the GUI core's headers include prism_core's (EditSettings uses prism::Matrix3).
target_link_libraries(prism_gui_core PUBLIC prism_core PRIVATE prism_warnings)

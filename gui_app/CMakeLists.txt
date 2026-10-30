# The desktop app: an SDL3 window drawn with Dear ImGui. It stays thin: it draws, and forwards
# clicks and keys to prism_gui_core's Session, which does the work. It never calls a pipeline
# stage itself.

# The About window's texts (about_info.hpp), from what this build links: the components and
# THIRD_PARTY_NOTICES.txt (cmake/ThirdPartyNotices.cmake), embedded so the window shows them
# wherever the app is copied to.
set(PRISM_ABOUT_COMPONENTS "")
foreach(entry IN LISTS PRISM_THIRD_PARTY_COMPONENTS)
  string(REPLACE "|" "\", \"" entry "${entry}")
  string(APPEND PRISM_ABOUT_COMPONENTS "    ThirdPartyComponent{\"${entry}\"},\n")
endforeach()
file(READ ${PRISM_THIRD_PARTY_NOTICES} PRISM_ABOUT_NOTICES HEX)
string(REGEX REPLACE "(..)" "0x\\1," PRISM_ABOUT_NOTICES "${PRISM_ABOUT_NOTICES}")
string(REPLACE "\\" "\\\\" prism_notice "${PRISM_REQUIRED_NOTICE}")
string(REPLACE "\"" "\\\"" prism_notice "${prism_notice}")
set(PRISM_ABOUT_REQUIRED_NOTICE "\"${prism_notice}\"")
configure_file(about_info.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/about_info.cpp @ONLY)

add_executable(prism_gui
  app.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/about_info.cpp
  file_dialogs.cpp
  main.cpp
  preview_texture.cpp
  theme.cpp)
target_include_directories(prism_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(prism_gui PRIVATE prism_gui_core imgui SDL3::SDL3 prism_warnings)

# windows.h (pulled in by SDL on Windows) defines min and max as macros, which break std::min and
# std::max; and most of it isn't needed.
if(WIN32)
  target_compile_definitions(prism_gui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# On macOS, a few lines of Objective-C++ connect the app menu's Preferences item (see
# macos_menu.hpp).
if(APPLE)
  enable_language(OBJCXX)
  target_sources(prism_gui PRIVATE macos_menu.mm)
  target_link_libraries(prism_gui PRIVATE "-framework Cocoa")
endif()

prism_copy_libraw_dll(prism_gui)

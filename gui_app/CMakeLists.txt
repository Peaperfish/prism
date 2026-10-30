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

# WIN32: a Windows GUI program, started without a console window. SDL_main.h still gives main()
# UTF-8 arguments (main.cpp). MACOSX_BUNDLE: on macOS it is Prism.app (see below).
add_executable(prism_gui WIN32 MACOSX_BUNDLE
  app.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/about_info.cpp
  file_dialogs.cpp
  main.cpp
  preview_texture.cpp
  theme.cpp)
target_include_directories(prism_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(prism_gui PRIVATE prism_gui_core imgui SDL3::SDL3 prism_warnings)

# The placeholder icon, drawn by the build (packaging/make_icon.cpp): no image file in the repo.
add_executable(prism_make_icon ${PROJECT_SOURCE_DIR}/packaging/make_icon.cpp)
target_link_libraries(prism_make_icon PRIVATE stb prism_warnings)

if(WIN32)
  set(prism_ico ${CMAKE_CURRENT_BINARY_DIR}/prism.ico)
  add_custom_command(OUTPUT ${prism_ico}
    COMMAND prism_make_icon --ico ${prism_ico}
    DEPENDS prism_make_icon
    COMMENT "Drawing the app icon"
    VERBATIM)
  target_sources(prism_gui PRIVATE ${prism_ico})
  prism_add_version_resource(prism_gui "Prism" ICON ${prism_ico})
endif()

# On macOS the app is a bundle, Prism.app: Info.plist (packaging/macos/Info.plist.in) gives its
# name, version, bundle id, minimum macOS and Retina support, and the icon goes in Resources as
# Prism.icns, made from the drawn PNGs with iconutil (part of macOS).
if(APPLE)
  set(PRISM_BUNDLE_EXECUTABLE Prism)
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(PRISM_MACOS_MINIMUM ${CMAKE_OSX_DEPLOYMENT_TARGET})
  else()
    set(PRISM_MACOS_MINIMUM 13.3)
  endif()
  configure_file(${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in
                 ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
  set(prism_iconset ${CMAKE_CURRENT_BINARY_DIR}/Prism.iconset)
  set(prism_icns ${CMAKE_CURRENT_BINARY_DIR}/Prism.icns)
  add_custom_command(OUTPUT ${prism_icns}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${prism_iconset}
    COMMAND prism_make_icon --iconset ${prism_iconset}
    COMMAND iconutil -c icns -o ${prism_icns} ${prism_iconset}
    DEPENDS prism_make_icon
    COMMENT "Drawing the app icon"
    VERBATIM)
  target_sources(prism_gui PRIVATE ${prism_icns})
  set_source_files_properties(${prism_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(prism_gui PROPERTIES
    OUTPUT_NAME ${PRISM_BUNDLE_EXECUTABLE}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
endif()

# windows.h (pulled in by SDL on Windows) defines min and max as macros, which break std::min and
# std::max; and most of it isn't needed.
if(WIN32)
  target_compile_definitions(prism_gui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# On macOS, a few lines of Objective-C++ connect the app menu's Preferences item (see
# macos_menu.hpp). OBJCXX is enabled in the top-level CMakeLists.txt.
if(APPLE)
  target_sources(prism_gui PRIVATE macos_menu.mm)
  target_link_libraries(prism_gui PRIVATE "-framework Cocoa")
endif()

prism_copy_libraw_dll(prism_gui)

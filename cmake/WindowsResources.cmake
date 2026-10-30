# Windows only: gives an executable a version resource (Explorer's Properties > Details shows the
# version, description and copyright) and, optionally, an icon. The version comes from
# project(VERSION), the copyright from the Required Notice in LICENSE.

# prism_add_version_resource(<target> <description> [ICON <file.ico>])
function(prism_add_version_resource target description)
  cmake_parse_arguments(PARSE_ARGV 2 arg "" "ICON" "")
  get_target_property(PRISM_RC_NAME ${target} OUTPUT_NAME)
  if(NOT PRISM_RC_NAME)
    set(PRISM_RC_NAME ${target})
  endif()
  set(PRISM_RC_DESCRIPTION "${description}")
  if(arg_ICON)
    set(PRISM_RC_ICON "1 ICON \"${arg_ICON}\"")
  else()
    set(PRISM_RC_ICON "")
  endif()
  set(rc ${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc)
  configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/version.rc.in ${rc} @ONLY)
  target_sources(${target} PRIVATE ${rc})
  if(arg_ICON)
    # The icon is generated during the build: compile the .rc after it.
    set_source_files_properties(${rc} PROPERTIES OBJECT_DEPENDS ${arg_ICON})
  endif()
endfunction()

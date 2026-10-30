add_executable(prism main.cpp)
target_link_libraries(prism PRIVATE prism_core prism_warnings)

# Converts a camera profile (LightCraft JSON) to a matrix file for --raw-camera-matrix.
add_executable(prism_profile prism_profile.cpp)
target_link_libraries(prism_profile PRIVATE prism_core prism_warnings)

# On Windows the C runtime builds argv from the UTF-16 command line in the "ANSI" code page
# (e.g. Windows-1252), so a file name in CJK or with an emoji arrives as '?'. This manifest makes
# UTF-8 the process's ANSI code page (Windows 10 1903 and later): argv arrives as UTF-8, which
# main() turns into paths with prism::path_from_utf8. MSVC's linker embeds a .manifest listed as a
# source. The test binary deliberately has no manifest, so the library tests show the library
# itself handles non-ASCII paths, without help from the code page.
if(MSVC)
  target_sources(prism PRIVATE utf8.manifest)
  target_sources(prism_profile PRIVATE utf8.manifest)
endif()

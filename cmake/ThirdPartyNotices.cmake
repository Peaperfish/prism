# Writes THIRD_PARTY_NOTICES.txt for what this configuration actually links: stb always, SDL3 and
# Dear ImGui with PRISM_BUILD_GUI, LibRaw with PRISM_WITH_LIBRAW. Licence texts are read from the
# downloaded sources being built (and SDL's notices only for the files SDL compiles here), never
# typed in by hand. LibRaw's own licence files are not copied into it: the release ships them
# unchanged in licenses/LibRaw/ and this file points there.
#
# Included from the top-level CMakeLists.txt after every dependency is available. Sets
# PRISM_THIRD_PARTY_NOTICES to the generated file's path and PRISM_THIRD_PARTY_COMPONENTS to a list
# of "name version|licence" entries (for the About window).

set(PRISM_THIRD_PARTY_NOTICES ${PROJECT_BINARY_DIR}/THIRD_PARTY_NOTICES.txt)
set(prism_repo_url https://github.com/Peaperfish/prism)
set(PRISM_THIRD_PARTY_COMPONENTS "")

# The first /* ... */ comment in `file` whose text contains `marker`, without the comment markers
# and the leading "*" of each line.
function(prism_comment_block out file marker)
  file(READ ${file} text)
  # MATCHALL returns a list, which would split the texts at their semicolons.
  string(REPLACE ";" "<semicolon>" text "${text}")
  string(REGEX MATCHALL "/\\*([^*]|\\*+[^*/])*\\*+/" blocks "${text}")
  foreach(block IN LISTS blocks)
    string(FIND "${block}" "${marker}" at)
    if(NOT at EQUAL -1)
      string(REGEX REPLACE "^/\\*[ \t]*(-\\*-[^\n]*)?\n?" "" block "${block}")
      string(REGEX REPLACE "\n?[ \t]*\\*+/$" "" block "${block}")
      # One "*" (and a space) at the start of each line. No "^" here: CMake would match it again
      # after each replacement and take a bullet's "*" too.
      string(REGEX REPLACE "\n[ \t]*\\*[ \t]?" "\n" block "\n${block}")
      string(SUBSTRING "${block}" 1 -1 block)
      string(REPLACE "<semicolon>" ";" block "${block}")
      set(${out} "${block}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "No comment containing '${marker}' in ${file}")
endfunction()

# Appends `title`, an underline and `body` to the variable `var`.
macro(prism_notice_section var title body)
  string(LENGTH "${title}" prism_title_length)
  string(REPEAT "-" ${prism_title_length} prism_underline)
  string(APPEND ${var} "\n\n${title}\n${prism_underline}\n\n${body}\n")
endmacro()

set(prism_notices "Third-party software in Prism ${PROJECT_VERSION}
====================================

Prism itself is under the PolyForm Noncommercial License 1.0.0 (see LICENSE).
It includes the components below, each under its own licence.")

# --- LibRaw -------------------------------------------------------------------------------------
if(PRISM_WITH_LIBRAW AND PRISM_LIBRAW_FROM_SOURCE)
  file(READ ${libraw_SOURCE_DIR}/COPYRIGHT prism_libraw_copyright)
  # The paragraphs naming the code LibRaw itself contains (dcraw, DCB/FBDD, X3F, DNG SDK).
  string(FIND "${prism_libraw_copyright}" "LibRaw uses" prism_at)
  if(prism_at EQUAL -1)
    message(FATAL_ERROR "LibRaw's COPYRIGHT no longer lists the code it uses: update ${CMAKE_CURRENT_LIST_FILE}")
  endif()
  string(SUBSTRING "${prism_libraw_copyright}" ${prism_at} -1 prism_libraw_uses)
  string(STRIP "${prism_libraw_uses}" prism_libraw_uses)
  prism_comment_block(prism_dcb ${libraw_SOURCE_DIR}/src/demosaic/dcb_demosaic.cpp "Gozdz")
  prism_comment_block(prism_x3f ${libraw_SOURCE_DIR}/internal/x3f_tools.h "Karlsson")
  list(APPEND PRISM_THIRD_PARTY_COMPONENTS "LibRaw ${PRISM_LIBRAW_VERSION}|CDDL 1.0")
  prism_notice_section(prism_notices "LibRaw ${PRISM_LIBRAW_VERSION}" "\
https://www.libraw.org/

Licence: Common Development and Distribution License (CDDL) 1.0.
LibRaw is offered under LGPL 2.1 or CDDL 1.0; Prism uses it under CDDL 1.0.
It is statically linked and unmodified.

  Licence text:      licenses/LibRaw/LICENSE.CDDL
  Copyright notices: licenses/LibRaw/COPYRIGHT

Source code: LibRaw-${PRISM_LIBRAW_VERSION}-source.tar.gz, attached to the Prism
${PROJECT_VERSION} release at ${prism_repo_url}/releases/tag/v${PROJECT_VERSION},
and upstream at https://www.libraw.org/data/LibRaw-${PRISM_LIBRAW_VERSION}.tar.gz

LibRaw contains code from other projects, named in its COPYRIGHT file
(licenses/LibRaw/COPYRIGHT):

${prism_libraw_uses}

DCB demosaic and FBDD denoise (src/demosaic/dcb_demosaic.cpp in LibRaw):

${prism_dcb}

X3F library (internal/x3f_tools.h in LibRaw):

${prism_x3f}")
elseif(PRISM_WITH_LIBRAW)
  list(APPEND PRISM_THIRD_PARTY_COMPONENTS "LibRaw ${LIBRAW_VERSION} (system)|LGPL 2.1 or CDDL 1.0")
  prism_notice_section(prism_notices "LibRaw ${LIBRAW_VERSION}" "\
https://www.libraw.org/

This build uses the LibRaw installed on the system (LGPL 2.1 or CDDL 1.0); its licence
files come with that installation.")
endif()

# --- SDL3 and Dear ImGui (the window) -----------------------------------------------------------
if(PRISM_BUILD_GUI)
  file(READ ${sdl3_SOURCE_DIR}/LICENSE.txt prism_sdl_licence)
  string(STRIP "${prism_sdl_licence}" prism_sdl_licence)
  set(prism_sdl_body "https://www.libsdl.org/
Licence: zlib. Statically linked.

${prism_sdl_licence}")

  # Files in SDL under their own notices, if this configuration compiles them.
  get_target_property(prism_sdl_sources SDL3-static SOURCES)
  set(prism_sdl_others
    "src/libm/e_pow.c|SDL's math functions (src/libm), from Sun Microsystems' fdlibm|Sun Microsystems"
    "src/video/SDL_rotate.c|Surface rotation (src/video/SDL_rotate.c), from SDL_gfx|Schiffler"
    "src/joystick/controller_type.c|Controller types (src/joystick/controller_type.c)|Valve"
    "src/events/imKStoUCS.c|X11 keysym table (src/events/imKStoUCS.c)|Jamey Sharp")
  foreach(entry IN LISTS prism_sdl_others)
    string(REPLACE "|" ";" entry "${entry}")
    list(GET entry 0 prism_file)
    list(GET entry 1 prism_what)
    list(GET entry 2 prism_marker)
    list(FIND prism_sdl_sources "${sdl3_SOURCE_DIR}/${prism_file}" prism_index)
    if(NOT prism_index EQUAL -1)
      prism_comment_block(prism_text ${sdl3_SOURCE_DIR}/${prism_file} "${prism_marker}")
      string(APPEND prism_sdl_body "\n\n${prism_what}:\n\n${prism_text}")
    endif()
  endforeach()
  list(FILTER prism_sdl_sources INCLUDE REGEX "/src/video/yuv2rgb/[^/]*\\.c$")
  if(prism_sdl_sources)
    file(READ ${sdl3_SOURCE_DIR}/src/video/yuv2rgb/LICENSE prism_text)
    string(STRIP "${prism_text}" prism_text)
    string(APPEND prism_sdl_body "\n\nYUV to RGB conversion (src/video/yuv2rgb):\n\n${prism_text}")
  endif()
  list(APPEND PRISM_THIRD_PARTY_COMPONENTS "SDL ${PRISM_SDL_VERSION}|zlib")
  prism_notice_section(prism_notices "SDL ${PRISM_SDL_VERSION}" "${prism_sdl_body}")

  file(READ ${imgui_SOURCE_DIR}/LICENSE.txt prism_imgui_licence)
  string(STRIP "${prism_imgui_licence}" prism_imgui_licence)
  list(APPEND PRISM_THIRD_PARTY_COMPONENTS "Dear ImGui ${PRISM_IMGUI_VERSION}|MIT"
                                           "ProggyClean and ProggyForever fonts|MIT")
  prism_notice_section(prism_notices "Dear ImGui ${PRISM_IMGUI_VERSION}" "\
https://github.com/ocornut/imgui
Licence: MIT. Statically linked.

${prism_imgui_licence}

Dear ImGui embeds the fonts Prism's interface uses, both under the MIT licence:
  ProggyClean, Copyright (c) 2004, 2005 Tristan Grimmer
  ProggyForever, Copyright (c) 2026 Disco Hello, Copyright (c) 2019,2023 Tristan Grimmer
It also contains stb_truetype, stb_rect_pack and stb_textedit by Sean Barrett (MIT or
public domain), and a list of Japanese kanji under CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/), which Prism doesn't use.")
endif()

# --- stb (image files) --------------------------------------------------------------------------
file(READ ${PROJECT_SOURCE_DIR}/third_party/stb/stb_image.h prism_stb)
string(FIND "${prism_stb}" "ALTERNATIVE A - MIT License" prism_at)
string(SUBSTRING "${prism_stb}" ${prism_at} -1 prism_stb)
string(REGEX REPLACE "\n-+\nALTERNATIVE B.*" "" prism_stb "${prism_stb}")
string(REPLACE "ALTERNATIVE A - MIT License\n" "" prism_stb "${prism_stb}")
list(APPEND PRISM_THIRD_PARTY_COMPONENTS "stb_image and stb_image_write|MIT or public domain")
prism_notice_section(prism_notices "stb_image and stb_image_write" "\
https://github.com/nothings/stb
Licence: MIT or public domain (Unlicense), at the user's choice; reproduced here under MIT.

${prism_stb}")

file(WRITE ${PRISM_THIRD_PARTY_NOTICES}.tmp "${prism_notices}\n")
file(COPY_FILE ${PRISM_THIRD_PARTY_NOTICES}.tmp ${PRISM_THIRD_PARTY_NOTICES} ONLY_IF_DIFFERENT)
file(REMOVE ${PRISM_THIRD_PARTY_NOTICES}.tmp)

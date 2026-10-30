# Builds LibRaw from its release tarball as a shared library, libraw_r (.dylib/.so/.dll), for
# PRISM_LIBRAW_FROM_SOURCE=ON. Used where no system LibRaw is available (Windows) or where we want
# the same LibRaw on every OS. LibRaw is LGPL 2.1 or CDDL 1.0: keeping it a separate shared library,
# unmodified, lets users replace it with their own build.
#
# LibRaw no longer ships CMake support, so this lists its sources itself: the 79 objects of
# LIB_MT_OBJECTS in LibRaw's Makefile.dist. On a LibRaw update, compare that list with the one below.

include(FetchContent)

set(PRISM_LIBRAW_VERSION 0.22.3)
# The pinned release tarball; the SHA-256 is the one Homebrew's libraw formula also pins.
FetchContent_Declare(
  libraw
  URL https://www.libraw.org/data/LibRaw-${PRISM_LIBRAW_VERSION}.tar.gz
  URL_HASH SHA256=789f3c33466210a1a0907f747a97c715d21bbf4af712f257fcbbd90111b227e4
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# The tarball has no CMakeLists.txt, so this only downloads and unpacks it.
FetchContent_MakeAvailable(libraw)

set(prism_libraw_sources
  src/libraw_c_api.cpp
  src/libraw_datastream.cpp
  src/decoders/canon_600.cpp
  src/decoders/crx.cpp
  src/decoders/decoders_dcraw.cpp
  src/decoders/decoders_libraw.cpp
  src/decoders/decoders_libraw_dcrdefs.cpp
  src/decoders/dng.cpp
  src/decoders/fp_dng.cpp
  src/decoders/fuji_compressed.cpp
  src/decoders/generic.cpp
  src/decoders/kodak_decoders.cpp
  src/decoders/load_mfbacks.cpp
  src/decoders/olympus14.cpp
  src/decoders/pana8.cpp
  src/decoders/smal.cpp
  src/decoders/sonycc.cpp
  src/decoders/unpack.cpp
  src/decoders/unpack_thumb.cpp
  src/decompressors/losslessjpeg.cpp
  src/demosaic/aahd_demosaic.cpp
  src/demosaic/ahd_demosaic.cpp
  src/demosaic/dcb_demosaic.cpp
  src/demosaic/dht_demosaic.cpp
  src/demosaic/misc_demosaic.cpp
  src/demosaic/xtrans_demosaic.cpp
  src/integration/dngsdk_glue.cpp
  src/integration/rawspeed_glue.cpp
  src/metadata/adobepano.cpp
  src/metadata/canon.cpp
  src/metadata/ciff.cpp
  src/metadata/cr3_parser.cpp
  src/metadata/epson.cpp
  src/metadata/exif_gps.cpp
  src/metadata/fuji.cpp
  src/metadata/hasselblad_model.cpp
  src/metadata/identify.cpp
  src/metadata/identify_tools.cpp
  src/metadata/kodak.cpp
  src/metadata/leica.cpp
  src/metadata/makernotes.cpp
  src/metadata/mediumformat.cpp
  src/metadata/minolta.cpp
  src/metadata/misc_parsers.cpp
  src/metadata/nikon.cpp
  src/metadata/normalize_model.cpp
  src/metadata/olympus.cpp
  src/metadata/p1.cpp
  src/metadata/pentax.cpp
  src/metadata/samsung.cpp
  src/metadata/sony.cpp
  src/metadata/tiff.cpp
  src/postprocessing/aspect_ratio.cpp
  src/postprocessing/dcraw_process.cpp
  src/postprocessing/mem_image.cpp
  src/postprocessing/postprocessing_aux.cpp
  src/postprocessing/postprocessing_utils.cpp
  src/postprocessing/postprocessing_utils_dcrdefs.cpp
  src/preprocessing/ext_preprocess.cpp
  src/preprocessing/raw2image.cpp
  src/preprocessing/subtract_black.cpp
  src/tables/cameralist.cpp
  src/tables/colorconst.cpp
  src/tables/colordata.cpp
  src/tables/wblists.cpp
  src/utils/curves.cpp
  src/utils/decoder_info.cpp
  src/utils/init_close_utils.cpp
  src/utils/open.cpp
  src/utils/phaseone_processing.cpp
  src/utils/read_utils.cpp
  src/utils/thumb_utils.cpp
  src/utils/utils_dcraw.cpp
  src/utils/utils_libraw.cpp
  src/write/apply_profile.cpp
  src/write/file_write.cpp
  src/write/tiff_writer.cpp
  src/x3f/x3f_parse_process.cpp
  src/x3f/x3f_utils_patched.cpp)
list(TRANSFORM prism_libraw_sources PREPEND ${libraw_SOURCE_DIR}/)

add_library(libraw_r SHARED ${prism_libraw_sources})
# The name already starts with "lib": without PREFIX "" it would be liblibraw_r.dylib on macOS.
# On every OS the file is then libraw_r.dylib/.so/.dll, as LibRaw's own builds name it.
set_target_properties(libraw_r PROPERTIES PREFIX "")
# PUBLIC: users write #include <libraw/libraw.h>, as with a system install.
target_include_directories(libraw_r PUBLIC ${libraw_SOURCE_DIR})
# No warnings: it is third-party code we don't change (like stb and GoogleTest, it doesn't get
# prism_warnings either).
if(MSVC)
  # LIBRAW_BUILDLIB makes LibRaw's headers mark its API __declspec(dllexport) while building the DLL;
  # code using the DLL doesn't define it and gets dllimport (libraw_types.h). No OpenMP: Prism
  # decodes on its own threads, and the headers only ask for OpenMP when _OPENMP is defined.
  target_compile_definitions(libraw_r PRIVATE LIBRAW_BUILDLIB _CRT_SECURE_NO_WARNINGS)
  target_compile_options(libraw_r PRIVATE /w)
else()
  target_compile_options(libraw_r PRIVATE -w)
endif()
# htonl and friends come from Winsock (LibRaw also asks for it with #pragma comment).
if(WIN32)
  target_link_libraries(libraw_r PRIVATE ws2_32)
endif()

# Windows has no rpath: an .exe finds a DLL in its own directory (or on PATH). Copy libraw_r.dll
# next to every executable that links it, after each build. Call this before
# gtest_discover_tests(), whose post-build step already runs the test executable.
function(prism_copy_libraw_dll target)
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:libraw_r> $<TARGET_FILE_DIR:${target}>
      VERBATIM)
  endif()
endfunction()

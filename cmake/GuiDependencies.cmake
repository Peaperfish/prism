# SDL3 and Dear ImGui for the GUI window (PRISM_BUILD_GUI=ON only; nothing here is downloaded
# otherwise). Both are pinned to a release tarball by URL and SHA-256: one download with no git
# history, and the build fails if the file behind the URL ever changes. Updating means changing
# the version and the hash.

include(FetchContent)

set(PRISM_SDL_VERSION 3.4.18)
set(PRISM_IMGUI_VERSION 1.93.0)

FetchContent_Declare(
  sdl3
  URL https://github.com/libsdl-org/SDL/releases/download/release-${PRISM_SDL_VERSION}/SDL3-${PRISM_SDL_VERSION}.tar.gz
  URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_Declare(
  imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v${PRISM_IMGUI_VERSION}.tar.gz
  URL_HASH SHA256=f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# SDL linked statically, so the app is one executable with no SDL library next to it. Only the
# subsystems a photo editor uses are built: the others (audio, camera, game controllers, haptics,
# sensors, tray icons) would add code, size and, for HIDAPI, files under other licences.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(sdl3 imgui)

# Dear ImGui ships no CMake build: the core files plus the SDL3 platform backend and the
# SDL_Renderer backend (Metal on macOS, Direct3D on Windows, OpenGL or Vulkan on Linux, chosen by
# SDL; no GL loader needed).
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
target_compile_features(imgui PUBLIC cxx_std_11)

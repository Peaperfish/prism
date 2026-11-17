#pragma once

// SDL 3.4.18 leaves a window's CAMetalLayer as it set it for a linear renderer (RGBA16Float,
// extended linear sRGB, EDR on) when the renderer is destroyed, so a default renderer made after
// it washes the UI out and leaks the layer's drawables (spike "SDL linear renderer on macOS",
// #97). reset_metal_layer puts the layer back as a fresh SDL Metal view has it (BGRA8Unorm, no
// colour space, EDR off); Display calls it on every renderer recreation. Apple only: it is built
// and called only there.
namespace prism::gui_app {
void reset_metal_layer(void* metal_layer);
}  // namespace prism::gui_app

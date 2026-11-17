#pragma once

// Shared by the JPEG writers (image.cpp's save_srgb8 and ultra_hdr.cpp): internal, not part of the
// public API.

namespace prism::detail {

// The SDR picture's JPEG quality. 95 keeps JPEG artefacts hard to see while still compressing
// well. An Ultra HDR JPEG's primary image uses it too, so it decodes to the same pixels as the
// plain JPEG.
inline constexpr int sdr_jpeg_quality = 95;

}  // namespace prism::detail

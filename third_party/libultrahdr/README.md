# libultrahdr (one ported routine)

- Source: https://github.com/google/libultrahdr at tag `v2.0.2`, `lib/src/gainmapmath.cpp` (lines 1615-1664), `floatToUnsignedFractionImpl`
- Licence: libultrahdr is distributed under the terms of both the MIT license and the Apache License (Version 2.0); the ported routine is used under MIT. `LICENSE` is libultrahdr's `LICENSE-MIT`, copied unmodified from the same tag
- Use: `float_to_unsigned_fraction` in `lib/gain_map.cpp` is a port of that routine (kept step for step, so the ISO 21496-1 fractions in an Ultra HDR JPEG are the ones libultrahdr writes). It is compiled into `prism_core`, so it ships in `prism` and `prism_gui`; `cmake/ThirdPartyNotices.cmake` puts this licence in `THIRD_PARTY_NOTICES.txt`
- Nothing else from libultrahdr is in the repo or linked into what ships: the optional checker (`PRISM_CHECK_LIBULTRAHDR`) links the system library into a test program only

To update: copy `LICENSE-MIT` from the new tag, check the ported lines in `lib/gain_map.cpp` against the new `gainmapmath.cpp`, and update the tag and line numbers here and in `lib/gain_map.cpp`, and the version in `cmake/ThirdPartyNotices.cmake`.

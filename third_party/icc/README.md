# sRGB ICC profile (vendored)

- Source: https://github.com/saucecontrol/Compact-ICC-Profiles at commit `bdd84663061bc4ae95ca70decff54f581e27f702`, `profiles/sRGB-v2-magic.icc` (unmodified)
- Licence: CC0 1.0 Universal (public domain dedication), in `LICENSE`, copied from the same commit
- Use: embedded in the primary image of every Ultra HDR JPEG Prism writes (`lib/srgb_icc.cpp` holds the same 736 bytes as an array; `UltraHdrJpeg.TheIccProfileIsTheStoredOne` checks they are equal)

| File | Bytes | SHA-256 |
|------|-------|---------|
| `sRGB-v2-magic.icc` | 736 | `af4efe28f6d311799865f325ba39184a2e978b113ca74124d60af7de22b105f4` |

To update: download the profile and licence from the same repo, then update the commit, size and hash here, the array in `lib/srgb_icc.cpp` and the expected hash in `tests/ultra_hdr_test.cpp`.

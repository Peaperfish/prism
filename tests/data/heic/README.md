# HEIC fixtures

`quadrants_o1.heic` to `quadrants_o8.heic` are the 64 x 32 four-quadrant picture of
`tests/synthetic_exif.hpp` (top-left red, top-right green, bottom-left blue, bottom-right yellow),
written with Orientation 1 to 8. The pixels are the same in all eight. ImageIO records the
orientation twice: as HEIF transform boxes (`irot` for a turn, `imir` for a mirror, the latter in
the files for 2, 4, 5 and 7) and as the EXIF Orientation tag, so the files differ in those.

`quadrants_meta.heic` is the same picture with Orientation 1 and camera metadata: TIFF Make
"Pentax Ä" and Model "K-1"; Exif ISO 400, exposure time 0.004 s, f/2.8, focal length 35 mm
(52 mm in 35 mm film terms), lens model "Objektiv 写真" and date taken 2024:05:06 07:08:09. It is
there to test that every `PhotoInfo` field, non-ASCII text included, comes back unchanged.

None of them contains a location. Made on macOS by `tools/make_heic_fixtures.cpp` (the file says
how to build and run it); they are checked in so the tests need no tool and no camera file.

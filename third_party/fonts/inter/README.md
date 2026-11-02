# Inter (vendored)

- Source: https://github.com/rsms/inter, release v4.1 (`Inter-4.1.zip`, `extras/ttf/`)
- Files: `Inter-Regular.ttf` (411,640 bytes), `Inter-SemiBold.ttf` (419,744 bytes), unmodified
- License: SIL Open Font License 1.1, no Reserved Font Name; the text is in `OFL.txt`
  (the zip's `LICENSE.txt`, unchanged)
- Use: the interface font of `prism_gui`. CMake turns the two files into byte arrays compiled
  into the executable (`gui_app/fonts.cpp.in`), so no font file is needed at run time.

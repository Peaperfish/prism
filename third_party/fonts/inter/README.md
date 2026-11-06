# Inter (vendored)

- Source: https://github.com/rsms/inter, release v4.1 (`Inter-4.1.zip`, `extras/ttf/`)
- Files: `Inter-Regular.ttf` (411,736 bytes), `Inter-SemiBold.ttf` (419,840 bytes), **modified**: Inter's
  tabular figures are baked in (see "How the files were made" below)
- License: SIL Open Font License 1.1, no Reserved Font Name (so the modified files keep the name
  "Inter"); the text is in `OFL.txt` (the zip's `LICENSE.txt`, unchanged)
- Use: the interface font of `prism_gui`. CMake turns the two files into byte arrays compiled
  into the executable (`gui_app/fonts.cpp.in`), so no font file is needed at run time.

## How the files were made

Every digit of the interface font has the same width, so numbers that change (a zoom of 56%, an
exposure of +1.00 stops, a sample read-out) don't make the text around them move. Inter has this
as its `tnum` OpenType feature, which dear ImGui does not apply, so the feature is frozen into the
fonts: the digits' glyphs are replaced by their tabular versions.

Made with [opentype-feature-freezer](https://github.com/twardoch/fonttools-opentype-feature-freezer)
1.32.2 (fonttools 4.60.2) from the two files in the Inter 4.1 release's `extras/ttf/`:

    pip install opentype-feature-freezer
    pyftfeatfreeze -f 'tnum' Inter-Regular.ttf  Inter-Regular.ttf.new
    pyftfeatfreeze -f 'tnum' Inter-SemiBold.ttf Inter-SemiBold.ttf.new

(no `-R`: the font keeps its name). SHA-256 of the originals from Inter-4.1.zip:

    40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82  Inter-Regular.ttf
    78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3  Inter-SemiBold.ttf

After the freeze the digits 0 to 9 have one advance width (1328 units of 2048 in Regular, 1325 in
SemiBold). Nothing else in the fonts was changed.

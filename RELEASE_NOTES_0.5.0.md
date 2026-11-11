# Prism 0.5.0 – Darkroom

This release makes the darkroom controls physical. White balance is set in Kelvin and tint, with a picker that finds the light from a grey in the photo. Contrast is a paper grade. Dodge and burn is painted on the photo with a brush. Export can also write a smaller size.

## New

- **Dodge & burn with a brush.** Paint to hold back or add light, in stops, on the parts of the photo you choose. The stage comes right after Exposure. Press `B`, or choose View > Brush, to turn the brush on. `X` swaps dodge and burn. Hold Option (Alt on Windows and Linux) to paint one stroke in the other tone. `[` and `]` change the size, `Shift+[` and `Shift+]` change the edge, and holding `M` shows where you have painted. One stroke is one undo step. Strokes are included in an export while the Dodge & burn switch is on, but they are not saved in recipes. ([#22](https://github.com/Peaperfish/prism/issues/22))
- **White balance in Kelvin and tint.** Name the light the photo was lit by, from 2000 K to 15000 K, and from green to magenta. The neutral picker (`W`) sets both from a grey, white or other neutral spot in the photo. On the command line, `--kelvin` and `--tint` do the same. Recipe format 2 records white balance only when you chose one. ([#21](https://github.com/Peaperfish/prism/issues/21))
- **Export at a smaller size.** The Export sheet offers Original, Long side (in pixels) or Percent. `--long-side` does the same on the command line. ([#52](https://github.com/Peaperfish/prism/issues/52))

## Changed

- Contrast is shown and set as a paper grade, from 0 to 5. ([#23](https://github.com/Peaperfish/prism/issues/23))
- Each photo opens at Decode, the first stage. A note at the top of the photo says what Decode shows. ([#57](https://github.com/Peaperfish/prism/issues/57), [#59](https://github.com/Peaperfish/prism/issues/59))
- The panel is never narrower than its text needs, and its resize handle is centred on its edge. ([#56](https://github.com/Peaperfish/prism/issues/56), [#57](https://github.com/Peaperfish/prism/issues/57))
- Stage order: Exposure → Dodge & burn → White balance → Colour matrix → Saturation → Contrast → Tone curve.

## Fixed

- Shortcuts no longer act behind the Save Recipe… sheet or other modal sheets. ([#54](https://github.com/Peaperfish/prism/issues/54))
- The Export sheet says "about" when a RAW file's full size is only an estimate. ([#55](https://github.com/Peaperfish/prism/issues/55))
- Quitting while a preview was rendering could wait for one extra render.

## Known limits

- Brush strokes aren't saved in recipes.
- The apps are not signed or notarized yet, so macOS and Windows warn the first time you open them. ([#11](https://github.com/Peaperfish/prism/issues/11))
- HEIC uses about 0.5 GB more memory than the same photo as a JPEG. ([#42](https://github.com/Peaperfish/prism/issues/42))

## Learn more

The recipe format is described in [docs/RECIPES.md](https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md), the app in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md), and batches in the [command-line reference](https://github.com/Peaperfish/prism/blob/main/docs/CLI.md).

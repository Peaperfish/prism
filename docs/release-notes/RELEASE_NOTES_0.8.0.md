# Prism 0.8.0 – HDR

Export photos that keep their light above white, from the command line too. An HDR file looks exactly like today's export on an ordinary screen, and on an HDR screen its highlights keep their light above white, as far as the headroom you choose. On a Mac with an HDR screen the canvas shows that light while you edit.

## New

- **HDR export.** Output encode has an **HDR** switch, off at first, and a **Headroom** slider from +0.00 to +4.00 stops in steps of 0.10, starting at +2.00 stops. With HDR on, a JPEG export is an Ultra HDR JPEG. It contains the same SDR picture Prism exports with HDR off, plus a gain map that compatible viewers use to show the light above white on an HDR screen. +1.00 stop is twice white's light. A PNG stays SDR, so with HDR on Prism suggests a .jpg name and refuses a .png one. While HDR is on, Output encode's subtitle says how far above white the file keeps light: "sRGB for the screen · HDR up to +2.00 stops". ([#105](https://github.com/Peaperfish/prism/issues/105), [#107](https://github.com/Peaperfish/prism/issues/107))
- **HDR from the command line.** `--headroom <stops>` makes `-o photo.jpg` and `--out-dir` write Ultra HDR JPEGs, and so does a recipe's `[hdr]` with `--recipe`; `--headroom 0` overrides it and writes the same picture as 0.7.1. A `.png` output with a headroom is refused before any photo is opened ("HDR files need .jpg", exit code 2). ([#102](https://github.com/Peaperfish/prism/issues/102), [#105](https://github.com/Peaperfish/prism/issues/105))
- **Recipes keep the headroom.** A recipe saved with HDR on has an `[hdr]` section with `headroom` and says `prism_recipe = 3`; loading it turns HDR on at that headroom. Recipes without HDR save in the same format as before, so 0.7.1 still opens them. ([#102](https://github.com/Peaperfish/prism/issues/102))
- **The canvas shows the light above white on a Mac with an HDR screen.** With HDR on, the final picture shows what a viewer of the exported file shows on that screen as it is set now: the more room the screen has above white, the more of the headroom you see. The clip overlay (J), the stroke mask (M), the easel and the photo's own pixels, once you zoom past the preview, show it too. Where the clip overlay, the stroke mask or the easel's veil covers the photo, it shows the SDR picture under them, as an ordinary screen does, so no highlight shines through them. With HDR off, the window is drawn exactly as in 0.7.1. ([#106](https://github.com/Peaperfish/prism/issues/106))

## Changed

- **The Sample pin stays on its point of the photo.** Moving, resizing or turning the frame, and undoing or redoing a frame change, keep the pin on the same point of the photo; it is cleared only when that point leaves the frame. A pin placed at Fit reads the photo pixel under the point you clicked once you zoom in to the photo's own pixels. ([#73](https://github.com/Peaperfish/prism/issues/73))

## Viewing HDR files

On a Mac with an HDR screen, macOS Preview shows the light above white in an exported HDR file, up to a limit (see Known limits). An app that doesn't read the gain map shows the SDR picture, the same picture as the export without HDR.

## Known limits

- The canvas shows HDR only on macOS. On Windows and Linux, and on a screen with no room above white, the canvas shows the SDR picture; the exported HDR file still keeps the light above white.
- With HDR on, split compare shows both halves in SDR, because the compare stage has no light above white.
- With HDR on, earlier stages and the test strip show the SDR picture: only the final picture has light above white.
- The clip marks and the status note count all light above white as clipped, even where HDR keeps it; the J overlay has no mark for light kept above white, and Sample's "On screen" line reads the plain picture, so it stops at 100% of white's light. ([#113](https://github.com/Peaperfish/prism/issues/113))
- macOS Preview shows light above white only up to 8× white's light (+3.00 stops), whatever the file's headroom.
- With the photo's own pixels on screen, a pixel's HDR light can differ very slightly from the exported file's: its gain map is made for the part on screen, not the whole photo.
- The soft edge of a brush stroke over a highlight, while the stroke mask (M) is shown, steps from the HDR picture to the SDR one instead of fading.
- At Fit, a tight crop looks softer than it exports, because Fit is drawn from the preview. Zoom in to see the photo's own pixels.
- On a photo too large for the memory rule, zooming past the preview shows the preview enlarged, not the photo's own pixels.
- Opening another photo while a stopped full-size read is finishing reads that photo twice. ([#82](https://github.com/Peaperfish/prism/issues/82))
- Brush size keys may not work on every keyboard layout. ([#64](https://github.com/Peaperfish/prism/issues/64))
- Frames and brush strokes aren't saved in recipes, and they are lost when the photo closes. Prism asks before closing, even straight after an export.
- The apps are not signed or notarized yet, so macOS and Windows warn the first time you open them. ([#11](https://github.com/Peaperfish/prism/issues/11))
- HEIC uses about 0.5 GB more memory than the same photo as a JPEG. ([#42](https://github.com/Peaperfish/prism/issues/42))

## Learn more

HDR export and light above white are described in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md), `[hdr]` in [docs/RECIPES.md](https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md), and `--headroom` in the [command-line reference](https://github.com/Peaperfish/prism/blob/main/docs/CLI.md).

This product includes Gain Map technology under license by Adobe.

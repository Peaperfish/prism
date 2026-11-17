# Prism 0.8.0 – HDR

> **DRAFT, not for publishing.** Every line marked **TO DECIDE:** is wording or behaviour that isn't settled yet (#107 and the follow-ups in #106). Delete this box and every marker before tagging.

**TO DECIDE:** the title after the dash: "HDR" (as the milestone), or "Light above white" (the GUIDE section #107 names).

**TO DECIDE:** the summary. Draft: Export photos that keep their light above white. An HDR file looks exactly like today's export on an ordinary screen, and on an HDR screen its highlights keep their light above white, as far as the headroom you choose. On a Mac with an HDR screen the canvas shows that light while you edit.

## New

- **HDR export.** Output encode has an **HDR** switch, off at first, and a **Headroom** slider from +0.00 to +4.00 stops in steps of 0.10, starting at +2.00 stops. With HDR on, a JPEG export is an Ultra HDR JPEG: the SDR picture every viewer shows, exactly as without HDR, plus a gain map from which an HDR screen rebuilds the light above white. +1.00 stop is twice white's light. A PNG stays SDR, so with HDR on Prism asks for a .jpg name. ([#105](https://github.com/Peaperfish/prism/issues/105))
- **HDR from the command line.** `--headroom <stops>` makes `-o photo.jpg` and `--out-dir` write Ultra HDR JPEGs; `--headroom 0` writes exactly what 0.7.1 wrote. A `.png` output with a headroom is refused before any photo is opened. ([#102](https://github.com/Peaperfish/prism/issues/102), [#105](https://github.com/Peaperfish/prism/issues/105))
- **Recipes keep the headroom.** A recipe saved with HDR on has an `[hdr]` section with `headroom` and says `prism_recipe = 3`; loading it turns HDR on at that headroom. Recipes without HDR save in the same format as before, so 0.7.1 still opens them. ([#102](https://github.com/Peaperfish/prism/issues/102))
- **The canvas shows the light above white on a Mac with an HDR screen**, such as a recent MacBook Pro's screen. With HDR on, the final picture shows what a viewer of the exported file shows on that screen as it is set now: the more room the screen has above white, the more of the headroom you see. The clip overlay (J), the stroke mask (M), the easel and the photo's own pixels at 100% and closer show it too. With HDR off, the window is drawn exactly as in 0.7.1. ([#106](https://github.com/Peaperfish/prism/issues/106))
  - **TO DECIDE:** whether to say how the overlays look over the light above white. Draft: "Where the clip overlay, the stroke mask or the easel's veil covers the photo, it shows the SDR picture under them, as an ordinary screen does, so no highlight shines through them."
- **TO DECIDE:** the clip marks, the status note, the J overlay's amber and the pixel sampler for light above white (#107). Not built yet: the marks still count everything above white as clipped, and the sampler reads the SDR picture. Draft lines once they are: "Clip marks say how much light is kept as HDR light and how much is cut off above the headroom." and "Sample colour also says how much of white's light the HDR screen shows there, for example 230% of white's light (+1.20 stops above white)."

## Changed

- **Output encode has a second line**, with the HDR switch and the Headroom slider. ([#105](https://github.com/Peaperfish/prism/issues/105))
- **TO DECIDE:** Output encode's subtitle while HDR is on: "sRGB for the screen · HDR up to +2.00 stops", or unchanged ("sRGB for the screen"). See #107.

## Viewing HDR files

- Chrome and Edge 116 and later, Android, and Safari 26 show the light above white. Other apps and older browsers show the SDR picture, which is exactly the export without HDR.
- **TO DECIDE:** whether to mention that macOS Preview shows at most 3 stops above white (measured: with a headroom of +4.00 stops, Preview's peak is about 7.4× white, not 16×). Draft: "macOS Preview shows up to +3.00 stops above white, whatever the file's headroom."

## Known limits

- The canvas shows HDR only on macOS. On Windows, and on a screen with no room above white, the canvas shows the SDR picture; the exported HDR file still keeps the light above white.
- **TO DECIDE:** split compare. With HDR on, split compare shows both halves in SDR, because the compare stage has no light above white. Keep this line, or replace it once the split's behaviour is decided (#106).
- With the photo's own pixels on screen, a pixel's HDR light can differ very slightly from the exported file's: its gain map is made for the part on screen, not the whole photo.
- **TO DECIDE:** the soft edge of a brush stroke over a highlight, while the stroke mask (M) is shown: the edge steps from the HDR picture to the SDR one instead of fading. Keep as a known limit, or change the overlay's fade first (#106).
- On a photo too large for the memory rule, zooming past the preview shows the preview enlarged, not the photo's own pixels.
- Opening another photo while a stopped full-size read is finishing reads that photo twice. ([#82](https://github.com/Peaperfish/prism/issues/82))
- Brush size keys may not work on every keyboard layout. ([#64](https://github.com/Peaperfish/prism/issues/64))
- Frames and brush strokes aren't saved in recipes, and they are lost when the photo closes. Prism asks before closing, even straight after an export.
- The apps are not signed or notarized yet, so macOS and Windows warn the first time you open them. ([#11](https://github.com/Peaperfish/prism/issues/11))
- HEIC uses about 0.5 GB more memory than the same photo as a JPEG. ([#42](https://github.com/Peaperfish/prism/issues/42))

## Learn more

HDR in the app is described in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md), `[hdr]` in [docs/RECIPES.md](https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md), and `--headroom` in the [command-line reference](https://github.com/Peaperfish/prism/blob/main/docs/CLI.md).

This product includes Gain Map technology under license by Adobe.

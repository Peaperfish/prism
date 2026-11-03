# Prism 0.2.0 – See the light

This release is about seeing what each step of the pipeline does to your photo.

## New

- **Pipeline view with controls.** Every stage is listed in the order the light passes through it. Click a stage to see the photo at that point, turn the middle stages on or off, and change Exposure, Saturation, Contrast and the tone curve right in the row. ([#2](https://github.com/Peaperfish/prism/issues/2))
- **Compare any two stages.** Split the picture between the original or any stage and the one you are viewing, and drag the divider. ([#3](https://github.com/Peaperfish/prism/issues/3))
- **Histogram and scopes.** A histogram (RGB and luminance) for whichever stage you are viewing, plus parade, waveform and vectorscope. ([#1](https://github.com/Peaperfish/prism/issues/1), [#13](https://github.com/Peaperfish/prism/issues/13))
- **Clipping by stage.** Each row shows how much was clipped there and whether the detail is gone or can still come back. Press `J` to mark those pixels on the photo. ([#4](https://github.com/Peaperfish/prism/issues/4))
- **Clipping note.** The right end of the status bar always says what clipped once a photo is open. ([#31](https://github.com/Peaperfish/prism/issues/31))
- **Gamut warning.** Shows colours too saturated for sRGB and which stage pushed them out. ([#14](https://github.com/Peaperfish/prism/issues/14))
- **Test strip.** Press `T` to see the photo in five bands at −1, −½, 0, +½ and +1 stop, and click a band to use its exposure. Export is not affected. ([#15](https://github.com/Peaperfish/prism/issues/15))
- **Plain-language explanations.** A box at the foot of the panel says what the control under the mouse does to the light, with the numbers worked out for the current value. ([#16](https://github.com/Peaperfish/prism/issues/16))
- **A new look.** Calmer colours, bigger text, and `Cmd`/`Ctrl` `+`, `-` and `0` to change the interface size. ([#26](https://github.com/Peaperfish/prism/issues/26))
- **Smoother preview while dragging.** The picture keeps up while you drag a slider, even with the scopes, clipping marks and a comparison on. ([#5](https://github.com/Peaperfish/prism/issues/5))
- **A prompt before losing edits.** Quitting or opening another photo asks first if you have edits you have not exported. ([#32](https://github.com/Peaperfish/prism/issues/32))

## Fixed

- Preferences no longer opens by itself after Open or Export. ([#24](https://github.com/Peaperfish/prism/issues/24))
- Trackpad scrolling on macOS was far too fast; the panel now follows your fingers. ([#25](https://github.com/Peaperfish/prism/issues/25))
- `Esc` now closes the Preferences, About and notices windows, and backs out of the test strip, then the comparison, then the viewed stage, one step at a time.

## Known limits

- The app is not signed or notarized yet, so macOS and Windows warn the first time you open it. ([#11](https://github.com/Peaperfish/prism/issues/11))
- The preview is not colour-managed for wide-gamut (P3) displays. It is shown as sRGB.
- There is no zoom or pan yet. ([#27](https://github.com/Peaperfish/prism/issues/27))

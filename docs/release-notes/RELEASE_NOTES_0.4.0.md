# Prism 0.4.0 – Fresh look

This release gives Prism a new look and a clearer way to read the light. The header has Undo, Redo and Export…; the Look menu lists the recipes; and the Light path shows every stage of the edit in one place, with its controls. The scopes have been reworked to show where the light is and where it clips.

## New

- **A fresh look.** The header has Undo and Redo as icons, and Export… is the only button with the accent colour. The Look menu lists the looks and the recipe actions, and the explanation area and zoom chip are restyled to match.
- **The Light path.** It replaces the Pipeline section and the Adjust sliders. Each stage has its own row, with a switch, a line that shows its value and a slider. Stages past the one you are viewing are dimmed. Ctrl-click (Cmd-click on a Mac) a row's slider to type a value in the row's own unit: stops for Exposure, a factor (×) for Saturation and Contrast ("-0.5", "−0.5" and "1 stop" all work). Each row carries clip marks, hollow and filled, for its stage. **Compare** sets two stages side by side, and **Swap** exchanges them.
- **Scopes in four tabs.** Histogram, parade, waveform and vectorscope share one plot height. The histogram's red, green and blue are blended, so where they overlap the colours add towards cyan, magenta, yellow and white. Triangles in the histogram's top corners light when pixels are clipped at that end.
- **A readout under the scope.** It says, for example, "0.1% below black / 0.0% above white". "Below black" means a pixel with no light left. Pixels outside sRGB are counted on their own, so a colour can be outside sRGB without being below black. The **J** clip overlay is now a text action in the Scopes header.
- **Guide lines at 5%, 18% and 50% of white's light** on the parade and the waveform, placed where those shares of white's light fall on the scale.
- **A round vectorscope** that keeps every colour inside its ring, including the most saturated greens and magentas.

## Fixed

- A press on empty space in the panel no longer blocks the keyboard shortcuts.
- The status bar's camera line no longer appears or disappears as the clipping note changes.
- During a test strip, the clipping text from before the strip is no longer shown.
- Menus and status messages use a real "…" instead of three dots.

## Upgrading from 0.3.1

- **The panel width.** 0.3.1 saved the panel width as 320 points even when you never chose it, so 0.4.0 cannot tell that case apart. If your saved width is exactly 320, the panel opens at the new default of 440 points, once. Any other width you chose is kept.
- **The Adjust sliders** are now the sliders in the Light path rows. They edit the same settings as before.
- **Recipes** are unchanged both ways: a recipe from 0.3.1 loads in 0.4.0, and a recipe saved in 0.4.0 loads in 0.3.1.

## Known limits

- The apps are not signed or notarized yet, so macOS and Windows warn the first time you open them. ([#11](https://github.com/Peaperfish/prism/issues/11))
- The preview is not colour-managed for wide-gamut (P3) displays. It is shown as sRGB.
- **100% means 100% of the 2560 px preview, not of the file.** A pixel in the preview can stand for several file pixels, and Sample says so. An export always has every pixel. Zooming to the file's real pixels is planned. ([#37](https://github.com/Peaperfish/prism/issues/37))
- Pinch to zoom has not been tested on real Windows or Linux hardware. The wheel with `Cmd` (macOS) or `Ctrl` (Windows and Linux) zooms everywhere.
- File names in Chinese, Japanese or Korean show as `??` in the window (the file still opens and exports correctly). ([#7](https://github.com/Peaperfish/prism/issues/7))
- Opening a large RAW file freezes the window for a moment while it is decoded. ([#6](https://github.com/Peaperfish/prism/issues/6))
- DNG files compressed with deflate or lossy JPEG don't open. ([#9](https://github.com/Peaperfish/prism/issues/9))
- Picking a built-in look after loading a recipe keeps the recipe's name. ([#35](https://github.com/Peaperfish/prism/issues/35))
- There is no Linux download yet. ([#10](https://github.com/Peaperfish/prism/issues/10))
- HEIC photos open on macOS only; Windows and Linux show a message saying so.
- For HEIC, only the standard picture is used: the HDR layer an iPhone adds is ignored.
- A HEIC whose rotation is stored only in its EXIF data (some converters write files like this) opens sideways. iPhone photos are not affected.
- HEIC uses about 0.5 GB more memory than the same photo as a JPEG. ([#42](https://github.com/Peaperfish/prism/issues/42))

## Learn more

The recipe format is described in [docs/RECIPES.md](https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md), the app in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md), and batches in the [command-line reference](https://github.com/Peaperfish/prism/blob/main/docs/CLI.md).

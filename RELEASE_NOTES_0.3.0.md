# Prism 0.3.0 – Recipes

This release is about keeping a look, and about looking closer: save an edit as a small text file you can read, run it on a whole shoot, zoom into the picture, and read the numbers of any pixel.

## New

- **Recipes.** A recipe is a plain text file, ending in `.prism`, that writes down an edit: exposure, colour matrix, saturation, contrast and tone curve, each with its on/off switch. It holds numbers, not pixels, so you can use it on any photo, read it in a text editor, change a number and send it to someone. ([#17](https://github.com/Peaperfish/prism/issues/17))
- **Save and load recipes in the app.** **File → Save Recipe…** writes your edit down; **File → Load Recipe…**, or dropping a `.prism` file on the window, applies one to the open photo as a single undo step. If a recipe has a mistake, nothing changes and a window lists each one with its line number. Closing the app with edits you haven't exported or saved now offers to save a recipe. ([#18](https://github.com/Peaperfish/prism/issues/18))
- **A whole shoot from the command line.** `prism photo1.ARW photo2.ARW --recipe warm.prism --out-dir edited` applies a recipe to many photos. The recipe is checked once before any photo is opened. If one photo fails, the rest carry on, and the last line says how many were written. Flags such as `--exposure` still work and replace that stage of the recipe. ([#19](https://github.com/Peaperfish/prism/issues/19))
- **The built-in looks are recipes you can read.** Under the Look menu, a line now shows what the look does in numbers ("+1.00 stops, colour ×1.25, contrast ×1.15, filmic"), and **View recipe** opens the look as the file it is, with a **Copy** button. ([#20](https://github.com/Peaperfish/prism/issues/20))
- **Zoom and pan.** Pinch or use the wheel to zoom about the cursor, scroll with two fingers or drag to move the picture, or hold `Space` and drag from anywhere. `F` fits the photo, `1` goes to 100%, `=` and `-` zoom in and out. Above 200% the picture stops smoothing, so you see its pixels as squares. Zooming never re-renders the photo, and it does not affect export. ([#27](https://github.com/Peaperfish/prism/issues/27))
- **Reading a pixel.** Press `I` and point at the photo to read the pixel under the cursor at the stage you are viewing; click to pin it and watch one spot of sky as you move Exposure. You get the linear values (1.00 is white), the pixel's share "of white's light", and how many stops it is above or below middle grey. If it is over white or too saturated for sRGB, a line says which later stage brings it back or clips it, using the same test as the clipping marks. ([#28](https://github.com/Peaperfish/prism/issues/28))

## Fixed

- On Linux, builds made from source with GCC 15 could give a photo processed on several threads slightly different numbers from one processed on a single thread. GCC builds now do each multiply and add separately, so every build gives the same result. The macOS and Windows downloads were never affected. ([#36](https://github.com/Peaperfish/prism/issues/36))
- `Ctrl+Q` quits on Windows and Linux.
- Scrolling to the bottom of the right-hand panel no longer makes it jump. The Explain area now keeps one height, and it keeps showing the control you just left when the cursor passes between two controls. ([#38](https://github.com/Peaperfish/prism/issues/38))

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

## Learn more

The recipe format is described in [docs/RECIPES.md](https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md), the app in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md), and batches in the [command-line reference](https://github.com/Peaperfish/prism/blob/main/docs/CLI.md).

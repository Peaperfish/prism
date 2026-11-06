# Prism 0.3.1 – Photos the right way up

This release opens iPhone photos on macOS and shows camera photos the right way up, in the preview and in exports. It also fixes a few things that were getting in the way.

## New

- **iPhone photos (HEIC/HEIF) open on macOS.** Colours are converted from the photo's own colour space (usually Display P3), so very saturated colours are kept, and the photo opens the right way up. A damaged HEIC file shows an error instead of opening. The `prism` command line opens them too.
- **JPEGs open the right way up.** Prism follows the camera's orientation setting (EXIF), in the preview and in exports, so a photo taken with the camera on its side is no longer shown sideways.

## Fixed

- A photo given when starting Prism now opens even when macOS adds its own launch options. ([#39](https://github.com/Peaperfish/prism/issues/39))
- The zoom chip is readable in the light theme.
- The explanation area fades at its bottom edge when its text scrolls, so you can see there is more.
- The panel-width setting in Preferences reaches the widest panel.

## Known limits

- The apps are not signed or notarized yet, so macOS and Windows warn the first time you open them: see "Running the unsigned apps" below for the steps. ([#11](https://github.com/Peaperfish/prism/issues/11))
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

## Learn more

The app is described in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md).

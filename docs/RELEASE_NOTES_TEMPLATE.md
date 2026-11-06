Prism's desktop app, for macOS and Windows.

## What it does

- Opens PNG, JPEG and camera RAW files (.arw, .dng, .cr2, .cr3, .nef, .raf), from File > Open
  or by dropping a file on the window.
- Shows a live preview that follows every slider as you drag it.
- Zoom and pan: zoom about the cursor, move the picture, fit it to the window or go to 100%.
  Above 200% you see the pixels as squares.
- Reads any pixel: the linear values, its share "of white's light" and how many stops it is
  above or below middle grey, at the stage you are viewing.
- Simple mode: looks for RAW and for PNG/JPEG, and sliders for the main adjustments, with
  undo and redo.
- Recipes: save an edit as a small, readable text file (`.prism`) and load it on any photo. The
  built-in looks are recipes too, and the app shows their numbers and their file.
- Pipeline view: every stage in the order the light passes through it, with its controls. Turn
  stages on or off and click one to see the photo at that point.
- Compare any two stages, including the original, side by side.
- Histogram, parade, waveform and vectorscope for the stage you are viewing.
- Clipping by stage, shown per stage and on the photo, plus a warning for colours outside sRGB.
- Test strip: the photo in five bands at different exposures, to choose by eye.
- A plain-language explanation of each control, with the numbers for its current value.
- Exports the result as PNG or JPEG, and asks first if you would lose edits you haven't
  exported or saved as a recipe.
- Light and dark themes, following the system by default.
- Preferences for the theme, the interface size and the panel width.
- The command-line tool `prism` is included in both downloads. It edits one photo or a whole
  shoot at once with `--recipe` and `--out-dir`.

## Downloads

| File | For |
|---|---|
| `prism-{{VERSION}}-macos-universal.zip` | macOS 13.3 or later, Apple Silicon and Intel |
| `prism-{{VERSION}}-windows-x64.zip` | Windows 10 and 11, 64-bit |
| `LibRaw-{{LIBRAW_VERSION}}-source.tar.gz` | LibRaw's source code, as used in this release |
| `SHA256SUMS.txt` | SHA-256 checksums of the files above |

## Checking the downloads

`SHA256SUMS.txt` lists the SHA-256 checksum of each file. To check a download, run
`shasum -a 256 -c SHA256SUMS.txt --ignore-missing` on macOS, or `Get-FileHash <file>` in
PowerShell on Windows, and compare the result with the line for that file.

## Running the unsigned apps

Prism isn't signed by Apple or Microsoft yet, so both systems warn you the first time.

**macOS:** unzip, then drag Prism.app to Applications. The first time, right-click (or
Control-click) Prism.app and choose **Open**, then **Open** again. On macOS 15 and later that
no longer works: try to open Prism once, then go to **System Settings → Privacy & Security**,
scroll down to the message about Prism and click **Open Anyway**. After that it opens normally.

**Windows:** extract the whole zip, then run `prism_gui.exe`. If Microsoft Defender SmartScreen
says "Windows protected your PC", click **More info → Run anyway**. If Smart App Control is on
(Windows 11), it blocks unsigned apps and there is no way to allow just Prism.

## Coming next

What is planned is tracked as [milestones on GitHub](https://github.com/Peaperfish/prism/milestones)
and summarised in the README's [Roadmap](https://github.com/Peaperfish/prism#roadmap).

## Licences

Prism is under the PolyForm Noncommercial License 1.0.0. The third-party components and their
licences are listed in `THIRD_PARTY_NOTICES.txt` in each download.

Includes LibRaw {{LIBRAW_VERSION}} under CDDL 1.0; its source is attached to this release.

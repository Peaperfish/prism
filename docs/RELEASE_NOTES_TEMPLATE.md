<!-- The release workflow fills in {{VERSION}} and {{LIBRAW_VERSION}} and uses this as the
     draft release's notes. Edit the draft on GitHub before publishing if needed. -->

The first release of Prism's desktop app, for macOS and Windows. It is a pre-release: expect
rough edges.

## What it does

- Opens PNG, JPEG and camera RAW files (.arw, .dng, .cr2, .cr3, .nef, .raf), from File > Open
  or by dropping a file on the window.
- Shows a live preview that follows every slider as you drag it.
- Simple mode: presets for RAW and for PNG/JPEG, and sliders for the main adjustments, with
  undo and redo.
- Exports the result as PNG or JPEG.
- Light and dark themes, following the system by default.
- Preferences for the theme, the interface size and the panel width.
- The command-line tool `prism` is included in both downloads.

## Downloads

| File | For |
|---|---|
| `prism-{{VERSION}}-macos-universal.zip` | macOS 13.3 or later, Apple Silicon and Intel |
| `prism-{{VERSION}}-windows-x64.zip` | Windows 10 and 11, 64-bit |
| `LibRaw-{{LIBRAW_VERSION}}-source.tar.gz` | LibRaw's source code, as used in this release |
| `SHA256SUMS.txt` | SHA-256 checksums of the files above |

## Running the unsigned apps

Prism isn't signed by Apple or Microsoft yet, so both systems warn you the first time.

**macOS:** unzip, then drag Prism.app to Applications. The first time, right-click (or
Control-click) Prism.app and choose **Open**, then **Open** again. On macOS 15 and later that
no longer works: try to open Prism once, then go to **System Settings → Privacy & Security**,
scroll down to the message about Prism and click **Open Anyway**. After that it opens normally.

**Windows:** extract the whole zip, then run `prism_gui.exe`. If Microsoft Defender SmartScreen
says "Windows protected your PC", click **More info → Run anyway**. If Smart App Control is on
(Windows 11), it blocks unsigned apps and there is no way to allow just Prism.

## Known limitations

- No Advanced mode yet: only Simple mode's presets and sliders.
- File names in Chinese, Japanese or Korean show as `??` in the window (the file still opens
  and exports correctly).
- Opening a large RAW file freezes the window for a moment while it is decoded.

## Licences

Prism is under the PolyForm Noncommercial License 1.0.0. The third-party components and their
licences are listed in `THIRD_PARTY_NOTICES.txt` in each download.

Includes LibRaw {{LIBRAW_VERSION}} under CDDL 1.0; its source is attached to this release.

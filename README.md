# Prism

*Edit with light, not filters.*

[![CI](https://github.com/Peaperfish/prism/actions/workflows/ci.yml/badge.svg)](https://github.com/Peaperfish/prism/actions/workflows/ci.yml)

A small, fast command-line photo tool written in modern C++.

Prism loads an image (PNG, JPEG or camera RAW), runs it through a simple colour pipeline (exposure, a 3×3 colour matrix and a tone curve), and writes the result back out. It's a learning project for working directly with pixel buffers, colour maths and performance in C++20.

> 🚧 Work in progress.

## Why Prism

Mainstream photo editors often come with a subscription and a cloud catalog, hide what their sliders do, and sell looks as presets you can't read. Prism is built on four principles instead:

1. **Free for personal use.** No subscription, no cloud, no catalog: open a file, edit it, export it. (See [Licences](#licences).)
2. **It teaches as you go.** Every control should say in plain words what it does to the light, and you should be able to see each step happen. Planned for [0.2.0](https://github.com/Peaperfish/prism/milestone/1): a pipeline view, colour scopes and a test strip.
3. **No black-box presets.** A look should be a readable recipe you can see and adjust. Planned for [0.3.0](https://github.com/Peaperfish/prism/milestone/3): a plain-text recipe format, with the built-in presets rewritten as recipes.
4. **Physical controls.** You adjust light rather than apply effects. Exposure is already in stops; white balance in Kelvin, contrast as paper grades, and dodge and burn are planned for [0.4.0](https://github.com/Peaperfish/prism/milestone/4).

## Downloading and running Prism

Ready-made builds of the desktop app and the command-line tool are on the [Releases](https://github.com/Peaperfish/prism/releases) page:

| Download | For |
|---|---|
| `prism-<version>-macos-universal.zip` | macOS 13.3 or later, Apple Silicon and Intel |
| `prism-<version>-windows-x64.zip` | Windows 10 and 11, 64-bit |

Each zip holds the app (`Prism.app` on macOS, `prism_gui.exe` on Windows), the command-line tool (`prism`, `prism.exe`), Prism's `LICENSE.md`, `THIRD_PARTY_NOTICES.txt`, LibRaw's licence files in `licenses/LibRaw/` and a short `README.txt`. Both include RAW support. Nothing needs installing beside them.

The apps aren't signed by Apple or Microsoft yet, so both systems warn the first time:

- **macOS:** unzip and drag `Prism.app` to Applications. The first time, right-click (or Control-click) it and choose **Open**, then **Open** again. On macOS 15 and later that no longer works: try to open Prism once, then go to **System Settings → Privacy & Security**, scroll down to the message about Prism and click **Open Anyway**. After that it opens normally. (Only downloaded files are checked; a build of your own opens straight away.)
- **Windows:** extract the whole zip, then run `prism_gui.exe`. If Microsoft Defender SmartScreen says "Windows protected your PC", click **More info → Run anyway**. Smart App Control (Windows 11), when it is on, blocks unsigned apps with no way to allow just one.

Each release also has `LibRaw-<version>-source.tar.gz`, the LibRaw source the builds were made from, and `SHA256SUMS.txt`. How the downloads are built: [Release builds](#release-builds).

## Features

- [x] Load and save PNG / JPEG (via [stb](https://github.com/nothings/stb))
- [x] sRGB ↔ linear conversion
- [x] Exposure adjustment (in stops)
- [x] 3×3 colour matrix (e.g. camera → sRGB, or creative grades)
- [x] Saturation and contrast (luminance-preserving, gamut-limited saturation; log-space S-curve contrast)
- [x] Tone curves (gamma, filmic)
- [x] Histogram output (R, G, B and luma image, plus clipping summary)
- [x] RAW support (via [LibRaw](https://www.libraw.org): .arw, .dng, .cr2, .cr3, .nef, .raf; optional)
- [x] Multithreaded processing (standard library threads, no dependencies)
- [x] Camera matrices from camera profiles (`prism_profile`: LightCraft JSON, DNG files and DCP camera profiles)
- [ ] SIMD processing

## Usage

```sh
prism input.jpg -o output.png --exposure +0.5 --matrix samples/grade.txt --curve filmic --histogram hist.png

# Check clipping without writing an output image:
prism input.jpg --exposure +1 --histogram hist.png

# A Sony RAW file, at half size for a quick preview:
prism DSC03866.ARW --raw-half --exposure +1 --curve filmic -o preview.jpg

# ...with more colour and contrast, closer to the camera's JPEG:
prism DSC03866.ARW --raw-half --exposure +1 --saturation 1.25 --contrast 1.15 --curve filmic -o punchy.jpg

# Single-threaded, printing how long each stage took:
prism input.jpg -o output.jpg --curve filmic --threads 1 --time
```

At least one of `-o` and `--histogram` is required. Without `-o`, the full pipeline still runs and the histogram shows the processed result; only the output image is skipped.

| Option | Effect |
|--------|--------|
| `-o <file>` | Output file; `.png`, `.jpg` or `.jpeg` picks the format |
| `--exposure <stops>` | Multiply linear values by 2^stops (`+1` doubles, `-1` halves) |
| `--matrix <file>` | 3×3 colour matrix: 9 numbers, row-major, `#` starts a comment (see `samples/grade.txt`) |
| `--saturation <s>` | Scale each pixel's distance from its own luminance (`1` = unchanged, `0` = greyscale, up to `10`). Brightness and hue are kept; see [Saturation and contrast](#saturation-and-contrast) |
| `--contrast <c>` | S-curve around middle grey (0.18) in stops: `1` = unchanged, `1.15` = each stop near middle grey becomes 1.15 stops; `0.1` to `10` |
| `--curve <spec>` | `gamma:<value>` (>1 brightens midtones), `reinhard[:<white>]` (default white 4), or `filmic` (Hable) |
| `--histogram <file>` | Save a 512×200 histogram of the result (R, G, B overlaid additively over a grey Rec. 709 luma fill) and print the % of pixels clipped to 0 and 255 per channel |
| `--raw-half` | RAW input only: decode at half the width and height using LibRaw's half-size mode (no demosaicing). About twice as fast on an α7R V file |
| `--raw-camera-matrix <file>` | RAW input only: use your own 3×3 camera RGB → linear sRGB matrix (same file format as `--matrix`) instead of LibRaw's built-in one. See [RAW files](#raw-files) |
| `--threads <n>` | Use up to `n` threads (default: one per hardware thread; `1` = single-threaded). Images under ~1 MP always run on one thread, where starting threads would cost more than it saves. The output is bit-identical for any thread count |
| `--time` | Print how long each stage took (load, exposure, matrix, saturation, contrast, curve, save, histogram) and the total. A stage given its neutral value (e.g. `--exposure 0`) is skipped and not listed |

Options are always applied in pipeline order, whatever order you type them.

## Pipeline

```
load (8-bit sRGB) → linearise ─┐
                               ├→ exposure → colour matrix → saturation → contrast → tone curve → encode sRGB → save
load RAW (LibRaw, linear) ─────┘
```

All processing happens in 32-bit float, linear light. The histogram is measured on the final 8-bit sRGB values (what ends up in the file, before any JPEG compression), so it matches what a camera or Lightroom shows.

### Using the pipeline from C++

`prism::process` (`include/prism/process.hpp`) runs the whole pipeline in one call; the CLI uses it, so this order is written down in one place only (`lib/process.cpp`):

```cpp
prism::ProcessSettings settings;              // one std::optional per stage; empty = skipped
settings.exposure_stops = 0.5f;
settings.curve = prism::parse_curve("filmic");
prism::ProcessResult result = prism::process(img, settings, /*threads=*/0, stop,  // stop: a std::stop_token, or {}
    [](const prism::StageTime& t) { std::cout << t.name << ' ' << t.ms << " ms\n"; });
```

A stage set to its neutral value (exposure 0, the identity matrix, saturation 1, contrast 1) is skipped too, so it leaves every bit as it was; a tone curve always runs. The optional `std::stop_token` is checked before each stage: once a stop is requested, `process` returns `ProcessResult::cancelled`, with the stages so far applied and the rest not (a valid but unfinished image). The optional callback gets each stage's name and time.

`prism::downscale(img, max_long_side, threads)` (`include/prism/resize.hpp`) shrinks an image so its longer side is `max_long_side`, keeping the aspect ratio. Each output pixel is the average of the source area it covers (a box filter), in linear light. It never enlarges: an image that is already small enough comes back unchanged. On a 61 MP image, down to 2560 px takes 64 ms on one thread and 11 ms on 12 (see [Benchmark](#benchmark)).

## Saturation and contrast

Both work in linear light, after the colour matrix and before the tone curve. A RAW decoded with no picture style looks flatter and less colourful than the camera's JPEG, and these two controls put some of that back.

**Saturation.** Each pixel is split into its luminance `Y = 0.2126 R + 0.7152 G + 0.0722 B` (the Rec. 709 weights: how bright each sRGB primary looks) and its offset from grey, `RGB − Y`. Saturation scales only the offset: `out = Y + s · (RGB − Y)`. Brightness stays exactly the same, greys (offset 0) stay grey, and hue stays the same because all three offsets scale together.

Pushing a strong colour away from grey can send its weakest channel below zero, for example the blue of a saturated yellow. Light can't be negative, and clipping that channel to 0 later would also change the pixel's hue and brightness. So where `s` would do that, the pixel gets the largest factor that brings its weakest channel to exactly 0 instead: `k = Y / (Y − min(R, G, B))`. That's the most saturated colour of the same hue and brightness that sRGB can show. Pixels with `Y ≤ 0` become black, and `--saturation 1` leaves the image exactly as it was.

**Contrast.** Contrast is about *ratios* of light (stops), so it works in log2 space around middle grey, 0.18. A value `x` is `e = log2(x / 0.18)` stops from middle grey, and the curve is

```
u  = e / w                                 w = 2 stops
e' = e + (c − 1) · w · u / √(1 + u²)
```

Near middle grey the slope is `c`, so 1 stop becomes `c` stops. `u / √(1 + u²)` is an S-shaped function like `tanh` (slope 1 at 0, levelling off at ±1), but costs a square root where `tanh` costs about as much as the `log2` and `exp2` together. Further than a few `w` stops away it approaches ±1, so shadows and highlights move by at most `(c − 1) · w` stops instead of being stretched without limit: an S-curve. Because it acts on stops, a pixel one stop over middle grey and one stop under get mirror-image treatment at any brightness. The curve is strictly increasing for every `c > 0`, 0.18 never moves, and values `≤ 0` are scaled by the curve's slope at zero, so it stays continuous. It is applied to each channel separately, like the tone curves, so it also adds a little saturation when `c > 1`.

On the α7R V test file (`--raw-half --exposure +1 --curve filmic`), `--saturation 1.25 --contrast 1.15` puts 2.3% of pixels at blue = 0 (gamut-limited yellows and oranges, against 0.01% without) and nothing at 255.

## RAW files

RAW files are recognised by their extension (`.arw`, `.dng`, `.cr2`, `.cr3`, `.nef`, `.raf`, any case) and decoded with LibRaw into the same linear-light float image a PNG or JPEG becomes, so every other option works unchanged. See `lib/raw.cpp` for each LibRaw setting and why it is needed. In short:

- **Linear output.** No gamma curve (gamma 1/1) and no auto-brightening, in 16 bits per channel, converted to float in [0, 1]. The sensor's white level maps to exactly 1.0, so `--exposure` means the same thing for every shot.
- **As-shot white balance.** The camera's own white balance multipliers are used; LibRaw never guesses.
- **Linear sRGB primaries.** LibRaw converts camera RGB to sRGB with its matrix for the camera model. A DNG carries this matrix in the file. For ARW (and CR3, NEF, ...) LibRaw uses its built-in table, derived from Adobe's colour data.
- **Orientation.** The camera's orientation flag is applied, so portrait shots come out upright (and the width and height swap).
- **Highlights are clipped** at the sensor's white level, so blown areas stay neutral white.

**Expect a linear RAW to look darker, flatter and less saturated than the camera's JPEG.** The camera applies a contrasty tone curve and boosts saturation; Prism only applies what you ask for. Try `--exposure +1 --curve filmic` as a starting point. LibRaw also keeps a few dozen pixels of border that the camera crops off (an α7R V file is 9566×6374 here, against 9504×6336 for the camera JPEG).

### Your own camera matrix

`--raw-camera-matrix <file>` asks LibRaw for the camera's **native RGB** instead: white-balanced and demosaiced, but with no colour conversion. Prism then applies your matrix in float, without the clipping LibRaw's own conversion step does. The matrix must map **white-balanced camera RGB → linear sRGB (D65)**, row-major, like LibRaw's `rgb_cam`. Each row should sum to 1, so white stays white. It is *not* a DNG `ColorMatrix` (that one maps XYZ → camera). For reference, LibRaw 0.22's matrix for the α7R V (ILCE-7RM5) is:

```
 1.522197 -0.339775 -0.182423
-0.168875  1.632612 -0.463737
-0.012806 -0.417928  1.430735
```

Passing exactly this matrix reproduces Prism's default RAW output (mean difference 0.006 of an 8-bit level). That makes it a good check before trying your own.

### Camera profiles (`prism_profile`)

`prism_profile` turns a camera profile into a matrix file for `--raw-camera-matrix`. It reads [LightCraft](https://github.com/storytold/lightcraft) camera profiles (`assets/camera-profiles/<model>.json` there), DNG files and DCP camera profiles (see [DNG and DCP profiles](#dng-and-dcp-profiles)). It tells them apart by their first bytes, not the file name: JSON starts with `{`, a DNG or DCP is a TIFF file (`II` or `MM`, then 42 or `0x4352`). Anything else is refused with an error naming both formats.

```sh
./build/src/prism_profile ILCE-7RM5.json -o samples/a7rv_lightcraft.txt --note "LightCraft PR #3022"
prism DSC03866.ARW --raw-half --exposure +1 --curve filmic --raw-camera-matrix samples/a7rv_lightcraft.txt -o out.jpg
```

The output file starts with comments saying which profile it came from (plus any `--note` lines), the original matrix, and what was skipped. The profile's file name (without its directory), its model and the notes are cleaned as for DNG and DCP files (see below), so they can't add a matrix row. With `-o` the file has `\n` line endings on every OS, Windows included, so it is the same bytes wherever it was made; `load_matrix` also reads `\r\n` files. `samples/a7rv_lightcraft.txt` is the α7R V (ILCE-7RM5) profile from LightCraft PR #3022.

**What a LightCraft profile is.** One 3×3 matrix from white-balanced camera RGB to **linear Rec. 2020**, plus a hue/saturation/value table. Both were fitted (least squares over hundreds of thousands of pixels from many photos) so that the raw data matches **the camera's own JPEGs**. So the matrix reproduces the in-camera look (Sony "Standard": noticeably more saturated), and **it is not a colorimetric calibration** like a DNG profile measured against a colour chart. The hue/saturation/value table bends individual hues, which a 3×3 matrix can't do, so `prism_profile` skips it and says so.

**The matrix chain.** A matrix acts on the vector to its right, so a chain is read right to left, and order matters (matrix multiplication is not commutative):

```
camera → sRGB = normalize_rows( XYZ→sRGB · Rec2020→XYZ · M_lightcraft )
                                 ③           ②              ①
```

1. `M_lightcraft`: white-balanced camera RGB → linear Rec. 2020.
2. Rec. 2020 → CIE XYZ, the device-independent space every RGB space is defined against. Built from the Rec. 2020 primaries and the D65 white point (`rgb_to_xyz` in `lib/camera_profile.cpp`).
3. XYZ → linear sRGB: the inverse of sRGB → XYZ.

Rec. 2020 and sRGB share the same white (D65), so no chromatic adaptation is needed. Finally each row is divided by its sum, so camera (1, 1, 1), which is white after white balance, gives sRGB (1, 1, 1). LightCraft's rows sum to 0.997, 0.998 and 1.035, so this only removes a slight blue cast.

**DNG profiles** use the same machinery. The library reads their colour tags from DNG files and DCP camera profiles (`load_dng_profile` in `include/prism/dng_profile.hpp`):

- **ForwardMatrix** (white-balanced camera → XYZ under D50): `XYZ→sRGB · Bradford(D50→D65) · ForwardMatrix`. The Bradford step adapts D50 white to sRGB's D65 white. Skip it and white comes out slightly yellow.
- **ColorMatrix** (XYZ → camera, *before* white balance): `XYZ→sRGB · ColorMatrix⁻¹ · diag(ColorMatrix · D65)`. The diagonal term puts back the white balance, as a per-*input*-channel gain (columns), which is what white balance is. Just normalising the rows of `XYZ→sRGB · ColorMatrix⁻¹` would also make white come out white, but it would tint every other colour.
- With two calibrations (usually Standard light A, 2856 K, and D65, 6504 K), the tags are interpolated for a white balance temperature, linearly in 1/temperature, as DNG 1.7.1.0 chapter 6 says: both ForwardMatrices if both calibrations have one, else both ColorMatrices. A temperature outside the two uses the nearer calibration alone. A single calibration is used at any temperature.

#### DNG and DCP profiles

```sh
./build/src/prism_profile camera.dcp -o camera_dcp.txt
./build/src/prism_profile photo.dng -o photo_dng.txt --temperature 5000
```

A DCP holds one camera profile. A DNG carries its main profile in IFD 0, and that is the one read. The output file is the same matrix format as above: `#` comment lines, then three rows. The comments give the file name (without its directory), the format, the camera model, the profile name and copyright, the method (which matrices, interpolated or from one calibration, at what temperature, with the weights), and what was skipped or ignored. Every string taken from the file is cleaned first: control characters (newline, carriage return and tab included) become spaces, so a profile name can't end its comment line and add a matrix row; bytes from 0x80 up become `?`, so the header is plain ASCII; each string is cut to 120 characters, ending in `...`.

**What the matrix is.** It is a *colorimetric* matrix: the profile's ForwardMatrix (or ColorMatrix) describes how the camera's sensor responds to colour, measured against CIE XYZ, so the matrix maps camera RGB to the colours that were in front of the camera, as closely as a 3×3 matrix can. A DNG profile's *look* is somewhere else, in tables that a 3×3 matrix cannot represent: the hue/saturation map (`ProfileHueSatMapData1/2`), the look table (`ProfileLookTable`) and the tone curve (`ProfileToneCurve`). `prism_profile` skips them and lists them, with their sizes, in the header. Since the matrices describe the camera rather than the look, two profiles for the same camera that look very different (say a neutral one and a vivid one) can have the same matrices and differ only in their tables. `prism_profile` then gives both the same output. This is the opposite of a LightCraft profile, whose single matrix was fitted to the camera's JPEGs and so carries their look.

Not applied, and listed in the header when present: a DNG's `CameraCalibration1/2` (per-unit corrections) and `AnalogBalance`, and a third calibration (DNG 1.6), of which only the first two are used.

**`--temperature <K>`** (default 6504, D65) is the white balance temperature, in kelvin, at which the two ForwardMatrices are interpolated. It should be the colour temperature of the light the photo was taken in: Prism's RAW path white-balances with the camera's as-shot multipliers, and `prism_profile` does not read them from the photo. Only profiles with two ForwardMatrices accept a value other than 6504. A profile with one calibration uses it at every temperature, and ColorMatrices are always white-balanced for D65 here, so interpolating them for another light would give a matrix white-balanced for the wrong one (white balance for other lights is a later step). In both cases `--temperature` fails with an error explaining why, instead of being ignored. Two calibrations with the same temperature, or a ForwardMatrix in only one of them, are refused: there is no rule for interpolating them.

**ProfileEmbedPolicy.** Profiles carry usage rules in this tag (DNG 1.7.1.0, p. 52): 0 = allow copying, 1 = embed if used, 2 = embed never, 3 = no restrictions. The header gives the value and its meaning, and `prism_profile` always writes the matrix. What the specification says, and what `prism_profile` adds:

| Policy | The specification says | stderr and header |
|---|---|---|
| 0, 1 | "The camera profile may not be used to process non-DNG files" (1 applies "the same rules as 'allow copying'") | a warning: *"The DNG specification says profiles with ProfileEmbedPolicy 0 or 1, or 2 when embedded in a DNG, may not be used to process non-DNG files; using this matrix for non-DNG (RAW) files may not be permitted, and that is your responsibility."* |
| 2, in a DCP | stored profiles "can be used to process images but cannot be embedded in files" | a note: *"The DNG specification says this profile may not be embedded in files."* |
| 2, in a DNG | "If a camera profile is already embedded in a DNG file, then this value has the same restrictions as 'embed if used'" | the same warning as 0 and 1 |
| 3 | "no restrictions" | nothing |
| 4 and up | not defined | a warning: *"ProfileEmbedPolicy 4 is not defined by the DNG specification, so the profile's terms are unknown."* (with the actual value) |

| no tag | "Default: 0" (p. 51) | the same warning as 0, and the header says the tag is missing and the default applies |

No camera profile or DNG is in this repository, and none of their values are: the tests build small files with made-up matrices.

**α7R V results** (DSC03866.ARW, `--raw-half --exposure +1 --curve filmic`):

| | LibRaw built-in | LightCraft |
|---|---|---|
| Matrix | `1.522 -0.340 -0.182`<br>`-0.169 1.633 -0.464`<br>`-0.013 -0.418 1.431` | `2.147 -1.200 0.053`<br>`-0.262 1.415 -0.153`<br>`-0.050 -1.046 2.096` |
| Pixels < 0 after the matrix (R / G / B) | 0.001% / 0.000% / 0.012% | 0.062% / 0.000% / 5.461% |

Between the two outputs: mean CIE76 ΔE 5.7 (median 4.0, 95th percentile 16.7, max 53.9). The LightCraft version has clearly warmer, more saturated autumn foliage. Its blue row (`2.10·B − 1.05·G`) drives saturated yellows and oranges below zero in blue. Those colours lie outside sRGB, and the tone curve clips them to 0.

## GUI

A desktop app for macOS and Windows, in progress. `prism_gui` (`gui_app/`) is a window built with SDL3 and Dear ImGui; it is off by default and built with `-DPRISM_BUILD_GUI=ON` (see [Building the GUI](#building-the-gui)). So far it opens a photo (File → Open, Ctrl/Cmd+O, drag and drop, or a path on the command line), shows a live preview fitted to the window, edits it in Simple mode (a preset plus Brightness, Contrast and Colour, with undo and redo: Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z or Ctrl+Y) and exports it at full resolution as PNG or JPEG (File → Export, Ctrl/Cmd+E). Errors appear as a banner in the window. Help → About Prism (on macOS: Prism → About Prism) shows the version, Prism's licence and the third-party components with their full notices. Packaging is done: see [Downloading and running Prism](#downloading-and-running-prism). The pipeline view, the histogram and scopes, comparing stages and the rest are planned: see the [Roadmap](#roadmap).

### Themes and Preferences

Preferences (on macOS: Prism → Preferences…, Cmd+,; on Windows and Linux: Edit → Preferences…, Ctrl+,) is an ordinary window whose changes apply at once:

- **Appearance:** System (the default: follows the operating system's light or dark mode, also when it switches while Prism is open), Light or Dark. Whatever the theme, the canvas behind the photo stays a neutral grey (slightly lighter in Light), never white, black or tinted, so it doesn't sway how bright or colourful the photo looks.
- **Interface size:** Auto (the display's scale, e.g. 150% on a Windows laptop set to 150%) or 75–200% in steps of 25.
- **Panel width:** the control panel's width; it can also be dragged at its left edge. It never takes more than 40% of the window (or less than 240 points where there is room). "Reset layout" goes back to 320 points, "Restore defaults" resets everything.

They are kept in `preferences.txt`, key=value lines next to ImGui's `imgui.ini` in the folder SDL picks for the app: `~/Library/Application Support/Prism/Prism/` on macOS, `%APPDATA%\Prism\Prism\` on Windows, `~/.local/share/Prism/Prism/` on Linux. The file is written atomically (a temporary file renamed over it). A missing or damaged file, or a bad value, falls back to the default for that setting alone, and settings Prism doesn't know (from a newer version) are kept.

### GUI core

The app's logic lives without any window in the static library `prism_gui_core` (`gui_core/`, namespace `prism::gui`). It needs nothing beyond `prism_core` and the standard library, so it is always built and every CI job tests it, the ThreadSanitizer and AddressSanitizer jobs included (`prism_gui_core_tests`). The layers:

```
prism_gui (gui_app/: SDL3 + Dear ImGui)      draws and forwards clicks; never calls the pipeline itself
   ▲
prism_gui_core (gui_core/)                   no UI code, unit-tested
   ▲
prism_core (lib/)                            the pipeline, shared with the CLI
```

| Part | What it does |
|------|--------------|
| `settings.hpp` | `EditSettings`: every value the GUI edits (exposure, colour matrix file, saturation, contrast, tone curve, an on/off switch per stage, RAW options). `to_process_settings()` turns it into what `prism::process` takes, with exactly the CLI's curves; `validate()` explains bad values. Simple mode's brightness, contrast and colour sliders (−100…+100) are invertible maps onto three of its fields, so Simple and Advanced edit the same object |
| `presets.hpp` | Named looks (Natural, Original, Vivid, Soft, Black & white), different for RAW and PNG/JPEG. A preset sets only the look; settings that match no preset are "Custom" |
| `history.hpp` | Undo/redo of whole `EditSettings` snapshots. A slider drag, marked by `begin_gesture()` / `end_gesture()`, is one step; at most 100 steps |
| `session.hpp` | The open photo: `open_file()` decodes it (RAW at half size), shrinks it with `prism::downscale` to a 2560 px preview base and frees the full image; errors come back as a kind plus a sentence for the user. `export_image()` decodes the file again at full size, runs the pipeline with a stop token and saves PNG or JPEG |
| `preview_renderer.hpp` | A worker thread that re-renders the preview whenever the settings change. The newest request wins: it replaces one that hasn't started and stops the one in progress between stages (`std::stop_token`). The UI polls for finished 8-bit frames; nothing calls back into UI code |
| `live_preview.hpp` | Keeps the preview in step with the session for a UI that redraws every frame: submits a render only when the preview base or the settings changed, and says whether one is still in flight ("Rendering…") |
| `slider_gesture.hpp` | Turns a slider's per-frame state (pressed, moved, released) into History gestures, so a whole drag is one undo step, made on release |
| `background_export.hpp` | Runs `export_image()` on its own thread with a stop token, so the window keeps drawing while a full-resolution export runs; the UI polls for the result |
| `main_thread_queue.hpp` | A small locked queue that hands values from any thread to the UI thread, e.g. the answer of a file dialog whose callback runs on another thread |
| `preferences.hpp` | The app's own settings (theme, interface size, panel width): defaults, reading and writing the key=value file, clamping, and resolving "System" to light or dark |
| `layout.hpp` | Window layout as pure functions: the panel's width for a given window and scale, and shortening a file name in the middle to fit the status bar |
| `file_types.hpp` | The file dialogs' filters (RAW only in a build with LibRaw), and the export file name: the suggested name next to the source, and the extension added if the user left it out |

A full preview re-render (every stage plus the sRGB encode, 2560×1707 from a 61 MP image) takes 10 ms on 12 threads and 53 ms on one (`prism_bench`'s "preview render" row).

## Roadmap

v0.1.0 is the first pre-release of the desktop app. What's planned next is tracked as GitHub issues in four milestones.

[**0.2.0 – See the light**](https://github.com/Peaperfish/prism/milestone/1): understand what each step does to the light.

- [A histogram for any pipeline stage](https://github.com/Peaperfish/prism/issues/1), RGB and luminance
- [Colour scopes](https://github.com/Peaperfish/prism/issues/13): RGB parade/waveform and vectorscope, for any stage
- [A gamut warning](https://github.com/Peaperfish/prism/issues/14): colours outside sRGB, and which stage pushed them out
- [A pipeline view](https://github.com/Peaperfish/prism/issues/2): every stage visible and toggleable; click a stage to preview the photo at that point
- [Compare any two stages](https://github.com/Peaperfish/prism/issues/3), including the original, side by side or split
- [Clipping warnings](https://github.com/Peaperfish/prism/issues/4) that say which stage clipped
- [A test strip](https://github.com/Peaperfish/prism/issues/15): one image in bands at −1, −½, 0, +½ and +1 stop, to choose exposure by eye
- [A plain-language explanation](https://github.com/Peaperfish/prism/issues/16) for every control: what it does to the light
- [Smoother preview while dragging](https://github.com/Peaperfish/prism/issues/5): let a running render finish and replace only the waiting request

[**0.3.0 – Recipes**](https://github.com/Peaperfish/prism/milestone/3): readable, shareable recipes instead of black-box presets.

- [The .prism recipe format](https://github.com/Peaperfish/prism/issues/17): plain text, versioned, documented, diffable
- [Save and load recipes](https://github.com/Peaperfish/prism/issues/18) in the GUI
- [Batch editing from the CLI](https://github.com/Peaperfish/prism/issues/19): `prism <files> --recipe <file>`
- [The built-in presets as readable recipes](https://github.com/Peaperfish/prism/issues/20) that show their values

[**0.4.0 – Darkroom**](https://github.com/Peaperfish/prism/milestone/4): physical controls.

- [White balance in Kelvin and tint](https://github.com/Peaperfish/prism/issues/21), with a click-on-neutral picker
- [Contrast as paper grades](https://github.com/Peaperfish/prism/issues/23)
- [Dodge and burn](https://github.com/Peaperfish/prism/issues/22) with brushes (masks)

[**Later**](https://github.com/Peaperfish/prism/milestone/2), not scheduled yet:

- [Open files in the background](https://github.com/Peaperfish/prism/issues/6) with a Cancel button, instead of freezing on large RAW files
- [Show CJK and other non-Latin file names](https://github.com/Peaperfish/prism/issues/7) correctly
- [A "Discard edits?" prompt](https://github.com/Peaperfish/prism/issues/8) when opening another photo or quitting
- [zlib support](https://github.com/Peaperfish/prism/issues/9) for compressed DNGs
- [A Linux download](https://github.com/Peaperfish/prism/issues/10), starting with a tarball of the command-line tool
- [Signed apps](https://github.com/Peaperfish/prism/issues/11): a signed and notarized macOS app, a signed Windows app
- [Export progress per stage](https://github.com/Peaperfish/prism/issues/12)

## Performance

Every per-pixel stage (sRGB decode/encode, exposure, matrix, tone curve, histogram) splits the image into one contiguous block of rows per thread. Each thread writes only its own rows, so no locks are needed; the histogram gives each thread private counters and adds them up at the end. See `include/prism/parallel.hpp`.

Decoding and compressing the PNG/JPEG file itself (stb) is still single-threaded, so with `-o` the save step is the largest remaining cost.

### Benchmark

`prism_bench` times each stage on a synthetic 9504×6336 (61 MP) image held in memory, single-threaded and multithreaded, and reports the median of several runs. Always benchmark a Release build:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/bench/prism_bench                     # defaults: 9504x6336, 5 runs, all threads
./build-release/bench/prism_bench --size 4000x3000 --runs 9 --threads 8
```

It is not part of `ctest`. Results on an Apple M2 Pro (8 performance + 4 efficiency cores), in ms:

| Stage | 1 thread | 12 threads | Speedup |
|-------|---------:|-----------:|--------:|
| sRGB decode | 171 | 31 | 5.6× |
| exposure | 15 | 8 | 1.8× |
| matrix | 71 | 11 | 6.7× |
| saturation | 152 | 22 | 6.8× |
| contrast | 215 | 33 | 6.5× |
| tone curve (gamma:2.2) | 201 | 32 | 6.3× |
| tone curve (filmic) | 44 | 9 | 5.0× |
| sRGB encode | 159 | 23 | 7.0× |
| histogram | 236 | 31 | 7.7× |
| downscale to 2560 px | 64 | 11 | 5.9× |
| GUI preview render, 2560 px (all stages + encode) | 53 | 10 | 5.1× |

Contrast used to be the slowest per-pixel stage at 897 ms / 122 ms, because each value needed a `log2`, an `exp2` and a square root (and 1757 ms / 218 ms with `std::tanh` as the sigmoid). It now looks the curve up in a table: see [Fast contrast](#fast-contrast).

Before the lookup-table encoder (below), sRGB encode took 913 ms / 122 ms and the histogram 1069 ms / 134 ms (1 / 12 threads), so single-threaded they are now 5.7× and 4.5× faster.

With `PRISM_TEST_RAW=<file>` set, `prism_bench` also times loading that RAW file at full and half size. For a 61 MP α7R V ARW (lossless compressed, 88 MB), in ms:

| RAW load | 1 thread | 12 threads | Output |
|----------|---------:|-----------:|-------:|
| full size | 2186 | 1767 | 6374×9566 |
| half size (`--raw-half`) | 893 | 859 | 3187×4783 |

`--threads` only splits Prism's 16-bit → float conversion. Most of the time goes to reading and decompressing the file, which LibRaw does single-threaded, so half size is only about 2× faster even though it skips demosaicing.

Exposure barely speeds up because one multiply per value is so little work that it is limited by memory bandwidth, not by the CPU (it already moves ~100 GB/s on one core).

### Tone curves without `std::function`

`ToneCurve` used to hold a `std::function<float(float)>`, called once per value. A `std::function` can hold any callable, so the compiler only sees "call whatever is stored in here": an indirect call it can't inline. Every value paid for the call, and the loop couldn't be vectorised, since the compiler can't run 4 calls to unknown code in one instruction.

Each curve is now a small struct (`GammaCurve`, `ReinhardCurve`, `FilmicCurve` in `pipeline.hpp`) with an inline `operator()`, and `ToneCurve` holds a `std::variant` of them. `tone_curve` calls `std::visit` **once per image**. Its lambda is a template, so the compiler builds one loop per curve type, and in each loop it knows exactly which maths runs. It can then inline it and vectorise it. Reinhard and filmic are just multiplies, adds and divides, and they compile to NEON instructions that handle 4 floats at once (`fdiv.4s`). Gamma called `powf` from the system maths library at the time. The compiler can't see inside that or vectorise it, so gamma only saved the indirect call (it has since been vectorised too: see [Fast gamma](#fast-gamma)). (Templating the loop on the curve type by hand would compile to the same thing: `std::visit` is just a switch on the variant's tag that calls the template.)

Median of 9 runs at 60 MP, in ms (1 thread / 12 threads):

| Curve | `std::function` | `std::variant` | Speedup (1 thread) |
|-------|----------------:|---------------:|--------:|
| gamma:2.2 | 831 / 108 | 699 / 88 | 1.2× |
| reinhard | 210 / 30 | 27 / 8.5 | 7.7× |
| filmic | 305 / 41 | 44 / 8.8 | 6.9× |

On 12 threads, Reinhard and filmic now run at about 165 GB/s, the speed of just reading and writing the buffer, so they are limited by memory, not maths. A separate test of the three ways to call the curve (single-threaded, 60 M values, filmic) shows where the time went: `std::function` per value 152 ms, `std::visit` per value 109 ms (no `std::function`, but still a branch per value and no vectorisation), `std::visit` once with a typed loop 14 ms.

**The output is bit-identical.** Same formulas, same float operations in the same order. Vector division gives exactly the same result as scalar division, and the compiler doesn't contract `a * b + c` into a fused multiply-add any differently when the code is inlined. This was checked by hashing the old and new `tone_curve` output for all 2³² float bit patterns for gamma 2.2, Reinhard (white 4 and 1.5) and filmic: the hashes match. `ToneCurve.ImageMatchesMapBitForBit` checks that the vectorised loop matches the scalar `map()` over a million inputs covering every exponent, NaN and infinity.

**No lookup table.** A table only pays off when computing the value costs more than looking it up. Reinhard and filmic are now limited by memory, so a table could only add work.

### Fast sRGB encode

`linear_to_srgb8` used to call `pow()` for every channel of every pixel. The 8-bit output only changes at 255 points, so it now looks the answer up instead: a table of the 255 linear values where the output steps up, plus a small table indexed by the top bits of the float that lands within one comparison of the right answer. The thresholds are found by bisecting the original `pow` version (`linear_to_srgb8_reference`), so the result is bit-identical to it, not just close. A plain 8-step binary search over the thresholds is also exact but only ~1.4× faster than `pow`, because its loads depend on each other.

`prism_bench --exhaustive` compares the fast version with the reference for all 2³² float bit patterns (including NaNs and infinities) and takes about 2 s on 12 threads. With both `-o` and `--histogram`, the image is encoded once and the same bytes are saved and counted.

### Fast contrast

`contrast` used to call `std::log2` and `std::exp2` for every value. Now it builds a table of the curve's gain (`x' / x`) once per image and looks every value up in it:

- **Indexed by the float's bits.** The index is the top 17 bits of the float: sign, exponent and the first 8 mantissa bits. Positive floats sort the same as their bit patterns, and within one power of two the bits grow linearly with the value. That gives 256 table points per stop, at most 0.4% apart, across the whole float range.
- **Linear interpolation** between the two nearest points, using the remaining 15 mantissa bits. The gain changes smoothly with the stops, so a straight line between points 0.4% apart is very close.
- **Middle grey stays exact.** The points are shifted so that 0.18 sits exactly on one, where the gain is exactly 1.
- **No branches.** Keeping the sign bit in the index makes the table cover every bit pattern. The negative half holds the curve's slope at 0, so `x * gain` is right for negatives, zero and NaN too. The table is 512 KB, built in ~0.4 ms; only the parts real pixel values hit end up in the cache.

The result is no longer bit-identical to the formula, so the rule is: **after the filmic curve and sRGB encode, every value stays within 1/255 of the exact curve** (`contrast_reference`: the formula, with `std::log2` and `std::exp2`). `Contrast.StaysWithinOneLevelOfTheExactCurve` checks about a million inputs at 7 contrast values from 0.1 to 10, with and without filmic. `prism_bench --exhaustive` checks every float from 0 to 256 (above that every output is 255) at `c` = 0.1, 1.15 and 10, in about 20 s:

| `c` | Worst difference, filmic + sRGB | Worst, sRGB only | 8-bit outputs that differ (by 1) |
|----:|------:|------:|------:|
| 0.1 | 0.016 levels | 0.021 levels | 0.0002% |
| 1.15 | 0.003 levels | 0.004 levels | 0.00004% |
| 10 | 0.11 levels | 0.047 levels | 0.001% |

Median of 9 runs at 60 MP, `--contrast 1.15`, in ms:

| | 1 thread | 12 threads |
|---|---:|---:|
| `std::log2` + `std::exp2` per value | 897 | 122 |
| inline polynomial `log2` / `exp2` (vectorised) | 257 | 39 |
| lookup table | **215** | **33** |

The polynomial version computes `log2` from the float's exponent bits plus a short series for the mantissa, and `exp2` the other way round. It is vectorised: 4 values per instruction. But it still needs a division and a square root per value, and the table needs neither. The table can't be vectorised, because NEON has no instruction to load 4 values from 4 different table positions. It still wins, at about 4 cycles per value.

Two things that mattered along the way:

- **Capturing by reference.** The first polynomial version was *slower* than `std::log2` (1311 ms). The loop's lambda captured its constants by reference, so to the compiler they were just memory that writing a pixel might change. It had to reload them every iteration and decided vectorising wasn't worth it (`-Rpass-missed=loop-vectorize` says why). Capturing them by value fixed that.
- **Clamping the index.** Clamping the table index with `min` / `max` was ~10% slower than making every bit pattern a valid index.

### Fast gamma

`gamma:<g>` used to call `std::pow(x, 1/g)` per value, which can't be vectorised. It now computes `2^(log2(x) / g)` with `fast_log2` and `fast_exp2` (`include/prism/fast_math.hpp`), written so the compiler can inline them and vectorise the loop:

- **`fast_log2`** reads `x = 2^e · m` straight from the float's bits. The exponent `e` is already there, and `log2(m)` comes from a short series: `m` is in [√½, √2), where 4 terms are enough (error < 5e-8).
- **`fast_exp2`** splits `y` into a whole number `n` and a fraction `f`. It computes `2^f` with a degree-6 polynomial and multiplies by `2^n` by adding `n` to the result's exponent bits.
- No branches and no library calls: `x ≤ 0`, NaN, overflow and underflow are handled with selects. `x ≤ 0` gives 0, NaN stays NaN, results above the float range become infinity and results below ~2e-38 become 0. The CPU then runs 4 values per instruction.

The result is within 4e-7 of `std::pow` for gamma 2.2 (relative), and 8e-6 at gamma 0.1, since `log2`'s error is multiplied by 1/gamma. The same 1/255 rule as contrast applies, measured against `std::pow(max(x, 0), 1/g)`: `ToneCurve.GammaStaysWithinOneLevelOfPow` checks about a million inputs at 8 gamma values from 0.1 to 10. `prism_bench --exhaustive` checks every float from 0 to 256 at gamma 0.1, 2.2 and 10: the worst difference is **0.0001 of a level**, with or without filmic after it, and 0.0004% or fewer of 8-bit outputs differ (each by 1, values sitting exactly on a rounding boundary). `map()` and the image loop still match bit for bit.

Median of 9 runs at 60 MP, `gamma:2.2`, in ms:

| | 1 thread | 12 threads |
|---|---:|---:|
| `std::pow` per value | 699 | 88 |
| `fast_exp2(fast_log2(x) / g)` (vectorised) | **201** | **32** |

A lookup table like contrast's wasn't used here. Contrast's table loop alone takes ~215 ms, already slower than this: gamma has no square root or sigmoid, so the polynomial has less to do than it did for contrast. A table would also put 512 KB into `GammaCurve`, which is meant to be a small struct.

### Checking for data races

ThreadSanitizer instruments the build to report any two threads touching the same memory without synchronisation. Use a separate build directory:

```sh
cmake -S . -B build-tsan -DPRISM_SANITIZE_THREAD=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tsan
ctest --test-dir build-tsan
```

A race makes the test fail with a `WARNING: ThreadSanitizer: data race` report showing both threads' stack traces.

### Checking for memory errors and undefined behaviour

AddressSanitizer (ASan) reports reads and writes outside a buffer, use after free and memory leaks. UndefinedBehaviorSanitizer (UBSan) reports undefined behaviour such as signed integer overflow, out-of-range shifts and misaligned loads. They are built together, in their own build directory (they can't be combined with ThreadSanitizer):

```sh
cmake -S . -B build-asan -DPRISM_SANITIZE_ADDRESS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build-asan
```

ASan stops at the first error it finds. UBSan only prints a `runtime error:` line and carries on, unless `UBSAN_OPTIONS=halt_on_error=1` makes it fail the test.

## Building

Requires CMake 3.20+ and a C++20 compiler (GCC 11+, Clang 14+ or MSVC 2022 or later).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -j    # tests can run in parallel
```

With Visual Studio on Windows, the build type is chosen when building rather than when configuring (it is a multi-config generator): `cmake --build build --config Release` and `ctest --test-dir build -C Release`.

### Building the GUI

`-DPRISM_BUILD_GUI=ON` (default `OFF`) adds the desktop app, `prism_gui`. It downloads SDL 3.4.18 and Dear ImGui 1.93.0 at configure time (release tarballs, pinned with their SHA-256 in `cmake/GuiDependencies.cmake`) and links both statically, so the app needs no SDL library next to it. With the option off nothing is downloaded and the build is unchanged.

```sh
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Release -DPRISM_BUILD_GUI=ON
cmake --build build-gui --target prism_gui --config Release
open build-gui/gui_app/Prism.app               # macOS (or Prism.app/Contents/MacOS/Prism photo.jpg)
./build-gui/gui_app/prism_gui                  # Linux (optionally: a photo to open)
build-gui\gui_app\Release\prism_gui.exe         # Windows
```

On macOS the target is an app bundle, `Prism.app`, with its `Info.plist` (`packaging/macos/Info.plist.in`) and an icon; on Windows `prism_gui.exe` is a GUI program (no console window) with a version resource and an icon. The icon is a placeholder that the build draws (`packaging/make_icon.cpp`), so no image file is in the repo.

On Linux SDL needs the X11 and Wayland development packages to have a window to open; the CI job's `apt-get install` line lists them. A cold build takes about a minute and a half, most of it SDL.

### RAW support (LibRaw)

RAW support is optional. Install LibRaw and pkg-config (which CMake uses to find it) first:

```sh
brew install libraw pkg-config        # macOS
sudo apt install libraw-dev pkg-config  # Debian / Ubuntu
```

CMake then turns it on automatically (`-- RAW support: LibRaw 0.22.3` when configuring). To build without it, pass `-DPRISM_WITH_LIBRAW=OFF`. Prism still builds and passes all its tests, and RAW files fail with a clear error. If you install LibRaw after configuring a build directory without it, re-run cmake with `-DPRISM_WITH_LIBRAW=ON`. LibRaw is a private dependency of `prism_core`, like stb: only `lib/raw.cpp` includes its headers.

#### Building LibRaw from source (Windows)

Windows has no system LibRaw, so there CMake can build it instead:

```sh
cmake -S . -B build -DPRISM_LIBRAW_FROM_SOURCE=ON
cmake --build build --config Release
```

`-DPRISM_LIBRAW_FROM_SOURCE=ON` (default `OFF`) skips pkg-config, downloads the LibRaw 0.22.3 release tarball from libraw.org at configure time (pinned, with its SHA-256 checked) and builds it, unmodified, as a shared library: `libraw_r.dll` on Windows, `libraw_r.dylib` on macOS, `libraw_r.so` on Linux. `cmake/LibRaw.cmake` lists its 79 source files (those of LibRaw's own `Makefile.dist`). It is built without OpenMP, Little CMS, zlib or libjpeg, so DNGs compressed with deflate or lossy JPEG don't open; other RAW formats are unaffected. It works on every OS; the Windows CI job uses it, and Linux and macOS keep using pkg-config unless you turn it on.

Windows has no rpath, so after each build CMake copies `libraw_r.dll` next to `prism.exe`, `prism_profile.exe`, `prism_bench.exe` and `prism_tests.exe`; copy it along if you move them. On macOS and Linux the executables find the library in the build directory through their rpath.

**Licence:** LibRaw is dual-licensed under the [LGPL 2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) or the [CDDL 1.0](https://opensource.org/license/cddl-1-0), at your choice. Development builds link it dynamically and unmodified. The release downloads use it under CDDL 1.0, statically linked and unmodified (see [Licences](#licences)).

### Release builds

`-DPRISM_RELEASE=ON` (default `OFF`) is the configuration the downloads are built with. It builds LibRaw from the pinned tarball as above, but as a **static** library, and refuses anything else: turning `PRISM_LIBRAW_FROM_SOURCE` or `PRISM_WITH_LIBRAW` off, or pointing `FETCHCONTENT_SOURCE_DIR_LIBRAW` at a local tree, is a configure error, and `lib/raw.cpp` checks at compile time that the `libraw.h` it includes is the pinned version, so a system or Homebrew LibRaw can't slip in. The tarball is downloaded once to `<build>/LibRaw-<version>-source.tar.gz`, checked against the pinned SHA-256 and unpacked from there, so that file is exactly the source that was built. On Windows the C runtime is linked statically (`/MT`), so no Visual C++ redistributable is needed. On macOS a deployment target must be given. Without the option nothing of this is downloaded or built.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPRISM_RELEASE=ON -DPRISM_BUILD_GUI=ON \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"   # macOS
cmake --build build-release --parallel
PRISM_REQUIRE_RAW=1 ctest --test-dir build-release -j
packaging/macos/package.sh build-release dist "arm64 x86_64"
```

On Windows: `cmake -S . -B build-release -A x64 -DPRISM_RELEASE=ON -DPRISM_BUILD_GUI=ON`, build and test with `--config Release` / `-C Release`, then `packaging/windows/package.ps1 -Build build-release -Out dist`.

`cmake --install` lays out the download (`cmake/Package.cmake`), and the packaging scripts sign the macOS app ad hoc, check that the binaries use only the operating system's own libraries, check the LibRaw tarball's hash and that its licence files in the download are byte-identical to the tarball's, and zip it (`ditto` on macOS). macOS 13.3 is the minimum because Prism's settings files use `std::to_chars` for floats.

`.github/workflows/release.yml` does the same on GitHub for both systems after running every test with RAW support required. Run by hand (Actions → Release → Run workflow) it is a dry run that uploads the zips and the LibRaw tarball as workflow artifacts. Pushing a tag `v<version>`, which must match `project(prism VERSION …)` in `CMakeLists.txt`, the one place the version is set, also creates a **draft** pre-release with both zips, the LibRaw tarball and `SHA256SUMS.txt`, and notes from `docs/RELEASE_NOTES_TEMPLATE.md`; it is published by hand. The version also reaches `prism --version`, the About window, the macOS `Info.plist` and the Windows version resource.

### Continuous integration

`.github/workflows/ci.yml` builds and runs `ctest` on every push and pull request:

| Job | Compiler | Build | LibRaw |
|-----|----------|-------|--------|
| Linux GCC + LibRaw | GCC (ubuntu-latest) | Release | on (`apt install libraw-dev`) |
| Linux Clang | Clang (ubuntu-latest) | Release | off |
| macOS (Apple Clang) | Apple Clang (macos-latest) | Release | off |
| Linux Clang ThreadSanitizer | Clang (ubuntu-latest) | Debug, `PRISM_SANITIZE_THREAD=ON` | off |
| Linux Clang ASan + UBSan | Clang (ubuntu-latest) | Debug, `PRISM_SANITIZE_ADDRESS=ON` | off |
| Windows MSVC + LibRaw (source) | MSVC (windows-latest, Visual Studio generator) | Release | on (`PRISM_LIBRAW_FROM_SOURCE=ON`: `libraw_r.dll`) |
| GUI build (Linux / macOS / Windows) | default compiler of each runner | Release, `PRISM_BUILD_GUI=ON`, builds `prism_gui` only (no window is opened, no tests) | off |

There is no camera RAW file in CI, so the `RawFile.*` integration tests report as skipped. Instead the `RawSyntheticDng.*` tests write a tiny DNG (32×32 uniform grey Bayer pixels, from `tests/synthetic_dng.hpp`) and decode it, including from a non-ASCII path; in the jobs without LibRaw they check the "built without LibRaw" error instead. The two LibRaw jobs set `PRISM_REQUIRE_RAW=1`, so a build that lost RAW support fails. The Windows job also checks with `dumpbin` that `prism.exe` and `prism_tests.exe` import `libraw_r.dll` and that it sits next to them. The GoogleTest and LibRaw downloads are cached between runs. The release builds have their own workflow, `release.yml` (see [Release builds](#release-builds)); `ci.yml` doesn't build them.

### Testing with a real RAW file

RAW files are too large to keep in the repo, so the RAW integration tests (`RawFile.*` and `Cli.RawWithIdentityCameraMatrixRuns`) are skipped unless `PRISM_TEST_RAW` points to one. Optionally, `PRISM_TEST_RAW_SIZE` also checks the exact output size, after rotation:

```sh
PRISM_TEST_RAW=~/Pictures/DSC03866.ARW ctest --test-dir build -R Raw
PRISM_TEST_RAW=~/Pictures/DSC03866.ARW PRISM_TEST_RAW_SIZE=6374x9566 ctest --test-dir build -R Raw
```

They check the size, that every value is in [0, 1], that `--raw-half` halves the dimensions, and that `--raw-camera-matrix` with the identity matrix works.

Camera profiles work the same way: `PRISM_TEST_DCP` and `PRISM_TEST_DNG` point at a DCP and a DNG of your own. The tests check that the file parses and that `prism_profile` turns it into a matrix file that loads, with rows summing to 1. They print no values from the file.

```sh
PRISM_TEST_DCP=~/profiles/camera.dcp PRISM_TEST_DNG=~/Pictures/photo.dng ctest --test-dir build -R RealFile
```

### Code formatting

The C++ style is described in `.clang-format`. Format the files you changed with `clang-format -i <files>`, or the whole tree (Prism's own code only; `third_party/` stays unmodified) with:

```sh
git ls-files '*.cpp' '*.hpp' ':!third_party' | xargs clang-format -i
```

The one-off commit that reformatted everything is listed in `.git-blame-ignore-revs`. To make `git blame` skip it and show the commit that really last changed each line, run once per clone:

```sh
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

## Project layout

```
prism/
├── CMakeLists.txt
├── cmake/        # LibRaw.cmake: LibRaw source build; GuiDependencies.cmake: SDL3 + Dear ImGui;
│                 # ThirdPartyNotices.cmake, Package.cmake, WindowsResources.cmake: releases
├── src/          # CLI entry points: prism, prism_profile
├── gui_core/     # prism_gui_core: the GUI's logic without a window
├── gui_app/      # prism_gui: the desktop app's window (SDL3 + Dear ImGui; PRISM_BUILD_GUI=ON)
├── include/prism # public headers
├── lib/          # core image + colour code (raw.cpp: LibRaw; camera_profile.cpp: matrix maths)
├── tests/        # GoogleTest unit tests (tests/gui/: prism_gui_core_tests)
├── samples/      # example matrix files
├── bench/        # prism_bench: per-stage timings on a 61 MP image
├── packaging/    # Info.plist, Windows version resource, icon drawing, README.txt, package scripts
├── docs/         # release notes template
├── .github/      # CI and release workflows
└── third_party/  # stb headers
```

## Licences

Prism is licensed under the PolyForm Noncommercial License 1.0.0 (SPDX identifier `PolyForm-Noncommercial-1.0.0`); the full text is in [LICENSE.md](LICENSE.md).

In short: you may use, copy, change and share Prism for any noncommercial purpose, as long as anyone you give it to also gets the licence terms and the `Required Notice:` line, but commercial use is not allowed.

This is a source-available licence, not an open-source one: because it restricts commercial use, it is not approved by the Open Source Initiative (OSI).

**LibRaw in the downloads.** The release downloads include [LibRaw](https://www.libraw.org) 0.22.3, which is offered under the LGPL 2.1 or the CDDL 1.0; Prism uses it under the **CDDL 1.0**, statically linked and unmodified. Its licence files, `LICENSE.CDDL` and `COPYRIGHT`, are in each download's `licenses/LibRaw/` folder, unchanged. Its source code is attached to each release on GitHub as `LibRaw-<version>-source.tar.gz` (the exact tarball the release was built from) and is available from libraw.org. Each download's `THIRD_PARTY_NOTICES.txt`, generated by the build from what it links (`cmake/ThirdPartyNotices.cmake`), lists every component with its licence; the About window shows the same.

### Third-party software

| Name | Licence | How Prism uses it |
|------|---------|-------------------|
| [stb](https://github.com/nothings/stb) (`stb_image.h` v2.30, `stb_image_write.h` v1.16) | MIT or public domain (Unlicense), at your choice | Vendored, unmodified, in `third_party/stb/` and compiled into `prism_core` to read and write PNG and JPEG |
| [GoogleTest](https://github.com/google/googletest) v1.17.0 | BSD 3-Clause | Tests only: downloaded at configure time by CMake `FetchContent` and linked into `prism_tests`. Not in the repo and not linked into `prism` or `prism_profile` |
| [LibRaw](https://www.libraw.org) 0.22.3 | LGPL 2.1 or CDDL 1.0, at your choice; the release downloads use CDDL 1.0 | Optional (`PRISM_WITH_LIBRAW`). Release downloads (`PRISM_RELEASE=ON`): built from the pinned tarball and linked statically, unmodified, with `LICENSE.CDDL` and `COPYRIGHT` shipped in `licenses/LibRaw/`. Other builds: a separate shared library that `prism_core` links dynamically. On Linux and macOS: found on the system with pkg-config (`libraw_r`), not bundled. On Windows (and with `PRISM_LIBRAW_FROM_SOURCE=ON` anywhere): the unmodified release tarball is downloaded at configure time and built as the bundled shared `libraw_r.dll` (`.dylib`, `.so`), which is copied next to the executables. Not in the repo. Its licence files, `LICENSE.LGPL`, `LICENSE.CDDL` and `COPYRIGHT` (which also lists the code LibRaw contains from others, e.g. dcraw), are in the unpacked source: `build/_deps/libraw-src/`, or `<FETCHCONTENT_BASE_DIR>/libraw-src/` when that is set (CI: `.deps/libraw-src/`) |
| [SDL](https://www.libsdl.org) 3.4.18 | zlib (a few files it compiles carry their own permissive notices, e.g. fdlibm and yuv2rgb; `THIRD_PARTY_NOTICES.txt` reproduces them) | GUI only (`PRISM_BUILD_GUI=ON`): downloaded at configure time and linked statically into `prism_gui`, with audio, camera, joystick/HIDAPI, haptic, sensor and tray support turned off. Not in the repo |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.93.0 | MIT | GUI only: downloaded at configure time and compiled into `prism_gui` with its SDL3 and SDL_Renderer backends. Not in the repo. It embeds the fonts the interface uses, ProggyClean and ProggyForever (MIT) |
| [Little CMS 2](https://www.littlecms.com) | MIT | Not used by Prism's code: the system LibRaw's pkg-config file requires it, so builds with a system LibRaw also link it. Not bundled, and not used by the LibRaw source build |

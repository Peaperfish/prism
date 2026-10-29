# Prism

A small, fast command-line photo tool written in modern C++.

Prism loads an image (PNG, JPEG or camera RAW), runs it through a simple colour pipeline (exposure, a 3×3 colour matrix and a tone curve), and writes the result back out. It's a learning project for working directly with pixel buffers, colour maths and performance in C++20.

> 🚧 Work in progress.

## Features

- [x] Load and save PNG / JPEG (via [stb](https://github.com/nothings/stb))
- [x] sRGB ↔ linear conversion
- [x] Exposure adjustment (in stops)
- [x] 3×3 colour matrix (e.g. camera → sRGB, or creative grades)
- [x] Tone curves (gamma, filmic)
- [x] Histogram output (R, G, B and luma image, plus clipping summary)
- [x] RAW support (via [LibRaw](https://www.libraw.org): .arw, .dng, .cr2, .cr3, .nef, .raf; optional)
- [x] Multithreaded processing (standard library threads, no dependencies)
- [x] Camera matrices from camera profiles (`prism_profile`: LightCraft JSON; DNG ForwardMatrix / ColorMatrix maths in the library)
- [ ] SIMD processing

## Usage

```sh
prism input.jpg -o output.png --exposure +0.5 --matrix samples/grade.txt --curve filmic --histogram hist.png

# Check clipping without writing an output image:
prism input.jpg --exposure +1 --histogram hist.png

# A Sony RAW file, at half size for a quick preview:
prism DSC03866.ARW --raw-half --exposure +1 --curve filmic -o preview.jpg

# Single-threaded, printing how long each stage took:
prism input.jpg -o output.jpg --curve filmic --threads 1 --time
```

At least one of `-o` and `--histogram` is required. Without `-o`, the full pipeline still runs and the histogram shows the processed result; only the output image is skipped.

| Option | Effect |
|--------|--------|
| `-o <file>` | Output file; `.png`, `.jpg` or `.jpeg` picks the format |
| `--exposure <stops>` | Multiply linear values by 2^stops (`+1` doubles, `-1` halves) |
| `--matrix <file>` | 3×3 colour matrix: 9 numbers, row-major, `#` starts a comment (see `samples/grade.txt`) |
| `--curve <spec>` | `gamma:<value>` (>1 brightens midtones), `reinhard[:<white>]` (default white 4), or `filmic` (Hable) |
| `--histogram <file>` | Save a 512×200 histogram of the result (R, G, B overlaid additively over a grey Rec. 709 luma fill) and print the % of pixels clipped to 0 and 255 per channel |
| `--raw-half` | RAW input only: decode at half the width and height using LibRaw's half-size mode (no demosaicing). About twice as fast on an α7R V file |
| `--raw-camera-matrix <file>` | RAW input only: use your own 3×3 camera RGB → linear sRGB matrix (same file format as `--matrix`) instead of LibRaw's built-in one. See [RAW files](#raw-files) |
| `--threads <n>` | Use up to `n` threads (default: one per hardware thread; `1` = single-threaded). Images under ~1 MP always run on one thread, where starting threads would cost more than it saves. The output is bit-identical for any thread count |
| `--time` | Print how long each stage took (load, exposure, matrix, curve, save, histogram) and the total |

Options are always applied in pipeline order, whatever order you type them.

## Pipeline

```
load (8-bit sRGB) → linearise ─┐
                               ├→ exposure → colour matrix → tone curve → encode sRGB → save
load RAW (LibRaw, linear) ─────┘
```

All processing happens in 32-bit float, linear light. The histogram is measured on the final 8-bit sRGB values (what ends up in the file, before any JPEG compression), so it matches what a camera or Lightroom shows.

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

`prism_profile` turns a camera profile into a matrix file for `--raw-camera-matrix`. It currently reads [LightCraft](https://github.com/storytold/lightcraft) camera profiles (`assets/camera-profiles/<model>.json` there):

```sh
./build/src/prism_profile ILCE-7RM5.json -o samples/a7rv_lightcraft.txt --note "LightCraft PR #3022"
prism DSC03866.ARW --raw-half --exposure +1 --curve filmic --raw-camera-matrix samples/a7rv_lightcraft.txt -o out.jpg
```

The output file starts with comments saying which profile it came from (plus any `--note` lines), the original matrix, and what was skipped. `samples/a7rv_lightcraft.txt` is the α7R V (ILCE-7RM5) profile from LightCraft PR #3022.

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

**DNG profiles** (in the library, not yet read from files by `prism_profile`) use the same machinery:

- **ForwardMatrix** (white-balanced camera → XYZ under D50): `XYZ→sRGB · Bradford(D50→D65) · ForwardMatrix`. The Bradford step adapts D50 white to sRGB's D65 white. Skip it and white comes out slightly yellow.
- **ColorMatrix** (XYZ → camera, *before* white balance): `XYZ→sRGB · ColorMatrix⁻¹ · diag(ColorMatrix · D65)`. The diagonal term puts back the white balance, as a per-*input*-channel gain (columns), which is what white balance is. Just normalising the rows of `XYZ→sRGB · ColorMatrix⁻¹` would also make white come out white, but it would tint every other colour.
- With two calibrations, the one whose illuminant is closest to D65 (in mireds) is used, preferring its ForwardMatrix.

**α7R V results** (DSC03866.ARW, `--raw-half --exposure +1 --curve filmic`):

| | LibRaw built-in | LightCraft |
|---|---|---|
| Matrix | `1.522 -0.340 -0.182`<br>`-0.169 1.633 -0.464`<br>`-0.013 -0.418 1.431` | `2.147 -1.200 0.053`<br>`-0.262 1.415 -0.153`<br>`-0.050 -1.046 2.096` |
| Pixels < 0 after the matrix (R / G / B) | 0.001% / 0.000% / 0.012% | 0.062% / 0.000% / 5.461% |

Between the two outputs: mean CIE76 ΔE 5.7 (median 4.0, 95th percentile 16.7, max 53.9). The LightCraft version has clearly warmer, more saturated autumn foliage. Its blue row (`2.10·B − 1.05·G`) drives saturated yellows and oranges below zero in blue. Those colours lie outside sRGB, and the tone curve clips them to 0.

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
| tone curve (filmic) | 305 | 42 | 7.3× |
| sRGB encode | 159 | 23 | 7.0× |
| histogram | 236 | 31 | 7.7× |

Before the lookup-table encoder (below), sRGB encode took 913 ms / 122 ms and the histogram 1069 ms / 134 ms (1 / 12 threads), so single-threaded they are now 5.7× and 4.5× faster.

With `PRISM_TEST_RAW=<file>` set, `prism_bench` also times loading that RAW file at full and half size. For a 61 MP α7R V ARW (lossless compressed, 88 MB), in ms:

| RAW load | 1 thread | 12 threads | Output |
|----------|---------:|-----------:|-------:|
| full size | 2186 | 1767 | 6374×9566 |
| half size (`--raw-half`) | 893 | 859 | 3187×4783 |

`--threads` only splits Prism's 16-bit → float conversion. Most of the time goes to reading and decompressing the file, which LibRaw does single-threaded, so half size is only about 2× faster even though it skips demosaicing.

Exposure barely speeds up because one multiply per value is so little work that it is limited by memory bandwidth, not by the CPU (it already moves ~100 GB/s on one core).

### Fast sRGB encode

`linear_to_srgb8` used to call `pow()` for every channel of every pixel. The 8-bit output only changes at 255 points, so it now looks the answer up instead: a table of the 255 linear values where the output steps up, plus a small table indexed by the top bits of the float that lands within one comparison of the right answer. The thresholds are found by bisecting the original `pow` version (`linear_to_srgb8_reference`), so the result is bit-identical to it, not just close. A plain 8-step binary search over the thresholds is also exact but only ~1.4× faster than `pow`, because its loads depend on each other.

`prism_bench --exhaustive` compares the fast version with the reference for all 2³² float bit patterns (including NaNs and infinities) and takes about 2 s on 12 threads. With both `-o` and `--histogram`, the image is encoded once and the same bytes are saved and counted.

### Checking for data races

ThreadSanitizer instruments the build to report any two threads touching the same memory without synchronisation. Use a separate build directory:

```sh
cmake -S . -B build-tsan -DPRISM_SANITIZE_THREAD=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tsan
ctest --test-dir build-tsan
```

A race makes the test fail with a `WARNING: ThreadSanitizer: data race` report showing both threads' stack traces.

## Building

Requires CMake 3.20+ and a C++20 compiler (GCC 11+, Clang 14+ or MSVC 2022).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

### RAW support (LibRaw)

RAW support is optional. Install LibRaw and pkg-config (which CMake uses to find it) first:

```sh
brew install libraw pkg-config        # macOS
sudo apt install libraw-dev pkg-config  # Debian / Ubuntu
```

CMake then turns it on automatically (`-- RAW support: LibRaw 0.22.3` when configuring). To build without it, pass `-DPRISM_WITH_LIBRAW=OFF`. Prism still builds and passes all its tests, and RAW files fail with a clear error. If you install LibRaw after configuring a build directory without it, re-run cmake with `-DPRISM_WITH_LIBRAW=ON`. LibRaw is a private dependency of `prism_core`, like stb: only `lib/raw.cpp` includes its headers.

**Licence:** LibRaw is dual-licensed under the [LGPL 2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) or the [CDDL 1.0](https://opensource.org/license/cddl-1-0), at your choice. Prism links it dynamically (the Homebrew `libraw_r` shared library) and does not modify it, which the LGPL allows. If you distribute Prism binaries built with LibRaw, include LibRaw's copyright notice and licence, and let users swap in their own build of the library (dynamic linking already allows that). LibRaw in turn uses Little CMS (MIT licence).

### Testing with a real RAW file

RAW files are too large to keep in the repo, so the RAW integration tests (`RawFile.*` and `Cli.RawWithIdentityCameraMatrixRuns`) are skipped unless `PRISM_TEST_RAW` points to one. Optionally, `PRISM_TEST_RAW_SIZE` also checks the exact output size, after rotation:

```sh
PRISM_TEST_RAW=~/Pictures/DSC03866.ARW ctest --test-dir build -R Raw
PRISM_TEST_RAW=~/Pictures/DSC03866.ARW PRISM_TEST_RAW_SIZE=6374x9566 ctest --test-dir build -R Raw
```

They check the size, that every value is in [0, 1], that `--raw-half` halves the dimensions, and that `--raw-camera-matrix` with the identity matrix works.

## Project layout

```
prism/
├── CMakeLists.txt
├── src/          # CLI entry points: prism, prism_profile
├── include/prism # public headers
├── lib/          # core image + colour code (raw.cpp: LibRaw; camera_profile.cpp: matrix maths)
├── tests/        # GoogleTest unit tests
├── samples/      # example matrix files
├── bench/        # prism_bench: per-stage timings on a 61 MP image
└── third_party/  # stb headers
```

## Licence

Source-available for **non-commercial use only**. See [LICENSE](LICENSE).

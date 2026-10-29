# Prism

A small, fast command-line photo tool written in modern C++.

Prism loads an image, runs it through a simple colour pipeline (exposure, a 3×3 colour matrix and a tone curve), and writes the result back out. It's a learning project for working directly with pixel buffers, colour maths and performance in C++20.

> 🚧 Work in progress.

## Features

- [x] Load and save PNG / JPEG (via [stb](https://github.com/nothings/stb))
- [x] sRGB ↔ linear conversion
- [x] Exposure adjustment (in stops)
- [x] 3×3 colour matrix (e.g. camera → sRGB, or creative grades)
- [x] Tone curves (gamma, filmic)
- [x] Histogram output (R, G, B and luma image, plus clipping summary)
- [ ] RAW support (via LibRaw)
- [ ] Multithreaded / SIMD processing

## Usage

```sh
prism input.jpg -o output.png --exposure +0.5 --matrix samples/grade.txt --curve filmic --histogram hist.png
```

| Option | Effect |
|--------|--------|
| `-o <file>` | Output file; `.png`, `.jpg` or `.jpeg` picks the format |
| `--exposure <stops>` | Multiply linear values by 2^stops (`+1` doubles, `-1` halves) |
| `--matrix <file>` | 3×3 colour matrix: 9 numbers, row-major, `#` starts a comment (see `samples/grade.txt`) |
| `--curve <spec>` | `gamma:<value>` (>1 brightens midtones), `reinhard[:<white>]` (default white 4), or `filmic` (Hable) |
| `--histogram <file>` | Save a 512×200 histogram of the result (R, G, B overlaid additively over a grey Rec. 709 luma fill) and print the % of pixels clipped to 0 and 255 per channel |

Options are always applied in pipeline order, whatever order you type them.

## Pipeline

```
load (8-bit sRGB) → linearise → exposure → colour matrix → tone curve → encode sRGB → save
```

All processing happens in 32-bit float, linear light. The histogram is measured on the final 8-bit sRGB values (what ends up in the file, before any JPEG compression), so it matches what a camera or Lightroom shows.

## Building

Requires CMake 3.20+ and a C++20 compiler (GCC 11+, Clang 14+ or MSVC 2022).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Project layout

```
prism/
├── CMakeLists.txt
├── src/          # CLI entry point
├── include/prism # public headers
├── lib/          # core image + colour code
├── tests/        # GoogleTest unit tests
└── third_party/  # stb headers
```

## Licence

Source-available for **non-commercial use only**. See [LICENSE](LICENSE).

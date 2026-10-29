# Prism

A small, fast command-line photo tool written in modern C++.

Prism loads an image, runs it through a simple colour pipeline (exposure, a 3×3 colour matrix and a tone curve), and writes the result back out. It's a learning project for working directly with pixel buffers, colour maths and performance in C++20.

> 🚧 Work in progress.

## Features

- [x] Load and save PNG / JPEG (via [stb](https://github.com/nothings/stb))
- [x] sRGB ↔ linear conversion
- [ ] Exposure adjustment (in stops)
- [ ] 3×3 colour matrix (e.g. camera → sRGB, or creative grades)
- [ ] Tone curves (gamma, filmic)
- [ ] Histogram output
- [ ] RAW support (via LibRaw)
- [ ] Multithreaded / SIMD processing

## Usage

```sh
prism input.jpg -o output.png --exposure +0.5 --matrix grade.txt --curve gamma:2.2
```

## Pipeline

```
load (8-bit sRGB) → linearise → exposure → colour matrix → tone curve → encode sRGB → save
```

All processing happens in 32-bit float, linear light.

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

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
- [x] Multithreaded processing (standard library threads, no dependencies)
- [ ] SIMD processing

## Usage

```sh
prism input.jpg -o output.png --exposure +0.5 --matrix samples/grade.txt --curve filmic --histogram hist.png

# Check clipping without writing an output image:
prism input.jpg --exposure +1 --histogram hist.png

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
| `--threads <n>` | Use up to `n` threads (default: one per hardware thread; `1` = single-threaded). Images under ~1 MP always run on one thread, where starting threads would cost more than it saves. The output is bit-identical for any thread count |
| `--time` | Print how long each stage took (load, exposure, matrix, curve, save, histogram) and the total |

Options are always applied in pipeline order, whatever order you type them.

## Pipeline

```
load (8-bit sRGB) → linearise → exposure → colour matrix → tone curve → encode sRGB → save
```

All processing happens in 32-bit float, linear light. The histogram is measured on the final 8-bit sRGB values (what ends up in the file, before any JPEG compression), so it matches what a camera or Lightroom shows.

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
| sRGB encode | 925 | 119 | 7.8× |
| histogram | 1075 | 137 | 7.9× |

Exposure barely speeds up because one multiply per value is so little work that it is limited by memory bandwidth, not by the CPU (it already moves ~100 GB/s on one core).

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

## Project layout

```
prism/
├── CMakeLists.txt
├── src/          # CLI entry point
├── include/prism # public headers
├── lib/          # core image + colour code
├── tests/        # GoogleTest unit tests
├── bench/        # prism_bench: per-stage timings on a 61 MP image
└── third_party/  # stb headers
```

## Licence

Source-available for **non-commercial use only**. See [LICENSE](LICENSE).

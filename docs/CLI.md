# Command-line reference

Prism has two command-line tools. The download holds `prism` (`prism.exe` on Windows); `prism_profile` is built from the source (see the README) and sits next to `prism` in the build folder. `prism` runs a photo through the same pipeline as the app and writes the result. `prism_profile` turns a camera profile into a matrix file for `prism`. For the app, see [GUIDE.md](GUIDE.md).

`prism --help` prints the usage text, but to **stderr**, not stdout (exit code 0). To page it, redirect: `prism --help 2>&1 | less`.

Contents: [prism](#prism) · [Options of prism](#options-of-prism) · [Examples](#examples) · [Exit codes](#exit-codes) · [prism_profile](#prism_profile)

## prism

```
prism <input> [-o <output>] [--histogram <file>] [options]
```

`prism` loads `<input>`, applies the options you give, and writes `<output>`. The input is a PNG, a JPEG or a camera RAW file (`.arw`, `.dng`, `.cr2`, `.cr3`, `.nef`, `.raf`, in any case). The output is `.png`, `.jpg` or `.jpeg`, chosen by the extension of the name you give with `-o`. JPEGs are saved at quality 95. If `<output>` exists, it is replaced.

- Exactly one input file is allowed, and it must be given (anything else that doesn't start with `-` is an error).
- At least one of `-o` and `--histogram` is required. With only `--histogram`, the whole pipeline still runs and the histogram shows the processed result, but no image is written. That is a quick way to check clipping before you export.
- Every option except `--help`, `--version`, `--time`, `--raw-half` takes the next argument as its value, even if it starts with a minus sign, so `--exposure -1` works.
- An option given twice is an error.
- **Options are always applied in pipeline order**, whatever order you type them: exposure, colour matrix, saturation, contrast, tone curve. See [How Prism thinks about light](GUIDE.md#how-prism-thinks-about-light).
- A stage left at its neutral value is skipped: exposure 0, the identity matrix, saturation 1 and contrast 1 leave every bit as it was. A tone curve always runs when you give one.
- All processing is in linear light, in floating point. PNG and JPEG inputs are converted to linear light first and to 8-bit sRGB at the end. A RAW file is decoded to linear light with the camera's as-shot white balance (see the [README](../README.md#raw-files) for the details). A RAW file looks darker and flatter than the camera's JPEG until you add exposure and a tone curve; `--exposure +1 --curve filmic` is a good start.

After a successful run, `prism` prints `wrote <file> (<width>x<height>)` for the output image, `wrote <file> (histogram)` for the histogram, and the clipping line described under `--histogram`.

## Options of prism

### `-o <file>`

The output image. The extension (`.png`, `.jpg` or `.jpeg`, in any case) picks the format; any other extension fails with "unsupported extension" when the file is saved, after the processing has run. No default.

### `--exposure <stops>`

Brighten (`+`) or darken (`-`) by this many stops. Every value is multiplied by 2 to the power of the stops, in linear light: `+1` doubles the light, `-1` halves it, `+0.5` is about 1.4×. Any finite number is accepted. Default: not applied.

### `--matrix <file>`

Apply a 3×3 colour matrix, in linear light, after exposure. The file holds 9 numbers, separated by spaces or line breaks, row-major: the first three make the first row, which says how the new red is made from the old red, green and blue, and so on. `#` starts a comment that runs to the end of the line. Exactly 9 numbers are required; anything else fails with an error that names the file. Default: not applied. See `samples/grade.txt` in the repository for an example (a mild warm grade).

### `--saturation <s>`

Scale each pixel's colourfulness around its own luminance. `1` leaves the image as it is, `0` makes it greyscale, `2` doubles the distance from grey. Brightness and hue are kept, and a colour that would go below zero stops at the most saturated colour sRGB can show. Accepts 0 to 10. Default: not applied (same as 1).

### `--contrast <c>`

An S-curve around middle grey (0.18), in stops. `1` leaves the image as it is; `1.15` makes each stop near middle grey 1.15 stops wide; below 1 is less contrast. The effect fades beyond about 2 stops from middle grey. Accepts 0.1 to 10. Default: not applied (same as 1).

### `--curve <spec>`

Apply a tone curve, after contrast. The spec is one of:

| Spec | Curve |
|---|---|
| `gamma:<value>` | `out = in^(1/value)`. Above 1 brightens the midtones, below 1 darkens them. `<value>` must be a number above 0; it is required |
| `reinhard` or `reinhard:<white>` | Rolls highlights off smoothly. `<white>` is the linear value that lands exactly on white (brighter values clip), above 0. Default white: 4 |
| `filmic` | Soft shadows and highlights (the "Uncharted 2" curve by John Hable). No parameter |

An unknown or malformed spec fails with an error that lists the available curves. Default: no curve.

### `--histogram <file>`

Save a histogram image of the result, 512 pixels wide and 200 high (red, green and blue overlaid over a grey fill of the luminance), and print the percentage of pixels clipped at 0 and at 255 for each channel:

```
clipped (0 / 255):  R 0.00% / 1.25%  G 0.00% / 0.80%  B 0.10% / 0.00%
```

The file format follows the extension (`.png`, `.jpg`, `.jpeg`). The histogram is measured after the whole pipeline, on the 8-bit sRGB values that are saved (before any JPEG compression). It works with or without `-o`. No default.

### `--raw-half`

RAW input only. Decode at half the width and height, which skips demosaicing and is much faster; good for previews. On a PNG or JPEG input it is an error ("only applies to RAW input"), so a wrong input file doesn't go unnoticed.

### `--raw-camera-matrix <file>`

RAW input only. Convert camera colour to linear sRGB with your own 3×3 matrix instead of LibRaw's built-in one for the camera model. The file has the same format as for `--matrix`. The matrix must map white-balanced camera RGB to linear sRGB (D65), row-major, with each row summing to 1 so that white stays white. `prism_profile` (below) can make this file from a camera profile. On a PNG or JPEG input it is an error. Default: LibRaw's matrix.

### `--threads <n>`

Use up to `n` threads, a whole number from 1 to 1024. `1` is single-threaded. Default: one per hardware thread (the number is printed in `--help`). Images under about 1 megapixel always run on one thread, because starting threads would cost more than it saves. The output is bit-identical for any thread count.

### `--time`

Print how long each stage took, in milliseconds, and the total, as in:

```
time (1 thread):
  load            0.2 ms
  exposure        0.0 ms
  curve           0.0 ms
  save            0.9 ms
  histogram       7.0 ms
  total           8.2 ms
```

The stages that can appear are `load`, `exposure`, `matrix`, `saturation`, `contrast`, `curve`, `save` and `histogram`. A stage that was skipped (a neutral value, or no `-o` or `--histogram`) isn't listed. The thread count shown is the number actually used.

### `-h`, `--help`

Print the usage text to stderr and exit with code 0.

### `--version`

Print `prism <version>` to stdout and exit with code 0.

## Examples

A JPEG, brightened by half a stop, with a warm grade, written as a PNG:

```sh
prism photo.jpg -o photo.png --exposure +0.5 --matrix samples/grade.txt
```

A RAW file at +1 stop, with the filmic curve, written as a JPEG:

```sh
prism DSC03866.ARW --exposure +1 --curve filmic -o DSC03866.jpg
```

The same file as a quick preview at half size, with more colour and contrast (closer to the camera's JPEG):

```sh
prism DSC03866.ARW --raw-half --exposure +1 --saturation 1.25 --contrast 1.15 --curve filmic -o preview.jpg
```

Check clipping without writing a picture:

```sh
prism photo.jpg --exposure +1 --histogram hist.png
```

Print how long each stage takes, on one thread:

```sh
prism photo.jpg -o out.jpg --curve filmic --threads 1 --time
```

A whole folder, in a shell loop (bash or zsh). Prism processes one file per run:

```sh
mkdir -p edited
for f in *.jpg; do
  prism "$f" --exposure +0.5 --curve filmic -o "edited/${f%.jpg}.jpg" || echo "failed: $f"
done
```

For RAW files, change the pattern (here Sony ARW files) and add `--exposure +1`:

```sh
for f in *.ARW; do
  prism "$f" --exposure +1 --curve filmic -o "edited/${f%.ARW}.jpg" || echo "failed: $f"
done
```

In Windows PowerShell:

```powershell
New-Item -ItemType Directory -Force edited | Out-Null
foreach ($f in Get-ChildItem *.jpg) {
  prism $f.Name --exposure +0.5 --curve filmic -o "edited\$($f.BaseName).jpg"
}
```

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success. Also `--help` and `--version` |
| 1 | The run failed: a file couldn't be read or written, an input can't be decoded, a matrix file or a `--curve` spec is wrong, or a RAW file was given to a build without RAW support. The message after `error:` says which |
| 2 | The command line is wrong: no input, a missing value, an unknown option, an option given twice, a number outside its range, neither `-o` nor `--histogram`, or `--raw-half` or `--raw-camera-matrix` on a PNG or JPEG. Prism prints `error: <reason>` and then the usage text |

Errors go to stderr.

## prism_profile

```
prism_profile <profile> [-o <matrix.txt>] [--temperature <K>] [--note <text>]
```

`prism_profile` converts a camera profile into a matrix file for `prism --raw-camera-matrix`. `<profile>` is a [LightCraft](https://github.com/storytold/lightcraft) camera profile (JSON), a DNG file or a DCP camera profile. It tells them apart by their first bytes, not the file name. The output is a white-balanced camera RGB → linear sRGB (D65) matrix with rows summing to 1, preceded by `#` comment lines that say where it came from and what was skipped. The README has the details of the maths: [Camera profiles](../README.md#camera-profiles-prism_profile).

### `-o <matrix.txt>`

Write the matrix file here. Default: write it to stdout. Giving `-o` twice, or without a value, is a usage error.

### `--temperature <K>`

The white balance temperature, in kelvin, at which a DNG or DCP profile's two ForwardMatrices are interpolated. A positive number. Default: 6504 (D65). It should be the colour temperature of the light the photo was taken in, because `prism_profile` doesn't read it from the photo. It only applies to DNG and DCP profiles that have two ForwardMatrices: with a LightCraft profile, or a profile with one calibration, any other value is an error rather than being ignored.

### `--note <text>`

Add a comment line with this text to the matrix file, for example which repository or commit the profile came from. May be given several times.

### Help

`prism_profile -h` or `prism_profile --help` prints the usage text to stderr and exits with code 0. (The usage text does not list `-h` and `--help` themselves.) `prism_profile` has no `--version`.

### Examples

```sh
prism_profile ILCE-7RM5.json -o samples/a7rv_lightcraft.txt --note "LightCraft PR #3022"
prism_profile camera.dcp -o camera_dcp.txt
prism_profile photo.dng -o photo_dng.txt --temperature 5000
prism DSC03866.ARW --raw-half --exposure +1 --curve filmic --raw-camera-matrix samples/a7rv_lightcraft.txt -o out.jpg
```

### Exit codes

`0` on success and for `--help`; `1` when the profile can't be read or converted (the message after `error:` says why); `2` for a wrong command line (no profile, an unknown option, `--temperature` that is not a positive number or that doesn't apply to the profile).

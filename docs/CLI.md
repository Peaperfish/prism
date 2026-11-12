# Command-line reference

Prism has two command-line tools. The download holds `prism` (`prism.exe` on Windows); `prism_profile` is built from the source (see the README) and sits next to `prism` in the build folder. `prism` runs a photo through the same pipeline as the app and writes the result. `prism_profile` turns a camera profile into a matrix file for `prism`. For the app, see [GUIDE.md](GUIDE.md).

`prism --help` prints the usage text, but to **stderr**, not stdout (exit code 0). To page it, redirect: `prism --help 2>&1 | less`.

Contents: [prism](#prism) · [Options of prism](#options-of-prism) · [Recipes and batches](#recipes-and-batches) · [Examples](#examples) · [Exit codes](#exit-codes) · [prism_profile](#prism_profile)

## prism

```
prism <input>... [-o <output> | --out-dir <dir>] [--histogram <file>] [options]
```

`prism` loads `<input>`, applies the options you give, and writes `<output>`. The input is a PNG, a JPEG, a camera RAW file (`.arw`, `.dng`, `.cr2`, `.cr3`, `.nef`, `.raf`, in any case) or, on macOS only, an iPhone HEIC photo (`.heic`, `.heif`). The output is `.png`, `.jpg` or `.jpeg`, chosen by the extension of the name you give with `-o`. JPEGs are saved at quality 95. If `<output>` exists, it is replaced.

- At least one input file must be given. Every argument that doesn't start with `-` and isn't the value of an option is an input. Several inputs need `--out-dir` (see `--out-dir` below).
- At least one of `-o`, `--out-dir` and `--histogram` is required. With only `--histogram`, the whole pipeline still runs and the histogram shows the processed result, but no image is written. That is a quick way to check clipping before you export.
- Every option except `--help`, `--version`, `--time`, `--raw-half` takes the next argument as its value, even if it starts with a minus sign, so `--exposure -1` works.
- An option given twice is an error.
- **Options are always applied in pipeline order**, whatever order you type them: the frame (`--rotate`, `--crop`, `--aspect`), reduction (`--long-side`), exposure, white balance, colour matrix, saturation, contrast, tone curve. The frame comes first of all, and `--long-side` after it: the photo is framed and reduced right after decoding, before exposure, so every later stage works on the small image. See [How Prism thinks about light](GUIDE.md#how-prism-thinks-about-light).
- A stage left at its neutral value is skipped: exposure 0, the identity matrix, saturation 1 and contrast 1 leave every bit as it was. A tone curve always runs when you give one.
- All processing is in linear light, in floating point. PNG and JPEG inputs are converted to linear light first and to 8-bit sRGB at the end. A RAW file is decoded to linear light with the camera's as-shot white balance (see the [README](../README.md#raw-files) for the details). A RAW file looks darker and flatter than the camera's JPEG until you add exposure and a tone curve; `--exposure +1 --curve filmic` is a good start.

After a successful run, `prism` prints `wrote <file> (<width>x<height>)` for the output image, `wrote <file> (histogram)` for the histogram, and the clipping line described under `--histogram`.

## Options of prism

### `-o <file>`

The output image. The extension (`.png`, `.jpg` or `.jpeg`, in any case) picks the format; any other extension fails with "unsupported extension" when the file is saved, after the processing has run. No default.

### `--out-dir <dir>`

Write each result into the folder `<dir>`, which must already exist, as `<input name>_prism.jpg` (or `.png`, see `--format`): `DSC03866.ARW` becomes `DSC03866_prism.jpg`, the name the app's **Export** suggests. With `--long-side 2048` it becomes `DSC03866_prism_2048.jpg`, for every input, including those too small to be reduced, so the names can be known in advance. A file of that name is replaced, as with `-o`. Needed for more than one input, and then `-o` and `--histogram` can't be used. With a single input it can take the place of `-o`, but not be given together with it. A folder that doesn't exist, or two inputs that would give the same name (such as `a/photo.jpg` and `b/photo.png`, or `photo.jpg` and `PHOTO.PNG`), is an error before any photo is processed. No default.

### `--format jpg|png`

The format `--out-dir` writes: `jpg` (the default) for JPEG at quality 95, exactly as `-o` writes a `.jpg`, or `png` for PNG. Only with `--out-dir`: with `-o` the extension of the name you give chooses the format, so `--format` there is an error. Anything other than `jpg` or `png` (in small letters) is an error.

### `--recipe <file>`

Apply the edit in a recipe file (`.prism`, described in [RECIPES.md](RECIPES.md)). The recipe is read and checked before any photo is opened: if it has mistakes, `prism` prints one `error:` line for each, with its line number, writes nothing and exits with code 1. A missing or unreadable recipe file is reported the same way. Any of `--exposure`, `--kelvin` and `--tint`, `--matrix`, `--saturation`, `--contrast` and `--curve` that you give as well replaces that stage of the recipe. No default.

### `--exposure <stops>`

Brighten (`+`) or darken (`-`) by this many stops. Every value is multiplied by 2 to the power of the stops, in linear light: `+1` doubles the light, `-1` halves it, `+0.5` is about 1.4×. Any finite number is accepted. Default: not applied.

### `--kelvin <K>`

The light the photo was lit by, as a colour temperature from 2000 to 15000. It follows Lightroom's convention: a higher Kelvin makes the photo warmer, a lower one cooler. The edit runs right after exposure, in linear light. For a RAW file it is relative to the light the camera recorded as shot, so `--kelvin` names the light you now say the scene had; for PNG, JPEG and HEIC files, which have no such record, 6500 K with a tint of 0 means "no change". If you give only one of `--kelvin` and `--tint`, the other is taken from the photo's as-shot light (6500 and 0 where there is none). Replaces the recipe's `[white_balance]`. Default: not applied (as shot).

### `--tint <n>`

The green–magenta tint of that light, from −100 to 100. A positive tint is a greener light, which makes the photo more magenta; a negative one the opposite. Each unit moves the light 0.0002 away from the Planckian locus (the colours of glowing bodies) in the CIE 1960 uv plane. See `--kelvin` for how the two combine and what "no change" is. Replaces the recipe's `[white_balance]`. Default: not applied (as shot).

### `--matrix <file>`

Apply a 3×3 colour matrix, in linear light, after exposure. The file holds 9 numbers, separated by spaces or line breaks, row-major: the first three make the first row, which says how the new red is made from the old red, green and blue, and so on. `#` starts a comment that runs to the end of the line. Exactly 9 numbers are required; anything else fails with an error that names the file. Default: not applied. See `samples/grade.txt` in the repository for an example (a mild warm grade).

### `--saturation <s>`

Scale each pixel's colourfulness around its own luminance. `1` leaves the image as it is, `0` makes it greyscale, `2` doubles the distance from grey. Brightness and hue are kept, and a colour that would go below zero stops at the most saturated colour sRGB can show. Accepts 0 to 10. The maths is in the README: [Saturation and contrast](../README.md#saturation-and-contrast). Default: not applied (same as 1).

### `--contrast <c>`

An S-curve around middle grey (0.18), in stops. `1` leaves the image as it is; `1.15` makes each stop near middle grey 1.15 stops wide; below 1 is less contrast. The effect fades beyond about 2 stops from middle grey. Accepts 0.1 to 10. Default: not applied (same as 1). The maths is in the README: [Saturation and contrast](../README.md#saturation-and-contrast).

### `--curve <spec>`

Apply a tone curve, after contrast. The spec is one of:

| Spec | Curve |
|---|---|
| `gamma:<value>` | `out = in^(1/value)`. Above 1 brightens the midtones, below 1 darkens them. `<value>` must be a number above 0; it is required |
| `reinhard` or `reinhard:<white>` | Rolls highlights off smoothly. `<white>` is the linear value that lands exactly on white (brighter values clip), above 0. Default white: 4 |
| `filmic` | Soft shadows and highlights (the "Uncharted 2" curve by John Hable). No parameter |

An unknown or malformed spec fails with an error that lists the available curves. Default: no curve.

### `--histogram <file>`

Save a histogram image of the result, 512 pixels wide and 200 high (red, green and blue overlaid additively over a grey fill of the Rec. 709 luminance), and print the percentage of pixels clipped at 0 and at 255 for each channel:

```
clipped (0 / 255):  R 0.00% / 1.25%  G 0.00% / 0.80%  B 0.10% / 0.00%
```

The file format follows the extension (`.png`, `.jpg`, `.jpeg`). The histogram is measured after the whole pipeline, on the 8-bit sRGB values that are saved (before any JPEG compression). It works with or without `-o`. No default.

### `--rotate <degrees>`

Straighten the photo by turning it about its centre, by a decimal number from −45 to 45 degrees (`0`, `1.2`, `-0.8`, `+1.5`). Positive turns the picture clockwise on screen; negative turns it anticlockwise. A value outside −45 to 45, or text that is not a finite number, is a usage error (exit code 2):

```
error: --rotate needs a number from -45 to 45, got '46'
```

Turning the photo closes the blades in as needed (keeping the shape) so that no corner of the frame runs past the photo's edge. When fitting makes the frame smaller than what you asked for, `prism` prints a note on stderr with the final pixel size, as for a 6000 × 4000 photo turned by `1.2` degrees:

```
note: photo.jpg: the frame was made smaller so no corner shows; it is now 5818 × 3878 px
```

The note is not an error: the photo is still written and the exit code is unaffected. At `0` degrees the box is copied pixel for pixel with no resampling; when turned, each pixel is resampled in linear light (Catmull-Rom over the 4×4 neighbours, clamped to the 2×2 nearest source pixels so turning never overshoots or clips). Default: 0 (not applied).

### `--crop <l,t,r,b>`

Keep only this part of the photo: the left, top, right and bottom edges as four decimal fractions from `0` to `1` of the photo's width and height, separated by commas with no spaces (for example `--crop 0.05,0.05,0.95,0.95` or `--crop 0.1,0,0.9,1`). The edges are measured along the straightened axes (around the photo's centre), so at `--rotate 0` it is a plain upright crop, and with `--rotate` the box is fitted inside the turned photo if a corner would run off the edge.

All four numbers must be between `0` and `1`, with `left` strictly less than `right` and `top` strictly less than `bottom`. Anything else is a usage error (exit code 2), with a message saying which rule failed:

- `--crop needs four numbers from 0 to 1, left,top,right,bottom, got '<text>'`
- `--crop edges must be from 0 to 1, got '<text>'`
- `--crop needs left less than right and top less than bottom, got '<text>'`

Default: `0,0,1,1` (the whole photo).

### `--aspect <shape>`

Make the frame the largest one of this shape, centred inside the `--crop` box (or inside the whole photo when `--crop` is not given), before fitting for any turn:

| `<shape>` | Width : height |
|---|---|
| `original` | the photo's own shape |
| `free` | no change to the `--crop` box |
| `1:1` | square |
| `4:5` / `5:4` | 4 wide × 5 tall (portrait) / 5 wide × 4 tall (landscape) |
| `5:7` / `7:5` | 5 wide × 7 tall / 7 wide × 5 tall |
| `3:2` / `2:3` | 3 wide × 2 tall / 2 wide × 3 tall |
| `16:9` / `9:16` | 16 wide × 9 tall / 9 wide × 16 tall |

Unlike the app's Aspect menu (which follows the photo's orientation and uses `X` to turn the shape), the command line reads `W:H` literally as `width:height` on every photo: `--aspect 4:5` is always 4 wide by 5 tall (portrait), even on a landscape photo, and `--aspect 5:4` is always 5 wide by 4 tall; `--aspect 16:9` is always wide, and `--aspect 9:16` is always tall. Any other value is a usage error (exit code 2):

```
error: --aspect needs original, free, 1:1, 4:5, 5:7, 3:2 or 16:9 (also 5:4, 7:5, 2:3 and 9:16), got '4/5'
```

Default: not applied.

**Order relative to other options.** The three frame options (`--rotate`, `--crop` and `--aspect`) always run together as one step, **first of all**, right after decoding (including `--raw-half` and `--raw-camera-matrix`). When more than one of the three is given, `--crop` defines the box along the straightened axes, `--aspect` cuts the largest centred rectangle of that shape inside the box, and `--rotate` turns the photo about its centre and closes the blades in if a corner would pass the photo's edge. A whole-photo frame at `0` degrees (`--rotate 0`, `--crop 0,0,1,1`, `--aspect original` or `--aspect free` without a turn) is a no-op and skips the `frame` stage completely.

After framing, `--long-side` measures and shrinks the **framed** photo (not the unframed file), and then exposure, white balance, colour matrix, saturation, contrast, tone curve, `--histogram` and the output all work on and measure the framed picture. A frame is not part of a recipe: `--recipe` and the frame flags combine on the same run, and a recipe file never holds a frame.

### `--long-side <pixels>`

Shrink the photo so that its longer side is this many pixels, keeping the aspect ratio (the shorter side is rounded to the nearest pixel). A whole number of at least 1, written in digits only: `0`, `-5`, `2.5` and `1e3` are usage errors (exit code 2). It works for RAW files too, and with `--raw-half` it applies to the half-size decode.

The reduction happens right after decoding and before exposure, with the same area averaging on linear light as the app's preview, so a small export matches what you see and is faster to make: every stage works on the small image.

Smaller only: Prism never enlarges. A photo whose longer side is not larger than `<pixels>` keeps its size, and `prism` says so on stderr, as in:

```
note: IMG_0042.jpg is 1600x1067, not larger than --long-side 2048; kept its size
```

The note is not an error: the photo is still written and the exit code is unaffected. The name suffix described under `--out-dir` is added for every input of a batch, noted or not. A name you give yourself with `-o` is used as it is. Default: not applied.

### `--raw-half`

RAW input only. Decode at half the width and height, which uses LibRaw's half-size mode (no demosaicing) and is much faster, about twice as fast on a 61-megapixel Sony α7R V file; good for previews. On a PNG or JPEG input it is an error ("only applies to RAW input"), so a wrong input file doesn't go unnoticed.

### `--raw-camera-matrix <file>`

RAW input only. Convert camera colour to linear sRGB with your own 3×3 matrix instead of LibRaw's built-in one for the camera model. The file has the same format as for `--matrix`. The matrix must map white-balanced camera RGB to linear sRGB (D65), row-major, with each row summing to 1 so that white stays white. `prism_profile` (below) can make this file from a camera profile. On a PNG or JPEG input it is an error. Default: LibRaw's matrix. Passing LibRaw's own matrix for your camera reproduces the default output, which makes a good check before trying your own. More in the README: [RAW files](../README.md#your-own-camera-matrix).

### `--threads <n>`

Use up to `n` threads, a whole number from 1 to 1024. `1` is single-threaded. Default: one per hardware thread (the number is printed in `--help`). Images under about 1 megapixel always run on one thread, because starting threads would cost more than it saves. The output is bit-identical for any thread count.

### `--time`

Print how long each stage took, in milliseconds, and the total, as in:

```
time (1 thread):
  load                0.2 ms
  exposure            0.0 ms
  curve               0.0 ms
  save                0.9 ms
  histogram           7.0 ms
  total               8.2 ms
```

The stages that can appear are `load`, `frame` (with a frame that changes the photo), `resize` (with `--long-side`), `exposure`, `white_balance`, `matrix`, `saturation`, `contrast`, `curve`, `save` and `histogram`. A stage that was skipped (a neutral value, or no `-o` or `--histogram`) isn't listed. The thread count in the heading is the number the stages on the final image actually used (an image under about 1 MP runs on one thread). A `frame` or `resize` step that worked on a larger image than the result, and so used a different number, says its own on its line, as `(12 threads)`.

### `-h`, `--help`

Print the usage text to stderr and exit with code 0.

### `--version`

Print `prism <version>` to stdout and exit with code 0.

## Recipes and batches

A recipe (`.prism`, described in [RECIPES.md](RECIPES.md)) lets you settle on a look in the app, save it with **File → Save Recipe…**, and then run it on one photo or a whole shoot from the command line.

`--recipe <file>` loads the recipe and checks it once, before any photo is opened. If the file is missing, unreadable or has mistakes, `prism` prints one `error:` line per mistake (with the line number), writes nothing and exits with code 1.

When you give stage flags alongside `--recipe`, **flags win stage by stage**:

- `--exposure` replaces `[exposure]` and switches that stage on;
- `--kelvin` and `--tint` replace `[white_balance]` and switch it on; a recipe's other white balance value is not kept, so giving only one of the two takes the other from the photo's as-shot light;
- `--matrix` replaces `[matrix]` and switches it on;
- `--saturation` replaces `[saturation]` and switches it on;
- `--contrast` replaces `[contrast]` and switches it on;
- `--curve` replaces the whole `[curve]` section (both shape and parameter) and switches it on.

A recipe stage with `on = false` is skipped unless you give the flag for that stage. A stage left out of the recipe (and not given as a flag) **does nothing** on the command line, because `prism` starts each photo from no adjustments — unlike the app, where a recipe leaves omitted stages at whatever values the open photo already had.

To process **several inputs** in one run, pass them on the command line with `--out-dir <dir>` (an existing folder). With more than one input, `-o` and `--histogram` are usage errors (exit code 2), and `--out-dir` is required. Each result is written into `<dir>` as `<input stem>_prism.jpg` (or `<input stem>_prism.png` with `--format png`), matching the name the app's **Export** suggests. With `--long-side <n>` the name is `<input stem>_prism_<n>.jpg` for every input, including those too small to be reduced (they keep their size and get a `note:` on stderr). If a file of that name already exists in `<dir>`, it is replaced. Before any photo is decoded, `prism` checks the whole batch: if `--out-dir` does not exist or is not a folder, or if two inputs would produce the same output name (ignoring case, such as `a/photo.jpg` and `b/PHOTO.png`), it exits with code 2 and writes nothing.

Photos in a batch are processed one after another (each photo already uses all hardware threads). Each photo that succeeds prints its usual `wrote <file> (<width>x<height>)` line. If one photo fails (unreadable, decode error or write error), `prism` prints an `error:` line naming that photo and carries on with the rest. At the end of a run with more than one input, it prints a summary line to stdout:

```
3 of 4 photos written; 1 failed.
```

(or `4 of 4 photos written.` when all succeeded). It exits with code 0 if every photo succeeded, 1 if any photo failed (or if the recipe, matrix or curve was bad), and 2 for a command-line usage error.

Apply a saved recipe to a single RAW file:

```sh
prism DSC03866.ARW --recipe warm.prism -o DSC03866_warm.jpg
```

Apply a recipe to a batch of RAW files into an existing `edited` folder, overriding the recipe's exposure for this shoot:

```sh
prism DSC03866.ARW DSC03867.ARW DSC03868.ARW --recipe warm.prism --exposure +1.25 --out-dir edited
```

Export a batch of RAW files with a recipe as lossless PNGs at half size, replacing the recipe's tone curve with filmic:

```sh
prism IMG_001.CR3 IMG_002.CR3 --raw-half --recipe look.prism --curve filmic --format png --out-dir previews
```

## Examples

Straighten a 6000 × 4000 JPEG by 1.2° clockwise and cut a 16:9 frame (turning the full width closes the blades in slightly so no corner runs off the photo, and `prism` notes the final size on stderr):

```sh
prism photo.jpg --rotate 1.2 --aspect 16:9 -o wide.jpg
```

```
note: photo.jpg: the frame was made smaller so no corner shows; it is now 5932 × 3336 px
wrote wide.jpg (5932x3336)
```

Straighten by 0.8° anticlockwise, trim 5% off each edge, cut a vertical 4:5 frame from the middle, shrink the framed picture to 2048 pixels on its longer side (here the height), and apply +0.5 stops and the filmic curve (because the 5% margin already leaves room for the 0.8° turn, the blades don't need to close in and no `note:` is printed):

```sh
prism photo.jpg --rotate -0.8 --crop 0.05,0.05,0.95,0.95 --aspect 4:5 --long-side 2048 --exposure +0.5 --curve filmic -o framed.jpg
```

```
wrote framed.jpg (1638x2048)
```

A whole shoot for sharing online, each photo reduced to 2048 pixels on its longer side, written as `edited/<name>_prism_2048.jpg`:

```sh
prism *.jpg --long-side 2048 --out-dir edited
```

A JPEG, brightened by half a stop, with a warm grade and the filmic curve, written as a PNG, plus a histogram of the result:

```sh
prism input.jpg -o output.png --exposure +0.5 --matrix samples/grade.txt --curve filmic --histogram hist.png
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

A whole folder can go to `--out-dir` in one run (see `--out-dir`). A shell loop (bash or zsh) gives you full control over the names instead:

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
| 1 | The run failed: a file couldn't be read or written, an input can't be decoded, a recipe has mistakes or can't be read, a matrix file or a `--curve` spec is wrong, or a RAW file was given to a build without RAW support. The message after `error:` says which |
| 2 | The command line is wrong: no input, a missing value, an unknown option, an option given twice, a number outside its range, a malformed `--crop` or unknown `--aspect`, a `--format` other than `jpg` or `png` or without `--out-dir`, none of `-o`, `--out-dir` and `--histogram`, `--raw-half` or `--raw-camera-matrix` on a PNG or JPEG, several inputs without `--out-dir` (or with `-o` or `--histogram`), `-o` together with `--out-dir`, an `--out-dir` folder that doesn't exist, or two inputs that would be written under the same name. Prism prints `error: <reason>` and then the usage text |

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

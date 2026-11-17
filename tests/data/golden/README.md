# SDR golden references

These files pin Prism's SDR output as it was in 0.7.1 (commit and version in `SOURCE.txt` and
`raw/SOURCE.txt`), so the
0.8.0 HDR work can show that HDR off still gives the same pixels. `tests/golden_sdr_test.cpp`
(engine and `prism` command line) and `tests/gui/golden_export_test.cpp` (the app's export) run
the same processing and compare the decoded 8-bit sRGB pixels with these files.

- `input/prism-window.jpg`: the sample photo, a copy of `docs/images/prism-window.jpg` kept here
  so a new screenshot in the docs never changes what the tests check.
- `image/<look>.png` and `.jpg`: that photo at `--long-side 256` under each image look in
  `recipes/image/`.
- `synthetic/synthetic.dng`: the 32 × 32 DNG of `tests/synthetic_dng.hpp` (every photosite at
  13990, about 0.21 of white), written by `tools/make_synthetic_dng.cpp`; `synthetic/<look>.png` and `.jpg`
  are it under each RAW look. These are the RAW references CI checks, wherever LibRaw is built:
  the tests write the helper's bytes to their temp folder and run them, and
  `GoldenSdr.SyntheticDngIsTheStoredOne` checks the helper still writes this very file. The
  pictures are uniform grey, so off Apple arm64 both the PNG and the JPEG check are in effect
  exact: a 1-level difference is in every sample, and these JPEGs decode to their PNGs exactly.
  The level is chosen so every look is at least 35 codes of the decode from its next 8-bit level
  (see `tools/make_synthetic_dng.cpp`). Being neutral grey, with no as-shot white, they cover the
  decode and the tonal part of each look, not its colour (saturation, the bw channel mix).
- `raw/<look>.png` and `.jpg`: the maintainer's test RAW (a Sony a7R V `.ARW`, not in the
  repository) at `--long-side 256` under each RAW look in `recipes/raw/`. `raw/source.sha256` is
  that file's SHA-256: the RAW tests run only when `PRISM_TEST_RAW` names a file with this hash,
  and skip, saying why, otherwise (CI has no RAW file).

How close is close enough: built for Apple arm64 (the macOS CI runners and the maintainer's Mac)
the pixels must match exactly. Elsewhere a PNG may differ by 1 level per channel in at most 0.5% of
the samples, and a JPEG, compared decoded with stb against the decoded reference JPEG, may differ
by no more, at worst and on average, than that reference JPEG differs from its own PNG (see
`tests/golden.hpp` for why).

## Regenerating, only on purpose

Regenerate only when a change to the SDR output is intended, never to make a failing test pass, and
say so in the commit. Check out the commit to pin (copy `tools/make_sdr_goldens.sh` and
`synthetic/synthetic.dng` in if that commit predates them), build `prism` there with LibRaw, then:

```sh
PRISM_TEST_RAW=/path/to/the.ARW tools/make_sdr_goldens.sh build/src/prism --overwrite
```

Without `PRISM_TEST_RAW` the RAW references and `raw/SOURCE.txt` are left as they are (so that file
can be in an older format than `SOURCE.txt`). If `tests/synthetic_dng.hpp`
changed, first rewrite the DNG as `tools/make_synthetic_dng.cpp` says. Run the tests afterwards: the
engine and app export must still match the new files.

# SDR golden references

These files pin Prism's SDR output as it was in 0.7.1 (commit and version in `SOURCE.txt`), so the
0.8.0 HDR work can show that HDR off still gives the same pixels. `tests/golden_sdr_test.cpp`
(engine and `prism` command line) and `tests/gui/golden_export_test.cpp` (the app's export) run
the same processing and compare the decoded 8-bit sRGB pixels with these files.

- `input/prism-window.jpg`: the sample photo, a copy of `docs/images/prism-window.jpg` kept here
  so a new screenshot in the docs never changes what the tests check.
- `image/<look>.png` and `.jpg`: that photo at `--long-side 256` under each image look in
  `recipes/image/`.
- `raw/<look>.png` and `.jpg`: the maintainer's test RAW (a Sony a7R V `.ARW`, not in the
  repository) at `--long-side 256` under each RAW look in `recipes/raw/`. `raw/source.sha256` is
  that file's SHA-256: the RAW tests run only when `PRISM_TEST_RAW` names a file with this hash,
  and skip, saying why, otherwise (CI has no RAW file).

How close is close enough: built for Apple arm64 (the macOS CI runners and the maintainer's Mac)
the pixels must match exactly. Elsewhere a PNG may differ by 1 level per channel in at most 0.5% of
the samples, and a JPEG, compared decoded with stb against the decoded reference JPEG, by at most
6 levels in at most 2% of the samples (see the tests' header comments for why).

## Regenerating, only on purpose

Regenerate only when a change to the SDR output is intended, never to make a failing test pass, and
say so in the commit. Build `prism` from a clean checkout of the commit to pin, then:

```sh
PRISM_TEST_RAW=/path/to/the.ARW tools/make_sdr_goldens.sh build/src/prism --overwrite
```

Without `PRISM_TEST_RAW` the RAW references are left as they are. Run the tests afterwards: the
engine and app export must still match the new files.

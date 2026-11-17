#!/usr/bin/env bash
# Regenerates the SDR golden references in tests/data/golden/ (see the README there).
#
# The goldens pin Prism's SDR output: tests/golden_sdr_test.cpp and tests/gui/golden_export_test.cpp
# fail when an export's 8-bit pixels change. Run this ONLY on purpose, when a change to the output
# is wanted, and say so in the commit; never to make a failing golden test pass.
#
#   tools/make_sdr_goldens.sh <prism executable> --overwrite
#
# Writes, at --long-side 256 with each built-in look's recipe:
#   image/<look>.png and .jpg  from input/prism-window.jpg, for every recipes/image/*.prism
#   synthetic/<look>.png, .jpg from synthetic/synthetic.dng (tests/synthetic_dng.hpp's DNG, written
#                              by tools/make_synthetic_dng.cpp), for every recipes/raw/*.prism;
#                              these run in CI wherever LibRaw is built. Needs prism with LibRaw.
#   raw/<look>.png and .jpg    from $PRISM_TEST_RAW, for every recipes/raw/*.prism, only when
#                              PRISM_TEST_RAW is set; raw/source.sha256 records that file's SHA-256
#                              (the RAW tests skip unless their PRISM_TEST_RAW has the same hash)
#   SOURCE.txt, raw/SOURCE.txt the last commit that changed Prism's code or recipes (lib/, src/,
#                              include/, gui_core/src/, recipes/) and the prism version the image
#                              and synthetic (SOURCE.txt) and the RAW references came from
#                              (raw/SOURCE.txt only changes with them). Not the commit the script
#                              ran at: that hash changes whenever the branch is rebased.
# Run it in a checkout of the commit you mean to pin (copy this script and
# tests/data/golden/synthetic/synthetic.dng in if that commit predates them), with prism built there
# with LibRaw: it refuses a checkout whose code or recipes have uncommitted changes, and checks the
# DNG and prism's RAW support before writing anything.
set -euo pipefail

if [[ $# -ne 2 || "$2" != "--overwrite" ]]; then
    sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
prism="$1"
repo="$(cd "$(dirname "$0")/.." && pwd)"
golden="$repo/tests/data/golden"
long_side=256

code_paths=(CMakeLists.txt cmake include lib src gui_core recipes third_party)
if [[ -n "$(git -C "$repo" status --porcelain -- "${code_paths[@]}")" ]]; then
    echo "uncommitted changes to Prism's code or recipes: the references would not match a commit"
    exit 1
fi
if [[ ! -f "$golden/synthetic/synthetic.dng" ]]; then
    echo "no $golden/synthetic/synthetic.dng: write it with tools/make_synthetic_dng.cpp first"
    exit 1
fi
trial="$(mktemp -d)"
if ! "$prism" "$golden/synthetic/synthetic.dng" -o "$trial/trial.png" > /dev/null 2>&1; then
    rm -rf "$trial"
    echo "$prism could not read the synthetic DNG: build it with LibRaw"
    exit 1
fi
rm -rf "$trial"

# What the references depend on: the last commit that changed these. (The guard above also covers
# the build files.)
pinned_paths=(lib src include gui_core/src recipes)
source_txt() {
    echo "code as at $(git -C "$repo" log -1 --format='%H (%s)' -- "${pinned_paths[@]}")"
    echo "$("$prism" --version 2>&1 | head -1)"
    echo "long side $long_side"
}

sha256() {
    if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

render() {  # <input> <recipe> <output without extension>
    for ext in png jpg; do
        "$prism" "$1" -o "$3.$ext" --recipe "$2" --long-side "$long_side" > /dev/null
    done
}

mkdir -p "$golden/image"
for recipe in "$repo"/recipes/image/*.prism; do
    render "$golden/input/prism-window.jpg" "$recipe" "$golden/image/$(basename "$recipe" .prism)"
done

mkdir -p "$golden/synthetic"
for recipe in "$repo"/recipes/raw/*.prism; do
    render "$golden/synthetic/synthetic.dng" "$recipe" \
        "$golden/synthetic/$(basename "$recipe" .prism)"
done

if [[ -n "${PRISM_TEST_RAW:-}" ]]; then
    mkdir -p "$golden/raw"
    for recipe in "$repo"/recipes/raw/*.prism; do
        render "$PRISM_TEST_RAW" "$recipe" "$golden/raw/$(basename "$recipe" .prism)"
    done
    sha256 "$PRISM_TEST_RAW" > "$golden/raw/source.sha256"
    source_txt > "$golden/raw/SOURCE.txt"
else
    echo "PRISM_TEST_RAW is not set: the RAW references were left as they were"
fi

{
    source_txt
    echo "synthetic.dng sha256 $(sha256 "$golden/synthetic/synthetic.dng")"
} > "$golden/SOURCE.txt"
echo "wrote $golden"

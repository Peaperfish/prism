#!/bin/bash
# Packages a PRISM_RELEASE build for macOS and checks the result:
#
#   packaging/macos/package.sh <build-dir> <out-dir> <architectures>
#
# e.g. packaging/macos/package.sh build dist "arm64 x86_64". Writes
# <out-dir>/prism-<version>-macos-<universal|arm64>.zip (a folder holding Prism.app, prism,
# LICENSE.md, THIRD_PARTY_NOTICES.txt, README.txt and licenses/LibRaw/) and
# <out-dir>/LibRaw-<version>-source.tar.gz, the exact tarball the build compiled.
set -euo pipefail

build=$1
out=$2
archs=$3

# shellcheck source=/dev/null
source "$build/release.env"  # PRISM_VERSION, LIBRAW_VERSION, LIBRAW_SHA256, LIBRAW_TARBALL

if [[ "$archs" == "arm64 x86_64" ]]; then
  flavour=universal
else
  flavour=$archs
fi
name="prism-$PRISM_VERSION-macos-$flavour"
stage="$out/stage/$name"

rm -rf "$out/stage" "$out/$name.zip"
mkdir -p "$out"
cmake --install "$build" --prefix "$stage"

# Every binary must carry a signature on Apple Silicon. Ad-hoc ("-"): no Developer ID, so
# Gatekeeper still asks the first time, but the app isn't reported as damaged.
codesign --force --deep -s - "$stage/Prism.app"
codesign --force -s - "$stage/prism"

echo "== Checks"
for bin in "$stage/Prism.app/Contents/MacOS/Prism" "$stage/prism"; do
  # Only the OS's own libraries and frameworks: nothing from Homebrew, no LibRaw dylib.
  # (The indented lines are the libraries; the others name the file and, if fat, the slice.)
  if otool -L "$bin" | grep -E '^[[:space:]]' | grep -v -E '^[[:space:]]+(/usr/lib/|/System/Library/)'; then
    echo "error: $bin links a library that isn't part of macOS" >&2
    exit 1
  fi
  if otool -L "$bin" | grep -i libraw; then
    echo "error: $bin links LibRaw dynamically" >&2
    exit 1
  fi
  for arch in $archs; do
    if ! lipo "$bin" -verify_arch "$arch"; then
      echo "error: $bin has no $arch code" >&2
      exit 1
    fi
  done
  echo "$bin: $(lipo -archs "$bin"), minimum macOS $(vtool -show-build "$bin" | awk '/minos/ {print $2; exit}')"
done
codesign --verify --deep --strict --verbose=2 "$stage/Prism.app"
codesign --verify --strict --verbose=2 "$stage/prism"
"$stage/prism" --version

# Prism's licence, with its Required Notice, and the README that points to it.
for f in LICENSE.md THIRD_PARTY_NOTICES.txt README.txt; do
  if [[ ! -f "$stage/$f" ]]; then
    echo "error: the download has no $f" >&2
    exit 1
  fi
done
if ! grep -q '^Required Notice: ' "$stage/LICENSE.md"; then
  echo "error: LICENSE.md has no 'Required Notice:' line" >&2
  exit 1
fi
if ! grep -q 'LICENSE\.md' "$stage/README.txt"; then
  echo "error: README.txt doesn't refer to LICENSE.md" >&2
  exit 1
fi

# LibRaw's source: the tarball the build unpacked, with the pinned hash, and its licence files
# in the download exactly as they are in it.
actual=$(shasum -a 256 "$LIBRAW_TARBALL" | awk '{print $1}')
if [[ "$actual" != "$LIBRAW_SHA256" ]]; then
  echo "error: $LIBRAW_TARBALL has SHA-256 $actual, not the pinned $LIBRAW_SHA256" >&2
  exit 1
fi
unpacked=$(mktemp -d)
tar -xzf "$LIBRAW_TARBALL" -C "$unpacked"
for f in LICENSE.CDDL COPYRIGHT; do
  cmp "$unpacked/LibRaw-$LIBRAW_VERSION/$f" "$stage/licenses/LibRaw/$f"
done
rm -rf "$unpacked"
cp "$LIBRAW_TARBALL" "$out/LibRaw-$LIBRAW_VERSION-source.tar.gz"
echo "LibRaw $LIBRAW_VERSION source: SHA-256 $actual (matches the pin); licence files identical"

# ditto keeps the bundle's signature, symlinks and extended attributes (plain zip can break them).
ditto -c -k --keepParent "$stage" "$out/$name.zip"
check=$(mktemp -d)
ditto -x -k "$out/$name.zip" "$check"
codesign --verify --deep --strict "$check/$name/Prism.app"
rm -rf "$check" "$out/stage"
echo "== Wrote $out/$name.zip"

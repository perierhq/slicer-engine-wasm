#!/usr/bin/env bash
# Adapted from Muon3D Slicer Engine (https://github.com/Muon-3D/muon3d-slicer-engine, AGPL-3.0-only),
# engine/deps/ at commit ba080b202c. Changes: Bambu Studio 2.8.2.61 pins (Qhull 8.0.1,
# bambulab/libnoise 1.0.0, bundled Eigen), Emscripten 6.0.13, macOS bash 3.2 compatibility.
# Fetch, hash-check and extract the third-party sources the wasm engine's dependencies are built from.
# Git Bash on Windows, or a Linux shell (needs curl and bsdtar); idempotent, so re-running only does what is
# missing.
#
#   bash engine/deps/fetch-deps.sh                    # everything the default build needs
#   bash engine/deps/fetch-deps.sh boost libjpeg-turbo # selected packages
#   WITH_GMP=1 bash engine/deps/fetch-deps.sh         # also GMP + MPFR (only build-deps.sh's optional steps use them)
#
# Layout (ORCA_WASM_ROOT defaults to ~/OrcaWasm; big files stay out of the repo and any synced folder):
#   $ORCA_WASM_ROOT/deps-src/_archives/<file>        downloaded archive
#   $ORCA_WASM_ROOT/deps-src/<name>-<version>/       extracted source, top-level folder stripped
#
# The expected hashes live in SHA256SUMS next to this script (sha256sum format) and are the single
# source of truth: an archive whose hash is not listed there, or does not match, stops the script.
# Pass RECORD_NEW=1 once to append the hash of a newly added archive (and review it before committing).
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../scripts/toolchain.sh
source "$HERE/../scripts/toolchain.sh"
ORCA_WASM_ROOT=$(mixed_path "${ORCA_WASM_ROOT:-$ORCAWASM_DEFAULT_ROOT}")   # accepts /x/... or X:/... forms
check_root "$ORCA_WASM_ROOT"
SRC=$ORCA_WASM_ROOT/deps-src
ARC=$SRC/_archives
SUMS=$HERE/SHA256SUMS
RECORD_NEW=${RECORD_NEW:-0}
WITH_GMP=${WITH_GMP:-0}
# bsdtar (libarchive): handles .zip/.tar.gz/.tar.bz2 and --strip-components; GNU tar cannot read .zip.
# Windows ships one as System32/tar.exe; on Linux it is the libarchive-tools package.
if [[ $ORCAWASM_WINDOWS == 1 ]]; then BSDTAR=${BSDTAR:-/c/Windows/System32/tar.exe}; else BSDTAR=${BSDTAR:-bsdtar}; fi
command -v "$BSDTAR" >/dev/null || { echo "bsdtar not found ($BSDTAR); set BSDTAR" >&2; exit 1; }

# name | version | url | where the pin comes from
DEPS=(
  "boost|1.84.0|https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz|bambu deps/Boost/Boost.cmake"
  "oneTBB|2021.12.0|https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v2021.12.0.zip|bump from bambu deps/TBB/TBB.cmake 2021.5.0 (no wasm support); mt only"
  "CGAL|5.6.3|https://github.com/CGAL/cgal/releases/download/v5.6.3/CGAL-5.6.3.zip|bump from bambu deps/CGAL/CGAL.cmake 5.4 (5.4 cannot be used without GMP: CGAL/mpz_class.h, gmpxx.h); as Muon3D"
  "cereal|1.3.0|https://github.com/USCiLab/cereal/archive/refs/tags/v1.3.0.zip|bambu deps/Cereal/Cereal.cmake"
  "nlopt|2.5.0|https://github.com/stevengj/nlopt/archive/v2.5.0.tar.gz|bambu deps/NLopt/NLopt.cmake"
  "libnoise|1.0.0|https://github.com/bambulab/libnoise/archive/refs/tags/v1.0.0.zip|bambu deps/libnoise/libnoise.cmake"
  "qhull|8.0.1|https://github.com/qhull/qhull/archive/v8.0.1.zip|bambu deps/Qhull/Qhull.cmake"
  "libjpeg-turbo|3.0.1|https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.0.1.zip|bambu deps/JPEG/JPEG.cmake"
)

mkdir -p "$ARC"
want=("$@" "")

for entry in "${DEPS[@]}"; do
  IFS='|' read -r name ver url pin <<<"$entry"
  if [[ ${#want[@]} -gt 1 && " ${want[*]} " != *" $name "* ]]; then continue; fi
  if [[ ${#want[@]} -eq 1 && $WITH_GMP != 1 && ( $name == gmp || $name == mpfr ) ]]; then
    echo "[skip]  $name $ver (optional: WITH_GMP=1)"; continue
  fi

  base=$(basename "$url")
  # GitHub tag archives are called v1.3.0.zip / 1.0.zip: prefix the package name to keep them unique.
  case "$base" in v[0-9]*|[0-9]*) base="$name-$base" ;; esac
  file=$ARC/$base
  dest=$SRC/$name-$ver

  if [[ ! -s "$file" ]]; then
    echo "[fetch] $name $ver <- $url"
    curl -fsSL --retry 3 --retry-delay 5 -o "$file.part" "$url"
    mv "$file.part" "$file"
  fi

  got=$(sha256sum "$file" | cut -d' ' -f1)
  expect=$(awk -v f="$base" '$2 == f { print $1 }' "$SUMS")
  if [[ -z "$expect" ]]; then
    if [[ $RECORD_NEW == 1 ]]; then
      printf '%s  %s\n' "$got" "$base" >> "$SUMS"
      echo "[new]   $name $ver sha256 $got recorded in SHA256SUMS ($pin)"
    else
      echo "[FAIL]  $base is not listed in $SUMS (got $got); re-run with RECORD_NEW=1 to add it" >&2
      exit 1
    fi
  elif [[ "$(tr A-F a-f <<<"$got")" != "$(tr A-F a-f <<<"$expect")" ]]; then
    echo "[FAIL]  $name: sha256 $got != expected $expect" >&2
    exit 1
  else
    echo "[ok]    $name $ver ($pin)"
  fi

  # .extracted records what the folder was extracted from; a hash change forces a fresh extract.
  if [[ ! -f "$dest/.extracted" ]] || ! grep -q "sha256=$got" "$dest/.extracted"; then
    rm -rf "$dest"; mkdir -p "$dest"
    # Windows bsdtar cannot create symlinks; the only one in these archives is a doc image in nlopt.
    excl=(--exclude __none__)
    [[ $name == nlopt ]] && excl=(--exclude '*/doc/nlopt-mkdocs-theme/img/favicon.png')
    "$BSDTAR" -xf "$(native_path "$file")" -C "$(native_path "$dest")" --strip-components=1 "${excl[@]}"
    echo "$url sha256=$got" > "$dest/.extracted"
    echo "[x]     extracted -> $dest"
  fi
done
echo "fetch-deps: done ($SRC)"

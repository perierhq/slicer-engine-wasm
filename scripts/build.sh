#!/usr/bin/env bash
# One command from an empty machine to dist/: emsdk, dependency sources and prefixes, the pinned Bambu Studio release,
# the engine, the host scripts and the profiles bundle. Linux (CI: ubuntu-latest) and macOS.
#   scripts/build.sh                     the single-threaded engine (st)
#   VARIANTS="st mt" scripts/build.sh    both; mt is the threaded engine (pthreads + oneTBB)
# Environment: ORCA_WASM_ROOT (work folder, default ./work), DIST (output, default ./dist), JOBS (default 4),
# NICE (default 10), BAMBU_SRC (default $ORCA_WASM_ROOT/BambuStudio).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
source "$HERE/pin.sh"
export ORCA_WASM_ROOT=${ORCA_WASM_ROOT:-$REPO/work}
VARIANTS=${VARIANTS:-${VARIANT:-st}}
DIST=${DIST:-$REPO/dist}
export JOBS=${JOBS:-4}
export EMCC_PIN
export BAMBU_SRC=${BAMBU_SRC:-$ORCA_WASM_ROOT/BambuStudio}
NICE=${NICE:-10}
mkdir -p "$ORCA_WASM_ROOT"

if [[ ! -f $ORCA_WASM_ROOT/emsdk/.emscripten ]]; then
  [[ -d $ORCA_WASM_ROOT/emsdk ]] || git clone --depth 1 https://github.com/emscripten-core/emsdk "$ORCA_WASM_ROOT/emsdk"
  (cd "$ORCA_WASM_ROOT/emsdk" && ./emsdk install "$EMCC_PIN" && ./emsdk activate "$EMCC_PIN")
fi

bash "$REPO/deps/fetch-deps.sh"
for v in $VARIANTS; do
  VARIANT=$v nice -n "$NICE" bash "$REPO/deps/build-deps.sh"
done
bash "$HERE/get-bambu.sh"
for v in $VARIANTS; do
  VARIANT=$v nice -n "$NICE" bash "$HERE/build-engine.sh" bambu_engine
done

mkdir -p "$DIST"
for v in $VARIANTS; do
  cp "$ORCA_WASM_ROOT/build-engine-$v/out/engine-$v.mjs" "$ORCA_WASM_ROOT/build-engine-$v/out/engine-$v.wasm" "$DIST/"
done
cp "$REPO"/host/*.mjs "$DIST/"
python3 "$HERE/make-profiles.py" "$BAMBU_SRC" "$DIST/profiles.tar"
ls -la "$DIST"

#!/usr/bin/env bash
# Configures and builds the engine for one variant (st|mt) against the dependency prefix.
#   ORCA_WASM_ROOT=<work> VARIANT=st JOBS=4 scripts/build-engine.sh [target]
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/toolchain.sh"
source "$HERE/pin.sh"
VARIANT=${VARIANT:-st}
JOBS=${JOBS:-4}
ROOT=${ORCA_WASM_ROOT:?set ORCA_WASM_ROOT}
export ORCA_WASM_ROOT=$ROOT
BAMBU_SRC=${BAMBU_SRC:-$ROOT/BambuStudio}
BUILD=${ENGINE_BUILD:-$ROOT/build-engine-$VARIANT}
setup_emsdk
check_emcc "$EMCC_PIN"
if [[ ! -f $BUILD/build.ninja ]]; then
  emcmake cmake -C "$ROOT/prefix-$VARIANT/share/orcawasm/initial-cache.cmake" -G Ninja \
    -S "$HERE/../engine" -B "$BUILD" -DBAMBU_SRC="$BAMBU_SRC" -DENGINE_VARIANT="$VARIANT"
fi
cmake --build "$BUILD" -j "$JOBS" ${1:+--target "$1"}
ls -la "$BUILD/out" 2>/dev/null || true

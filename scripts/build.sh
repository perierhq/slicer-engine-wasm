#!/usr/bin/env bash
# One command from an empty machine to dist/: emsdk, dependency sources and prefix, the pinned Bambu Studio
# release, the engine. Linux (CI: ubuntu-latest) and macOS.
#   scripts/build.sh                 everything, single-threaded engine (st)
#   VARIANT=mt scripts/build.sh      the threaded engine (oneTBB)
# Environment: ORCA_WASM_ROOT (work folder, default ./work), JOBS (default 4), NICE (default 10).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
source "$HERE/pin.sh"
export ORCA_WASM_ROOT=${ORCA_WASM_ROOT:-$REPO/work}
export VARIANT=${VARIANT:-st}
export JOBS=${JOBS:-4}
export EMCC_PIN
NICE=${NICE:-10}
mkdir -p "$ORCA_WASM_ROOT"

if [[ ! -f $ORCA_WASM_ROOT/emsdk/.emscripten ]]; then
  [[ -d $ORCA_WASM_ROOT/emsdk ]] || git clone --depth 1 https://github.com/emscripten-core/emsdk "$ORCA_WASM_ROOT/emsdk"
  (cd "$ORCA_WASM_ROOT/emsdk" && ./emsdk install "$EMCC_PIN" && ./emsdk activate "$EMCC_PIN")
fi

bash "$REPO/deps/fetch-deps.sh"
nice -n "$NICE" bash "$REPO/deps/build-deps.sh"
BAMBU_SRC=${BAMBU_SRC:-$ORCA_WASM_ROOT/BambuStudio} bash "$HERE/get-bambu.sh"
BAMBU_SRC=${BAMBU_SRC:-$ORCA_WASM_ROOT/BambuStudio} nice -n "$NICE" bash "$HERE/build-engine.sh" bambu_engine

OUT=$ORCA_WASM_ROOT/build-engine-$VARIANT/out
mkdir -p "$REPO/dist"
for f in "$OUT"/bambu-engine-$VARIANT.mjs "$OUT"/bambu-engine-$VARIANT.wasm; do
  cp "$f" "$REPO/dist/"
  b=$(basename "$f")
  if command -v brotli >/dev/null; then brotli -f -q 11 -o "$REPO/dist/$b.br" "$REPO/dist/$b"; fi
  gzip -9 -k -f "$REPO/dist/$b"
done
cp "$REPO/tools/slice-node.mjs" "$REPO/dist/"
(cd "$REPO/dist" && { command -v sha256sum >/dev/null && sha256sum * || shasum -a 256 *; } > SHA256SUMS)
echo "upstream: bambulab/BambuStudio $BAMBU_TAG ($BAMBU_COMMIT)" > "$REPO/dist/SOURCE.txt"
ls -la "$REPO/dist"

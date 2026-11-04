#!/usr/bin/env bash
# Fetches Bambu Studio at the pinned tag (shallow) into $ORCA_WASM_ROOT/BambuStudio and applies patches/.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/pin.sh"
ROOT=${ORCA_WASM_ROOT:?set ORCA_WASM_ROOT}
DEST=${BAMBU_SRC:-$ROOT/BambuStudio}
if [[ ! -d $DEST/.git ]]; then
  git clone --depth 1 --branch "$BAMBU_TAG" https://github.com/bambulab/BambuStudio "$DEST"
fi
got=$(git -C "$DEST" rev-parse HEAD)
[[ $got == "$BAMBU_COMMIT" ]] || { echo "BambuStudio at $got, expected $BAMBU_COMMIT ($BAMBU_TAG)" >&2; exit 1; }
for p in "$HERE"/../patches/*.patch; do
  [[ -e $p ]] || continue
  if git -C "$DEST" apply --check "$p" 2>/dev/null; then git -C "$DEST" apply "$p"; echo "applied $(basename "$p")";
  elif git -C "$DEST" apply --reverse --check "$p" 2>/dev/null; then echo "already applied $(basename "$p")";
  else echo "patch $(basename "$p") does not apply" >&2; exit 1; fi
done
echo "$DEST"

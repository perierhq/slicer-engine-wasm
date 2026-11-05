#!/usr/bin/env bash
# Assembles a release from a built dist/: the engine variants, the host scripts, the profiles, the corresponding source
# (Bambu Studio at the pinned commit + this recipe with its patches), SHA256SUMS and manifest.json.
#   scripts/package-release.sh <version, e.g. 0.1.0> [dist dir, default dist] [out dir, default release]
# Environment: ORCA_WASM_ROOT (default ./work, for the Bambu Studio checkout), REPOSITORY (default perierhq/slicer-engine-wasm).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
source "$HERE/pin.sh"
VERSION=${1:?version}
DIST=$(cd "${2:-$REPO/dist}" && pwd)
OUT=${3:-$REPO/release}
ROOT=${ORCA_WASM_ROOT:-$REPO/work}
BAMBU_SRC=${BAMBU_SRC:-$ROOT/BambuStudio}
REPOSITORY=${REPOSITORY:-perierhq/slicer-engine-wasm}
rm -rf "$OUT"; mkdir -p "$OUT"

RUNTIME="bambu-engine-core.mjs engine-st.mjs engine-st.wasm engine-mt.mjs engine-mt.wasm profiles.tar"
HOST="bambu-engine.mjs bambu-engine-worker.mjs"
for f in $RUNTIME $HOST; do cp "$DIST/$f" "$OUT/"; done

# Corresponding source: upstream exactly at the pinned commit (patches not applied: they are in the recipe), and this
# recipe at the commit being released.
[[ $(git -C "$BAMBU_SRC" rev-parse HEAD) == "$BAMBU_COMMIT" ]] || { echo "$BAMBU_SRC is not at $BAMBU_COMMIT" >&2; exit 1; }
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
TOP="slicer-engine-wasm-$VERSION-source"
git -C "$BAMBU_SRC" archive --format=tar --prefix="$TOP/BambuStudio/" "$BAMBU_COMMIT" > "$STAGE/upstream.tar"
git -C "$REPO" archive --format=tar --prefix="$TOP/recipe/" HEAD > "$STAGE/recipe.tar"
cat > "$STAGE/SOURCE.md" <<MD
# slicer-engine-wasm $VERSION: corresponding source

- \`BambuStudio/\`: Bambu Studio $BAMBU_TAG (commit $BAMBU_COMMIT), unmodified, from https://github.com/bambulab/BambuStudio
- \`recipe/\`: https://github.com/$REPOSITORY at $(git -C "$REPO" rev-parse HEAD): build scripts, CMake, shims, stubs, the
  engine's entry point, the host scripts and \`recipe/patches/\`, our changes to Bambu Studio (\`recipe/scripts/get-bambu.sh\`
  applies them with \`git apply\`)
- Third-party libraries (Boost, CGAL, oneTBB, ...): \`recipe/deps/fetch-deps.sh\` fetches them at the versions and SHA-256 in
  \`recipe/deps/SHA256SUMS\`

Build: \`BAMBU_SRC=\$PWD/BambuStudio VARIANTS="st mt" recipe/scripts/build.sh\` (see recipe/README.md). Licence: AGPL-3.0.
MD
python3 "$HERE/release-tools.py" source "$STAGE" "$OUT/source.tar.gz" "$TOP"
python3 "$HERE/release-tools.py" manifest "$OUT" "$VERSION" "$REPOSITORY" "$BAMBU_TAG" "$BAMBU_COMMIT" "$EMCC_PIN" "$RUNTIME" "$REPO/patches"
ls -la "$OUT"

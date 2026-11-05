#!/bin/bash
# Reference outputs from Bambu Studio's own command line on the Mac, for tests/expected.json.
#   tests/tools/mac-reference.sh <out-dir> <case>...      cases: the inputs in tests/inputs without .3mf
# Projects with a full configuration are sliced as they are (--slice 0). App-style projects (they only name presets)
# are sliced with their presets flattened by flatten-presets.py (--load-settings, --load-filaments), with their own
# filament colours (--filament-colour) so the CLI computes the flush matrix from them as the engine does.
# BAMBU (default: /Applications/BambuStudio.app/Contents/MacOS/BambuStudio) must be the pinned release (scripts/pin.sh).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$1; shift
BAMBU=${BAMBU:-/Applications/BambuStudio.app/Contents/MacOS/BambuStudio}
PROFILES=${PROFILES:-$(dirname "$(dirname "$BAMBU")")/Resources/profiles/BBL}
DATADIR=$OUT/bambu-data   # scratch settings, never the user's
bambu() { perl -e 'alarm shift; exec @ARGV' 900 "$BAMBU" --datadir "$DATADIR" --debug 1 "$@"; }
for name in "$@"; do
  input=$HERE/../inputs/$name.3mf
  plate=${PLATE:-0}
  dir=$OUT/$name; rm -rf "$dir"; mkdir -p "$dir"
  if [[ $name == *.app ]]; then
    colours=$(python3 "$HERE/flatten-presets.py" "$input" "$PROFILES" "$dir/presets")
    filaments=$(ls "$dir/presets"/filament_*.json | sort -V | tr '\n' ';' | sed 's/;$//')
    bambu --load-settings "$dir/presets/machine.json;$dir/presets/process.json" --load-filaments "$filaments" \
      --filament-colour "$colours" --slice "$plate" --outputdir "$dir" --export-settings "$dir/settings.json" "$input" > "$dir/log.txt" 2>&1
  else
    bambu --slice "$plate" --outputdir "$dir" "$input" > "$dir/log.txt" 2>&1
  fi
  echo "$name: $(cd "$dir" && shasum -a 256 plate_*.gcode | tr '\n' ' ')"
done

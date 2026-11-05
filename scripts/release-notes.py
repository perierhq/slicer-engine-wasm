#!/usr/bin/env python3
"""Release notes from the release's manifest.json and the CI test run.
  release-notes.py <release/manifest.json> <build/test-out>
"""
import json
import os
import sys

manifest = json.load(open(sys.argv[1]))
tests = sys.argv[2]
up = manifest["upstream"]
print(f"Bambu Studio {up['version']}'s slicing engine (libslic3r and its command-line slicing path) in WebAssembly.")
print()
print(f"- Upstream: [{up['tag']}]({up['repository']}/tree/{up['commit']}) (`{up['commit'][:12]}`), Emscripten {manifest['emscripten']}")
print(f"- Patches: {', '.join('`' + p + '`' for p in manifest['patches'])}")
print("- Corresponding source: `source.tar.gz` (Bambu Studio at the pinned commit + this recipe with its patches)")
print("- Not affiliated with Bambu Lab, Prusa Research or Muon3D. AGPL-3.0.")
print()
print("| File | Bytes | SHA-256 |")
print("|---|---|---|")
for f in manifest["files"]:
    print(f"| `{f['name']}` | {f['bytes']:,} | `{f['sha256']}` |")
print()
print("Node (ubuntu-latest, 4 vCPU) slice times in ms, every case byte-identical between st and mt and checked against "
      "tests/expected.json:")
print()
print("| Case | st | mt |")
print("|---|---|---|")
timings = {v: json.load(open(os.path.join(tests, v, "timings.json"))) for v in ("st", "mt") if os.path.exists(os.path.join(tests, v, "timings.json"))}
for case in timings.get("st", {}):
    print(f"| {case} | {timings['st'].get(case, '')} | {timings.get('mt', {}).get(case, '')} |")

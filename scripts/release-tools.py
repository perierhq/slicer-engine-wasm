#!/usr/bin/env python3
"""Helpers for scripts/package-release.sh.

  release-tools.py source <stage dir> <out.tar.gz> <top folder>
      Merges stage/upstream.tar, stage/recipe.tar and stage/SOURCE.md into one reproducible .tar.gz.
  release-tools.py manifest <release dir> <version> <repository> <upstream tag> <upstream commit> <emscripten> "<runtime files>" <patches dir>
      Writes manifest.json (format 2, as the app's EngineBundle reads it, plus provenance and every file) and SHA256SUMS.
"""
import gzip
import hashlib
import io
import json
import os
import sys
import tarfile


def source(stage, out, top):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as dst:
        for part in ("upstream.tar", "recipe.tar"):
            with tarfile.open(os.path.join(stage, part)) as src:
                for member in src.getmembers():
                    if member.type in (tarfile.XGLTYPE, tarfile.XHDTYPE) or member.name == "pax_global_header":
                        continue
                    dst.addfile(member, src.extractfile(member) if member.isfile() else None)
        data = open(os.path.join(stage, "SOURCE.md"), "rb").read()
        info = tarfile.TarInfo(f"{top}/SOURCE.md")
        info.size, info.mode, info.mtime = len(data), 0o644, 0
        dst.addfile(info, io.BytesIO(data))
    with open(out, "wb") as f, gzip.GzipFile(fileobj=f, mode="wb", compresslevel=9, mtime=0, filename="") as gz:
        gz.write(buf.getvalue())


def manifest(out, version, repository, tag, commit, emcc, runtime, patches):
    base = f"https://github.com/{repository}/releases/download/v{version}/"

    def entry(name):
        data = open(os.path.join(out, name), "rb").read()
        return {"name": name, "url": base + name, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}

    upstream_version = ".".join(str(int(x)) for x in tag.lstrip("v").split("."))
    files = sorted(f for f in os.listdir(out) if f not in ("manifest.json", "SHA256SUMS"))
    data = {
        "format": 2,
        "engine": f"Bambu Studio {upstream_version} slicing engine (slicer-engine-wasm)",
        "version": version,
        "license": "AGPL-3.0-only",
        "source": f"https://github.com/{repository}",
        "core": "bambu-engine-core.mjs",
        "module": "engine-st.mjs",
        "wasm": "engine-st.wasm",
        "profiles": "profiles.tar",
        "threaded": {"module": "engine-mt.mjs", "wasm": "engine-mt.wasm"},
        "host": {"api": "bambu-engine.mjs", "worker": "bambu-engine-worker.mjs"},
        "upstream": {"name": "Bambu Studio", "version": upstream_version, "repository": "https://github.com/bambulab/BambuStudio",
                     "tag": tag, "commit": commit},
        "emscripten": emcc,
        "patches": sorted(p for p in os.listdir(patches) if p.endswith(".patch")),
        "source_archive": entry("source.tar.gz"),
        "assets": [entry(n) for n in runtime.split()],
        "files": [entry(n) for n in files],
    }
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        for name in sorted(os.listdir(out)):
            if name != "SHA256SUMS":
                f.write(f"{hashlib.sha256(open(os.path.join(out, name), 'rb').read()).hexdigest()}  {name}\n")


if __name__ == "__main__":
    {"source": source, "manifest": manifest}[sys.argv[1]](*sys.argv[2:])

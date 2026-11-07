#!/usr/bin/env python3
"""Builds the engine's profiles bundle: Bambu Studio's system presets (resources/profiles/BBL) for the printer
families the engine serves, pruned to what those printers can use, as one uncompressed tar.

  make-profiles.py <BambuStudio source> <out.tar> [--list]

Kept, in BBL.json's own order (Bambu Studio loads the lists in order, parents first):
  * the printer models in MODELS and every machine preset for them;
  * every process and filament preset whose compatible_printers names one of those machines;
  * everything those presets inherit from or include (the @base filaments, fdm_* templates, G-code templates).
Everything else (other printers' presets, cover images, bed models) is left out. The tar holds BBL.json and the BBL/
files under the same relative paths, so the engine reads it exactly as Bambu Studio reads resources/profiles.
The output is byte-for-byte reproducible (sorted members, zero timestamps and owners).
"""
import io
import json
import os
import sys
import tarfile

# The single-nozzle printers: the P1, X1 and A1 families, and the P2S, H2S and A2L (since 0.1.1). H2D-class printers
# (two nozzles: H2D, H2D Pro, H2C, X2D) need the multi-extruder path the engine does not port.
MODELS = [
    "Bambu Lab X1 Carbon", "Bambu Lab X1", "Bambu Lab X1E",
    "Bambu Lab P1P", "Bambu Lab P1S",
    "Bambu Lab A1 mini", "Bambu Lab A1",
    "Bambu Lab P2S", "Bambu Lab H2S", "Bambu Lab A2L",
]
LISTS = {"machine_list": "machine", "process_list": "process", "filament_list": "filament"}


def main():
    src, out = sys.argv[1], sys.argv[2]
    root = os.path.join(src, "resources", "profiles")
    vendor = json.load(open(os.path.join(root, "BBL.json"), encoding="utf-8"))

    def load(sub_path):
        with open(os.path.join(root, "BBL", sub_path), encoding="utf-8") as f:
            return json.load(f)

    entries = {}  # list key -> [(name, sub_path, data)]
    by_name = {}
    for key in LISTS:
        entries[key] = []
        for item in vendor[key]:
            data = load(item["sub_path"])
            entries[key].append((item["name"], item["sub_path"], data))
            by_name[(LISTS[key], item["name"])] = data

    keep = set()  # (kind, name)

    def keep_with_parents(kind, name):
        if (kind, name) in keep:
            return
        data = by_name.get((kind, name))
        if data is None:
            raise SystemExit(f"{kind} preset {name!r} is referenced but not in BBL.json")
        keep.add((kind, name))
        if data.get("inherits"):
            keep_with_parents(kind, data["inherits"])
        includes = data.get("include", [])
        for include in [includes] if isinstance(includes, str) else includes:
            keep_with_parents(kind, include)

    machines = set()
    for name, _, data in entries["machine_list"]:
        if data.get("instantiation") == "true" and data.get("printer_model") in MODELS:
            machines.add(name)
            keep_with_parents("machine", name)
    for key in ("process_list", "filament_list"):
        kind = LISTS[key]
        for name, _, data in entries[key]:
            if data.get("instantiation") == "true" and machines.intersection(data.get("compatible_printers", [])):
                keep_with_parents(kind, name)

    pruned = {k: v for k, v in vendor.items() if k not in LISTS and k != "machine_model_list"}
    pruned["machine_model_list"] = [m for m in vendor["machine_model_list"] if m["name"] in MODELS]
    members = {}
    for model in pruned["machine_model_list"]:
        members["BBL/" + model["sub_path"]] = load(model["sub_path"])
    for key, kind in LISTS.items():
        pruned[key] = []
        for name, sub_path, data in entries[key]:
            if (kind, name) in keep:
                pruned[key].append({"name": name, "sub_path": sub_path})
                members["BBL/" + sub_path] = data
    # Same key order as Bambu's file (the loader doesn't care, people reading it might).
    ordered = {k: pruned[k] for k in vendor if k in pruned}

    files = {"BBL.json": json.dumps(ordered, indent=4, ensure_ascii=False).encode() + b"\n"}
    for path, data in members.items():
        with open(os.path.join(root, path), "rb") as f:
            files[path] = f.read()  # the original bytes

    with tarfile.open(out, "w", format=tarfile.USTAR_FORMAT) as tar:
        for path in sorted(files):
            info = tarfile.TarInfo(path)
            info.size = len(files[path])
            info.mtime = 0
            info.mode = 0o644
            tar.addfile(info, io.BytesIO(files[path]))

    counts = {k: len(v) for k, v in pruned.items() if isinstance(v, list)}
    size = sum(len(b) for b in files.values())
    print(f"{out}: {len(files)} files, {size / 1e6:.2f} MB of JSON; {counts}")
    if "--list" in sys.argv:
        for path in sorted(files):
            print(" ", path)


if __name__ == "__main__":
    main()

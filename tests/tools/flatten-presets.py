#!/usr/bin/env python3
"""The reference side of the preset-resolution test: flattens the presets an app-style 3MF names, the way Bambu Studio's
PresetBundle resolves system presets (inherits, then includes, then the preset's own keys), applies the 3MF's own values
to the preset that holds each key, and writes machine.json, process.json and filament_<n>.json for Bambu Studio's
command line (--load-settings / --load-filaments). The engine does the same inside (engine_main.cpp,
resolve_presets) with Bambu's own PresetBundle code; this script is the independent check, run on the Mac.

  flatten-presets.py <project.3mf> <BambuStudio resources/profiles/BBL> <out-dir>

Prints the filament colours, ';'-separated, for --filament-colour.
"""
import glob
import json
import os
import sys
import zipfile

META = {"inherits", "include", "instantiation"}
IDENTITY = {"printer_settings_id", "print_settings_id", "filament_settings_id", "inherits_group", "different_settings_to_system"}


def index(profiles, kind):
    presets = {}
    for path in glob.glob(os.path.join(profiles, kind, "**", "*.json"), recursive=True):
        data = json.load(open(path, encoding="utf-8"))
        if isinstance(data, dict) and "name" in data:
            presets[data["name"]] = data
    return presets


def resolve(presets, name, depth=0):
    if depth > 32:
        raise SystemExit(f"{name}: inherits in a loop")
    preset = presets.get(name)
    if preset is None:
        raise SystemExit(f"No Bambu Studio preset is called {name}")
    config = {}
    if preset.get("inherits"):
        config.update(resolve(presets, preset["inherits"], depth + 1))
    includes = preset.get("include", [])
    for include in [includes] if isinstance(includes, str) else includes:
        config.update({k: v for k, v in resolve(presets, include, depth + 1).items() if k not in ("name", "type", "from", "setting_id")})
    config.update({k: v for k, v in preset.items() if k not in META})
    return config


def main():
    project, profiles, out = sys.argv[1:4]
    config = json.loads(zipfile.ZipFile(project).read("Metadata/project_settings.config"))
    printer, process, filaments = config["printer_settings_id"], config["print_settings_id"], config["filament_settings_id"]
    configs = {
        "machine": [resolve(index(profiles, "machine"), printer)],
        "process": [resolve(index(profiles, "process"), process)],
        "filament": [resolve(index(profiles, "filament"), name) for name in filaments],
    }
    # The project's values go to the preset that holds the key; per-filament lists are split over the filaments.
    for key, value in config.items():
        if key in IDENTITY:
            continue
        if key in configs["filament"][0] and isinstance(value, list) and len(value) == len(filaments):
            for n, filament in enumerate(configs["filament"]):
                filament[key] = [value[n]]
            continue
        holder = next((kind for kind in ("machine", "filament", "process") if key in configs[kind][0]), None)
        if holder is None:
            continue  # not a preset key (project-level, or unknown to Bambu Studio): the 3MF itself carries it
        for preset in configs[holder]:
            preset[key] = value
    os.makedirs(out, exist_ok=True)
    names = {"machine": [printer], "process": [process], "filament": filaments}
    for kind, presets in configs.items():
        for n, (preset, name) in enumerate(zip(presets, names[kind])):
            preset.update({"name": name, "type": kind, "from": "system", "instantiation": "true"})
            file = f"{kind}.json" if kind != "filament" else f"filament_{n + 1}.json"
            json.dump(preset, open(os.path.join(out, file), "w"), indent=1, sort_keys=True)
    print(";".join(config.get("filament_colour", [])))


if __name__ == "__main__":
    main()

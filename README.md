# slicer-engine-wasm

Bambu Studio's slicing engine (libslic3r and its command-line slicing path, without the GUI), compiled to WebAssembly so it can run on a phone, offline.

- **Upstream:** [Bambu Studio](https://github.com/bambulab/BambuStudio) by Bambu Lab, itself based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research and Slic3r by Alessandro Ranellucci. All AGPL-3.0
- **Prior art:** the WebAssembly build approach follows [Muon3D's slicer engine](https://github.com/Muon-3D/muon3d-slicer-engine) (AGPL-3.0), which compiles OrcaSlicer's engine
- **Licence:** AGPL-3.0, like everything it's built from. See `LICENSE`
- **Not affiliated** with Bambu Lab, Prusa Research or Muon3D. "Bambu Studio" is Bambu Lab's name for its software, used here only to say where the code comes from

## What it does

A Bambu Studio 3MF project goes in; G-code for every plate (or one) and a result with each plate's slice information come out. The output is Bambu Studio's own: on the test projects it is byte-identical to `BambuStudio --slice` on the Mac (see [Status](#status)).

- **Projects with a full configuration** (what Bambu Studio writes) are sliced as the command line slices them.
- **Projects that only name their presets** (`printer_settings_id`, `print_settings_id`, `filament_settings_id`) and carry the values they change are resolved inside the engine from Bambu's system presets (bundled as `profiles.tar`): each preset flattened as Bambu Studio resolves it (inherits, includes, own keys), the project's values put into the preset that holds each key, then composed the way the command line composes `--load-settings` / `--load-filaments` (printer, process, each filament into its slot, per extruder variant).
- **Flush volumes:** a project without a `flush_volumes_matrix` gets Bambu's, computed from the filament colours (`FlushVolCalculator`, the GUI's `get_min_flush_volumes`).
- **Plates:** every plate (`--slice 0`, including its pre-check pass) or plate *n* (`--slice n`), at Bambu Studio's plate origins, with each plate's own settings (bed type, print sequence, ...).
- **Printers:** the bundled profiles cover Bambu's single-nozzle printers: the P1, X1 and A1 families (P1P, P1S, X1, X1 Carbon, X1E, A1, A1 mini) and, since 0.1.1, the P2S, H2S and A2L. Projects with a full configuration for other single-nozzle printers slice too. Multi-nozzle printers (H2D class: H2D, H2D Pro, H2C, X2D) are refused.

## Use it from a page

A release holds:

| File | What |
|---|---|
| `engine-st.wasm`, `engine-st.mjs` | the single-threaded engine and its Emscripten glue |
| `engine-mt.wasm`, `engine-mt.mjs` | the threaded engine (pthreads + oneTBB): needs a cross-origin isolated page (COOP `same-origin`, COEP `require-corp`) |
| `profiles.tar` | Bambu's system presets for its single-nozzle printers (P1, X1, A1 families, P2S, H2S, A2L; BBL.json and BBL/, pruned by `scripts/make-profiles.py`) |
| `bambu-engine-core.mjs` | one slice in a fresh engine instance (`runSlice`): runs wherever the engine runs (a worker, Node) |
| `bambu-engine.mjs`, `bambu-engine-worker.mjs` | the page API below: a worker per slice, progress, cancel |
| `manifest.json` | version, upstream tag and commit, patches, every file with size and SHA-256 (format 2, as the app reads it) |
| `SHA256SUMS`, `source.tar.gz` | checksums; the corresponding source (Bambu Studio at the pinned commit + this recipe and its patches) |

```js
import * as engine from './bambu-engine.mjs';

await engine.init({ variant: 'auto' });   // 'st', 'mt', or 'auto': mt when the page is cross-origin isolated
const { plates, result, log } = await engine.slice({
  input: bytes,                           // Uint8Array/ArrayBuffer of the .3mf, or a URL
  plate: 0,                               // 0: every plate; n: plate n
  onProgress: (percent, stage, plate, plateCount) => {},
  signal,                                 // optional AbortSignal
});
// plates: [{ index, name, gcode: Uint8Array, info }]
engine.cancel();                          // ends running slices; they reject with an AbortError
engine.events.addEventListener('progress', (e) => e.detail);  // {percent, stage, plate, plateCount}
```

`init` compiles the engine once and fetches the profiles; every `slice` starts a new worker with a fresh engine instance from the compiled module (about 15–30 ms): like the command line, which slices once per process, Bambu labels objects with a process-wide counter. Cancelling ends the worker, which also frees its memory. Errors reject with `err.code` (the engine's return code), `err.result` and `err.log`.

Each plate's `info` carries:

- `slice_info`: the fields of Bambu Studio's `Metadata/slice_info.config` for the plate: `prediction`, `weight`, `first_layer_time`, `outside`, `support_used`, `label_object_enabled`, `nozzle_diameters`, `printer_model_id`, `filament_maps`, `filaments` (id, `tray_info_idx`, type, colour, `used_m`, `used_g`, used for object/support, load and unload times), `objects` (`identify_id`, name, skipped), `warnings`
- the command line's `result.json` numbers: `time_s`, `main_time_s`, `feature_times_s`, `filament_changes`, `objects` (id, name, triangles, bounding box)
- per filament: `total_g`, `total_m`, `model_g`, `support_g`, `flush_g`, `tower_g`; and `total_g`, `flush_g` for the plate

`result` also says how the configuration was obtained (`presets`: `project` or `resolved`, `flush_matrix`: `project` or `computed`) and lists `notes` (presets not listed for the printer, project values Bambu Studio doesn't know, the command line's prime tower rule).

Options for `slice` (and `runSlice`): `presets: 'auto' | 'resolve' | 'project'` (auto resolves when the project has no `inherits_group` / `different_settings_to_system`) and `logLevel` (Bambu's 0–5, default 1). The output does not depend on the log level.

### Lower level

`bambu-engine-core.mjs` without the page API (as the tests do in Node):

```js
import { runSlice } from './bambu-engine-core.mjs';
const { default: createEngine } = await import('./engine-st.mjs');
const wasmModule = await WebAssembly.compile(wasmBytes);   // keep it; one instance per slice
const { plates, result, log } = await runSlice(createEngine, { wasmModule, profiles: tarBytes, input, plate: 0, onProgress });
```

The engine itself exports `engine_init(resources_dir, log_level)` and `engine_run(options_json)` (`{"input", "outdir", "plate", "presets"}`); it writes `plate_<n>.gcode` and `result.json` to `outdir` and calls `Module.onProgress(percent, stage, plate, plateCount)`. `tools/slice-node.mjs` slices a file from the command line: `node tools/slice-node.mjs <dist> <project.3mf> <outdir> [plate] [st|mt]`.

## Status

Compared with Bambu Studio 2.8.2.61's own command line on macOS (arm64), `tests/expected.json`, checked by CI for both variants:

| Input | Result |
|---|---|
| 20 mm cube, two-colour plate (full configuration) | byte-identical |
| three-plate project, `--slice 0` and `--slice 2` | byte-identical, every plate |
| cube and two colours as the app writes them (preset names, no flush matrix) | byte-identical to the CLI on the same presets flattened (`--load-settings`); flush matrix `0,123,471,0` as Bambu's |
| four-colour desk plate and support-heavy hook, as the app writes them | config block identical, totals equal to the last digit (time within 0.01 %); a few hundred of 190,000–360,000 lines differ by rounding. Bambu's arm64 and x86-64 builds differ from each other the same way (fused multiply-add) |

The threaded engine produces the same bytes as the single-threaded one on every test.

**0.1.1** changes only `profiles.tar`: the same Bambu Studio 2.8.2.61 profiles with the P2S, H2S and A2L added. Every test above is byte-identical with it (both variants), and so are app-style projects for the A1 mini. The three printers slice from their own presets (`0.20mm Standard @BBL P2S` and so on); there is no 2.8.2.61 command-line reference for them on the test Mac (it has Bambu Studio 2.9.1.70), whose command line, on the same 2.8.2.61 profiles, prints the same filament grams and an estimate 11–13 s longer on a 20 mm cube; it is 9 s longer for the P1S on the same cube, so the gap is the 2.9 engine's, not the profiles'. Node on the Mac (11 cores, shared): hook 7.6 s st, 3.5 s mt; desk 3.5 s st, 1.9 s mt; cube 0.4 s.

Sizes: `engine-st.wasm` 9.8 MB (2.2 MB brotli), `engine-mt.wasm` 10.4 MB (2.2 MB brotli), glue 71 / 91 KB, `profiles.tar` 4.0 MB (1,567 files, 2.8 MB of JSON: 108 machine, 190 process, 1,258 filament presets; 0.1.0's, for the P1, X1 and A1 families only, was 2.5 MB).

## Build

```bash
scripts/build.sh                          # st: emsdk, dependencies, the pinned Bambu Studio, the engine, into dist/
VARIANTS="st mt" scripts/build.sh         # both variants
node tests/run.mjs dist st                # compare with Bambu Studio's own output (and: dist mt)
scripts/package-release.sh 0.1.0 dist     # release/: assets, source.tar.gz, SHA256SUMS, manifest.json
```

`ORCA_WASM_ROOT` (default `./work`) holds the emsdk, sources and build trees: about 5 GB per variant. `DIST` (default `./dist`), `JOBS` (default 4) and `NICE` (default 10). On a 4-core runner a first build of both variants takes about an hour; CI caches the emsdk and the dependency prefixes. A `v*` tag builds, tests and publishes the release (`.github/workflows/build.yml`).

On macOS the emsdk needs Python ≥ 3.10 first on `PATH` (e.g. `PATH=/opt/homebrew/bin:$PATH`).

## What's here

This repository holds a build recipe, not a copy of Bambu Studio:

- `scripts/`: pins (`pin.sh`), fetch Bambu Studio and apply `patches/` (`get-bambu.sh`), build (`build.sh`, `build-engine.sh`), the profiles bundle (`make-profiles.py`), the release (`package-release.sh`)
- `deps/`: fetch and build the third-party libraries with Emscripten (Boost, CGAL, cereal, NLopt, libnoise, Qhull, libjpeg-turbo; oneTBB for the threaded build), SHA-256 pinned
- `engine/`: the out-of-tree CMake build of Bambu's `libslic3r`, the shims and stubs that replace desktop-only code (OCCT, OpenCV, assimp, OpenVDB, OpenSSL, wxWidgets, tbbmalloc), and `bridge/engine_main.cpp`, the slicing path of Bambu Studio's command line
- `host/`: the JavaScript around the engine (above)
- `patches/`: our changes to the pinned release, one topic per patch, each with its reason
- `tests/`: inputs, `expected.json` (SHA-256 of Bambu Studio's own output), `run.mjs`; `tests/tools/` makes the references on a Mac (`mac-reference.sh`, `flatten-presets.py`) and the multi-plate input

### Patches

| Patch | Why |
|---|---|
| `0001-utils-hpp-emscripten-is-unix` | `PathSanitizer` doesn't compile on a platform that is neither Apple nor Linux; Emscripten is treated as Linux (log masking only) |
| `0002-bbs-3mf-remove-backup-without-thread` | `~Model` hands its unpacked 3MF to a backup thread the single-threaded build can't start; an exception inside the destructor terminated the engine when a 3MF failed to load |
| `0003-floating-concentric-out-of-bounds-reads` | a use-after-free (an `EdgeGrid` over a freed temporary) and an out-of-bounds read in the floating-vertical-shell fill. They made the output depend on the heap's history (log level, allocator, threads) and crashed a desk plate. Bambu Studio's own result is kept, deterministically |
| `0004-per-filament-lists-read-past-end` | per-filament lists shorter than the filament count (which the command line itself produces) were read past their end in `Print` and `WipeTower`; past the end now reads 0, as it did in practice |

They were found with AddressSanitizer on the wasm build. None changes Bambu Studio's output on the test projects.

## Pinned versions

| | Version |
|---|---|
| Bambu Studio | `v02.08.02.61` (`926a7192574b`) |
| Emscripten | 6.0.13 |
| Boost | 1.84.0 (as upstream) |
| CGAL | 5.6.3 (upstream pins 5.4, which needs GMP; 5.6.3 has a GMP-free mode) |
| oneTBB | 2021.12.0 for threads (upstream pins 2021.5.0, which has no wasm support); the single-threaded build uses a serial header shim |
| Qhull, NLopt, cereal, libnoise, libjpeg-turbo | as upstream |
| zlib, libpng | Emscripten ports |

Left out, as the command-line slicing path does not need them: OpenCASCADE (STEP import), OpenCV, assimp, OpenVDB (only SLA hollowing and an old tree-support path that is compiled out), OpenSSL and libcurl (encrypted log upload), wxWidgets, GLEW/GLFW (thumbnails; the CLI writes none for Bambu printers), FFmpeg, tbbmalloc (`scalable_malloc` goes to mimalloc).

## Updating to a new Bambu Studio release

1. `scripts/pin.sh`: the new tag and its commit (`git ls-remote https://github.com/bambulab/BambuStudio refs/tags/<tag>`).
2. `scripts/get-bambu.sh` into a fresh `BAMBU_SRC`: it stops if a patch no longer applies. Rebase the patch on the new source (`git diff` after fixing it by hand), or drop it if upstream fixed the bug; keep its explanation at the top.
3. Build (`scripts/build-engine.sh bambu_engine`). Compile errors usually come from new desktop-only code: add the file to `BAMBU_LIBSLIC3R_EXCLUDED` (`engine/cmake/BambuSources.cmake`) or a stub in `engine/stubs/`. `src/BambuStudio.cpp` is what `engine/bridge/engine_main.cpp` ports: diff it between the two tags and carry changes in the ported sections over (the comments cite `BS:<line>`).
4. New references on a Mac with that Bambu Studio installed: `tests/tools/mac-reference.sh /tmp/ref cube twocolour multiplate cube.app twocolour.app desk.app hook.app desk-engine.app` (and `PLATE=2 ... multiplate` for `--slice 2`); put the hashes into `tests/expected.json` and run `node tests/run.mjs dist st` and `mt`. Byte differences on cube, two colours or the plates mean something in the port is off; desk and hook compare by config block and totals.
5. Optionally an AddressSanitizer build (add `-fsanitize=address` to the prefix's initial cache flags for a separate build tree) over the test inputs.
6. Tag `v<x.y.z>`: CI builds, tests and publishes the release with its manifest.

The 2.8.2.61 adaptation: 4 patches, 9 stub files, about 1,300 lines of entry point.

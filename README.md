# slicer-engine-wasm

Bambu Studio's slicing engine (libslic3r and its command-line slicing path, without the GUI), compiled to WebAssembly so it can run on a phone, offline.

- **Upstream:** [Bambu Studio](https://github.com/bambulab/BambuStudio) by Bambu Lab, itself based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research and Slic3r by Alessandro Ranellucci. All AGPL-3.0
- **Prior art:** the WebAssembly build approach follows [Muon3D's slicer engine](https://github.com/Muon-3D/muon3d-slicer-engine) (AGPL-3.0), which compiles OrcaSlicer's engine
- **Licence:** AGPL-3.0, like everything it's built from. See `LICENSE`
- **Not affiliated** with Bambu Lab, Prusa Research or Muon3D. "Bambu Studio" is Bambu Lab's name for its software, used here only to say where the code comes from

## What's here

This repository holds a build recipe, not a copy of Bambu Studio:

- `scripts/`: fetch a pinned Bambu Studio release (`pin.sh`, `get-bambu.sh`), build the engine (`build.sh`, `build-engine.sh`)
- `deps/`: fetch and build the third-party libraries with Emscripten (Boost, CGAL, cereal, NLopt, libnoise, Qhull, libjpeg-turbo; oneTBB for the threaded build)
- `engine/`: the out-of-tree CMake build of Bambu's `libslic3r`, the shims and stubs that replace desktop-only code (OCCT, OpenCV, assimp, OpenVDB, OpenSSL, wxWidgets), and `bridge/engine_main.cpp`, the one-plate slicing path of Bambu Studio's command line
- `patches/`: our changes on top of the pinned release, one topic per patch
- `tools/slice-node.mjs`: slice a 3MF under Node
- `.github/workflows/`: CI that builds the engine

Each release carries the built `engine.wasm` and its JavaScript glue, their SHA-256 checksums, the exact upstream commit, and a source archive of upstream plus patches. That archive is the corresponding source for the binaries.

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

Left out, as the command-line slicing path does not need them: OpenCASCADE (STEP import), OpenCV, assimp, OpenVDB (only SLA hollowing and an old tree-support path that is compiled out), OpenSSL and libcurl (encrypted log upload), wxWidgets, GLEW/GLFW (thumbnails; the CLI writes none for Bambu printers), FFmpeg.

## Build

```bash
scripts/build.sh          # emsdk, dependencies, the pinned Bambu Studio, the engine, into dist/
```

`ORCA_WASM_ROOT` (default `./work`) holds the emsdk, sources and build trees: about 5 GB. `JOBS` (default 4) and `NICE` (default 10) keep the machine usable. On a 4-core runner the first build takes roughly an hour; CI caches the emsdk and the dependency prefix.

## Interface

The engine talks to its host through files only: a Bambu 3MF project with a full configuration goes in, G-code and a result file come out.

```js
const Module = await createBambuEngine();
Module.FS.writeFile('/in/plate.3mf', bytes);
Module.ccall('engine_init', null, ['string', 'number'], ['/resources', 1]);
const rc = Module.ccall('engine_slice', 'number', ['string', 'string', 'number'], ['/in/plate.3mf', '/out', 0]);
const gcode = Module.FS.readFile('/out/plate_1.gcode');
const result = JSON.parse(new TextDecoder().decode(Module.FS.readFile('/out/result.json')));
```

This is `BambuStudio --slice 0 --outputdir /out plate.3mf`. Use a fresh instance per slice (keep the compiled `WebAssembly.Module`): like the CLI, Bambu labels objects with a process-wide id counter.

Not ported yet: `--load-settings`/`--load-filaments`, flush-matrix computation for projects without one, arrange and orient, multi-extruder printers (H2D class), plates other than the first.

## Status

Feasibility spike. Compared with Bambu Studio 2.8.2.61's own command line on macOS for the same 3MF:

- 20 mm cube, two-colour plate and two calibration projects: byte-identical G-code
- support-heavy hook and a four-colour desk plate: same layers, features, filament per tool to 0.01 g, start and end G-code; a few hundred of 190,000–360,000 lines differ by rounding (time estimate +0.09 % and +0.02 %). Bambu's own x86-64 and arm64 builds differ from each other more than that, so this is floating-point noise, not a different slicer

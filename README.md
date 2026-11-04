# slicer-engine-wasm

Bambu Studio's slicing engine (libslic3r and its command-line slicing path, without the GUI), compiled to WebAssembly so it can run on a phone, offline.

- **Upstream:** [Bambu Studio](https://github.com/bambulab/BambuStudio) by Bambu Lab, itself based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research and Slic3r by Alessandro Ranellucci. All AGPL-3.0
- **Prior art:** the WebAssembly build approach follows [Muon3D's slicer engine](https://github.com/Muon-3D/muon3d-slicer-engine) (AGPL-3.0), which compiles OrcaSlicer's engine
- **Licence:** AGPL-3.0, like everything it's built from. See `LICENSE`
- **Not affiliated** with Bambu Lab, Prusa Research or Muon3D. "Bambu Studio" is Bambu Lab's name for its software, used here only to say where the code comes from

## What's here

This repository holds a build recipe, not a copy of Bambu Studio:

- `scripts/`: fetch a pinned Bambu Studio release, build the dependencies and the engine with Emscripten
- `patches/`: our changes on top of the pinned release, one topic per patch
- `.github/workflows/`: CI that builds the engine and publishes releases

Each release carries the built `engine.wasm` and its JavaScript glue, their SHA-256 checksums, the exact upstream commit, and a source archive of upstream plus patches. That archive is the corresponding source for the binaries.

## Interface

The engine talks to its host through files only: a 3MF project with a full configuration goes in, G-code and slice metadata come out. Details follow with the first release.

## Status

Feasibility spike: the goal is G-code identical to the desktop Bambu Studio for the same 3MF.

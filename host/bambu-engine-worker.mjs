// One slice in a Web Worker (started by bambu-engine.mjs): a fresh engine instance from the compiled module, progress
// to the page, the results back as transferable buffers.
// SPDX-License-Identifier: AGPL-3.0-only
import { runSlice } from './bambu-engine-core.mjs';

self.addEventListener('message', async ({ data }) => {
  const { compiled, variant, baseUrl, profiles, input, plate, presets, logLevel } = data;
  let last = { percent: -1, stage: '' };
  let log = [];
  try {
    const glue = new URL(`engine-${variant}.mjs`, baseUrl).href;
    const { default: createEngine } = await import(glue);
    const run = await runSlice(createEngine, {
      wasmModule: compiled ?? await WebAssembly.compileStreaming(fetch(new URL(`engine-${variant}.wasm`, baseUrl))),
      profiles, input, plate, presets, logLevel,
      moduleArgs: variant === 'mt' ? { mainScriptUrlOrBlob: glue } : {},
      onProgress(percent, stage, plateNumber, plateCount) {
        // Whole percents, and the stage when it changes (without the per-layer counter).
        const name = stage.replace(/: layer \d+$/, '');
        if (percent === last.percent && name === last.stage) return;
        last = { percent, stage: name };
        self.postMessage({ kind: 'progress', percent, stage: name, plate: plateNumber, plateCount });
      },
    });
    log = run.log;
    const plates = run.plates.map((p) => ({ index: p.index, name: p.name, gcode: p.gcode, info: p.info }));
    self.postMessage({ kind: 'done', plates, result: run.result, log }, plates.map((p) => p.gcode.buffer));
  } catch (error) {
    // An abort in the engine (out of memory, a failed assertion, a trap) ends up here.
    self.postMessage({ kind: 'error', message: String(error?.message ?? error), log: error?.log ?? log.slice(-40) });
  }
});

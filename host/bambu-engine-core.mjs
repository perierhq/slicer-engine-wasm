// One slice in one fresh engine instance: the part of the host that runs wherever the engine runs (the page's worker,
// Node). Instantiates the compiled module, puts the profiles and the project into the engine's file system, runs
// engine_run (engine/bridge/engine_main.cpp) and reads the results back.
// SPDX-License-Identifier: AGPL-3.0-only

/** Unpacks an uncompressed (ustar) tar into the engine's file system under `root`. */
export function untarInto(FS, bytes, root) {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const decoder = new TextDecoder();
  const field = (offset, length) => {
    const raw = u8.subarray(offset, offset + length);
    const end = raw.indexOf(0);
    return decoder.decode(end === -1 ? raw : raw.subarray(0, end));
  };
  for (let offset = 0; offset + 512 <= u8.length;) {
    const name = field(offset, 100);
    if (!name) break;
    const size = parseInt(field(offset + 124, 12).trim() || '0', 8);
    const type = String.fromCharCode(u8[offset + 156] || 48);
    const prefix = field(offset + 345, 155);
    const path = `${root}/${prefix ? prefix + '/' : ''}${name}`;
    const data = offset + 512;
    if (type === '0' || type === '\0') {
      FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
      FS.writeFile(path, u8.subarray(data, data + size));
    } else if (type === '5') {
      FS.mkdirTree(path);
    }
    offset = data + Math.ceil(size / 512) * 512;
  }
}

/**
 * Runs one slice.
 * @param {Function} createEngine  the Emscripten factory (default export of engine-<variant>.mjs)
 * @param {object} job
 *   wasmModule  compiled WebAssembly.Module (kept by the caller and reused; each slice gets a new instance)
 *   profiles    the profiles tar (Uint8Array/ArrayBuffer), or null for projects that carry their full configuration
 *   input       the project (Uint8Array)
 *   plate       0 for every plate (as BambuStudio --slice 0), n for plate n
 *   presets     'auto' (default) | 'resolve' | 'project'
 *   logLevel    Bambu's log level 0-5 (default 1: errors)
 *   onProgress  (percent, stage, plate, plateCount) => void
 *   moduleArgs  extra Emscripten module arguments (e.g. mainScriptUrlOrBlob for threads)
 * @returns {{plates: {index, name, gcode: Uint8Array, info}[], result, log: string[]}}
 */
export async function runSlice(createEngine, job) {
  const log = [];
  const Module = await createEngine({
    ...(job.moduleArgs ?? {}),
    instantiateWasm(imports, done) {
      WebAssembly.instantiate(job.wasmModule, imports).then((instance) => done(instance, job.wasmModule));
      return {};
    },
    print: (s) => log.push(s),
    printErr: (s) => log.push(s),
    onProgress: job.onProgress,
  });
  try {
    return run(Module, job, log);
  } catch (error) {
    // A trap or abort in the engine: keep its last log lines with the error.
    if (error && typeof error === 'object') error.log = log.slice(-40);
    throw error;
  } finally {
    // Threads: stop the pthread workers this instance started.
    Module.PThread?.terminateAllThreads?.();
  }
}

function run(Module, job, log) {
  {
    const { FS } = Module;
    for (const dir of ['/in', '/out', '/tmp', '/data', '/resources']) FS.mkdirTree(dir);
    if (job.profiles) untarInto(FS, job.profiles, '/resources/profiles');
    FS.writeFile('/in/project.3mf', job.input instanceof Uint8Array ? job.input : new Uint8Array(job.input));
    Module.ccall('engine_init', null, ['string', 'number'], ['/resources', job.logLevel ?? 1]);
    const options = JSON.stringify({ input: '/in/project.3mf', outdir: '/out', plate: job.plate ?? 0, presets: job.presets ?? 'auto' });
    const rc = Module.ccall('engine_run', 'number', ['string'], [options]);
    let result;
    try {
      result = JSON.parse(new TextDecoder().decode(FS.readFile('/out/result.json')));
    } catch {
      result = { return_code: rc, error_string: log.slice(-5).join('\n') || 'the engine wrote no result' };
    }
    const plates = [];
    for (const info of result.plates ?? []) {
      plates.push({ index: info.index, name: info.name, gcode: FS.readFile('/out/' + info.gcode), info });
    }
    result.heap_bytes = Module.wasmMemory?.buffer.byteLength;
    return { plates, result, log };
  }
}

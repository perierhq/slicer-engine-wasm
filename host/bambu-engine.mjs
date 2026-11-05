// The engine's host API for a web page: compile the engine once, run each slice in its own Web Worker with a fresh
// engine instance, report progress, cancel by ending the worker.
//
//   import * as engine from './bambu-engine.mjs';
//   await engine.init({ variant: 'auto' });                     // 'st', 'mt' or 'auto' (mt when cross-origin isolated)
//   const { plates, result, log } = await engine.slice({ input: bytes, plate: 0, onProgress });
//   engine.cancel();                                            // ends the running slice: it rejects with AbortError
//
// The page needs the files of a release next to this module (or at `baseUrl`): bambu-engine-worker.mjs,
// bambu-engine-core.mjs, engine-<variant>.mjs and .wasm, and profiles.tar. The threaded variant needs
// crossOriginIsolated (COOP same-origin + COEP require-corp) for SharedArrayBuffer.
// SPDX-License-Identifier: AGPL-3.0-only

const state = { variant: null, baseUrl: null, compiled: null, profiles: null, compileMs: 0, running: new Set() };

/** True when the page can run the threaded engine (SharedArrayBuffer and shared WebAssembly memory). */
export function canUseThreads() {
  if (!globalThis.crossOriginIsolated || typeof SharedArrayBuffer !== 'function') return false;
  try {
    new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
    return true;
  } catch {
    return false;
  }
}

/**
 * Compiles the engine and loads the profiles, once. Calling it again with another variant switches.
 * @param {{variant?: 'auto'|'st'|'mt', baseUrl?: string|URL, profiles?: boolean}} options
 * @returns {Promise<{variant: string, compileMs: number, threads: boolean}>}
 */
export async function init({ variant = 'auto', baseUrl = new URL('.', import.meta.url), profiles = true } = {}) {
  const chosen = variant === 'auto' ? (canUseThreads() ? 'mt' : 'st') : variant;
  if (chosen === 'mt' && !canUseThreads()) throw new Error('The threaded engine needs a cross-origin isolated page');
  const base = new URL(baseUrl, location.href).href;
  if (state.compiled && state.variant === chosen && state.baseUrl === base) return { variant: chosen, compileMs: state.compileMs, threads: chosen === 'mt' };
  const t0 = performance.now();
  const [compiled, tar] = await Promise.all([
    WebAssembly.compileStreaming(fetch(new URL(`engine-${chosen}.wasm`, base))),
    profiles ? fetch(new URL('profiles.tar', base)).then((r) => {
      if (!r.ok) throw new Error(`profiles.tar: ${r.status}`);
      return r.arrayBuffer();
    }) : null,
  ]);
  Object.assign(state, { variant: chosen, baseUrl: base, compiled, profiles: tar, compileMs: performance.now() - t0 });
  return { variant: chosen, compileMs: state.compileMs, threads: chosen === 'mt' };
}

/** Progress of running slices: 'progress' events with detail {percent, stage, plate, plateCount}. */
export const events = new EventTarget();

/**
 * Slices a Bambu 3MF project in a fresh engine instance.
 * @param {object} job
 *   input       Uint8Array | ArrayBuffer | string|URL (fetched)
 *   plate       0: every plate (as BambuStudio --slice 0); n: plate n
 *   presets     'auto' | 'resolve' | 'project' (see README)
 *   logLevel    Bambu's log level 0-5 (default 1)
 *   onProgress  (percent, stage, plate, plateCount) => void
 *   signal      an AbortSignal that cancels the slice
 * @returns {Promise<{plates: {index: number, name: string, gcode: Uint8Array, info: object}[], result: object, log: string[]}>}
 *   Rejects with the engine's error (err.code, err.result, err.log) or an AbortError when cancelled.
 */
export async function slice({ input, plate = 0, presets = 'auto', logLevel = 1, onProgress, signal } = {}) {
  if (!state.compiled) throw new Error('Call init() first');
  if (typeof input === 'string' || input instanceof URL) input = new Uint8Array(await (await fetch(input)).arrayBuffer());
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const worker = new Worker(new URL('bambu-engine-worker.mjs', state.baseUrl), { type: 'module', name: 'bambu-engine' });
  return new Promise((resolve, reject) => {
    const entry = { worker, reject };
    const finish = () => {
      state.running.delete(entry);
      worker.terminate();
      signal?.removeEventListener('abort', abort);
    };
    const abort = () => {
      finish();
      reject(new DOMException('The slice was cancelled', 'AbortError'));
    };
    entry.cancel = abort;
    if (signal?.aborted) return abort();
    signal?.addEventListener('abort', abort);
    state.running.add(entry);
    worker.addEventListener('message', ({ data }) => {
      if (data.kind === 'progress') {
        onProgress?.(data.percent, data.stage, data.plate, data.plateCount);
        events.dispatchEvent(new CustomEvent('progress', { detail: data }));
      } else if (data.kind === 'done') {
        finish();
        if (data.result.return_code === 0) resolve({ plates: data.plates, result: data.result, log: data.log });
        else reject(Object.assign(new Error(data.result.error_string || 'The engine failed'), { code: data.result.return_code, result: data.result, log: data.log }));
      } else if (data.kind === 'error') {
        finish();
        reject(Object.assign(new Error(data.message), { code: 100, log: data.log }));
      }
    });
    worker.addEventListener('error', (event) => {
      finish();
      reject(Object.assign(new Error(`The engine worker failed: ${event.message || 'unknown error'}`), { code: 101 }));
    });
    worker.postMessage({
      compiled: state.compiled, variant: state.variant, baseUrl: state.baseUrl, profiles: state.profiles,
      input: bytes, plate, presets, logLevel,
    });
  });
}

/** Cancels every running slice (each rejects with an AbortError). */
export function cancel() {
  for (const entry of [...state.running]) entry.cancel();
}

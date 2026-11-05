#!/usr/bin/env node
// Slices every case in tests/expected.json with the built engine under Node, through the same host code the page uses
// (host/bambu-engine-core.mjs), and compares the SHA-256 of each output with the one Bambu Studio's own command line
// produced for the same input (expected.json says how each reference was made).
//   node tests/run.mjs [dist dir, default dist] [variant st|mt, default st] [case name ...]
// The dist dir holds engine-<variant>.mjs/.wasm and profiles.tar. Outputs land in build/test-out/<variant>/<case>/.
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { runSlice } from '../host/bambu-engine-core.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const [distArg = 'dist', variant = 'st', ...only] = process.argv.slice(2);
const dist = resolve(distArg);
const expected = JSON.parse(readFileSync(join(here, 'expected.json'), 'utf8'));
const glue = pathToFileURL(join(dist, `engine-${variant}.mjs`)).href;
const { default: createEngine } = await import(glue);
const wasmModule = await WebAssembly.compile(readFileSync(join(dist, `engine-${variant}.wasm`)));
const profiles = readFileSync(join(dist, 'profiles.tar'));
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');
const outRoot = process.env.TEST_OUT ?? join(here, '..', 'build', 'test-out', variant);
const moduleArgs = variant === 'mt' ? { mainScriptUrlOrBlob: fileURLToPath(glue) } : {};

const get = (obj, path) => path.split('.').reduce((o, k) => (o == null ? undefined : o[k]), obj);
let failed = 0;
const timings = {};
for (const c of expected.cases) {
  if (only.length && !only.includes(c.name)) continue;
  const t0 = performance.now();
  const progress = [];
  const { plates, result, log } = await runSlice(createEngine, {
    wasmModule, profiles, moduleArgs,
    input: readFileSync(join(here, c.input)),
    plate: c.plate ?? 0,
    presets: c.presets ?? 'auto',
    onProgress: (percent, stage) => progress.push([percent, stage]),
  });
  const ms = performance.now() - t0;
  timings[c.name] = Math.round(ms);
  const dir = join(outRoot, c.name);
  mkdirSync(dir, { recursive: true });
  const problems = [];
  if (result.return_code !== 0) problems.push(`engine returned ${result.return_code}: ${result.error_string}`);
  const files = Object.fromEntries(plates.map((p) => [p.info.gcode, p.gcode]));
  for (const [file, hash] of Object.entries(c.outputs ?? {})) {
    const bytes = files[file];
    if (!bytes) { problems.push(`${file}: missing`); continue; }
    const got = sha256(bytes);
    if (got !== hash) problems.push(`${file}: sha256 ${got}, expected ${hash}`);
  }
  for (const [file, bytes] of Object.entries(files)) writeFileSync(join(dir, file), bytes);
  if (c.plates !== undefined && plates.length !== c.plates) problems.push(`${plates.length} plates, expected ${c.plates}`);
  for (const [path, want] of Object.entries(c.result ?? {})) {
    const got = get(result, path);
    if (JSON.stringify(got) !== JSON.stringify(want)) problems.push(`result.${path} = ${JSON.stringify(got)}, expected ${JSON.stringify(want)}`);
  }
  for (const [path, [want, tolerance]] of Object.entries(c.approx ?? {})) {
    const got = get(result, path);
    if (typeof got !== 'number' || Math.abs(got - want) > tolerance) problems.push(`result.${path} = ${got}, expected ${want} ± ${tolerance}`);
  }
  if (!progress.length || progress.at(-1)[0] !== 100) problems.push(`progress ended at ${JSON.stringify(progress.at(-1))}`);
  writeFileSync(join(dir, 'result.json'), JSON.stringify(result, null, 1) + '\n');
  writeFileSync(join(dir, 'log.txt'), log.join('\n') + '\n');
  writeFileSync(join(dir, 'progress.json'), JSON.stringify(progress) + '\n');
  console.log(`${problems.length ? 'FAIL' : 'ok  '} ${c.name} (${variant}, ${(ms / 1000).toFixed(1)} s)`);
  for (const p of problems) console.log(`     ${p}`);
  if (problems.length) failed++;
}
writeFileSync(join(outRoot, 'timings.json'), JSON.stringify(timings, null, 1) + '\n');
process.exit(failed ? 1 : 0);

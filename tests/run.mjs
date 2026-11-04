#!/usr/bin/env node
// Slices every case in tests/expected.json with the built engine under Node and compares the SHA-256 of each output
// with the one Bambu Studio's own command line produced for the same input (see "reference" in expected.json).
//   node tests/run.mjs [dist dir, default dist] [variant st|mt, default st] [case name ...]
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const [distArg = 'dist', variant = 'st', ...only] = process.argv.slice(2);
const dist = resolve(distArg);
const expected = JSON.parse(readFileSync(join(here, 'expected.json'), 'utf8'));
const { default: createBambuEngine } = await import(pathToFileURL(join(dist, `bambu-engine-${variant}.mjs`)).href);
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');
const outRoot = process.env.TEST_OUT ?? join(here, '..', 'build', 'test-out', variant);

let failed = 0;
for (const c of expected.cases) {
  if (only.length && !only.includes(c.name)) continue;
  const t0 = performance.now();
  const log = [];
  const Module = await createBambuEngine({ print: (s) => log.push(s), printErr: (s) => log.push(s) });
  for (const d of ['/in', '/out', '/tmp', '/data']) Module.FS.mkdirTree(d);
  Module.FS.writeFile('/in/input.3mf', readFileSync(join(here, c.input)));
  Module.ccall('engine_init', null, ['string', 'number'], ['/resources', 1]);
  const rc = Module.ccall('engine_slice', 'number', ['string', 'string', 'number'], ['/in/input.3mf', '/out', c.plate ?? 0]);
  const ms = performance.now() - t0;
  const dir = join(outRoot, c.name);
  mkdirSync(dir, { recursive: true });
  const problems = [];
  if (rc !== 0) problems.push(`engine returned ${rc}: ${new TextDecoder().decode(Module.FS.readFile('/out/result.json'))}`);
  for (const [file, hash] of Object.entries(c.outputs)) {
    let bytes;
    try { bytes = Module.FS.readFile('/out/' + file); } catch { problems.push(`${file}: missing`); continue; }
    writeFileSync(join(dir, file), bytes);
    const got = sha256(bytes);
    if (got !== hash) problems.push(`${file}: sha256 ${got}, expected ${hash}`);
  }
  writeFileSync(join(dir, 'log.txt'), log.join('\n') + '\n');
  console.log(`${problems.length ? 'FAIL' : 'ok  '} ${c.name} (${variant}, ${(ms / 1000).toFixed(1)} s)`);
  for (const p of problems) console.log(`     ${p}`);
  if (problems.length) failed++;
}
process.exit(failed ? 1 : 0);

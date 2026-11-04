#!/usr/bin/env node
// Slices a Bambu 3MF with the wasm engine under Node, the way `BambuStudio --slice 0 --outputdir <dir> <file>`
// does: node tools/slice-node.mjs <engine.mjs> <project.3mf> <outdir> [plate] [log level 0-5]
// Writes <outdir>/plate_<n>.gcode and <outdir>/result.json (with wall time and peak wasm heap).
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { basename, resolve } from 'node:path';

const [enginePath, input, outdir, plate = '0', logLevel = '1'] = process.argv.slice(2);
if (!enginePath || !input || !outdir) {
  console.error('usage: slice-node.mjs <engine.mjs> <project.3mf> <outdir> [plate] [loglevel]');
  process.exit(2);
}
const t0 = performance.now();
const { default: createBambuEngine } = await import(pathToFileURL(resolve(enginePath)).href);
const Module = await createBambuEngine({ print: (s) => process.stdout.write(s + '\n'), printErr: (s) => process.stderr.write(s + '\n') });
const tBoot = performance.now();
Module.FS.mkdirTree('/in'); Module.FS.mkdirTree('/out'); Module.FS.mkdirTree('/tmp'); Module.FS.mkdirTree('/data');
const name = basename(input);
Module.FS.writeFile('/in/' + name, readFileSync(input));
Module.ccall('engine_init', null, ['string', 'number'], ['/resources', Number(logLevel)]);
const tSlice = performance.now();
const rc = Module.ccall('engine_slice', 'number', ['string', 'string', 'number'], ['/in/' + name, '/out', Number(plate)]);
const tDone = performance.now();
mkdirSync(outdir, { recursive: true });
const result = JSON.parse(new TextDecoder().decode(Module.FS.readFile('/out/result.json')));
for (const f of Module.FS.readdir('/out')) {
  if (f.endsWith('.gcode')) writeFileSync(`${outdir}/${f}`, Module.FS.readFile('/out/' + f));
}
result.wall = { boot_ms: tBoot - t0, slice_ms: tDone - tSlice, total_ms: tDone - t0 };
result.heap_bytes = Module.wasmMemory.buffer.byteLength;
result.node_max_rss_bytes = process.resourceUsage().maxRSS * 1024;
writeFileSync(`${outdir}/result.json`, JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result));
process.exit(rc);

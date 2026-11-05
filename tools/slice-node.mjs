#!/usr/bin/env node
// Slices a Bambu 3MF with the wasm engine under Node, as `BambuStudio --slice <plate> --outputdir <dir> <file>` does:
//   node tools/slice-node.mjs <dist dir> <project.3mf> <outdir> [plate=0] [variant=st] [log level=1] [presets=auto]
// <dist dir> holds engine-<variant>.mjs/.wasm, profiles.tar and bambu-engine-core.mjs (a release, or scripts/build.sh's
// dist/). Writes <outdir>/plate_<n>.gcode for each plate and <outdir>/result.json (with wall times), and prints progress.
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [distArg, input, outdir, plate = '0', variant = 'st', logLevel = '1', presets = 'auto'] = process.argv.slice(2);
if (!distArg || !input || !outdir) {
  console.error('usage: slice-node.mjs <dist dir> <project.3mf> <outdir> [plate] [st|mt] [log level 0-5] [auto|resolve|project]');
  process.exit(2);
}
const dist = resolve(distArg);
const core = existsSync(join(dist, 'bambu-engine-core.mjs')) ? join(dist, 'bambu-engine-core.mjs') : new URL('../host/bambu-engine-core.mjs', import.meta.url).pathname;
const { runSlice } = await import(pathToFileURL(core).href);
const glue = join(dist, `engine-${variant}.mjs`);
const t0 = performance.now();
const { default: createEngine } = await import(pathToFileURL(glue).href);
const wasmModule = await WebAssembly.compile(readFileSync(join(dist, `engine-${variant}.wasm`)));
const tCompile = performance.now();
let last = '';
const { plates, result, log } = await runSlice(createEngine, {
  wasmModule,
  profiles: existsSync(join(dist, 'profiles.tar')) ? readFileSync(join(dist, 'profiles.tar')) : null,
  input: readFileSync(input),
  plate: Number(plate),
  presets,
  logLevel: Number(logLevel),
  moduleArgs: variant === 'mt' ? { mainScriptUrlOrBlob: glue } : {},
  onProgress(percent, stage) {
    const line = `${String(percent).padStart(3)}% ${stage.replace(/: layer \d+$/, '')}`;
    if (line !== last) process.stderr.write((last = line) + '\n');
  },
});
const tDone = performance.now();
mkdirSync(outdir, { recursive: true });
for (const p of plates) writeFileSync(join(outdir, p.info.gcode), p.gcode);
result.wall = { compile_ms: tCompile - t0, slice_ms: tDone - tCompile };
result.node_max_rss_bytes = process.resourceUsage().maxRSS * 1024;
writeFileSync(join(outdir, 'result.json'), JSON.stringify(result, null, 1) + '\n');
writeFileSync(join(outdir, 'log.txt'), log.join('\n') + '\n');
console.log(JSON.stringify({ return_code: result.return_code, error_string: result.error_string, plates: plates.length, notes: result.notes, wall: result.wall }));
process.exit(result.return_code ? 1 : 0);

// Proves the browser engine sounds the same as the desktop one: the same
// script of commands runs through engine.wasm and through a native build of
// the same engine code, and the outputs are compared sample by sample.
//   node web/tests/parity.mjs <native_render binary>

import { readFileSync, writeFileSync, mkdirSync, readdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { instantiate, Cmd, Character, Voice } from '../site/engine.js';
import { decodeWavMono } from '../site/wav.js';

const here = dirname(fileURLToPath(import.meta.url));
const site = join(here, '..', 'site');
const nativeBin = process.argv[2];
const work = join(tmpdir(), 'ms-parity');
mkdirSync(join(work, 'hr16'), { recursive: true });

const sr = 48000;
const step = sr * 60 / 132 / 4;
const total = Math.round(step * 16 * 3);
// [time, type, track, index, value, sampleId]
const events = [
  [0, Cmd.Play, 0, 0, 0, 0],
  [0, Cmd.SetCharacter, 2, Character.Hr16, 0, 0],
  [0, Cmd.SetCharacter, 1, Character.Live, 0, 0],
  [0, Cmd.SetFxParam, -1, 22, 0.4, 0],          // master reverb mix
  [0, Cmd.SetFxParam, 0, 3, 0.8, 0],            // kick distortion drive
  [Math.round(step * 5), Cmd.Trigger, 3, 0, 0.8, 0],
  [Math.round(step * 9), Cmd.SetStep, 4, 3, 1, 0],
  [Math.round(step * 16), Cmd.SelectPattern, 0, 1, 1, 0],
  [Math.round(step * 20), Cmd.SetVoiceParam, 0, 0, 0.3, 0],
  [Math.round(step * 30), Cmd.SetFxParam, 1, 11, 0.5, 0], // click delay mix
];

// HR-16 sounds, decoded once in JavaScript and handed to both engines.
const hr16 = [];
for (const f of readdirSync(join(site, 'hr16')).sort()) {
  const n = parseInt(f.match(/_(\d\d)\.wav$/)[1], 10);
  const buf = readFileSync(join(site, 'hr16', f));
  const w = decodeWavMono(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
  hr16.push([n, w]);
  const rec = new Float32Array(w.samples.length + 1);
  rec[0] = w.sampleRate; rec.set(w.samples, 1);
  writeFileSync(join(work, 'hr16', String(n).padStart(2, '0') + '.f32'), Buffer.from(rec.buffer));
}

// ---- wasm
const engine = instantiate(new WebAssembly.Module(readFileSync(join(site, 'engine.wasm'))));
for (const [n, w] of hr16) engine.setHr16(n, w.samples, w.sampleRate);
engine.x.ms_prepare(sr);
const wasmOut = new Float32Array(total * 2);
let e = 0;
for (let pos = 0; pos < total;) {
  while (e < events.length && events[e][0] <= pos) { const c = events[e++]; engine.x.ms_command(c[1], c[2], c[3], c[4], c[5]); }
  const n = Math.min(128, total - pos);
  engine.x.ms_process(n);
  const l = engine.left(n), r = engine.right(n);
  for (let i = 0; i < n; i++) { wasmOut[(pos + i) * 2] = l[i]; wasmOut[(pos + i) * 2 + 1] = r[i]; }
  pos += n;
}

// ---- native
const script = new Float32Array(2 + events.length * 6);
script[0] = sr; script[1] = total;
events.forEach((c, i) => script.set(c, 2 + i * 6));
writeFileSync(join(work, 'script.f32'), Buffer.from(script.buffer));
execFileSync(nativeBin, [join(work, 'script.f32'), join(work, 'hr16'), join(work, 'native.f32')]);
const nb = readFileSync(join(work, 'native.f32'));
const nativeOut = new Float32Array(nb.buffer.slice(nb.byteOffset, nb.byteOffset + nb.byteLength));

// ---- compare
let maxDiff = 0, peak = 0;
for (let i = 0; i < total * 2; i++) {
  maxDiff = Math.max(maxDiff, Math.abs(wasmOut[i] - nativeOut[i]));
  peak = Math.max(peak, Math.abs(nativeOut[i]));
}
const db = maxDiff > 0 ? (20 * Math.log10(maxDiff)).toFixed(1) + ' dBFS' : 'identical';
console.log(`samples ${total}, native peak ${peak.toFixed(3)}, largest difference ${maxDiff.toExponential(2)} (${db})`);
if (nativeOut.length !== wasmOut.length || peak < 0.05 || maxDiff > 1e-3) {
  console.error('FAIL: the browser engine does not match the native engine');
  process.exit(1);
}
console.log('OK: browser engine matches the native engine');

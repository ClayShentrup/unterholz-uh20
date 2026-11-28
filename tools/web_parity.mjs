// Parity: the browser engine (WebAssembly) against the native renderer, sample for sample.
//   node tools/web_parity.mjs --cli build/uh20_cli --wasm docs/play/uh20.wasm --in guitar.wav [--bench] [--limit -100]
// Renders the input through both at each setting in web/parity.json (unset knobs at the plug-in's defaults, from
// docs/play/amp.js), and fails if any output sample differs by more than --limit dB relative to that output's peak.
// The input can be any mono or stereo WAV (averaged to mono, as the plug-in does); CI uses a built-in recording, decoded.
// The native renderer reads and writes raw mono float32; the amp is mono, so the WebAssembly engine's two sides must
// also be identical to each other. --bench also reports the realtime
// CPU fraction of the WebAssembly engine (seconds of processing per second of audio, one core, 128-sample blocks).
// SPDX-License-Identifier: AGPL-3.0-or-later
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const cli = arg('--cli', join(root, 'build/uh20_cli'));
const wasmPath = arg('--wasm', join(root, 'docs/play/uh20.wasm'));
const input = arg('--in');
const limitDb = Number(arg('--limit', '-100'));
const bench = process.argv.includes('--bench');
if (!input) { console.error('usage: web_parity.mjs --in guitar.wav [--cli path] [--wasm path] [--bench]'); process.exit(2); }

const { amp } = await import(pathToFileURL(join(root, 'docs/play/amp.js')));
const { loadAmp } = await import(pathToFileURL(join(root, 'docs/play/player/wasm-amp.js')));
const settings = JSON.parse(readFileSync(join(root, 'web/parity.json'), 'utf8')).settings;

// WAV reading as the native renderer does it: channels averaged in double, then rounded to float
function readWav(path) {
  const b = readFileSync(path); let o = 12, ch = 0, bits = 0, fmt = 0, sr = 0, data = null;
  while (o + 8 <= b.length) {
    const id = b.toString('latin1', o, o + 4), sz = b.readUInt32LE(o + 4);
    if (id === 'fmt ') {
      fmt = b.readUInt16LE(o + 8); ch = b.readUInt16LE(o + 10); sr = b.readUInt32LE(o + 12); bits = b.readUInt16LE(o + 22);
      if (fmt === 0xFFFE && sz >= 26) fmt = b.readUInt16LE(o + 32);
    } else if (id === 'data') { data = b.subarray(o + 8, o + 8 + sz); break; }
    o += 8 + sz + (sz & 1);
  }
  const bps = bits / 8, n = Math.floor(data.length / (bps * ch)), out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let c = 0; c < ch; c++) {
      const p = (i * ch + c) * bps;
      s += fmt === 3 && bits === 32 ? data.readFloatLE(p) : bits === 16 ? data.readInt16LE(p) / 32768
        : bits === 24 ? data.readIntLE(p, 3) / 8388608 : data.readInt32LE(p) / 2147483648;
    }
    out[i] = s / ch;
  }
  return { x: out, sr };
}

const { x, sr } = readWav(input);
const N = x.length;   // the native renderer writes as many samples as it reads
const engine = await loadAmp(readFileSync(wasmPath));
const tmp = mkdtempSync(join(tmpdir(), 'web-parity-'));
const db = v => 20 * Math.log10(Math.max(v, 1e-30));
let worst = -Infinity, fail = false;

for (const s of settings) {
  const vals = Object.fromEntries(amp.params.map(p => [p.key, p.def]));
  Object.assign(vals, s.knobs);
  // native
  const inF = join(tmp, 'in.f32'), out = join(tmp, 'native.f32');
  writeFileSync(inF, Buffer.from(x.buffer, x.byteOffset, x.byteLength));
  execFileSync(cli, [inF, out, String(sr), ...amp.params.map(p => `${p.cli}=${vals[p.key]}`)]);
  const nb = readFileSync(out), nat = new Float32Array(nb.buffer, nb.byteOffset, nb.byteLength / 4), nN = nat.length;
  // WebAssembly, in the worklet's 128-sample blocks
  const a = engine.create(sr);
  amp.params.forEach((p, id) => a.setParam(id, vals[p.key]));
  a.prepare();
  const L = new Float32Array(N), R = new Float32Array(N), B = 128, inb = new Float32Array(B), lb = new Float32Array(B), rb = new Float32Array(B);
  const t0 = performance.now();
  for (let i = 0; i < N; i += B) {
    const n = Math.min(B, N - i); inb.fill(0);
    inb.set(x.subarray(i, i + n));
    a.process(inb, lb, rb, n); L.set(lb.subarray(0, n), i); R.set(rb.subarray(0, n), i);
  }
  const secs = (performance.now() - t0) / 1000;
  a.destroy();
  if (nN !== N) { console.log(`${s.name}: length ${N} (wasm) vs ${nN} (native)`); fail = true; continue; }
  let peak = 0, diff = 0, same = 0, sides = 0;
  for (let i = 0; i < N; i++) {
    const v = nat[i];
    peak = Math.max(peak, Math.abs(v));
    const d = Math.max(Math.abs(v - L[i]), Math.abs(v - R[i])); diff = Math.max(diff, d); if (d === 0) same++;
    sides = Math.max(sides, Math.abs(L[i] - R[i]));
  }
  if (sides !== 0) { console.log(`FAIL ${s.name}: the two sides differ (${sides})`); fail = true; }
  const rel = db(diff / peak);
  worst = Math.max(worst, rel);
  const ok = rel <= limitDb;
  if (!ok) fail = true;
  console.log(`${ok ? 'PASS' : 'FAIL'} ${s.name.padEnd(34)} max diff ${diff === 0 ? 'none (bit-identical)' : `${db(diff).toFixed(1)} dBFS = ${rel.toFixed(1)} dB re peak`}`
    + `, ${(100 * same / N).toFixed(1)}% of samples identical` + (bench ? `, realtime CPU ${(secs / (N / sr)).toFixed(3)}` : ''));
}
rmSync(tmp, { recursive: true, force: true });
console.log(`worst: ${worst === -Infinity ? 'bit-identical' : worst.toFixed(1) + ' dB re peak'} (limit ${limitDb} dB)`);
process.exit(fail ? 1 : 0);

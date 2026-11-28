// The player's audio thread (docs/play/player/worklet.js) in Node, with the real engine (WebAssembly) and a simulated
// clock, so a busy device can be played deterministically: each block's processing is charged a chosen share of the
// block's time, each parameter change a chosen cost. Every knob change must reach the engine within 100 ms of audio,
// however busy the thread (a device running above half load once never applied a knob change at all), and must move
// the output. Run with the browser's fine clock and with a 1 ms clock (Firefox's audio thread may have no
// performance.now(), and Date.now() is coarser than a 2.7 ms block).
//   node tools/worklet_test.mjs [--wasm docs/play/uh20.wasm] [--worklet docs/play/player/worklet.js]
// SPDX-License-Identifier: AGPL-3.0-or-later
import { readFileSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const wasm = readFileSync(resolve(arg('--wasm', join(root, 'docs/play/uh20.wasm'))));
const { amp } = await import(pathToFileURL(join(root, 'docs/play/amp.js')));

const SR = 48000, N = 128, BLOCK_MS = 1000 * N / SR;
let clock = 0, coarse = false, Proc = null;
Object.defineProperty(globalThis, 'performance', { value: { now: () => (coarse ? Math.floor(clock) : clock) }, configurable: true, writable: true });
globalThis.sampleRate = SR;
globalThis.AudioWorkletProcessor = class { constructor() { this.port = { postMessage: m => this.port.sent.push(m), sent: [], onmessage: null }; } };
globalThis.registerProcessor = (name, cls) => { Proc = cls; };
await import(pathToFileURL(resolve(arg('--worklet', join(root, 'docs/play/player/worklet.js')))));

const id = Object.fromEntries(amp.params.map((p, i) => [p.key, i]));
const values = amp.params.map(p => p.def); values[id.involts] = 1.6;
const rec = new Float32Array(SR * 4); for (let i = 0; i < rec.length; i++) rec[i] = 0.1 * Math.sin(2 * Math.PI * 196 * i / SR);

let failures = 0;
async function run(name, load, setCost, script) {
  const p = new Proc({ processorOptions: { wasm, values, source: 'recording', recording: rec, playing: true, loop: true } });
  while (!p.amp) await new Promise(r => setTimeout(r, 5));
  const applied = [];   // [block, id, value] each time the engine is handed a change
  const set = p.amp.setParam, proc = p.amp.process;
  p.amp.setParam = (i, v) => { clock += setCost; applied.push([blk, i, v]); set(i, v); };
  p.amp.process = (...a) => { clock += load * BLOCK_MS; proc(...a); };
  const out = [[new Float32Array(N), new Float32Array(N)]];
  let blk = 0; const rms = [];
  const sent = [];   // [block, id, value]
  for (; blk < Math.round(1500 / BLOCK_MS); blk++) {
    for (const [atMs, key, v] of script) if (Math.round(atMs / BLOCK_MS) === blk) { p.message({ type: 'param', id: id[key], value: v }); sent.push([blk, id[key], v]); }
    p.process([[]], out);
    let s = 0; for (const x of out[0][0]) s += x * x; rms.push(s / N);
  }
  // every sent value is either applied or superseded by a later value of the same knob that is applied, within 100 ms
  let worst = 0;
  for (const [b, i, v] of sent) {
    const later = sent.filter(([b2, i2]) => i2 === i && b2 >= b).map(x => x[0]);
    const hit = applied.find(([ab, ai, av]) => ai === i && ab >= b && (av === v || later.some(lb => lb > b && lb <= ab)));
    worst = Math.max(worst, hit ? (hit[0] - b) * BLOCK_MS : Infinity);
  }
  const lvl = (a, b) => 10 * Math.log10(rms.slice(Math.round(a / BLOCK_MS), Math.round(b / BLOCK_MS)).reduce((x, y) => x + y, 0) / Math.round((b - a) / BLOCK_MS) + 1e-30);
  const moved = lvl(1200, 1500) - lvl(200, 400);
  const ok = worst <= 100 && Math.abs(moved) > 1;
  if (!ok) failures++;
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}: slowest change reached the engine after ${isFinite(worst) ? worst.toFixed(1) + ' ms' : 'never'} (limit 100), output moved ${moved.toFixed(1)} dB (must move)`);
}

const gainUp = [[500, 'volume', 10]];
const drag = Array.from({ length: 25 }, (_, k) => [500 + 12 * k, 'volume', +(5 + 0.2 * (k + 1)).toFixed(1)]);   // a 300 ms knob drag, 5 -> 10
for (const c of [false, true]) {
  coarse = c;
  for (const load of [0.3, 0.7, 0.95])
    for (const [cost, what] of [[0.003, 'light knob changes'], [12, 'knob changes costing 12 ms each']]) {
      const clk = c ? '1 ms clock' : 'fine clock';
      await run(`${clk}, load ${Math.round(100 * load)}%, ${what}, one step`, load, cost, gainUp);
      await run(`${clk}, load ${Math.round(100 * load)}%, ${what}, a drag`, load, cost, drag);
    }
}
// ---- buffered mode: the engine in a worker (render.js) ahead of the playhead, the worklet playing its chunks
coarse = false;
const { loadAmp } = await import(pathToFileURL(join(root, 'docs/play/player/wasm-amp.js')));
const { Renderer } = await import(pathToFileURL(join(root, 'docs/play/player/render.js')));
const BLOCKS = s => Math.round(s * SR / N);
const vals = (over = {}) => { const v = values.slice(); for (const [k, x] of Object.entries(over)) v[id[k]] = x; return v; };
async function realtime(v0, seconds, change) {   // change: [block, key, value]
  const p = new Proc({ processorOptions: { wasm, values: v0, source: 'recording', recording: rec, playing: true, loop: true } });
  while (!p.amp) await new Promise(r => setTimeout(r, 5));
  const out = [[new Float32Array(N), new Float32Array(N)]], L = [], R = [];
  for (let b = 0; b < BLOCKS(seconds); b++) { if (change && b === change[0]) p.message({ type: 'param', id: id[change[1]], value: change[2] }); p.process([[]], out); L.push(...out[0][0]); R.push(...out[0][1]); }
  return { L, R };
}
// speed: the renderer's seconds of work per second of audio (it renders only as fast as that allows, in real time)
async function buffered(v0, seconds, { speed = 0.2, change = null } = {}) {
  const p = new Proc({ processorOptions: { wasm: null, values: v0, source: 'recording', recording: rec, playing: true, loop: true } });
  const eng = await loadAmp(wasm);
  const r = new Renderer(eng, SR, msg => p.chunk(msg));
  const proc = r.a.process; r.a.process = (x, l, rr, k) => { clock += speed * 1000 * k / SR; proc(x, l, rr, k); };
  p.message({ type: 'recording', data: rec });
  p.message({ type: 'buffered', port: { postMessage: m => { if (m.type === 'played') r.setPlayed(m.t); } } });
  r.start(v0, rec, true, 0);
  const out = [[new Float32Array(N), new Float32Array(N)]], L = [], R = [];
  let budget = 0, at = null, waits = 0, aheadMax = 0, firstSound = -1;
  for (let b = 0; b < BLOCKS(seconds) + BLOCKS(3); b++) {
    budget += N / speed;   // samples it can render during one block of playback
    while (budget >= r.CH && r.wants()) { r.step(); budget -= r.CH; }
    if (!r.wants()) budget = Math.min(budget, r.CH);
    if (change && L.length >= change[0] * N && !at) { const P = p.vt; const nv = v0.slice(); nv[id[change[1]]] = change[2]; r.setValues(nv); at = { P, S: r.T }; }
    p.process([[]], out);
    if (p.waiting) waits++; aheadMax = Math.max(aheadMax, p.queuedAhead() / SR);
    if (firstSound < 0 && !p.waiting) firstSound = b;
    if (firstSound >= 0 && L.length < BLOCKS(seconds) * N) { L.push(...out[0][0]); R.push(...out[0][1]); }
  }
  return { L, R, at, underruns: p.underruns, startup: firstSound * N / SR, aheadMax };
}
const same = (a, b) => { let d = 0; for (let i = 0; i < a.L.length; i++) d = Math.max(d, Math.abs(a.L[i] - b.L[i]), Math.abs(a.R[i] - b.R[i])); return a.L.length === b.L.length ? d : Infinity; };
const check = (ok, msg) => { if (!ok) failures++; console.log(`${ok ? 'PASS' : 'FAIL'} ${msg}`); };
for (const [name, over] of [['defaults', {}], ['drive, clip, sag, master 8', { drive: 5, clip: 5, sag: 6, master: 8, volume: 7 }]]) {
  const v0 = vals(over), rt = await realtime(v0, 2.5), bf = await buffered(v0, 2.5);
  check(same(rt, bf) === 0, `buffered equals real time, ${name}: max difference ${same(rt, bf)} (must be 0); ${bf.underruns} underruns`);
  // a knob change mid-play: the buffered stream goes back to the first chunk start past the playhead and the lead
  const bc = await buffered(v0, 2.5, { change: [BLOCKS(1.0), 'volume', 9.5] });
  const rc = await realtime(v0, 2.5, [bc.at.S / N, 'volume', 9.5]);
  if (same(rc, bc) !== 0) { let i = 0; while (i < rc.L.length && rc.L[i] === bc.L[i]) i++; console.log(`  first difference at ${(i / SR).toFixed(4)} s (sample ${i})`); }
  check(same(rc, bc) === 0 && bc.at.S % N === 0, `buffered equals real time with the volume moved at ${(bc.at.S / SR).toFixed(3)} s (from the playhead at ${(bc.at.P / SR).toFixed(3)} s): max difference ${same(rc, bc)}`);
}
// slow devices: rendering takes 70% and 95% of real time (no gaps once going), and 130% (it must wait, never skip)
for (const speed of [0.7, 0.95, 1.3]) {
  const v0 = vals({ sag: 3 });
  const bc = await buffered(v0, 6, { speed, change: [BLOCKS(3.0), 'volume', 9] });
  const lag = (bc.at.S - bc.at.P) / SR;
  const ok = speed < 1 ? bc.underruns === 0 && lag <= 0.5 : bc.underruns > 0;
  check(ok, `renderer at ${Math.round(100 * speed)}% of real time: starts after ${bc.startup.toFixed(2)} s, ${bc.underruns} underruns (${speed < 1 ? 'must be 0' : 'must wait, not skip'}), buffer up to ${bc.aheadMax.toFixed(2)} s ahead, a knob change sounds ${lag.toFixed(2)} s after the playhead${speed < 1 ? ' (limit 0.5)' : ''}`);
}
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);

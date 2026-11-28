// The buffered player's renderer: the amp engine (wasm-amp.js) run ahead of the playhead on a decoded recording, in
// chunks, for devices that cannot run it in real time. Used by render-worker.js (a Web Worker) and by the tests.
//
// Exactness: before each chunk the engine's whole state is kept (amp_snapshot). When a knob moves, rendering goes back
// to the first kept chunk start at or after the playhead plus a lead (the time a chunk takes to render, so the new
// sound arrives before the playhead gets there), restores the state there and renders on with the new setting. That is
// the very sample stream the engine produces live when the knob moves at that sample, so playback goes on seamlessly,
// with no crossfade and nothing approximated.
// Messages it posts (to the playback worklet): { type: 'chunk', epoch, start, src, n, L, R, end } (start: the virtual
// time of its first sample; src: that sample's index in the recording) and { type: 'flush', epoch, start } (a jump: the
// worklet drops what it holds and waits for the new stream). A chunk of a newer epoch replaces everything from its start.
// SPDX-License-Identifier: AGPL-3.0-or-later

export const CHUNK_SECONDS = 0.125, AHEAD_SECONDS = 3;   // a state kept per chunk: about 1 MB each

export class Renderer {
  constructor(engine, rate, post) {
    this.eng = engine; this.rate = rate; this.post = post;
    this.a = engine.create(rate);
    this.CH = Math.max(128, Math.round(CHUNK_SECONDS * rate / 128) * 128);     // a whole number of the worklet's blocks
    this.slots = Math.ceil(AHEAD_SECONDS * rate / this.CH) + 4;
    this.B = Math.min(engine.maxBlock, this.CH);
    this.rec = null; this.loop = true; this.epoch = 0; this.T = 0; this.src = 0; this.played = 0; this.ended = true;
    this.snaps = new Map();   // chunk start (virtual time) -> { slot, src }
    this.msPerChunk = 0;      // measured
  }
  // start over: a fresh engine at these settings on this recording, from src (as the live player does on its first Play)
  start(values, rec, loop, src = 0) {
    this.values = values.slice(); values.forEach((v, i) => this.a.setParam(i, v)); this.a.prepare();
    this.rec = rec; this.loop = loop; this.src = Math.min(src, rec.length); this.T = 0; this.played = 0; this.ended = false;
    this.snaps.clear(); this.epoch++;
    this.post({ type: 'flush', epoch: this.epoch, start: 0 });
  }
  // the first kept chunk start at or after t, else the render head (with nothing kept there, the engine is already at it)
  restorePoint(t) {
    const S = Math.ceil(t / this.CH) * this.CH;
    if (S < this.T && this.snaps.has(S)) {
      const s = this.snaps.get(S); this.a.restore(s.slot); this.T = S; this.src = s.src; this.ended = false;
      this.values.forEach((v, i) => this.a.setParam(i, v));   // a state kept just after a knob moved holds it as pending
    }
    for (const k of [...this.snaps.keys()]) if (k > this.T) this.snaps.delete(k);
    return this.T;
  }
  lead() { return Math.round(this.rate * Math.max(0.12, 1.5 * this.msPerChunk / 1000 + 0.05)); }
  // knobs moved: from the first chunk the playhead has not reached (with the lead), with these settings
  setValues(values) {
    this.restorePoint(this.played + this.lead());
    this.values = values.slice(); values.forEach((v, i) => this.a.setParam(i, v));
    this.epoch++;
  }
  // a jump in the recording (seek, restart, another recording): the engine's state goes on, the source jumps
  seek(src, rec) {
    if (rec) this.rec = rec;
    const S = this.restorePoint(this.played);
    this.src = Math.min(src, this.rec.length); this.ended = false; this.epoch++;
    this.post({ type: 'flush', epoch: this.epoch, start: S });
  }
  setLoop(loop) { if (loop !== this.loop) { this.loop = loop; if (loop && this.ended) this.ended = false; } }
  setPlayed(t) { this.played = t; }
  wants() { return !!this.rec && !this.ended && this.T - this.played < AHEAD_SECONDS * this.rate; }
  // render one chunk; false when there is nothing to do
  step() {
    if (!this.wants()) return false;
    const t0 = (globalThis.performance || Date).now(), n = this.CH, rec = this.rec;
    const slot = (this.T / n) % this.slots;
    this.a.snapshot(slot); this.snaps.set(this.T, { slot, src: this.src });
    for (const k of [...this.snaps.keys()]) if (k < this.T - (this.slots - 2) * n) this.snaps.delete(k);
    const x = new Float32Array(n), srcStart = this.src; let m = n;
    for (let i = 0; i < n; i++) {
      if (this.src >= rec.length) { if (this.loop && rec.length) this.src = 0; else { m = i; break; } }
      x[i] = rec[this.src++];
    }
    const L = new Float32Array(m), R = new Float32Array(m);
    for (let o = 0; o < m; o += this.B) { const k = Math.min(this.B, m - o); this.a.process(x.subarray(o, o + k), L.subarray(o, o + k), R.subarray(o, o + k), k); }
    const end = m < n; if (end) this.ended = true;
    this.post({ type: 'chunk', epoch: this.epoch, start: this.T, src: srcStart, n: m, L, R, end }, [L.buffer, R.buffer]);
    this.T += m;
    const ms = (globalThis.performance || Date).now() - t0;
    this.msPerChunk = this.msPerChunk ? 0.8 * this.msPerChunk + 0.2 * ms : ms;
    return true;
  }
  // seconds of rendering per second of audio
  speed() { return this.msPerChunk / (1000 * this.CH / this.rate); }
}

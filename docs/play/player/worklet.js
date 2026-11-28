// The audio thread of the browser amp player: the amp engine (WebAssembly, wasm-amp.js) in an AudioWorkletProcessor.
// Source: a built-in recording (held here, so the transport is sample-accurate) or the live input (one chosen channel).
// Then the input trim, the engine, a bypass crossfade to the dry signal, the listening volume, meters and an analysis
// tap. Amp-agnostic: the amp's parameters arrive as numbered values.
// Buffered mode (recordings, on devices too slow for real time): the engine runs ahead in a worker (render-worker.js)
// and its chunks arrive on a MessagePort; this plays them back to back by their virtual time, sends the playhead back,
// and when it runs dry it waits (silence, the playhead held) until START_SECONDS are queued again, as a video does.
// SPDX-License-Identifier: AGPL-3.0-or-later
import { loadAmp } from './wasm-amp.js';

const now = globalThis.performance ? () => performance.now() : () => Date.now();
const QUANTUM = 128;
const MAX_DEBT = 50, MAX_WAIT = 60;   // ms
const START_SECONDS = 0.75, XFADE_SECONDS = 0.02;

class AmpPlayer extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions;
    this.values = o.values.slice();            // every parameter, by id
    this.pending = new Map();                  // changes not yet handed to the engine
    this.amp = null;
    // source and transport
    this.source = o.source || 'recording';
    this.rec = o.recording || null; this.pos = 0; this.playing = !!o.playing; this.loop = o.loop !== false;
    this.channel = 0; this.channels = 0;
    // gains, smoothed per sample (one pole, ~8 ms): no steps, so no zipper noise
    this.k = 1 - Math.exp(-1 / (0.008 * sampleRate));
    this.trim = 1; this.trimT = 1; this.vol = 1; this.volT = 1; this.bypass = !!o.bypass; this.wet = this.bypass ? 0 : 1;
    // meters, analysis tap, load
    this.inPk = 0; this.outPk = 0; this.meterN = 0; this.meterEvery = Math.round(0.033 * sampleRate / QUANTUM);
    this.stats = { n: 0, in: 0, l: 0, r: 0, pk: 0 };
    this.busy = 0; this.span = 0; this.debt = 0; this.load = 0; this.frames = 0; this.pendingSince = 0;
    this.mono = new Float32Array(QUANTUM); this.L = new Float32Array(QUANTUM); this.R = new Float32Array(QUANTUM);
    this.port.onmessage = e => this.message(e.data);
    // buffered mode
    this.buffered = false; this.q = []; this.qEpoch = -1; this.vt = 0; this.waiting = true; this.underruns = 0; this.old = null; this.xf = 0;
    this.chunkPort = null; this.ackN = 0;
    if (!o.wasm) { this.port.postMessage({ type: 'ready', ms: 0, sampleRate, engine: false }); return; }   // buffered only
    loadAmp(o.wasm).then(engine => {
      const t0 = now();
      this.amp = engine.create(sampleRate);
      this.values.forEach((v, id) => this.amp.setParam(id, v));
      this.amp.prepare();
      this.port.postMessage({ type: 'ready', ms: now() - t0, sampleRate });
    }).catch(err => this.port.postMessage({ type: 'error', message: String(err && err.message || err) }));
  }

  message(m) {
    switch (m.type) {
      case 'param':
        if (!this.pending.size) this.pendingSince = this.frames;
        this.values[m.id] = m.value; this.pending.set(m.id, m.value); break;
      case 'recording': this.rec = m.data; this.pos = 0; break;
      case 'source': this.source = m.source; break;
      case 'transport':
        if ('loop' in m) this.loop = m.loop;
        if ('pos' in m) this.pos = Math.max(0, Math.min(m.pos, this.rec ? this.rec.length : 0));
        if ('playing' in m) this.playing = m.playing;
        this.port.postMessage({ type: 'pos', pos: this.pos, playing: this.playing });
        break;
      case 'channel': this.channel = m.channel; break;
      case 'trim': this.trimT = Math.pow(10, m.db / 20); break;
      case 'volume': this.volT = m.db <= -60 ? 0 : Math.pow(10, m.db / 20); break;
      case 'bypass': this.bypass = m.on; break;
      case 'buffered':   // switch to playing the worker's chunks, which arrive on m.port
        this.buffered = true; this.q = []; this.qEpoch = -1; this.waiting = true; this.chunkPort = m.port;
        this.chunkPort.onmessage = e => this.chunk(e.data);
        break;
      case 'stats':   // RMS and peak since the last reset (the analysis tap)
        { const s = this.stats, rms = x => Math.sqrt(x / Math.max(s.n, 1));
          this.port.postMessage({ type: 'stats', id: m.id, n: s.n, inRms: rms(s.in), outRmsL: rms(s.l), outRmsR: rms(s.r), outPeak: s.pk }); }
        if (m.reset) this.stats = { n: 0, in: 0, l: 0, r: 0, pk: 0 };
        break;
    }
  }

  chunk(c) {
    if (c.epoch < this.qEpoch) return;
    if (c.type === 'flush') { this.q = []; this.old = null; this.qEpoch = c.epoch; this.vt = c.start; this.waiting = true; this.ack(); return; }
    if (c.epoch > this.qEpoch) {   // new settings from c.start on: what is queued from there is stale
      this.qEpoch = c.epoch;
      const keep = [], stale = [];
      for (const k of this.q) { if (k.start >= c.start) stale.push(k); else { if (k.start + k.n > c.start) { stale.push({ ...k }); k.n = c.start - k.start; } keep.push(k); } }
      this.q = keep;
      // normally the new stream starts ahead of the playhead and simply follows on; if it is late, fade into it
      if (this.vt > c.start && stale.length) { this.old = stale; this.xf = Math.round(XFADE_SECONDS * sampleRate); this.xfAt = this.vt; }
    }
    this.q.push(c);
  }
  ack() { if (this.chunkPort) this.chunkPort.postMessage({ type: 'played', t: this.vt }); }
  queuedAhead() { let t = this.vt; for (const k of this.q) if (k.start <= t && k.start + k.n > t) t = k.start + k.n; return t - this.vt; }
  // fill L, R (and the dry signal x) from the queue; returns false when it ran dry
  playQueued(x, L, R, n) {
    const len = this.rec ? this.rec.length : 0;
    if (this.waiting) {
      const ended = this.q.some(k => k.end);
      if (this.queuedAhead() < START_SECONDS * sampleRate && !ended) { L.fill(0); R.fill(0); x.fill(0); return true; }
      this.waiting = false;
    }
    for (let i = 0; i < n; i++) {
      while (this.q.length && this.q[0].start + this.q[0].n <= this.vt) { const d = this.q.shift(); if (d.end) { this.playing = false; this.port.postMessage({ type: 'ended' }); } }
      const k = this.q[0];
      if (!this.playing || !k || k.start > this.vt) {
        L.fill(0, i); R.fill(0, i); x.fill(0, i);
        if (this.playing && !this.q.some(c => c.end && c.start + c.n <= this.vt)) { this.waiting = true; this.underruns++; }
        return;
      }
      const off = this.vt - k.start; let l = k.L[off], r = k.R[off];
      if (this.old && this.vt - this.xfAt < this.xf) {
        const o = this.old.find(c => c.start <= this.vt && c.start + c.n > this.vt);
        if (o) { const g = (this.vt - this.xfAt) / this.xf, oo = this.vt - o.start; l = o.L[oo] * (1 - g) + l * g; r = o.R[oo] * (1 - g) + r * g; }
      } else this.old = null;
      L[i] = l; R[i] = r;
      let s = k.src + off; if (len && s >= len) s -= len;
      x[i] = this.rec && s < len ? this.rec[s] : 0; this.pos = s + 1;
      this.vt++;
    }
  }

  process(inputs, outputs) {
    const out = outputs[0], oL = out[0], oR = out[1] || out[0], n = oL.length, x = this.mono;
    // 1. the source, mono
    x.fill(0);
    if (this.source === 'live') {
      const inp = inputs[0] || [];
      if (inp.length !== this.channels) { this.channels = inp.length; this.port.postMessage({ type: 'channels', count: inp.length }); }
      const c = inp[Math.min(this.channel, inp.length - 1)];
      if (c) x.set(c);
    } else if (this.buffered) {
      // the dry signal comes with the queued audio (step 3)
    } else if (this.rec && this.playing && this.amp) {   // the transport waits for the engine
      const r = this.rec;
      for (let i = 0; i < n; i++) {
        if (this.pos >= r.length) {
          if (this.loop) this.pos = 0;
          else { this.playing = false; this.port.postMessage({ type: 'ended' }); break; }
        }
        x[i] = r[this.pos++];
      }
    }
    // 2. input trim (live input only; the recordings are calibrated), input meter
    const live = this.source === 'live';
    let pk = this.inPk;
    for (let i = 0; i < n; i++) {
      if (live) { this.trim += this.k * (this.trimT - this.trim); x[i] *= this.trim; }
      const a = Math.abs(x[i]); if (a > pk) pk = a;
    }
    this.inPk = pk;
    // 3. the amp: parameter changes go in once per block, as a plug-in host applies them. Applying them costs time on
    // top of the block's processing: that time is a debt, paid back only out of later blocks' spare time (the budget
    // less what processing took) and capped at MAX_DEBT, and while it is owed further changes wait, so knob drags never
    // stack up on the audio thread. A change never waits longer than MAX_WAIT, however busy the thread is.
    const budget = 1000 * n / sampleRate;
    let tSet = 0, tProc = 0;
    if (this.buffered && this.source !== 'live') {
      if (this.playing) { this.playQueued(x, this.L, this.R, n); let pk = this.inPk; for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(x[i])); this.inPk = pk; }
      else { this.L.fill(0); this.R.fill(0); }
      if (++this.ackN >= 8) { this.ackN = 0; this.ack(); }
    } else if (this.amp) {
      if (this.pending.size && (this.debt <= 0 || 1000 * (this.frames - this.pendingSince) / sampleRate >= MAX_WAIT)) {
        const ts = now();
        for (const [id, v] of this.pending) this.amp.setParam(id, v);
        this.pending.clear(); tSet = now() - ts;
      }
      const tp = now(); this.amp.process(x, this.L, this.R, n); tProc = now() - tp;
    } else { this.L.fill(0); this.R.fill(0); }
    this.frames += n;
    const dt = tSet + tProc;
    this.debt = Math.min(MAX_DEBT, Math.max(0, this.debt + tSet - Math.max(0, budget - tProc)));
    this.busy += dt; this.span += budget;
    if (this.span >= 1000) { this.load = this.busy / this.span; this.busy = 0; this.span = 0; }   // over the last second
    // 4. bypass crossfade, volume, output meter, analysis tap
    const wetT = this.bypass ? 0 : 1, s = this.stats;
    let opk = this.outPk;
    for (let i = 0; i < n; i++) {
      this.wet += 2 * this.k * (wetT - this.wet);
      this.vol += this.k * (this.volT - this.vol);
      const dry = x[i] * (1 - this.wet);
      const l = (this.L[i] * this.wet + dry) * this.vol, r = (this.R[i] * this.wet + dry) * this.vol;
      oL[i] = l; if (oR !== oL) oR[i] = r;
      const a = Math.max(Math.abs(l), Math.abs(r)); if (a > opk) opk = a;
      s.in += x[i] * x[i]; s.l += l * l; s.r += r * r; if (a > s.pk) s.pk = a;
    }
    s.n += n; this.outPk = opk;
    for (let c = 2; c < out.length; c++) out[c].fill(0);
    // 5. reports, ~30 times a second
    if (++this.meterN >= this.meterEvery) {
      this.port.postMessage({ type: 'meter', in: this.inPk, out: this.outPk, pos: this.pos, playing: this.playing,
        load: this.load, ready: !!this.amp, buffered: this.buffered, ahead: this.buffered ? this.queuedAhead() / sampleRate : 0,
        waiting: this.buffered && this.waiting, underruns: this.underruns });
      this.meterN = 0; this.inPk = 0; this.outPk = 0;
    }
    return true;
  }
}

registerProcessor('amp-player', AmpPlayer);

// The browser amp player: the amp's panel (knobs, switches, help line), a built-in recording or the live input as the
// source, and the engine running in an AudioWorklet (worklet.js). Shared by every amp: everything amp-specific comes in
// the `amp` object (see docs/play/amp.js). It mounts into any element of any page (its styles are scoped to
// .amp-player); options:
//   base        where amp.wasm and recordings/ live, relative to the page (default '.')
//   recordings  the recordings list, relative to base (default 'recordings/recordings.json')
//   lazy        true: fetch the engine and the audio only on warm() or the first Play (the page itself stays light)
//   anchor      the section's id: the knob settings are kept in the address as #anchor&key=value&...
// Also exposes window.ampPlayer for tests (offline renders, the analysis tap, a main-thread benchmark).
// SPDX-License-Identifier: AGPL-3.0-or-later
import { createKnob, createToggle, snap, fmt } from './knob.js';
import { loadAmp } from './wasm-amp.js';

const WORKLET = new URL('./worklet.js', import.meta.url);
const h = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v; else if (k === 'text') e.textContent = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v); else e.setAttribute(k, v);
  }
  for (const k of kids) if (k != null) e.append(k);
  return e;
};
const dbfs = a => (a > 1e-6 ? 20 * Math.log10(a) : -120);
const clock = s => { s = Math.max(0, s); const m = Math.floor(s / 60), r = s - 60 * m; return `${m}:${r < 10 ? '0' : ''}${r.toFixed(1)}`; };

export async function mountPlayer(root, amp, opts = {}) {
  const base = new URL(opts.base || '.', location.href);
  const recList = new URL(opts.recordings || 'recordings/recordings.json', base);
  const P = amp.params, byKey = Object.fromEntries(P.map((p, i) => [p.key, i]));
  root.classList.add('amp-player');
  const de = root.style;
  for (const [k, v] of Object.entries(amp.colours || {})) de.setProperty(`--${k}`, v);
  if (amp.fonts && amp.fonts.display) de.setProperty('--display', `'${amp.fonts.display}'`);

  // ---- state: the knobs (from the plug-in's defaults, then the address's #key=value list), the source, the monitor
  const values = P.map(p => p.def);
  const fromHash = () => {
    for (const kv of location.hash.slice(1).split('&')) {
      const [k, v] = kv.split('='); const i = byKey[decodeURIComponent(k || '')];
      if (i !== undefined && P[i].kind !== 'input' && v !== undefined && isFinite(+v)) values[i] = snap(P[i], +v);
    }
  };
  fromHash();
  const S = { source: 'recording', rec: null, recIndex: 0, playing: false, loop: true, pos: 0, bypass: false, volume: 0, trim: 0, channel: 0,
              stream: null, deviceId: '', channels: 0, liveVolts: (amp.input && amp.input.volts) || 1.6 };
  const A = { ctx: null, node: null, hint: '', ready: false, readyMs: 0, mic: null, load: 0, pending: null, engine: false };
  // buffered playback (recordings, on devices too slow for real time): the engine in a worker (render-worker.js), ahead
  // of the playhead. Mode: 'auto' (real time, unless this device measured too slow: remembered), 'realtime', 'buffered'
  const store = { get: k => { try { return localStorage.getItem(`${amp.name}.${k}`); } catch { return null; } },
                  set: (k, v) => { try { localStorage.setItem(`${amp.name}.${k}`, v); } catch {} } };
  const B = { modeN: 0, worker: null, ready: null, rate: 0, active: false, speed: 0, ahead: 0, waiting: false, mode: store.get('playback') || 'auto', slow: store.get('slow') === '1', timer: 0, last: 0, hot: 0 };
  const wantBuffered = () => S.source === 'recording' && (B.mode === 'buffered' || (B.mode === 'auto' && B.slow));
  const decoded = new Map();   // recording index -> { rate, data }

  // ---- page
  const help = h('div', { class: 'help', 'aria-live': 'polite', text: amp.idleHelp || '' });
  const showHelp = p => { help.textContent = p && p.help ? p.help : (amp.idleHelp || ''); };
  const controls = [];
  const send = (id, v) => { if (A.node) A.node.port.postMessage({ type: 'param', id, value: v }); };
  let hashTimer = 0;
  const change = (id, v) => {
    values[id] = v; send(id, v); if (B.active) workerValues();
    clearTimeout(hashTimer);
    hashTimer = setTimeout(() => {
      const q = P.map((p, i) => (p.kind !== 'input' && values[i] !== p.def ? `${p.key}=${values[i]}` : null)).filter(Boolean).join('&');
      const hash = [opts.anchor, q].filter(Boolean).join('&');
      history.replaceState(null, '', hash ? `#${hash}` : location.pathname + location.search);
    }, 250);
  };
  const ctl = (p, i) => {
    const o = { onChange: v => change(i, v), onFocus: showHelp, onBlur: () => showHelp(null) };
    const c = p.kind === 'toggle' ? createToggle(p, values[i], o) : createKnob(p, values[i], o);
    controls[i] = c; return c.el;
  };

  const jewel = h('span', { class: 'jewel', 'aria-hidden': 'true' });
  const switches = h('div', { class: 'sw' }, ...P.map((p, i) => (p.kind === 'toggle' ? ctl(p, i) : null)));
  const rows = h('div', { class: 'rows' });
  const ncol = Math.max(...amp.layout.map(r => r.length));
  for (const row of amp.layout) {
    const r = h('div', { class: 'row' });
    // as many columns as the widest row, sized to their knobs; a later row's groups line up under the first row's
    // (col: a group's 1-based column)
    r.style.setProperty('--cols', `repeat(${ncol}, auto)`);
    row.forEach((g, gi) => {
      const grp = h('div', { class: 'group' + (g.align === 'end' ? ' end' : '') },
        h('h3', { text: g.title }), h('div', { class: 'knobs' }, ...g.keys.map(k => ctl(P[byKey[k]], byKey[k]))));
      grp.style.gridColumn = String(g.col || gi + 1);
      r.append(grp);
    });
    rows.append(r);
  }
  const panel = h('section', { class: 'panel', 'aria-label': `${amp.name} controls` },
    h('div', { class: 'phead' }, jewel, h('div', { class: 'mark' }, h('div', { class: 'word', text: amp.name }), h('div', { class: 'tag', text: amp.tagline || '' })), switches),
    rows, h('div', { class: 'pfoot' }, help, h('span', { class: 'cap', text: amp.footer || '' })));
  panel.style.backgroundImage = `url(${grain(1120, 520, root)})`;

  // source card
  const recSel = h('select', { 'aria-label': 'Input: a recording or your guitar' });
  const recDesc = h('p', { class: 'desc' });
  const playBtn = h('button', { class: 'btn main', type: 'button', text: 'Play' });
  const restartBtn = h('button', { class: 'btn', type: 'button', text: 'Restart' });
  const loopBtn = h('button', { class: 'btn', type: 'button', 'aria-pressed': 'true', text: 'Loop' });
  const seek = h('input', { type: 'range', min: '0', max: '1', step: '0.01', value: '0', 'aria-label': 'Position' });
  const timeTxt = h('span', { class: 'time', text: '0:00.0' });
  // buffered mode: what has been rendered ahead of the playhead, under the seek bar (as a video shows what it has loaded)
  const bufBar = h('div', { class: 'bufbar hide', title: 'Rendered ahead' }, h('i'), h('i'));
  const loadBar = h('div', { class: 'loadbar hide', role: 'progressbar', 'aria-label': 'Loading' }, h('i'));
  const recPane = h('div', {},
    h('div', { class: 'line' }, playBtn, restartBtn, loopBtn), loadBar, h('div', { class: 'line' }, seek, timeTxt), bufBar);

  const liveBtn = h('button', { class: 'btn main', type: 'button', text: 'Allow the audio input' });
  const stopBtn = h('button', { class: 'btn hide', type: 'button', text: 'Stop input' });
  const devSel = h('select', { 'aria-label': 'Input device' }, h('option', { value: '', text: 'Default input' }));
  const chSel = h('select', { 'aria-label': 'Input channel' }, h('option', { value: '0', text: 'Input 1' }));
  const trim = h('input', { type: 'range', min: '-24', max: '24', step: '0.5', value: '0', 'aria-label': 'Input trim (dB)' });
  const trimTxt = h('span', { class: 'readout', text: '0.0 dB' });
  const latTxt = h('p', { class: 'note' });
  const liveWarn = h('p', { class: 'warn hide', role: 'alert', text: "This device can't run the amp in real time. Try a faster computer or a desktop browser." });
  const livePane = h('div', { class: 'hide' },
    liveWarn,
    h('div', { class: 'line' }, liveBtn, stopBtn),
    h('div', { class: 'line' }, h('label', { text: 'Device' }), devSel),
    h('div', { class: 'line' }, h('label', { text: 'Channel' }), chSel),
    h('div', { class: 'line' }, h('label', { text: 'Input trim' }), trim, trimTxt),
    h('p', { class: 'note', text: (amp.input && amp.input.note) || '' }),
    h('p', { class: 'note' }, h('b', { text: 'Use headphones. ' }),
      'Speakers feed the amp back into your pickups, and the browser adds latency: the figure below is what it reports. Plug the guitar into an audio interface (a phone or laptop microphone will not sound like a guitar). Echo cancellation, noise suppression and automatic gain are all switched off.'),
    latTxt);
  const srcCard = h('section', { class: 'card', 'aria-label': 'Input' }, h('h2', { text: 'Input' }),
    h('div', { class: 'line' }, recSel), recDesc, recPane, livePane);

  // monitor card
  const presetSel = h('select', { 'aria-label': 'Preset' }, h('option', { value: '', text: 'Presets…' }),
    ...(amp.presets || []).map((pr, i) => h('option', { value: String(i), text: pr.name })));
  const modeSel = h('select', { 'aria-label': 'Playback mode' },
    h('option', { value: 'auto', text: 'Auto' }), h('option', { value: 'realtime', text: 'Real time' }), h('option', { value: 'buffered', text: 'Buffered' }));
  const modeTxt = h('p', { class: 'stats mode' });
  const bypassBtn = h('button', { class: 'btn', type: 'button', 'aria-pressed': 'false', text: 'Bypass' });
  const vol = h('input', { type: 'range', min: '-40', max: '6', step: '0.5', value: '0', 'aria-label': 'Volume (dB)' });
  const volTxt = h('span', { class: 'readout', text: '0.0 dB' });
  const meter = (aim) => { const m = h('div', { class: 'meter', role: 'meter', 'aria-valuemin': '-60', 'aria-valuemax': '0' }, h('i')); if (aim !== undefined) { const b = h('b', { title: `aim: ${aim} dBFS` }); b.style.left = `${(aim + 60) / 60 * 100}%`; m.append(b); } return m; };
  const inMeter = meter(amp.input ? amp.input.aimDb : undefined), outMeter = meter();
  const inTxt = h('span', { class: 'readout', text: '–' }), outTxt = h('span', { class: 'readout', text: '–' });
  const status = h('p', { class: 'status', role: 'status', text: 'Press Play to start: the first time, the circuit takes a moment to warm up (its tables are built in your browser).' });
  const statsTxt = h('p', { class: 'stats' });
  const loadWarn = h('p', { class: 'status err hide', role: 'alert' });
  const monCard = h('section', { class: 'card', 'aria-label': 'Monitor' }, h('h2', { text: 'Monitor' }),
    h('div', { class: 'line' }, bypassBtn, presetSel),
    h('div', { class: 'line' }, h('label', { text: 'Playback' }), modeSel), modeTxt,
    h('div', { class: 'line' }, h('label', { text: 'Volume' }), vol, volTxt),
    h('div', { class: 'line' }, h('label', { text: 'Input' }), inMeter, inTxt),
    h('div', { class: 'line' }, h('label', { text: 'Output' }), outMeter, outTxt),
    status, statsTxt, loadWarn);

  root.append(panel, h('div', { class: 'deck' }, srcCard, monCard));

  // ---- loading: the recording list at once; the engine and the first recording at once too, or with opts.lazy on
  // warm() (the page calls it when the player scrolls into view) or the first Play
  // downloads report their progress (bytes so far / expected) to the loading state on the Play button
  const got = new Map();
  async function download(url) {
    const r = await fetch(url); if (!r.ok) throw new Error(`${url.split('/').pop()}: HTTP ${r.status}`);
    const total = +r.headers.get('content-length') || 0;
    if (!r.body || !r.body.getReader) { const b = await r.arrayBuffer(); got.set(url, [b.byteLength, b.byteLength]); busyTick(); return b; }
    const rd = r.body.getReader(), parts = []; let n = 0;
    for (;;) { const { done, value } = await rd.read(); if (done) break; parts.push(value); n += value.length; got.set(url, [n, Math.max(total, n)]); busyTick(); }
    const out = new Uint8Array(n); let o = 0; for (const p of parts) { out.set(p, o); o += p.length; }
    got.set(url, [n, n]); busyTick(); return out.buffer;
  }
  const wasmUrl = new URL(amp.wasm, base).href;
  let wasmP = null;
  const engineBytes = () => wasmP || (wasmP = download(wasmUrl));
  // warm(): the engine and the chosen recording download, and the recording decodes at the usual 48 kHz, so Play is
  // quick (not with the browser's data saver on: then only on Play)
  let warmed = false;
  const warm = () => {
    if (warmed || (navigator.connection && navigator.connection.saveData)) return; warmed = true;
    engineBytes().catch(() => {});
    if (recs[S.recIndex]) decode(S.recIndex, 48000).catch(() => {});
  };
  let recs = [];
  const recBytes = new Map();
  const fetchBytes = url => { if (!recBytes.has(url)) recBytes.set(url, download(url)); return recBytes.get(url); };
  try {
    recs = (await (await fetch(recList)).json()).recordings || [];
  } catch (e) { setStatus(`Could not load the recordings list: ${e.message}`, true); }
  // the picker: every recording (name, guitar, length), then the live input
  recs.forEach((r, i) => recSel.append(h('option', { value: `rec:${i}`, text: [r.name, r.guitar, r.seconds ? clock(r.seconds).replace(/\.\d$/, '') : ''].filter(Boolean).join(' · ') })));
  recSel.append(h('option', { value: 'live', text: 'Your guitar (audio input)…' }));
  const showRec = () => {
    const r = recs[S.recIndex];
    recDesc.textContent = S.source === 'live' ? '' : r ? r.description || '' : '';
    if (r) { seek.max = String(r.seconds || 1); timeTxt.textContent = `${clock(S.pos)} / ${clock(r.seconds || 0)}`; }
  };
  showRec();
  if (!opts.lazy) warm();

  function setStatus(t, err) { status.textContent = t; status.classList.toggle('err', !!err); }

  // decode a recording at a sample rate, trying its fallbacks (another container for the same Opus stream) when the
  // browser refuses a file or decodes it to the wrong length (WebKit has had Ogg Opus quirks)
  const decoding = new Map();   // one decode per recording and rate, however many callers wait for it
  function decode(i, rate, ctx) {
    const key = `${i}@${rate}`;
    if (!decoding.has(key)) decoding.set(key, decodeOnce(i, rate, ctx).catch(e => { decoding.delete(key); throw e; }));
    return decoding.get(key);
  }
  async function decodeOnce(i, rate, ctx) {
    const key = `${i}@${rate}`;
    if (!recs[i]) throw new Error(`no recording ${i}`);
    if (decoded.has(key)) return decoded.get(key);
    const r = recs[i], files = [r.file, ...(r.fallbacks || [])], errors = [];
    for (const f of files) {
      try {
        const url = new URL(f, recList).href;
        const bytes = (await fetchBytes(url)).slice(0);   // decodeAudioData detaches its buffer
        const c = ctx || new OfflineAudioContext(1, 1, rate);
        const buf = await new Promise((res, rej) => { const p = c.decodeAudioData(bytes, res, rej); if (p && p.then) p.then(res, rej); });
        const data = buf.getChannelData(0);
        let pk = 0; for (let k = 0; k < data.length; k += 7) pk = Math.max(pk, Math.abs(data[k]));
        if (r.seconds && Math.abs(buf.duration - r.seconds) > 0.25) throw new Error(`decoded to ${buf.duration.toFixed(2)} s, not ${r.seconds} s`);
        if (dbfs(pk) < -50) throw new Error('decoded to silence');
        const out = { rate: buf.sampleRate, data: new Float32Array(data), file: f, duration: buf.duration };
        if (r.trim_db) { const g = Math.pow(10, r.trim_db / 20); for (let k = 0; k < out.data.length; k++) out.data[k] *= g; }
        decoded.set(key, out);
        return out;
      } catch (e) { errors.push(`${f}: ${e && e.message || e}`); }
    }
    throw new Error(`this browser could not decode the recording (${errors.join('; ')})`);
  }

  // ---- audio: one context; low latency for the live input, a roomier buffer for recordings (some knob moves rebuild
  // a table, and the larger buffer absorbs that without a glitch)
  async function ensureAudio(hint, engine = true) {
    if (A.ctx && (A.hint === hint || (A.hint === 'interactive' && hint === 'playback')) && (A.engine || !engine)) { if (A.ctx.state !== 'running') await A.ctx.resume(); return; }
    closeAudio();
    const ctx = new AudioContext({ latencyHint: hint });
    const resumed = ctx.resume();   // inside the click, for browsers that want the gesture
    A.ctx = ctx; A.hint = hint; A.ready = false; A.engine = engine;
    if (engine) setStatus('Warming up the circuit: building its tables…');
    await ctx.audioWorklet.addModule(WORKLET);
    A.engineReady = new Promise((res, rej) => { A.onReady = res; A.onFail = rej; });
    const node = new AudioWorkletNode(ctx, 'amp-player', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCountMode: 'max',
      processorOptions: { wasm: engine ? await engineBytes() : null, values: values.slice(), source: S.source, loop: S.loop, bypass: S.bypass },
    });
    node.port.onmessage = e => onWorklet(e.data);
    node.connect(ctx.destination);
    A.node = node;
    node.port.postMessage({ type: 'volume', db: S.volume });
    node.port.postMessage({ type: 'trim', db: S.trim });
    node.port.postMessage({ type: 'channel', channel: S.channel });
    send(byKey.involts, S.source === 'live' ? S.liveVolts : recVolts());
    await resumed;
    if (S.stream) connectMic();
    showLatency();
  }
  function closeAudio() {
    if (A.ctx) { try { A.ctx.close(); } catch {} }
    A.ctx = null; A.node = null; A.ready = false; A.mic = null; A.engine = false; A.recLoaded = -1; B.active = false;
  }
  // ---- the worker (buffered mode)
  async function ensureWorker() {
    const rate = A.ctx.sampleRate;
    if (B.worker && B.rate === rate) return B.ready;
    if (B.worker) B.worker.terminate();
    B.rate = rate; B.worker = new Worker(new URL('./render-worker.js', import.meta.url), { type: 'module' });
    B.ready = new Promise((res, rej) => {
      B.worker.onmessage = e => { const m = e.data;
        if (m.type === 'ready') res(m); else if (m.type === 'speed') B.speed = m.speed;
        else if (m.type === 'error') { rej(new Error(m.message)); setStatus(`Buffered playback failed: ${m.message}`, true); } };
      B.worker.onerror = e => rej(new Error(e.message || 'the worker failed to start'));
    });
    engineBytes().then(w => B.worker.postMessage({ type: 'init', wasm: w, rate }));
    return B.ready;
  }
  const workerVals = () => { const v = values.slice(); v[byKey.involts] = recVolts(); return v; };
  function workerValues() {   // knob moves, at most every 100 ms (the worker goes back to the playhead each time)
    const go = () => { B.timer = 0; B.last = performance.now(); if (B.active) B.worker.postMessage({ type: 'values', values: workerVals() }); };
    if (B.timer) return;
    const wait = 100 - (performance.now() - B.last);
    if (wait <= 0) go(); else B.timer = setTimeout(go, wait);
  }
  // start buffered playback of the chosen recording from S.pos (the context, then the recording, then the worker's
  // engine, then the first 0.75 s rendered)
  async function playBuffered() {
    if (B.active && A.node && A.recLoaded === S.recIndex) {   // resume: the buffer is still there
      if (A.ctx.state !== 'running') await A.ctx.resume();
      S.playing = true; A.node.port.postMessage({ type: 'transport', playing: true });
      setBusy('buffer'); await new Promise(res => { B.onPlaying = res; });
      setBusy(null); playBtn.textContent = 'Pause'; setStatus('Playing (buffered).'); return;
    }
    setBusy('download', [wasmUrl, recUrl(S.recIndex)]);
    await ensureAudio(A.hint || 'playback', false);
    const [d] = await Promise.all([fetchBytes(recUrl(S.recIndex)).then(() => { setBusy('decode'); return decode(S.recIndex, A.ctx.sampleRate, A.ctx); }), engineBytes()]);
    setBusy('warm'); setStatus('Buffered playback: warming up the circuit…');
    await ensureWorker();
    A.node.port.postMessage({ type: 'recording', data: d.data.slice(0) }); A.recLoaded = S.recIndex; A.recRate = d.rate;
    if (!B.active) {
      const ch = new MessageChannel();
      B.worker.postMessage({ type: 'port', port: ch.port1 }, [ch.port1]);
      A.node.port.postMessage({ type: 'buffered', port: ch.port2 }, [ch.port2]);
      B.active = true;
    }
    B.worker.postMessage({ type: 'start', values: workerVals(), rec: d.data, loop: S.loop, src: Math.round(S.pos * d.rate) });
    S.playing = true; A.node.port.postMessage({ type: 'transport', playing: true });
    setBusy('buffer'); B.waiting = true;
    await new Promise(res => { B.onPlaying = res; });
    setBusy(null); playBtn.textContent = 'Pause'; setStatus('Playing (buffered).'); showMode();
  }
  // the device measured too slow for real time: remember it and carry on buffered from here
  async function toBuffered() {
    B.slow = true; store.set('slow', '1');
    const was = S.playing; pause(); closeAudio();
    setStatus('This device can\'t run the amp in real time, so it now renders ahead and plays from a buffer (the sound is the same).');
    if (was) play();
  }
  const recVolts = () => (recs[S.recIndex] && recs[S.recIndex].involts) || (amp.input && amp.input.volts) || 1.6;

  async function loadRecording(i, from = 0) {
    const d = await decode(i, A.ctx.sampleRate, A.ctx);
    A.node.port.postMessage({ type: 'recording', data: d.data.slice(0) });
    S.pos = from;
    A.node.port.postMessage({ type: 'transport', pos: Math.round(from * d.rate), loop: S.loop, playing: S.playing });
    A.recLoaded = i; A.recRate = d.rate;
    return d;
  }

  function onWorklet(m) {
    if (m.type === 'ready') {
      A.ready = true; A.readyMs = m.ms; if (A.onReady) { A.onReady(); A.onReady = null; }
      setStatus(`Ready. The circuit's tables took ${(m.ms / 1000).toFixed(1)} s to build; running at ${(m.sampleRate / 1000).toFixed(1)} kHz.`);
    } else if (m.type === 'error') { setStatus(`The engine failed to start: ${m.message}`, true); if (A.onFail) A.onFail(new Error(m.message)); }
    else if (m.type === 'meter') meterIn(m);
    else if (m.type === 'pos') { if (A.recRate) S.pos = m.pos / A.recRate; }
    else if (m.type === 'ended') { S.playing = false; S.pos = 0; playBtn.textContent = 'Play'; A.node.port.postMessage({ type: 'transport', pos: 0 }); }
    else if (m.type === 'channels') { S.channels = m.count; fillChannels(); }
    else if (m.type === 'stats' && A.statsWait) { A.statsWait(m); A.statsWait = null; }
  }

  // ---- meters (and the pilot jewel, which warms with the output, as on the plug-in)
  const hold = { in: { v: 0, t: 0 }, out: { v: 0, t: 0 } };
  let lit = 0;
  function setMeter(el, txt, hd, pk, hot) {
    const d = dbfs(pk), now = performance.now();
    el.firstChild.style.width = `${Math.max(0, Math.min(100, (d + 60) / 60 * 100))}%`;
    el.classList.toggle('hot', d > hot);
    el.setAttribute('aria-valuenow', d.toFixed(1));
    if (d > hd.v || now - hd.t > 1500) { hd.v = d; hd.t = now; }
    txt.textContent = hd.v <= -100 ? '–' : `${hd.v.toFixed(1)} dB`;
  }
  function meterIn(m) {
    setMeter(inMeter, inTxt, hold.in, m.in, -1);
    setMeter(outMeter, outTxt, hold.out, m.out, -1);
    const target = Math.min(1, m.out * 2.5);
    lit += (target > lit ? 0.3 : 0.05) * (target - lit);
    jewel.style.setProperty('--lit', lit.toFixed(3));
    if (S.source === 'recording' && A.recRate && !seeking) {
      S.pos = m.pos / A.recRate; seek.value = String(S.pos);
      const r = recs[S.recIndex]; timeTxt.textContent = `${clock(S.pos)} / ${clock(r ? r.seconds : 0)}`;
    }
    A.load = m.load;
    // buffered: the rendered-ahead stretch on the seek bar (as a video shows what it has loaded), and the playhead
    // waiting for the buffer after a start, a seek or a run-dry
    B.ahead = m.ahead || 0; B.waiting = !!m.waiting;
    if (B.active && S.source === 'recording') {
      const dur = (recs[S.recIndex] && recs[S.recIndex].seconds) || 1, a = 100 * S.pos / dur, b = Math.min(200, 100 * (S.pos + B.ahead) / dur);
      const [i1, i2] = bufBar.children; bufBar.classList.remove('hide');
      i1.style.left = `${a}%`; i1.style.width = `${Math.min(b, 100) - a}%`;   // to the end, then on from the start (the loop)
      i2.style.left = '0%'; i2.style.width = `${Math.max(0, b - 100)}%`;
      if (S.playing && !m.waiting && B.onPlaying) { B.onPlaying(); B.onPlaying = null; }
      if (S.playing && m.waiting && !busy) { setBusy('buffer'); B.onPlaying = () => { setBusy(null); playBtn.textContent = 'Pause'; }; }
      if (busy && busy.stage === 'buffer') busyTick();
    } else bufBar.classList.add('hide');
    statsTxt.textContent = (B.active && S.source === 'recording'
      ? `Buffered · rendering at ${(100 * B.speed).toFixed(0)}% of real time · ${B.ahead.toFixed(1)} s ready` + (m.underruns ? ` · ran dry ${m.underruns}×` : '')
      : `DSP load ${(100 * m.load).toFixed(1)}% of real time`) + ` · ${(A.ctx.sampleRate / 1000).toFixed(1)} kHz` + latencyShort();
    // the load is measured over each second (a new figure once a second)
    if (m.load !== A.lastLoad) {
      A.lastLoad = m.load; A.hot = m.load > 0.85 ? (A.hot || 0) + 1 : 0;
      const measured = A.ready && S.playing && performance.now() - (B.since || 0) > 1500;   // a whole second of playing
      // a recording in real time on a device that can't keep up: switch to buffered (auto), once and for good
      if (measured && S.source === 'recording' && !B.active && B.mode === 'auto' && !busy) {
        B.hot = m.load > 0.6 ? B.hot + 1 : 0;
        if (m.load > 0.85 || B.hot >= 2) toBuffered();
      }
      // the live input: real time or nothing, so say so when the device can't keep up
      if (S.source === 'live' && A.ready && S.stream && m.load > 0.85) liveWarn.classList.remove('hide');
    }
    const showLoad = A.hot >= 2 && !B.active && S.source === 'recording' && B.mode === 'realtime';
    loadWarn.classList.toggle('hide', !showLoad);
    if (showLoad) loadWarn.textContent = `The amp is using ${Math.round(100 * m.load)}% of the time this device gives the audio, so it may crackle. Closing other tabs helps, or set Playback to Buffered.`;
    if (++B.modeN % 15 === 0) showMode();
  }
  function showMode() {
    modeSel.value = B.mode;
    modeTxt.textContent = S.source === 'live' ? 'Real time: the live input always is.'
      : B.active || (!A.ctx && wantBuffered()) ? `Buffered: the amp renders ahead of the playhead on another thread; the sound is the same. Knob changes are heard about ${(Math.max(0.12, 1.5 * B.speed * 0.125 + 0.05) + 0.1).toFixed(1)} s later.`
      : `Real time.${B.mode === 'auto' ? ' If this device can\'t keep up, it switches to buffered playback by itself.' : ''}`;
  }
  showMode();
  modeSel.addEventListener('change', () => {
    B.mode = modeSel.value; store.set('playback', B.mode);
    if (B.mode === 'auto') { B.slow = false; store.set('slow', '0'); }   // measure again
    showMode();
    if (S.source === 'recording' && A.ctx && B.active !== wantBuffered()) { const was = S.playing; pause(); closeAudio(); if (was) play(); }
  });

  // ---- transport
  // ---- the loading state, on the Play button and the bar under it: downloads (with their real progress), then
  // decoding, then the circuit's tables; the button only becomes Pause once the sound is actually going
  let busy = null;   // { urls, stage }
  function setBusy(stage, urls) {
    if (!stage) { busy = null; playBtn.classList.remove('busy'); playBtn.removeAttribute('aria-busy'); loadBar.classList.add('hide'); return; }
    busy = { stage, urls: urls || (busy && busy.urls) || [] };
    playBtn.classList.add('busy'); playBtn.setAttribute('aria-busy', 'true'); loadBar.classList.remove('hide'); busyTick();
  }
  function busyTick() {
    if (!busy) return;
    let n = 0, t = 0; for (const u of busy.urls) { const g = got.get(u); if (g) { n += g[0]; t += g[1]; } else t += 1e6; }
    const dl = t ? n / t : 1;
    const buf = Math.min(1, B.ahead / 0.75);
    const f = busy.stage === 'download' ? 0.6 * dl : busy.stage === 'decode' ? 0.7 : busy.stage === 'warm' ? 0.85 : busy.stage === 'buffer' ? buf : 1;
    playBtn.textContent = busy.stage === 'download' ? `Loading ${Math.round(100 * dl)}%` : busy.stage === 'decode' ? 'Decoding…'
      : busy.stage === 'buffer' ? `Buffering ${Math.round(100 * buf)}%` : 'Warming up…';
    loadBar.firstChild.style.width = `${(100 * f).toFixed(1)}%`;
    loadBar.classList.toggle('wait', busy.stage === 'decode' || busy.stage === 'warm');
    loadBar.title = playBtn.textContent;
  }
  const recUrl = i => new URL(recs[i].file, recList).href;
  async function play() {
    if (!recs.length || busy) return;
    if (wantBuffered()) { try { await playBuffered(); } catch (e) { setBusy(null); playBtn.textContent = 'Play'; S.playing = false; setStatus(e.message || String(e), true); } return; }
    try {
      const needRec = A.recLoaded !== S.recIndex || !A.ctx || A.recRate !== A.ctx.sampleRate;
      setBusy('download', [...(A.ready ? [] : [wasmUrl]), ...(needRec ? [recUrl(S.recIndex)] : [])]);
      await ensureAudio(A.hint || new URLSearchParams(location.search).get('latency') || 'playback');
      if (A.recLoaded !== S.recIndex || A.recRate !== A.ctx.sampleRate) {
        setStatus(A.ready ? 'Decoding the recording…' : 'Warming up the circuit and decoding the recording…');
        await fetchBytes(recUrl(S.recIndex)); setBusy('decode');
        await loadRecording(S.recIndex, S.pos);
      }
      S.playing = true;
      A.node.port.postMessage({ type: 'transport', playing: true, loop: S.loop });   // the worklet starts it once the engine is up
      if (!A.ready) { setBusy('warm'); await A.engineReady; }
      setBusy(null); playBtn.textContent = 'Pause'; setStatus('Playing.'); B.hot = 0; B.since = performance.now(); showMode();
    } catch (e) { setBusy(null); playBtn.textContent = S.playing ? 'Pause' : 'Play'; setStatus(e.message || String(e), true); }
  }
  function pause() { S.playing = false; playBtn.textContent = 'Play'; if (A.node) A.node.port.postMessage({ type: 'transport', playing: false }); }
  playBtn.addEventListener('click', () => (busy ? null : S.playing ? pause() : play()));
  const jump = src => { if (B.active) B.worker.postMessage({ type: 'seek', src }); else if (A.node && A.recRate) A.node.port.postMessage({ type: 'transport', pos: src }); };
  restartBtn.addEventListener('click', () => { S.pos = 0; seek.value = '0'; jump(0); if (!S.playing) play(); });
  loopBtn.addEventListener('click', () => { S.loop = !S.loop; loopBtn.setAttribute('aria-pressed', String(S.loop)); if (A.node) A.node.port.postMessage({ type: 'transport', loop: S.loop }); if (B.active) B.worker.postMessage({ type: 'loop', loop: S.loop }); });
  let seeking = false;
  seek.addEventListener('input', () => { seeking = true; S.pos = +seek.value; const r = recs[S.recIndex]; timeTxt.textContent = `${clock(S.pos)} / ${clock(r ? r.seconds : 0)}`; });
  seek.addEventListener('change', () => { seeking = false; if (A.recRate) jump(Math.round(S.pos * A.recRate)); });
  // choosing a recording keeps the knobs and keeps playing if it was; choosing the live input asks for it at once
  let resumeAfterLive = false;
  recSel.addEventListener('change', async () => {
    if (recSel.value === 'live') { resumeAfterLive = S.playing; setSource('live'); showRec(); startLive(); return; }
    const i = +recSel.value.slice(4), back = S.source === 'live';
    setSource('recording');
    const keep = back ? resumeAfterLive : S.playing;
    S.recIndex = i; S.pos = 0; seek.value = '0'; showRec();
    if (A.node && B.active && wantBuffered()) {
      try {
        if (!decoded.has(`${i}@${A.ctx.sampleRate}`)) { setBusy('download', [recUrl(i)]); await fetchBytes(recUrl(i)); setBusy('decode'); }
        const d = await decode(i, A.ctx.sampleRate, A.ctx); A.recLoaded = i;
        A.node.port.postMessage({ type: 'recording', data: d.data.slice(0) });
        B.worker.postMessage({ type: 'values', values: workerVals() });
        B.worker.postMessage({ type: 'seek', src: 0, rec: d.data });
        setBusy(null); playBtn.textContent = S.playing ? 'Pause' : 'Play'; setStatus(S.playing ? 'Playing (buffered).' : 'Ready.');
        if (!S.playing && keep) play();
      } catch (e) { setBusy(null); playBtn.textContent = 'Play'; setStatus(e.message, true); }
    } else if (A.node && wantBuffered()) { pause(); closeAudio(); if (keep) play(); }
    else if (A.node) {
      send(byKey.involts, recVolts());
      try {
        setStatus('Decoding the recording…'); S.playing = keep;
        if (!decoded.has(`${i}@${A.ctx.sampleRate}`)) { setBusy('download', [recUrl(i)]); await fetchBytes(recUrl(i)); setBusy('decode'); }
        await loadRecording(S.recIndex); setBusy(null); setStatus(keep ? 'Playing.' : 'Ready.'); playBtn.textContent = keep ? 'Pause' : 'Play';
      } catch (e) { setBusy(null); playBtn.textContent = 'Play'; setStatus(e.message, true); }
    } else if (keep) play();
  });

  // ---- live input
  function setSource(src) {
    if (S.source === src) return;
    S.source = src;
    recPane.classList.toggle('hide', src !== 'recording'); livePane.classList.toggle('hide', src !== 'live');
    recSel.value = src === 'live' ? 'live' : `rec:${S.recIndex}`;
    if (src === 'live' && S.playing) pause();
    if (A.node) { A.node.port.postMessage({ type: 'source', source: src }); send(byKey.involts, src === 'live' ? S.liveVolts : recVolts()); }
  }

  async function startLive() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) { setStatus('This browser has no audio input here (it needs HTTPS and a recent browser).', true); return; }
    try {
      const audio = { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: { ideal: 8 }, latency: { ideal: 0 } };
      if (S.deviceId) audio.deviceId = { exact: S.deviceId };
      if (B.active) closeAudio();   // the live input always runs in real time
      const audioUp = ensureAudio('interactive');   // starts the context inside the gesture, before the permission prompt
      const stream = await navigator.mediaDevices.getUserMedia({ audio });
      if (S.stream) S.stream.getTracks().forEach(t => t.stop());
      S.stream = stream;
      await audioUp;
      connectMic();
      await listDevices();
      liveBtn.classList.add('hide'); stopBtn.classList.remove('hide');
      const st = stream.getAudioTracks()[0].getSettings();
      S.channels = Math.max(S.channels, st.channelCount || 0); fillChannels();
      if (A.ready) setStatus(`Listening to ${stream.getAudioTracks()[0].label || 'the input'}.`);
    } catch (e) {
      setStatus(e && e.name === 'NotAllowedError' ? 'The browser was not allowed to use the input. Allow it in the address bar and try again.' : `Could not open the input: ${e.message || e}`, true);
    }
  }
  function connectMic() {
    if (!A.ctx || !S.stream) return;
    if (A.mic) try { A.mic.disconnect(); } catch {}
    A.mic = A.ctx.createMediaStreamSource(S.stream);
    A.mic.connect(A.node);
    showLatency();
  }
  function stopLive() { liveWarn.classList.add('hide');
    if (S.stream) S.stream.getTracks().forEach(t => t.stop());
    S.stream = null; if (A.mic) try { A.mic.disconnect(); } catch {} A.mic = null;
    liveBtn.classList.remove('hide'); stopBtn.classList.add('hide');
    setStatus('Input stopped.');
  }
  async function listDevices() {
    const devs = (await navigator.mediaDevices.enumerateDevices()).filter(d => d.kind === 'audioinput');
    const cur = S.stream && S.stream.getAudioTracks()[0].getSettings().deviceId;
    devSel.replaceChildren(...devs.map((d, i) => h('option', { value: d.deviceId, text: d.label || `Input device ${i + 1}` })));
    if (cur) devSel.value = cur;
  }
  function fillChannels() {
    const n = Math.max(1, S.channels || 1);
    chSel.replaceChildren(...Array.from({ length: n }, (_, i) => h('option', { value: String(i), text: `Input ${i + 1}` })));
    if (S.channel >= n) { S.channel = 0; if (A.node) A.node.port.postMessage({ type: 'channel', channel: 0 }); }
    chSel.value = String(S.channel);
  }
  liveBtn.addEventListener('click', startLive);
  stopBtn.addEventListener('click', stopLive);
  devSel.addEventListener('change', () => { S.deviceId = devSel.value; if (S.stream) startLive(); });
  chSel.addEventListener('change', () => { S.channel = +chSel.value; if (A.node) A.node.port.postMessage({ type: 'channel', channel: S.channel }); });
  trim.addEventListener('input', () => { S.trim = +trim.value; trimTxt.textContent = `${S.trim >= 0 ? '+' : ''}${S.trim.toFixed(1)} dB`; if (A.node) A.node.port.postMessage({ type: 'trim', db: S.trim }); });
  if (navigator.mediaDevices && navigator.mediaDevices.addEventListener) navigator.mediaDevices.addEventListener('devicechange', () => { if (S.stream) listDevices(); });

  function latency() {
    if (!A.ctx) return null;
    const base = A.ctx.baseLatency || 0, out = A.ctx.outputLatency || 0;
    const tr = S.stream && S.stream.getAudioTracks()[0], inp = tr && tr.getSettings ? tr.getSettings().latency || 0 : 0;
    return { base, output: out, input: inp, total: base + out + inp };
  }
  function latencyShort() { const l = latency(); return l ? ` · latency ${(1000 * (l.base + l.output)).toFixed(1)} ms out` + (l.input ? ` + ${(1000 * l.input).toFixed(1)} ms in` : '') : ''; }
  function showLatency() {
    const l = latency(); if (!l) return;
    latTxt.textContent = `Latency the browser reports: ${(1000 * l.base).toFixed(1)} ms processing (baseLatency) + ${(1000 * l.output).toFixed(1)} ms output (outputLatency)`
      + (l.input ? ` + ${(1000 * l.input).toFixed(1)} ms input` : ', input not reported')
      + `: about ${(1000 * l.total).toFixed(0)} ms from string to ear, before your interface's own converters.` + (l.output ? '' : ' (Some browsers report 0 for the output part until audio has played.)');
  }
  setInterval(() => { if (A.ctx && S.source === 'live') showLatency(); }, 2000);

  // ---- monitor
  bypassBtn.addEventListener('click', () => { S.bypass = !S.bypass; bypassBtn.setAttribute('aria-pressed', String(S.bypass)); bypassBtn.textContent = S.bypass ? 'Bypassed: dry' : 'Bypass'; if (A.node) A.node.port.postMessage({ type: 'bypass', on: S.bypass }); });
  vol.addEventListener('input', () => { S.volume = +vol.value; volTxt.textContent = S.volume <= -40 ? 'off' : `${S.volume >= 0 ? '+' : ''}${S.volume.toFixed(1)} dB`; if (A.node) A.node.port.postMessage({ type: 'volume', db: S.volume <= -40 ? -120 : S.volume }); });
  presetSel.addEventListener('change', () => {
    const pr = (amp.presets || [])[+presetSel.value]; if (!pr) return;
    // as the plug-in does: the knobs a preset names move, the others stay where they are
    P.forEach((p, i) => { if (p.kind === 'input' || pr.values[p.key] === undefined) return; const v = snap(p, pr.values[p.key]); if (v !== values[i]) { controls[i].set(v); change(i, v); } });
    presetSel.value = '';
  });
  window.addEventListener('hashchange', () => { const old = values.slice(); fromHash(); values.forEach((v, i) => { if (v !== old[i] && controls[i]) { controls[i].set(v); send(i, v); } }); });

  // ---- for tests: the analysis tap, offline renders through the same worklet, and a main-thread benchmark
  const knobArray = knobs => { const v = P.map(p => p.def); for (const [k, x] of Object.entries(knobs || {})) v[byKey[k]] = x; return v; };
  const api = {
    amp, warm, get recordings() { return recs; }, get node() { return A.node; }, get buffered() { return B.active; }, get values() { return Object.fromEntries(P.map((p, i) => [p.key, values[i]])); },
    get state() { return { ...S, stream: !!S.stream, ready: A.ready, readyMs: A.readyMs, load: A.load, sampleRate: A.ctx && A.ctx.sampleRate, ctxState: A.ctx && A.ctx.state, hint: A.hint }; },
    latency,
    set(key, v) { const i = byKey[key]; v = snap(P[i], v); if (controls[i]) controls[i].set(v); change(i, v); },
    play, pause, setSource,
    seek(seconds) { S.pos = seconds; seek.value = String(seconds); if (A.node && A.recRate) A.node.port.postMessage({ type: 'transport', pos: Math.round(seconds * A.recRate) }); },
    bypass(on) { if (S.bypass !== !!on) bypassBtn.click(); },
    // RMS (dBFS) and peak of the input and output since the last reset
    stats(reset = true) {
      if (!A.node) return Promise.resolve(null);
      return new Promise(res => { A.statsWait = m => res({ n: m.n, seconds: m.n / A.ctx.sampleRate, inRms: dbfs(m.inRms), outRmsL: dbfs(m.outRmsL), outRmsR: dbfs(m.outRmsR), outPeak: dbfs(m.outPeak) }); A.node.port.postMessage({ type: 'stats', reset }); });
    },
    // render `seconds` of the recording from `start` through the worklet in an OfflineAudioContext at `rate`
    async renderOffline({ recording = S.recIndex, seconds = 10, start = 0, knobs = {}, bypass = false, rate = 48000, envelope = 0.5, samples = 0, sampleAt = 0 } = {}) {
      const d = await decode(recording, rate);
      const pre = 256, N = Math.round(seconds * rate);
      const c = new OfflineAudioContext({ numberOfChannels: 2, length: pre + N, sampleRate: rate });
      await c.audioWorklet.addModule(WORKLET);
      const vals = knobArray(knobs); if (!('involts' in knobs)) vals[byKey.involts] = (recs[recording] && recs[recording].involts) || P[byKey.involts].def;
      const node = new AudioWorkletNode(c, 'amp-player', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
        processorOptions: { wasm: await engineBytes(), values: vals, source: 'recording', recording: d.data.slice(Math.round(start * rate), Math.round(start * rate) + N), playing: true, loop: false, bypass } });
      node.connect(c.destination);
      let readyMs = 0;
      const ready = new Promise((res, rej) => { node.port.onmessage = e => { if (e.data.type === 'ready') { readyMs = e.data.ms; res(); } else if (e.data.type === 'error') rej(new Error(e.data.message)); }; });
      c.suspend(pre / rate).then(async () => { await ready; c.resume(); });
      const t0 = performance.now(), buf = await c.startRendering(), wall = performance.now() - t0;
      const L = buf.getChannelData(0).subarray(pre), R = buf.getChannelData(1).subarray(pre);
      let sl = 0, sr = 0, pk = 0; for (let i = 0; i < N; i++) { sl += L[i] * L[i]; sr += R[i] * R[i]; pk = Math.max(pk, Math.abs(L[i]), Math.abs(R[i])); }
      const env = []; const W = Math.round(envelope * rate);
      if (envelope) for (let i = 0; i + W <= N; i += W) { let s = 0; for (let k = i; k < i + W; k++) s += 0.5 * (L[k] * L[k] + R[k] * R[k]); env.push(+dbfs(Math.sqrt(s / W)).toFixed(2)); }
      return { file: d.file, rate, seconds, readyMs, wallMs: wall, rmsL: dbfs(Math.sqrt(sl / N)), rmsR: dbfs(Math.sqrt(sr / N)), peak: dbfs(pk), envelope: env,
        samples: samples ? { at: sampleAt, L: Array.from(L.subarray(sampleAt, sampleAt + samples)), R: Array.from(R.subarray(sampleAt, sampleAt + samples)) } : undefined };
    },
    // the engine on the main thread: seconds of processing per second of audio, at these knobs
    async benchmark({ recording = S.recIndex, seconds = 10, knobs = {}, rate = 48000 } = {}) {
      const d = await decode(recording, rate);
      const eng = await loadAmp(await engineBytes());
      let t = performance.now(); const a = eng.create(rate); const createMs = performance.now() - t;
      knobArray(knobs).forEach((v, i) => a.setParam(i, v)); a.prepare();
      const N = Math.round(seconds * rate), B = 128, l = new Float32Array(B), r = new Float32Array(B);
      t = performance.now();
      for (let i = 0; i + B <= N; i += B) a.process(d.data.subarray(i, i + B), l, r, B);
      const ms = performance.now() - t; a.destroy();
      return { createMs, realtime: ms / 1000 / seconds };
    },
  };
  window.ampPlayer = api;
  return api;
}

// brushed graphite: a matte black ground with fine horizontal grain (the plug-in's, from the same seed)
function grain(W, H, el) {
  const c = document.createElement('canvas'); c.width = W; c.height = H;
  const g = c.getContext('2d');
  const base = g.createLinearGradient(0, 0, 0, H);
  const cs = getComputedStyle(el);
  base.addColorStop(0, cs.getPropertyValue('--graphite').trim() || '#161618'); base.addColorStop(1, cs.getPropertyValue('--black').trim() || '#0a0a0b');
  g.fillStyle = base; g.fillRect(0, 0, W, H);
  let s = 1960; const rnd = () => ((s = (s * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
  for (let i = 0; i < 2600; i++) {
    const y = Math.floor(rnd() * H) + 0.5, x0 = rnd() * W, len = 40 + rnd() * 260;
    g.strokeStyle = `rgba(${rnd() < 0.5 ? '255,255,255' : '0,0,0'},${(0.012 + rnd() * 0.02).toFixed(3)})`;
    g.beginPath(); g.moveTo(x0, y); g.lineTo(x0 + len, y); g.stroke();
  }
  const sheen = g.createLinearGradient(0, 0, 0, 160);
  sheen.addColorStop(0, 'rgba(255,255,255,.035)'); sheen.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = sheen; g.fillRect(0, 0, W, 160);
  return c.toDataURL('image/png');
}

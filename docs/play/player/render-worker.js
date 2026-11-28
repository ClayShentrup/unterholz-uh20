// The buffered player's Web Worker: render.js on its own thread, its chunks sent straight to the playback worklet over
// a MessagePort (the worklet answers there with the playhead). Messages from the page:
//   { type: 'init', wasm, rate }   load the engine (its tables are built here), then post { type: 'ready', ms }
//   { type: 'port', port }   the playback worklet's end of a MessageChannel (again whenever the audio restarts)
//   { type: 'start', values, rec, loop, src }   { type: 'values', values }   { type: 'seek', src, rec? }   { type: 'loop', loop }
// SPDX-License-Identifier: AGPL-3.0-or-later
import { loadAmp } from './wasm-amp.js';
import { Renderer } from './render.js';

let r = null, port = null, running = false;
const now = () => (self.performance || Date).now();
function pump() {   // one chunk at a time, yielding between them so the page's and the worklet's messages get through
  if (running || !r) return;
  running = true;
  const go = () => { if (r.step()) { report(); setTimeout(go, 0); } else running = false; };
  setTimeout(go, 0);
}
let lastReport = 0;
function report() { const t = now(); if (t - lastReport > 250) { lastReport = t; self.postMessage({ type: 'speed', speed: r.speed() }); } }

self.onmessage = async e => {
  const m = e.data;
  try {
    if (m.type === 'init') {
      const t0 = now(), engine = await loadAmp(m.wasm);
      r = new Renderer(engine, m.rate, (msg, tr) => { if (port) port.postMessage(msg, tr || []); });
      self.postMessage({ type: 'ready', ms: now() - t0 });
    } else if (m.type === 'port') {
      port = m.port;
      port.onmessage = ev => { if (ev.data.type === 'played' && r) { r.setPlayed(ev.data.t); pump(); } };
    } else if (m.type === 'start') { r.start(m.values, m.rec, m.loop, m.src || 0); pump(); }
    else if (m.type === 'values') { r.setValues(m.values); pump(); }
    else if (m.type === 'seek') { r.seek(m.src, m.rec); pump(); }
    else if (m.type === 'loop') { r.setLoop(m.loop); pump(); }
  } catch (err) { self.postMessage({ type: 'error', message: String(err && err.message || err) }); }
};

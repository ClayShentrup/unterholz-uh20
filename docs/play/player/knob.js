// A rotary knob drawn as the plug-ins draw theirs (black glass, a fine knurl, a champagne rim and pointer, a thin lit
// arc), working with mouse, touch and keyboard. Drag up or right to turn it up (Shift for fine moves); double-click or
// double-tap for the default; arrows, Page Up/Down, Home and End from the keyboard. Values snap to the parameter's
// step; `skew` is JUCE's NormalisableRange skew, so the travel matches the plug-in's.
// SPDX-License-Identifier: AGPL-3.0-or-later

const SVG = 'http://www.w3.org/2000/svg';
const A0 = -135, A1 = 135;   // degrees from 12 o'clock, as the plug-ins' rotary range (1.25 pi to 2.75 pi)
let uid = 0;

const el = (name, attrs = {}, parent) => {
  const e = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (parent) parent.appendChild(e);
  return e;
};
const polar = (cx, cy, r, deg) => [cx + r * Math.sin(deg * Math.PI / 180), cy - r * Math.cos(deg * Math.PI / 180)];
const arcPath = (cx, cy, r, a, b) => {
  const [x0, y0] = polar(cx, cy, r, a), [x1, y1] = polar(cx, cy, r, b);
  return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${r} ${r} 0 ${b - a > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
};

export function toPos(p, v) { const t = (v - p.min) / (p.max - p.min); return Math.pow(Math.min(1, Math.max(0, t)), p.skew || 1); }
export function snap(p, v) {
  const s = Math.round((v - p.min) / p.step) * p.step + p.min;
  return Math.min(p.max, Math.max(p.min, +s.toFixed(6)));
}
export function fromPos(p, pos) {
  const t = Math.pow(Math.min(1, Math.max(0, pos)), 1 / (p.skew || 1));
  return snap(p, p.min + t * (p.max - p.min));
}
export const fmt = (p, v) => (p.fmt ? p.fmt(v) : String(v));

// p: the parameter (amp.js); onChange(value) when the user moves it; onFocus(p) / onBlur() for the help line
export function createKnob(p, value, { onChange, onFocus, onBlur }) {
  const d = p.size === 'big' ? 92 : 60, S = d + 20, c = S / 2, r = d / 2, id = `k${++uid}`;
  const root = document.createElement('div');
  root.className = 'knob' + (p.size === 'big' ? ' big' : '');
  root.style.setProperty('--kd', `${S}px`);

  const svg = el('svg', { viewBox: `0 0 ${S} ${S}`, class: 'knob-svg', 'aria-hidden': 'true' });
  const defs = el('defs', {}, svg);
  const g = el('radialGradient', { id: `${id}b`, cx: '0.3', cy: '0.15', r: '0.95' }, defs);
  el('stop', { offset: '0', 'stop-color': '#2c2c2f' }, g); el('stop', { offset: '1', 'stop-color': '#050506' }, g);
  el('path', { d: arcPath(c, c, r + 7, A0, A1), class: 'k-track' }, svg);
  const glow = el('path', { class: 'k-glow' }, svg), arc = el('path', { class: 'k-arc' }, svg);
  el('circle', { cx: c + 1, cy: c + 3, r, class: 'k-shadow' }, svg);
  el('circle', { cx: c, cy: c, r, fill: `url(#${id}b)` }, svg);
  let knurl = '';
  for (let i = 0; i < 48; i++) {
    const a = i / 48 * 360, [x0, y0] = polar(c, c, r * 0.86, a), [x1, y1] = polar(c, c, r * 0.98, a);
    knurl += `M${x0.toFixed(2)} ${y0.toFixed(2)}L${x1.toFixed(2)} ${y1.toFixed(2)}`;
  }
  el('path', { d: knurl, class: 'k-knurl' }, svg);
  el('circle', { cx: c, cy: c, r: r - 0.5, class: 'k-rim' }, svg);
  el('ellipse', { cx: c, cy: c - r * 0.525, rx: r * 0.55, ry: r * 0.275, class: 'k-shine' }, svg);
  const ptr = el('line', { class: 'k-ptr' }, svg);

  const face = document.createElement('div');
  face.className = 'knob-face';
  face.tabIndex = 0;
  face.setAttribute('role', 'slider');
  face.setAttribute('aria-label', p.label);
  face.setAttribute('aria-valuemin', p.min); face.setAttribute('aria-valuemax', p.max);
  face.appendChild(svg);
  const label = document.createElement('div'); label.className = 'knob-label'; label.textContent = p.label;
  const val = document.createElement('div'); val.className = 'knob-value';
  root.append(face, label, val);

  let v = value;
  const draw = () => {
    const pos = toPos(p, v), a = A0 + pos * (A1 - A0);
    const d2 = pos > 0.002 ? arcPath(c, c, r + 7, A0, a) : '';
    arc.setAttribute('d', d2); glow.setAttribute('d', d2);
    const [x0, y0] = polar(c, c, r * 0.42, a), [x1, y1] = polar(c, c, r * 0.86, a);
    ptr.setAttribute('x1', x0); ptr.setAttribute('y1', y0); ptr.setAttribute('x2', x1); ptr.setAttribute('y2', y1);
    const text = fmt(p, v);
    val.textContent = text;
    face.setAttribute('aria-valuenow', v); face.setAttribute('aria-valuetext', text);
  };
  const set = (nv, user) => {
    nv = Math.min(p.max, Math.max(p.min, nv));
    if (nv === v) return;
    v = nv; draw();
    if (user) onChange(v);
  };
  draw();

  // dragging: up or right turns it up, 250 px for the whole travel (JUCE's default), Shift for a tenth of that
  let drag = null, lastTap = 0, tapX = 0, tapY = 0;
  face.addEventListener('pointerdown', e => {
    if (e.button !== 0) return;
    e.preventDefault(); face.focus({ preventScroll: true });
    const t = performance.now();
    if (t - lastTap < 320 && Math.hypot(e.clientX - tapX, e.clientY - tapY) < 12) { set(p.def, true); lastTap = 0; return; }
    lastTap = t; tapX = e.clientX; tapY = e.clientY;
    drag = { x: e.clientX, y: e.clientY, pos: toPos(p, v), id: e.pointerId };
    face.setPointerCapture(e.pointerId); root.classList.add('active');
  });
  face.addEventListener('pointermove', e => {
    if (!drag || e.pointerId !== drag.id) return;
    const px = (e.clientX - drag.x) - (e.clientY - drag.y);
    if (Math.abs(px) > 3) lastTap = 0;   // a drag is not a tap
    set(fromPos(p, drag.pos + px / (e.shiftKey ? 2500 : 250)), true);
  });
  const end = e => { if (drag && e.pointerId === drag.id) { drag = null; root.classList.remove('active'); } };
  face.addEventListener('pointerup', end); face.addEventListener('pointercancel', end);
  face.addEventListener('dblclick', e => { e.preventDefault(); set(p.def, true); });
  face.addEventListener('keydown', e => {
    const big = (p.max - p.min) / 10, step = e.shiftKey ? big / 10 : p.step;
    const k = e.key;
    let nv = null;
    if (k === 'ArrowUp' || k === 'ArrowRight') nv = v + Math.max(step, p.step);
    else if (k === 'ArrowDown' || k === 'ArrowLeft') nv = v - Math.max(step, p.step);
    else if (k === 'PageUp') nv = v + big; else if (k === 'PageDown') nv = v - big;
    else if (k === 'Home') nv = p.min; else if (k === 'End') nv = p.max;
    else if (k === 'Delete' || k === 'Backspace') nv = p.def;
    if (nv === null) return;
    e.preventDefault();
    set(snap(p, nv), true);
  });
  for (const ev of ['pointerenter', 'focus']) face.addEventListener(ev, () => onFocus && onFocus(p));
  for (const ev of ['pointerleave', 'blur']) face.addEventListener(ev, () => onBlur && onBlur(p));

  return { el: root, set: nv => set(nv, false), get: () => v };
}

// a latching switch (the plug-ins' Bright): a pilot dot and a label
export function createToggle(p, value, { onChange, onFocus, onBlur }) {
  const b = document.createElement('button');
  b.type = 'button'; b.className = 'toggle'; b.setAttribute('aria-label', p.label);
  b.innerHTML = '<span class="dot"></span><span class="t"></span>';
  b.querySelector('.t').textContent = p.label;
  let v = value;
  const draw = () => { b.setAttribute('aria-pressed', v > 0.5 ? 'true' : 'false'); b.classList.toggle('on', v > 0.5); };
  b.addEventListener('click', () => { v = v > 0.5 ? 0 : 1; draw(); onChange(v); });
  for (const ev of ['pointerenter', 'focus']) b.addEventListener(ev, () => onFocus && onFocus(p));
  for (const ev of ['pointerleave', 'blur']) b.addEventListener(ev, () => onBlur && onBlur(p));
  draw();
  return { el: b, set: nv => { v = nv; draw(); }, get: () => v };
}

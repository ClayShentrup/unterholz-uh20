// The UH-20 in the browser: everything amp-specific the shared player (player/) needs. The engine binding is
// web/uh20_web.cpp; this file names its parameters (in the binding's id order), their ranges, defaults and labels
// exactly as the plug-in has them (source/PluginProcessor.cpp, source/PluginEditor.cpp), the panel layout, the wordmark
// and the colours. The panel's own look (flat, light, German labels with English under them, the REC / AMP pair) is
// uh20.css plus decorate() below; the player itself is unchanged.
// SPDX-License-Identifier: AGPL-3.0-or-later

const one = v => v.toFixed(1);

export const amp = {
  name: 'Unterholz',
  wasm: 'uh20.wasm',
  tagline: 'UH-20   JFET-VORVERSTÄRKER',
  footer: 'OPEN HARDWARE  /  ENTWURF 11.2026',
  site: '../',
  source: 'https://github.com/ClayShentrup/unterholz-uh20',
  // the plug-in's palette (source/PluginEditor.h: kPanel, kInk, kBand, kOrange, kRule, kMuted)
  colours: { black: '#dbd9cf', graphite: '#dbd9cf', steel: '#595955', ink: '#1a1a1a', brass: '#cccac0', mute: '#6b6b66', glow: '#e05c1f',
             band: '#141414', rule: '#b3b0a6' },
  fonts: { display: 'Helvetica Neue' },

  // In the binding's id order (the plug-in's parameter order). key: the name in the address and in tools/uh20_cli;
  // label: the panel's German name; sub: the English one under it; def: the plug-in's default. Every knob is linear,
  // 0 to 10 in steps of 0.1, as in the plug-in. kind: knob, toggle, or input (not on the panel).
  params: [
    { key: 'volume', cli: 'volume', label: 'LAUT', sub: 'volume', min: 0, max: 10, step: 0.1, def: 5, fmt: one,
      help: 'LAUT (volume): preamp gain, after the tone stack. Clean up to about 5, edge of breakup at 6-7, crunch above.' },
    { key: 'drive', cli: 'drive', label: 'ANTRIEB', sub: 'drive', min: 0, max: 10, step: 0.1, def: 0, fmt: one,
      help: 'ANTRIEB (drive): one more J201 gain stage ahead of the tone stack. 0 = switched out (the clean amp); 2 = edge of breakup; 5 = crunch; 8-10 = singing lead.' },
    { key: 'treble', cli: 'treble', label: 'HÖHEN', sub: 'treble', min: 0, max: 10, step: 0.1, def: 3, fmt: one,
      help: 'HÖHEN (treble): top end. The tone stack comes before the last gain stages, so more treble also drives them harder.' },
    { key: 'middle', cli: 'middle', label: 'MITTEN', sub: 'middle', min: 0, max: 10, step: 0.1, def: 7, fmt: one,
      help: 'MITTEN (middle): midrange body. Low = the scooped Mark-style stack; high = fuller, and more mids pushed into the gain stages.' },
    { key: 'bass', cli: 'bass', label: 'TIEFEN', sub: 'bass', min: 0, max: 10, step: 0.1, def: 5, fmt: one,
      help: 'TIEFEN (bass): low end. With high LAUT or ANTRIEB keep it at 5 or below; above that the low end gets loose.' },
    { key: 'presence', cli: 'presence', label: 'PRÄSENZ', sub: 'presence', min: 0, max: 10, step: 0.1, def: 5, fmt: one,
      help: "PRÄSENZ (presence): upper-mid bite from the power stage's feedback loop. 0 = smooth and dark, 10 = cutting." },
    { key: 'clip', cli: 'clip', label: 'CLIP', sub: 'LED clip', accent: true, min: 0, max: 10, step: 0.1, def: 0, fmt: one,
      help: "CLIP: LEDs clip the peaks: flatter tops, more sustain, thicker (asymmetric). The diode-clipping idea of Marshall's JCM800 2205/2210 (1982) and Silver Jubilee 2555 (1987, LEDs). 0 = off." },
    { key: 'sag', cli: 'sag', label: 'DURCHHANG', sub: 'sag', accent: true, min: 0, max: 10, step: 0.1, def: 0, fmt: one,
      help: 'DURCHHANG (sag): the power section gives way under hard playing. Higher = hard notes and chords squash (up to 3.5 dB) and swell back as you ease off; soft playing is left alone.' },
    { key: 'master', cli: 'master', label: 'SUMME', sub: 'master', min: 0, max: 10, step: 0.1, def: 6.5, fmt: one,
      help: 'SUMME (master): level into the power stage. 6.5 = the studio clean tone; higher = more power-amp crunch and compression.' },
    { key: 'bright', cli: 'bright', label: 'HELL', sub: 'bright', kind: 'toggle', min: 0, max: 1, step: 1, def: 0,
      help: 'HELL (bright): treble bleed across LAUT. Adds sparkle at low LAUT settings and fades out as LAUT goes up.' },
    { key: 'shift', cli: 'shift', label: 'TIEF', sub: 'shift', kind: 'toggle', min: 0, max: 1, step: 1, def: 0,
      help: 'TIEF (bass shift): lets more low end into the first stage. Fatter, warmer; a little looser.' },
    // the plug-in's Output choice (0 = REC, 1 = AMP), drawn as its pair of switches by decorate()
    { key: 'output', cli: 'output', label: 'AMP', sub: 'no cab', kind: 'toggle', min: 0, max: 1, step: 1, def: 0,
      help: 'AMP: no cabinet. For a real amp and speaker, or your own cabinet impulse response after the plugin.',
      helpRec: 'REC: the analog cab-sim output (the hardware\'s REC jack). For recording straight into a DAW.' },
    { key: 'involts', cli: 'involts', label: 'input volts', kind: 'input', min: 0.1, max: 4, step: 0.01, def: 1.6 },
    // Output trim: a plug-in parameter with no knob on its panel (the monitor's Volume does that job here). The plug-in's
    // output runs hot (the built-in lead peaks at +5 dBFS at the defaults and +11 at ANTRIEB 8), which a DAW's floating-point
    // mix takes but a browser's output clips, so the player sits 14 dB lower: peaks stay under 0 dBFS at every setting, and
    // the level is about the other amps' players'. It changes only the level, not the tone.
    { key: 'trim', cli: 'trim', label: 'trim', kind: 'input', min: -24, max: 24, step: 0.1, def: -14 },
  ],

  // the plug-in's panel: one row of knobs in its three sections (the switches sit under them)
  layout: [
    [ { title: 'VORSTUFE / preamp', keys: ['volume', 'drive', 'treble', 'middle', 'bass', 'presence'] },
      { title: 'CHARAKTER', keys: ['clip', 'sag'] }, { title: 'AUSGANG', keys: ['master'], align: 'end' } ],
  ],
  // the plug-in has no presets
  presets: [],
  idleHelp: 'Point at any knob or switch to see what it does.',

  // live input: the engine reads 0 dBFS as this many volts at the guitar jack (the plug-in's default calibration)
  input: {
    volts: 1.6,
    aimDb: -6,
    note: 'The UH-20 reads full scale (0 dBFS) as 1.6 volts from the guitar, as the plug-in does by default (its EINGANG setting). A passive guitar played hard peaks at roughly 0.5 to 1 volt, about -6 dBFS here, so set the input trim until your hardest playing peaks near -6 on the input meter. Much hotter and the amp hears a hotter guitar than yours; much quieter and it cleans up.',
  },
};

// The plug-in's panel details the shared player doesn't draw: the English name under each German one, Clip and Sag's
// orange pointers, the switches under the knobs, and the Output choice as its REC / AMP pair.
export function decorate(root, player) {
  const P = amp.params;
  const NS = 'http://www.w3.org/2000/svg';
  const svgEl = (name, attrs) => { const e = document.createElementNS(NS, name); for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v); return e; };
  for (const face of root.querySelectorAll('.knob-face')) {
    const p = P.find(q => q.label === face.getAttribute('aria-label'));
    if (!p) continue;
    const k = face.closest('.knob');
    k.dataset.key = p.key;
    if (p.accent) k.classList.add('accent');
    const sub = document.createElement('div'); sub.className = 'knob-sub'; sub.textContent = p.sub;
    k.querySelector('.knob-label').after(sub);
    // the plug-in's knob (source/PluginEditor.cpp, KnobLook): eleven ticks round a flat black cap, and a ring inside it.
    // Its cap is 27 px in radius; the shared knob's is 30, so its 5-9 px ticks and 6 px ring inset scale by 30 / 27.
    const svg = face.querySelector('svg'), ptr = svg && svg.querySelector('.k-ptr');
    if (!ptr) continue;
    const c = svg.viewBox.baseVal.width / 2, r = 30, s = r / 27;
    let d = '';
    for (let t = 0; t <= 10; t++) {
      const a = (-135 + 27 * t) * Math.PI / 180, x = Math.sin(a), y = -Math.cos(a);
      d += `M${(c + x * (r + 5 * s)).toFixed(2)} ${(c + y * (r + 5 * s)).toFixed(2)}L${(c + x * (r + 9 * s)).toFixed(2)} ${(c + y * (r + 9 * s)).toFixed(2)}`;
    }
    svg.insertBefore(svgEl('path', { d, class: 'k-ticks' }), svg.firstChild);
    svg.insertBefore(svgEl('circle', { cx: c, cy: c, r: (r - 6 * s).toFixed(2), class: 'k-ring' }), ptr);
  }
  // the header band: the wordmark on the left, the model line and the draft line on the right, the orange bar at the end
  const head = root.querySelector('.phead'), mark = root.querySelector('.phead .mark'), cap = root.querySelector('.pfoot .cap'), jewel = root.querySelector('.phead .jewel');
  if (head && mark) {
    const side = document.createElement('div'); side.className = 'side';
    const tag = mark.querySelector('.tag'); if (tag) side.append(tag);
    if (cap) { cap.classList.add('draft'); side.append(cap); }
    head.append(side);
    if (jewel) head.append(jewel);
  }
  const sw = root.querySelector('.phead .sw'), rows = root.querySelector('.rows');
  if (sw && rows) { sw.classList.add('switches'); rows.after(sw); }
  const help = root.querySelector('.help');
  const show = t => { help.textContent = t || amp.idleHelp; help.classList.toggle('on', !!t); };
  for (const b of root.querySelectorAll('.toggle')) {
    const p = P.find(q => q.label === b.getAttribute('aria-label'));
    if (!p) continue;
    b.dataset.key = p.key;
    const s = document.createElement('span'); s.className = 'sub'; s.textContent = p.sub; b.append(s);
    if (p.key !== 'output') continue;
    // REC and AMP: one choice, two switches (REC on = AMP off); the original toggle stays as AMP
    const rec = document.createElement('button');
    rec.type = 'button'; rec.className = 'toggle rec'; rec.setAttribute('aria-label', 'REC');
    rec.innerHTML = '<span class="dot"></span><span class="t">REC</span><span class="sub">analog cab</span>';
    const draw = () => { const on = player.values.output < 0.5; rec.classList.toggle('on', on); rec.setAttribute('aria-pressed', String(on)); };
    rec.addEventListener('click', () => { player.set('output', 0); draw(); });
    // AMP only switches AMP on (as the plug-in's does), instead of the shared toggle's on/off
    b.addEventListener('click', e => { e.stopImmediatePropagation(); player.set('output', 1); draw(); }, true);
    for (const ev of ['pointerenter', 'focus']) rec.addEventListener(ev, () => show(p.helpRec));
    for (const ev of ['pointerleave', 'blur']) rec.addEventListener(ev, () => show(null));
    b.before(rec);
    addEventListener('hashchange', draw);
    draw();
  }
  for (const el of root.querySelectorAll('.knob-face, .toggle'))
    for (const ev of ['pointerenter', 'focus']) el.addEventListener(ev, () => help.classList.add('on'));
  for (const el of root.querySelectorAll('.knob-face, .toggle'))
    for (const ev of ['pointerleave', 'blur']) el.addEventListener(ev, () => help.classList.remove('on'));
  // no presets in the plug-in: no preset menu
  if (!(amp.presets || []).length) { const ps = root.querySelector('select[aria-label="Preset"]'); if (ps) ps.classList.add('hide'); }
  root.classList.add('decorated');
}

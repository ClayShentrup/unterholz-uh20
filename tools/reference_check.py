#!/usr/bin/env python3
"""reference_check: the engine against the committed reference set (tests/reference/), rendered once by the reference
engine (the plug-in's engine built with -DUH20_REFERENCE=1: 8x oversampling and every original numerical path).

Like comparing an MP3 with its WAV: a faster engine may differ from the reference in the waveform, but not audibly. A
change passes when, for every signal and setting:
  tone       each third-octave band from 50 Hz to 8 kHz is within 0.5 dB of the reference, in each channel
  loudness   integrated loudness is within 0.2 LU
  aliasing   on 880 and 1318 Hz tones through the hot corners: within amp_sanity's limit (-50 dB, the 'all gain 10' corner
             exempt there as in amp_sanity) and no more than 3 dB above the reference, by amp_sanity's measure and by an
             exact one (every product of a steady tone lands on a bin of a 1 s FFT: whatever is not a harmonic is aliasing;
             amp_sanity's +/-250 Hz masks hide aliases that happen to fall near a harmonic, which they do at some
             oversampling ratios)
  behaviour  amp_sanity's touch and sweep checks: 0 FAIL
Waveform residuals against the reference engine (--ref-cli) are reported for information; they are not a gate.

Usage:
  reference_check.py --cli build/uh20_cli [--ref-cli build/uh20_cli_ref] [--quick] [--json out.json]
  reference_check.py --cli build/uh20_cli_ref --tight     the reference engine reproduces its own set (CI)
  reference_check.py --write --cli build/uh20_cli_ref     render the set again (only when the reference is meant to change)
The checker is shared by the Unterholz, Hanwell and Rindu amps; the UH-20 parts are DEFAULTS, SETTINGS, DIS, SPEC and the
renderer (uh20_cli reads and writes raw float32 mono).
Needs numpy, scipy, soundfile, pyloudnorm. Exit status: the number of failed checks.
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import argparse, json, os, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np, soundfile as sf, pyloudnorm as pyln

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import amp_sanity  # noqa: E402

REF = os.path.join(ROOT, "tests", "reference")
SR = 48000
TOL = {"band_db": 0.5, "lufs": 0.2, "alias_rise_db": 3.0, "alias_limit_db": amp_sanity.LIMITS["alias_db"]}
TIGHT = {"band_db": 0.01, "lufs": 0.01, "alias_rise_db": 0.1, "alias_limit_db": amp_sanity.LIMITS["alias_db"]}
BANDS = 1000 * 2.0 ** (np.arange(-13, 10) / 3)   # third-octave centres, 50 Hz .. 8 kHz

# the plug-in's defaults (source/PluginProcessor.cpp, uh20::Params); every render names every knob
DEFAULTS = {"volume": 5, "treble": 3, "middle": 7, "bass": 5, "presence": 5, "master": 6.5, "sag": 0, "clip": 0, "drive": 0,
            "bright": 0, "shift": 0, "output": 0, "trim": 0, "involts": 1.6}
BASE = {"treble": 4, "presence": 6}   # the sample set's and the sanity spec's base: volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5
SETTINGS = {
    "default": {},
    "clean (volume 5, treble 3, middle 7, bass 5, presence 6, master 6.5)": {"presence": 6},
    "clean, AMP output": {"presence": 6, "output": 1},
    "edge (drive 4)": dict(BASE, drive=4),
    "crunch (volume 7, master 7.5)": dict(BASE, volume=7, master=7.5),
    "crunch, AMP output": dict(BASE, volume=7, master=7.5, output=1),
    "drive 2": dict(BASE, drive=2),
    "drive 6": dict(BASE, drive=6),
    "drive 8": dict(BASE, drive=8),
    "drive 8, AMP output": dict(BASE, drive=8, output=1),
    "drive 8, master 9": {"drive": 8, "master": 9},
    "drive 10": dict(BASE, drive=10),
    "clip 5": dict(BASE, clip=5),
    "drive 6, clip 5": dict(BASE, drive=6, clip=5),
    "sag 7 (volume 8, master 8)": dict(BASE, volume=8, master=8, sag=7),
    "master 10": dict(BASE, master=10),
    "master 10, volume 8, sag 10": dict(BASE, master=10, volume=8, sag=10),
    "lead, sag 10 (drive 6, clip 3, master 8)": {"drive": 6, "clip": 3, "treble": 5, "middle": 6, "bass": 4, "presence": 6, "master": 8, "sag": 10},
    "bright and shift": dict(BASE, bright=1, shift=1),
    "bright and shift, AMP output": dict(BASE, bright=1, shift=1, output=1),
    "dark clean (shift)": {"volume": 4, "treble": 2, "middle": 5, "bass": 6, "presence": 3, "shift": 1},
    "bright clean (bright)": {"volume": 4, "treble": 7, "middle": 3, "bass": 5, "presence": 7, "bright": 1},
    "drive 10, clip 8, sag 8": dict(BASE, drive=10, clip=8, sag=8),
}
SPEC = os.path.join(ROOT, "tests", "sanity", "uh20.json")
DIS = ["the-puzzle-remains-lead", "shape-of-the-almost-rhythm", "shape-of-the-almost-chorus-lead", "red-rocks-riff"]


def plucks(seconds=2.0, peak=0.5):
    """tests/uh20_tests.cpp's plucks(): Karplus-Strong on E2 A3 E4 B4 E5, the same generator, sample for sample."""
    n = int(seconds * SR); y = np.zeros(n); r = 12345
    for k, f in enumerate([82.41, 220.0, 329.63, 493.88, 659.26]):
        start, ln = int(k * 0.06 * SR), int(round(SR / f)); buf = np.empty(ln)
        for i in range(ln):
            r = (r * 1664525 + 1013904223) & 0xFFFFFFFF; buf[i] = (r >> 8) / 16777216.0 - 0.5
        p = 0
        for i in range(start, n):
            q = (p + 1) % ln; v = buf[p]; buf[p] = 0.4985 * (buf[p] + buf[q]); p = q; y[i] += v
    return (y / np.abs(y).max() * peak).astype(np.float32)


def signals():
    s = {f"DI {d}": sf.read(os.path.join(REF, "signals", d + ".flac"), dtype="float32")[0] for d in DIS}
    s["synthetic plucks"] = plucks()
    s["synthetic guitar (amp_sanity)"] = amp_sanity.guitar_di().astype(np.float32)
    return s


class Renderer:
    def __init__(self, cli):
        self.cli = os.path.abspath(os.path.expanduser(cli)); self.tmp = tempfile.mkdtemp(prefix="refcheck_"); self.n = 0
    def __call__(self, x, knobs):
        k = dict(DEFAULTS); k.update(knobs); self.n += 1
        i = os.path.join(self.tmp, f"{self.n}_{np.random.randint(1 << 30)}")
        np.concatenate([x, np.zeros(SR, np.float32)]).astype(np.float32).tofile(i + ".in")
        subprocess.run([self.cli, i + ".in", i + ".out", str(SR)] + [f"{a}={b}" for a, b in k.items()], check=True, capture_output=True)
        y = np.fromfile(i + ".out", np.float32).astype(float); os.remove(i + ".in"); os.remove(i + ".out")
        return y[:, None]   # one channel (the amp is mono): the input plus a 1 s tail


def bands(y):
    """third-octave band levels (dB, absolute), per channel"""
    w = np.hanning(len(y))[:, None]; Y = np.abs(np.fft.rfft(y * w, axis=0)) ** 2; fr = np.fft.rfftfreq(len(y), 1 / SR)
    out = []
    for c in BANDS:
        m = (fr >= c / 2 ** (1 / 6)) & (fr < c * 2 ** (1 / 6)); out.append(10 * np.log10(Y[m].sum(0) / len(y) ** 2 + 1e-30))
    return np.array(out).T   # [channel][band]


def alias_exact(y, f):
    """share of a steady tone's output that is not at a harmonic of f: a 1 s rectangular window on an integer-Hz tone puts
    every harmonic and every alias exactly on a bin, so nothing leaks and nothing hides"""
    seg = y[SR:2 * SR].sum(1) if y.ndim > 1 else y[SR:2 * SR]
    Y = np.abs(np.fft.rfft(seg)) ** 2; k = np.arange(len(Y)); use = k >= 15
    return float(10 * np.log10(Y[use & (k % f != 0)].sum() / Y[use].sum() + 1e-30))


def alias_cases(spec):
    base = dict(spec["fixed"]); base.update(spec["base"])
    return {n: dict(base, **k) for n, k in amp_sanity.hot_corners(amp_sanity.Amp(spec))}


def measure(render, pool, ref_render=None):
    sig = signals(); spec = json.load(open(SPEC))
    jobs = [(s, n) for s in sig for n in SETTINGS]
    meter = pyln.Meter(SR)
    def one(j):
        y = render(sig[j[0]], SETTINGS[j[1]])
        m = {"bands": np.round(bands(y), 4).tolist(), "lufs": round(float(meter.integrated_loudness(y)), 4)}
        if ref_render:   # the waveform against the reference engine's, for information
            r = ref_render(sig[j[0]], SETTINGS[j[1]]); m["residual"] = float(10 * np.log10(((y - r) ** 2).sum() / ((r ** 2).sum() + 1e-30) + 1e-30))
        return j, m
    tone = {}
    for j, m in pool.map(one, jobs):
        tone.setdefault(j[0], {})[j[1]] = m
    al = {}
    ac = alias_cases(spec)
    def two(j):
        n, f = j; y = render(amp_sanity.sine(f, 0.2).astype(np.float32), ac[n])
        return j, amp_sanity.harmonic_shares(y.mean(1), f)[1], alias_exact(y, f)
    for (n, f), s, e in pool.map(two, [(n, f) for n in ac for f in (880, 1318)]):
        al.setdefault(n, {})[str(f)] = {"sanity": round(float(s), 3), "exact": round(float(e), 3)}
    return tone, al


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--cli", required=True); ap.add_argument("--ref-cli")
    ap.add_argument("--write", action="store_true"); ap.add_argument("--tight", action="store_true")
    ap.add_argument("--quick", action="store_true", help="amp_sanity's quick sweep"); ap.add_argument("--no-sanity", action="store_true")
    ap.add_argument("--json"); ap.add_argument("--jobs", type=int, default=os.cpu_count())
    a = ap.parse_args(); t0 = time.time()
    with ThreadPoolExecutor(a.jobs) as pool:
        tone, al = measure(Renderer(a.cli), pool, Renderer(a.ref_cli) if a.ref_cli else None)
        if a.write:
            json.dump({"about": "Measured from the reference engine (-DUH20_REFERENCE=1, 8x) by tools/reference_check.py --write. "
                       "bands: third-octave levels in dB, 50 Hz - 8 kHz (centres listed), per channel; lufs: integrated loudness; "
                       "alias: off-harmonic share in dB of 880/1318 Hz tones at amp_sanity's hot corners, its measure and the exact one.",
                       "band_centres": np.round(BANDS, 2).tolist(), "settings": SETTINGS, "defaults": DEFAULTS,
                       "tone": tone, "alias": al}, open(os.path.join(REF, "reference.json"), "w"), indent=0)
            print(f"wrote tests/reference/reference.json ({sum(len(v) for v in tone.values())} renders, {time.time() - t0:.0f} s)"); return 0
        ref = json.load(open(os.path.join(REF, "reference.json"))); tol = TIGHT if a.tight else TOL
        if ref["settings"] != SETTINGS or ref["defaults"] != DEFAULTS: print("FAIL the settings differ from the ones the reference was rendered at"); return 1
        res = {(s, n): tone[s][n]["residual"] for s in tone for n in tone[s] if "residual" in tone[s][n]}
        sanity = None
        if not a.no_sanity:
            spec = json.load(open(SPEC)); amp = amp_sanity.Amp(spec, a.cli); R = amp_sanity.Report()
            di = amp_sanity.guitar_di()
            amp_sanity.t_touch(amp, R, pool, a.quick); amp_sanity.t_sweep(amp, R, pool, a.quick, di); sanity = R
    fails = 0; rows = []
    print(f"\ntone and loudness (limits: bands {tol['band_db']} dB, loudness {tol['lufs']} LU)" + ("; waveform residual vs the reference engine, for information" if res else ""))
    worst_b = worst_l = 0
    for s in ref["tone"]:
        for n in ref["tone"][s]:
            r = ref["tone"][s][n]; m = tone[s][n]
            db = np.abs(np.array(m["bands"]) - np.array(r["bands"])); i = np.unravel_index(np.argmax(db), db.shape)
            dl = abs(m["lufs"] - r["lufs"]); ok = db.max() <= tol["band_db"] and dl <= tol["lufs"]; fails += not ok
            worst_b = max(worst_b, db.max()); worst_l = max(worst_l, dl)
            extra = f", residual {res[(s, n)]:.1f} dB" if (s, n) in res else ""
            line = f"  {'PASS' if ok else 'FAIL'}  {s} / {n}: bands within {db.max():.3f} dB (worst {BANDS[i[1]]:.0f} Hz ch{i[0] + 1}), loudness {m['lufs'] - r['lufs']:+.3f} LU{extra}"
            print(line); rows.append({"signal": s, "setting": n, "ok": bool(ok), "band_db": float(db.max()), "lufs": m["lufs"] - r["lufs"], "residual_db": res.get((s, n))})
    print(f"\naliasing (limit {tol['alias_limit_db']} dB, at most {tol['alias_rise_db']} dB above the reference; exact measure and amp_sanity's)")
    for n in ref["alias"]:
        for f in ref["alias"][n]:
            r = ref["alias"][n][f]; m = al[n][f]
            rise = max(m["sanity"] - r["sanity"], m["exact"] - r["exact"])
            lim_ok = n == "all gain 10" or m["sanity"] < tol["alias_limit_db"]
            ok = rise <= tol["alias_rise_db"] and lim_ok; fails += not ok
            print(f"  {'PASS' if ok else 'FAIL'}  {n}, {f} Hz: exact {m['exact']:.1f} dB (ref {r['exact']:.1f}), amp_sanity {m['sanity']:.1f} dB (ref {r['sanity']:.1f})")
            rows.append({"alias": n, "f": f, "ok": bool(ok), "exact": m["exact"], "sanity": m["sanity"], "ref_exact": r["exact"], "ref_sanity": r["sanity"]})
    if sanity is not None: fails += sanity.fails; print(f"\namp_sanity touch and sweep: {sanity.fails} FAIL")
    if res: v = list(res.values()); print(f"waveform residual vs the reference engine: median {np.median(v):.1f} dB, worst {max(v):.1f} dB (information)")
    print(f"\nworst band {worst_b:.3f} dB, worst loudness {worst_l:.3f} LU; {fails} failed; {time.time() - t0:.0f} s")
    if a.json: json.dump({"tolerances": tol, "fails": fails, "rows": rows}, open(a.json, "w"), indent=1)
    return min(fails, 100)


if __name__ == "__main__":
    sys.exit(main())

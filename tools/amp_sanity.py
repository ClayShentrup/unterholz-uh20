#!/usr/bin/env python3
"""amp_sanity: standard sanity checks for a guitar amp plugin, run through its offline command-line renderer.

Usage:  amp_sanity.py <spec.json> [--cli PATH] [--quick] [--only TEST,...] [--json out.json]

The spec names the renderer's calling convention, its knobs, a base setting and which knobs add gain. Every test signal is
synthetic (plucked strings with a realistic pickup noise floor, sines), so the suite runs anywhere, CI included, with no
recordings. Each check prints PASS or FAIL with its numbers; the exit status is the number of failed checks.

What each check catches (all were real bugs in these amps):
  stability    silence in -> silence out at every corner and random settings: self-oscillation, NaN/Inf, DC, ringing tails
  sweep        each gain knob 0..10: blocking (dropouts as the knob rises), level collapse, compression running backwards,
               hiss and hum brought up into the playing
  touch        sines at soft and hard picking levels through the hot corners and presets: the amp must clean up when you
               play softly (a fuzz that ignores touch failed this), and soft-picking distortion must stay under the spec's
               ceiling for what the amp is meant to be
  aliasing     the top notes of a guitar (880, 1318 Hz) through the hot corners: energy off the harmonic series
  continuity   every knob in half steps: sudden level jumps (switch thresholds, taper bugs)
  alive        every knob 0 vs 10 changes the sound (dead or miswired knobs)
  rates        44.1 / 48 / 96 kHz give the same level and spectrum (sample-rate-dependent filters or solvers)
  channels     each output channel measured on its own, at the hot corners with every effect (fx_knobs) at several
               levels: no channel may drop out against the same setting without effects, and the two channels may not
               differ by more than 20 dB while playing (a one-channel collapse hides in a mono sum); an effect may lag the dry by one 10 ms frame, since a delayed
               wet-only channel's note onsets land in the next frame. Effects listed in
               fx_dynamic (a volume swell) change the level on purpose and are only covered by the stability check
"""
import json, os, subprocess, sys, tempfile, time, argparse
import numpy as np
from concurrent.futures import ThreadPoolExecutor

SR = 48000
LIMITS = {
    "silence_dbfs": -80.0,      # rms with zero input after 1 s of settling
    "tail_dbfs": -70.0,         # rms of the last 0.5 s, 2.5 s after the playing stops (effects off)
    "dc_dbfs": -40.0,
    "level_dip_db": 1.5,        # loud-frame level may not fall by more than this as a gain knob rises
    "dropout_frac": 0.005,      # loud 10 ms frames more than 6 dB under the previous step's envelope
    "dropout_event_ms": 40,
    "crest_rise_db": 1.0,       # crest factor may not rise by more than this from step 4 of a gain knob upward
    "noise_db": -30.0,          # between-note noise vs loud playing, at every step
    "touch_db": 3.0,            # distortion must fall by at least this much from hard (-14 dBFS) to soft (-30 dBFS) picking
    "alias_db": -50.0,          # energy off the harmonic series (beyond +/-250 Hz of each harmonic), share of total, cab on
    "jump_db": 3.0,             # loudness change between adjacent half steps of any knob
    "jump_gain_db": 6.0,        # the same for gain/volume-type knobs (steep tapers are normal there)
    "alive_db": 1.0,            # a knob 0 vs 10 must move some third-octave band by this much
    "rate_level_db": 0.75,
    "rate_band_db": 2.0,        # max third-octave difference 60 Hz - 8 kHz between rates
}

# ---------------------------------------------------------------- test signals
def pluck(f, dur, amp, rng, bright=0.5, mute=False):
    """Karplus-Strong string, picked noise burst, decays naturally; mute shortens the decay (palm mute)."""
    n = int(dur * SR); N = max(2, int(round(SR / f)))
    buf = rng.uniform(-1, 1, N); buf = np.convolve(buf, [bright, 1 - bright], "same")
    out = np.empty(n); idx = 0; loss = 0.990 if mute else 0.9996
    for i in range(n):
        v = buf[idx]; nxt = buf[(idx + 1) % N]; buf[idx] = loss * 0.5 * (v + nxt); out[i] = v; idx = (idx + 1) % N
    out *= np.minimum(1, np.arange(n) / (0.002 * SR))
    return amp * out / (np.abs(out).max() + 1e-12)

def guitar_di(seed=1):
    """About 20 s of guitar-like DI at realistic levels (peaks -6 dBFS hard, -24 soft), with gaps, a long decay to silence and
    a pickup noise floor (white hiss -78 dBFS rms, 60 Hz hum and harmonics -82 dBFS)."""
    rng = np.random.default_rng(seed)
    E2 = 82.41; st = lambda k: E2 * 2 ** (k / 12)
    parts = []
    def add(sig, gap=0.0): parts.append(sig); parts.append(np.zeros(int(gap * SR)))
    for k, a in [(0, .5), (7, .5), (12, .5), (19, .4), (24, .4), (31, .35), (36, .3)]:      # single notes, E2..E5
        add(pluck(st(k), 0.55, a, rng), 0.05)
    add(np.zeros(int(0.8 * SR)))
    for root in [0, 5, 7, 3]:                                                                   # power chords, hard
        add(sum(pluck(st(root + d), 0.9, .45, rng) for d in (0, 7, 12)), 0.05)
    add(np.zeros(int(0.8 * SR)))
    for k in range(8):                                                                          # palm-muted chugs
        add(sum(pluck(st(d), 0.18, .5, rng, mute=True) for d in (0, 7)), 0.04)
    add(np.zeros(int(0.8 * SR)))
    for chord in [(0, 7, 12, 16, 19, 24), (5, 12, 17, 21, 24, 29)]:                             # strummed open chords
        s = np.zeros(int(1.6 * SR))
        for j, d in enumerate(chord):
            p = pluck(st(d), 1.6 - j * 0.012, .3, rng); s[int(j * 0.012 * SR):][:len(p)] += p
        add(s, 0.05)
    for k, a in [(12, .06), (19, .06), (24, .05)]:                                              # soft picking
        add(pluck(st(k), 0.6, a, rng), 0.05)
    add(pluck(st(19), 4.0, .5, rng))                                                            # long note into silence
    add(np.zeros(int(1.5 * SR)))
    x = np.concatenate(parts)
    t = np.arange(len(x)) / SR
    noise = rng.normal(0, 10 ** (-78 / 20), len(x)) + 10 ** (-82 / 20) * np.sqrt(2) * sum(
        np.sin(2 * np.pi * 60 * h * t) / h for h in (1, 2, 3, 5))
    return x + noise

def sine(f, amp, dur=2.0, sr=SR):
    t = np.arange(int(dur * sr)) / sr
    return amp * np.sin(2 * np.pi * f * t) * np.minimum(1, t / 0.02)

# ---------------------------------------------------------------- running the renderer
class Amp:
    def __init__(self, spec, cli=None):
        self.s = spec; self.cli = os.path.expanduser(cli or spec["cli"]); self.tmp = tempfile.mkdtemp(prefix="ampsan_")
        self.n = 0
    def args(self, knobs, cab=True):
        k = dict(self.s.get("fixed", {})); k.update(self.s["base"]); k.update(knobs)
        k.update(self.s["cab_on"] if cab else self.s["cab_off"])
        return [f"{a}={b}" for a, b in k.items()]
    def render(self, x, knobs, cab=True, sr=SR, tail=0.0, keep_channels=False):
        x = np.concatenate([x, np.zeros(int(tail * sr))]).astype(np.float32)
        self.n += 1; i = f"{self.tmp}/{os.getpid()}_{self.n}_{np.random.randint(1e9)}"
        if self.s["io"] == "f32":
            x.tofile(i + ".in"); subprocess.run([self.cli, i + ".in", i + ".out", str(sr)] + self.args(knobs, cab), check=True,
                                                capture_output=True)
            y = np.fromfile(i + ".out", np.float32).astype(float)
        else:
            import soundfile as sf
            sf.write(i + ".in.wav", x, sr, subtype="FLOAT")
            subprocess.run([self.cli, i + ".in.wav", i + ".out.wav"] + self.args(knobs, cab), check=True, capture_output=True)
            y, _ = sf.read(i + ".out.wav")
            if y.ndim == 2 and keep_channels: y = y
            else: y = y if y.ndim == 1 else y.mean(1)
        for f in os.listdir(self.tmp):
            if f.startswith(os.path.basename(i)): os.remove(os.path.join(self.tmp, f))
        if y.ndim == 1 and keep_channels: y = np.stack([y, y], 1)
        if len(y) >= len(x): return y[:len(x)]
        return np.concatenate([y, np.zeros((len(x) - len(y),) + y.shape[1:])])

# ---------------------------------------------------------------- measurements
FL = 480
def env_db(y): return 20 * np.log10(np.sqrt(np.convolve(y ** 2, np.ones(FL) / FL, "same"))[::FL] + 1e-12)
def db(v): return 10 * np.log10(v + 1e-30)
def third_octaves(y, sr=SR):
    Y = np.abs(np.fft.rfft(y * np.hanning(len(y)))) ** 2; fr = np.fft.rfftfreq(len(y), 1 / sr)
    cs = 1000 * 2 ** (np.arange(-14, 10) / 3)
    return cs, np.array([db(Y[(fr >= c / 2 ** (1 / 6)) & (fr < c * 2 ** (1 / 6))].sum() / Y.sum()) for c in cs])
def harmonic_shares(y, f, sr=SR):
    y = y[int(0.6 * sr):int(1.6 * sr)]; y = y - y.mean()
    Y = np.abs(np.fft.rfft(y * np.blackman(len(y)))) ** 2; fr = np.fft.rfftfreq(len(y), 1 / sr)
    H = np.array([Y[np.abs(fr - k * f) < 5].sum() for k in range(1, int(sr / 2 / f) + 1)])
    harm = fr < 300                  # wide mask: supply-ripple sidebands are real hardware, not aliasing
    for k in range(1, int(sr / 2 / f) + 1): harm |= np.abs(fr - k * f) < 250
    return db(H[1:].sum() / H[0]), db(Y[~harm].sum() / Y[fr >= 15].sum())

class Report:
    def __init__(self): self.rows = []; self.fails = 0
    def check(self, test, name, ok, detail, warn_only=False):
        st = "PASS" if ok else ("WARN" if warn_only else "FAIL")
        self.rows.append({"test": test, "case": name, "status": st, "detail": detail}); self.fails += st == "FAIL"
        print(f"  {st}  {name}: {detail}", flush=True)

# ---------------------------------------------------------------- the checks
def t_stability(amp, R, pool, quick):
    print("stability: silence, tails, DC, NaN at corners and random settings")
    ks = amp.s["knobs"] + amp.s.get("fx_knobs", []); rng = np.random.default_rng(7)
    cases = [("all 0", {k: 0 for k in ks}), ("all 10", {k: 10 for k in ks}), ("all 5", {k: 5 for k in ks}),
             ("base", {})] + [(f"random {i}", {k: round(float(rng.uniform(0, 10)), 1) for k in ks}) for i in range(4 if quick else 12)]
    burst = np.concatenate([pluck(110, 1.0, .5, np.random.default_rng(3)), np.zeros(int(0.2 * SR))])
    def one(c):
        name, kn = c
        z = amp.render(np.zeros(int(2.0 * SR)), kn)
        y = amp.render(burst, kn, tail=3.0)
        return name, kn, z, y
    for name, kn, z, y in pool.map(one, cases):
        finite = np.all(np.isfinite(z)) and np.all(np.isfinite(y))
        sil = db(np.mean(z[SR:] ** 2)) if finite else 0; tail = db(np.mean(y[-SR // 2:] ** 2)) if finite else 0
        dc = 20 * np.log10(abs(y[int(0.2 * SR):].mean()) + 1e-12) if finite else 0
        fx_on = any(kn.get(f, amp.s.get("base", {}).get(f, 0)) >= 0.05 for f in amp.s.get("fx_knobs", []))   # echoes and reverbs ring on by design
        ok = finite and sil < LIMITS["silence_dbfs"] and (fx_on or tail < LIMITS["tail_dbfs"]) and dc < LIMITS["dc_dbfs"]
        R.check("stability", name, ok, f"finite {finite}, silence {sil:.0f} dBFS, tail {tail:.0f} dBFS, DC {dc:.0f} dBFS"
                + ("" if ok or name.startswith("all") or name == "base" else f"  [{kn}]"))

def t_sweep(amp, R, pool, quick, di):
    print("sweep: each gain knob 0..10 (blocking, level, compression, noise)")
    ex = env_db(di); quiet = ex < np.percentile(ex, 8)
    steps = [0, 2, 4, 6, 8, 10] if quick else list(range(11))
    for g in amp.s["gain_knobs"]:
        for ctxname, ctx in [("base", {})] + [(f"{h}=10", {h: 10}) for h in amp.s.get("hot_context", []) if h != g]:
            ys = list(pool.map(lambda v: amp.render(di, dict(ctx, **{g: v})), steps))
            es = [env_db(y) for y in ys]; prev = None; best = -1e9; msgs = []; ok = True; warns = []
            for v, y, e in zip(steps, ys, es):
                loud = e > e.max() - 15; lvl = np.mean(e[loud]); crest = 20 * np.log10(np.abs(y).max() / np.sqrt(np.mean(y ** 2)))
                noise = np.mean(e[quiet]) - lvl
                drop = ev = 0
                if prev is not None:
                    d = (e - prev)[loud] < -6; drop = d.mean()
                    runs = np.diff(np.flatnonzero(np.diff(np.concatenate([[0], d.astype(int), [0]]))))[::2] if d.any() else [0]
                    ev = max(runs) * FL / SR * 1000
                bad = []
                if g not in amp.s.get("level_free", []) and lvl < best - LIMITS["level_dip_db"]: bad.append(f"level fell {best - lvl:.1f} dB")
                if drop > LIMITS["dropout_frac"] or ev >= LIMITS["dropout_event_ms"]: bad.append(f"dropouts {drop * 100:.1f}% (longest {ev:.0f} ms)")
                if v >= 4 and prev is not None and crest > pcrest + LIMITS["crest_rise_db"] and pv >= 4: bad.append(f"crest rose {crest - pcrest:.1f} dB")
                if noise > LIMITS["noise_db"]: (bad if ctxname == "base" else warns).append(f"noise {noise:.0f} dB")
                if bad: ok = False; msgs.append(f"{g}={v}: " + ", ".join(bad))
                best = max(best, lvl); prev = e; pcrest = crest; pv = v
            if warns: R.check("sweep", f"{g} ({ctxname}) noise", False, "between-note hiss/hum close to the playing: " + ", ".join(warns), warn_only=True)
            R.check("sweep", f"{g} ({ctxname})", ok, "; ".join(msgs) if msgs else
                    f"level {np.mean(es[0][es[0] > es[0].max() - 15]):.1f} -> {np.mean(es[-1][es[-1] > es[-1].max() - 15]):.1f} dB, "
                    f"noise at 10 {np.mean(es[-1][quiet]) - np.mean(es[-1][es[-1] > es[-1].max() - 15]):.0f} dB")

def hot_corners(amp):
    c = [("base", {})] + [(f"{g}=10", {g: 10}) for g in amp.s["gain_knobs"]]
    c.append(("all gain 10", {g: 10 for g in amp.s["gain_knobs"]}))
    c += [(f"{p['name']}", p["knobs"]) for p in amp.s.get("presets", [])]
    return c

def t_touch(amp, R, pool, quick):
    print("touch: the amp cleans up with softer picking, and soft-picking distortion stays under the spec's ceiling")
    ceil = amp.s.get("soft_thd_max_db", -6.0)
    jobs = [(n, k, f, a) for n, k in hot_corners(amp) for f in (110, 196) for a in (0.03, 0.2)]
    res = dict(pool.map(lambda j: ((j[0], j[2], j[3]), harmonic_shares(amp.render(sine(j[2], j[3]), j[1]), j[2])[0]), jobs))
    for n, _ in hot_corners(amp):
        soft = max(res[(n, f, 0.03)] for f in (110, 196)); clean = min(res[(n, f, 0.2)] - res[(n, f, 0.03)] for f in (110, 196))
        maxed = n == "all gain 10"
        ok = clean >= LIMITS["touch_db"] and (maxed or soft <= ceil)
        R.check("touch", n, ok, f"soft-picking distortion {soft:.1f} dB re fundamental (ceiling {ceil}), cleans up by {clean:.1f} dB "
                f"from hard to soft (min {LIMITS['touch_db']})", warn_only=maxed)

def t_alias(amp, R, pool, quick):
    print("aliasing: energy off the harmonic series")
    jobs = [(n, k, f) for n, k in hot_corners(amp) for f in (880, 1318)]
    res = list(pool.map(lambda j: (j, harmonic_shares(amp.render(sine(j[2], 0.2), j[1]), j[2])[1]), jobs))
    for n, _ in hot_corners(amp):
        r = [(j[2], v) for j, v in res if j[0] == n]; worst = max(r, key=lambda t: t[1])
        R.check("aliasing", n, worst[1] < LIMITS["alias_db"], f"worst {worst[1]:.1f} dB at {worst[0]} Hz (limit {LIMITS['alias_db']})",
                warn_only=n == "all gain 10")

def loud_level(y):
    e = env_db(y); return np.mean(e[e > e.max() - 15])

def t_continuity(amp, R, pool, quick, di):
    print("continuity: every knob in half steps, no sudden level jumps")
    clip = di[:int(9 * SR)]
    vals = np.arange(0, 10.01, 1.0 if quick else 0.5)
    for k in amp.s["knobs"]:
        lv = list(pool.map(lambda v: loud_level(amp.render(clip, {k: float(v)})), vals))
        j = np.abs(np.diff(lv))
        if k in amp.s["gain_knobs"]: j[vals[:-1] < 1.0] = 0     # volume-type pots are steep near 0 by design
        i = int(np.argmax(j))
        lim = LIMITS["jump_gain_db"] if k in amp.s["gain_knobs"] else LIMITS["jump_db"]
        R.check("continuity", k, j[i] < lim, f"largest step {j[i]:.1f} dB between {vals[i]} and {vals[i + 1]}; range {min(lv):.1f}..{max(lv):.1f} dB")

def t_alive(amp, R, pool, quick, di):
    print("alive: every knob changes the sound")
    clip = di[:int(9 * SR)]
    for k in amp.s["knobs"]:
        ctx = amp.s.get("alive_context", {}).get(k, {})
        y0, y1 = pool.map(lambda v: amp.render(clip, dict(ctx, **{k: v})), (0, 10))
        _, b0 = third_octaves(y0); _, b1 = third_octaves(y1)
        dl = abs(loud_level(y1) - loud_level(y0)); dmax = max(np.max(np.abs(b1 - b0)), dl)
        R.check("alive", k, dmax >= LIMITS["alive_db"], f"largest change {dmax:.1f} dB")

def t_rates(amp, R, pool, quick, di):
    print("rates: 44.1 / 48 / 96 kHz agree")
    from scipy.signal import resample_poly
    clip = di[:int(9 * SR)]
    for n, kn in hot_corners(amp)[:3]:
        out = {}
        for sr, (u, d) in [(44100, (147, 160)), (48000, (1, 1)), (96000, (2, 1))]:
            x = resample_poly(clip, u, d) if sr != SR else clip
            y = amp.render(x, kn, sr=sr); finite = np.all(np.isfinite(y))
            e = env_db(resample_poly(y, d, u) if sr != SR else y)
            out[sr] = (finite, np.mean(e[e > e.max() - 15]), third_octaves(y, sr)[1])
        cs = third_octaves(clip)[0]; m = (cs >= 60) & (cs <= 8000)
        dl = max(abs(out[s][1] - out[48000][1]) for s in out); dbd = max(np.max(np.abs(out[s][2][m] - out[48000][2][m])) for s in out)
        ok = all(out[s][0] for s in out) and dl < LIMITS["rate_level_db"] and dbd < LIMITS["rate_band_db"]
        R.check("rates", n, ok, f"level spread {dl:.2f} dB, worst band {dbd:.1f} dB")

def t_channels(amp, R, pool, quick, di):
    print("channels: each output channel on its own, with the effects in and out (a collapse in one channel hides in a mono sum)")
    fx = [f for f in amp.s.get("fx_knobs", []) if f not in amp.s.get("fx_dynamic", [])]   # fx_dynamic (e.g. a swell) shape the level by design
    ex = env_db(di); act = ex > ex.max() - 30
    cases = [(n, k) for n, k in hot_corners(amp)]
    levels = [3, 6, 10] if not quick else [5, 10]
    jobs = [(n, dict(k), None, 0) for n, k in cases] + [(n, dict(k, **{f: v}), f, v) for n, k in cases for f in fx for v in levels]
    res = list(pool.map(lambda j: (j, amp.render(di, j[1], keep_channels=True)), jobs))
    ref = {j[0]: y for j, y in res if j[2] is None}
    for (n, kn, f, v), y in res:
        bad = []
        for c in range(y.shape[1]):
            e = env_db(y[:, c]); er = env_db(ref[n][:, c]); m = min(len(e), len(er), len(act)); e, er = e[:m], er[:m]
            loud = act[:m] & (er > er.max() - 30)
            erl = np.minimum(er, np.concatenate([er[:1], er[:-1]]))   # an effect may lag the dry by up to one 10 ms frame
            drop = (e - erl)[loud] < -12
            if drop.mean() > LIMITS["dropout_frac"]: bad.append(f"channel {c + 1} drops out on {drop.mean() * 100:.1f}% of the playing")
        if y.shape[1] == 2:
            el, er2 = env_db(y[:, 0]), env_db(y[:, 1]); m = min(len(el), len(act)); loud = act[:m]
            el, er2 = el[:m], er2[:m]; sh = lambda v: np.concatenate([v[:1], v[:-1]])
            gap = np.minimum.reduce([np.abs(el - er2), np.abs(sh(el) - er2), np.abs(el - sh(er2))])[loud]   # same one-frame lag
            if np.mean(gap > 20) > LIMITS["dropout_frac"]: bad.append(f"channels differ by over 20 dB on {np.mean(gap > 20) * 100:.1f}% of the playing")
        name = n + ("" if f is None else f", {f} {v}")
        R.check("channels", name, not bad, "; ".join(bad) if bad else "both channels follow the playing")

TESTS = {"stability": t_stability, "sweep": t_sweep, "touch": t_touch, "aliasing": t_alias, "continuity": t_continuity,
         "alive": t_alive, "rates": t_rates, "channels": t_channels}
NEEDS_DI = {"sweep", "continuity", "alive", "rates", "channels"}

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("spec"); ap.add_argument("--cli"); ap.add_argument("--quick", action="store_true")
    ap.add_argument("--only"); ap.add_argument("--json"); ap.add_argument("--jobs", type=int, default=os.cpu_count())
    a = ap.parse_args()
    spec = json.load(open(a.spec)); amp = Amp(spec, a.cli); R = Report(); di = guitar_di()
    print(f"{spec['name']}: {amp.cli}"); t0 = time.time()
    with ThreadPoolExecutor(a.jobs) as pool:
        for name, fn in TESTS.items():
            if a.only and name not in a.only.split(","): continue
            fn(amp, R, pool, a.quick, di) if name in NEEDS_DI else fn(amp, R, pool, a.quick)
    print(f"\n{spec['name']}: {R.fails} failed of {len(R.rows)} checks, {time.time() - t0:.0f} s")
    if a.json: json.dump({"amp": spec["name"], "limits": LIMITS, "results": R.rows}, open(a.json, "w"), indent=1)
    sys.exit(min(R.fails, 100))

if __name__ == "__main__":
    main()

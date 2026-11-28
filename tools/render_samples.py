"""Render docs/samples from docs/samples/permutations.txt: the puzzle-remains lead DI (not in the repo; pass its path or keep it at
~/rindu/samples/src/puzzle_lead_cut.wav) through uh20_cli, then -18 LUFS with a transparent peak limiter (-1.5 dBFS ceiling, no
other compression, so the amp's own dynamics are what you hear), two identical channels like the other amps' samples, and Opus
(128 kbps VBR) encoded straight from the float WAV. Needs numpy, scipy, soundfile, pyloudnorm and ffmpeg with libopus.

permutations.txt lines are  name|key=value ...  with an optional third field  |group=NAME. Samples in a group share one gain, the
one that puts the group's first sample at -18 LUFS, so a knob that changes level (Sag) is heard doing it. Each grouped sample's
intended offset from -18 is written to docs/samples/levels.json, which tools/check_samples.py reads.

Every name starts with what the setting measurably is, and the script refuses to run if a name disagrees. It measures distortion
of a 196 Hz tone at medium picking level (0.1) through the AMP output: under 5% is Clean, 5-15% Edge of breakup, 15-30% Crunch,
over 30% Heavy crunch, or Lead when the Drive stage is on. `render_samples.py --check-labels` runs only that check (no DI needed,
CI runs it).

Usage: render_samples.py [DI.wav] [--only 06,07,...]  (--only: just the samples whose numbers are listed; a grouped sample also
renders the rest of its group so they share the gain)"""
import numpy as np, soundfile as sf, pyloudnorm as pl, subprocess, os, sys, tempfile, json
from scipy.ndimage import minimum_filter1d
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); T = tempfile.mkdtemp()
args = sys.argv[1:]; ONLY = None
if "--only" in args: i = args.index("--only"); ONLY = set(args[i + 1].split(",")); del args[i:i + 2]
CLI = os.path.join(R, "build", "uh20_cli")

def medium_thd(knobs, f=196.0, a=0.1):
    t = np.arange(2 * 48000) / 48000; (a * np.sin(2 * np.pi * f * t) * np.minimum(1, t / 0.02)).astype(np.float32).tofile(f"{T}/s.f32")
    subprocess.run([CLI, f"{T}/s.f32", f"{T}/s.out", "48000", "output=1"] + knobs.split(), check=True)
    y = np.fromfile(f"{T}/s.out", np.float32).astype(float)[28800:76800]; y -= y.mean()
    Y = np.abs(np.fft.rfft(y * np.blackman(len(y)))) ** 2; fr = np.fft.rfftfreq(len(y), 1 / 48000)
    H = [Y[np.abs(fr - k * f) < 5].sum() for k in range(1, 60)]; return 100 * np.sqrt(sum(H[1:]) / H[0])

def character(knobs):
    d = medium_thd(knobs); drive = float(dict(kv.split("=") for kv in knobs.split()).get("drive", 0))
    c = "clean" if d < 5 else "edge of breakup" if d < 15 else "crunch" if d < 30 else ("lead" if drive >= 0.05 else "heavy crunch")
    return c, d

def check_labels():
    bad = 0
    for l in open(f"{R}/docs/samples/permutations.txt"):
        if not l.strip(): continue
        name, knobs = l.rstrip("\n").split("|")[:2]; c, d = character(knobs)
        head = name[3:].split(" - ")[0].lower()
        ok = c in head and not (c == "crunch" and "heavy crunch" in head)
        bad += not ok; print(f"{'PASS' if ok else 'FAIL'}  {name[:50]}: measures {c} ({d:.1f}% at medium picking)")
    return bad

if "--check-labels" in args: sys.exit(check_labels())
if check_labels(): sys.exit("a sample's name disagrees with what it measures; rename it first")
DI = args[0] if args else os.path.expanduser("~/rindu/samples/src/puzzle_lead_cut.wav")
x, sr = sf.read(DI); x = x if x.ndim == 1 else x.mean(1)
np.concatenate([x, np.zeros(int(0.5 * sr))]).astype(np.float32).tofile(f"{T}/in.f32")
meter = pl.Meter(sr)

def limit(y, ceil_db=-1.5, look=0.003, rel=0.05):   # lookahead peak limiter: gain = min over the window of ceiling/|x|, smoothed
    ceil = 10 ** (ceil_db / 20); a = np.abs(y).max(1); need = np.minimum(1, ceil / np.maximum(a, 1e-12))
    g = minimum_filter1d(need, size=2 * int(look * sr) + 1); k = np.exp(-1 / (rel * sr)); s = 1.0; out = np.empty_like(g)
    for i in range(len(g)):
        s = g[i] if g[i] < s else k * s + (1 - k) * g[i]; out[i] = s
    return y * out[:, None]

def render(args):
    subprocess.run([CLI, f"{T}/in.f32", f"{T}/out.f32", str(sr)] + args.split(), check=True)
    y = np.fromfile(f"{T}/out.f32", dtype=np.float32).astype(np.float64)
    return np.stack([y, y], 1)                      # two identical channels, measured as stereo like the other amps

lines = [l.rstrip("\n").split("|") for l in open(f"{R}/docs/samples/permutations.txt") if l.strip()]
groups = {}
for f in lines:
    if len(f) > 2 and f[2].startswith("group="): groups.setdefault(f[2][6:], []).append(f[0])
want = {f[0] for f in lines if not ONLY or f[0].split()[0] in ONLY}
for g, members in groups.items():
    if want & set(members): want |= set(members)
lp = f"{R}/docs/samples/levels.json"; levels = json.load(open(lp)) if os.path.exists(lp) else {}
gain = {}
for f in lines:
    name, args = f[0], f[1]; grp = f[2][6:] if len(f) > 2 and f[2].startswith("group=") else None
    if name not in want: continue
    y = render(args); L = meter.integrated_loudness(y)
    if grp is None: g = -18 - L
    else: g = gain.setdefault(grp, -18 - L)
    y = limit(y * 10 ** (g / 20)); Lf = meter.integrated_loudness(y)
    key = name.split()[0]
    if grp is None: levels.pop(key, None)
    else: levels[key] = round(Lf + 18, 1)
    sf.write(f"{T}/u.wav", y.astype(np.float32), sr, subtype="FLOAT")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", f"{T}/u.wav", "-c:a", "libopus", "-b:a", "128k", "-vbr", "on",
                    "-compression_level", "10", "-application", "audio", f"{R}/docs/samples/{name}.opus"], check=True)
    print(f"{name}: {Lf:.1f} LUFS" + (f" (group {grp})" if grp else ""))
json.dump(dict(sorted(levels.items())), open(lp, "w"), indent=1)

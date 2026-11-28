"""Render docs/samples from docs/samples/permutations.txt: the puzzle-remains lead DI (not in the repo; pass its path or keep it at
~/rindu/samples/src/puzzle_lead_cut.wav) through uh20_cli, light compression, -18 LUFS, then Opus (128 kbps VBR, two identical channels like the other amps' samples; some players handle mono Ogg Opus
unreliably) encoded
straight from the float WAV. Needs numpy, soundfile, pyloudnorm and ffmpeg with libopus.
Usage: render_samples.py [DI.wav] [--only 06,07,...]  (--only: just the samples whose numbers are listed)"""
import numpy as np, soundfile as sf, pyloudnorm as pl, subprocess, os, sys, tempfile
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); T = tempfile.mkdtemp()
args = sys.argv[1:]; ONLY = None
if "--only" in args: i = args.index("--only"); ONLY = set(args[i + 1].split(",")); del args[i:i + 2]
DI = args[0] if args else os.path.expanduser("~/rindu/samples/src/puzzle_lead_cut.wav")
CLI = os.path.join(R, "build", "uh20_cli")
x, sr = sf.read(DI); x = x if x.ndim == 1 else x.mean(1)
np.concatenate([x, np.zeros(int(0.5 * sr))]).astype(np.float32).tofile(f"{T}/in.f32")
def comp(y, sr, thr_db, ratio=2.5, att=0.008, rel=0.18):   # light feed-forward compression, RMS detector, soft knee
    a, r = np.exp(-1 / (att * sr)), np.exp(-1 / (rel * sr)); env = 0; g = np.empty_like(y); p2 = y * y
    for i in range(len(y)):
        c = a if p2[i] > env else r; env = c * env + (1 - c) * p2[i]
        over = 10 * np.log10(env + 1e-12) - thr_db
        g[i] = -(0 if over < -3 else ((over + 3) ** 2 / 12 * (1 - 1 / ratio) if over < 3 else over * (1 - 1 / ratio)))
    return y * 10 ** (g / 20)
for line in open(f"{R}/docs/samples/permutations.txt"):
    name, args = line.rstrip("\n").split("|")
    if ONLY and name.split()[0] not in ONLY: continue
    subprocess.run([CLI, f"{T}/in.f32", f"{T}/out.f32", str(sr)] + args.split(), check=True)
    y = np.fromfile(f"{T}/out.f32", dtype=np.float32).astype(np.float64)
    y = y / (np.abs(y).max() + 1e-12) * 0.5
    y = comp(y, sr, 10 * np.log10(np.mean(y ** 2)) + 6)
    y = np.stack([y, y], 1)                                   # two identical channels, measured as stereo like the other amps
    y *= 10 ** ((-18 - pl.Meter(sr).integrated_loudness(y)) / 20)
    if np.abs(y).max() > 0.89: y *= 0.89 / np.abs(y).max()
    sf.write(f"{T}/u.wav", y.astype(np.float32), sr, subtype="FLOAT")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", f"{T}/u.wav", "-c:a", "libopus", "-b:a", "128k", "-vbr", "on",
                    "-compression_level", "10", "-application", "audio", f"{R}/docs/samples/{name}.opus"], check=True)
    print(name)

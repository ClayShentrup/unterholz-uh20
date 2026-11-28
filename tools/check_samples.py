#!/usr/bin/env python3
"""check_samples: sanity checks for an amp's published sample files, as served or as committed.

Usage:  check_samples.py <pages URL or local docs/ dir> [...more]

For every <audio src> on the page (or every .opus under <dir>/samples): it must be served 200 as audio/ogg, decode as Opus
at 48 kHz in two channels with neither channel silent or polarity-flipped, sit within 1 LU of the -18 LUFS target, keep its
true peak at or below -1 dBTP, have no stretch quieter than 15 LU under its own loudness (outside the final tail), and run
within 15% of the median length of its page. Prints PASS/FAIL per file; exit status = number of failures.
"""
import sys, os, re, subprocess, tempfile, urllib.parse, urllib.request, glob
import numpy as np, pyloudnorm as pl

TARGET, TOL, TP_MAX, HOLE_LU, LEN_TOL = -18.0, 1.0, -1.0, 15.0, 0.15

def decode(path):
    info = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=codec_name,sample_rate,channels",
                           "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip().split(",")
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-f", "f32le", "-ac", "2", "-ar", "48000", "-"], capture_output=True).stdout
    return info, np.frombuffer(raw, np.float32).astype(float).reshape(-1, 2)

def true_peak(y):
    from scipy.signal import resample_poly
    return 20 * np.log10(np.abs(resample_poly(y, 4, 1, axis=0)).max() + 1e-12)

def sources(target):
    if target.startswith("http"):
        base = target if target.endswith("/") else target + "/"
        html = urllib.request.urlopen(base).read().decode()
        srcs = list(dict.fromkeys(re.findall(r'<audio[^>]*src="([^"]+)"', html)))
        return [(urllib.parse.unquote(s.split("/")[-1]), urllib.parse.urljoin(base, s)) for s in srcs]
    return [(os.path.basename(p), p) for p in sorted(glob.glob(os.path.join(target, "samples", "*.opus")))]

def fetch(src, tmp):
    if not src.startswith("http"): return src, None
    req = urllib.request.Request(src, headers={"Cache-Control": "no-cache"})
    with urllib.request.urlopen(req) as r:
        ct = r.headers.get("Content-Type", ""); p = os.path.join(tmp, "s.opus"); open(p, "wb").write(r.read())
    return p, ct

def check(target):
    print(target); tmp = tempfile.mkdtemp(); rows = []
    for name, src in sources(target):
        p, ct = fetch(src, tmp); (codec, sr, ch), y = decode(p)
        L = pl.Meter(48000).integrated_loudness(y); tp = true_peak(y)
        rms = np.sqrt(np.mean(y ** 2, 0)); corr = np.corrcoef(y[:, 0], y[:, 1])[0, 1] if rms.min() > 0 else 0
        w = 3 * 48000; st = [pl.Meter(48000).integrated_loudness(y[i:i + w]) for i in range(0, len(y) - 2 * w, w)]
        st = [v for v in st if np.isfinite(v)]
        rows.append(dict(name=name, ct=ct, codec=codec, sr=sr, ch=ch, L=L, tp=tp, rms=rms, corr=corr, hole=L - min(st) if st else 99,
                         dur=len(y) / 48000))
    med = np.median([r["dur"] for r in rows]) if rows else 0; fails = 0
    for r in rows:
        bad = []
        if r["ct"] is not None and not r["ct"].startswith("audio/ogg"): bad.append(f"served as {r['ct']}")
        if r["codec"] != "opus" or r["sr"] != "48000": bad.append(f"{r['codec']} {r['sr']} Hz")
        if r["ch"] != "2": bad.append(f"{r['ch']} channel(s)")
        if 20 * np.log10(r["rms"].min() + 1e-12) < 20 * np.log10(r["rms"].max() + 1e-12) - 20: bad.append("one channel silent")
        if r["corr"] < 0: bad.append(f"channels out of polarity ({r['corr']:.2f})")
        if abs(r["L"] - TARGET) > TOL: bad.append(f"{r['L']:.1f} LUFS (target {TARGET} ± {TOL})")
        if r["tp"] > TP_MAX: bad.append(f"true peak {r['tp']:.1f} dBTP")
        if r["hole"] > HOLE_LU: bad.append(f"a stretch {r['hole']:.0f} LU under the file's loudness")
        if abs(r["dur"] - med) > LEN_TOL * med: bad.append(f"{r['dur']:.1f} s vs median {med:.1f} s")
        fails += bool(bad)
        print(f"  {'FAIL' if bad else 'PASS'}  {r['name'][:70]}: {r['L']:.1f} LUFS, {r['tp']:.1f} dBTP, {r['ch']} ch, {r['dur']:.1f} s"
              + ("  <- " + "; ".join(bad) if bad else ""))
    print(f"  {fails} of {len(rows)} failed")
    return fails

if __name__ == "__main__":
    sys.exit(min(100, sum(check(t) for t in sys.argv[1:])))

"""compare.py AMP CLIP DRIVE [BURST TSTART [SAG]]: runs ../spice/rhythm.py (ngspice) and amp_transient (the engine) for the same 220 Hz sine and prints,
per node, the residual (BURST: the first 0.1 s at BURST x AMP, to exercise gate blocking; TSTART: start of the compared window) of the engine against SPICE (best alignment within +/-15 us) and the level difference."""
import os, subprocess, sys, numpy as np
H = os.path.dirname(os.path.abspath(__file__))
A, J, D = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
B, T0 = (sys.argv[4], sys.argv[5]) if len(sys.argv) > 5 else ("1", "0.3")
SG = sys.argv[6] if len(sys.argv) > 6 else "0"
out = subprocess.run([sys.executable, os.path.join(H, "../spice/rhythm.py"), A, str(J), f"drive={D}", f"burst={B}", f"tstart={T0}", f"sag={SG}"], check=True, cwd=H,
                     capture_output=True, text=True).stdout.split()[-1]
s = np.loadtxt(out)
c = np.array([l.split() for l in subprocess.run([os.path.join(H, "amp_transient"), A, str(J), str(D), B, T0, SG], check=True, capture_output=True, text=True).stdout.splitlines()], float)
for k, n in enumerate(["first/Drive stage", "Q3 drain", "pair out", "speaker"]):
    ts, ys = s[:, 2 * k], s[:, 2 * k + 1]; ac = ys - ys.mean(); best = 1e9
    for sh in np.linspace(-15e-6, 15e-6, 31):
        yc = np.interp(ts + sh, c[:, 0], c[:, k + 1]); d = (yc - yc.mean()) - ac
        best = min(best, 20 * np.log10(np.sqrt(np.mean(d ** 2)) / np.sqrt(np.mean(ac ** 2)) + 1e-30))
    g = 20 * np.log10(np.std(np.interp(ts, c[:, 0], c[:, k + 1])) / np.std(ys))
    print(f"{n:18s} {best:6.1f} dB residual, level {g:+.2f} dB")
# the first node's mean (its bias point) just after TSTART and at the end: a gate that conducts and charges its coupling
# capacitor shows up as a shifted mean that recovers
t0 = float(T0); ts, ys = s[:, 0], s[:, 1]
for nm, (tt, yy) in [("SPICE", (ts, ys)), ("engine", (c[:, 0], c[:, 1]))]:
    a = yy[(tt >= t0) & (tt < t0 + 0.02)].mean(); b = yy[tt >= 0.4].mean()
    print(f"{'first-node mean':18s} {nm:6s} {t0:.2f}-{t0 + 0.02:.2f} s: {a:6.2f} V, 0.40-0.50 s: {b:6.2f} V")

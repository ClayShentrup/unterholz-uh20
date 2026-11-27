"""compare.py AMP CLIP DRIVE: runs ../spice/rhythm.py (ngspice) and amp_transient (the engine) for the same 220 Hz sine and prints,
per node, the residual of the engine against SPICE (best alignment within +/-15 us) and the level difference."""
import os, subprocess, sys, numpy as np
H = os.path.dirname(os.path.abspath(__file__))
A, J, D = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
out = subprocess.run([sys.executable, os.path.join(H, "../spice/rhythm.py"), A, str(J), f"drive={D}"], check=True, cwd=H,
                     capture_output=True, text=True).stdout.split()[-1]
s = np.loadtxt(out)
c = np.array([l.split() for l in subprocess.run([os.path.join(H, "amp_transient"), A, str(J), str(D)], check=True, capture_output=True, text=True).stdout.splitlines()], float)
for k, n in enumerate(["first/Drive stage", "Q3 drain", "pair out", "speaker"]):
    ts, ys = s[:, 2 * k], s[:, 2 * k + 1]; ac = ys - ys.mean(); best = 1e9
    for sh in np.linspace(-15e-6, 15e-6, 31):
        yc = np.interp(ts + sh, c[:, 0], c[:, k + 1]); d = (yc - yc.mean()) - ac
        best = min(best, 20 * np.log10(np.sqrt(np.mean(d ** 2)) / np.sqrt(np.mean(ac ** 2)) + 1e-30))
    g = 20 * np.log10(np.std(np.interp(ts, c[:, 0], c[:, k + 1])) / np.std(ys))
    print(f"{n:18s} {best:6.1f} dB residual, level {g:+.2f} dB")

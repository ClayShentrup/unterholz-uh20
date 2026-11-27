"""Every pot <= 100k (for MCP41HV51 recall): the LEAD networks scaled R/10, C x10 behind a follower, against the originals, in ngspice AC.
Drive network: J1's 27k drain -> 220n -> 470k||470p -> 1M log Drive (1n bright). Stack: the follower (~1k) -> 470p/220k treble/33k slope/
22n/22n/1M bass/22k mid -> 1.5M -> 1M Master -> 0.1u -> 1M."""
import subprocess, numpy as np, os
H = os.path.dirname(os.path.abspath(__file__))
def run(body, name):
    out = f"/tmp/claude_{name}.txt"
    open(f"/tmp/claude_{name}.cir", "w").write(f"* {name}\nVIN in 0 DC 0 AC 1\n{body}\n.ac dec 10 30 20000\n.control\nrun\nwrdata {out} vdb(out)\n.endc\n.end\n")
    subprocess.run(["ngspice", "-b", f"/tmp/claude_{name}.cir"], capture_output=True); a = np.loadtxt(out); return a[:, 0], a[:, 1]
def drive(k, rsrc, dv=0.5):
    return f"RS in n0 {rsrc}\nC1 n0 a {220e-9*k}\nR1 a t {470e3/k}\nC2 a t {470e-12*k}\nRA t out {1e6/k*(1-dv)}\nRB out 0 {1e6/k*dv}\nCB t out {1e-9*k}"
def stack(k, rsrc, t=0.5, b=0.1, m=0.5, ma=0.37):
    return (f"RS in n0 {rsrc}\nC5 n0 tt {470e-12*k}\nRT1 tt tw {220e3/k*(1-t)}\nRT2 tw tb {220e3/k*t}\nR6 n0 x {33e3/k}\nC6 x tb {22e-9*k}\n"
            f"RB tb mt {1e6/k*b}\nC7 x mt {22e-9*k}\nRM mt 0 {22e3/k*m}\nRBAL tw bal {1.5e6/k}\nRM1 bal mv {1e6/k*(1-ma)}\nRM2 mv 0 {1e6/k*ma}\nCC mv out {0.1e-6*k}\nRL out 0 {1e6/k}")
for name, orig, scaled in [("drive", drive(1, 27e3), drive(10, 2.7e3)), ("stack", stack(1, 1e3), stack(10, 100))]:
    f, a = run(orig, name + "_o"); _, b = run(scaled, name + "_s")
    print(f"{name}: scaled vs original, 30 Hz - 20 kHz: max difference {np.abs(a-b).max():.4f} dB")

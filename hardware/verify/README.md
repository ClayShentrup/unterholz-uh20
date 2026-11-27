# Circuit-simulation checks against ngspice

`source/dsp/JfetKit.h` is the circuit kit the UH-20 engine is built on: the vendor J201 model (the same `.model` as
`../spice/j201.lib`), linear networks in state-space form with exact DC start-up and port loading, and the J201 blocks (common-source
stage with its source network, source follower, a gain stage DC-coupled into a follower solved jointly, gate conduction).

- `amp_transient.cpp AMP CLIP DRIVE [BURST TSTART [SAG]]` runs the whole engine (8x, 384 kHz) on a 220 Hz sine and prints the nodes that
  `../spice/rhythm.py` writes; `compare.py AMP CLIP DRIVE [BURST TSTART [SAG]]` runs both and prints the residual per node (BURST: the first 0.1 s at BURST times
  the level, which makes a gate conduct and charge its coupling capacitor; TSTART: where the compared window starts) and the first
  node's mean just after TSTART and at the end (build `amp_transient` first:
  `clang++ -std=c++20 -O2 amp_transient.cpp -o amp_transient`).
- `lead_transient.cpp AMP` (the kit's first check, on the v0.2 lead netlist, which v0.3 dropped) runs `../spice/jfet800.cir` (the LEAD preamp in J201s, Drive at 0.7) block by block at 500 kHz and prints
  the same nodes ngspice writes (`v(s4) v(tw) v(d2) v(d1)`), for a 220 Hz sine of AMP volts.
- `lead_response.cpp` measures the small-signal response at the stack output and the cold clipper.

Results (2026-11-27), C++ against ngspice:

| | |
|---|---|
| DC operating point | every node within 3 mV |
| small-signal response, stack output, 100 Hz - 20 kHz | within 0.23 dB (junction capacitances included as Miller input capacitance) |
| small-signal response, cold-clipper drain | within 0.16 dB |
| 220 Hz transient, input stage | -57 dB residual at every level |
| 220 Hz transient, cold clipper / stack output | -40 to -51 dB at 20-100 mV, -25 to -38 dB at 0.4-0.8 V (after a few microseconds of lag from the junction capacitances); harmonics within 0.1-0.5 dB, except the follower's second harmonic while its gate conducts hard |

The amp itself, v0.3 (2026-11-27), `compare.py` against ngspice, panel at the studio-clean settings, 220 Hz:

| AMP V, Clip, Drive | first or Drive stage | Q3 drain | speaker |
|---|---|---|---|
| 0.1, 0, 0 (Drive out) | -58.4 dB | -49.7 dB | -49.3 dB |
| 0.1, 0, 3 | -52.8 dB | -47.7 dB | -48.8 dB |
| 0.3, 5, 3 | -58.4 dB | -52.5 dB | -56.2 dB |
| 0.1, 5, 8 | -49.4 dB | -45.4 dB | -49.8 dB |
| 0.3, 0, 8 (Drive stage's gate conducting) | -38.3 dB | -36.6 dB | -38.8 dB |

Levels agree within 0.04 dB at every node.

v0.3.1 (2026-11-27), the revised Drive stage and the Sag transconductor (`hardware/DESIGN.md`, v0.3.1):

| AMP V, Clip, Drive | first or Drive stage | Q3 drain | speaker |
|---|---|---|---|
| 0.1, 0, 0 (Drive out) | -58.4 dB | -49.7 dB | -49.3 dB |
| 0.1, 0, 3 | -60.8 dB | -51.4 dB | -50.9 dB |
| 0.3, 5, 3 | -52.7 dB | -50.8 dB | -51.5 dB |
| 0.1, 5, 8 | -46.4 dB | -45.0 dB | -45.3 dB |
| 0.3, 0, 8 (gate conducting) | -45.4 dB | -40.8 dB | -43.7 dB |
| 0.3, 0, 10 (gate conducting) | -44.8 dB | -41.5 dB | -44.3 dB |
| 0.1, 0, 10, `BURST 10 TSTART 0.1` (blocking test) | -45.5 dB | -42.6 dB | -42.3 dB |
| 0.3, 0, 0, Sag 10, from 0.1 s | -58.5 dB | -49.9 dB | -51.6 dB |
| 0.1, 0, 8, Sag 10, from 0.1 s | -46.6 dB | -44.4 dB | -42.8 dB |

Levels agree within 0.03 dB at every node. In the blocking test the Drive stage's mean is 9.05 V in ngspice and 9.06 V in the plug-in
just after the strum, 8.25 V in both at the end; the v0.3.0 stage, run the same way in ngspice, is cut off (drain at 18 V) from 0.10 to
0.15 s.

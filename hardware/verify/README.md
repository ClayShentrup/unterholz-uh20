# Circuit-simulation checks against ngspice

`source/dsp/JfetKit.h` is the circuit kit the UH-20 engine is being rebuilt on: the vendor J201 model (the same `.model` as
`../spice/j201.lib`), linear networks in state-space form with exact DC start-up and port loading, and the J201 blocks (common-source
stage with its source network, source follower, a gain stage DC-coupled into a follower solved jointly, gate conduction).

- `lead_transient.cpp AMP` runs `../spice/jfet800.cir` (the LEAD preamp in J201s, Drive at 0.7) block by block at 500 kHz and prints
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

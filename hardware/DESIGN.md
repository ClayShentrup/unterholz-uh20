# UH-20 circuit design

The deliverable is a schematic of ordinary parts. The plugin (`source/dsp/UH20Engine.h`) is meant to be a simulation of exactly that circuit. Each block below is labelled **circuit** (modelled from components) or **behavioural** (modelled from measurements, to be replaced by its circuit).

## Signal path (v0.1)

1. **Input stage (circuit).** J201 at about 0.1 mA, Rd 68k, Rs 3.3k + 1.5k ‖ 0.68 µF (TIEF adds 22 µF).
2. **JUBILÄUM (circuit).** A dual-gang 470k/100k reverse-log pot with click-off. One gang feeds an LED pair; the other bypasses part of Q1's source resistor so the stage drives the LEDs harder. To be updated to the 2555 arrangement (anti-parallel LEDs in series with 1N4007s, 2 vs 1, 1 nF across) documented in `REFERENCE_CIRCUITS.md`.
3. **RHYTHMUS (circuit).** A plate-driven tone stack (250k treble / 250k bass / 25k mid, 250 pF, 100k slope, 0.1 µF / 0.047 µF), solved as a 6-node nodal network. Then Volume 1M log with a 100 pF bright cap, two J201 gain stages (Rd 47k, Rs 2.2k ‖ 22 µF) and a 100k/6.8k interstage divider.
4. **LEAD (behavioural, being replaced).** Gain stages fitted to measured classic-amp compression and harmonics. Its circuit replacement is in `spice/jfet800.cir`: four J201s translating the 2203-style preamp stage by stage (input 27k/1.5k bypassed, cold clipper 56k/2.2k unbypassed, gain stage 56k/1.8k, source follower into a 470 pF / 33k / 22 nF / 22 nF stack). It matches the tube preamp's THD and odd-harmonic profile within about 1-2 dB at every level.
5. **Power stage (behavioural, being replaced).** A tanh characteristic with presence in the feedback leg (50k + 4.7k + 22 nF) and supply sag (an envelope-driven droop of up to 50%, 15 ms / 120 ms). Its circuit: a J201 (or matched-BJT) long-tailed pair, whose transfer is tanh-shaped, fed from an RC supply.
6. **REC (circuit).** An analog cab sim: Sallen-Key high-pass at 98 Hz, MFB band-passes at 259 Hz (+9.7 dB) and 3.0 kHz (+10.6 dB) summed, and two Sallen-Key low-passes at 4.94 kHz. All E96 values.

## Method

- `spice/tube_stage.py` computes a tube stage's DC transfer (Koren 12AX7 model, published values).
- `spice/match_stage.py` searches E12 resistor values for a J201 stage at 18 V with the same normalised curve.
- First results: cold clipper 0.3%, gain stage 0.7%, input stage 1.0% of swing.
- Interstage dividers are the tube divider × the next stage's input scale × the previous stage's output scale.

## Status 2026-11-27: converting the behavioural blocks

- **Fixed in v0.1.2:** the Jubilee LED solve is exact (Lambert W). Before, it was wrong on 17-35% of samples by up to 5 V.
- **Circuit kit:** `source/dsp/JfetKit.h` simulates J201 stages from the vendor model, with the drain loading, source networks,
  gate conduction, follower interaction and junction capacitance of the real circuit. Checked against ngspice in
  `hardware/verify/` (see its README): the LEAD preamp of `spice/jfet800.cir` matches within 0.23 dB small-signal and to -25..-59 dB
  on transient waveforms.
- **Still behavioural in the shipped engine, to be replaced with JfetKit blocks:**
  1. RHYTHMUS Q1-Q3: a linear small-signal filter plus the fitted `jshape` curve per stage. Replace with `CSStage`s at the component
     values above (Q1 Rd 68k, Rs 3.3k + 1.5k || 0.68 uF with the Jubilee gang; Q2/Q3 Rd 47k, Rs 2.2k || 22 uF), driving the existing
     networks through their ports.
  2. LEAD: the fitted gain stages. Replace with the verified `jfet800.cir` chain, with Drive as the 1M log pot and the stack pots live.
  3. Output stage and Sag: tanh plus an envelope-driven droop. To be replaced by a real power section: a J201 long-tailed pair into a
     class AB LM3886, with Presence in its feedback network. Sag becomes real supply impedance: the LM3886 rails from an unregulated
     supply whose series resistance the Sag control sets, so the rails droop with the load current.
- The REC cab sim is already a circuit (active filters on E96 values).

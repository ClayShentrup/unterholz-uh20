# UH-20 circuit design

The deliverable is a schematic of ordinary parts. The plugin (`source/dsp/UH20Engine.h`) is meant to be a simulation of exactly that circuit. Each block below is labelled **circuit** (modelled from components) or **behavioural** (modelled from measurements, to be replaced by its circuit).

## Signal path (v0.1)

1. **Input stage (circuit).** J201 at about 0.1 mA, Rd 68k, Rs 3.3k + 1.5k ‖ 0.68 µF (TIEF adds 22 µF).
2. **CLIP (called JUBILÄUM in v0.1; circuit).** A dual-gang 470k/100k reverse-log pot (47k/100k since v0.2) with click-off. One gang feeds an LED pair; the other bypasses part of Q1's source resistor so the stage drives the LEDs harder. To be updated to the 2555 arrangement (anti-parallel LEDs in series with 1N4007s, 2 vs 1, 1 nF across) documented in `REFERENCE_CIRCUITS.md`.
3. **RHYTHMUS (circuit).** A plate-driven tone stack (250k treble / 250k bass / 25k mid, 250 pF, 100k slope, 0.1 µF / 0.047 µF), solved as a 6-node nodal network. Then Volume 1M log with a 100 pF bright cap, two J201 gain stages (Rd 47k, Rs 2.2k ‖ 22 µF) and a 100k/6.8k interstage divider.
4. **LEAD (behavioural, being replaced).** Gain stages fitted to measured classic-amp compression and harmonics. Its circuit replacement is in `spice/jfet800.cir`: four J201s translating the 2203-style preamp stage by stage (input 27k/1.5k bypassed, cold clipper 56k/2.2k unbypassed, gain stage 56k/1.8k, source follower into a 470 pF / 33k / 22 nF / 22 nF stack). It matches the tube preamp's THD and odd-harmonic profile within about 1-2 dB at every level.
5. **Power stage (behavioural, being replaced).** A tanh characteristic with presence in the feedback leg (50k + 4.7k + 22 nF) and supply sag (an envelope-driven droop of up to 50%, 15 ms / 120 ms). Its circuit: a J201 (or matched-BJT) long-tailed pair, whose transfer is tanh-shaped, fed from an RC supply.
6. **REC (circuit).** An analog cab sim: Sallen-Key high-pass at 98 Hz, MFB band-passes at 259 Hz (+9.7 dB) and 3.0 kHz (+10.6 dB) summed, and two Sallen-Key low-passes at 4.94 kHz. All E96 values.

## Method

- `spice/tube_stage.py` computes a tube stage's DC transfer (Koren 12AX7 model, published values).
- `spice/match_stage.py` searches E12 resistor values for a J201 stage at 18 V with the same normalised curve.
- First results: cold clipper 0.3%, gain stage 0.7%, input stage 1.0% of swing.
- Interstage dividers are the tube divider × the next stage's input scale × the previous stage's output scale.

## v0.2: the whole box is a circuit (2026-11-27)

Nothing in the shipped engine is behavioural any more. `source/dsp/UH20Engine.h` builds the box from parts (`source/dsp/JfetKit.h`).
Every J201 is the vendor SPICE model, solved each sample with its source network, drain load and gate conduction.

- **RHYTHM.** Q1 (J201, Rd 68k, source 3.3k + 1.5k || 0.68 uF, Clip gang A: 100k reverse-log + 10 uF across the 3.3k; Shift adds 22 uF)
  -> follower -> Mark III stack -> Volume (bright cap) -> Q2 (47k, 2.2k || 22 uF) -> 22n -> 100k/6.8k -> Q3 (47k, 2.2k || 22 uF)
  -> follower -> Master -> power section. The Clip LEDs (a red LED one way, two the other) hang off the stack input through a coupling
  capacitor and Clip gang B; Clip 0 switches the whole branch out.
- **LEAD.** `spice/jfet800.cir`: J1 (27k, 1.5k || 100 uF) behind a 47k/56k input pad -> follower -> Drive -> J2 cold clipper (56k / 2.2k)
  with the Clip LEDs behind its coupling capacitor -> 470k || 470p -> J3 (56k / 1.8k) DC-coupled to a J201 follower -> buffer ->
  Marshall stack -> 150k channel balance -> Master.
- **POWER (class AB).** J201 long-tailed pair (22k drains, 30k tail, +/-15 V) -> 1 uF -> LM3886 (gain 4.93) -> 8 ohm load. Global
  feedback: 100k from the speaker to the pair's second gate, 4.7k down; Presence = 0.1 uF + 5k across it. 220 pF on the pair's output
  compensates the loop: the gain margin is at least 10.3 dB at every Presence and Master setting (found by lowering the feedback resistor
  until the loop rings; the tests check it settles). Rails: 22 V AC secondary, bridge, 4700 uF per rail, through 0.3 ohm of winding plus
  the Sag rheostat (0-2.7 ohm), so the rails droop under load (7.4 V further at Sag 10 than at 0 when driven hard). The LM3886 clips 3 V
  inside its rails.
- **REC.** The speaker voltage through a 1/20 divider into the active cab simulator (Sallen-Key high-pass, two MFB band-passes, two
  Sallen-Key low-passes), refitted through this circuit to the measured studio clean tone: 1.39 dB rms, 60 Hz - 12.5 kHz.

### Every potentiometer is 100k or less

So that production units can recall presets with MCP41HV51 digital pots (36 V, 100k max). Each network that had larger pots is the
original scaled by 1/10 in resistance and 10 in capacitance, driven by a DC-coupled rail-to-rail op-amp follower on the 18 V supply
(OPA2197 class) through the original source resistance / 10, so its transfer function is unchanged.

| network | original pots | now |
|---|---|---|
| rhythm stack + Volume + Clip LED branch | 250k / 250k / 25k, 1M, 470k | 25k / 25k / 2.5k, 100k, 47k |
| rhythm into Master | 1M | 100k |
| lead Drive | 1M | 100k |
| lead stack + Master | 220k / 1M / 22k, 1M | 22k / 100k / 2.2k, 100k |

Checks:
- The lead networks are identical in ngspice AC (`spice/lead_scaling.py`: 0.0000 dB difference, 30 Hz - 20 kHz).
- Rhythm, scaled against unscaled: the same fit to the studio tone (1.39 dB rms); level and distortion curves within 0.1 dB and 1%;
  -53 dB waveform residual clean. At Clip 6 it is -36 dB, because the LEDs now carry ten times the current at the same voltage (about
  0.1 V of knee shift).
- The lead keeps its level and distortion curves within 0.4 dB and 1.5%.

### Verified against ngspice

`spice/rhythm.py` writes the rhythm channel and power section with the scaled networks, the followers, the LEDs, the long-tailed pair and
the LM3886 as a rail-limited gain block. `hardware/verify/` holds the lead harness.
- Rhythm, 220 Hz at 20 mV-0.3 V, Clip 0 and 6: every node within 0.03 dB of ngspice; waveforms -49 to -58 dB residual.
- Rhythm small-signal response, 50 Hz - 16 kHz: within 0.35 dB.
- Lead preamp: within 0.23 dB small-signal; -25 to -59 dB residual on waveforms.

The JSFX in `jsfx/` is the earlier v0.1 model (behavioural gain stages, tanh power stage) with the exact LED solve. The plugin is the
reference.

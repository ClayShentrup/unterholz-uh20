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

## v0.3: one amp, one character (2026-11-27)

The lead channel is gone. There is one signal path, the v0.2 rhythm channel below, and one more knob, **ANTRIEB (Drive)**, which
switches a second cascaded J201 stage of the same family in between Q1 and the Mark III stack. From the studio clean tone it climbs to
edge of breakup, then crunch, then a saturated, singing lead, all through the same stack, LEDs, gain stages and power section.

- **Drive stage.** Q1's follower -> 1k -> 1 uF -> Drive pot (100k log; its bottom returns to ground through 1k) -> J201 gate
  (Rd 68k, Rs 1.5k || 22 uF, 18 V) -> rail-to-rail follower -> the stack's 6.8k feed, in place of Q1's follower. The 1k under the pot
  keeps the stage's input from ever reaching zero, so from about Drive 0.5 up the level stays near the clean tone's and the knob mostly
  adds gain and saturation (integrated loudness on a guitar part, Drive 0 -> 10: -8.0, -6.7 at 0.5, -4.2 at 1, -2.0 at 2, -1.0 at 3,
  0.0 at 5, +1.5 at 7, -1.1 LUFS at 10 as the stage saturates). The gate conducts when driven hard: the engine solves it against the
  network's Thevenin at the gate and injects that current back, so the coupling capacitor charges and the bias shifts as in the circuit.
- **Drive 0 is a switch.** At 0 (below 0.05) the pot's switch takes the stage out and Q1's follower drives the stack directly, exactly
  the v0.2 circuit: v0.3 at Drive 0 is bit-identical to v0.2's rhythm channel (`tests/`: the four v0.2 rhythm goldens are frozen and
  still match; the guitar DI through the v0.2 and v0.3 command-line renderers at seven settings compares byte for byte). The plug-in
  crossfades over about 85 ms when the switch moves while playing; the hardware switch would pop.
- **Harmonics.** 110 Hz at -30 dBFS, harmonics relative to the fundamental at the speaker: -43 dB at Drive 0, -38 at 1, -30 at 3,
  -26 at 5, -12 at 7, -10 dB at 10.
- **Clip** is the diode-clipping idea Marshall introduced in the JCM800 2205/2210 split-channel heads (1982) and carried into the
  Silver Jubilee 2555 (1987), which used LEDs. Here it is a red LED one way and two in series the other, behind a coupling capacitor at
  the stack input.

Against ngspice (`spice/rhythm.py ... drive=D` and `verify/compare.py`), 220 Hz, every node within 0.04 dB in level:

| AMP V, Clip, Drive | Drive stage | Q3 drain | speaker |
|---|---|---|---|
| 0.1, 0, 3 | -52.8 dB | -47.7 dB | -48.8 dB |
| 0.3, 5, 3 | -58.4 dB | -52.5 dB | -56.2 dB |
| 0.1, 5, 8 | -49.4 dB | -45.4 dB | -49.8 dB |
| 0.3, 0, 8 (gate conducting) | -38.3 dB | -36.6 dB | -38.8 dB |

### Sag is an active rheostat, set by a 100k pot (2026-11-27)

Sag was a 0-2.7 ohm power rheostat in each rail. It carries the supply current, so no digital pot could ever take its place. Each rail now
has an active rheostat instead: a MOSFET that an op-amp holds at 2.7 ohm x Sag/10 for any current, set by a 100k pot that carries less
than a microamp. It presents the rails with the same resistance, so the plug-in, which models the supply as 0.3 ohm of winding plus
2.7 ohm x Sag/10 ahead of the reservoirs, is unchanged (goldens byte-identical).

- **Circuit (+ rail; the - rail is its mirror with an N-channel IRF3205).** Bridge + -> 25 mOhm sense resistor (4-terminal, 3 W) ->
  IRF4905 P-channel MOSFET (source at the sense resistor, drain at the 4700 uF reservoir). A fixed divider across the whole element,
  100 ohm over 10.7k (0.1%), taps 1/108 of its voltage. The Sag pot, 100k linear (one gang per rail), sits across the sense resistor;
  its wiper, at g = Sag/10 from the bridge end, sits g x I x 25 mOhm below the bridge output. An OPA197 compares the two taps (1k into
  each input) and drives the gate through 47 ohm, forcing V/108 = g x I x 0.025 ohm: the element is 2.7 x g ohm. Because the divider
  spans the whole element, the FET's own resistance and the sense resistor's drop do not enter the result.
- **The controller floats on the bridge output.** Each op-amp runs from an isolated +/-12 V DC-DC (Murata NMA1512SC, from the 15 V
  supply) whose common is the bridge output, so its inputs sit within millivolts of mid-supply and the gate swings only by the FET's
  gate-source voltage (at most 12 V). The pot floats with it, inside that +/-12 V: a production MCP41HV51 fits there, with its digital
  lines through an isolator (ISO7741 class).
- **Sag 0 is the pot's switch.** It closes a relay (DPST, 10 mOhm contacts per pole) across both elements: a near-short, as the
  rheostat's end stop was. Between Sag 0 and about 0.15 the element bottoms out at its floor (25 mOhm + the FET's Rds(on), about 40 mOhm)
  instead of 2.7 x g.
- **One direction.** Current in each element only ever flows from the bridge into the reservoir (the bridge blocks the reverse), as in
  the rheostat; the FETs' body diodes point against it.
- **Dissipation** is the rheostat's: I^2 R of the charging pulses. With the LM3886 clipping hard into 8 ohm continuously, each element
  averages 6.3 W (+) / 7.3 W (-) at Sag 10, 5.2 / 5.7 W at Sag 7, 2.8 / 3.1 W at Sag 3, with peaks of 26 W inside a charging pulse
  (TO-220, on the amp's heatsink). The sense resistor takes about 1% of it.
- **Power-up.** The empty reservoirs charge through the elements: without help the FET takes 190-260 W for a few milliseconds (1.2-1.8 J
  in all). A standard soft-start, 47 ohm (10 W) in series with the primary shorted by a relay after 0.5 s, cuts that to 31-54 W
  (`spice/sag.py startup`). Once the relay closes it is out of the circuit.
- **Stability and accuracy.** Simulated at 50 ns steps through power-up and full clipping, the gate shows no ringing (its deviation from
  a 20 us running mean stays under 2 mV while conducting), and V/I across the element, whenever it carries more than 1 A, is
  2.700-2.704 ohm at Sag 10, 0.811-0.815 at Sag 3 and 0.136-0.141 at Sag 0.5 (targets 2.700, 0.810, 0.135).

`spice/supply.py` is the supply as netlist text (centre-tapped secondary, 0.3 ohm per half, bridge, the Sag element, 4700 uF per rail, the
LM3886 drawing its real rail currents); `spice/rhythm.py ... supply=rheo|active sag=S` runs the whole amp on it. The FETs are VDMOS fits
to their datasheets (threshold, Rds(on), gfs, capacitances) and the op-amp a 10 MHz, 20 V/us macro-model. `spice/sag.py compare` runs the
power section on its supply with each element, driven hard by a stepped 110 Hz tone (0.3, 1, 2, 4 V at the Master's output; the last two
clip the LM3886) and by four plucked low-E notes, and compares the active rheostat against the rheostat:

| Sag | + rail at rest -> lowest, rheostat / active (V) | rails, largest difference | speaker residual | speaker, largest difference |
|---|---|---|---|---|
| step 0 | 29.95 -> 26.82 / 29.94 -> 26.79 | 41.6 mV (relay contacts) | -117.0 dB | 2.8 mV |
| step 3 | 29.56 -> 24.04 / 29.55 -> 24.04 | 3.9 mV | -116.9 dB | 2.5 mV |
| step 7 | 29.15 -> 21.45 / 29.15 -> 21.44 | 3.5 mV | -88.7 dB | 17.7 mV |
| step 10 | 28.88 -> 20.06 / 28.88 -> 20.06 | 3.4 mV | -86.0 dB | 13.0 mV |
| pluck 0 | 29.95 -> 26.64 / 29.94 -> 26.61 | 43.4 mV (relay contacts) | -108.6 dB | 5.3 mV |
| pluck 3 | 29.56 -> 24.27 / 29.55 -> 24.27 | 3.9 mV | -107.4 dB | 5.2 mV |
| pluck 7 | 29.15 -> 22.03 / 29.15 -> 22.03 | 3.5 mV | -82.2 dB | 18.7 mV |
| pluck 10 | 28.88 -> 20.82 / 28.88 -> 20.81 | 3.4 mV | -82.7 dB | 40.2 mV |

The - rail matches the same way (Sag 10, step: -29.04 -> -19.38 V for both). The speaker swings about 25 V peak, so the largest
difference is 0.16% of it.

The v0.2 notes below are kept as the record; their LEAD parts describe the channel v0.3 removed.

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
  the Sag resistance (0-2.7 ohm; since v0.3 an active rheostat set by a 100k pot, above), so the rails droop under load (7.4 V further at Sag 10 than at 0 when driven hard). The LM3886 clips 3 V
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
| Drive (v0.3) | | 100k |
| Sag (v0.3) | 0-2.7 ohm power rheostat per rail, carrying the supply current | 100k linear, dual gang, setting an active rheostat per rail (above); it carries under a microamp and floats on the controller's isolated +/-12 V |
| v0.2 lead Drive (removed) | 1M | 100k |
| v0.2 lead stack + Master (removed) | 220k / 1M / 22k, 1M | 22k / 100k / 2.2k, 100k |

Checks:
- The v0.2 lead networks were identical in ngspice AC (0.0000 dB difference, 30 Hz - 20 kHz; the script left with the channel).
- Rhythm, scaled against unscaled: the same fit to the studio tone (1.39 dB rms); level and distortion curves within 0.1 dB and 1%;
  -53 dB waveform residual clean. At Clip 6 it is -36 dB, because the LEDs now carry ten times the current at the same voltage (about
  0.1 V of knee shift).
- The lead keeps its level and distortion curves within 0.4 dB and 1.5%.

### Verified against ngspice

`spice/rhythm.py` writes the rhythm channel and power section with the scaled networks, the followers, the LEDs, the long-tailed pair and
the LM3886 as a rail-limited gain block, and (v0.3) the Drive stage. `hardware/verify/` holds the harnesses.
- Rhythm, 220 Hz at 20 mV-0.3 V, Clip 0 and 6: every node within 0.03 dB of ngspice; waveforms -49 to -58 dB residual.
- Rhythm small-signal response, 50 Hz - 16 kHz: within 0.35 dB.
- Lead preamp: within 0.23 dB small-signal; -25 to -59 dB residual on waveforms.
- Sag: the active rheostat against the power rheostat on the real supply (`spice/sag.py compare`): rails within 4 mV at Sag 3-10 (43 mV
  at Sag 0, the relay's contacts), speaker -82 to -117 dB residual (table above).

The JSFX in `jsfx/` is the earlier v0.1 model (two channels, behavioural gain stages, tanh power stage) with the exact LED solve. The plugin is the
reference.

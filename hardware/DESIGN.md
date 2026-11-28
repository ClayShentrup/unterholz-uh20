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

## v0.4.1: the output, calibrated (2026-11-28)

The circuit is unchanged. What changed is how the plug-in reads its output jacks: one fixed gain at the very end of the chain, after
the circuit and with the output trim, like the input level of the interface the amp is recorded through. It is
`Engine::VFS` in `source/dsp/UH20Engine.h`: **2.4 V peak at the REC or AMP jack = 0 dBFS**, the output's counterpart of the input's
1.6 V (EINGANG). Up to v0.4.0 the two jacks were read differently, REC at 0.5 V = 0 dBFS and AMP at 2 V, which put the REC output
12 dB above its analog relationship to AMP and its peaks above full scale (a DAW's floating-point mix takes that; a browser or an
interface output clips it).

- **Why 2.4 V.** The REC jack has a ceiling: the LM3886 clipping at its rails, through the 1/20 divider and the cab simulator. The
  highest peak found over the four DIs (`tests/reference/signals`) and every corner of Treble, Middle, Bass, Presence, Bright and
  Shift at Drive, Volume and Master 10 (Sag and Clip 0, which only lower it) is 2.33 V. At 2.4 V = 0 dBFS that is -0.26 dBFS, so no
  setting clips with the trim at 0 dB, and the studio clean sits about 7.5 dB under full scale.
- **Levels on the puzzle-remains lead DI** (peak dBFS / integrated loudness of the mono output, LUFS; the plug-in writes it to both
  channels, which a stereo meter reads 3 LU higher):

  | setting | v0.4.0 REC | v0.4.1 REC | v0.4.0 AMP | v0.4.1 AMP |
  |---|---|---|---|---|
  | defaults | +5.9 / -8.3 | -7.7 / -22.0 | -6.5 / -17.5 | -8.1 / -19.1 |
  | Drive 8 | +11.9 / +0.9 | -1.8 / -12.7 | -4.2 / -8.2 | -5.7 / -9.8 |
  | Drive 10, Master 10 | +12.7 / +1.7 | -1.0 / -11.9 | -3.2 / -7.4 | -4.8 / -9.0 |

  REC changed by -13.62 dB (0.5 / 2.4) and AMP by -1.58 dB (2 / 2.4), exactly: every golden and reference render nulls against the
  v0.4.0 one times that gain to the float32 rounding of the files (-149 dB). The reference set's measurements moved by the same
  gain (third-octave bands to 0.0001 dB, loudness likewise except one quiet synthetic render, 0.03 LU, whose quietest blocks now fall
  under the loudness meter's absolute -70 LUFS gate).
- **Against the Hanwell.** Hanwell 1.2.0 at its defaults on the same DI: -7.2 dBFS peak, -18.0 LUFS (stereo). The UH-20's
  defaults on REC: -7.7 dBFS, -19.0 LUFS read the same way, 0.9 LU under it.
- **REC and AMP.** Both jacks now read with the same calibration, so their relationship is the circuit's: at the defaults the peaks
  are about equal (-7.7 and -8.1 dBFS) and AMP is 2.9 LU louder, the energy the cab simulator's high-pass and 4.9 kHz low-passes take
  out. A cabinet impulse response after AMP takes out about as much, so AMP into an IR lands near REC; AMP's hottest settings peak
  near -4.8 dBFS.
- **Not changed:** the JSFX (`jsfx/`), which keeps the v0.4.0 reading (REC 0.5 V, AMP 2 V = 0 dBFS); the published samples, which
  are loudness-matched, so a gain before the matching changes nothing in them.

## The engine, solved with fewer operations (2026-11-28)

The circuit is unchanged; the plug-in now solves it with about a third of the work. Every change is a different way of finding the
same root of the same equations, so the output moves only by rounding: against the reference engine the waveform residual is
-173 to -186 dB on the four DIs at 23 settings (`tools/reference_check.py --ref-cli`).

- **J201 stages.** The drain load is linear (a network port, i = G vd - J), so the drain voltage follows from the drain current and each
  stage is one equation in the current. It is solved by Newton with the model's own slope (`J201::idd`), from the current extrapolated
  over the last three samples, inside a bracket. The general two-unknown Newton with a numerical Jacobian is the fallback.
- **The long-tailed pair** is one equation in the tail voltage: each side's current is closed-form in saturation (the current is then
  linear in its drain voltage), and the slope follows by the implicit function theorem.
- **Stopping rules.** A step is final when the error it can leave, |f''| step^2 / 2|f'|, is below 1e-17 A (stages) or 1e-13 V (pair).
  That only applies when the step cannot leave saturation. Otherwise the step must fall under 1e-10 A or 1e-7 V. The bypassed stages
  are almost linear in their current, so they take one step.
- **Gate clamps.** Below a forward bias worked out once per knob move (`jk::gateOff`) a gate draws less than the original test's
  threshold, so the early-out returns the same answer without the junction's exponentials and power. A conducting gate is solved by
  Newton with the junction's analytic slope, starting from its last conducting sample.
- **Clip LEDs.** Newton on the diode drop, starting from the last sample's drop, instead of Lambert's W.
- **Supply.** The rectified 60 Hz comes from a rotating phasor that is re-anchored to the exact phase every cycle, and divisions became
  reciprocals.
- **Host rate.** The internal rate stays at about 384 kHz: 8x at 44.1/48 kHz, 4x at 88.2/96 kHz, 2x at 176.4/192 kHz.

`tests/uh20_tests.cpp` checks each analytic solver against the general one, sample after sample, over a hard noisy drive. The largest
differences are 5e-10 V (stage), 1e-11 V (pair), 1e-10 V (gate) and 8e-14 V (LEDs).

CPU, one Apple Silicon core, 20 s of the puzzle-remains lead DI at 48 kHz, 128-sample blocks: defaults 10.8% -> 3.4%, Drive 8 /
Master 9 21.1% -> 6.8%. In the browser engine (WebAssembly in Node): 18.3% -> 3.9% and 33.5% -> 8.1%.

**The reference engine.** `-DUH20_REFERENCE=1` (CMake targets `uh20_cli_ref`, `uh20_tests_reference`) builds the original general
solvers at 8x at every host rate. Its output is byte-identical to v0.3.1. It rendered `tests/reference/reference.json`, and every
faster engine is held to that set (`tools/reference_check.py`, in CI):
- third-octave bands from 50 Hz to 8 kHz within 0.5 dB
- loudness within 0.2 LU
- aliasing of 880/1318 Hz tones at the hot corners within amp_sanity's limit and no more than 3 dB above the reference's
- amp_sanity's touch and sweep checks at 0 FAIL

**Goldens.** Bit-identity is no longer the test for the frozen v0.2/v0.3.0 goldens (Drive 0, Sag 0). They still match at -152 dB,
which is the float32 floor of the stored files, and the -100 dB test is unchanged. "Identical to v0.2" now means identical to
rounding, plus the reference check. The reference build reproduces v0.3.1 byte for byte. Statements below that a setting is
"byte-identical to v0.3.0" were true when they were written. They now hold for the reference build, and to rounding for the plug-in.

**Lower oversampling did not pass.** Tone and loudness pass easily at 4x (worst band 0.16 dB). Aliasing does not pass at 4x or 6x,
and 7x is marginal:
- 4x: Drive 10 at 1318 Hz rises 17 dB, to -53.5 dB.
- 6x: Drive 10 rises 5-6 dB.
- 7x: passes at the two gate tones (+2.9 dB at the all-gain-10 corner). On more tones it rises 3.6-4.6 dB at Drive 10 (1046,
  1568 and 2093 Hz).

So 44.1/48 kHz stays at 8x.

## v0.3.1: a Drive that cannot block, a Sag that compresses (2026-11-27)

**What was wrong.** At Drive 10, v0.3.0 dropped out: on the puzzle-remains lead DI, 9% of the loud 10 ms frames (AMP output) fell more
than 6 dB under Drive 9, in 210 events up to 170 ms long, and the loud level fell 3.4 dB below Drive 9. The cause is the Drive stage's
gate. The full pot put all of Q1's swing (several volts) on a J201 whose gate conducts about 0.6 V above its source; each peak charged
the 1 uF coupling capacitor through the 1k feed, and it could only discharge through the ~100k pot, so the gate sat volts negative and
the stage was cut off for tens of milliseconds (blocking distortion). ngspice shows the same: after a 0.1 s strum at 10x the playing
level, the v0.3.0 Drive stage's drain sits at 18.00 V (off) from 0.10 to 0.15 s and the speaker falls from 17 V to 0.4 V rms. Q2 and Q3
never blocked. The rest of the knob had too much gain as well: the noise between notes was only 17.5 dB under the playing at Drive 10
(Hanwell at gain 10: 27.1 dB), and the stage had full bass gain ahead of three clipping stages.

**Drive stage, v0.3.1.** Q1's follower -> 1k -> **47 nF** -> **100k** -> Drive pot (100k log) -> **3.3k** to ground; wiper -> **47k
gate stopper** -> J201 (Rd 68k **|| 470 pF**, Rs 1.5k **|| 0.68 uF**, 18 V) -> rail-to-rail follower -> the stack's 6.8k feed.
- The 47k stopper limits the gate current when the gate conducts, and the 47 nF (instead of 1 uF) lets any bias shift recover in a few
  milliseconds, so a hard pick cannot cut the stage off. ngspice, same 10x strum: the drain's mean moves from 8.25 V to 9.05 V and back,
  and the stage never stops conducting.
- 100k above the pot and 3.3k below it set the range: the stage's input runs from 1/60 of Q1's output just past the switch to 1/2 at
  10, so Drive 10 is the most gain the design allows, and the low end of the knob starts just above the clean tone's level.
- 0.68 uF across the 1.5k source (Q1's own value) trims the lows ahead of the clipping, as the 0.68 uF cathode bypasses of the Marshall
  lead stages do; 470 pF across the 68k drain resistor rolls the stage off above about 5 kHz, taming the fizz after the clipping.
- Same J201, same 68k drain, same follower: the same voice getting hotter. Every pot is still 100k or less.
- Drive 0 is untouched (the stage is switched out): every Drive 0 render is byte-identical to v0.3.0 at Sag 0.

**Sag, v0.3.1.** v0.3's Sag was resistance in the supply (the 0-2.7 ohm rheostat, then the active rheostat below). Rails only matter
when the LM3886 is near clipping, so Sag changed the sound by 0.4 dB even at Master 10 / Volume 8; at Sag 10 and Master 10 it then
made the LM3886 clip hard on its rails (30% of the time on a hard 196 Hz note). Sag now works the way a valve amp's B+ sag does: the
power section's own ceiling comes down when the amp works hard.
- **Circuit.** A 0.1 ohm resistor in the speaker's return senses its current; a precision full-wave rectifier (OPA2197, gain 10) makes
  1 V per amp, smoothed by 2k / 1 uF (2 ms). The Sag pot (100k linear) takes Sag/10 of it into a gain of 4.8 on a 5 V rail (an OPA197:
  it limits at 5 V), then 100k / 0.68 uF (68 ms) and a buffer. A 30k V-to-I and a matched PNP mirror (BCM62B) source that control
  voltage / 30k, up to 0.17 mA, into the long-tailed pair's tail node. The pair then carries up to a third less current, and its swing,
  tail current x drain resistor, is the power section's ceiling: hard playing pulls it down by up to 3.5 dB and it swells back over
  about 70 ms as the playing eases. Soft playing draws little current, so Sag leaves it alone.
- **Sag 0** turns the transconductor off (the pot's wiper at 0 V): no current, and the plug-in at Sag 0 is byte-identical to v0.3.0.
- The supply is the plain unregulated one again (0.3 ohm of winding per half, 4700 uF per rail). The active rheostat is **not built**:
  with the pair setting the ceiling, the LM3886 no longer reaches even sagged rails (Sag 10, Master 10, Volume 10, Drive 10, 196 Hz at
  -6 dBFS: 12.9 V peak at the speaker with the rheostat's rails at 19 V; the speaker's peak, THD and high-harmonic share are the same with
  and without it), so it is redundant. Its section below, `spice/supply.py` and `spice/sag.py` are kept as the record.
- The LM3886 is kept off its rails by the gain staging: on the 196 Hz sine at -15 and -6 dBFS, at every Drive sample setting (Drive 0-10,
  Clip 0 and 5) and at Volume 10 / Master 10 with Drive 0 and 10, the speaker peaks at 11.9-20.3 V against a limit of 23.8-25.4 V and
  never touches it (v0.3.0 touched it at Drive 10).

**Measured, puzzle-remains lead DI** (42 s, Volume 5, Treble 4, Middle 7, Bass 5, Presence 6, Master 6.5, Clip 0; 10 ms frames;
loud = DI frames within 20 dB of the DI's 95th percentile; dropout = a loud frame more than 6 dB under the next-lower Drive step;
crest = median frame peak / rms over loud frames; noise = loud median minus the median of frames where the DI is 45 dB down):

| Drive | loud dB, AMP v0.3.0 -> v0.3.1 | dropouts | crest dB | loud - noise dB | loud dB, REC v0.3.0 -> v0.3.1 | loud - noise dB, REC |
|---|---|---|---|---|---|---|
| 0 | -17.2 -> -17.2 | 0 / 0 | 7.2 / 7.2 | 47.3 / 47.3 | -8.2 -> -8.2 | 47.5 / 47.5 |
| 2 | -10.3 -> -12.9 | 0 / 0 | 3.7 / 6.0 | 44.1 / 47.7 | -1.3 -> -4.4 | 44.8 / 47.6 |
| 4 | -8.6 -> -10.6 | 0 / 0 | 2.5 / 4.1 | 40.7 / 45.9 | 0.3 -> -2.0 | 41.2 / 46.2 |
| 6 | -7.6 -> -8.1 | 0 / 0 | 2.6 / 2.2 | 31.3 / 39.4 | 1.5 -> 0.6 | 32.3 / 40.0 |
| 8 | -7.2 -> -7.5 | 0 / 0 | 2.8 / 2.2 | 25.1 / 32.9 | 1.9 -> 1.3 | 25.7 / 34.0 |
| 10 | -10.8 -> -7.4 | **8.9%, 210 events, 170 ms** / 0 | **7.7** / 2.4 | **17.5** / 29.9 | -2.1 -> 1.6 | 18.4 / 30.6 |

Hanwell at gain 10 on the same DI: 27.1 dB (cab off), 29.9 dB (cab on). The full table, every step and Clip 5, is in the v0.3.1 release
notes.

Against ngspice (`verify/compare.py`, 220 Hz, panel at the studio-clean settings), every node within 0.03 dB in level:

| AMP V, Clip, Drive | Drive stage (Q1 at Drive 0) | Q3 drain | speaker |
|---|---|---|---|
| 0.1, 0, 0 (Drive out) | -58.4 dB | -49.7 dB | -49.3 dB |
| 0.1, 0, 3 | -60.8 dB | -51.4 dB | -50.9 dB |
| 0.3, 5, 3 | -52.7 dB | -50.8 dB | -51.5 dB |
| 0.1, 5, 8 | -46.4 dB | -45.0 dB | -45.3 dB |
| 0.3, 0, 8 (gate conducting) | -45.4 dB | -40.8 dB | -43.7 dB |
| 0.3, 0, 10 (gate conducting) | -44.8 dB | -41.5 dB | -44.3 dB |
| 0.1, 0, 10, first 0.1 s at 10x (the blocking test), from 0.1 s | -45.5 dB | -42.6 dB | -42.3 dB |
| 0.3, 0, 0, Sag 10, from 0.1 s | -58.5 dB | -49.9 dB | -51.6 dB |
| 0.1, 0, 8, Sag 10, from 0.1 s | -46.6 dB | -44.4 dB | -42.8 dB |

In the blocking test the Drive stage's mean is 9.05 V (SPICE) / 9.06 V (plug-in) at 0.10-0.12 s and 8.25 / 8.25 V at 0.4-0.5 s.

## v0.3: one amp, one character (2026-11-27)

The lead channel is gone. There is one signal path, the v0.2 rhythm channel below, and one more knob, **ANTRIEB (Drive)**, which
switches a second cascaded J201 stage of the same family in between Q1 and the Mark III stack. From the studio clean tone it climbs to
edge of breakup, then crunch, then a saturated, singing lead, all through the same stack, LEDs, gain stages and power section.

- **Drive stage** (v0.3.0; v0.3.1 above replaced its values). Q1's follower -> 1k -> 1 uF -> Drive pot (100k log; its bottom returns to ground through 1k) -> J201 gate
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

(Superseded in v0.3.1, above: Sag now acts on the long-tailed pair and the active rheostat is not built. Kept as the record.)

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
| Sag (v0.3.1) | | 100k linear, a divider on the rectified speaker-current signal (v0.3.1 section); the active rheostat is not built |
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

# Unterholz UH-20

![The UH-20 panel](docs/screenshot.png)

**A JFET guitar amp, published as an open circuit and as a plugin that simulates that circuit.**

**[Play it in your browser](https://clayshentrup.github.io/unterholz-uh20/#play)** · **[Listen to samples](https://clayshentrup.github.io/unterholz-uh20/#samples)** · **[Download](https://github.com/ClayShentrup/unterholz-uh20/releases/latest)**

The UH-20 is one amp with one character. At its reference settings it is a studio-preamp-style clean tone: glassy, with a sweet second harmonic that grows smoothly as you dig in. One knob, **ANTRIEB** (drive), switches in another J201 gain stage ahead of the tone stack and takes the same amp from edge of breakup through crunch to a saturated, singing lead.

The preamp feeds a class AB power section: a J201 long-tailed pair driving an LM3886, with global feedback (Presence lives in that feedback network) and Sag, a transconductor that takes tail current from the pair as the speaker current rises, so hard playing pulls the power section's ceiling down. The output is either an analog cab simulation (REC) or the bare amp (AMP).

The goal is a real, buildable amp made of ordinary parts: JFETs, an LM3886, op-amps for the cab simulator, LEDs, resistors, capacitors and pots. The plugin is a simulation of exactly that circuit, so you can hear and play the design before building it.

## Download

Get the latest build from **[Releases](https://github.com/ClayShentrup/unterholz-uh20/releases/latest)**:

| Platform | Formats |
|---|---|
| macOS (Apple Silicon + Intel) | VST3, AU, CLAP, standalone app |
| Windows (64-bit) | VST3, CLAP, standalone app |

On macOS you can also install with Homebrew:

```bash
brew tap clayshentrup/tap
brew trust clayshentrup/tap
brew install --cask unterholz-uh20
```

The installer package asks for your Mac password.

The macOS builds are not notarized yet; `INSTALL.txt` in the download explains the one-time step.

[`jsfx/Unterholz UH-20.jsfx`](jsfx/) is the earlier v0.1 model for REAPER (two channels, behavioural gain stages and power stage, with the exact LED solve). The plugin is the circuit simulation and the reference.

## Play it in your browser

**[clayshentrup.github.io/unterholz-uh20/#play](https://clayshentrup.github.io/unterholz-uh20/#play)**, on the main page: the amp itself, not a recording of it. The engine ([`source/dsp`](source/dsp)) is compiled to WebAssembly unchanged and runs in an AudioWorklet, so the panel's knobs and switches (names, ranges and defaults as in the plugin) change the sound live. The input is a dry guitar recording from the picker or your own guitar through an audio interface (headphones, please). The knob settings live in the address, so a sound can be shared as a link.

- **Build.** `tools/build_web.sh` compiles [`web/uh20_web.cpp`](web/uh20_web.cpp) (the C API: `amp_create(sampleRate)`, `amp_set_param(amp, id, value)`, `amp_process(amp, in, outL, outR, n)`, plus `amp_snapshot` / `amp_restore` for buffered playback) with Emscripten at -O3, no fast-math, into `docs/play/uh20.wasm`, and records a hash of the sources in `docs/play/build.json`; CI checks it.
- **Parity.** `node tools/web_parity.mjs --in guitar.wav` renders seven settings ([`web/parity.json`](web/parity.json)) through the WebAssembly engine and through `uh20_cli`. Against the same source built with `-ffp-contract=off` (`--limit -140`) they agree to the float output's rounding, about -146 dB relative to the peak or better. Against the shipping arm64 build the limit is -100 dB: there the compiler fuses multiply-adds, which WebAssembly can't, and with every knob at 10 the circuit's gain carries those last-bit differences to about -103 dB. CI runs both.
- **The audio thread and buffered playback.** `node tools/worklet_test.mjs` runs the player's AudioWorklet in Node with a simulated clock: knob changes reach the engine within 100 ms at up to 95% load, and buffered playback (for devices too slow for real time: the engine renders ahead in a Web Worker and a knob change re-renders from the playhead) matches real time sample for sample. CI runs it.
- **Level.** The player uses the plugin's own output level (trim 0 dB): since v0.4.1 the output is calibrated so the defaults peak about 7.5 dB under full scale and the hottest settings stay under 0 dBFS, which a browser needs (it clips above it).
- **The panel.** The player in [`docs/play/player/`](docs/play/player) is shared with the other amps; this amp's parameters, labels and the panel details it doesn't draw (English names under the German ones, the REC / AMP pair, the orange pointers) are in [`docs/play/amp.js`](docs/play/amp.js), and the plugin's flat light look is [`docs/play/uh20.css`](docs/play/uh20.css). The old `/play/` address redirects to the main page with its settings.

## Samples

**[Listen to them in your browser](https://clayshentrup.github.io/unterholz-uh20/#samples)**, one player per sample, or open any file below. Each file is level-matched to -18 LUFS with only a peak limiter (no compression), except the two SAG series (17-20, 21-23): each shares the gain of its Sag 0 file, so Sag's squeeze is audible. The intended offsets are in [`docs/samples/levels.json`](docs/samples/levels.json).

One guitar part, the lead from Broken Ladder's *the puzzle remains* (a Gretsch Duo Jet, recorded straight in), through the UH-20 at 16 settings, with the REC (analog cab) output. The only other processing is light compression after the amp (at most 3 dB) and level matching. Each file is named by its knobs. The first is the setting used on the record; 05-10 sweep ANTRIEB from 0 to 10 with Clip off, and 11-16 repeat the sweep at Clip 5.

| | Settings |
|---|---|
| 01 | [Clean - volume 5, treble 3, middle 7, bass 5, presence 6, master 6.5 (the puzzle remains)](docs/samples/01%20Clean%20-%20volume%205%2C%20treble%203%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5%20%28the%20puzzle%20remains%29.opus) |
| 02 | [Clean - volume 3, treble 5, middle 6, bass 4, presence 5, master 6.5, bright](docs/samples/02%20Clean%20-%20volume%203%2C%20treble%205%2C%20middle%206%2C%20bass%204%2C%20presence%205%2C%20master%206.5%2C%20bright.opus) |
| 03 | [Crunch - volume 7, treble 4, middle 7, bass 5, presence 6, master 7.5](docs/samples/03%20Crunch%20-%20volume%207%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%207.5.opus) |
| 04 | [Heavy crunch - volume 8, treble 4, middle 7, bass 5, presence 6, master 8, sag 7](docs/samples/04%20Heavy%20crunch%20-%20volume%208%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%208%2C%20sag%207.opus) |
| 05 | [Clean - drive 0, clip 0, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/05%20Clean%20-%20drive%200%2C%20clip%200%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 06 | [Clean - drive 2, clip 0, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/06%20Clean%20-%20drive%202%2C%20clip%200%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 07 | [Edge of breakup - drive 4, clip 0, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/07%20Edge%20of%20breakup%20-%20drive%204%2C%20clip%200%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 08 | [Crunch - drive 6, clip 0, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/08%20Crunch%20-%20drive%206%2C%20clip%200%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 09 | [Lead - drive 8, clip 0, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/09%20Lead%20-%20drive%208%2C%20clip%200%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 10 | [Lead - drive 10, clip 0, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/10%20Lead%20-%20drive%2010%2C%20clip%200%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 11 | [Clean - drive 0, clip 5, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/11%20Clean%20-%20drive%200%2C%20clip%205%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 12 | [Clean - drive 2, clip 5, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/12%20Clean%20-%20drive%202%2C%20clip%205%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 13 | [Edge of breakup - drive 4, clip 5, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/13%20Edge%20of%20breakup%20-%20drive%204%2C%20clip%205%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 14 | [Crunch - drive 6, clip 5, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/14%20Crunch%20-%20drive%206%2C%20clip%205%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 15 | [Lead - drive 8, clip 5, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/15%20Lead%20-%20drive%208%2C%20clip%205%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 16 | [Lead - drive 10, clip 5, volume 5, treble 4, middle 7, bass 5, presence 6, master 6.5](docs/samples/16%20Lead%20-%20drive%2010%2C%20clip%205%2C%20volume%205%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5.opus) |
| 17 | [Heavy crunch, sag 0 - volume 7, treble 4, middle 7, bass 5, presence 6, master 9](docs/samples/17%20Heavy%20crunch%2C%20sag%200%20-%20volume%207%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%209.opus) |
| 18 | [Heavy crunch, sag 3 - volume 7, treble 4, middle 7, bass 5, presence 6, master 9](docs/samples/18%20Heavy%20crunch%2C%20sag%203%20-%20volume%207%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%209.opus) |
| 19 | [Heavy crunch, sag 6 - volume 7, treble 4, middle 7, bass 5, presence 6, master 9](docs/samples/19%20Heavy%20crunch%2C%20sag%206%20-%20volume%207%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%209.opus) |
| 20 | [Heavy crunch, sag 10 - volume 7, treble 4, middle 7, bass 5, presence 6, master 9](docs/samples/20%20Heavy%20crunch%2C%20sag%2010%20-%20volume%207%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%209.opus) |
| 21 | [Lead, sag 0 - drive 6, clip 3, volume 5, treble 5, middle 6, bass 4, presence 6, master 8](docs/samples/21%20Lead%2C%20sag%200%20-%20drive%206%2C%20clip%203%2C%20volume%205%2C%20treble%205%2C%20middle%206%2C%20bass%204%2C%20presence%206%2C%20master%208.opus) |
| 22 | [Lead, sag 5 - drive 6, clip 3, volume 5, treble 5, middle 6, bass 4, presence 6, master 8](docs/samples/22%20Lead%2C%20sag%205%20-%20drive%206%2C%20clip%203%2C%20volume%205%2C%20treble%205%2C%20middle%206%2C%20bass%204%2C%20presence%206%2C%20master%208.opus) |
| 23 | [Lead, sag 10 - drive 6, clip 3, volume 5, treble 5, middle 6, bass 4, presence 6, master 8](docs/samples/23%20Lead%2C%20sag%2010%20-%20drive%206%2C%20clip%203%2C%20volume%205%2C%20treble%205%2C%20middle%206%2C%20bass%204%2C%20presence%206%2C%20master%208.opus) |
| 24 | [Dark clean - volume 4, treble 2, middle 5, bass 6, presence 3, master 6.5, shift](docs/samples/24%20Dark%20clean%20-%20volume%204%2C%20treble%202%2C%20middle%205%2C%20bass%206%2C%20presence%203%2C%20master%206.5%2C%20shift.opus) |
| 25 | [Bright clean - volume 4, treble 7, middle 3, bass 5, presence 7, master 6.5, bright](docs/samples/25%20Bright%20clean%20-%20volume%204%2C%20treble%207%2C%20middle%203%2C%20bass%205%2C%20presence%207%2C%20master%206.5%2C%20bright.opus) |
| 26 | [Mid lead - drive 8, volume 5, treble 3, middle 9, bass 3, presence 5, master 7, sag 4](docs/samples/26%20Mid%20lead%20-%20drive%208%2C%20volume%205%2C%20treble%203%2C%20middle%209%2C%20bass%203%2C%20presence%205%2C%20master%207%2C%20sag%204.opus) |

## The knobs

Point at any control in the plugin and the strip at the bottom explains it.

| Knob | What it does to the sound | What it is in the circuit |
|---|---|---|
| **LAUT** (volume) | Preamp gain: clean up to about 5, edge of breakup at 6-7, crunch above. | 100k log pot after the tone stack, feeding the second J201 gain stage through 220 nF. |
| **ANTRIEB** (drive) | 0 = the clean amp; about 2 = edge of breakup; 5 = crunch; 8-10 = a saturated, singing lead. | A cascaded J201 stage (68k ‖ 470 pF drain, 1.5k ‖ 0.68 µF source) ahead of the tone stack, fed through 47 nF, a 100k log pot between 100k and 3.3k, and a 47k gate stopper; at 0 a switch takes it out of the circuit. |
| **HÖHEN** (treble) | Top end; since the stack sits before the last gain stages, more treble also drives them harder. | 25k treble pot and 2.5 nF of the Mark III-style tone stack. |
| **MITTEN** (middle) | Midrange body: low = scooped, high = fuller and pushier. | 2.5k mid pot at the foot of the stack. |
| **TIEFEN** (bass) | Low end; with high LAUT or ANTRIEB keep it at 5 or below, or the lows get loose. | 25k log bass pot with the stack's 1 µF and 0.47 µF. |
| **PRÄSENZ** (presence) | Upper-mid bite: 0 = smooth and dark, 10 = cutting. | 5k pot and 0.1 µF in the power amp's global feedback leg. |
| **CLIP** (LED clip) | LEDs flatten the peaks: more squash and sustain, thicker because the clipping is asymmetric; 0 = off. It is the diode-clipping idea Marshall introduced in the JCM800 2205/2210 split-channel heads (1982) and carried into the Silver Jubilee 2555 (1987), which used LEDs. | Dual-gang reverse-log pot: 47k in series with the LEDs (a red LED one way, two the other, behind a coupling capacitor at the stack input) and 100k bypassing part of the first stage's source resistor, so the stage drives them harder; 0 switches the branch out. |
| **DURCHHANG** (sag) | Hard playing pulls the power section's ceiling down by up to 3.5 dB and it swells back over about 70 ms as you ease off; soft playing is left alone; 0 = off. | A transconductor driven by the speaker current (0.1 Ω sense, rectified, 100k linear pot, 68 ms smoothing) takes up to a third of the long-tailed pair's tail current. |
| **SUMME** (master) | Level into the power section: 6.5 is the reference clean tone; higher adds power-amp crunch and compression. | 100k log pot between the last gain stage's follower and the J201 long-tailed pair that drives the LM3886. |

| Switch / setting | What it does to the sound | What it is in the circuit |
|---|---|---|
| **HELL** (bright) | Sparkle at low LAUT settings; it fades out as LAUT goes up. | 1 nF from the Volume pot's top to its wiper. |
| **TIEF** (bass shift) | Fatter, warmer first stage; a little looser. | 22 µF added across the first J201's 1.5k source resistor. |
| **REC / AMP** (output) | REC: an analog cabinet simulation, for recording straight in. AMP: no cabinet, for a real cabinet or your own impulse response. | The speaker voltage through a 1/20 divider; REC adds the active cab-sim filters (Sallen-Key high-pass, two band-passes, two Sallen-Key low-passes). |
| **EINGANG** (input) | How many volts a full-scale input means (default 1.6 V = 0 dBFS); raise it for a hot interface, lower it for a quiet one. | The plug-in's calibration of the input jack, not a part of the circuit. |
| **TRIM** (output trim) | Output level after the amp, ±24 dB. At 0 dB the REC and AMP jacks are read at 2.4 V = 0 dBFS: the defaults peak near -8 dBFS, the hottest settings just under 0. | The plug-in's output gain, not a part of the circuit. |

Marshall, JCM800 and Silver Jubilee are trademarks of Marshall Amplification plc, and Mark III is a Mesa/Boogie model. Unterholz is not affiliated with or endorsed by either company; the names only describe where circuit ideas come from.

## How it is made

The plugin simulates the circuit part by part, in volts ([`source/dsp/JfetKit.h`](source/dsp/JfetKit.h), [`source/dsp/UH20Engine.h`](source/dsp/UH20Engine.h)):

- every J201 is the vendor SPICE model, solved each sample together with its real source network, its drain load and gate conduction (Newton with the model's analytic slope; see [`hardware/DESIGN.md`](hardware/DESIGN.md) for how the solvers are held to the original general ones);
- tone stacks, volume and master pots, coupling and feedback networks are linear networks solved exactly;
- the LED clipper is solved exactly (Newton on its exact equation, checked against Lambert W);
- the power section is a J201 long-tailed pair into an LM3886 (class AB) limited by its rails, with global feedback compensated by 220 pF on the pair's output (measured gain margin at least 10 dB at every setting), rails from a 60 Hz rectifier and reservoir capacitors, and the Sag transconductor on the pair's tail;
- it runs at 8x oversampling at 44.1/48 kHz (4x at 88.2/96 kHz, 2x at 176.4/192 kHz: the same internal rate) with 8th-order anti-aliasing filters.

It is checked against ngspice on the same netlists ([`hardware/verify/`](hardware/verify/), [`hardware/spice/rhythm.py`](hardware/spice/rhythm.py)):

| | |
|---|---|
| preamp + power section, Drive 0, 220 Hz at 20 mV-0.3 V, Clip 0 and 6 | every node within 0.04 dB of ngspice, waveforms -47 to -62 dB residual |
| with the Drive stage (v0.3.1), Drive 3, 8 and 10, Clip 0 and 5, 220 Hz at 0.1-0.3 V, and a 10x strum into Drive 10 (the blocking test) | every node within 0.03 dB, waveforms -41 to -61 dB residual |
| Sag (v0.3.1), Sag 5 and 10, Drive 0 and 8 | every node within 0.03 dB, waveforms -43 to -59 dB residual |
| small-signal response, 50 Hz - 16 kHz | within 0.35 dB |
| v0.3's active rheostat against a 0-2.7 Ω power rheostat (not built since v0.3.1, kept as the record), Sag 0/3/7/10 ([`sag.py`](hardware/spice/sag.py)) | rails within 4 mV (43 mV at Sag 0, the bypass relay's contacts), speaker -82 to -117 dB residual |

The REC cab simulator (active filters) is fitted through this circuit to a measured studio preamp's clean tone: 1.39 dB rms from 60 Hz to 12.5 kHz at Volume 5, Treble 3, Middle 7, Bass 5, Presence 5, Master 6.5.

The design history is in [`hardware/`](hardware/): SPICE netlists of the classic tube stages from published schematic values ([`REFERENCE_CIRCUITS.md`](hardware/REFERENCE_CIRCUITS.md)), and each stage's JFET translation, chosen so its transfer curve matches the tube stage's shape. [`hardware/DESIGN.md`](hardware/DESIGN.md) tracks the design and its status.

## Building from source

```bash
git clone --recursive https://github.com/ClayShentrup/unterholz-uh20
cd unterholz-uh20
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target UnterholzUH20_All UnterholzUH20_CLAP
```

You need CMake 3.22+ and a C++20 compiler. JUCE and the CLAP extensions come in as git submodules. Every push is built on GitHub Actions, and every `v*` tag publishes a release.

**Tests.** `ctest --test-dir build -C Release` runs [`tests/uh20_tests.cpp`](tests/uh20_tests.cpp) on every CI build: golden input/output pairs (short plucked-string signals whose output must match [`tests/golden`](tests/golden) to -100 dB, so any change to the sound fails; the ones recorded by v0.2.0 and v0.3.0 at Sag 0 are frozen, which proves Drive 0 is still exactly the v0.2 amp), a check that every solver converges (a 1 ppm input change must move the output by no more than -95 dB), hard high notes that must stay free of subharmonics and inside an aliasing budget, the Clip LED solve against its exact equation, the power amp's feedback loop settling at every Presence and Master setting, Sag squashing a hard note and leaving soft playing alone, no dropout after a 0 dBFS strum at Drive 6-10 (the v0.3.0 blocking), knob moves and the Drive switch that must not click, Drive adding harmonics steadily from 0 to 10, and extreme settings that must stay finite and fall silent. The frozen goldens are matched to rounding (-152 dB, the float32 floor of the files), no longer bit for bit, since the engine's solvers became analytic; since v0.4.1 they are compared with the output calibration's fixed gain applied (they keep the level they were recorded at). `uh20_tests_reference` runs the same tests on the reference engine. When the sound is meant to change, `uh20_tests --update` rewrites the Drive goldens in the same commit (never the frozen ones).

**Reference set.** [`tools/reference_check.py`](tools/reference_check.py) holds the engine to renders of the reference engine (`-DUH20_REFERENCE=1`, the target `uh20_cli_ref`: 8x and the original general solvers). [`tests/reference`](tests/reference) holds 20 s excerpts of four guitar DIs and the measured features of 23 settings on them and on two synthetic signals. The engine must stay within 0.5 dB in every third-octave band from 50 Hz to 8 kHz and within 0.2 LU in loudness. Its aliasing on 880/1318 Hz tones at the hot corners must stay within amp_sanity's limit and no more than 3 dB above the reference's, and amp_sanity's touch and sweep checks must pass. CI runs `reference_check.py --cli build/uh20_cli_ref --tight --no-sanity` (the reference still reproduces its set) and `reference_check.py --cli build/uh20_cli --ref-cli build/uh20_cli_ref --quick`. `tools/uh20_bench` measures CPU. `tools/render_samples.py` rebuilds the listening samples (Opus, 128 kbps VBR, from the float renders).

**Sample files.** [`tools/check_samples.py`](tools/check_samples.py) checks every published sample on each CI build: Opus at 48 kHz in two channels (neither silent or flipped), within 1 LU of -18 LUFS, true peak at or below -1 dBTP, no quiet holes, and lengths that match. Run it on the live page too: `python3 tools/check_samples.py https://clayshentrup.github.io/unterholz-uh20/`.

**Sanity suite.** [`tools/amp_sanity.py`](tools/amp_sanity.py) runs the offline renderer through checks for the faults that make an amp sound broken rather than different: blocking (dropouts as a gain knob rises), level collapse, compression running backwards, hiss brought up into the playing, an amp that no longer cleans up when you pick softly, aliasing on the top notes, self-oscillation, DC and ringing tails at extreme settings, level jumps between knob steps, dead knobs, and 44.1 / 48 / 96 kHz disagreeing. Every signal is synthetic, so CI runs it on each build: `python3 tools/amp_sanity.py tests/sanity/uh20.json --cli build/uh20_cli` (add `--quick` for the short version). Limits are at the top of the file; WARN lines are reported but don't fail the build.

## License

[AGPL-3.0-or-later](LICENSE). The plugin is built with [JUCE](https://juce.com) under its AGPLv3 option, with [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) for CLAP.

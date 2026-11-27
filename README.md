# Unterholz UH-20

![The UH-20 panel](docs/screenshot.png)

**A JFET guitar preamp, published as an open circuit and as a plugin that simulates that circuit.**

The UH-20 has two channels:

- **RHYTHMUS**: a studio-preamp-style clean channel. Glassy, with a sweet second harmonic that grows smoothly as you dig in.
- **LEAD**: a Marshall-style drive channel. It sweeps from edge-of-breakup crunch through creamy, JTM45-style crunch to saturation.

Both channels feed a class AB power section: a J201 long-tailed pair driving an LM3886, with global feedback (Presence lives in that feedback network) and an unregulated supply whose series resistance is the Sag control. The output is either an analog cab simulation (REC) or the bare amp (AMP).

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

[`jsfx/Unterholz UH-20.jsfx`](jsfx/) is the earlier v0.1 model for REAPER (behavioural gain stages and power stage, with the exact LED solve). The plugin is the circuit simulation and the reference.

## Samples

**[Listen to them in your browser](https://clayshentrup.github.io/unterholz-uh20/#samples)**, one player per sample, or open any file below.

One guitar part, the lead from Broken Ladder's *the puzzle remains* (a Gretsch Duo Jet, recorded straight in), through the UH-20 at ten settings, with the REC (analog cab) output. The only other processing is light compression after the amp (at most 3 dB) and level matching. Each file is named by its knobs. The first is the setting used on the record.

| | Settings |
|---|---|
| 01 | [Rhythm - volume 5, treble 3, middle 7, bass 5, presence 6, master 6.5 (the puzzle remains)](docs/samples/01%20Rhythm%20-%20volume%205%2C%20treble%203%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%206.5%20%28the%20puzzle%20remains%29.opus) |
| 02 | [Rhythm - volume 3, treble 5, middle 6, bass 4, presence 5, master 6.5, bright](docs/samples/02%20Rhythm%20-%20volume%203%2C%20treble%205%2C%20middle%206%2C%20bass%204%2C%20presence%205%2C%20master%206.5%2C%20bright.opus) |
| 03 | [Rhythm - volume 7, treble 4, middle 7, bass 5, presence 6, master 7.5](docs/samples/03%20Rhythm%20-%20volume%207%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%207.5.opus) |
| 04 | [Rhythm - volume 8, treble 4, middle 7, bass 5, presence 6, master 8, sag 7](docs/samples/04%20Rhythm%20-%20volume%208%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%208%2C%20sag%207.opus) |
| 05 | [Rhythm - volume 6, treble 4, middle 7, bass 5, presence 6, master 7, clip 5](docs/samples/05%20Rhythm%20-%20volume%206%2C%20treble%204%2C%20middle%207%2C%20bass%205%2C%20presence%206%2C%20master%207%2C%20clip%205.opus) |
| 06 | [Lead - drive 3, treble 5, middle 6, bass 5, presence 5, master 6.5](docs/samples/06%20Lead%20-%20drive%203%2C%20treble%205%2C%20middle%206%2C%20bass%205%2C%20presence%205%2C%20master%206.5.opus) |
| 07 | [Lead - drive 5, treble 5, middle 6, bass 5, presence 5, master 6.5](docs/samples/07%20Lead%20-%20drive%205%2C%20treble%205%2C%20middle%206%2C%20bass%205%2C%20presence%205%2C%20master%206.5.opus) |
| 08 | [Lead - drive 7, treble 5, middle 7, bass 4, presence 6, master 6.5, clip 4](docs/samples/08%20Lead%20-%20drive%207%2C%20treble%205%2C%20middle%207%2C%20bass%204%2C%20presence%206%2C%20master%206.5%2C%20clip%204.opus) |
| 09 | [Lead - drive 9, treble 6, middle 6, bass 5, presence 7, master 7, sag 5](docs/samples/09%20Lead%20-%20drive%209%2C%20treble%206%2C%20middle%206%2C%20bass%205%2C%20presence%207%2C%20master%207%2C%20sag%205.opus) |
| 10 | [Lead - drive 6, treble 6, middle 5, bass 6, presence 5, master 6.5, bass shift](docs/samples/10%20Lead%20-%20drive%206%2C%20treble%206%2C%20middle%205%2C%20bass%206%2C%20presence%205%2C%20master%206.5%2C%20bass%20shift.opus) |

## The controls

Point at any control in the plugin and the strip at the bottom explains it.

| Knob | What it does |
|---|---|
| **LAUT** (rhythm volume) | Clean-channel gain. Clean up to about 5, edge of breakup at 6-7 |
| **ANTRIEB** (lead drive) | Lead-channel gain: low = crunch, about 5 = creamy crunch, 8-10 = saturated |
| **HÖHEN / MITTEN / TIEFEN** | Treble / middle / bass. Before the gain in RHYTHMUS, after the distortion in LEAD |
| **PRÄSENZ** (presence) | Upper-mid bite: a 0.1 µF + 5k network in the power amp's feedback loop |
| **CLIP** | LEDs clip the peaks: flatter tops, more squash and sustain, thicker because the clipping is asymmetric. 0 = off |
| **DURCHHANG** (sag) | Series resistance in the power supply: under hard chords the rails droop, loud notes squash, then bloom back |
| **SUMME** (master) | 1M log master into the power section. 6.5 is the reference clean tone |

| Switch | What it does |
|---|---|
| **RHYTHMUS / LEAD** | Channel select |
| **HELL** | Bright cap on the rhythm volume |
| **TIEF** | Bass shift into the first stage |
| **REC / AMP** | Analog cab-sim output, or no cab (for a real cabinet or your own impulse response) |

## How it is made

The plugin simulates the circuit part by part, in volts ([`source/dsp/JfetKit.h`](source/dsp/JfetKit.h), [`source/dsp/UH20Engine.h`](source/dsp/UH20Engine.h)):

- every J201 is the vendor SPICE model, solved each sample together with its real source network, its drain load and gate conduction;
- tone stacks, volume and master pots, coupling and feedback networks are linear networks solved exactly;
- the LED clipper is solved exactly (Lambert W);
- the power section is a J201 long-tailed pair into an LM3886 (class AB) limited by its rails, with global feedback compensated by 220 pF on the pair's output (measured gain margin at least 10 dB at every setting), and rails from a 60 Hz rectifier, reservoir capacitors and the Sag resistance;
- it runs at 8x oversampling with 8th-order anti-aliasing filters.

It is checked against ngspice on the same netlists ([`hardware/verify/`](hardware/verify/), [`hardware/spice/rhythm.py`](hardware/spice/rhythm.py)):

| | |
|---|---|
| rhythm channel + power section, 220 Hz at 20 mV-0.3 V, Clip 0 and 6 | every node within 0.04 dB of ngspice, waveforms -47 to -62 dB residual |
| rhythm small-signal response, 50 Hz - 16 kHz | within 0.35 dB |
| lead preamp | within 0.23 dB small-signal; -25 to -59 dB residual on waveforms |

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

**Tests.** `ctest --test-dir build -C Release` runs [`tests/uh20_tests.cpp`](tests/uh20_tests.cpp) on every CI build: golden input/output pairs for both channels (short plucked-string signals whose output must match [`tests/golden`](tests/golden) to -100 dB, so any change to the sound fails), a check that every solver converges (a 1 ppm input change must move the output by no more than -95 dB), hard high notes that must stay free of subharmonics and inside an aliasing budget, the Clip LED solve against its exact equation, the power amp's feedback loop settling at every Presence and Master setting, Sag drooping the rails under load, knob moves that must not click, and extreme settings that must stay finite and fall silent. When the sound is meant to change, `uh20_tests --update` rewrites the goldens in the same commit. `tools/render_samples.py` rebuilds the listening samples (Opus, 128 kbps VBR, from the float renders).

## License

[AGPL-3.0-or-later](LICENSE). The plugin is built with [JUCE](https://juce.com) under its AGPLv3 option, with [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) for CLAP.

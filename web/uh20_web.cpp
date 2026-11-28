// The browser build of the UH-20: the plug-in's engine (source/dsp/UH20Engine.h), unchanged, behind a small C API for
// WebAssembly. The page's AudioWorklet (docs/play/player/worklet.js) drives it; tools/web_parity.mjs holds it to the
// native renderer (tools/uh20_cli) to float rounding. Build: tools/build_web.sh.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../source/dsp/UH20Engine.h"
#include <emscripten/emscripten.h>
#include <memory>
#include <vector>

namespace {
// parameter ids, in the order of the `params` list in docs/play/amp.js (the plug-in's parameter order)
enum Id { VOLUME, DRIVE, TREBLE, MIDDLE, BASS, PRESENCE, CLIP, SAG, MASTER, BRIGHT, SHIFT, OUTPUT, INVOLTS, TRIM, NPARAM };
constexpr int kMaxBlock = 4096;

struct Amp {
    uh20::Engine e; uh20::Params p; double sr = 48000; bool dirty = false; std::vector<uh20::Engine*> snaps;
    float in[kMaxBlock], l[kMaxBlock], r[kMaxBlock];
    // the plug-in's defaults (source/PluginProcessor.cpp) are uh20::Params' own
};
}

extern "C" {
// a new amp at this sample rate, prepared with the plug-in's defaults
EMSCRIPTEN_KEEPALIVE Amp* amp_create(double sampleRate) {
    auto* a = new Amp; a->sr = sampleRate; a->e.setParams(a->p); a->e.prepare(sampleRate); return a;
}
EMSCRIPTEN_KEEPALIVE void amp_destroy(Amp* a) { for (auto* s : a->snaps) delete s; delete a; }
// the engine's whole state (every capacitor, stage and supply node, and the settings) copied into a numbered slot, and
// back: the buffered player renders ahead in chunks, keeps the state at each chunk's start, and when a knob moves it goes
// back to the first chunk not yet played and renders on from there with the new setting, which is exactly what the
// engine playing live does when the knob moves at that sample
// (the engine has const members, so it is copied by construction: the old copy is destroyed and a new one built in place)
static void copyEngine(uh20::Engine* to, const uh20::Engine& from) { std::destroy_at(to); std::construct_at(to, from); }
EMSCRIPTEN_KEEPALIVE void amp_snapshot(Amp* a, int slot) {
    if (slot < 0 || slot > 255) return;
    if ((int)a->snaps.size() <= slot) a->snaps.resize((size_t)slot + 1, nullptr);
    if (!a->snaps[(size_t)slot]) a->snaps[(size_t)slot] = new uh20::Engine(a->e); else copyEngine(a->snaps[(size_t)slot], a->e);
}
EMSCRIPTEN_KEEPALIVE int amp_restore(Amp* a, int slot) {
    if (slot < 0 || slot >= (int)a->snaps.size() || !a->snaps[(size_t)slot]) return 0;
    copyEngine(&a->e, *a->snaps[(size_t)slot]); a->p = a->e.p; a->dirty = false; return 1;
}
// set one control (knobs 0..10; bright, shift 0/1; output 0 = REC, 1 = AMP; involts in volts at 0 dBFS; trim in dB);
// applied at the next process call, once per block, as the plug-in applies its parameters
EMSCRIPTEN_KEEPALIVE void amp_set_param(Amp* a, int id, double v) {
    auto& p = a->p;
    switch (id) {
        case VOLUME: p.volume = v; break;     case DRIVE: p.drive = v; break;       case TREBLE: p.treble = v; break;
        case MIDDLE: p.middle = v; break;     case BASS: p.bass = v; break;         case PRESENCE: p.presence = v; break;
        case CLIP: p.clip = v; break;         case SAG: p.sag = v; break;           case MASTER: p.master = v; break;
        case BRIGHT: p.bright = v > 0.5; break; case SHIFT: p.shift = v > 0.5; break; case OUTPUT: p.output = v > 0.5 ? 1 : 0; break;
        case INVOLTS: p.inputVolts = v; break; case TRIM: p.trimDb = v; break;
        default: return;
    }
    a->dirty = true;
}
// start over at the current settings: the state a freshly prepared plug-in has
EMSCRIPTEN_KEEPALIVE void amp_prepare(Amp* a) { a->e.setParams(a->p); a->e.prepare(a->sr); a->dirty = false; }
// mono in, stereo out (the amp is mono: both sides carry the same signal), n <= amp_max_block() samples
EMSCRIPTEN_KEEPALIVE void amp_process(Amp* a, const float* in, float* outL, float* outR, int n) {
    if (a->dirty) { a->e.setParams(a->p); a->dirty = false; }
    for (int i = 0; i < n; ++i) outL[i] = in[i];
    a->e.process(outL, n);
    for (int i = 0; i < n; ++i) outR[i] = outL[i];
}
// scratch buffers inside the module's memory, so the caller needs no allocator: 0 = in, 1 = left, 2 = right
EMSCRIPTEN_KEEPALIVE float* amp_buffer(Amp* a, int which) { return which == 0 ? a->in : which == 1 ? a->l : a->r; }
EMSCRIPTEN_KEEPALIVE int amp_max_block() { return kMaxBlock; }
EMSCRIPTEN_KEEPALIVE int amp_param_count() { return NPARAM; }
}

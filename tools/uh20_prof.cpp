// uh20_prof in.f32 [k=v ...]: where the engine's time goes, stage by stage (built with UH20_PROFILE=1). The counter's own cost
// per lap is measured and taken off each stage; absolute numbers read high (the isb serialises), so use the shares.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../source/dsp/UH20Engine.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    uh20::Params p;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i]; auto e = a.find('='); std::string k = a.substr(0, e); double v = std::atof(a.c_str() + e + 1);
        if (k == "volume") p.volume = v; else if (k == "treble") p.treble = v; else if (k == "middle") p.middle = v; else if (k == "bass") p.bass = v;
        else if (k == "presence") p.presence = v; else if (k == "clip") p.clip = v; else if (k == "sag") p.sag = v; else if (k == "master") p.master = v;
        else if (k == "bright") p.bright = v > 0.5; else if (k == "shift") p.shift = v > 0.5; else if (k == "output") p.output = (int)v;
        else if (k == "drive") p.drive = v;
    }
    FILE* f = std::fopen(argv[1], "rb"); if (!f) return 2;
    std::vector<float> x; float t; while (std::fread(&t, 4, 1, f) == 1) x.push_back(t); std::fclose(f);
    // the counter's cost: empty laps
    uh20prof::last = uh20prof::now(); for (int i = 0; i < 1000000; ++i) uh20prof::lap(23);
    const double lapCost = (double)uh20prof::acc[23] / uh20prof::cnt[23];
    for (auto& a : uh20prof::acc) a = 0; for (auto& c : uh20prof::cnt) c = 0;
    uh20::Engine eng; eng.setParams(p); eng.prepare(48000);
    uh20prof::last = uh20prof::now();
    for (size_t i = 0; i < x.size(); i += 128) eng.process(x.data() + i, (int)std::min<size_t>(128, x.size() - i));
    uint64_t fr; asm volatile("mrs %0, cntfrq_el0" : "=r"(fr));
    const char* names[14] = {"(block loop)", "up filter", "Q1 gate + stage", "Drive stage", "Clip LEDs", "stack network", "Q2 gate + stage",
                             "Q2->Q3 network, Q3 gate + stage", "Master network", "LTP gate + pair", "LM3886, rails, Sag, feedback", "cab (REC)", "(return)", "down filter"};
    double tot = 0, ns[14]; for (int k = 0; k < 14; ++k) { ns[k] = std::max(0.0, (uh20prof::acc[k] - lapCost * uh20prof::cnt[k]) * 1e9 / fr); tot += ns[k]; }
    const double audio = x.size() / 48000.0;
    std::printf("total %.1f%% of a core (profiled; reads high)\n", 100 * tot * 1e-9 / audio);
    for (int k = 0; k < 14; ++k) if (uh20prof::cnt[k]) std::printf("  %-34s %5.1f%%  %6.1f ns/call\n", names[k], 100 * ns[k] / tot, ns[k] / uh20prof::cnt[k]);
    const char* cn[12] = {"J201 stage solves", "  their Newton steps", "  fallbacks", "pair solves", "  their Newton steps", "  fallbacks",
                          "pair sides in triode", "  their Newton steps", "LED solves", "  their Newton steps", "gate clamps past the early-out", "  their Newton steps"};
    for (int k = 0; k < 12; ++k) std::printf("  %-34s %.3g per output sample\n", cn[k], (double)jk::count[k] / x.size());
    return 0;
}

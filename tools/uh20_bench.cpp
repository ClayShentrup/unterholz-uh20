// uh20_bench in.f32 [passes] [k=v ...] [sr=48000]: CPU cost of the engine, as a share of one core in real time.
// The input (raw float32 mono) is run through a freshly prepared engine in 128-sample blocks, as a host would; the best of
// the passes is reported (the others carry scheduler noise). Run it without nice: a nice'd process can land on an
// efficiency core and read about twice as slow.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../source/dsp/UH20Engine.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: uh20_bench in.f32 [passes] [k=v...]\n"); return 1; }
    int passes = 5, first = 2;
    if (argc > 2 && std::string(argv[2]).find('=') == std::string::npos) { passes = std::atoi(argv[2]); first = 3; }
    uh20::Params p; double sr = 48000;
    for (int i = first; i < argc; ++i) {
        std::string a = argv[i]; auto e = a.find('='); std::string k = a.substr(0, e); double v = std::atof(a.c_str() + e + 1);
        if (k == "volume") p.volume = v; else if (k == "treble") p.treble = v; else if (k == "middle") p.middle = v; else if (k == "bass") p.bass = v;
        else if (k == "presence") p.presence = v; else if (k == "clip") p.clip = v; else if (k == "sag") p.sag = v; else if (k == "master") p.master = v;
        else if (k == "bright") p.bright = v > 0.5; else if (k == "shift") p.shift = v > 0.5; else if (k == "output") p.output = (int)v;
        else if (k == "drive") p.drive = v; else if (k == "involts") p.inputVolts = v; else if (k == "sr") sr = v;
    }
    FILE* f = std::fopen(argv[1], "rb"); if (!f) return 2;
    std::vector<float> x; float t; while (std::fread(&t, 4, 1, f) == 1) x.push_back(t); std::fclose(f);
    double best = 1e9, sink = 0;
    for (int pass = 0; pass < passes; ++pass) {
        uh20::Engine eng; eng.setParams(p); eng.prepare(sr);
        std::vector<float> y = x;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < y.size(); i += 128) eng.process(y.data() + i, (int)std::min<size_t>(128, y.size() - i));
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        best = std::min(best, s); sink += y[y.size() / 2];
    }
    std::printf("%.2f%% of one core (best of %d, %.1f s of audio)%s\n", 100 * best / (x.size() / sr), passes, x.size() / sr, sink == 12345.678 ? " " : "");
    return 0;
}

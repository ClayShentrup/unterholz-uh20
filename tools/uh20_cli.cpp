// Offline test harness: uh20_cli in.f32 out.f32 samplerate key=value ...  (raw float32 mono in/out)
#include "../source/dsp/UH20Engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: uh20_cli in.f32 out.f32 sr [k=v...]\n"); return 1; }
    uh20::Params p; double sr = std::atof(argv[3]);
    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i]; auto e = a.find('='); std::string k = a.substr(0, e); double v = std::atof(a.c_str() + e + 1);
        if (k == "volume") p.volume = v; else if (k == "treble") p.treble = v; else if (k == "middle") p.middle = v; else if (k == "bass") p.bass = v;
        else if (k == "presence") p.presence = v; else if (k == "jubilee") p.jubilee = v; else if (k == "sag") p.sag = v; else if (k == "master") p.master = v;
        else if (k == "bright") p.bright = v > 0.5; else if (k == "shift") p.shift = v > 0.5; else if (k == "output") p.output = (int)v;
        else if (k == "trim") p.trimDb = v; else if (k == "channel") p.channel = (int)v; else if (k == "drive") p.drive = v; else if (k == "involts") p.inputVolts = v;
    }
    FILE* f = std::fopen(argv[1], "rb"); if (!f) return 2;
    std::vector<float> x; float t; while (std::fread(&t, 4, 1, f) == 1) x.push_back(t); std::fclose(f);
    uh20::Engine eng; eng.setParams(p); eng.prepare(sr);
    eng.process(x.data(), (int)x.size());
    FILE* o = std::fopen(argv[2], "wb"); std::fwrite(x.data(), 4, x.size(), o); std::fclose(o);
    return 0;
}

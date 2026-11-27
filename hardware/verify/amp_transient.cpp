// amp_transient AMP CLIP DRIVE: runs the UH-20 engine (8x, 384 kHz) on a 220 Hz sine of AMP volts with the rest of the panel at the
// studio-clean settings and prints the nodes ../spice/rhythm.py writes: time, first stage drain (Drive's drain when Drive is on),
// Q3 drain, pair output, speaker. Build: clang++ -std=c++20 -O2 amp_transient.cpp -o amp_transient
#define private public
#include "../../source/dsp/UH20Engine.h"
#undef private
#include <cstdio>
#include <cstdlib>
using namespace uh20;
int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: amp_transient AMP CLIP DRIVE\n"); return 1; }
    Params p; p.output = 1; p.clip = std::atof(argv[2]); p.drive = std::atof(argv[3]);
    const double AMP = std::atof(argv[1]);
    Engine e; e.CRES = 1e3; e.setParams(p); e.prepare(48000);   // stiff reservoirs, as in the netlist's ideal supply
    const double FSI = e.FSI;
    for (int i = 0; i < (int)(0.5 * FSI); ++i) {
        const double t = i / FSI; e.amp(AMP * std::sin(2 * kPi * 220 * t));
        if (t >= 0.3) std::printf("%.9e %.6f %.6f %.6f %.6f\n", t, e.don ? e.qd.vd : e.q1.vd, e.q3.vd, e.ltp.d2, e.vout);
    }
}

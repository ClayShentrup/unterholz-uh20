#include "../../source/dsp/JfetKit.h"
#include <cstdio>
#include <cstdlib>
using namespace jk;
int main() { const double FS = 384000, PV = 0.7;
  for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 15000.0, 20000.0}) {
    CSStage j1; j1.Rd = 27e3; j1.Rs1 = 0; j1.Rs2 = 1500; j1.Cs = 100e-6; j1.prepare(FS);
    CSStage j2; j2.Rd = 56e3; j2.Rs1 = 0; j2.Rs2 = 2200; j2.Cs = 0; j2.prepare(FS);
    GainFollower j34; j34.a.Rd = 56e3; j34.a.Rs1 = 0; j34.a.Rs2 = 1800; j34.a.Cs = 0; j34.Rf = 10e3; j34.prepare(FS);
    Network<4> n1; n1.rs = 1; n1.C(0, 1, 220e-9); n1.R(1, 2, 470e3); n1.C(1, 2, 470e-12); n1.R(2, 3, 1e6 * (1 - PV) + 1); n1.R(3, -1, 1e6 * PV + 1); n1.C(2, 3, 1e-9); n1.C(3, -1, j2.inputCapacitance(650e3)); n1.out = 3; n1.build(FS);
    Network<3> n2; n2.rs = 1; n2.C(0, 1, 220e-9); n2.R(1, 2, 470e3); n2.C(1, 2, 470e-12); n2.R(2, -1, 180e3); n2.C(2, -1, j34.a.inputCapacitance()); n2.out = 2; n2.build(FS);
    Network<6> st; st.rs = 1; st.C(0, 1, 470e-12); st.R(1, 2, 110e3); st.R(2, 3, 110e3); st.R(0, 4, 33e3); st.C(4, 3, 22e-9); st.R(3, 5, 100e3); st.C(4, 5, 22e-9); st.R(5, -1, 11e3); st.R(2, -1, 1e6); st.out = 2; st.build(FS);
    n1.initDC(j1.vd0); n2.initDC(j2.vd0); st.initDC(j34.vf0);
    const double A = 1e-4, kin = 27.0 / 127.0, rin = 100e3 * 27e3 / 127e3; const int n = (int)(0.3 * FS); double c[3] = {}, sn[3] = {};
    for (int i = 0; i < n; ++i) { const double t = i / FS, vin = A * std::sin(2 * M_PI * f * t);
      const double d1 = j1.tick(gateClamp(vin * kin, rin, j1.vs), n1.portG(), n1.portJ()); const double w = n1.process(d1);
      const double d2 = j2.tick(gateClamp(w, n1.rOut, j2.vs), n2.portG(), n2.portJ()); const double g3 = n2.process(d2);
      const double s4 = j34.tick(gateClamp(g3, n2.rOut, j34.a.vs), st.portG(), st.portJ()); const double tw = st.process(s4);
      if (i > n / 2) { const double v[3] = {tw, d2, d1}; for (int k = 0; k < 3; ++k) { c[k] += v[k] * std::cos(2 * M_PI * f * t); sn[k] += v[k] * std::sin(2 * M_PI * f * t); } } }
    printf("%.0f", f); for (int k = 0; k < 3; ++k) printf(" %.4f", 20 * std::log10(2 * std::hypot(c[k], sn[k]) / (n - n / 2 - 1) / A)); printf("\n"); } }

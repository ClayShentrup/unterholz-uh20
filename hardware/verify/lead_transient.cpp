// the jfet800.cir lead preamp, block by block, against ngspice's transient (PV = 0.7, 220 Hz)
#include "../../source/dsp/JfetKit.h"
#include <cstdio>
#include <cstdlib>
using namespace jk;
int main(int argc, char** argv) {
  const double AMP = atof(argv[1]), PV = 0.7, FS = 500000;   // ngspice wrote every 2 us
  CSStage j1; j1.Rd = 27e3; j1.Rs1 = 0; j1.Rs2 = 1500; j1.Cs = 100e-6; j1.prepare(FS);
  CSStage j2; j2.Rd = 56e3; j2.Rs1 = 0; j2.Rs2 = 2200; j2.Cs = 0; j2.prepare(FS);
  GainFollower j34; j34.a.Rd = 56e3; j34.a.Rs1 = 0; j34.a.Rs2 = 1800; j34.a.Cs = 0; j34.Rf = 10e3; j34.prepare(FS);
  Network<4> n1; n1.rs = 1; n1.C(0, 1, 220e-9); n1.R(1, 2, 470e3); n1.C(1, 2, 470e-12); n1.R(2, 3, 1e6 * (1 - PV) + 1); n1.R(3, -1, 1e6 * PV + 1); n1.C(2, 3, 1e-9); n1.C(3, -1, j2.inputCapacitance(650e3)); n1.out = 3; n1.build(FS);
  Network<3> n2; n2.rs = 1; n2.C(0, 1, 220e-9); n2.R(1, 2, 470e3); n2.C(1, 2, 470e-12); n2.R(2, -1, 180e3); n2.C(2, -1, j34.a.inputCapacitance()); n2.out = 2; n2.build(FS);
  Network<6> st; st.rs = 1; st.C(0, 1, 470e-12); st.R(1, 2, 110e3); st.R(2, 3, 110e3); st.R(0, 4, 33e3); st.C(4, 3, 22e-9); st.R(3, 5, 100e3); st.C(4, 5, 22e-9); st.R(5, -1, 11e3); st.R(2, -1, 1e6); st.out = 2; st.build(FS);
  // pre-charge the coupling capacitors to the DC operating point by running silence for 2 s
  const double kin = 27.0 / 127.0, rin = 100e3 * 27e3 / 127e3;
  auto step = [&](double vin, double* o) {
    const double g1 = gateClamp(vin * kin, rin, j1.vs);
    const double d1 = j1.tick(g1, n1.portG(), n1.portJ()); const double w = n1.process(d1);
    const double g2 = gateClamp(w, n1.rOut, j2.vs);
    const double d2 = j2.tick(g2, n2.portG(), n2.portJ()); const double g3in = n2.process(d2);
    const double g3 = gateClamp(g3in, n2.rOut, j34.a.vs);
    const double s4 = j34.tick(g3, st.portG(), st.portJ()); const double tw = st.process(s4);
    o[0] = s4; o[1] = tw; o[2] = d2; o[3] = d1;
  };
  double o[4];
  n1.initDC(j1.vd0); n2.initDC(j2.vd0); st.initDC(j34.vf0);
  for (int i = 0; i < (int)(0.05 * FS); ++i) step(0, o);
  for (int i = 0; i < (int)(0.2 * FS); ++i) { const double t = i / FS; step(AMP * std::sin(2 * M_PI * 220 * t), o); if (t >= 0.1) printf("%.9e %.6f %.6f %.6f %.6f\n", t, o[0], o[1], o[2], o[3]); }
}

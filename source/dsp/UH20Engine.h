// Unterholz UH-20: the circuit of the box, simulated from its parts. Two channels into one class AB power section.
//   RHYTHM: J201 input stage (Clip: LEDs at the stack input and a gang that opens up the stage's source), Mark III tone stack,
//           Volume (1M log, bright cap), two J201 gain stages.
//   LEAD:   the 2203 preamp translated stage by stage into J201s (hardware/spice/jfet800.cir), Drive = its 1M log volume, the
//           Clip LEDs behind the cold clipper, a source follower into a Marshall tone stack.
//   POWER:  Master (1M log) into a J201 long-tailed pair, then an LM3886 (class AB) with global feedback to the pair's second gate,
//           Presence in that feedback network, the speaker load, and rails from an unregulated supply whose series resistance is
//           the Sag control. REC: an active cab simulator on the speaker output (1/20 divider); AMP: the same divider without it.
// Every J201 is the vendor model (hardware/spice/j201.lib) solved per sample with its real loading (source/dsp/JfetKit.h).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cmath>
#include <algorithm>
#include <initializer_list>
#include "JfetKit.h"

namespace uh20 {

constexpr double kPi = 3.14159265358979323846;

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(double nb0, double nb1, double nb2, double na1, double na2) { b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2; }
    void reset() { z1 = z2 = 0; }
    inline double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    // analog (n0 + n1 s)/(d0 + d1 s), bilinear with constant K
    void firstOrder(double n0, double n1, double d0, double d1, double K) {
        const double n = d0 + d1 * K;
        set((n0 + n1 * K) / n, (n0 - n1 * K) / n, 0, (d0 - d1 * K) / n, 0);
    }
    // analog (n0 + n1 s + n2 s^2)/(d0 + d1 s + d2 s^2)
    void secondOrder(double n0, double n1, double n2, double d0, double d1, double d2, double K) {
        const double n = d0 + d1 * K + d2 * K * K;
        set((n0 + n1 * K + n2 * K * K) / n, (2 * n0 - 2 * n2 * K * K) / n, (n0 - n1 * K + n2 * K * K) / n,
            (2 * d0 - 2 * d2 * K * K) / n, (d0 - d1 * K + d2 * K * K) / n);
    }
    static double prewarp(double f, double fs) { const double w = 2 * kPi * f; return w / std::tan(w / (2 * fs)); }
    void hp2(double f, double Q, double fs) { const double w = 2 * kPi * f; secondOrder(0, 0, 1, w * w, w / Q, 1, prewarp(f, fs)); }
    void lp2(double f, double Q, double fs) { const double w = 2 * kPi * f; secondOrder(w * w, 0, 0, w * w, w / Q, 1, prewarp(f, fs)); }
    void bp2(double f, double Q, double fs) { const double w = 2 * kPi * f; secondOrder(0, w / Q, 0, w * w, w / Q, 1, prewarp(f, fs)); }
    void peak(double f0, double gdb, double q, double fs) {
        const double A = std::pow(10.0, gdb / 40), w = 2 * kPi * f0 / fs, al = std::sin(w) / (2 * q), a0 = 1 + al / A;
        set((1 + al * A) / a0, -2 * std::cos(w) / a0, (1 - al * A) / a0, -2 * std::cos(w) / a0, (1 - al / A) / a0);
    }
};

// 8th-order Butterworth low-pass (oversampling filters)
struct Butter8 {
    Biquad s[4];
    void set(double f, double fs) { s[0].lp2(f, 0.5098, fs); s[1].lp2(f, 0.6013, fs); s[2].lp2(f, 0.9000, fs); s[3].lp2(f, 2.5629, fs); }
    void reset() { for (auto& b : s) b.reset(); }
    inline double process(double x) { return s[3].process(s[2].process(s[1].process(s[0].process(x)))); }
};

inline double taperA(double x) { return x <= 0.5 ? 0.2 * x : 0.1 + 1.8 * (x - 0.5); }
inline double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

struct Params {
    double volume = 5, treble = 3, middle = 7, bass = 5, presence = 5, clip = 0, sag = 0, master = 6.5;
    bool bright = false, shift = false;
    int output = 0;          // 0 = REC (analog cab sim), 1 = AMP (no cab)
    double inputVolts = 1.6; // volts at 0 dBFS
    double trimDb = 0;
    int channel = 0;         // 0 = Rhythmus, 1 = Lead
    double drive = 5;        // Lead drive (Antrieb)
    bool operator==(const Params&) const = default;
};

class Engine {
public:
    void prepare(double sampleRate) {
        srate = sampleRate; FSI = srate * OS;
        aaup.set(18000, FSI); aadn.set(18000, FSI);
        built = false; update(p, true); reset();
    }
    void reset() {
        aaup.reset(); aadn.reset();
        for (auto* b : {&chp, &cp1, &cp2, &clp1, &clp2}) b->reset();
        // DC operating point of every stage, then each network charged to it
        q1.settle(); q2.settle(); q3.settle(); l1.settle(); l2.settle(); l34.prepare(FSI); ltp.settle();
        rN1.initDC(q1.vd0); rN2.initDC(q2.vd0); rN3.initDC(q3.vd0);
        lN1.initDC(l1.vd0); lN2.initDC(l2.vd0); lN3.initDC(l34.vf0);
        cpl.initDC(ltp.d20); fb.initDC(0);
        vout = 0; vposR = vnegR = railRest(); ph = 0;
    }
    void setParams(const Params& np) { if (!(np == p)) update(np, false); }
    void process(float* buf, int n) { for (int i = 0; i < n; ++i) buf[i] = (float)tick(buf[i]); }
    double tick(double in) {
        const double xin = in * p.inputVolts;
        double ydec = 0;
        for (int k = 0; k < OS; ++k) {
            const double u = aaup.process(k == 0 ? xin * OS : 0.0);
            const double z = aadn.process(amp(u));
            if (k == 0) ydec = z;
        }
        return ydec * gout;
    }

    // ---------------------------------------------------------------- circuit
    static constexpr double VPRE = 18;        // preamp supply
    static constexpr double RL = 8;            // speaker load, ohms
    static constexpr double GLM = 1 + 22e3 / 5.6e3;   // LM3886 non-inverting gain
    static constexpr double KREC = 1.0 / 20;   // speaker output to the cab simulator's input (19.1k/1k divider)
    double CCOMP = 220e-12;                    // compensation capacitor on the pair's output drain
#ifndef UH20_OS
#define UH20_OS 8
#endif
    static constexpr int OS = UH20_OS;
    Params p;
    double srate = 48000, FSI = 384000;
    Butter8 aaup, aadn;
    jk::JStage q1, q2, q3, l2;      // rhythm stages; lead cold clipper
    jk::CSStage l1;                  // lead input stage
    jk::GainFollower l34;            // lead gain stage + source follower
    jk::LongTailPair ltp;
    jk::Network<10> rN1;  // Q1 drain -> 4.7k -> A -> Mark III stack -> Volume (bright) -> 22n -> Q2 gate (1M); A -> 0.47u -> LEDs
    jk::Network<3> rN2;   // Q2 drain -> 22n -> 100k -> Q3 gate (6.8k)
    jk::Network<5> rN3;   // Q3 drain -> 100n -> 47k -> Master (1M log) -> 0.1u -> LTP gate (1M)
    jk::Network<4> lN1;   // J1 drain -> 220n -> 470k || 470p -> Drive (1M log, 1n bright) -> J2 gate
    jk::Network<3> lN2;   // J2 drain -> 220n -> a (LEDs) -> 470k || 470p -> J3 gate (180k)
    jk::Network<9> lN3;   // follower -> Marshall stack -> 1.5M -> Master (1M log) -> 0.1u -> LTP gate (1M)
    jk::Network<2> cpl;   // LTP drain 2 -> 1u -> LM3886 + input (47k)
    jk::Network<2> fb;    // speaker -> 100k -> LTP gate 2 (4.7k; Presence: 0.1u + 5k)
    Biquad chp, cp1, cp2, clp1, clp2;
    double a1 = 0, a2 = 0, csc = 4.12 / 14.12, gout = 1;
    bool jon = false, built = false; double rj = 1e9, rsup = 0.3, vout = 0, vposR = 30, vnegR = 30, ph = 0;
    // LEDs: a red LED one way, two in series the other (1.8 V at 10 mA each)
    static constexpr double NVT1 = 2.0 * jk::VT, NVT2 = 4.0 * jk::VT;
    const double ISLED = 0.01 / std::exp(1.8 / NVT1), ISLED2 = 0.01 / std::exp(3.6 / NVT2);
    // supply: 22 V AC secondary (31 V peak), bridge, 4700 uF per rail, LM3886 quiescent 50 mA
    static constexpr double VPK = 31.1 - 0.9, IQ = 0.05, DROP = 3.0;
    double CRES = 4700e-6;
    double railRest() const { return VPK - IQ * rsup * 3; }

    // rail-to-rail op-amp follower on the 18 V preamp supply (e.g. OPA2197): unity gain, limited at its rails
    static double buf(double v) { return std::clamp(v, 0.02, VPRE - 0.02); }

    inline double amp(double vin) {
        double g1raw, g1rout;
        if (p.channel == 0) {
            const double g1 = jk::gateClamp(vin, 1e3, q1.vs);
            const double d1 = q1.tick(g1, [](double) { return 0.0; });   // drain drives an op-amp follower only
            const double b1 = buf(d1);
            const double il = jon ? jk::diodeString(rN1.injV(b1), rj + rN1.Zi, ISLED, NVT1, ISLED2, NVT2) : 0.0;
            const double w = rN1.process(b1, -il);
            const double d2 = q2.tick(jk::gateClamp(w, rN1.rOut, q2.vs), [&](double vd) { return rN2.portG() * vd - rN2.portJ(); });
            const double g3 = rN2.process(d2);
            const double d3 = q3.tick(jk::gateClamp(g3, rN2.rOut, q3.vs), [](double) { return 0.0; });
            g1raw = rN3.process(buf(d3)); g1rout = rN3.rOut;
        } else {
            const double d1 = l1.tick(jk::gateClamp(vin * 56.0 / 103.0, 47e3 * 56e3 / 103e3, l1.vs), 0, 0);   // 47k / 56k input pad
            const double w = lN1.process(buf(d1));
            auto led = [&](double vd) { return jon ? jk::diodeString(lN2.injV(vd), rj + lN2.Zi, ISLED, NVT1, ISLED2, NVT2) : 0.0; };
            const double d2 = l2.tick(jk::gateClamp(w, lN1.rOut, l2.vs), [&](double vd) { return lN2.portG() * vd - lN2.portJi(-led(vd)); });
            const double g3 = lN2.process(d2, -led(d2));
            const double s4 = l34.tick(jk::gateClamp(g3, lN2.rOut, l34.a.vs), 0, 0);
            g1raw = lN3.process(buf(s4)); g1rout = lN3.rOut;
        }
        // power section: long-tailed pair (gate 2 = the feedback network's output, from last sample's speaker voltage)
        const double g1 = jk::gateClamp(g1raw, g1rout, ltp.vt);
        const double d2 = ltp.tick(g1, fb.outV(vout), cpl.portG(), cpl.portJ());
        const double vp = cpl.process(d2);
        // LM3886 into the speaker, limited by its rails (a few volts of dropout)
        vout = std::clamp(GLM * vp, -(vnegR - DROP), vposR - DROP);
        fb.process(vout);
        // rails: full-wave rectified 60 Hz secondary through the supply resistance (winding + Sag) into the reservoirs
        ph += 60.0 / FSI; if (ph >= 1) ph -= 1;
        const double vsec = VPK * std::abs(std::sin(2 * kPi * ph)) + 0.9 - 0.9;
        const double ip = (vout > 0 ? vout / RL : 0) + IQ, in = (vout < 0 ? -vout / RL : 0) + IQ;
        vposR += ((vsec > vposR ? (vsec - vposR) / rsup : 0) - ip) / (CRES * FSI);
        vnegR += ((vsec > vnegR ? (vsec - vnegR) / rsup : 0) - in) / (CRES * FSI);
        // outputs: REC = the speaker voltage through the 1/20 divider into the cab simulator; AMP = the same, uncabbed
        const double y = vout * KREC;
        if (p.output == 1) return y;
        double c = chp.process(y * csc);
        c = c + a1 * cp1.process(c) + a2 * cp2.process(c);
        return clp2.process(clp1.process(c));
    }

    void update(const Params& np, bool force) {
        const bool first = !built; p = np; built = true;
        const double fs = FSI;
        // ---- rhythm Q1: J201, Rd 68k; source 3.3k (|| Clip gang A: 100k rev-log + 10u) then 1.5k || 0.68u (Shift: + 22u)
        jon = p.clip >= 0.05;
        rj = 22 + 47e3 * taperA(1 - p.clip / 10);        // Clip gang B: 47k reverse-log (the 470k original / 10)
        const double rg = 220 + 100e3 * taperA(1 - p.clip / 10);
        q1.Vdd = VPRE; q1.Rd = 68e3; q1.src.clear();
        q1.src.R(0, 1, 3300); if (jon) { q1.src.R(0, 2, rg); q1.src.C(2, 1, 10e-6); } else q1.src.R(0, 2, 1e9);
        q1.src.R(1, -1, 1500); q1.src.C(1, -1, 0.68e-6 + (p.shift ? 22e-6 : 0)); q1.build(fs);
        // Q1 drain -> 4.7k -> A; Mark III stack from A (250p, 250k treble, 100k slope, 0.1u, 0.047u, 250k bass, 25k mid); w -> Volume
        {
            const double t = clampd(p.treble / 10, 0.0004, 0.9996), b = std::max(taperA(p.bass / 10), 0.0004), m = std::max(p.middle / 10, 0.004);
            const double vv = clampd(taperA(p.volume / 10), 0.0005, 0.9995);
            // every pot <= 100k: the network is the original scaled by 1/10 (R) and 10 (C), driven by the drain's follower through
            // 6.8k (Q1's 68k drain resistance / 10), so its transfer is unchanged
            auto& n = rN1; n.clear(); n.rs = 6800; n.inj = 9;
            n.R(0, 1, 470);                           // 1 = A (stack input)
            if (jon) n.C(1, 9, 4.7e-6); n.R(9, -1, 100e3);   // 9 = the LED string's coupling node (4.7u in, 100k bleeder); Clip 0 switches the branch out
            n.C(1, 2, 2.5e-9);                        // 2 = treble top
            n.R(2, 3, 25e3 * (1 - t)); n.R(3, 4, 25e3 * t);     // 3 = treble wiper (w), 4 = treble bottom
            n.R(1, 5, 10e3);                          // 5 = x (slope)
            n.C(5, 4, 1e-6); n.R(4, 6, 25e3 * b); n.C(5, 6, 0.47e-6); n.R(6, -1, 2.5e3 * m);   // 6 = mid
            n.R(3, 7, 100e3 * (1 - vv)); n.R(7, -1, 100e3 * vv); if (p.bright) n.C(3, 7, 1e-9);    // 7 = volume wiper
            n.C(7, 8, 220e-9); n.R(8, -1, 100e3);     // 8 = Q2 gate
            n.out = 8; n.build(fs);
        }
        q2.Vdd = VPRE; q2.Rd = 47e3; q2.src.clear(); q2.src.R(0, -1, 2200); q2.src.C(0, -1, 22e-6); q2.build(fs);
        { auto& n = rN2; n.clear(); n.rs = 1; n.C(0, 1, 22e-9); n.R(1, 2, 100e3); n.R(2, -1, 6800); n.out = 2; n.build(fs); }
        q3.Vdd = VPRE; q3.Rd = 47e3; q3.src.clear(); q3.src.R(0, -1, 2200); q3.src.C(0, -1, 22e-6); q3.build(fs);
        const double ma = clampd(taperA(p.master / 10), 0.0005, 0.9995);
        { auto& n = rN3; n.clear(); n.rs = 4700; n.C(0, 1, 1e-6); n.R(1, 2, 4700); n.R(2, -1, 1e9);   // Q3 follower, 47k / 10
          n.R(2, 3, 100e3 * (1 - ma)); n.R(3, -1, 100e3 * ma); n.C(3, 4, 1e-6); n.R(4, -1, 100e3); n.out = 4; n.build(fs); }
        // ---- lead (jfet800.cir): J1 27k / 1.5k || 100u; Drive 1M log with 1n bright; J2 56k / 2.2k; LEDs behind it; J3 56k / 1.8k + follower
        l1.Vdd = VPRE; l1.Rd = 27e3; l1.Rs1 = 0; l1.Rs2 = 1500; l1.Cs = 100e-6; l1.prepare(fs);
        {
            const double dv = clampd(taperA(p.drive / 10), 0.0005, 0.9995);
            auto& n = lN1; n.clear(); n.rs = 2700; n.C(0, 1, 2.2e-6); n.R(1, 2, 47e3); n.C(1, 2, 4.7e-9);   // J1 follower, 27k / 10
            n.R(2, 3, 100e3 * (1 - dv) + 1); n.R(3, -1, 100e3 * dv + 1); n.C(2, 3, 10e-9); n.out = 3; n.build(fs);
        }
        l2.Vdd = VPRE; l2.Rd = 56e3; l2.src.clear(); l2.src.R(0, -1, 2200); l2.build(fs);
        { auto& n = lN2; n.clear(); n.rs = 1; n.inj = 1; n.C(0, 1, 220e-9); n.R(1, 2, 470e3); n.C(1, 2, 470e-12); n.R(2, -1, 180e3); n.out = 2; n.build(fs); }
        l34.a.Vdd = VPRE; l34.a.Rd = 56e3; l34.a.Rs1 = 0; l34.a.Rs2 = 1800; l34.a.Cs = 0; l34.Rf = 10e3;
        {
            const double t = clampd(p.treble / 10, 0.002, 0.998), b = clampd(taperA(p.bass / 10), 0.001, 0.999), m = clampd(p.middle / 10, 0.002, 0.998);
            auto& n = lN3; n.clear(); n.rs = 100;   // the follower, buffered (its own ~1k output resistance / 10)
            n.C(0, 1, 4.7e-9); n.R(1, 2, 22e3 * (1 - t) + 0.1); n.R(2, 3, 22e3 * t + 0.1);   // 2 = treble wiper
            n.R(0, 4, 3.3e3); n.C(4, 3, 220e-9); n.R(3, 5, 100e3 * b + 0.1); n.C(4, 5, 220e-9); n.R(5, -1, 2.2e3 * m + 5);
            n.R(2, 8, 150e3);                                                                                   // channel balance
            n.R(8, 6, 100e3 * (1 - ma) + 0.1); n.R(6, -1, 100e3 * ma + 0.1); n.C(6, 7, 1e-6); n.R(7, -1, 100e3);   // Master, LTP gate
            n.out = 7; n.build(fs);
        }
        // Miller input capacitance of each J201 at its operating point, at its gate node
        // ---- power: long-tailed pair (J201 x2, 22k drains, 30k tail, +/-15 V), coupling into the LM3886, feedback with Presence
        { auto& n = cpl; n.clear(); n.rs = 1; n.C(0, -1, CCOMP); n.C(0, 1, 1e-6); n.R(1, -1, 47e3); n.out = 1; n.build(fs); }   // CCOMP: dominant-pole compensation at the pair's output
        { auto& n = fb; n.clear(); n.rs = 100e3; n.R(0, -1, 4700); n.C(0, 1, 0.1e-6); n.R(1, -1, 5000 * (1 - clampd(p.presence / 10, 0, 1)) + 47); n.out = 0; n.build(fs); }
        rsup = 0.3 + 2.7 * clampd(p.sag / 10, 0, 1);   // winding + the Sag rheostat in the supply
        // cab simulator: Sallen-Key high-pass, two MFB band-passes summed in, two Sallen-Key low-passes (active filters on E96 values),
        // fitted through this circuit at Volume 5, Treble 3, Middle 7, Bass 5, Presence 5, Master 6.5 to the measured studio clean
        // tone: 1.39 dB rms, 60 Hz - 12.5 kHz
        chp.hp2(101.090, 1.2775, fs);
        cp1.bp2(276.845, 1.4134, fs); a1 = std::pow(10.0, 10.5098 / 20) - 1;
        cp2.bp2(2791.552, 2.7093, fs); a2 = std::pow(10.0, 9.8481 / 20) - 1;
        clp1.lp2(4855.70, 1.1474, fs); clp2.lp2(4855.70, 1.1474, fs);
        gout = std::pow(10.0, p.trimDb / 20) / (p.output == 1 ? 2.0 : 0.5);
        if (!first && force) {}
        (void)force;
    }
};

} // namespace uh20

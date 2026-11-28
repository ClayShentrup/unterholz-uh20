// Unterholz UH-20: the circuit of the box, simulated from its parts. One amp, one voice, into a class AB power section.
//   PREAMP: J201 input stage (Clip: LEDs at the stack input and a gang that opens up the stage's source); DRIVE: a cascaded J201
//           stage switched in ahead of the stack (0 = out); Mark III tone stack, Volume (bright cap), two J201 gain stages.
//   POWER:  Master into a J201 long-tailed pair, then an LM3886 (class AB) with global feedback to the pair's second gate,
//           Presence in that feedback network, the speaker load, rails from an unregulated supply, and Sag: a transconductor that
//           takes tail current from the pair as the speaker current rises, so the power section's ceiling comes down under hard
//           playing. REC: an active cab simulator on the speaker output (1/20 divider); AMP: the same divider without it.
// Every J201 is the vendor model (hardware/spice/j201.lib) solved per sample with its real loading (source/dsp/JfetKit.h).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cmath>
#include <algorithm>
#include <initializer_list>
#include "JfetKit.h"

// UH20_PROFILE=1 (tools/uh20_prof): time each stage with the ARM virtual counter (a lap per stage boundary); compiles to
// nothing otherwise
#if UH20_PROFILE
#include <cstdint>
namespace uh20prof {
inline uint64_t now() { uint64_t v; asm volatile("isb\n mrs %0, cntvct_el0" : "=r"(v)); return v; }
inline uint64_t acc[24] = {}, cnt[24] = {}, last = 0;
inline void lap(int k) { const uint64_t t = now(); acc[k] += t - last; ++cnt[k]; last = t; }
}
#define UH20_LAP(k) uh20prof::lap(k)
#else
#define UH20_LAP(k) ((void)0)
#endif

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
    double drive = 0;        // DRIVE (Antrieb): 0 = the cascaded stage switched out
    bool operator==(const Params&) const = default;
};

class Engine {
public:
    void prepare(double sampleRate) {
        srate = sampleRate; OS = osFor(srate); FSI = srate * OS;
        XF = kRef ? 32768 : (int)std::lround(32768 * FSI / 384000);
        aaup.set(18000, FSI); aadn.set(18000, FSI);
        built = false; update(p, true); reset();
    }
    void reset() {
        aaup.reset(); aadn.reset();
        for (auto* b : {&chp, &cp1, &cp2, &clp1, &clp2}) b->reset();
        // DC operating point of every stage, then each network charged to it
        q1.settle(); qd.settle(); q2.settle(); q3.settle(); ltp.settle();
        rN1.initDC(don ? qd.vd0 : q1.vd0); rN2.initDC(q2.vd0); rN3.initDC(q3.vd0); dN.initDC(q1.vd0); dL.initDC(qd.vd0);
        cpl.initDC(ltp.d20); fb.initDC(0);
        vout = 0; vposR = vnegR = railRest(); ph = 0; psin = 0; pcos = 1; xf = 0; isense = 0; vctl = 0; ltp.iinj = 0;
    }
    void setParams(const Params& np) { if (!(np == p)) update(np, false); }
    void process(float* buf, int n) { for (int i = 0; i < n; ++i) buf[i] = (float)tick(buf[i]); }
    double tick(double in) {
        const double xin = in * p.inputVolts;
        double ydec = 0;
        for (int k = 0; k < OS; ++k) {
            UH20_LAP(0);
            const double u = aaup.process(k == 0 ? xin * OS : 0.0);
            UH20_LAP(1);
            const double a = amp(u);
            UH20_LAP(12);
            const double z = aadn.process(a);
            UH20_LAP(13);
            if (k == 0) ydec = z;
        }
        return ydec * gout;
    }

    // ---------------------------------------------------------------- circuit
    static constexpr double VPRE = 18;        // preamp supply
    static constexpr double RL = 8;            // speaker load, ohms
    static constexpr double GLM = 1 + 22e3 / 5.6e3;   // LM3886 non-inverting gain
    static constexpr double KREC = 1.0 / 20;   // speaker output to the cab simulator's input (19.1k/1k divider)
    // The output's calibration, not a part of the circuit: the plug-in reads the REC or AMP jack as an interface would, 2.4 V peak
    // = 0 dBFS (the output's counterpart of the input's 1.6 V, Params::inputVolts). The REC jack's ceiling, the LM3886 clipping
    // through the divider and the cab simulator, is about 2.3 V, so the hottest settings peak just under 0 dBFS and the studio clean
    // about 7.5 dB under it (hardware/DESIGN.md, v0.4.1). Applied at the very end, with the output trim.
    static constexpr double VFS = 2.4;
    double CCOMP = 220e-12;                    // compensation capacitor on the pair's output drain
    // UH20_REFERENCE=1 builds the reference engine (uh20_cli_ref): 8x oversampling and the original general solvers, the engine
    // tests/reference was rendered with. The default build solves the same circuit with the analytic Newtons in JfetKit.h.
#if UH20_REFERENCE
    static constexpr bool kRef = true;
#undef UH20_OS
#define UH20_OS 8
#else
    static constexpr bool kRef = false;
#endif
#ifndef UH20_OS
#define UH20_OS 8
#endif
    // the oversampling factor: UH20_OS (8) at 44.1/48 kHz; at higher host rates the same internal rate (about 384 kHz), which is
    // what the solvers and the aliasing budget were set at: 4x at 88.2/96 kHz, 2x at 176.4/192 kHz (the reference: 8x at any rate)
    static int osFor(double sr) { return kRef || sr < 80000 ? UH20_OS : std::max(1, sr < 160000 ? UH20_OS / 2 : UH20_OS / 4); }
    int OS = UH20_OS;
    jk::GateClamp gc1, gcD, gc2, gc3, gcL;   // the gates' clamps (early-out biases from jk::gateOff, per knob move)
    jk::DiodeStringSolver led;
    // the stage solves: the reference engine's general ones, or the analytic ones (the same roots, to rounding)
    static inline double gate(double vth, double rth, double vs, jk::GateClamp& c) { if constexpr (kRef) return jk::gateClamp(vth, rth, vs); else return c(vth, rth, vs); }
    static inline double stage(jk::JStage& q, double vg, double G, double J) {
        if constexpr (kRef) return q.tick(vg, [&](double v) { return G * v - J; }); else return q.tickLin(vg, G, J);
    }
    Params p;
    double srate = 48000, FSI = 384000;
    Butter8 aaup, aadn;
    jk::JStage q1, qd, q2, q3;     // input stage, Drive stage, the two gain stages
    jk::LongTailPair ltp;
    jk::Network<10> rN1;  // Q1 drain -> 4.7k -> A -> Mark III stack -> Volume (bright) -> 22n -> Q2 gate (1M); A -> 0.47u -> LEDs
    jk::Network<3> rN2;   // Q2 drain -> 22n -> 100k -> Q3 gate (6.8k)
    jk::Network<5> rN3;   // Q3 drain -> 100n -> 47k -> Master (1M log) -> 0.1u -> LTP gate (1M)
    // DRIVE stage parts (v0.3.1): coupling cap, the resistors above and below the 100k pot, gate stopper, drain resistor and the
    // capacitor across it, source resistor and its (partial) bypass
    struct DriveParts { double Cc = 47e-9, Rtop = 100e3, Rbot = 3300, Rstop = 47e3, Rd = 68e3, Cd = 470e-12, Rs = 1500, Cs = 0.68e-6; } dp;
    jk::Network<5> dN;    // Drive: Q1 follower -> 1k -> 47n -> 100k -> 100k log pot (3.3k below it) -> 47k gate stopper -> Drive stage gate
    jk::Network<1> dL;    // Drive stage drain: 470p across its 68k drain resistor
    jk::Network<2> cpl;   // LTP drain 2 -> 1u -> LM3886 + input (47k)
    jk::Network<2> fb;    // speaker -> 100k -> LTP gate 2 (4.7k; Presence: 0.1u + 5k)
    Biquad chp, cp1, cp2, clp1, clp2;
    double a1 = 0, a2 = 0, csc = 4.12 / 14.12, gout = 1;
    bool jon = false, don = false, built = false; int xf = 0, XF = 32768;   // Drive switch fade: ~85 ms
    double rj = 1e9, rsup = 0.3, vout = 0, vposR = 30, vnegR = 30, ph = 0;
    // LEDs: a red LED one way, two in series the other (1.8 V at 10 mA each)
    static constexpr double NVT1 = 2.0 * jk::VT, NVT2 = 4.0 * jk::VT;
    const double ISLED = 0.01 / std::exp(1.8 / NVT1), ISLED2 = 0.01 / std::exp(3.6 / NVT2);
    // supply: 22 V AC secondary (31 V peak), bridge, 4700 uF per rail, LM3886 quiescent 50 mA
    static constexpr double VPK = 31.1 - 0.9, IQ = 0.05, DROP = 3.0;
    double CRES = 4700e-6;
    double railRest() const { return VPK - IQ * rsup * 3; }
    // SAG: the speaker current (0.1 ohm in the speaker's return, precision full-wave rectifier, 1 V/A), smoothed 2 ms, scaled by the
    // Sag pot and a gain of 4.8, limited at 5 V (the control amp's 5 V rail), smoothed again (100k / 0.68 uF, 68 ms), drives a 30k
    // V-to-I whose PNP mirror sources that current into the long-tailed pair's tail: the pair carries less, so the power section's
    // ceiling (the pair's swing, tail current x drain resistor) comes down under hard playing and recovers as it eases
    static constexpr double SAG_K = 4.8, SAG_VMAX = 5.0, SAG_TS = 2e-3, SAG_TC = 100e3 * 0.68e-6, SAG_RVI = 30e3;
    double ksag = 0, isense = 0, vctl = 0;
    double psin = 0, pcos = 1, rsin = 0, rcos = 1, grsup = 1, kres = 0, ksense = 0, kctl = 0;   // the default build's phasor and reciprocals

    // rail-to-rail op-amp follower on the 18 V preamp supply (e.g. OPA2197): unity gain, limited at its rails
    static double buf(double v) { return std::clamp(v, 0.02, VPRE - 0.02); }

    inline double amp(double vin) {
        const double g1 = gate(vin, 1e3, q1.vs, gc1);
        const double d1 = stage(q1, g1, 0, 0);   // drain drives an op-amp follower only
        double b1 = buf(d1);
        UH20_LAP(2);
        if (don || xf > 0) {   // DRIVE: Q1's follower -> Drive pot -> the cascaded J201 -> its own follower, in place of Q1's
            // the gate conducts when driven hard: solve it against the network's Thevenin at the gate and put that current back
            // into the network, so the coupling capacitor charges (bias shift) as in the circuit
            const double voc = dN.injV(b1), gd = gate(voc, dN.Zi, qd.vs, gcD), ig = (voc - gd) / dN.Zi;
            dN.process(b1, -ig);
            const double bs = buf(stage(qd, gd, dL.portG(), dL.portJ()));
            dL.process(qd.vd);
            if (xf > 0) {   // the switch moving while playing: a short crossfade of the signal and a slower glide of the DC level,
                            // so it neither clicks nor thumps (the circuit's switch would pop; the plug-in does not need to)
                const double t = 1.0 - (double)--xf / XF, wa = std::min(1.0, 8 * t), wd = t * t * (3 - 2 * t);
                const double on = don ? wa : 1 - wa, dc = don ? wd : 1 - wd;
                b1 = (1 - on) * (b1 - q1.vd0) + on * (bs - qd.vd0) + q1.vd0 + dc * (qd.vd0 - q1.vd0);
            } else b1 = bs;
        }
        UH20_LAP(3);
        double il = 0;
        if (jon) { if constexpr (kRef) il = jk::diodeString(rN1.injV(b1), rj + rN1.Zi, ISLED, NVT1, ISLED2, NVT2); else il = led.solve(rN1.injV(b1), rj + rN1.Zi, ISLED, NVT1, ISLED2, NVT2); }
        UH20_LAP(4);
        const double w = rN1.process(b1, -il);
        UH20_LAP(5);
        const double d2 = stage(q2, gate(w, rN1.rOut, q2.vs, gc2), rN2.portG(), rN2.portJ());
        UH20_LAP(6);
        const double g3 = rN2.process(d2);
        const double d3 = stage(q3, gate(g3, rN2.rOut, q3.vs, gc3), 0, 0);
        UH20_LAP(7);
        const double g1raw = rN3.process(buf(d3)), g1rout = rN3.rOut;
        UH20_LAP(8);
        // power section: long-tailed pair (gate 2 = the feedback network's output, from last sample's speaker voltage)
        const double gp = gate(g1raw, g1rout, ltp.vt, gcL);
        double dp; if constexpr (kRef) dp = ltp.tick(gp, fb.outV(vout), cpl.portG(), cpl.portJ()); else dp = ltp.tickFast(gp, fb.outV(vout), cpl.portG(), cpl.portJ());
        UH20_LAP(9);
        const double vp = cpl.process(dp);
        // LM3886 into the speaker, limited by its rails (a few volts of dropout)
        vout = std::clamp(GLM * vp, -(vnegR - DROP), vposR - DROP);
        fb.process(vout);
        // rails: full-wave rectified 60 Hz secondary through the supply resistance (winding + Sag) into the reservoirs
        double vsec;
        if constexpr (kRef) {
            ph += 60.0 / FSI; if (ph >= 1) ph -= 1;
            vsec = VPK * std::abs(std::sin(2 * kPi * ph)) + 0.9 - 0.9;
        } else {   // the same sine from a rotating phasor, re-anchored to the exact phase every cycle
            ph += 60.0 / FSI;
            if (ph >= 1) { ph -= 1; psin = std::sin(2 * kPi * ph); pcos = std::cos(2 * kPi * ph); }
            else { const double sn = psin * rcos + pcos * rsin; pcos = pcos * rcos - psin * rsin; psin = sn; }
            vsec = VPK * std::abs(psin);
        }
        if constexpr (kRef) {
            const double ip = (vout > 0 ? vout / RL : 0) + IQ, in = (vout < 0 ? -vout / RL : 0) + IQ;
            vposR += ((vsec > vposR ? (vsec - vposR) / rsup : 0) - ip) / (CRES * FSI);
            vnegR += ((vsec > vnegR ? (vsec - vnegR) / rsup : 0) - in) / (CRES * FSI);
            // Sag: the speaker current, full-wave rectified and smoothed, sets the current the transconductor pulls from the regulator's
            // adjust node (limited); the rail falls by that current times R2, through R2 || Cadj
            isense += (std::abs(vout) / RL - isense) / (SAG_TS * FSI);
            vctl += (std::min(ksag * isense, SAG_VMAX) - vctl) / (SAG_TC * FSI);
            ltp.iinj = vctl / SAG_RVI;
        } else {   // the same with the divisions as precomputed reciprocals
            const double il = vout * (1 / RL), ip = (vout > 0 ? il : 0) + IQ, in = (vout < 0 ? -il : 0) + IQ;
            vposR += ((vsec > vposR ? (vsec - vposR) * grsup : 0) - ip) * kres;
            vnegR += ((vsec > vnegR ? (vsec - vnegR) * grsup : 0) - in) * kres;
            isense += (std::abs(il) - isense) * ksense;
            vctl += (std::min(ksag * isense, SAG_VMAX) - vctl) * kctl;
            ltp.iinj = vctl * (1 / SAG_RVI);
        }
        UH20_LAP(10);
        // outputs: REC = the speaker voltage through the 1/20 divider into the cab simulator; AMP = the same, uncabbed
        const double y = vout * KREC;
        if (p.output == 1) return y;
        double c = chp.process(y * csc);
        c = c + a1 * cp1.process(c) + a2 * cp2.process(c);
        c = clp2.process(clp1.process(c));
        UH20_LAP(11);
        return c;
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
        // ---- DRIVE: a cascaded J201 of the same family (Rd 68k || 470p, Rs 1.5k || 0.68u), fed through 47n and a 100k log pot with
        // 100k above it and 3.3k below it (so the stage's input runs from 1/60 of Q1's output just past the switch to 1/2 at 10: the low
        // end starts just above the clean tone's level, the top is the most gain the design allows), into the gate through a 47k stopper; at 0 a switch takes the
        // stage out (Q1 drives the stack, as v0.2). The stopper limits the gate current when the gate conducts and the small coupling
        // capacitor lets the bias recover in milliseconds, so a hard pick cannot block the stage; the partial bypass trims the lows
        // ahead of the clipping and the 470p the fizz after it.
        const bool donNew = p.drive >= 0.05;
        qd.Vdd = VPRE; qd.Rd = dp.Rd; qd.src.clear(); qd.src.R(0, -1, dp.Rs); qd.src.C(0, -1, dp.Cs); qd.build(fs);
        { auto& n = dL; n.clear(); n.rs = 1; n.C(0, -1, dp.Cd); n.out = 0; n.build(fs); }   // Vdd is AC ground: the cap sits across Rd
        if (donNew || first) {   // switched out, the stage keeps its last pot setting while it fades away
            const double dv = clampd(taperA(p.drive / 10), 0.0005, 0.9995);
            auto& n = dN; n.clear(); n.rs = 1000; n.inj = 4; n.C(0, 1, dp.Cc); n.R(1, 2, dp.Rtop); n.R(2, 3, 100e3 * (1 - dv)); n.R(3, -1, 100e3 * dv + dp.Rbot);
            n.R(3, 4, dp.Rstop); n.out = 4; n.build(fs);
        }
        if (donNew != don && !first) {
            if (donNew && xf == 0) { qd.settle(); dN.initDC(q1.vd0); dL.initDC(qd.vd0); }   // switching the stage in: it starts at its operating point
            xf = XF - xf;                                                  // a reversal mid-fade continues from where it is
        }
        don = donNew;
        // Miller input capacitance of each J201 at its operating point, at its gate node
        // ---- power: long-tailed pair (J201 x2, 22k drains, 30k tail, +/-15 V), coupling into the LM3886, feedback with Presence
        { auto& n = cpl; n.clear(); n.rs = 1; n.C(0, -1, CCOMP); n.C(0, 1, 1e-6); n.R(1, -1, 47e3); n.out = 1; n.build(fs); }   // CCOMP: dominant-pole compensation at the pair's output
        { auto& n = fb; n.clear(); n.rs = 100e3; n.R(0, -1, 4700); n.C(0, 1, 0.1e-6); n.R(1, -1, 5000 * (1 - clampd(p.presence / 10, 0, 1)) + 47); n.out = 0; n.build(fs); }
        rsup = 0.3; grsup = 1 / rsup; kres = 1 / (CRES * fs); ksense = 1 / (SAG_TS * fs); kctl = 1 / (SAG_TC * fs);
        rsin = std::sin(2 * kPi * 60.0 / fs); rcos = std::cos(2 * kPi * 60.0 / fs);
        // rsup: the secondary's winding resistance (v0.3's Sag rheostat in the supply is gone: Sag acts on the pair, above)
        ksag = SAG_K * clampd(p.sag / 10, 0, 1);
        // cab simulator: Sallen-Key high-pass, two MFB band-passes summed in, two Sallen-Key low-passes (active filters on E96 values),
        // fitted through this circuit at Volume 5, Treble 3, Middle 7, Bass 5, Presence 5, Master 6.5 to the measured studio clean
        // tone: 1.39 dB rms, 60 Hz - 12.5 kHz
        chp.hp2(101.090, 1.2775, fs);
        cp1.bp2(276.845, 1.4134, fs); a1 = std::pow(10.0, 10.5098 / 20) - 1;
        cp2.bp2(2791.552, 2.7093, fs); a2 = std::pow(10.0, 9.8481 / 20) - 1;
        clp1.lp2(4855.70, 1.1474, fs); clp2.lp2(4855.70, 1.1474, fs);
        gout = std::pow(10.0, p.trimDb / 20) / VFS;   // REC and AMP read with the same calibration (v0.4.0: 0.5 V and 2 V)
        gc1.vOff = jk::gateOff(1e3); gcD.vOff = jk::gateOff(dN.Zi); gc2.vOff = jk::gateOff(rN1.rOut); gc3.vOff = jk::gateOff(rN2.rOut); gcL.vOff = jk::gateOff(rN3.rOut);
        if (!first && force) {}
        (void)force;
    }
};

} // namespace uh20

// Unterholz UH-20 — circuit model of a JFET guitar preamp (clean "Rhythmus" channel and Marshall-style "Lead" channel),
// a long-tailed-pair power stage with supply sag, and an analog cab-sim output.
// This is a line-for-line port of the REAPER JSFX reference implementation (jsfx/Unterholz UH-20.jsfx).
// Signals are modelled in volts. SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cmath>
#include <algorithm>
#include <initializer_list>

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

inline double th(double x) { return x > 20 ? 1.0 : (x < -20 ? -1.0 : std::tanh(x)); }

// JFET stage: nearly linear with a square-law even harmonic that levels off, then a knee toward cutoff (lu) / ohmic region (ld)
inline double jshape(double v, double lu, double ld, double p, double c) {
    v = v - c * v * v / (1 + std::pow(std::abs(v) / 3, 3));
    return v > 0 ? v / std::pow(1 + std::pow(v / lu, p), 1 / p) : v / std::pow(1 + std::pow(-v / ld, p), 1 / p);
}

inline double taperA(double x) { return x <= 0.5 ? 0.2 * x : 0.1 + 1.8 * (x - 0.5); }
inline double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

struct Params {
    double volume = 5, treble = 3, middle = 7, bass = 5, presence = 5, jubilee = 0, sag = 0, master = 6.5;
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
        srate = sampleRate; FSI = srate * OS; KI = 2 * FSI;
        GCT = 2 * CT * FSI; GCB = 2 * CBs * FSI; GCM = 2 * CM * FSI;
        reset(); update(p, true);
    }
    void reset() {
        for (Biquad* b : {&q1, &volf, &cc2, &q2, &cc4, &q3, &cc3, &pres, &lhp, &mlb, &mmd, &mtr, &chp, &cp1, &cp2, &clp1, &clp2}) b->reset();
        aaup.reset(); aadn.reset();
        jct = jcb = jcm = ict = icb = icm = 0; ledvd = 0; envs = 0;
    }
    void setParams(const Params& np) { if (!(np == p)) update(np, false); }

    // processes a mono buffer in place (input volts scale is applied inside)
    void process(float* buf, int n) {
        for (int i = 0; i < n; ++i) buf[i] = (float)tick(buf[i]);
    }
    double tick(double in) {
        const double xin = in * p.inputVolts;
        double ydec = 0;
        for (int k = 0; k < OS; ++k) {
            const double u = aaup.process(k == 0 ? xin * OS : 0.0);
            const double v1 = jshape(q1.process(u), 6, 10, 6, H2C);
            const double il = jon ? led(v1, RSRC + rj) : 0.0;
            double yy;
            if (p.channel == 0) {
                const double w = stack(v1 - il * RSRC);
                const double g2 = cc2.process(volf.process(w));
                const double v2 = jshape(q2.process(g2), 9, 6.8, 4, 0.01);
                const double g3 = cc4.process(v2) * 6.8 / 106.8;
                const double v3 = jshape(q3.process(g3), 9, 6.8, 4, 0.01);
                double po = pres.process(cc3.process(v3) * 10 / 160);
                po = 8.3 * th(po / 8.3);
                const double lp = 1 - (0.06 + sagd * 0.5) * std::min(envs, 1.0);
                yy = lp * th(po * pdrive / lp);
            } else {
                const double la = jshape(LGA * ldrv * (v1 - il * RSRC * 0.5), 9, 6.8, LPA, LCA);
                double lb = jshape(LGB * lhp.process(la), LBU, LBD, LPB, LCB);
                lb = lb > 0 ? LLC * th(lb / LLC) : lb;
                lb = mtr.process(mmd.process(mlb.process(lb)));
                const double lu = pres.process(lb) * lpdrv;
                const double lp = 1 - (0.06 + sagd * 0.5) * std::min(envs, 1.0);
                const double r = lu / lp;
                yy = lp * r / std::pow(1 + std::pow(std::abs(r), LPP), 1 / LPP);
            }
            const double a = std::abs(yy);
            envs += (a > envs ? att : rel) * (a - envs);
            double zz;
            if (p.output == 1) zz = yy;
            else {
                double c = chp.process(yy * csc);
                c = c + a1 * cp1.process(c) + a2 * cp2.process(c);
                zz = clp2.process(clp1.process(c));
            }
            zz = aadn.process(zz);
            if (k == 0) ydec = zz;
        }
        return ydec * gout;
    }

private:
    static constexpr int OS = 8;
    static constexpr double GM = 0.0009, VT = 0.02585, NLED = 2.0;
    const double ISLED = 0.01 / std::exp(1.8 / (NLED * VT));
    static constexpr double RSRC = 55000 + 4700, CT = 250e-12, CBs = 100e-9, CM = 47e-9;
    static constexpr double LGA = 45.3133, LPA = 5.1334, LCA = 0.0, LGB = 0.3514, LBU = 11.8858, LBD = 1.6598,
                            LPB = 2.1891, LCB = 0.0, LLC = 8.9370, LLG = 0.02909, LPP = 9.3287, H2C = 0.051;

    Params p;
    double srate = 48000, FSI = 384000, KI = 768000, GCT = 0, GCB = 0, GCM = 0;
    Biquad q1, volf, cc2, q2, cc4, q3, cc3, pres, lhp, mlb, mmd, mtr, chp, cp1, cp2, clp1, clp2;
    Butter8 aaup, aadn;
    bool jon = false;
    double rj = 0, pdrive = 1, sagd = 0, ldrv = 1, lpdrv = 1, a1 = 0, a2 = 0, csc = 4.12 / 14.12, att = 0, rel = 0, gout = 1;
    double envs = 0, ledvd = 0;
    // tone stack (6-node nodal model, trapezoidal capacitors)
    double MI[36] = {}, jct = 0, jcb = 0, jcm = 0, ict = 0, icb = 0, icm = 0;

    void stackBuild() {
        double M[36] = {};
        auto stamp = [&](int a, int b, double g) {
            if (a >= 0) M[a * 6 + a] += g;
            if (b >= 0) M[b * 6 + b] += g;
            if (a >= 0 && b >= 0) { M[a * 6 + b] -= g; M[b * 6 + a] -= g; }
        };
        const double t = clampd(p.treble / 10, 0.0004, 0.9996), b = std::max(taperA(p.bass / 10), 0.0004), m = std::max(p.middle / 10, 0.004);
        stamp(0, -1, 1 / RSRC);
        stamp(1, 2, 1 / (250000 * (1 - t))); stamp(2, 3, 1 / (250000 * t));
        stamp(0, 4, 1 / 100000.0); stamp(3, 5, 1 / (250000 * b)); stamp(5, -1, 1 / (25000 * m)); stamp(2, -1, 1 / 1000000.0);
        stamp(0, 1, GCT); stamp(4, 3, GCB); stamp(4, 5, GCM);
        double A[72];
        for (int i = 0; i < 6; ++i) for (int j = 0; j < 6; ++j) { A[i * 12 + j] = M[i * 6 + j]; A[i * 12 + 6 + j] = (i == j); }
        for (int k = 0; k < 6; ++k) {
            const double piv = A[k * 12 + k];
            for (int j = 0; j < 12; ++j) A[k * 12 + j] /= piv;
            for (int i = 0; i < 6; ++i) if (i != k) { const double f = A[i * 12 + k]; for (int j = 0; j < 12; ++j) A[i * 12 + j] -= f * A[k * 12 + j]; }
        }
        for (int i = 0; i < 6; ++i) for (int j = 0; j < 6; ++j) MI[i * 6 + j] = A[i * 12 + 6 + j];
    }
    inline double stack(double vs) {
        double R[6] = {vs / RSRC, 0, 0, 0, 0, 0}, V[6];
        R[0] += jct; R[1] -= jct; R[4] += jcb; R[3] -= jcb; R[4] += jcm; R[5] -= jcm;
        for (int i = 0; i < 6; ++i) { double s = 0; for (int j = 0; j < 6; ++j) s += MI[i * 6 + j] * R[j]; V[i] = s; }
        const double vct = V[0] - V[1], vcb = V[4] - V[3], vcm = V[4] - V[5];
        ict = GCT * vct - jct; icb = GCB * vcb - jcb; icm = GCM * vcm - jcm;
        jct = GCT * vct + ict; jcb = GCB * vcb + icb; jcm = GCM * vcm + icm;
        return V[2];
    }
    // LED pair behind the Jubilee pot: solve vo = x + rt*Is*(exp(x/(n*VT))-1) for the LED voltage x (safeguarded Newton)
    inline double led(double vo, double rt) {
        const double dir = vo >= 0 ? 1 : -1, n = dir > 0 ? NLED : 2 * NLED, av = std::abs(vo);
        double lo = 0, hi = av, x = ledvd * dir;
        if (x < lo || x > hi) x = 0.5 * av;
        for (int it = 0; it < 10; ++it) {
            const double ex = std::exp(x / (n * VT)), f = av - x - rt * ISLED * (ex - 1), df = -1 - rt * ISLED * ex / (n * VT);
            if (f > 0) lo = x; else hi = x;
            const double xn = x - f / df;
            x = (xn > lo && xn < hi) ? xn : 0.5 * (lo + hi);
        }
        ledvd = dir * x;
        return dir * ISLED * (std::exp(x / (n * VT)) - 1);
    }

    void update(const Params& np, bool force) {
        const bool stackChanged = force || np.treble != p.treble || np.middle != p.middle || np.bass != p.bass;
        p = np;
        const double KIx = KI;
        // Q1: J201 at ~0.1 mA, Rd 68k, Rs = 3.3k + 1.5k || 0.68u (Shift adds 22u); Jubilee dual-gang (470k LED gang + click-off, 100k gain gang)
        const double GM1 = 0.00061, RTH1 = 55000;
        jon = p.jubilee >= 0.05;
        rj = 220 + 470000 * taperA(1 - p.jubilee / 10);
        const double rg = 220 + 100000 * taperA(1 - p.jubilee / 10);
        const double ru = jon ? 3300 * rg / (3300 + rg) : 3300;
        const double cb1 = 0.68e-6 + (p.shift ? 22e-6 : 0), tb1 = 1500 * cb1;
        q1.firstOrder(-GM1 * RTH1, -GM1 * RTH1 * tb1, 1 + GM1 * (ru + 1500), tb1 * (1 + GM1 * ru), KIx);
        if (stackChanged) stackBuild();
        // Volume 1M log with an optional 100p bright cap across the upper section
        const double vv = clampd(taperA(p.volume / 10), 0.0005, 0.9995), rup = 1e6 * (1 - vv), rlo = 1e6 * vv, cbr = p.bright ? 100e-12 : 0;
        volf.firstOrder(rlo, rlo * rup * cbr, rlo + rup, rlo * rup * cbr, KIx);
        cc2.firstOrder(0, 22e-9 * 1e6, 1, 22e-9 * 1e6, KIx);
        q2.firstOrder(-GM * 24700, -GM * 24700 * 2200 * 22e-6, 1 + GM * 2200, 2200 * 22e-6, KIx);
        cc4.firstOrder(0, 22e-9 * 106800, 1, 22e-9 * 106800, KIx);
        q3.firstOrder(-GM * 24700, -GM * 24700 * 2200 * 22e-6, 1 + GM * 2200, 2200 * 22e-6, KIx);
        cc3.firstOrder(0, 100e-9 * 160000, 1, 100e-9 * 160000, KIx);
        const double rp = 4700 + 50000 * (1 - p.presence / 10);
        pres.firstOrder(1, 22e-9 * (rp + 10000), 1, 22e-9 * rp, KIx);
        pdrive = std::pow(10.0, (-3.5 + 3.6 * p.master) / 20);
        sagd = p.sag / 10;
        ldrv = std::pow(10.0, (-30 + 4 * p.drive) / 20);
        lhp.firstOrder(0, 1, 2 * kPi * 106.28, 1, KIx);
        lpdrv = LLG * std::pow(10.0, (-2 + 3.2 * p.master) / 20);
        mlb.peak(120, (p.bass - 5) * 2.2, 0.7, FSI);
        mmd.peak(650, (p.middle - 5) * 1.8 - 2, 0.8, FSI);
        mtr.peak(2500, (p.treble - 5) * 2.0, 0.7, FSI);
        aaup.set(18000, FSI); aadn.set(18000, FSI);
        chp.hp2(98.120, 0.7254, FSI);
        cp1.bp2(258.815, 1.1821, FSI); a1 = std::pow(10.0, 9.7496 / 20) - 1;
        cp2.bp2(3002.089, 2.0740, FSI); a2 = std::pow(10.0, 10.6133 / 20) - 1;
        clp1.lp2(4943.99, 1.0067, FSI); clp2.lp2(4943.99, 1.0067, FSI);
        att = 1 - std::exp(-1 / (0.015 * FSI)); rel = 1 - std::exp(-1 / (0.12 * FSI));
        gout = std::pow(10.0, p.trimDb / 20) / (p.output == 1 ? 2.0 : 0.5);
    }
};

} // namespace uh20

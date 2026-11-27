// JfetKit: the vendor J201 model (onsemi/Fairchild, the same .model ngspice runs: hardware/spice/j201.lib) and the circuit
// blocks the UH-20 is built from, each solved from that model. Signals are in volts. SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace jk {

// ---------------------------------------------------------------- linear networks (state-space, solved once per knob move)
template <int N>
struct Network {
    // Solved once per rebuild into state-space form: the state is the trapezoidal history current of each C and L,
    // s' = A s + B vs and out = C s + D vs, so a sample costs K^2 + 2K multiplies (K reactive parts) instead of a full nodal solve.
    // The state survives a rebuild (a pot moving), matched part by part, so turning a knob does not discharge the capacitors.
    struct El { int a, b, type; double val, g = 0, s = 0; };   // type 0 R, 1 C, 2 L; s: history current
    static constexpr int KMAX = 6;
    std::vector<El> el, old;
    double rs = 1;
    int out = N - 1, K = 0, ri[KMAX] = {};
    double A[KMAX * KMAX] = {}, Bv[KMAX] = {}, Cv[KMAX] = {}, D = 0, st[KMAX] = {};
    double P0[KMAX] = {}, PD = 0, rOut = 0;   // node-0 voltage = PD vs + P0.st (for the current drawn); Thevenin resistance at out
    void clear() { for (int k = 0; k < K; ++k) el[(size_t)ri[k]].s = st[k]; old = el; el.clear(); }
    void R(int a, int b, double r) { el.push_back({a, b, 0, r}); }
    void C(int a, int b, double c) { el.push_back({a, b, 1, c}); }
    void L(int a, int b, double l) { el.push_back({a, b, 2, l}); }
    void build(double fs) {
        double M[N * N] = {}, MI[N * N];
        auto stamp = [&](int a, int b, double g) {
            if (a >= 0) M[a * N + a] += g;
            if (b >= 0) M[b * N + b] += g;
            if (a >= 0 && b >= 0) { M[a * N + b] -= g; M[b * N + a] -= g; }
        };
        stamp(0, -1, 1 / rs);
        K = 0;
        std::vector<bool> used(old.size(), false);
        for (size_t n = 0; n < el.size(); ++n) {
            auto& e = el[n];
            e.g = e.type == 0 ? 1 / e.val : (e.type == 1 ? 2 * e.val * fs : 1 / (2 * e.val * fs));
            stamp(e.a, e.b, e.g);
            if (e.type == 0) continue;
            ri[K++] = (int)n; e.s = 0;
            for (size_t o = 0; o < old.size(); ++o)
                if (!used[o] && old[o].type == e.type && old[o].a == e.a && old[o].b == e.b && old[o].val == e.val) { e.s = old[o].s; used[o] = true; break; }
        }
        old.clear();
        double Ag[N * 2 * N];
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) { Ag[i * 2 * N + j] = M[i * N + j]; Ag[i * 2 * N + N + j] = (i == j); }
        for (int k = 0; k < N; ++k) {
            int pr = k; for (int i = k + 1; i < N; ++i) if (std::abs(Ag[i * 2 * N + k]) > std::abs(Ag[pr * 2 * N + k])) pr = i;
            if (pr != k) for (int j = 0; j < 2 * N; ++j) std::swap(Ag[k * 2 * N + j], Ag[pr * 2 * N + j]);
            const double piv = Ag[k * 2 * N + k];
            for (int j = 0; j < 2 * N; ++j) Ag[k * 2 * N + j] /= piv;
            for (int i = 0; i < N; ++i) if (i != k) { const double f = Ag[i * 2 * N + k]; if (f != 0) for (int j = 0; j < 2 * N; ++j) Ag[i * 2 * N + j] -= f * Ag[k * 2 * N + j]; }
        }
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) MI[i * N + j] = Ag[i * 2 * N + N + j];
        // node voltages for a unit source or a unit history current, then each part's next history current
        auto nodes = [&](double vs, int kUnit, double* V) {
            double rhs[N] = {}; rhs[0] = vs / rs;
            if (kUnit >= 0) { const auto& e = el[(size_t)ri[kUnit]]; if (e.a >= 0) rhs[e.a] -= 1; if (e.b >= 0) rhs[e.b] += 1; }
            for (int i = 0; i < N; ++i) { double t = 0; for (int j = 0; j < N; ++j) t += MI[i * N + j] * rhs[j]; V[i] = t; }
        };
        auto next = [&](const double* V, int j, double sj) {
            const auto& e = el[(size_t)ri[j]];
            const double v = (e.a >= 0 ? V[e.a] : 0) - (e.b >= 0 ? V[e.b] : 0);
            return e.type == 1 ? -(2 * e.g * v + sj) : 2 * e.g * v + sj;
        };
        double V[N];
        nodes(1, -1, V); D = V[out]; PD = V[0];
        { double rhs[N] = {}; rhs[out] = 1; double Vi[N]; for (int i = 0; i < N; ++i) { double t = 0; for (int j = 0; j < N; ++j) t += MI[i * N + j] * rhs[j]; Vi[i] = t; } rOut = Vi[out]; }
        for (int j = 0; j < K; ++j) Bv[j] = next(V, j, 0);
        for (int k = 0; k < K; ++k) {
            nodes(0, k, V); Cv[k] = V[out]; P0[k] = V[0];
            for (int j = 0; j < K; ++j) A[j * KMAX + k] = next(V, j, j == k ? 1 : 0);
        }
        for (int k = 0; k < K; ++k) st[k] = el[(size_t)ri[k]].s;
    }
    void reset() { for (auto& e : el) e.s = 0; for (auto& x : st) x = 0; }
    // start at the DC operating point for a source held at vsDC: capacitors charged (open), inductors carrying their current
    // (short); a capacitor's history current is then -g v, an inductor's +i
    void initDC(double vsDC) {
        double M[N * N] = {}, rhs[N] = {};
        auto stamp = [&](int a, int b, double g) {
            if (a >= 0) M[a * N + a] += g;
            if (b >= 0) M[b * N + b] += g;
            if (a >= 0 && b >= 0) { M[a * N + b] -= g; M[b * N + a] -= g; }
        };
        stamp(0, -1, 1 / rs); rhs[0] = vsDC / rs;
        for (int i = 0; i < N; ++i) M[i * N + i] += 1e-12;
        for (auto& e : el) { if (e.type == 0) stamp(e.a, e.b, 1 / e.val); else if (e.type == 2) stamp(e.a, e.b, 1e6); }
        for (int k = 0; k < N; ++k) {   // Gaussian elimination with partial pivoting
            int pr = k; for (int i = k + 1; i < N; ++i) if (std::abs(M[i * N + k]) > std::abs(M[pr * N + k])) pr = i;
            if (pr != k) { for (int j = 0; j < N; ++j) std::swap(M[k * N + j], M[pr * N + j]); std::swap(rhs[k], rhs[pr]); }
            for (int i = k + 1; i < N; ++i) { const double f = M[i * N + k] / M[k * N + k]; for (int j = k; j < N; ++j) M[i * N + j] -= f * M[k * N + j]; rhs[i] -= f * rhs[k]; }
        }
        double V[N]; for (int i = N - 1; i >= 0; --i) { double t = rhs[i]; for (int j = i + 1; j < N; ++j) t -= M[i * N + j] * V[j]; V[i] = t / M[i * N + i]; }
        for (int k = 0; k < K; ++k) {
            const auto& e = el[(size_t)ri[k]]; const double v = (e.a >= 0 ? V[e.a] : 0) - (e.b >= 0 ? V[e.b] : 0);
            st[k] = e.type == 1 ? -e.g * v : v * 1e6;   // inductor: its DC current through the short
        }
    }
    // current drawn from the source vs (through rs) this sample, before process(): i = (vs - V0)/rs, linear in vs
    inline double portG() const { return (1 - PD) / rs; }                      // d i / d vs
    inline double portJ() const { double v = 0; for (int k = 0; k < K; ++k) v += P0[k] * st[k]; return v / rs; }   // i = portG vs - portJ
    inline double process(double vs) {
        double y = D * vs, ns[KMAX];
        for (int k = 0; k < K; ++k) y += Cv[k] * st[k];
        for (int j = 0; j < K; ++j) { double t = Bv[j] * vs; for (int k = 0; k < K; ++k) t += A[j * KMAX + k] * st[k]; ns[j] = t; }
        for (int j = 0; j < K; ++j) st[j] = ns[j];
        return y;
    }
};


constexpr double VT = 0.025852;   // kT/q at 27 C

// SPICE NJF level 1 (Shichman-Hodges) with the J201 parameters; the drain/source series resistances (1 ohm each) are left out
struct J201 {
    static constexpr double Beta = 1.304e-3, Vto = -0.5961, Lambda = 2.8e-3, Is = 114.5e-15, N = 1, Isr = 1.091e-12, Nr = 2, M = 0.3876, Pb = 0.5;
    // drain current for vds >= 0 (the stages here never reverse it)
    static double id(double vgs, double vds) {
        const double vov = vgs - Vto;
        if (vov <= 0 || vds <= 0) return 0;
        const double cl = 1 + Lambda * vds;
        return vds >= vov ? Beta * vov * vov * cl : Beta * vds * (2 * vov - vds) * cl;
    }
    static constexpr double Cgd0 = 4.06e-12, Cgs0 = 3.002e-12, Fc = 0.5;
    // junction depletion capacitance at reverse bias v (SPICE form, linear above Fc*Pb)
    static double cj(double c0, double v) {
        if (v < Fc * Pb) return c0 / std::pow(1 - v / Pb, M);
        const double f2 = std::pow(1 - Fc, 1 + M), f3 = 1 - Fc * (1 + M);
        return c0 / f2 * (f3 + M * v / Pb);
    }
    // gate-junction current (gate to source), diffusion + recombination terms as in SPICE
    static double ig(double v) {
        const double diff = Is * (std::exp(std::min(v / (N * VT), 80.0)) - 1);
        const double fac = std::pow((1 - std::min(v, 0.99 * Pb) / Pb) * (1 - std::min(v, 0.99 * Pb) / Pb) + 0.005, M / 2);
        return diff + Isr * (std::exp(std::min(v / (Nr * VT), 80.0)) - 1) * fac;
    }
};

// drain voltage of a common-source stage (drain resistor Rd to Vdd) for a given gate-source voltage and source voltage
inline double drainVoltage(double vgs, double vs, double vdd, double rd) {
    double lo = vs, hi = vdd;
    for (int it = 0; it < 60; ++it) { const double m = 0.5 * (lo + hi); if (vdd - rd * J201::id(vgs, m - vs) > m) lo = m; else hi = m; }
    return 0.5 * (lo + hi);
}
// source voltage of a stage whose source resistor Rs is NOT bypassed (or of a source follower, rd = 0): vs = Rs * Id
inline void unbypassed(double vg, double vdd, double rd, double rs, double& vd, double& vs) {
    double lo = 0, hi = std::max(vg + 1.0, 0.0) + 1.0;   // vs is between 0 and just above the gate
    hi = std::max(hi, 1.0);
    for (int it = 0; it < 70; ++it) {
        const double m = 0.5 * (lo + hi), d = rd > 0 ? drainVoltage(vg - m, m, vdd, rd) : vdd;
        if (rs * J201::id(vg - m, d - m) > m) lo = m; else hi = m;
    }
    vs = 0.5 * (lo + hi); vd = rd > 0 ? drainVoltage(vg - vs, vs, vdd, rd) : vdd;
}

// ---------------------------------------------------------------- gate conduction
// a gate fed from a Thevenin source (vth, rth): the J201 gate junction conducts once forward-biased against the source.
// Solve (vth - vg)/rth = Ig(vg - vs) by bracketed Newton (monotone; always converges).
inline double gateClamp(double vth, double rth, double vs) {
    if (J201::ig(vth - vs) * rth < 1e-9) return vth;
    double lo = std::min(vth, vs) - 1, hi = vth, g = std::max(lo, std::min(hi, vs + 0.5));
    for (int it = 0; it < 80; ++it) {
        const double fv = (vth - g) / rth - J201::ig(g - vs);
        if (fv > 0) lo = g; else hi = g;
        const double h = 1e-7, df = ((vth - g - h) / rth - J201::ig(g + h - vs) - fv) / h;
        double n = df != 0 ? g - fv / df : 0.5 * (lo + hi); if (!(n > lo && n < hi)) n = 0.5 * (lo + hi);
        if (std::abs(n - g) < 1e-12) return n;
        g = n;
    }
    return g;
}

// ---------------------------------------------------------------- circuit blocks, solved exactly every sample
// Common-source J201 stage. Drain: Rd to Vdd, loaded by a linear network (its port current i = G vd - J). Source: Rs1 in series
// with (Rs2 || Cs) to ground (Cs = 0: Rs1 + Rs2 unbypassed). Both node voltages are linear in the drain current, so the
// stage reduces to one equation in Id, solved by bracketed Newton every sample.
struct CSStage {
    double Vdd = 18, Rd = 47e3, Rs1 = 0, Rs2 = 2200, Cs = 22e-6;
    double gc = 0, jc = 0, vc = 0;      // bypass capacitor companion (trapezoidal), its voltage
    double id = 0, vd = 0, vs = 0, vd0 = 0;
    void prepare(double fs) { gc = Cs > 0 ? 2 * Cs * fs : 0; settle(); }
    // small-signal input capacitance at the operating point (Miller): Cgs bootstrapped by an unbypassed source resistor,
    // Cgd multiplied by (1 + gain to the drain); rload: the AC load on the drain besides Rd (0 = none)
    double inputCapacitance(double rload = 0) const {
        const double gm = 2 * std::sqrt(J201::Beta * std::max(id, 1e-12)), rdl = rload > 0 ? Rd * rload / (Rd + rload) : Rd;
        const double rsu = Cs > 0 ? Rs1 : Rs1 + Rs2, a = gm * rdl / (1 + gm * rsu), as = gm * rsu / (1 + gm * rsu);
        return J201::cj(J201::Cgs0, -vs) * (1 - as) + J201::cj(J201::Cgd0, -vd) * (1 + a);
    }
    void settle() {   // DC operating point (capacitor open, no load current)
        double lo = 0, hi = Vdd / Rd;
        for (int it = 0; it < 80; ++it) { const double m = 0.5 * (lo + hi), s = m * (Rs1 + Rs2), d = Vdd - Rd * m;
            if (J201::id(-s, d - s) > m) lo = m; else hi = m; }
        id = 0.5 * (lo + hi); vs = id * (Rs1 + Rs2); vd = Vdd - Rd * id; vd0 = vd; vc = id * Rs2; jc = -gc * vc - 0; jc = gc * vc + (id - vc / Rs2) * 0;
        jc = gc * vc;   // capacitor at rest: i_c = gc*v - jc = 0
    }
    // vg: gate voltage; G, J: the drain load's port (i = G vd - J). Returns the drain voltage.
    inline double tick(double vg, double G, double J) {
        const double gr = 1 / Rs2, reff = Rs1 + 1 / (gr + gc), voff = jc / (gr + gc);
        const double gd = 1 / Rd + G, vdk = (Vdd / Rd + J) / gd;   // vd = vdk - Id/gd
        auto f = [&](double I) { const double s = reff * I + voff, d = vdk - I / gd; return J201::id(vg - s, d - s) - I; };
        double lo = 0, hi = std::max(Vdd / Rd + J, 1e-9) + 1e-6, I = std::clamp(id, lo, hi);
        for (int it = 0; it < 60; ++it) {
            const double fv = f(I); if (fv > 0) lo = I; else hi = I;
            const double h = 1e-9 + 1e-6 * I, df = (f(I + h) - fv) / h;
            double n = df != 0 ? I - fv / df : 0.5 * (lo + hi); if (!(n > lo && n < hi)) n = 0.5 * (lo + hi);
            if (std::abs(n - I) < 1e-13) { I = n; break; }
            I = n;
        }
        id = I; vs = reff * I + voff; vd = vdk - I / gd;
        // bypass node: vc = (I + jc)/(gr + gc); then advance the companion
        vc = (I + jc) / (gr + gc);
        const double ic = gc * vc - jc; jc = gc * vc + ic;
        return vd;
    }
};

// Source follower: drain at Vdd, Rs to ground, loaded by a network port. One equation in vs.
struct SourceFollower {
    double Vdd = 18, Rs = 10e3, vs = 0, vs0 = 0;
    void settle(double vgDC) { double lo = 0, hi = Vdd; for (int it = 0; it < 80; ++it) { const double m = 0.5 * (lo + hi); if (J201::id(vgDC - m, Vdd - m) > m / Rs) lo = m; else hi = m; } vs = vs0 = 0.5 * (lo + hi); }
    inline double tick(double vg, double G, double J) {
        auto f = [&](double s) { return J201::id(vg - s, Vdd - s) - s / Rs - (G * s - J); };
        double lo = -1, hi = Vdd, s = std::clamp(vs, lo, hi);
        for (int it = 0; it < 60; ++it) {
            const double fv = f(s); if (fv > 0) lo = s; else hi = s;
            const double h = 1e-7, df = (f(s + h) - fv) / h;
            double n = df != 0 ? s - fv / df : 0.5 * (lo + hi); if (!(n > lo && n < hi)) n = 0.5 * (lo + hi);
            if (std::abs(n - s) < 1e-11) { s = n; break; }
            s = n;
        }
        vs = s; return s;
    }
};

// A gain stage DC-coupled into a source follower (the cathode-follower pair): the follower's gate IS the gain stage's drain,
// and that gate conducts once it rises above the follower's source, loading the drain. Unknowns: gain-stage current I,
// drain voltage vd, follower source vs; solved together by damped Newton (numeric Jacobian) every sample.
struct GainFollower {
    CSStage a;                     // the gain stage (its Rd, source network, Vdd)
    double Rf = 10e3;              // follower source resistor
    double vf = 0, vf0 = 0;        // follower source voltage
    void prepare(double fs) {
        a.prepare(fs);
        // DC: follower gate current is tiny at rest but solve the pair anyway
        double x[3] = {a.id, a.vd, 0}; { double lo = 0, hi = a.Vdd; for (int it = 0; it < 80; ++it) { const double m = 0.5 * (lo + hi); if (J201::id(a.vd - m, a.Vdd - m) > m / Rf) lo = m; else hi = m; } x[2] = 0.5 * (lo + hi); }
        vf = vf0 = x[2];
    }
    inline double tick(double vg, double G, double J) {
        const double gr = 1 / a.Rs2, reff = a.Rs1 + 1 / (gr + a.gc), voff = a.jc / (gr + a.gc);
        auto F = [&](const double* x, double* f) {
            const double I = x[0], vd = x[1], s = x[2], vs = reff * I + voff;
            const double ig = J201::ig(vd - s);
            f[0] = J201::id(vg - vs, vd - vs) - I;                          // gain stage law
            f[1] = (a.Vdd - vd) / a.Rd - I - ig;                             // drain node KCL
            f[2] = J201::id(vd - s, a.Vdd - s) + ig - s / Rf - (G * s - J);   // follower source KCL
        };
        double x[3] = {a.id, a.vd, vf}, f[3];
        for (int it = 0; it < 40; ++it) {
            F(x, f); const double nrm = std::abs(f[0]) * 1e4 + std::abs(f[1]) * 1e4 + std::abs(f[2]) * 1e4;
            if (nrm < 1e-12) break;
            double Jm[9];
            for (int j = 0; j < 3; ++j) { double y[3] = {x[0], x[1], x[2]}, g[3]; const double h = j == 0 ? 1e-10 : 1e-7; y[j] += h; F(y, g); for (int i = 0; i < 3; ++i) Jm[i * 3 + j] = (g[i] - f[i]) / h; }
            // solve Jm dx = -f (3x3, Cramer)
            const double det = Jm[0] * (Jm[4] * Jm[8] - Jm[5] * Jm[7]) - Jm[1] * (Jm[3] * Jm[8] - Jm[5] * Jm[6]) + Jm[2] * (Jm[3] * Jm[7] - Jm[4] * Jm[6]);
            if (det == 0) break;
            double dx[3];
            for (int c = 0; c < 3; ++c) { double M[9]; for (int k = 0; k < 9; ++k) M[k] = Jm[k]; for (int r = 0; r < 3; ++r) M[r * 3 + c] = -f[r];
                dx[c] = (M[0] * (M[4] * M[8] - M[5] * M[7]) - M[1] * (M[3] * M[8] - M[5] * M[6]) + M[2] * (M[3] * M[7] - M[4] * M[6])) / det; }
            // limit the voltage steps (the junction exponentials) so Newton cannot overshoot
            double lim = 1; const double mx = std::max(std::abs(dx[1]), std::abs(dx[2])); if (mx > 0.5) lim = 0.5 / mx;
            for (int k = 0; k < 3; ++k) x[k] += lim * dx[k];
            x[0] = std::max(x[0], 0.0);
        }
        a.id = x[0]; a.vd = x[1]; vf = x[2]; a.vs = reff * x[0] + voff;
        a.vc = (x[0] + a.jc) / (gr + a.gc); const double ic = a.gc * a.vc - a.jc; a.jc = a.gc * a.vc + ic;
        return vf;
    }
};

} // namespace jk

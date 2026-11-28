// JfetKit: the vendor J201 model (onsemi/Fairchild, the same .model ngspice runs: hardware/spice/j201.lib) and the circuit
// blocks the UH-20 is built from, each solved from that model. Signals are in volts. SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace jk {

// UH20_PROFILE=1: solver call and iteration counts (tools/uh20_prof prints them); nothing otherwise
#if UH20_PROFILE
inline long count[16] = {};
#define JK_COUNT(k) (++jk::count[k])
#else
#define JK_COUNT(k) ((void)0)
#endif

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
    // current injection at node 'inj' (into the node): its effect on the state, the output, node 0 and the node itself
    int inj = -1; double Ei[KMAX] = {}, Yi = 0, Pi = 0, Zi = 0, Vi_vs = 0, Qi[KMAX] = {};
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
        if (rs > 0) stamp(0, -1, 1 / rs);
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
            double rhs[N] = {}; if (rs > 0) rhs[0] = vs / rs;
            if (kUnit == -2) { rhs[inj] += 1; }
            else if (kUnit >= 0) { const auto& e = el[(size_t)ri[kUnit]]; if (e.a >= 0) rhs[e.a] -= 1; if (e.b >= 0) rhs[e.b] += 1; }
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
        if (inj >= 0) Vi_vs = V[inj];
        for (int k = 0; k < K; ++k) {
            nodes(0, k, V); Cv[k] = V[out]; P0[k] = V[0]; if (inj >= 0) Qi[k] = V[inj];
            for (int j = 0; j < K; ++j) A[j * KMAX + k] = next(V, j, j == k ? 1 : 0);
        }
        if (inj >= 0) { nodes(0, -2, V); Yi = V[out]; Pi = V[0]; Zi = V[inj]; for (int j = 0; j < K; ++j) Ei[j] = next(V, j, 0); }
        for (int k = 0; k < K; ++k) st[k] = el[(size_t)ri[k]].s;
    }
    void reset() { for (auto& e : el) e.s = 0; for (auto& x : st) x = 0; }
    // start at the DC operating point for a source held at vsDC: capacitors charged (open), inductors carrying their current
    // (short); a capacitor's history current is then -g v, an inductor's +i
    void initDC(double vsDC, double iInjDC = 0) {
        double M[N * N] = {}, rhs[N] = {};
        auto stamp = [&](int a, int b, double g) {
            if (a >= 0) M[a * N + a] += g;
            if (b >= 0) M[b * N + b] += g;
            if (a >= 0 && b >= 0) { M[a * N + b] -= g; M[b * N + a] -= g; }
        };
        if (rs > 0) { stamp(0, -1, 1 / rs); rhs[0] = vsDC / rs; }
        for (int i = 0; i < N; ++i) M[i * N + i] += 1e-12;
        for (auto& e : el) { if (e.type == 0) stamp(e.a, e.b, 1 / e.val); else if (e.type == 2) stamp(e.a, e.b, 1e6); }
        if (inj >= 0) rhs[inj] += iInjDC;
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
    // with an injection node: its voltage before this sample's injection, its resistance, and the step with injection i
    inline double injV(double vs) const { double v = Vi_vs * vs; for (int k = 0; k < K; ++k) v += Qi[k] * st[k]; return v; }
    inline double portJi(double i) const { return portJ() + Pi * i / rs; }   // port current with injection: G vs - portJi(i)
    inline double outV(double vs, double i = 0) const { double y = D * vs + Yi * i; for (int k = 0; k < K; ++k) y += Cv[k] * st[k]; return y; }
    inline double process(double vs, double i) {
        double y = D * vs + Yi * i, ns[KMAX];
        for (int k = 0; k < K; ++k) y += Cv[k] * st[k];
        for (int j = 0; j < K; ++j) { double t = Bv[j] * vs + Ei[j] * i; for (int k = 0; k < K; ++k) t += A[j * KMAX + k] * st[k]; ns[j] = t; }
        for (int j = 0; j < K; ++j) st[j] = ns[j];
        return y;
    }
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
    // the same current with its partial derivatives (gm = d id / d vgs, gds = d id / d vds), for the analytic Newtons
    static inline double idd(double vgs, double vds, double& gm, double& gds) {
        const double vov = vgs - Vto;
        if (vov <= 0 || vds <= 0) { gm = gds = 0; return 0; }
        const double cl = 1 + Lambda * vds;
        if (vds >= vov) { const double q = Beta * vov * vov; gm = 2 * Beta * vov * cl; gds = q * Lambda; return q * cl; }
        const double t = vds * (2 * vov - vds); gm = 2 * Beta * vds * cl; gds = Beta * (2 * (vov - vds) * cl + t * Lambda); return Beta * t * cl;
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
    // the same current and its slope from one exponential (N = 1, Nr = 2: the diffusion term's exponential is the square of the
    // recombination term's) and one power
    static inline double igd(double v, double& slope) {
        const double e2 = std::exp(std::min(v / (Nr * VT), 40.0)), e1 = e2 * e2, u = 1 - std::min(v, 0.99 * Pb) / Pb, b = u * u + 0.005;
        const double fac = std::pow(b, M / 2), dfac = v < 0.99 * Pb ? -M * fac / b * u / Pb : 0.0;
        slope = Is * e1 / (N * VT) + Isr * (e2 / (Nr * VT) * fac + (e2 - 1) * dfac);
        return Is * (e1 - 1) + Isr * (e2 - 1) * fac;
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

// ---------------------------------------------------------------- solvers
inline long& unconverged() { static long n = 0; return n; }   // solves that ran out of iterations (tests check this stays 0)
inline long* unconvergedBy() { static long n[4] = {}; return n; }   // 0 stage, 1 pair, 2 gate clamp, 3 other

// Newton for n <= 4 unknowns with a numeric Jacobian and limited voltage steps; returns the final residual norm
template <int n, class F> double newton(F&& f, double* x, const double* scale, int maxIt = 40, double tol = 1e-11, bool count = true) {
    double r[n], r2[n], Jm[n * n], nrm = 0;
    for (int it = 0; it < maxIt; ++it) {
        f(x, r); nrm = 0; for (int i = 0; i < n; ++i) nrm = std::max(nrm, std::abs(r[i]));
        if (nrm < tol) return nrm;
        for (int j = 0; j < n; ++j) { double y[n]; for (int k = 0; k < n; ++k) y[k] = x[k]; const double h = 1e-7 * scale[j]; y[j] += h; f(y, r2); for (int i = 0; i < n; ++i) Jm[i * n + j] = (r2[i] - r[i]) / h; }
        double a[n * (n + 1)];   // Gaussian elimination on [J | -r]
        for (int i = 0; i < n; ++i) { for (int j = 0; j < n; ++j) a[i * (n + 1) + j] = Jm[i * n + j]; a[i * (n + 1) + n] = -r[i]; }
        for (int k = 0; k < n; ++k) {
            int p = k; for (int i = k + 1; i < n; ++i) if (std::abs(a[i * (n + 1) + k]) > std::abs(a[p * (n + 1) + k])) p = i;
            if (p != k) for (int j = 0; j <= n; ++j) std::swap(a[k * (n + 1) + j], a[p * (n + 1) + j]);
            if (a[k * (n + 1) + k] == 0) return nrm;
            for (int i = k + 1; i < n; ++i) { const double q = a[i * (n + 1) + k] / a[k * (n + 1) + k]; for (int j = k; j <= n; ++j) a[i * (n + 1) + j] -= q * a[k * (n + 1) + j]; }
        }
        double dx[n]; for (int i = n - 1; i >= 0; --i) { double t = a[i * (n + 1) + n]; for (int j = i + 1; j < n; ++j) t -= a[i * (n + 1) + j] * dx[j]; dx[i] = t / a[i * (n + 1) + i]; }
        double lim = 1; for (int i = 0; i < n; ++i) { const double m = std::abs(dx[i]) / (2 * scale[i]); if (m > lim) lim = m; }
        for (int i = 0; i < n; ++i) x[i] += dx[i] / lim;
    }
    if (count) ++unconverged();
    f(x, r); nrm = 0; for (int i = 0; i < n; ++i) nrm = std::max(nrm, std::abs(r[i]));
    return nrm;
}

// bracketed Newton for a strictly monotone residual f on [lo, hi] (f(lo), f(hi) of opposite signs); x is the start
template <class F> inline double solve1(F&& f, double lo, double hi, double x, double tol, double h) {
    double flo = f(lo); const bool inc = flo < 0;   // increasing if f(lo) < 0
    x = std::clamp(x, lo, hi);
    for (int it = 0; it < 80; ++it) {
        const double fx = f(x);
        if (std::abs(fx) < 1e-16) return x;
        if ((fx < 0) == inc) lo = x; else hi = x;
        if (hi - lo < tol) return 0.5 * (lo + hi);
        const double d = (f(x + h) - fx) / h;
        double n = d != 0 ? x - fx / d : 0.5 * (lo + hi);
        if (!(n > lo && n < hi)) n = 0.5 * (lo + hi);
        if (std::abs(n - x) < tol) return n;
        x = n;
    }
    ++unconverged(); ++unconvergedBy()[3];
    return x;
}

// a string of diodes/LEDs (saturation current is, emission voltage nvt) in series with rt, from a source voc: the exact current
// (Lambert's W: x = voc + k - nvt W(z), k = rt is, ln z = ln(k/nvt) + (voc + k)/nvt). Asymmetric strings take (isUp, nvtUp) for
// positive voc and (isDn, nvtDn) for negative.
inline double lambertW(double lnz) {
    double w;
    if (lnz < -2) { const double z = std::exp(lnz); w = z * (1 - z); }
    else if (lnz < 1.5) w = 0.567 * std::exp(0.6 * lnz);
    else w = lnz - std::log(lnz);
    for (int it = 0; it < 5; ++it) w = std::max(w * (1 + lnz - std::log(w)) / (1 + w), 1e-300);
    return w;
}
inline double diodeString(double voc, double rt, double isUp, double nvtUp, double isDn, double nvtDn) {
    const bool up = voc >= 0; const double is = up ? isUp : isDn, nvt = up ? nvtUp : nvtDn, av = std::abs(voc), k = rt * is;
    const double x = std::min(av + k - nvt * lambertW(std::log(k / nvt) + (av + k) / nvt), av);
    const double i = x / nvt < 30 ? is * std::expm1(x / nvt) : (av - x) / rt;
    return up ? i : -i;
}

// the same string solved by Newton on the drop across the diodes, x + rt is expm1(x/nvt) = |voc| (convex, rising), from last
// sample's drop, bracketed in [0, |voc|]; stops after a step under 1e-13 V; one exponential a step instead of Lambert's W's logs
struct DiodeStringSolver {
    double xUp = 0, xDn = 0;
    inline double solve(double voc, double rt, double isUp, double nvtUp, double isDn, double nvtDn) {
        const bool up = voc >= 0; const double is = up ? isUp : isDn, nvt = up ? nvtUp : nvtDn, av = std::abs(voc), k = rt * is, inv = 1 / nvt;
        double& xl = up ? xUp : xDn;
        double lo = 0, hi = av, x = std::clamp(xl, lo, hi);
        bool ok = false;
        JK_COUNT(8);
        for (int it = 0; it < 60; ++it) {
            JK_COUNT(9);
            const double e = std::exp(x * inv), h = x + k * (e - 1) - av;
            if (h > 0) hi = x; else lo = x;
            double n = x - h / (1 + k * inv * e); if (!(n >= lo && n <= hi)) n = 0.5 * (lo + hi);
            const bool done = std::abs(n - x) < 1e-13; x = n; if (done) { ok = true; break; }
        }
        if (!ok) return diodeString(voc, rt, isUp, nvtUp, isDn, nvtDn);
        xl = x;
        const double i = x * inv < 30 ? is * std::expm1(x * inv) : (av - x) / rt;
        return up ? i : -i;
    }
};

// ---------------------------------------------------------------- gate conduction
// a gate fed from a Thevenin source (vth, rth): the J201 gate junction conducts once forward-biased against the source.
// Solve (vth - vg)/rth = Ig(vg - vs) by bracketed Newton (monotone; always converges).
inline double gateClamp(double vth, double rth, double vs) {
    if (J201::ig(vth - vs) * rth < 1e-9) return vth;
    // (vth - g)/rth - Ig(g - vs) falls as g rises
    return solve1([&](double g) { return (vth - g) / rth - J201::ig(g - vs); }, std::min(vth, vs) - 1, vth, std::min(vth, vs + 0.5), 1e-10, 1e-7);
}
// the same clamp with an exact early-out: below a forward bias vOff (gateOff(rth), worked out once per knob move) the gate surely
// draws under 1e-9 V / rth, which is the test gateClamp makes with the junction's two exponentials and a power, so the answer is
// the same without evaluating them
inline double gateOff(double rth) {   // the largest bias at which Ig rth is still under 1e-9 (bisection; Ig rises with the bias)
    double lo = -1, hi = 1;
    for (int it = 0; it < 80; ++it) { const double m = 0.5 * (lo + hi); if (J201::ig(m) * rth < 1e-9) lo = m; else hi = m; }
    return lo - 1e-6;   // a margin: between it and the threshold gateClamp's own test decides
}
inline double gateClamp(double vth, double rth, double vs, double vOff) { if (vth - vs < vOff) return vth; JK_COUNT(10); return gateClamp(vth, rth, vs); }
// a gate's clamp with the exact early-out and, when it conducts, Newton with the junction's analytic slope from the last
// conducting sample's gate voltage, bracketed as gateClamp's; stops after a step under 1e-10 V (gateClamp's tolerance)
struct GateClamp {
    double vOff = 0, g = 0; bool was = false;
    inline double operator()(double vth, double rth, double vs) {
        if (vth - vs < vOff) { was = false; return vth; }
        if (J201::ig(vth - vs) * rth < 1e-9) { was = false; return vth; }
        JK_COUNT(10);
        double lo = std::min(vth, vs) - 1, hi = vth, x = was ? std::clamp(g, lo, hi) : std::min(vth, vs + 0.5);
        const double ir = 1 / rth;
        for (int it = 0; it < 60; ++it) {
            JK_COUNT(11);
            double sl; const double h = (vth - x) * ir - J201::igd(x - vs, sl);
            if (h > 0) lo = x; else hi = x;
            double n = x + h / (ir + sl); if (!(n >= lo && n <= hi)) n = 0.5 * (lo + hi);
            if (std::abs(n - x) < 1e-10 || hi - lo < 1e-10) { g = n; was = true; return n; }
            x = n;
        }
        was = false; return gateClamp(vth, rth, vs);
    }
};

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
        double I = a.id, vd = a.vd, sf = vf;
        auto F = [&](const double* x, double* f) {
            const double vs = reff * x[0] + voff, ig = J201::ig(x[1] - x[2]);
            f[0] = (J201::id(vg - vs, x[1] - vs) - x[0]) * 1e4;
            f[1] = ((a.Vdd - x[1]) / a.Rd - x[0] - ig) * 1e4;
            f[2] = (J201::id(x[1] - x[2], a.Vdd - x[2]) + ig - x[2] / Rf - (G * x[2] - J)) * 1e4;
        };
        double x[3] = {I, vd, sf}; const double sc[3] = {1e-4, 1.0, 1.0};
        if (!(newton<3>(F, x, sc, 12, 1e-11, false) < 1e-11 && x[0] >= 0)) {
            // robust path: for a drain voltage, J3's current and the follower's source are each a monotone 1-D solve; the drain
            // node's KCL then falls as the drain voltage rises
            double iL = I, sL = sf;
            auto Iof = [&](double v) { iL = solve1([&](double i) { const double vs = reff * i + voff; return J201::id(vg - vs, v - vs) - i; }, 0.0, 4e-3, iL, 1e-14, 1e-11); return iL; };
            auto Sof = [&](double v) { sL = solve1([&](double t) { return J201::id(v - t, a.Vdd - t) + J201::ig(v - t) - t / Rf - (G * t - J); }, -5.0, a.Vdd + 5, sL, 1e-10, 1e-6); return sL; };
            x[1] = solve1([&](double v) { return (a.Vdd - v) / a.Rd - Iof(v) - J201::ig(v - Sof(v)); }, -5.0, a.Vdd + 5, vd, 1e-10, 1e-6);
            x[0] = Iof(x[1]); x[2] = Sof(x[1]);
        }
        a.id = std::max(x[0], 0.0); a.vd = x[1]; vf = x[2]; a.vs = reff * a.id + voff;
        a.vc = (a.id + a.jc) / (gr + a.gc); const double ic = a.gc * a.vc - a.jc; a.jc = a.gc * a.vc + ic;
        return vf;
    }
};

// A J201 common-source stage with any source network (driven by the source current, Network with rs = 0, inj = 0) and any
// drain load given as a current function load(vd). Unknowns: drain current I, drain voltage vd; solved by Newton every sample.
struct JStage {
    double Vdd = 18, Rd = 47e3, id = 0, vd = 0, vs = 0, vd0 = 0, idPrev = 0, idPrev2 = 0, gLast = -1, igdC = 0;
    static constexpr double IMAX = 4 * J201::Beta * J201::Vto * J201::Vto * 4 + 1e-3;   // the bracket's top (any stage here: 18 V / Rd is less)
    static constexpr double TOL_I = 1e-10;   // tickLin's last step (A): quadratic convergence leaves the root within ~1e-16 A
    Network<3> src;
    void build(double fs) { src.rs = 0; src.inj = 0; src.out = 0; src.build(fs); }
    double vsFor(double I) const { return src.injV(0) + src.Zi * I; }
    void settle() {   // DC: capacitors open, no drain load current
        double lo = 0, hi = Vdd / Rd;
        for (int it = 0; it < 70; ++it) { const double m = 0.5 * (lo + hi); src.initDC(0, m); const double sv = vsFor(m), d = Vdd - Rd * m;
            if (J201::id(-sv, d - sv) > m) lo = m; else hi = m; }
        id = idPrev = idPrev2 = 0.5 * (lo + hi); src.initDC(0, id); vs = vsFor(id); vd = vd0 = Vdd - Rd * id;
    }
    // the drain load is linear (i = G vd - J), so the drain voltage follows from the current and the stage is one equation in the
    // current: Newton with the model's own slope, from last sample's current, inside a bracket (the residual falls as the current
    // rises). It stops after a step under 1e-14 A (quadratic convergence: the root is then exact to rounding); the general
    // solver below is the fallback.
    inline double tickLin(double vg, double G, double J) {
        if (G != gLast) { gLast = G; igdC = 1 / (1 / Rd + G); }   // the load's conductance changes only when a knob moves
        const double s0 = src.injV(0), z = src.Zi, igd = igdC, vdk = (Vdd / Rd + J) * igd;
        // start: the current extrapolated (quadratic) from the last three samples, inside the bracket
        double lo = 0, hi = IMAX, I = std::clamp(3 * (id - idPrev) + idPrev2, lo, hi);
        const double dvds = igd + z;
        JK_COUNT(0);
        for (int it = 0; it < 40; ++it) {
            JK_COUNT(1);
            double gm, gds; const double sv = s0 + z * I, vgs = vg - sv, vds = vdk - I * igd - sv, f = J201::idd(vgs, vds, gm, gds) - I;
            if (f > 0) lo = I; else hi = I;
            const double df = -gm * z - gds * dvds - 1;
            double n = I - f / df;
            if (!(n >= lo && n <= hi)) n = 0.5 * (lo + hi);
            const double step = std::abs(n - I), vov = vgs - J201::Vto;
            // in saturation the residual's curvature is known: the error left after this step is about |f''| step^2 / (2 |f'|);
            // when that is under 1e-17 A the step is final (the bypassed stages are nearly linear in the current: one step). It
            // holds only if the step cannot leave saturation: the margins to cut-off and to triode must exceed twice the step's
            // effect on them.
            const bool sat = vov > 2 * z * step && vds - vov > 2 * igd * step;
            if (step < TOL_I || (sat && 2 * J201::Beta * (z * z * (1 + J201::Lambda * vds) + 2 * z * vov * J201::Lambda * dvds) * step * step < -2e-17 * df)) { idPrev2 = idPrev; idPrev = id; id = n; vd = vdk - n * igd; vs = s0 + z * n; src.process(0, id); return vd; }
            I = n;
        }
        JK_COUNT(2); idPrev2 = idPrev; idPrev = id;
        return tick(vg, [&](double v) { return G * v - J; });
    }
    template <class L> inline double tick(double vg, L&& load) {
        {   // fast path: Newton on (I, vd) from last sample's solution
            double x[2] = {id, vd}; const double sc[2] = {1e-4, 1.0};
            auto F = [&](const double* y, double* r) { const double sv = vsFor(y[0]); r[0] = (J201::id(vg - sv, y[1] - sv) - y[0]) * 1e4; r[1] = ((Vdd - y[1]) / Rd - y[0] - load(y[1])) * 1e4; };
            if (newton<2>(F, x, sc, 12, 1e-11, false) < 1e-11 && x[0] >= 0) { id = x[0]; vd = x[1]; vs = vsFor(id); src.process(0, id); return vd; }
        }
        // robust path: drain node for a given current: (Vdd - vd)/Rd - I - load(vd) falls as vd rises
        double vdLast = vd;
        auto vdOf = [&](double I) {
            vdLast = solve1([&](double v) { return (Vdd - v) / Rd - I - load(v); }, -60.0, Vdd + 60, vdLast, 1e-10, 1e-6);
            return vdLast;
        };
        // stage law: id(vg - vs(I), vd(I) - vs(I)) - I falls as I rises
        const double imax = std::max(4 * J201::Beta * J201::Vto * J201::Vto, Vdd / Rd) * 4 + 1e-3;
        const double I = solve1([&](double i) { const double sv = vsFor(i), d = vdOf(i); return J201::id(vg - sv, d - sv) - i; }, 0.0, imax, id, 1e-14, 1e-11);
        id = std::max(I, 0.0); vd = vdOf(id); vs = vsFor(id); src.process(0, id);
        return vd;
    }
};

// J201 long-tailed pair: both sources on a tail resistor to Vn, drains on Rd from Vp; drain 2 carries a load (port G, J).
// Unknowns: tail voltage, the two drain currents. Output: drain 2 (non-inverting from gate 1, inverting from gate 2).
struct LongTailPair {
    double Vp = 15, Vn = -15, Rd = 22e3, Rt = 30e3, vt = 0, i1 = 0, i2 = 0, d2 = 0, d20 = 0, vtPrev = 0, vtPrev2 = 0;
    static constexpr double TOL_T = 1e-7;   // tickFast's last step (V): quadratic convergence leaves the tail voltage within ~1e-13 V
    double iinj = 0;   // current injected into the tail node from outside (the UH-20's Sag transconductor): the pair carries less
    void settle() {
        double x[3] = {-0.2, 2.5e-4, 2.5e-4}; const double sc[3] = {1.0, 1e-4, 1e-4};
        auto F = [&](const double* y, double* r) { const double d1 = Vp - Rd * y[1], dd = Vp - Rd * y[2];
            r[0] = (J201::id(-y[0], d1 - y[0]) - y[1]) * 1e4; r[1] = (J201::id(-y[0], dd - y[0]) - y[2]) * 1e4; r[2] = (y[1] + y[2] - (y[0] - Vn) / Rt) * 1e4; };
        newton<3>(F, x, sc, 200); vt = vtPrev = vtPrev2 = x[0]; i1 = x[1]; i2 = x[2]; d2 = d20 = Vp - Rd * i2;
    }
    // one side's current for a tail voltage t, its drain at A - B i: closed form in saturation (the current is then linear in
    // the drain voltage), otherwise Newton with the model's slope; didt: its slope against the tail voltage (implicit function)
    // curv: an upper bound on |d2i/dt2| in saturation (2 Beta (1 + lambda (A - t)) / (1 + q lambda B), doubled for the terms
    // left out), or -1 when the side is in triode
    // marg: how far t may move before the side leaves saturation (to cut-off or to triode), to first order
    static inline double side(double g, double t, double A, double B, double i0, double& didt, double& curv, double& marg) {
        const double vov = g - t - J201::Vto;
        if (vov <= 0) { didt = 0; curv = -1; marg = 0; return 0; }
        const double q = J201::Beta * vov * vov, c = 1 + J201::Lambda * (A - t), r = 1 / (1 + q * J201::Lambda * B), i = q * c * r, vds = A - B * i - t;
        double gm, gds;
        if (vds >= vov) {
            gm = 2 * J201::Beta * vov * (1 + J201::Lambda * vds); didt = -(gm + q * J201::Lambda) * r;
            curv = 4 * J201::Beta * c * r; marg = std::min(vov, (vds - vov) / std::max(-B * didt, 1e-30)); return i;
        }
        curv = -1; marg = 0;
        double lo = 0, hi = std::max((A - t) / B, 0.0), x = std::clamp(i0, lo, hi);
        JK_COUNT(6);
        for (int it = 0; it < 60; ++it) {
            JK_COUNT(7);
            const double f = J201::idd(g - t, A - B * x - t, gm, gds) - x;
            if (f > 0) lo = x; else hi = x;
            double n = x - f / (-gds * B - 1); if (!(n >= lo && n <= hi)) n = 0.5 * (lo + hi);
            const bool done = std::abs(n - x) < 1e-15; x = n; if (done || hi - lo < 1e-16) break;
        }
        J201::idd(g - t, A - B * x - t, gm, gds); didt = -(gm + gds) / (gds * B + 1);
        return x;
    }
    // the pair as one equation in the tail voltage (each side solved for it): Newton with the analytic slope from last sample's
    // tail voltage, bracketed; stops after a step under 1e-11 V (quadratic: exact to rounding); the general solver is the fallback
    inline double tickFast(double g1, double g2, double G, double J) {
        const double gd = 1 / Rd + G, A2 = (Vp / Rd + J) / gd, B2 = 1 / gd, irt = 1 / Rt;
        double lo = Vn, hi = Vp, t = std::clamp(3 * (vt - vtPrev) + vtPrev2, Vn, Vp);   // quadratic extrapolation
        JK_COUNT(3);
        for (int it = 0; it < 60; ++it) {
            JK_COUNT(4);
            double d1, d2s, c1, c2, m1, m2; const double a = side(g1, t, Vp, Rd, i1, d1, c1, m1), b = side(g2, t, A2, B2, i2, d2s, c2, m2);
            i1 = a; i2 = b;
            const double F = a + b - (t - Vn) * irt + iinj, dF = d1 + d2s - irt;
            if (F > 0) lo = t; else hi = t;
            double n = t - F / dF; if (!(n >= lo && n <= hi)) n = 0.5 * (lo + hi);
            const double step = std::abs(n - t);
            // final when the step is tiny, or (both sides in saturation, and the step too small to take either out of it, with a
            // factor 2 to spare) when the error it leaves, about |F''| step^2 / (2 |F'|), is under 1e-13 V; the currents then follow
            // the last step to first order (the error is second order)
            if (step < TOL_T || (c1 >= 0 && c2 >= 0 && 2 * step < std::min(m1, m2) && (c1 + c2) * step * step < -2e-13 * dF)) {
                vtPrev2 = vtPrev; vtPrev = vt; vt = n; i1 += d1 * (n - t); i2 += d2s * (n - t); d2 = (Vp / Rd + J - i2) / gd; return d2;
            }
            t = n;
        }
        JK_COUNT(5); vtPrev2 = vtPrev; vtPrev = vt;
        return tick(g1, g2, G, J);
    }
    inline double tick(double g1, double g2, double G, double J) {
        const double gd = 1 / Rd + G, imax = 4e-3;
        {   // fast path
            double x[3] = {vt, i1, i2}; const double sc[3] = {1.0, 1e-4, 1e-4};
            auto F = [&](const double* y, double* r) { const double d1 = Vp - Rd * y[1], dd = (Vp / Rd + J - y[2]) / gd;
                r[0] = (J201::id(g1 - y[0], d1 - y[0]) - y[1]) * 1e4; r[1] = (J201::id(g2 - y[0], dd - y[0]) - y[2]) * 1e4; r[2] = (y[1] + y[2] - (y[0] - Vn) / Rt + iinj) * 1e4; };
            if (newton<3>(F, x, sc, 12, 1e-11, false) < 1e-11 && x[1] >= 0 && x[2] >= 0) { vt = x[0]; i1 = x[1]; i2 = x[2]; d2 = (Vp / Rd + J - i2) / gd; return d2; }
        }
        // each side's current for a given tail voltage (monotone in the current), then the tail (monotone in vt)
        auto I1 = [&](double t) { i1 = solve1([&](double i) { return J201::id(g1 - t, Vp - Rd * i - t) - i; }, 0.0, imax, i1, 1e-14, 1e-11); return i1; };
        auto I2 = [&](double t) { i2 = solve1([&](double i) { const double dd = (Vp / Rd + J - i) / gd; return J201::id(g2 - t, dd - t) - i; }, 0.0, imax, i2, 1e-14, 1e-11); return i2; };
        vt = solve1([&](double t) { return I1(t) + I2(t) - (t - Vn) / Rt + iinj; }, Vn, Vp, vt, 1e-10, 1e-6);
        I1(vt); I2(vt); d2 = (Vp / Rd + J - i2) / gd;
        return d2;
    }
};

} // namespace jk

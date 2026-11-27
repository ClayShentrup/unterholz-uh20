// uh20_tests — short synthetic signals, each aimed at one property of the UH-20 engine, plus golden input/output pairs.
//   uh20_tests              run everything (exit code 1 on any failure)
//   uh20_tests --update     rewrite tests/golden/*.f32 from the current engine (only when the sound is meant to change)
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <tuple>
#include <functional>
#include <algorithm>
#include <initializer_list>
#include "../source/dsp/UH20Engine.h"

using namespace uh20;
static int failures = 0;
static void check(bool ok, const char* name, const char* fmt, double value, double limit) {
    std::printf("  %s  %-52s ", ok ? "pass" : "FAIL", name);
    std::printf(fmt, value, limit); std::printf("\n");
    if (!ok) ++failures;
}
static double db(double r) { return 10 * std::log10(r + 1e-300); }

// ---------------------------------------------------------------- signals
static const double FS = 48000;
// a guitar-like part: plucked strings (Karplus-Strong) on E2 A3 E4 B4 E5, deterministic
static std::vector<float> plucks(double seconds, double peak = 0.5) {
    const int n = (int)(seconds * FS); std::vector<double> y(n, 0.0); unsigned r = 12345;
    const double notes[5] = {82.41, 220.0, 329.63, 493.88, 659.26};
    for (int k = 0; k < 5; ++k) {
        const int start = (int)(k * 0.06 * FS), len = (int)std::lround(FS / notes[k]);
        std::vector<double> buf(len); for (auto& b : buf) { r = r * 1664525u + 1013904223u; b = ((r >> 8) / 16777216.0 - 0.5); }
        int p = 0;
        for (int i = start; i < n; ++i) { const int q = (p + 1) % len; const double v = buf[p]; buf[p] = 0.4985 * (buf[p] + buf[q]); p = q; y[i] += v; }
    }
    double m = 0; for (double v : y) m = std::max(m, std::abs(v));
    std::vector<float> out(n); for (int i = 0; i < n; ++i) out[i] = (float)(y[i] / m * peak); return out;
}
static std::vector<float> sine(double f, double dbfs, double seconds) {
    std::vector<float> s((size_t)(seconds * FS)); const double a = std::pow(10.0, dbfs / 20);
    for (size_t i = 0; i < s.size(); ++i) s[i] = (float)(a * std::sin(2 * kPi * f * i / FS)); return s;
}
struct Out { std::vector<double> L, R; };
static Out render(const Params& p, const std::vector<float>& in, double scale = 1.0) {
    Engine e; e.setParams(p); e.prepare(FS); Out o; o.L.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) o.L[i] = e.tick(in[i] * scale);
    o.R = o.L; return o;
}
static double rms(const std::vector<double>& v, size_t a = 0, size_t b = 0) { if (!b) b = v.size(); double s = 0; for (size_t i = a; i < b; ++i) s += v[i] * v[i]; return std::sqrt(s / (double)(b - a)); }
static double residualDb(const std::vector<double>& a, const std::vector<double>& b, double bScale = 1) {
    double e = 0, s = 0; for (size_t i = 0; i < a.size(); ++i) { const double d = a[i] - b[i] * bScale; e += d * d; s += a[i] * a[i]; } return db(e / s);
}
// share of the energy (20 Hz - 16 kHz) that is not at a harmonic of f: aliasing, subharmonics, noise
static double inharmonicDb(const std::vector<double>& y, size_t from, double f) {
    const int N = 32768; std::vector<std::complex<double>> X(N);
    for (int i = 0; i < N; ++i) { const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (N - 1)) + 0.08 * std::cos(4 * kPi * i / (N - 1)); X[i] = y[from + i] * w; }
    for (int i = 1, j = 0; i < N; ++i) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(X[i], X[j]); }
    for (int len = 2; len <= N; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -2 * kPi / len);
        for (int i = 0; i < N; i += len) { std::complex<double> w = 1; for (int k = 0; k < len / 2; ++k) { const auto u = X[i + k], v = X[i + k + len / 2] * w; X[i + k] = u + v; X[i + k + len / 2] = u - v; w *= wl; } }
    }
    double all = 0, off = 0;
    for (int k = 1; k < N / 2; ++k) {
        const double fr = k * FS / N; if (fr < 20 || fr > 16000) continue;
        const double p = std::norm(X[k]); all += p;
        const double h = std::round(fr / f); if (std::abs(fr - h * f) > 6 * FS / N) off += p;
    }
    return db(off / all);
}
static Params P(std::initializer_list<std::pair<const char*, double>> kv) {
    Params p;
    for (auto& [k, v] : kv) {
        const std::string s = k;
        if (s == "volume") p.volume = v; else if (s == "treble") p.treble = v; else if (s == "middle") p.middle = v; else if (s == "bass") p.bass = v;
        else if (s == "presence") p.presence = v; else if (s == "jubilee") p.jubilee = v; else if (s == "sag") p.sag = v; else if (s == "master") p.master = v;
        else if (s == "bright") p.bright = v > 0.5; else if (s == "shift") p.shift = v > 0.5; else if (s == "output") p.output = (int)v;
        else if (s == "channel") p.channel = (int)v; else if (s == "drive") p.drive = v;
    }
    return p;
}
static Params all10(int ch) { return P({{"channel", (double)ch}, {"volume", 10}, {"drive", 10}, {"treble", 10}, {"middle", 10}, {"bass", 10}, {"presence", 10}, {"jubilee", 10}, {"sag", 10}, {"master", 10}, {"bright", 1}, {"shift", 1}}); }
static Params all0(int ch) { return P({{"channel", (double)ch}, {"volume", 0}, {"drive", 0}, {"treble", 0}, {"middle", 0}, {"bass", 0}, {"presence", 0}, {"jubilee", 0}, {"sag", 0}, {"master", 0}}); }

// ---------------------------------------------------------------- golden input/output pairs
struct Golden { const char* name; Params p; double seconds; };
static std::string goldenDir() {
    std::string f = __FILE__; const size_t s = f.find_last_of("/\\"); return (s == std::string::npos ? std::string(".") : f.substr(0, s)) + "/golden/";
}

int main(int argc, char** argv) {
    const bool update = argc > 1 && !std::strcmp(argv[1], "--update");
    const std::vector<Golden> golden = {
        {"rhythm_clean", Params{}, 0.3},
        {"rhythm_jubilee", P({{"jubilee", 6}, {"sag", 5}}), 0.3},
        {"rhythm_pushed", P({{"volume", 9}, {"master", 9}, {"sag", 8}}), 0.3},
        {"rhythm_bright_shift_amp", P({{"bright", 1}, {"shift", 1}, {"output", 1}}), 0.3},
        {"lead_low", P({{"channel", 1}, {"drive", 3}}), 0.3},
        {"lead_high", P({{"channel", 1}, {"drive", 9}, {"jubilee", 4}, {"sag", 5}}), 0.3},
    };
    if (update) {
        for (auto& g : golden) {
            const Out o = render(g.p, plucks(g.seconds)); std::vector<float> f; for (double v : o.L) f.push_back((float)v);
            FILE* h = std::fopen((goldenDir() + g.name + ".f32").c_str(), "wb"); if (!h) { std::printf("cannot write %s\n", g.name); return 1; }
            std::fwrite(f.data(), 4, f.size(), h); std::fclose(h); std::printf("wrote golden/%s.f32\n", g.name);
        }
        return 0;
    }

    std::printf("golden input/output pairs (plucked strings; any change to the sound shows up here)\n");
    for (auto& g : golden) {
        FILE* h = std::fopen((goldenDir() + g.name + ".f32").c_str(), "rb");
        if (!h) { check(false, g.name, "missing golden file%.0f%.0f", 0, 0); continue; }
        std::vector<float> f; float t; while (std::fread(&t, 4, 1, h) == 1) f.push_back(t); std::fclose(h);
        const Out o = render(g.p, plucks(g.seconds)); double e = 0, s = 0;
        for (size_t i = 0; i < o.L.size() && i < f.size(); ++i) { const double d = o.L[i] - f[i]; e += d * d; s += (double)f[i] * f[i]; }
        if (f.size() != o.L.size()) e = s;
        check(db(e / s) < -100, (std::string("matches golden/") + g.name).c_str(), "%.1f dB (limit %.0f)", db(e / s), -100);
    }

    std::printf("every solver converges: a 1 ppm change at the input moves the output by about -120 dB\n");
    {
        const auto x = plucks(1.0);
        for (auto& [name, p] : std::vector<std::pair<const char*, Params>>{{"rhythm", Params{}}, {"rhythm, jubilee 10", P({{"jubilee", 10}})},
                 {"rhythm all on 10", all10(0)}, {"lead, drive 5", P({{"channel", 1}})}, {"lead, drive 10, jubilee 8, sag 8", P({{"channel", 1}, {"drive", 10}, {"jubilee", 8}, {"sag", 8}})}, {"lead all on 10", all10(1)}}) {
            const Out a = render(p, x), b = render(p, x, 1 + 1e-6);
            check(residualDb(a.L, b.L) < -95, name, "%.1f dB (limit %.0f)", residualDb(a.L, b.L), -95);
        }
    }

    std::printf("hard high notes stay clean: steady sine, share of energy off its harmonics (subharmonics, aliasing)\n");
    for (auto& [name, p, f, lim] : std::vector<std::tuple<const char*, Params, double, double>>{
             {"rhythm, jubilee 10, 2637 Hz", P({{"jubilee", 10}}), 2637.0, -55.0},
             {"rhythm, jubilee 10, 4186 Hz", P({{"jubilee", 10}}), 4186.0, -55.0},
             {"rhythm, 4186 Hz (aliasing budget)", Params{}, 4186.0, -55.0},
             {"lead, drive 7, 4186 Hz (aliasing budget)", P({{"channel", 1}, {"drive", 7}}), 4186.0, -38.0},
             {"lead, drive 10, jubilee 8, 2637 Hz", P({{"channel", 1}, {"drive", 10}, {"jubilee", 8}}), 2637.0, -36.0},
             {"lead, drive 10, jubilee 8, 4186 Hz", P({{"channel", 1}, {"drive", 10}, {"jubilee", 8}}), 4186.0, -30.0}}) {
        const Out o = render(p, sine(f, -6, 2.0)); const double v = inharmonicDb(o.L, (size_t)(1.2 * FS), f);
        check(v < lim, name, "%.1f dB (limit %.0f)", v, lim);
    }

    std::printf("exact solvers\n");
    {
        const double VT = 0.02585, ISLED = 0.01 / std::exp(1.8 / (2.0 * VT)); double worst = 0;
        for (double rt : {59700.0 + 220, 59700.0 + 100e3, 59700.0 + 470e3}) for (double vo = -60; vo <= 60; vo += 0.0013) {
            const double nvt = (vo >= 0 ? 2.0 : 4.0) * VT, av = std::abs(vo), k = rt * ISLED;
            const double x = std::min(av + k - nvt * lambertW(std::log(k / nvt) + (av + k) / nvt), av);
            worst = std::max(worst, std::abs(av - x - k * std::expm1(x / nvt)));
        }
        check(worst < 1e-6, "Jubilee LEDs: KVL error over -60..60 V", "%.2g V (limit %.0g)", worst, 1e-6);
    }

    std::printf("behaviour\n");
    for (int ch = 0; ch < 2; ++ch) for (int which = 0; which < 4; ++which) {
        Engine e; Params p = P({{"channel", (double)ch}, {"jubilee", 4}}); e.setParams(p); e.prepare(FS);
        double prev = 0, normal = 0, atMove = 0;
        for (int i = 0; i < 96000; ++i) {
            if (i == 48123) { if (which == 0) p.treble += 0.2; else if (which == 1) p.jubilee += 0.3; else if (which == 2) p.master += 0.1; else p.presence += 0.2; e.setParams(p); }
            const double y = e.tick(0.3 * std::sin(2 * kPi * 110 * i / FS)), st = std::abs(y - prev); prev = y;
            if (i > 24000 && i < 48000) normal = std::max(normal, st); if (i >= 48123 && i < 48200) atMove = std::max(atMove, st);
        }
        const char* kn[4] = {"treble", "jubilee", "master", "presence"};
        check(atMove / normal < 2.5, (std::string(ch ? "lead: a " : "rhythm: a ") + kn[which] + " nudge mid-note does not click").c_str(), "%.2fx a normal step (limit %.1f)", atMove / normal, 2.5);
    }
    {
        std::vector<float> burst = plucks(1.0, 4.0); burst.resize((size_t)(4 * FS), 0.f);
        for (int ch = 0; ch < 2; ++ch) for (int hi = 0; hi < 2; ++hi) {
            const Params pp = hi ? all10(ch) : all0(ch); const Out o = render(pp, burst); double peak = 0; bool finite = true;
            for (double v : o.L) { finite = finite && std::isfinite(v); peak = std::max(peak, std::abs(v)); }
            const std::string nm = std::string(ch ? "lead" : "rhythm") + (hi ? " all on 10" : " all on 0");
            check(finite && peak < 20, (nm + ": 8x-hot input stays finite and bounded").c_str(), "peak %.2f (limit %.0f)", finite ? peak : 1e9, 20);
            const double tail = rms(o.L, (size_t)(3.5 * FS), o.L.size());
            check(tail < 1e-5, (nm + ": silent 2.5 s after the playing stops").c_str(), "rms %.2g (limit %.0g)", tail, 1e-5);
        }
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

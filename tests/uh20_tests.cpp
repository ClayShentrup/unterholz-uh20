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
#define private public   // the tests reach into the circuit
#include "../source/dsp/UH20Engine.h"
#undef private

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
// spectrum helpers: share of the energy (20 Hz - 16 kHz) that is not at a harmonic of f (aliasing, subharmonics, noise),
// or (harm) the energy in the harmonics above f relative to f itself (THD)
static double inharmonicDb(const std::vector<double>& y, size_t from, double f, bool harm = false) {
    const int N = 32768; std::vector<std::complex<double>> X(N);
    for (int i = 0; i < N; ++i) { const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (N - 1)) + 0.08 * std::cos(4 * kPi * i / (N - 1)); X[i] = y[from + i] * w; }
    for (int i = 1, j = 0; i < N; ++i) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(X[i], X[j]); }
    for (int len = 2; len <= N; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -2 * kPi / len);
        for (int i = 0; i < N; i += len) { std::complex<double> w = 1; for (int k = 0; k < len / 2; ++k) { const auto u = X[i + k], v = X[i + k + len / 2] * w; X[i + k] = u + v; X[i + k + len / 2] = u - v; w *= wl; } }
    }
    double all = 0, off = 0, fund = 0, over = 0;
    for (int k = 1; k < N / 2; ++k) {
        const double fr = k * FS / N; if (fr < 20 || fr > 16000) continue;
        const double p = std::norm(X[k]); all += p;
        const double h = std::round(fr / f); if (std::abs(fr - h * f) > 6 * FS / N) off += p; else if (h == 1) fund += p; else over += p;
    }
    return harm ? db(over / fund) : db(off / all);
}
static Params P(std::initializer_list<std::pair<const char*, double>> kv) {
    Params p;
    for (auto& [k, v] : kv) {
        const std::string s = k;
        if (s == "volume") p.volume = v; else if (s == "treble") p.treble = v; else if (s == "middle") p.middle = v; else if (s == "bass") p.bass = v;
        else if (s == "presence") p.presence = v; else if (s == "clip" || s == "jubilee") p.clip = v; else if (s == "sag") p.sag = v; else if (s == "master") p.master = v;
        else if (s == "bright") p.bright = v > 0.5; else if (s == "shift") p.shift = v > 0.5; else if (s == "output") p.output = (int)v;
        else if (s == "drive") p.drive = v;
    }
    return p;
}
static Params all10(double drive) { return P({{"drive", drive}, {"volume", 10}, {"treble", 10}, {"middle", 10}, {"bass", 10}, {"presence", 10}, {"clip", 10}, {"sag", 10}, {"master", 10}, {"bright", 1}, {"shift", 1}}); }
static Params all0(double drive) { return P({{"drive", drive}, {"volume", 0}, {"treble", 0}, {"middle", 0}, {"bass", 0}, {"presence", 0}, {"clip", 0}, {"sag", 0}, {"master", 0}}); }

// ---------------------------------------------------------------- golden input/output pairs
// frozen = recorded by v0.2.0 (before Drive existed) or, the *_sag0 pair, by v0.3.0, and never rewritten: at Drive 0 and Sag 0 the
// engine must still match them exactly. rhythm_clip and rhythm_pushed (Sag 5 and 8) were frozen until v0.3.1 redesigned Sag.
struct Golden { const char* name; Params p; double seconds; bool frozen; };
static std::string goldenDir() {
    std::string f = __FILE__; const size_t s = f.find_last_of("/\\"); return (s == std::string::npos ? std::string(".") : f.substr(0, s)) + "/golden/";
}

int main(int argc, char** argv) {
    const bool update = argc > 1 && !std::strcmp(argv[1], "--update");
    const std::vector<Golden> golden = {
        {"rhythm_clean", Params{}, 0.3, true},
        {"rhythm_clip", P({{"clip", 6}, {"sag", 5}}), 0.3, false},
        {"rhythm_pushed", P({{"volume", 9}, {"master", 9}, {"sag", 8}}), 0.3, false},
        {"rhythm_clip_sag0", P({{"clip", 6}}), 0.3, true},
        {"rhythm_pushed_sag0", P({{"volume", 9}, {"master", 9}}), 0.3, true},
        {"rhythm_bright_shift_amp", P({{"bright", 1}, {"shift", 1}, {"output", 1}}), 0.3, true},
        {"drive_edge", P({{"drive", 2}}), 0.3, false},
        {"drive_lead", P({{"drive", 8}, {"clip", 5}, {"sag", 5}}), 0.3, false},
    };
    if (update) {
        for (auto& g : golden) {
            if (g.frozen) continue;
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
        for (auto& [name, p] : std::vector<std::pair<const char*, Params>>{{"rhythm", Params{}}, {"rhythm, clip 10", P({{"clip", 10}})},
                 {"all on 10, drive 0", all10(0)}, {"drive 5", P({{"drive", 5}})}, {"drive 10, clip 8, sag 8", P({{"drive", 10}, {"clip", 8}, {"sag", 8}})}, {"all on 10", all10(10)}}) {
            const Out a = render(p, x), b = render(p, x, 1 + 1e-6);
            check(residualDb(a.L, b.L) < -95, name, "%.1f dB (limit %.0f)", residualDb(a.L, b.L), -95);
        }
    }

    std::printf("hard high notes stay clean: steady sine, share of energy off its harmonics (subharmonics, aliasing)\n");
    for (auto& [name, p, f, lim] : std::vector<std::tuple<const char*, Params, double, double>>{
             {"rhythm, clip 10, 2637 Hz", P({{"clip", 10}}), 2637.0, -55.0},
             {"rhythm, clip 10, 4186 Hz", P({{"clip", 10}}), 4186.0, -50.0},
             {"rhythm, 4186 Hz (aliasing budget)", Params{}, 4186.0, -55.0},
             {"drive 7, 4186 Hz (aliasing budget)", P({{"drive", 7}}), 4186.0, -35.0},
             {"drive 10, clip 8, 2637 Hz", P({{"drive", 10}, {"clip", 8}}), 2637.0, -36.0},
             {"drive 10, clip 8, 4186 Hz", P({{"drive", 10}, {"clip", 8}}), 4186.0, -30.0}}) {
        const Out o = render(p, sine(f, -6, 2.0)); const double v = inharmonicDb(o.L, (size_t)(1.2 * FS), f);
        check(v < lim, name, "%.1f dB (limit %.0f)", v, lim);
    }

    std::printf("exact solvers\n");
    {
        const double VT = 0.02585, ISLED = 0.01 / std::exp(1.8 / (2.0 * VT)); double worst = 0;
        for (double rt : {59700.0 + 220, 59700.0 + 100e3, 59700.0 + 470e3}) for (double vo = -60; vo <= 60; vo += 0.0013) {
            const double nvt = (vo >= 0 ? 2.0 : 4.0) * VT, av = std::abs(vo), k = rt * ISLED;
            const double x = std::min(av + k - nvt * jk::lambertW(std::log(k / nvt) + (av + k) / nvt), av);
            worst = std::max(worst, std::abs(av - x - k * std::expm1(x / nvt)));
        }
        check(worst < 1e-6, "Clip LEDs: KVL error over -60..60 V", "%.2g V (limit %.0g)", worst, 1e-6);
    }

    std::printf("power section\n");
    for (double pres : {0.0, 5.0, 10.0}) for (double mas : {3.0, 10.0}) {   // the feedback loop settles after an impulse at every setting
        Engine e; e.setParams(P({{"presence", pres}, {"master", mas}, {"output", 1}})); e.prepare(FS);
        const int n = (int)(0.6 * e.FSI); double s1 = 0, s2 = 0; int m = 0;
        for (int i = 0; i < n; ++i) { e.amp(i == 800 ? 0.2 : 0.0); if (i > n / 2) { s1 += e.vout; s2 += e.vout * e.vout; ++m; } }
        const double v = std::sqrt(std::max(s2 / m - s1 * s1 / m / m, 0.0)) + std::abs(s1 / m);
        char nm[80]; std::snprintf(nm, sizeof nm, "loop settles: presence %.0f, master %.0f", pres, mas);
        check(v < 1e-3, nm, "%.2g V (limit %.0g)", v, 1e-3);
    }
    {   // Sag compresses: a hard note at Master 10 settles lower at Sag 10 than at Sag 0, a soft one barely changes, and after the hard
        // playing stops the amp swells back
        auto run = [&](double sag, double amp) {
            Engine e; e.setParams(P({{"volume", 8}, {"master", 10}, {"sag", sag}, {"output", 1}})); e.prepare(FS);
            std::vector<double> y; for (int i = 0; i < (int)(2.5 * FS); ++i) y.push_back(e.tick((i < 1.5 * FS ? amp : 0.003) * std::sin(2 * kPi * 110 * i / FS)));
            return y;
        };
        const auto h0 = run(0, 0.3), h10 = run(10, 0.3), s0 = run(0, 0.003), s10 = run(10, 0.003);
        const size_t a = (size_t)(1.0 * FS), b = (size_t)(1.5 * FS), c = (size_t)(2.2 * FS), d = (size_t)(2.5 * FS);
        const double hard = 20 * std::log10(rms(h10, a, b) / rms(h0, a, b)), soft = 20 * std::log10(rms(s10, a, b) / rms(s0, a, b));
        const double back = 20 * std::log10(rms(h10, c, d) / rms(h0, c, d));
        check(hard < -2.0, "Sag 10 squashes a hard note at Master 10", "%.2f dB vs Sag 0 (limit %.0f)", hard, -2.0);
        check(std::abs(soft) < 0.3, "Sag 10 leaves soft playing alone", "%.2f dB vs Sag 0 (limit %.1f)", std::abs(soft), 0.3);
        check(std::abs(back) < 0.5, "Sag 10 swells back 0.7 s after the hard playing stops", "%.2f dB vs Sag 0 (limit %.1f)", std::abs(back), 0.5);
    }
    std::printf("behaviour\n");
    for (int ch = 0; ch < 2; ++ch) for (int which = 0; which < 6; ++which) {
        if (which == 4 && ch == 0) continue;
        Engine e; Params p = P({{"drive", ch ? 6.0 : 0.0}, {"clip", 4}}); e.setParams(p); e.prepare(FS);
        double prev = 0, normal = 0, atMove = 0;
        for (int i = 0; i < 96000; ++i) {
            if (i == 48123) { if (which == 0) p.treble += 0.2; else if (which == 1) p.clip += 0.3; else if (which == 2) p.master += 0.1; else if (which == 3) p.presence += 0.2; else if (which == 4) p.drive += 0.2; else p.sag += 2; e.setParams(p); }
            const double y = e.tick(0.3 * std::sin(2 * kPi * 110 * i / FS)), st = std::abs(y - prev); prev = y;
            if (i > 24000 && i < 48000) normal = std::max(normal, st); if (i >= 48123 && i < 48200) atMove = std::max(atMove, st);
        }
        const char* kn[6] = {"treble", "clip", "master", "presence", "drive", "sag"};
        check(atMove / normal < 2.5, (std::string(ch ? "drive 6: a " : "drive 0: a ") + kn[which] + " nudge mid-note does not click").c_str(), "%.2fx a normal step (limit %.1f)", atMove / normal, 2.5);
    }
    {   // the Drive switch, moved while a note sounds (0 -> 3 and 3 -> 0): no click, no thump
        for (int dir = 0; dir < 2; ++dir) {
            Engine e; Params p = P({{"drive", dir ? 3.0 : 0.0}}); e.setParams(p); e.prepare(FS);
            double prev = 0, normal = 0, atMove = 0;
            for (int i = 0; i < 96000; ++i) {
                if (i == 48123) { p.drive = dir ? 0.0 : 3.0; e.setParams(p); }
                const double y = e.tick(0.1 * std::sin(2 * kPi * 220 * i / FS)), st = std::abs(y - prev); prev = y;
                if ((i > 24000 && i < 48000) || i > 72000) normal = std::max(normal, st); if (i >= 48123 && i < 72000) atMove = std::max(atMove, st);
            }
            check(atMove / normal < 1.5, dir ? "switching Drive off mid-note does not click" : "switching Drive on mid-note does not click",
                  "%.2fx the larger steady step before or after (limit %.1f)", atMove / normal, 1.5);
        }
    }
    {   // no blocking: a 0 dBFS strum then a quiet sustained note; in v0.3.0 the Drive stage's gate charged its coupling capacitor and
        // cut the stage off for tens of milliseconds. Every 10 ms frame after the strum must stay within 6 dB of the settled level.
        std::vector<float> x = plucks(0.25, 1.0); x.resize((size_t)(2.0 * FS), 0.f);
        for (size_t i = (size_t)(0.25 * FS); i < x.size(); ++i) x[i] = (float)(0.03 * std::sin(2 * kPi * 196 * i / FS));
        for (double d : {6.0, 8.0, 10.0}) for (int out = 0; out < 2; ++out) {
            const Out o = render(P({{"drive", d}, {"output", out}}), x); const double settled = rms(o.L, (size_t)(1.5 * FS), o.L.size());
            double worst = 0; for (size_t i = (size_t)(0.26 * FS); i + 480 <= o.L.size(); i += 480) worst = std::min(worst, 20 * std::log10(rms(o.L, i, i + 480) / settled));
            char nm[96]; std::snprintf(nm, sizeof nm, "drive %.0f, %s: no dropout after a 0 dBFS strum", d, out ? "AMP" : "REC");
            check(worst > -6, nm, "deepest frame %.1f dB under the settled level (limit %.0f)", worst, -6);
        }
    }
    {   // Drive gives a smooth rise in saturation: harmonic share of a 110 Hz note grows steadily with the knob
        double prev = -1e9; bool mono = true; std::string seq;
        for (double d : {0.0, 1.0, 3.0, 5.0, 7.0, 10.0}) {
            const Out o = render(P({{"drive", d}, {"output", 1}}), sine(110, -30, 2.0)); const double h = inharmonicDb(o.L, (size_t)(1.2 * FS), 110, true);
            mono = mono && h > prev - 0.5; prev = h; char b[32]; std::snprintf(b, sizeof b, "%s%.0f", seq.empty() ? "" : " ", h); seq += b;
        }
        check(mono, ("Drive 0 .. 10 adds harmonics steadily (" + seq + " dB)").c_str(), "%.0f%.0f", 0, 0);
    }
    {
        std::vector<float> burst = plucks(1.0, 4.0); burst.resize((size_t)(4 * FS), 0.f);
        for (int ch = 0; ch < 2; ++ch) for (int hi = 0; hi < 2; ++hi) {
            const Params pp = hi ? all10(ch ? 10 : 0) : all0(ch ? 0.5 : 0); const Out o = render(pp, burst); double peak = 0; bool finite = true;
            for (double v : o.L) { finite = finite && std::isfinite(v); peak = std::max(peak, std::abs(v)); }
            const std::string nm = std::string(hi ? "all on 10" : "all on 0") + (ch ? (hi ? ", drive 10" : ", drive 0.5") : ", drive 0");
            check(finite && peak < 20, (nm + ": 8x-hot input stays finite and bounded").c_str(), "peak %.2f (limit %.0f)", finite ? peak : 1e9, 20);
            const double tail = rms(o.L, (size_t)(3.5 * FS), o.L.size());
            check(tail < 1e-5, (nm + ": silent 2.5 s after the playing stops").c_str(), "rms %.2g (limit %.0g)", tail, 1e-5);
        }
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

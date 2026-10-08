// fx_test: objective tests of the effects (delays, reverb, flanger).  Exit 0 = all pass.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ampsurd/Effects.h"
#include "ampsurd/Resampler.h"

using namespace ampsurd;
static int failures = 0;
static const double kPi = 3.14159265358979323846;
static const double sr = 48000.0;

static void check(bool ok, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0)
{
    std::printf("[%s] ", ok ? "PASS" : "FAIL");
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    failures += ok ? 0 : 1;
}

struct Stereo { std::vector<double> L, R; };

// runs the chain over a stereo input in random-size blocks; `change(settings, sampleIndex)` may edit settings
template <typename F>
static Stereo run(FxChain& fx, Stereo x, FxSettings s, F change, unsigned seed = 1)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> blk(32, 512);
    for (size_t p = 0; p < x.L.size();)
    {
        const int n = (int) std::min<size_t>((size_t) blk(rng), x.L.size() - p);
        change(s, p);
        fx.process(x.L.data() + p, x.R.data() + p, n, s);
        p += (size_t) n;
    }
    return x;
}
static Stereo run(FxChain& fx, Stereo x, FxSettings s) { return run(fx, std::move(x), s, [](FxSettings&, size_t) {}); }

// impulse at sample kT0 (an effect switched on fades its input in over 10 ms)
static const size_t kT0 = 1000;
static Stereo impulse(size_t n) { Stereo x { std::vector<double>(n + kT0, 0.0), std::vector<double>(n + kT0, 0.0) }; x.L[kT0] = x.R[kT0] = 1.0; return x; }
static Stereo sine(size_t n, double f = 220.0, double a = 0.3)
{
    Stereo x { std::vector<double>(n), std::vector<double>(n) };
    for (size_t i = 0; i < n; ++i) x.L[i] = x.R[i] = a * std::sin(2 * kPi * f * (double) i / sr);
    return x;
}
static Stereo noise(size_t n, unsigned seed, double a = 0.2)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, a);
    Stereo x { std::vector<double>(n), std::vector<double>(n) };
    for (size_t i = 0; i < n; ++i) { x.L[i] = nd(rng); x.R[i] = nd(rng); }
    return x;
}
static double curvature(const std::vector<double>& v, size_t from = 2)
{
    double m = 0;
    for (size_t i = std::max<size_t>(2, from); i < v.size(); ++i) m = std::max(m, std::abs(v[i] - 2 * v[i - 1] + v[i - 2]));
    return m;
}
// Click detector: a click is broadband, a 220 Hz tone (also through the effects) is not.
// Peak of the signal above 4 kHz (4th-order high-pass), from `from` on.
static double hfPeak(const std::vector<double>& v, size_t from)
{
    const double w = std::tan(kPi * 4000.0 / sr);
    auto hp = [&](std::vector<double> x) {
        const double k = std::sqrt(2.0), n = 1.0 / (1.0 + k * w + w * w);
        const double b0 = n, b1 = -2 * n, b2 = n, a1 = 2 * (w * w - 1) * n, a2 = (1 - k * w + w * w) * n;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& s : x) { const double y = b0 * s + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = s; y2 = y1; y1 = y; s = y; }
        return x;
    };
    const auto y = hp(hp(v));
    double m = 0;
    for (size_t i = from; i < y.size(); ++i) m = std::max(m, std::abs(y[i]));
    return m;
}
// passes if the changed run has no more high-frequency content than the steady runs (+50 %), or stays below -70 dBFS
static bool clickFree(const std::vector<double>& changed, std::initializer_list<const std::vector<double>*> steady, double& got, double& ref)
{
    got = hfPeak(changed, 48000);
    ref = 0;
    for (auto* s : steady) ref = std::max(ref, hfPeak(*s, 48000));
    return got <= std::max(1.5 * ref, 3.16e-4);
}

static size_t argmaxAbs(const std::vector<double>& v, size_t from, size_t to)
{
    size_t best = from;
    for (size_t i = from; i < std::min(to, v.size()); ++i) if (std::abs(v[i]) > std::abs(v[best])) best = i;
    return best;
}

// RT60 from an impulse response: 1 kHz band, Schroeder integration, -5..-35 dB fit
static double rt60(const std::vector<double>& h)
{
    // biquad band-pass 1 kHz, Q 1.4
    const double w = 2 * kPi * 1000.0 / sr, al = std::sin(w) / (2 * 1.4);
    const double b0 = al, b2 = -al, a0 = 1 + al, a1 = -2 * std::cos(w), a2 = 1 - al;
    std::vector<double> y(h.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < h.size(); ++i)
    {
        const double v = (b0 * h[i] + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = h[i]; y2 = y1; y1 = v; y[i] = v;
    }
    std::vector<double> e(h.size());
    double acc = 0;
    for (size_t i = h.size(); i-- > 0;) { acc += y[i] * y[i]; e[i] = acc; }
    const double e0 = e[0];
    size_t t5 = 0, t35 = 0;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double db = 10 * std::log10(e[i] / e0 + 1e-300);
        if (!t5 && db <= -5) t5 = i;
        if (!t35 && db <= -35) { t35 = i; break; }
    }
    return t35 > t5 ? 2.0 * (double) (t35 - t5) / sr : 0.0;
}

int main()
{
    FxSettings off;

    // 1. everything off: bit-identical
    {
        FxChain fx; fx.prepare(sr, 512);
        const auto x = noise(48000, 1);
        const auto y = run(fx, x, off);
        check(y.L == x.L && y.R == x.R && !fx.anyActive(), "ALL OFF: output bit-identical to the input, nothing running");
    }

    // 2. delays: exact times, parallel
    {
        FxChain fx; fx.prepare(sr, 512);
        FxSettings s;
        s.delay[0] = { true, 500.0, 0.0f, 1.0f, 20000.0f, false };
        s.delay[1] = { true, 756.0, 0.0f, 1.0f, 20000.0f, false };
        const auto y = run(fx, impulse(48000), s);
        const size_t p1 = argmaxAbs(y.L, kT0 + 100, kT0 + 30000) - kT0, p2 = argmaxAbs(y.L, kT0 + 30000, kT0 + 48000) - kT0;
        check(p1 == 24000 && p2 == 36288, "DELAY: 500 ms and 756 ms in parallel -> echoes exactly at sample %.0f and %.0f (expected 24000, 36288)", (double) p1, (double) p2);

        FxChain fx2; fx2.prepare(sr, 512);
        FxSettings f;
        f.delay[0] = { true, 756.3, 0.0f, 1.0f, 20000.0f, false };
        const auto z = run(fx2, impulse(48000), f);
        const size_t q = argmaxAbs(z.L, kT0 + 100, kT0 + 48000) - kT0;
        check(q == 36302 || q == 36303, "DELAY: fractional time 756.3 ms -> echo between samples 36302 and 36303 (peak at %.0f)", (double) q);
    }
    {
        FxChain fx; fx.prepare(sr, 512);
        FxSettings s;
        s.delay[0] = { true, 250.0, 0.5f, 1.0f, 20000.0f, false };
        const auto y = run(fx, impulse(48000), s);
        const double e1 = std::abs(y.L[kT0 + 12000]), e2 = std::abs(y.L[kT0 + 24000]), e3 = std::abs(y.L[kT0 + 36000]);
        check(std::abs(e2 / e1 - 0.5) < 0.03 && std::abs(e3 / e2 - 0.5) < 0.03,
              "DELAY: FEEDBACK 50 %% -> each repeat at half the level of the one before (%.3f, %.3f)", e2 / e1, e3 / e2);
        s.delay[0].pingPong = true;
        FxChain pp; pp.prepare(sr, 512);
        const auto z = run(pp, impulse(48000), s);
        check(std::abs(z.L[kT0 + 12000]) > 0.9 && std::abs(z.R[kT0 + 12000]) < 1e-9 && std::abs(z.R[kT0 + 24000]) > 0.45 && std::abs(z.L[kT0 + 24000]) < 1e-9,
              "DELAY: PING-PONG -> first repeat left only, second right only");
    }
    {
        // time change while playing, spill-over when switched off, then idle and transparent again
        FxChain fx; fx.prepare(sr, 512);
        FxSettings s;
        s.delay[0] = { true, 500.0, 0.4f, 0.5f, 6000.0f, false };
        const auto steady = run(fx, sine(48000 * 4), s);
        FxChain fx2; fx2.prepare(sr, 512);
        const auto changed = run(fx2, sine(48000 * 4), s, [](FxSettings& st, size_t p) { if (p > 48000 * 2) st.delay[0].timeMs = 756.0; });
        double got, ref;
        FxSettings s756 = s; s756.delay[0].timeMs = 756.0;
        FxChain fx4; fx4.prepare(sr, 512);
        const auto steady756 = run(fx4, sine(48000 * 4), s756);
        { const bool ok = clickFree(changed.L, { &steady.L, &steady756.L }, got, ref);
        check(ok,
              "DELAY: changing the time while playing is click-free (content above 4 kHz %.1f dBFS vs %.1f dBFS steady)", 20 * std::log10(got + 1e-12), 20 * std::log10(ref + 1e-12)); }

        FxChain fx3; fx3.prepare(sr, 512);
        auto x = noise(48000 * 20, 3);
        for (size_t i = 48000; i < x.L.size(); ++i) x.L[i] = x.R[i] = 0.0; // 1 s of playing, then silence
        const auto y = run(fx3, x, s, [](FxSettings& st, size_t p) { st.delay[0].on = p < 48000; });
        double tail = 0;
        for (size_t i = 48000 + 24000; i < 48000 + 30000; ++i) tail = std::max(tail, std::abs(y.L[i]));
        auto after = noise(4800, 9);
        const auto z = run(fx3, after, off);
        check(tail > 0.01 && !fx3.anyActive() && z.L == after.L,
              "DELAY OFF: the repeats ring out (%.3f) instead of stopping, then it stops running and is bit-transparent", tail);
    }

    // 3. reverb: RT60 for every type, stereo, stability, type change while playing
    {
        double worst = 0;
        for (int t = 0; t < kNumReverbTypes; ++t)
            for (float rt : { 1.0f, 3.0f })
            {
                FxChain fx; fx.prepare(sr, 512);
                FxSettings s;
                s.reverb = { true, (ReverbType) t, rt, 0.0f, 20000.0f, 1.0f };
                auto imp = impulse((size_t) (sr * (rt * 2.0 + 1.0)));
                auto y = run(fx, imp, s);
                y.L[kT0] -= 1.0; // remove the dry impulse
                y.L.erase(y.L.begin(), y.L.begin() + kT0);
                const double m = rt60(y.L);
                const double err = std::abs(m / rt - 1.0);
                worst = std::max(worst, err);
                std::printf("[INFO] %-9s DECAY %.1f s -> measured RT60 %.2f s\n", reverbTypeName((ReverbType) t), rt, m);
            }
        check(worst < 0.15, "REVERB: measured decay time matches DECAY for all five types (worst error %.0f %%)", worst * 100);

        FxChain fx; fx.prepare(sr, 512);
        FxSettings s;
        s.reverb = { true, ReverbType::hall, 2.0f, 0.0f, 9000.0f, 1.0f };
        auto y = run(fx, impulse(48000 * 3), s);
        y.L[kT0] -= 1.0; y.R[kT0] -= 1.0;
        double ll = 0, rr = 0, lr = 0;
        for (size_t i = 0; i < y.L.size(); ++i) { ll += y.L[i] * y.L[i]; rr += y.R[i] * y.R[i]; lr += y.L[i] * y.R[i]; }
        const double corr = lr / std::sqrt(ll * rr);
        check(std::abs(corr) < 0.3, "REVERB: wide stereo (left/right correlation of the tail %.2f)", corr);

        FxChain big; big.prepare(sr, 512);
        FxSettings c;
        c.reverb = { true, ReverbType::cathedral, 12.0f, 250.0f, 20000.0f, 1.0f };
        const auto n = run(big, noise(48000 * 20, 5), c);
        double peak = 0; bool finite = true;
        for (double v : n.L) { finite = finite && std::isfinite(v); peak = std::max(peak, std::abs(v)); }
        check(finite && peak < 10.0, "REVERB: cathedral, 12 s decay, 20 s of noise: stable (peak %.2f)", peak);

        FxChain a; a.prepare(sr, 512);
        FxSettings r;
        r.reverb = { true, ReverbType::room, 1.5f, 10.0f, 7000.0f, 0.5f };
        const auto steady = run(a, sine(48000 * 6), r);
        FxChain b; b.prepare(sr, 512);
        const auto changed = run(b, sine(48000 * 6), r, [](FxSettings& st, size_t p) {
            if (p > 48000 * 2) st.reverb.type = ReverbType::plate;
        });
        FxSettings rp = r; rp.reverb.type = ReverbType::plate;
        FxChain d; d.prepare(sr, 512);
        const auto steadyPlate = run(d, sine(48000 * 6), rp);
        double got, ref;
        { const bool ok = clickFree(changed.L, { &steady.L, &steadyPlate.L }, got, ref);
        check(ok,
              "REVERB: changing the type while playing is click-free (content above 4 kHz %.1f dBFS vs %.1f dBFS steady)", 20 * std::log10(got + 1e-12), 20 * std::log10(ref + 1e-12)); }
        FxChain c2; c2.prepare(sr, 512);
        const auto moved = run(c2, sine(48000 * 6), r, [](FxSettings& st, size_t p) {
            if (p > 48000 * 2) st.reverb.preDelayMs = 80.0f;
        });
        // a pre-delay change glides (a short, slight pitch bend of the reverb), it never jumps
        { const bool ok = clickFree(moved.L, { &steady.L }, got, ref);
        check(ok,
              "REVERB: moving the pre-delay while playing is click-free (above 4 kHz %.1f dBFS vs %.1f dBFS steady)", 20 * std::log10(got + 1e-12), 20 * std::log10(ref + 1e-12)); }
    }

    // 4. flanger: stable at maximum feedback, on/off click-free
    {
        FxChain fx; fx.prepare(sr, 512);
        FxSettings s;
        s.flanger = { true, 2.0f, 1.0f, 0.9f, 0.5f };
        const auto y = run(fx, noise(48000 * 10, 7), s);
        double peak = 0; bool finite = true;
        for (double v : y.L) { finite = finite && std::isfinite(v); peak = std::max(peak, std::abs(v)); }
        check(finite && peak < 20.0, "FLANGER: 90 %% feedback, full depth, 10 s of noise: stable (peak %.2f)", peak);

        FxChain a; a.prepare(sr, 512);
        FxSettings f;
        f.flanger = { true, 0.3f, 0.6f, 0.5f, 0.5f };
        const auto on = run(a, sine(48000 * 4), f);
        FxChain b; b.prepare(sr, 512);
        const auto tog = run(b, sine(48000 * 4), f, [](FxSettings& st, size_t p) { st.flanger.on = (p / 24000) % 2 == 0; });
        const auto dry = sine(48000 * 4);
        double got, ref;
        { const bool ok = clickFree(tog.L, { &on.L, &dry.L }, got, ref);
        check(ok,
              "FLANGER: switching on/off while playing is click-free (above 4 kHz %.1f dBFS vs %.1f dBFS steady)", 20 * std::log10(got + 1e-12), 20 * std::log10(ref + 1e-12)); }
    }

    // 5. CPU: everything on
    {
        FxChain fx; fx.prepare(sr, 64);
        FxSettings s;
        s.flanger.on = true;
        s.delay[0] = { true, 500.0, 0.4f, 0.4f, 6000.0f, true };
        s.delay[1] = { true, 756.0, 0.4f, 0.4f, 6000.0f, false };
        s.reverb = { true, ReverbType::cathedral, 5.0f, 40.0f, 6000.0f, 0.3f };
        auto x = noise(48000 * 10, 8);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t p = 0; p + 64 <= x.L.size(); p += 64) fx.process(x.L.data() + p, x.R.data() + p, 64, s);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(secs / 10.0 * 100.0 < 10.0, "CPU: all effects on (cathedral) cost %.1f %% of one core", secs / 10.0 * 100.0);
    }

    // 6. StreamResampler (export, backing tracks): level, distortion, aliasing, length, chunking
    {
        auto tone = [](double f, double rate, size_t n) {
            std::vector<float> v(n);
            for (size_t i = 0; i < n; ++i) v[i] = (float) (0.5 * std::sin(2 * kPi * f * (double) i / rate));
            return v;
        };
        // fit amplitude of frequency f and the residual (everything else) in the middle part
        auto analyse = [](const std::vector<float>& y, double f, double rate, double& ampDb, double& residualDb) {
            const size_t a = y.size() / 4, b = 3 * y.size() / 4;
            double ss = 0, sc = 0, cc = 0, sy = 0, cy = 0;
            for (size_t i = a; i < b; ++i)
            {
                const double s1 = std::sin(2 * kPi * f * (double) i / rate), c1 = std::cos(2 * kPi * f * (double) i / rate);
                ss += s1 * s1; cc += c1 * c1; sc += s1 * c1; sy += s1 * y[i]; cy += c1 * y[i];
            }
            const double det = ss * cc - sc * sc, A = (sy * cc - cy * sc) / det, B = (cy * ss - sy * sc) / det;
            double res = 0, sig = 0;
            for (size_t i = a; i < b; ++i)
            {
                const double fit = A * std::sin(2 * kPi * f * (double) i / rate) + B * std::cos(2 * kPi * f * (double) i / rate);
                res += (y[i] - fit) * (y[i] - fit);
                sig += fit * fit;
            }
            ampDb = 20 * std::log10(std::sqrt(A * A + B * B) / 0.5);
            residualDb = 10 * std::log10(res / sig);
        };
        double worstAmp = 0, worstRes = -300;
        for (auto rates : { std::pair<double, double> { 44100, 48000 }, { 48000, 44100 }, { 48000, 96000 }, { 96000, 44100 } })
            for (double f : { 100.0, 1000.0, 15000.0 })
            {
                StreamResampler r(rates.first, rates.second, 1);
                const auto x = tone(f, rates.first, 96000);
                std::vector<std::vector<float>> out;
                const float* p = x.data();
                r.process(&p, (int) x.size(), out);
                r.finish(out);
                double a, res;
                analyse(out[0], f, rates.second, a, res);
                worstAmp = std::max(worstAmp, std::abs(a));
                worstRes = std::max(worstRes, res);
            }
        check(worstAmp < 0.01 && worstRes < -90.0,
              "RESAMPLER: 44.1/48/96 kHz conversions, 100 Hz-15 kHz: level within %.4f dB, everything else at %.0f dB", worstAmp, worstRes);

        // aliasing: a 23 kHz tone at 48 kHz has no place at 44.1 kHz and must disappear
        StreamResampler r(48000, 44100, 1);
        const auto x = tone(23000, 48000, 96000);
        std::vector<std::vector<float>> out;
        const float* p = x.data();
        r.process(&p, (int) x.size(), out);
        r.finish(out);
        double peak = 0;
        for (size_t i = out[0].size() / 4; i < 3 * out[0].size() / 4; ++i) peak = std::max(peak, (double) std::abs(out[0][i]));
        const size_t expectLen = (size_t) std::ceil(96000.0 * 44100.0 / 48000.0);
        check(20 * std::log10(peak / 0.5 + 1e-12) < -90.0 && out[0].size() == expectLen,
              "RESAMPLER: no aliasing (23 kHz at 48 kHz -> %.0f dB at 44.1 kHz), exact length (%.0f samples)", 20 * std::log10(peak / 0.5 + 1e-12), (double) out[0].size());

        // streaming in random chunks gives exactly the same result as one call
        StreamResampler one(44100, 48000, 2), many(44100, 48000, 2);
        const auto a = tone(440, 44100, 50000), b = tone(3000, 44100, 50000);
        std::vector<std::vector<float>> o1, o2;
        const float* ab[2] = { a.data(), b.data() };
        one.process(ab, 50000, o1); one.finish(o1);
        std::mt19937 rng(4);
        std::uniform_int_distribution<int> blk(1, 3000);
        for (int pos = 0; pos < 50000;)
        {
            const int n = std::min(blk(rng), 50000 - pos);
            const float* q[2] = { a.data() + pos, b.data() + pos };
            many.process(q, n, o2);
            pos += n;
        }
        many.finish(o2);
        check(o1 == o2, "RESAMPLER: processing in random chunks gives a bit-identical result");
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

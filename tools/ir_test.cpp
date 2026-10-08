// ir_test: objective tests of the cabinet IR stage (Convolver, IR preparation, IrSlot).  Exit 0 = all pass.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>

#include "ampsurd/Convolver.h"
#include "ampsurd/IrSlot.h"

using namespace ampsurd;
static int failures = 0;
static const double kPi = 3.14159265358979323846;

static void check(bool ok, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0)
{
    std::printf("[%s] ", ok ? "PASS" : "FAIL");
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    failures += ok ? 0 : 1;
}

static std::vector<double> noise(size_t n, unsigned seed, double amp = 0.5)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, amp);
    std::vector<double> v(n);
    for (auto& x : v) x = nd(rng);
    return v;
}

// a cabinet-like IR: decaying, low-passed noise
static std::vector<double> cabIr(int len, unsigned seed)
{
    auto v = noise((size_t) len, seed, 1.0);
    double lp = 0;
    for (int i = 0; i < len; ++i)
    {
        lp += 0.3 * (v[(size_t) i] - lp);
        v[(size_t) i] = lp * std::exp(-6.0 * i / len);
    }
    return v;
}

static void runBlocks(Convolver& c, const std::vector<double>& in, std::vector<double>& out, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> blk(1, 700);
    out = in;
    for (size_t p = 0; p < out.size();)
    {
        const int n = (int) std::min<size_t>((size_t) blk(rng), out.size() - p);
        c.process(out.data() + p, out.data() + p, n); // in place
        p += (size_t) n;
    }
}

static double dtftDb(const std::vector<double>& h, double f, double fs)
{
    std::complex<double> acc = 0;
    for (size_t n = 0; n < h.size(); ++n) acc += h[n] * std::polar(1.0, -2 * kPi * f * (double) n / fs);
    return 20 * std::log10(std::abs(acc) + 1e-300);
}

int main()
{
    // 1. Accuracy against direct convolution, random block sizes, all partition boundaries
    {
        double worst = 0;
        for (int len : { 1, 50, 64, 65, 300, 4096, 4097, 9000, 48000 })
        {
            const auto h = cabIr(len, (unsigned) len);
            const bool longIr = len > 20000;
            const auto x = noise(longIr ? 70000 : 30000, 7);
            Convolver c(h);
            std::vector<double> y;
            runBlocks(c, x, y, (unsigned) len + 1);
            double peak = 0, err = 0;
            for (size_t t = 0; t < x.size(); t += longIr ? 97 : 1)
            {
                double ref = 0;
                for (int k = 0; k < len && k <= (int) t; ++k) ref += h[(size_t) k] * x[t - (size_t) k];
                peak = std::max(peak, std::abs(ref));
                err = std::max(err, std::abs(ref - y[t]));
            }
            worst = std::max(worst, err / peak);
        }
        check(worst < 1e-12, "CONVOLVER: matches direct convolution for IRs of 1..48000 samples, random block sizes (worst relative error %.1e)", worst);
    }

    // 2. Zero latency: an impulse in gives the IR out, starting at the very first sample
    {
        const auto h = cabIr(10000, 3);
        Convolver c(h);
        std::vector<double> x(20000, 0.0), y;
        x[0] = 1.0;
        runBlocks(c, x, y, 5);
        double err = 0;
        for (size_t i = 0; i < x.size(); ++i) err = std::max(err, std::abs(y[i] - (i < h.size() ? h[i] : 0.0)));
        check(err < 1e-14 && std::abs(y[0] - h[0]) < 1e-15, "CONVOLVER: zero latency - impulse response reproduced from sample 0 (max error %.1e)", err);
    }

    // 3. CPU and evenness for a 1 s IR at 48 kHz (64-sample blocks)
    {
        double evenness = 0;
        Convolver c(cabIr(48000, 9));
        auto x = noise(48000 * 10, 2);
        double worstBlock = 0, total = 0;
        std::vector<double> times;
        std::vector<std::vector<double>> byPhase(32);
        int blocks = 0;
        for (size_t p = 0; p + 64 <= x.size(); p += 64, ++blocks)
        {
            const auto t0 = std::chrono::steady_clock::now();
            c.process(x.data() + p, x.data() + p, 64);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            total += us;
            if (blocks > 100) worstBlock = std::max(worstBlock, us);
            if (blocks > 100) times.push_back(us);
            if (blocks > 100) byPhase[(size_t) (blocks % 32)].push_back(us);
        }
        const double blockUs = 64.0 / 48000.0 * 1e6;
        const double load = total / blocks / blockUs * 100.0;
        std::sort(times.begin(), times.end());
        const double p999 = times[(size_t) (times.size() * 0.999)];
        double worstPhase = 0, bestPhase = 1e9;
        for (auto& v : byPhase)
        {
            std::sort(v.begin(), v.end());
            worstPhase = std::max(worstPhase, v[v.size() / 2]);
            bestPhase = std::min(bestPhase, v[v.size() / 2]);
        }
        std::printf("[INFO] block time median %.1f us, 99.9 %% %.1f us; typical time per position in the 2048-sample cycle %.1f..%.1f us\n",
                    times[times.size() / 2], p999, bestPhase, worstPhase);
        evenness = worstPhase / blockUs * 100.0;
        std::printf("[INFO] 1 s IR at 48 kHz: %.2f %% of one core on average, worst 64-sample block %.0f us (%.0f %% of the block's time)\n",
                    load, worstBlock, worstBlock / blockUs * 100.0);
        check(load < 8.0 && evenness < 10.0,
              "CONVOLVER: a 1 s IR costs %.2f %% of one core; work is evenly spread (no block typically needs more than %.1f %% of its time)", load, evenness);
    }

    // 4. IR preparation: silence trimmed, resampling keeps the frequency response, unit energy
    {
        std::vector<double> raw(100, 0.0);
        const auto body = cabIr(2048, 11);
        raw.insert(raw.end(), body.begin(), body.end());
        const auto same = prepareImpulseResponse(raw, 48000, 48000);
        double e = 0;
        for (double v : same.samples) e += v * v;
        check(same.trimmedSamples >= 95 && same.trimmedSamples <= 100 && std::abs(e - 1.0) < 1e-12 && !same.resampled,
              "IR PREP: 100 samples of leading silence removed (%.0f), unit energy (%.6f)", same.trimmedSamples, e);

        const auto res = prepareImpulseResponse(body, 44100, 48000);
        const auto ref = prepareImpulseResponse(body, 44100, 44100);
        double lo = 1e9, hi = -1e9;
        for (double f = 50; f < 18000; f *= 1.15)
        {
            const double d = dtftDb(res.samples, f, 48000) - dtftDb(ref.samples, f, 44100);
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
        check(res.resampled && hi - lo < 0.1, "IR PREP: 44.1 kHz IR in a 48 kHz session keeps its frequency response (50 Hz-18 kHz within %.3f dB)", hi - lo);

        const auto longIr = prepareImpulseResponse(cabIr(96000, 4), 48000, 48000, 1.0);
        check(longIr.truncated && longIr.samples.size() == 48000, "IR PREP: IRs longer than 1 s are shortened with a fade-out (%.0f samples)", (double) longIr.samples.size());
    }

    // 5. IrSlot: no IR / bypass = untouched and idle; IR on = exactly the convolution; switching is click-free
    {
        const double sr = 48000;
        auto sine = [&](size_t n) {
            std::vector<double> v(n);
            for (size_t i = 0; i < n; ++i) v[i] = 0.4 * std::sin(2 * kPi * 196.0 * i / sr) + 0.2 * std::sin(2 * kPi * 1250.0 * i / sr);
            return v;
        };
        const auto hA = prepareImpulseResponse(cabIr(3000, 21), sr, sr).samples;
        const auto hB = prepareImpulseResponse(cabIr(9000, 22), sr, sr).samples;

        IrSlot slot;
        slot.prepare(sr, 256);
        auto x = sine(4800), y = x;
        for (size_t p = 0; p < y.size(); p += 256) slot.process(y.data() + p, (int) std::min<size_t>(256, y.size() - p), true);
        check(y == x && !slot.isRunning(), "IR SLOT: without an IR the signal passes bit-identically and nothing runs");

        // steady states for the click reference
        auto render = [&](const std::vector<double>& h, size_t n) {
            Convolver c(h);
            auto v = sine(n);
            c.process(v.data(), v.data(), (int) v.size());
            return v;
        };
        const size_t N = 48000 * 3;
        const auto steadyA = render(hA, N), steadyB = render(hB, N), steadyDry = sine(N);
        auto curvature = [](const std::vector<double>& v, size_t from) {
            double m = 0;
            for (size_t i = std::max<size_t>(2, from); i < v.size(); ++i) m = std::max(m, std::abs(v[i] - 2 * v[i - 1] + v[i - 2]));
            return m;
        };
        const double steady = std::max({ curvature(steadyA, 20000), curvature(steadyB, 20000), curvature(steadyDry, 0) });

        IrSlot s2;
        s2.prepare(sr, 256);
        auto z = sine(N);
        bool exactOn = true, exactBypass = true;
        for (size_t p = 0, blk = 0; p < z.size(); p += 256, ++blk)
        {
            const int n = (int) std::min<size_t>(256, z.size() - p);
            if (blk == 10) s2.submit(std::make_unique<Convolver>(hA));          // load
            if (blk == 120) s2.submit(std::make_unique<Convolver>(hB));         // replace
            const bool enabled = !(blk >= 250 && blk < 350);                    // bypass for a while
            if (blk == 450) s2.submit(std::make_unique<Convolver>(std::vector<double> {})); // remove
            s2.process(z.data() + p, n, enabled);
            if (blk == 100) // IR A fully on: compare with the reference (IR adopted at block 10, cold start)
            {
                Convolver ref(hA);
                auto v = sine(p + (size_t) n);
                std::vector<double> part(v.begin() + 256 * 10, v.end());
                ref.process(part.data(), part.data(), (int) part.size());
                for (int i = 0; i < n; ++i) exactOn = exactOn && std::abs(z[p + (size_t) i] - part[part.size() - (size_t) n + (size_t) i]) < 1e-12;
            }
            if (blk == 300)
            {
                const auto d = sine(p + (size_t) n);
                for (int i = 0; i < n; ++i) exactBypass = exactBypass && z[p + (size_t) i] == d[p + (size_t) i];
                exactBypass = exactBypass && !s2.isRunning();
            }
            s2.collectGarbage();
        }
        check(exactOn, "IR SLOT: IR on -> output is exactly the convolution");
        check(exactBypass, "IR SLOT: bypassed -> output bit-identical to the input and the convolver is idle");
        const double c = curvature(z, 1);
        check(c <= steady * 1.05, "IR SLOT: load, replace, bypass, un-bypass and remove while playing are click-free (largest curvature %.4f vs %.4f steady)", c, steady);
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

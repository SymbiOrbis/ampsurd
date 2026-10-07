// limiter_test: proves the SafetyLimiter guarantee.
//   1. Ceiling is never exceeded (extreme signals, huge transients, random block sizes, all rates).
//   2. Below the ceiling it is bit-transparent (pure delay).
//   3. It turns level down instead of clipping: a sine driven 12 dB over the ceiling comes out
//      with near-zero distortion (compared with what a hard clipper would produce).
// Exit code 0 = all pass.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ampsurd/SafetyLimiter.h"

using ampsurd::SafetyLimiter;
static const double kPi = 3.14159265358979323846;

static double thd(const std::vector<double>& y, double f, double sr, size_t start)
{
    // Energy at the fundamental vs everything else (DFT at f over an integer number of cycles).
    const size_t cycles = (size_t) std::floor((double) (y.size() - start) * f / sr);
    const size_t len = (size_t) std::lround((double) cycles * sr / f);
    double re = 0, im = 0, total = 0;
    for (size_t i = 0; i < len; ++i)
    {
        const double v = y[start + i];
        re += v * std::cos(2 * kPi * f * (double) i / sr);
        im += v * std::sin(2 * kPi * f * (double) i / sr);
        total += v * v;
    }
    const double fund = 2.0 * (re * re + im * im) / (double) len;
    return std::sqrt(std::max(0.0, total - fund) / fund);
}

int main()
{
    int failures = 0;
    std::mt19937 rng(42);

    // 1. Ceiling guarantee
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        SafetyLimiter lim;
        lim.prepare(sr);
        const double ceil = lim.getCeiling();
        std::normal_distribution<double> noise(0.0, 1.0);
        std::uniform_int_distribution<int> blk(1, 512);
        std::uniform_real_distribution<double> u(0, 1);
        double maxOut = 0;
        size_t total = 0;
        std::vector<double> buf(512);
        while (total < (size_t) (sr * 60))
        {
            const int n = blk(rng);
            for (int i = 0; i < n; ++i)
            {
                // mixture: quiet guitar-like noise, loud bursts, single-sample spikes up to +60 dB
                double v = 0.2 * noise(rng);
                if ((total / 4800) % 3 == 1) v *= 30.0;
                if (u(rng) < 0.001) v = (u(rng) < 0.5 ? -1 : 1) * 1000.0;
                buf[(size_t) i] = v;
            }
            lim.process(buf.data(), n);
            for (int i = 0; i < n; ++i) maxOut = std::max(maxOut, std::abs(buf[(size_t) i]));
            total += (size_t) n;
        }
        const bool ok = maxOut <= ceil;
        failures += ok ? 0 : 1;
        std::printf("[%s] %6.0f Hz: 60 s of extreme signal (spikes to +60 dBFS), max output %.6f dBFS, ceiling %.2f dBFS\n",
                    ok ? "PASS" : "FAIL", sr, 20 * std::log10(maxOut), 20 * std::log10(ceil));
    }

    // 2. Transparency below the ceiling
    {
        SafetyLimiter lim;
        lim.prepare(48000.0);
        const int L = lim.getLatencySamples();
        std::vector<double> x(48000), y;
        std::normal_distribution<double> noise(0.0, 0.2);
        for (auto& v : x) v = std::clamp(noise(rng), -0.85, 0.85);
        y = x;
        for (size_t p = 0; p < y.size(); p += 64) lim.process(y.data() + p, (int) std::min<size_t>(64, y.size() - p));
        size_t diff = 0;
        for (size_t i = (size_t) L; i < x.size(); ++i) diff += (y[i] != x[i - (size_t) L]);
        failures += diff == 0 ? 0 : 1;
        std::printf("[%s] below ceiling: output identical to input delayed by %d samples (%zu differing samples)\n",
                    diff == 0 ? "PASS" : "FAIL", L, diff);
    }

    // 3. Limits, does not clip: sine 12 dB over the ceiling
    {
        const double sr = 48000.0, f = 220.0;
        SafetyLimiter lim;
        lim.prepare(sr);
        const double amp = lim.getCeiling() * std::pow(10.0, 12.0 / 20.0);
        std::vector<double> y((size_t) sr * 2), clipped(y.size());
        for (size_t i = 0; i < y.size(); ++i)
        {
            y[i] = amp * std::sin(2 * kPi * f * (double) i / sr);
            clipped[i] = std::clamp(y[i], -lim.getCeiling(), lim.getCeiling());
        }
        lim.process(y.data(), (int) y.size());
        const double t = thd(y, f, sr, (size_t) sr), tc = thd(clipped, f, sr, (size_t) sr);
        const bool ok = t < 0.01;
        failures += ok ? 0 : 1;
        std::printf("[%s] sine +12 dB over ceiling: distortion %.4f %% with limiter vs %.1f %% if it were clipped\n",
                    ok ? "PASS" : "FAIL", 100 * t, 100 * tc);
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

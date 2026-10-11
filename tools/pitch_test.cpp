// pitch_test: objective tests of the PitchShifter (tuning the guitar before the amps).
// Exit code 0 = all pass.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "ampsurd/Convolver.h"
#include "ampsurd/PitchShifter.h"

using namespace ampsurd;
static const double kPi = 3.14159265358979323846;
static int failures = 0;

static void check(bool ok, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0)
{
    std::printf("[%s] ", ok ? "PASS" : "FAIL");
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    failures += ok ? 0 : 1;
}

// plucked-string-like tone: harmonics 1..8, slow decay
static std::vector<double> pluck(double f, double sr, int n, double amp = 0.3)
{
    std::vector<double> x((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double s = 0;
        for (int h = 1; h <= 8; ++h) s += std::sin(2 * kPi * f * h * i / sr) / h;
        x[(size_t) i] = amp * s * std::exp(-0.6 * i / sr);
    }
    return x;
}

// strongest frequency in [lo, hi] (FFT of 1 s, parabolic interpolation)
static double peakFreq(const std::vector<double>& y, int start, double sr, double lo, double hi)
{
    const int N = 1 << 16;
    std::vector<double> re((size_t) N, 0.0), im((size_t) N, 0.0);
    const int len = std::min(N, (int) y.size() - start);
    for (int i = 0; i < len; ++i) re[(size_t) i] = y[(size_t) (start + i)] * (0.5 - 0.5 * std::cos(2 * kPi * i / len));
    Fft fft(N);
    fft.forward(re.data(), im.data());
    int best = 1;
    double bm = 0;
    for (int k = (int) (lo * N / sr); k <= (int) (hi * N / sr); ++k)
    {
        const double m = std::hypot(re[(size_t) k], im[(size_t) k]);
        if (m > bm) { bm = m; best = k; }
    }
    auto mag = [&](int k) { return std::log(std::hypot(re[(size_t) k], im[(size_t) k]) + 1e-30); };
    const double a = mag(best - 1), b = mag(best), c = mag(best + 1);
    const double p = 0.5 * (a - c) / (a - 2 * b + c);
    return (best + p) * sr / N;
}

int main()
{
    const double sr = 48000.0;
    const int N = (int) (sr * 3);

    // 1. tuning accuracy on guitar-like notes (low E, A, high e) for typical tunings
    {
        double worstCents = 0;
        for (double semis : { -1.0, -2.0, -5.0, -12.0, 2.0, 5.0, 12.0 })
            for (double f : { 82.41, 110.0, 329.6 })
            {
                PitchShifter ps;
                ps.prepare(sr);
                ps.setParameters(true, (float) semis, 0.0f);
                ps.reset();
                auto x = pluck(f, sr, N);
                ps.process(x.data(), N);
                const double expect = f * std::pow(2.0, semis / 12.0);
                const double got = peakFreq(x, (int) sr, sr, expect * 0.8, expect * 1.2);
                worstCents = std::max(worstCents, std::abs(1200.0 * std::log2(got / expect)));
            }
        check(worstCents < 5.0, "PITCH: -1/-2/-5/-12/+2/+5/+12 semitones on 82 / 110 / 330 Hz notes land on the right pitch (worst %.2f cents)", worstCents);
    }

    // 2. cents
    {
        PitchShifter ps;
        ps.prepare(sr);
        ps.setParameters(true, -1.0f, -30.0f);
        ps.reset();
        auto x = pluck(110.0, sr, N);
        ps.process(x.data(), N);
        const double expect = 110.0 * std::pow(2.0, -1.3 / 12.0), got = peakFreq(x, (int) sr, sr, 90, 120);
        check(std::abs(1200.0 * std::log2(got / expect)) < 3.0, "PITCH: -1 semitone -30 cents -> %.2f Hz (expected %.2f Hz)", got, expect);
    }

    // 3. clean sound: a shifted sine stays a sine (little roughness from the splices)
    {
        double worstDb = -1e9;
        for (double semis : { -2.0, -5.0, 3.0 })
            for (double f : { 82.41, 196.0 })
            {
                PitchShifter ps;
                ps.prepare(sr);
                ps.setParameters(true, (float) semis, 0.0f);
                ps.reset();
                std::vector<double> x((size_t) N);
                for (int i = 0; i < N; ++i) x[(size_t) i] = 0.3 * std::sin(2 * kPi * f * i / sr);
                ps.process(x.data(), N);
                const double fo = f * std::pow(2.0, semis / 12.0);
                // least-squares fit of a sine at fo over 1 s, residual = everything else
                double s = 0, c = 0;
                const int a0 = (int) sr, len = (int) sr;
                for (int i = a0; i < a0 + len; ++i) { s += x[(size_t) i] * std::sin(2 * kPi * fo * i / sr); c += x[(size_t) i] * std::cos(2 * kPi * fo * i / sr); }
                s *= 2.0 / len; c *= 2.0 / len;
                double res = 0, sig = 0;
                for (int i = a0; i < a0 + len; ++i)
                {
                    const double fit = s * std::sin(2 * kPi * fo * i / sr) + c * std::cos(2 * kPi * fo * i / sr);
                    res += (x[(size_t) i] - fit) * (x[(size_t) i] - fit);
                    sig += fit * fit;
                }
                worstDb = std::max(worstDb, 10 * std::log10(res / sig));
            }
        check(worstDb < -30.0, "PITCH: a shifted sine stays clean (artefacts at least %.1f dB below the note; -2/-5/+3 st, 82 / 196 Hz)", -worstDb);
    }

    // 4. delay: how late the shifted note starts (onset of a burst)
    {
        double worstMs = 0;
        for (double semis : { -2.0, -5.0, 2.0 })
            for (double f : { 82.41, 329.6 })
            {
                PitchShifter ps;
                ps.prepare(sr);
                ps.setParameters(true, (float) semis, 0.0f);
                ps.reset();
                std::vector<double> x((size_t) sr, 0.0);
                const int onset = 12000;
                for (int i = onset; i < (int) x.size(); ++i) x[(size_t) i] = 0.3 * std::sin(2 * kPi * f * (i - onset) / sr);
                ps.process(x.data(), (int) x.size());
                int first = -1;
                for (int i = 0; i < (int) x.size(); ++i)
                    if (std::abs(x[(size_t) i]) > 0.03) { first = i; break; }
                // the dry sine reaches 0.03 after asin(0.1)/(2 pi f) seconds
                const double dryReach = std::asin(0.1) / (2 * kPi * f) * sr;
                worstMs = std::max(worstMs, (first - onset - dryReach) * 1000.0 / sr);
            }
        check(worstMs < 9.0, "DELAY: the shifted note starts at most %.1f ms late (82 Hz and 330 Hz, -5 ... +2 semitones)", worstMs);
    }

    // 5. OFF = untouched; switching ON / OFF while playing = no click
    {
        PitchShifter ps;
        ps.prepare(sr);
        ps.setParameters(false, -2.0f, 0.0f);
        auto x = pluck(110.0, sr, (int) sr);
        auto y = x;
        ps.process(y.data(), (int) y.size());
        check(x == y && ps.getLatencySamples() == 0, "OFF: bit-identical, no delay reported");

        auto z = pluck(110.0, sr, (int) (sr * 2));
        PitchShifter t;
        t.prepare(sr);
        double steady = 0, worst = 0;
        for (int b = 0; b < (int) z.size(); b += 256)
        {
            t.setParameters(b >= (int) (0.5 * sr) && b < (int) (1.3 * sr), -3.0f, 0.0f);
            t.process(z.data() + b, std::min(256, (int) z.size() - b));
        }
        for (int i = 2; i < (int) z.size(); ++i)
        {
            const double c = std::abs(z[(size_t) i] - 2 * z[(size_t) i - 1] + z[(size_t) i - 2]);
            if (i > (int) (0.6 * sr) && i < (int) (1.2 * sr)) steady = std::max(steady, c);
            worst = std::max(worst, c);
        }
        const auto dry = pluck(110.0, sr, (int) sr);
        double dryCurv = 0;
        for (int i = 2; i < (int) dry.size(); ++i) dryCurv = std::max(dryCurv, std::abs(dry[(size_t) i] - 2 * dry[(size_t) i - 1] + dry[(size_t) i - 2]));
        check(worst <= std::max(steady, dryCurv) * 1.5, "ON / OFF while playing: no click (largest curvature %.4f vs %.4f playing)", worst, std::max(steady, dryCurv));
    }

    // 6. a chord (two notes) is shifted as a whole
    {
        PitchShifter ps;
        ps.prepare(sr);
        ps.setParameters(true, -2.0f, 0.0f);
        ps.reset();
        auto x = pluck(110.0, sr, N);
        const auto fifth = pluck(164.8, sr, N);
        for (int i = 0; i < N; ++i) x[(size_t) i] += fifth[(size_t) i];
        ps.process(x.data(), N);
        const double r = std::pow(2.0, -2.0 / 12.0);
        const double a = peakFreq(x, (int) sr, sr, 110 * r * 0.95, 110 * r * 1.05), b = peakFreq(x, (int) sr, sr, 164.8 * r * 0.95, 164.8 * r * 1.05);
        check(std::abs(1200 * std::log2(a / (110 * r))) < 5 && std::abs(1200 * std::log2(b / (164.8 * r))) < 5,
              "CHORD: A + E shifted down 2 semitones -> %.1f Hz + %.1f Hz (expected %.1f + %.1f)", a, b, 110 * r, 164.8 * r);
    }

    // 7. delay on a low power chord (E2 + B2: common period 24 ms) and CPU cost
    {
        PitchShifter ps;
        ps.prepare(sr);
        ps.setParameters(true, -2.0f, 0.0f);
        ps.reset();
        std::vector<double> x((size_t) sr, 0.0);
        const int onset = 12000;
        const auto e = pluck(82.41, sr, (int) sr), b = pluck(123.47, sr, (int) sr);
        for (int i = onset; i < (int) x.size(); ++i) x[(size_t) i] = e[(size_t) (i - onset)] + b[(size_t) (i - onset)];
        const auto t0 = std::chrono::steady_clock::now();
        ps.process(x.data(), (int) x.size());
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        int first = -1;
        for (int i = 0; i < (int) x.size(); ++i)
            if (std::abs(x[(size_t) i]) > 0.03) { first = i; break; }
        const double ms = (first - onset) * 1000.0 / sr;
        check(ms < 14.0, "DELAY: low E power chord starts %.1f ms late (the chord repeats only every 24 ms)", ms);
        check(us < 1e6 * 0.05, "CPU: 1 s of audio shifted in %.1f ms (%.1f %% of one core)", us / 1000.0, us / 1e4);
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

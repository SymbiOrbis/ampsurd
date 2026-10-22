// doubler_test: objective tests of the DOUBLER module.  Exit 0 = all pass.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ampsurd/Doubler.h"

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

static Stereo run(const std::vector<double>& mono, const DoublerSettings& s, uint32_t seed = 7, int block = 128, Doubler* keep = nullptr)
{
    Doubler local;
    Doubler& d = keep ? *keep : local;
    d.prepare(sr, block, seed);
    Stereo o { mono, mono };
    for (size_t p = 0; p < mono.size(); p += (size_t) block)
    {
        const int n = (int) std::min<size_t>((size_t) block, mono.size() - p);
        d.process(o.L.data() + p, o.R.data() + p, n, s);
    }
    return o;
}

static double power(const std::vector<double>& x, size_t from = 0, size_t to = 0)
{
    if (to == 0) to = x.size();
    double p = 0;
    for (size_t i = from; i < to; ++i) p += x[i] * x[i];
    return p / (double) (to - from);
}
static double db(double p) { return 10.0 * std::log10(std::max(p, 1e-30)); }

// guitar-like test: notes (sum of decaying harmonics, smooth 5 ms attack) separated by silences
static std::vector<double> notes(double seconds, double gapS, unsigned seed, int* count = nullptr)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(0, 1);
    std::vector<double> x((size_t) (seconds * sr), 0.0);
    size_t pos = (size_t) (0.05 * sr);
    int c = 0;
    const double roots[] = { 82.4, 110.0, 146.8, 98.0, 123.5 };
    while (pos < x.size())
    {
        const double f0 = roots[c % 5] * (u(rng) < 0.5 ? 1.0 : 2.0);
        const size_t len = (size_t) ((0.3 + 0.4 * u(rng)) * sr);
        for (size_t i = 0; i < len && pos + i < x.size(); ++i)
        {
            const double t = (double) i / sr;
            const double att = std::min(1.0, t / 0.005), rel = std::min(1.0, (double) (len - i) / (0.01 * sr));
            double v = 0;
            for (int h = 1; h <= 12; ++h) v += std::sin(2 * kPi * f0 * h * t + h) * std::exp(-t * (1.5 + 0.6 * h)) / h;
            x[pos + i] += 0.3 * v * att * rel;
        }
        pos += len + (size_t) (gapS * sr * (0.5 + u(rng)));
        ++c;
    }
    if (count) *count = c;
    return x;
}

int main()
{
    DoublerSettings on;
    on.on = true;

    // 1. off: untouched
    {
        const auto x = notes(1.0, 0.1, 1);
        DoublerSettings off;
        Doubler d;
        const auto o = run(x, off, 7, 128, &d);
        check(o.L == x && o.R == x && !d.isActive(), "OFF: output bit-identical, module idle");
    }

    // 2. a sine through one double (dry hard left, double hard right, LOW MONO off): pitch moves a little
    {
        auto s = on;
        s.voices = 1;
        s.width = 1.0;
        s.lowMonoHz = 0.0;
        s.levelDb = 0.0;
        for (int busy = 0; busy < 2; ++busy)
        {
        // busy = re-picked every 250 ms (drift runs at full speed), else one sustained note
        std::vector<double> x((size_t) (20.0 * sr));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / sr, ph = std::fmod(t, 0.25);
            const double env = busy ? 0.4 + 0.6 * std::exp(-ph / 0.05) * std::min(1.0, ph / 0.002) : 1.0;
            x[i] = 0.3 * env * std::sin(2 * kPi * 440.0 * t);
        }
        Doubler d;
        const auto o = run(x, s, 3, 128, &d);
        // instantaneous frequency of the right channel from zero crossings, 50 ms windows
        std::vector<double> cents;
        size_t last = 0;
        double lastT = -1;
        int zc = 0;
        double firstT = -1;
        const size_t win = (size_t) (0.05 * sr);
        for (size_t i = (size_t) (0.5 * sr); i < o.R.size(); ++i)
        {
            if (o.R[i - 1] < 0 && o.R[i] >= 0)
            {
                const double t = (double) (i - 1) + o.R[i - 1] / (o.R[i - 1] - o.R[i]);
                if (firstT < 0) firstT = t;
                lastT = t;
                ++zc;
            }
            if (i - last >= win && zc > 2)
            {
                const double f = (zc - 1) / ((lastT - firstT) / sr);
                cents.push_back(1200.0 * std::log2(f / 440.0));
                zc = 0;
                firstT = -1;
                last = i;
            }
        }
        double mx = 0, mean = 0, sd = 0;
        for (double c : cents) { mx = std::max(mx, std::abs(c)); mean += c; }
        mean /= (double) cents.size();
        for (double c : cents) sd += (c - mean) * (c - mean);
        sd = std::sqrt(sd / (double) cents.size());
        if (busy)
        {
            check(mx < 12.0 && sd > 0.5 && sd < 5.0, "PITCH while picking: the double drifts %.2f cents (sd), at most %.1f cents", sd, mx);
            break;
        }
        check(mx < 4.0 && sd > 0.1 && sd < 1.5, "PITCH sustained: the double drifts %.2f cents (sd), at most %.1f cents (nearly still: a moving comb would sound like a flanger)", sd, mx);
        const double dl = d.getVoiceDelayMs(0);
        check(dl >= 8.0 && dl <= 40.0, "DELAY: the double plays %.1f ms after the guitar (8-40 ms)", dl);
        // dry (left) = the sine, not delayed: pan law +3 dB (hard left), loudness -3 dB (one double at 0 dB)
        const double ratio = std::sqrt(power(o.L, (size_t) sr) / power(x, (size_t) sr));
        double corr = 0;
        for (size_t i = (size_t) sr; i < x.size(); ++i) corr += o.L[i] * x[i];
        corr /= std::sqrt(power(o.L, (size_t) sr) * power(x, (size_t) sr)) * (double) (x.size() - (size_t) sr);
        check(std::abs(20 * std::log10(ratio)) < 0.1 && corr > 0.9999, "DRY: hard left, not delayed (correlation %.5f), level %+.2f dB", corr,
              20 * std::log10(ratio));
        }
    }

    // 3. notes with silences: attacks are detected, new takes are drawn only in silences, no clicks
    {
        int count = 0;
        const auto x = notes(20.0, 0.25, 5, &count);
        auto s = on;
        s.voices = 3;
        Doubler d;
        const auto o = run(x, s, 11, 128, &d);
        check(d.getOnsets() >= count - 1 && d.getOnsets() <= count + 2, "ONSETS: %.0f of %.0f notes detected", d.getOnsets(), count);
        check(d.getRedraws() >= count / 2, "TAKES: new timing / tone drawn %.0f times, in the %.0f silences", d.getRedraws(), count);
        // clicks: energy above 9 kHz in the output vs. in the input (the notes have little there)
        auto hf = [](const std::vector<double>& y) {
            double p = 0; // 3rd-order difference = strong high-pass
            for (size_t i = 3; i < y.size(); ++i)
            {
                const double v = y[i] - 3 * y[i - 1] + 3 * y[i - 2] - y[i - 3];
                p = std::max(p, std::abs(v));
            }
            return p;
        };
        const double inHf = hf(x), outHf = std::max(hf(o.L), hf(o.R));
        check(outHf < 2.5 * inHf, "CLICKS: sharpest sample-to-sample change %.2f x the guitar's (none added)", outHf / inHf);
        bool finite = true;
        for (size_t i = 0; i < o.L.size(); ++i) finite = finite && std::isfinite(o.L[i]) && std::isfinite(o.R[i]);
        check(finite, "FINITE: all output samples finite");
    }

    // 4. loudness kept with 1, 2, 3 doubles; mono sum does not collapse
    {
        const auto x = notes(12.0, 0.1, 9);
        const double pin = power(x, (size_t) sr);
        double worst = 0, worstMono = 0;
        for (int v = 1; v <= 3; ++v)
            for (double lvl : { -6.0, 0.0 })
            {
                auto s = on;
                s.voices = v;
                s.levelDb = lvl;
                const auto o = run(x, s, (uint32_t) (20 + v));
                const double pout = 0.5 * (power(o.L, (size_t) sr) + power(o.R, (size_t) sr));
                worst = std::max(worst, std::abs(db(pout) - db(pin)));
                std::vector<double> m(x.size());
                for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5 * (o.L[i] + o.R[i]);
                worstMono = std::min(worstMono, db(power(m, (size_t) sr)) - db(pin));
            }
        check(worst < 1.5, "LOUDNESS: 1-3 doubles, -6 / 0 dB: as loud as the dry guitar within %.2f dB", worst);
        check(worstMono > -4.0, "MONO: summed to mono at most %.1f dB quieter (no collapse)", worstMono);
    }

    // 5. LOW MONO: a 50 Hz note stays in the centre, untouched in level
    {
        std::vector<double> x((size_t) (4.0 * sr));
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3 * std::sin(2 * kPi * 50.0 * (double) i / sr);
        auto s = on;
        s.voices = 2;
        s.lowMonoHz = 150.0;
        const auto o = run(x, s);
        const size_t from = (size_t) sr;
        const double gl = db(power(o.L, from)) - db(power(x, from)), gr = db(power(o.R, from)) - db(power(x, from));
        check(std::abs(gl) < 0.5 && std::abs(gr) < 0.5, "LOW MONO 150 Hz: a 50 Hz note stays centred and level (L %+.2f dB, R %+.2f dB)", gl, gr);
    }

    // 6. switching on / off and every setting while playing: finite, bounded
    {
        const auto x = notes(10.0, 0.05, 13);
        Doubler d;
        d.prepare(sr, 256, 5);
        Stereo o { x, x };
        std::mt19937 rng(3);
        std::uniform_real_distribution<double> u(0, 1);
        bool ok = true;
        double peakOut = 0;
        DoublerSettings s = on;
        for (size_t p = 0; p < x.size(); p += 64)
        {
            if ((p / 64) % 150 == 0)
            {
                s.on = u(rng) < 0.8;
                s.voices = 1 + (int) (u(rng) * 3);
                s.tight = u(rng);
                s.levelDb = -12 + 18 * u(rng);
                s.width = u(rng);
                s.lowMonoHz = u(rng) < 0.3 ? 0.0 : 20 + 280 * u(rng);
                s.character = u(rng);
            }
            const int n = (int) std::min<size_t>(64, x.size() - p);
            d.process(o.L.data() + p, o.R.data() + p, n, s);
            for (int i = 0; i < n; ++i)
            {
                ok = ok && std::isfinite(o.L[p + (size_t) i]) && std::isfinite(o.R[p + (size_t) i]);
                peakOut = std::max(peakOut, std::max(std::abs(o.L[p + (size_t) i]), std::abs(o.R[p + (size_t) i])));
            }
        }
        double peakIn = 0;
        for (double v : x) peakIn = std::max(peakIn, std::abs(v));
        check(ok && peakOut < 3.0 * peakIn, "STABLE: random settings / on-off while playing: finite, peak %.2f x the input", peakOut / peakIn);
    }

    // 7. CPU: three doubles
    {
        const auto x = notes(10.0, 0.1, 17);
        auto s = on;
        s.voices = 3;
        const auto t0 = std::chrono::steady_clock::now();
        run(x, s);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[INFO] CPU: 3 doubles = %.2f %% of one core (10 s of audio in %.0f ms)\n", 100 * secs / 10.0, secs * 1000);
        check(secs / 10.0 < 0.05, "CPU: 3 doubles under 5 %% of one core (%.2f %%)", 100 * secs / 10.0);
    }

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}

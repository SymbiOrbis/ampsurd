// root_test: objective tests of the ROOT module (RootEnhancer).  Exit 0 = all pass.
//
// The layer is measured alone (amp output = silence), with Goertzel at the chord's frequencies.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ampsurd/RootEnhancer.h"

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

// amplitude of the component at hz (Goertzel over the whole buffer, Hann window)
static double amp(const std::vector<double>& x, double hz, size_t from = 0)
{
    const size_t N = x.size() - from;
    const double w = 2.0 * kPi * hz / sr, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0, wsum = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * (double) i / (double) (N - 1));
        wsum += win;
        const double s0 = x[from + i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}
static double db(double a) { return 20.0 * std::log10(std::max(a, 1e-12)); }

static std::vector<double> chord(const std::vector<double>& hz, double a, double seconds)
{
    std::vector<double> x((size_t) (seconds * sr), 0.0);
    for (size_t i = 0; i < x.size(); ++i)
        for (double f : hz) x[i] += a * std::sin(2.0 * kPi * f * (double) i / sr);
    return x;
}

// runs the module on `di` in random-size blocks; returns the added layer (L starts silent)
static std::vector<double> layerOf(const std::vector<double>& di, const RootSettings& s, unsigned seed = 1)
{
    RootEnhancer r;
    r.prepare(sr, 512);
    std::vector<double> L(di.size(), 0.0), R(di.size(), 0.0);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> blk(1, 512);
    for (size_t p = 0; p < di.size();)
    {
        const int n = (int) std::min<size_t>((size_t) blk(rng), di.size() - p);
        r.process(di.data() + p, L.data() + p, R.data() + p, n, s);
        p += (size_t) n;
    }
    return L;
}

int main()
{
    // drop-B power chord: B1 + F#2 + B2 (root, fifth, octave)
    const double root = 61.735, fifth = 92.499, oct = 123.47;
    const auto pc = chord({ root, fifth, oct }, 0.15, 2.0);
    const size_t skip = (size_t) (0.5 * sr);

    // 1. off: untouched, nothing runs
    {
        RootEnhancer r;
        r.prepare(sr, 256);
        std::vector<double> L = pc, R = pc;
        RootSettings s;
        s.on = false;
        r.process(pc.data(), L.data(), R.data(), 256, s);
        check(L == pc && R == pc && !r.isActive(), "OFF: output bit-identical, module idle");
    }

    // 2. the curve is asymmetric (even harmonics) and smooth at 0
    {
        const double a = RootEnhancer::shape(1.0), b = RootEnhancer::shape(-1.0);
        const double slopeP = RootEnhancer::shape(1e-6) / 1e-6, slopeN = RootEnhancer::shape(-1e-6) / -1e-6;
        check(std::abs(a + b) > 0.05 && std::abs(slopeP - 1.0) < 1e-4 && std::abs(slopeN - 1.0) < 1e-4,
              "SHAPE: asymmetric (f(1) = %.3f, f(-1) = %.3f), slope 1 on both sides of 0", a, b);
    }

    RootSettings base;
    base.on = true;
    base.mode = RootMode::parallel;
    base.freqHz = 150.0;
    base.tightHz = 20.0;
    base.comp = 0.0;

    // 3. GROWL 0: clean low band - the root passes, no new tone one octave below
    {
        auto s = base;
        s.growl = 0.0;
        const auto y = layerOf(pc, s);
        const double r0 = amp(y, root, skip), sub = amp(y, root / 2.0, skip);
        check(std::abs(db(r0) - db(0.15)) < 1.5 && db(sub) - db(r0) < -60.0,
              "GROWL 0: root passes at %.1f dB (input -16.5), octave below %.0f dB re root (none)", db(r0), db(sub) - db(r0));
    }

    // 4. GROWL 1: the difference tone one octave below the root appears (power chord physics)
    double subGrowl = 0;
    {
        auto s = base;
        s.growl = 1.0;
        const auto y = layerOf(pc, s);
        const double r0 = amp(y, root, skip), sub = amp(y, root / 2.0, skip);
        subGrowl = db(sub) - db(r0);
        check(subGrowl > -30.0 && db(r0) > db(0.15) - 12.0,
              "GROWL 1: octave below the root at %.1f dB re root (difference tone), root %.1f dB", subGrowl, db(r0));
    }

    // 5. a major third gives a weaker octave below, but a tone TWO octaves down (0.25 f, the mud of
    //    distorted thirds); a single note gives no octave below at all
    {
        auto s = base;
        s.growl = 1.0;
        const auto third = chord({ root, root * 1.25 }, 0.15, 2.0);
        const auto y = layerOf(third, s);
        const double r0 = amp(y, root, skip), sub = amp(y, root / 2.0, skip), sub2 = amp(y, root / 4.0, skip);
        const auto single = layerOf(chord({ root }, 0.15, 2.0), s);
        const double singleSub = db(amp(single, root / 2.0, skip)) - db(amp(single, root, skip));
        check(db(sub) - db(r0) < subGrowl - 4.0 && db(sub2) - db(r0) > -40.0 && singleSub < -60.0,
              "GROWL 1: major third -> octave below %.1f dB, two octaves below %.1f dB re root; single note -> %.0f dB (none)",
              db(sub) - db(r0), db(sub2) - db(r0), singleSub);
    }

    // 6. SUB (experimental): a single low note gets a tone one octave below
    {
        auto s = base;
        s.mode = RootMode::sub;
        s.growl = 0.0;
        const double e2 = 82.407;
        const auto note = chord({ e2 }, 0.2, 2.0);
        const auto y = layerOf(note, s);
        const double r0 = amp(y, e2, skip), sub = amp(y, e2 / 2.0, skip);
        check(db(sub) - db(r0) > -12.0, "SUB: E2 82.4 Hz -> 41.2 Hz at %.1f dB re the note", db(sub) - db(r0));
    }

    // 7. TIGHT removes what is below it; FREQ removes what is above the band
    {
        auto s = base;
        s.tightHz = 80.0;
        s.freqHz = 100.0;
        const double at30 = RootEnhancer::responseDb(s, 30.0, sr), at90 = RootEnhancer::responseDb(s, 90.0, sr),
                     at800 = RootEnhancer::responseDb(s, 800.0, sr);
        check(at30 < -12.0 && at90 > -8.0 && at800 < -50.0, "FILTERS: TIGHT 80 / FREQ 100 -> 30 Hz %.1f dB, 90 Hz %.1f dB, 800 Hz %.1f dB",
              at30, at90, at800);
        auto e = base;
        e.lowDb = 6.0;
        const double gainAt100 = RootEnhancer::responseDb(e, 100.0, sr) - RootEnhancer::responseDb(base, 100.0, sr);
        check(std::abs(gainAt100 - 6.0) < 0.3, "BASS EQ: LOW +6 dB -> +%.2f dB at 100 Hz", gainAt100);
    }

    // 8. COMP evens out quiet and loud palm mutes
    {
        std::vector<double> x((size_t) (4.0 * sr), 0.0);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / sr;
            const double a = t < 2.0 ? 0.03 : 0.3; // a quiet, then a 20 dB louder part
            x[i] = a * std::sin(2.0 * kPi * root * t);
        }
        auto rangeDb = [&](double c) {
            auto s = base;
            s.growl = 0.0;
            s.comp = c;
            const auto y = layerOf(x, s);
            std::vector<double> q(y.begin() + (long) (1.0 * sr), y.begin() + (long) (2.0 * sr));
            std::vector<double> l(y.begin() + (long) (3.0 * sr), y.end());
            double pq = 0, pl = 0;
            for (double v : q) pq = std::max(pq, std::abs(v));
            for (double v : l) pl = std::max(pl, std::abs(v));
            return db(pl) - db(pq);
        };
        const double r0 = rangeDb(0.0), r1 = rangeDb(1.0);
        check(std::abs(r0 - 20.0) < 1.0 && r1 < r0 - 8.0, "COMP: a 20 dB louder part is %.1f dB louder without, %.1f dB with COMP 100 %%", r0, r1);
    }

    // 9. off fades out within 20 ms and the module stops
    {
        RootEnhancer r;
        r.prepare(sr, 512);
        auto s = base;
        s.growl = 0.5;
        std::vector<double> L(pc.size(), 0.0), R(pc.size(), 0.0);
        const int half = 48000;
        r.process(pc.data(), L.data(), R.data(), half, s);
        s.on = false;
        r.process(pc.data() + half, L.data() + half, R.data() + half, 1440, s); // 30 ms
        double after = 0;
        std::vector<double> L2(pc.size() - (size_t) half - 1440, 0.0), R2 = L2;
        r.process(pc.data() + half + 1440, L2.data(), R2.data(), (int) L2.size(), s);
        for (double v : L2) after = std::max(after, std::abs(v));
        check(after == 0.0 && !r.isActive(), "FADE: switched off -> silent after 30 ms, module idle");
    }

    // 10. stability: full-scale noise and bursts, extreme settings, every mode
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        std::vector<double> x((size_t) (6.0 * sr));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = ((i / 24000) % 2 == 0) ? 0.99 * u(rng) : (i % 3000 < 10 ? 0.99 : 0.0);
        bool ok = true;
        double worst = 0;
        for (int m = 0; m < 3; ++m)
            for (double g : { 0.0, 1.0 })
            {
                RootSettings s;
                s.on = true;
                s.mode = (RootMode) m;
                s.growl = g;
                s.levelDb = 12.0;
                s.subDb = s.lowDb = s.lowMidDb = 12.0;
                s.freqHz = 250.0;
                s.tightHz = 20.0;
                s.comp = 0.0;
                const auto y = layerOf(x, s, (unsigned) (m * 2 + 1));
                for (double v : y)
                {
                    if (!std::isfinite(v)) ok = false;
                    worst = std::max(worst, std::abs(v));
                }
            }
        check(ok && worst < 200.0, "STABLE: noise / clicks at 0.99 FS, ROOT +12, BASS EQ +12, all modes -> finite, peak %.1f", worst);
    }

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}

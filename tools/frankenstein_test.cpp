// frankenstein_test: objective tests of the frequency-split blending ("Create Frankenstein").
// The network is linear, so impulse responses give exact transfer functions.  Exit 0 = all pass.

#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>

#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/Frankenstein.h"

using namespace ampsurd;
static int failures = 0;

static void check(bool ok, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0)
{
    std::printf("[%s] ", ok ? "PASS" : "FAIL");
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    failures += ok ? 0 : 1;
}

static const double sr = 48000.0;
static const int N = 65536;

// magnitude response (dB) of the path from slot `src` to the output, for a given layout
static std::vector<std::complex<double>> response(const FrankensteinLayout& L, int src, std::array<bool, 5> feed = {})
{
    FrankensteinMixer m;
    m.prepare(sr, N);
    m.setTarget(L);
    m.snapToTarget();
    std::vector<double> imp((size_t) N, 0.0), zero((size_t) N, 0.0), out((size_t) N, 0.0);
    imp[0] = 1.0;
    std::array<const double*, 5> amps {};
    for (int s = 0; s < 5; ++s)
        amps[(size_t) s] = (s == src || feed[(size_t) s]) ? imp.data() : zero.data();
    m.process(amps, { 1, 1, 1, 1, 1 }, out.data(), N);
    std::vector<std::complex<double>> X((size_t) N);
    for (int i = 0; i < N; ++i) X[(size_t) i] = out[(size_t) i];
    CaptureAnalyzer::fft(X, false);
    return X;
}

static double dbAt(const std::vector<std::complex<double>>& X, double f)
{
    const int k = (int) std::lround(f / sr * N);
    return 20.0 * std::log10(std::abs(X[(size_t) k]) + 1e-30);
}

int main()
{
    const std::array<bool, 5> all { true, true, true, true, true };

    // 1. Perfect reconstruction: every section plays the same amp -> flat magnitude
    {
        double worst = 0;
        for (float width : { 0.0f, 0.3f, 0.9f })
            for (int sections : { 2, 3, 5 })
            {
                FrankensteinSettings s;
                s.enabled = true;
                s.sections = sections;
                s.width = width;
                s.amp = { 2, 2, 2, 2, 2 };
                const auto L = computeFrankensteinLayout(s, all);
                const auto X = response(L, 2);
                for (double f = 20; f < 20000; f *= 1.05)
                    worst = std::max(worst, std::abs(dbAt(X, f)));
            }
        check(worst < 0.01, "RECONSTRUCTION: same amp in every section -> its sound is unchanged (flat within %.4f dB, 20 Hz-20 kHz)", worst);
    }

    // 2. Different amps in different sections, but all fed the same signal -> still flat
    {
        double worst = 0;
        for (float width : { 0.0f, 0.5f, 0.9f })
        {
            FrankensteinSettings s;
            s.sections = 4;
            s.width = width;
            s.amp = { 0, 1, 2, 3, 0 };
            const auto L = computeFrankensteinLayout(s, all);
            const auto X = response(L, 0, { true, true, true, true, false });
            for (double f = 20; f < 20000; f *= 1.05) worst = std::max(worst, std::abs(dbAt(X, f)));
        }
        check(worst < 0.01, "COMPLEMENTARY: the sections always add up to the full spectrum (flat within %.4f dB)", worst);
    }

    // 3. Hand-over at 1 kHz, WIDTH 0: amp A below, amp B above
    {
        FrankensteinSettings s;
        s.sections = 2;
        s.width = 0.0f;
        s.amp = { 0, 1 };
        s.dividerHz[0] = 1000.0f;
        const auto L = computeFrankensteinLayout(s, all);
        const auto A = response(L, 0), Bx = response(L, 1);
        const double aLow = dbAt(A, 250), aHigh = dbAt(A, 4000), bLow = dbAt(Bx, 250), bHigh = dbAt(Bx, 4000);
        const double aCross = dbAt(A, 1000), bCross = dbAt(Bx, 1000);
        check(aLow > -0.1 && bHigh > -0.1 && aHigh < -40 && bLow < -40,
              "SPLIT: divider at 1 kHz: amp A %.1f dB at 250 Hz / %.1f dB at 4 kHz; amp B %.1f dB at 4 kHz", aLow, aHigh, bHigh);
        check(std::abs(aCross + 6.0) < 1.0 && std::abs(bCross + 6.0) < 1.0,
              "SPLIT: at the divider both amps play at half level (A %.1f dB, B %.1f dB)", aCross, bCross);
    }

    // 4. WIDTH makes the hand-over gradual
    {
        auto slopeAt = [&](float width) {
            FrankensteinSettings s;
            s.sections = 2;
            s.width = width;
            s.amp = { 0, 1 };
            s.dividerHz[0] = 1000.0f;
            const auto A = response(computeFrankensteinLayout(s, all), 0);
            return std::array<double, 2> { dbAt(A, 1000 * std::pow(2.0, 1.0)), dbAt(A, 1000 * std::pow(2.0, 2.0)) };
        };
        const auto narrow = slopeAt(0.0f), wide = slopeAt(0.9f);
        check(wide[0] > narrow[0] + 10 && wide[1] > narrow[1] + 10,
              "WIDTH: 90 %% keeps amp A audible above the divider (+1 oct: %.1f dB vs %.1f dB at width 0; +2 oct: %.1f vs %.1f dB)",
              wide[0], narrow[0], wide[1], narrow[1]);
    }

    // 5. Muting the middle amp: neighbours meet in the middle of its section
    {
        FrankensteinSettings s;
        s.sections = 3;
        s.width = 0.0f;
        s.amp = { 0, 1, 2 };
        s.dividerHz[0] = 200.0f;
        s.dividerHz[1] = 2000.0f;
        std::array<bool, 5> muted = all;
        muted[1] = false;
        const auto L = computeFrankensteinLayout(s, muted);
        const double meet = std::sqrt(200.0 * 2000.0);
        const auto A = response(L, 0), Cx = response(L, 2);
        check(L.numVisible == 2 && std::abs(L.visibleDividersHz[0] - meet) < 1.0 && L.dividerIsMerged[0]
                  && std::abs(dbAt(A, meet) + 6.0) < 1.0 && std::abs(dbAt(Cx, meet) + 6.0) < 1.0,
              "MUTE: middle amp muted -> sections 1 and 3 meet at %.0f Hz (geometric middle), both -6 dB there", L.visibleDividersHz[0]);

        std::array<bool, 5> solo {};
        solo[2] = true;
        const auto Ls = computeFrankensteinLayout(s, solo);
        double worst = 0;
        const auto S = response(Ls, 2);
        for (double f = 20; f < 20000; f *= 1.1) worst = std::max(worst, std::abs(dbAt(S, f)));
        check(Ls.numVisible == 1 && worst < 0.01, "SOLO: soloed amp plays over the whole spectrum (flat within %.4f dB)", worst);
    }

    // 6. Moving everything while playing: smooth and bounded
    {
        FrankensteinMixer m;
        m.prepare(sr, 256);
        std::mt19937 rng(1);
        std::normal_distribution<double> nd(0.0, 0.2);
        std::vector<std::vector<double>> amp(5, std::vector<double>(256));
        std::vector<double> out(256);
        double peak = 0, inPeak = 0;
        bool finite = true;
        for (int blk = 0; blk < 2000; ++blk)
        {
            FrankensteinSettings s;
            s.sections = 2 + (blk / 200) % 4;
            s.width = (float) ((blk % 97) / 100.0 * 0.9);
            s.amp = { blk % 5, (blk / 3) % 5, (blk / 7) % 5, 3, 4 };
            s.dividerHz = { (float) (100 + 50 * (blk % 40)), 2500.0f, 5000.0f, 9000.0f };
            std::array<bool, 5> aud = all;
            aud[(size_t) ((blk / 50) % 5)] = (blk % 3) != 0;
            m.setTarget(computeFrankensteinLayout(s, aud));
            std::array<const double*, 5> ptr {};
            for (int a = 0; a < 5; ++a)
            {
                for (auto& v : amp[(size_t) a]) { v = nd(rng); inPeak = std::max(inPeak, std::abs(v)); }
                ptr[(size_t) a] = amp[(size_t) a].data();
            }
            m.process(ptr, { 1, 1, 1, 1, 1 }, out.data(), 256);
            for (double v : out) { finite = finite && std::isfinite(v); peak = std::max(peak, std::abs(v)); }
        }
        check(finite && peak < 3.0 * inPeak,
              "GLIDES: 2000 blocks of random divider / width / section / mute changes: no invalid samples, peak %.2f x input peak", peak / inPeak);
    }

    // 7. NOTES mode: the guitar is split BEFORE the amps
    {
        auto splitResponse = [&](const FrankensteinLayout& L, int slot) {
            FrankensteinMixer m;
            m.prepare(sr, N);
            m.setTarget(L);
            m.snapToTarget();
            std::vector<double> imp((size_t) N, 0.0);
            imp[0] = 1.0;
            std::array<std::vector<double>, 5> outs;
            std::array<double*, 5> ptr {};
            for (int s2 = 0; s2 < 5; ++s2) { outs[(size_t) s2].assign((size_t) N, 0.0); ptr[(size_t) s2] = outs[(size_t) s2].data(); }
            m.processSplit(imp.data(), ptr, N);
            std::vector<std::complex<double>> X((size_t) N);
            for (int i = 0; i < N; ++i)
            {
                double v = 0;
                if (slot < 0) for (int s2 = 0; s2 < 5; ++s2) v += outs[(size_t) s2][(size_t) i];
                else v = outs[(size_t) slot][(size_t) i];
                X[(size_t) i] = v;
            }
            CaptureAnalyzer::fft(X, false);
            return X;
        };
        FrankensteinSettings s;
        s.sections = 3;
        s.width = 0.3f;
        s.amp = { 0, 1, 2, 0, 0 };
        s.dividerHz[0] = 250.0f;
        s.dividerHz[1] = 800.0f;
        s.beforeAmps = true;
        const auto L = computeFrankensteinLayout(s, all);
        const auto sum = splitResponse(L, -1);
        double worst = 0;
        for (double f = 20; f < 20000; f *= 1.05) worst = std::max(worst, std::abs(dbAt(sum, f)));
        check(worst < 0.01, "NOTES: the inputs of all amps add up to the unchanged guitar (flat within %.4f dB)", worst);
        const auto a = splitResponse(L, 0), b = splitResponse(L, 1), c = splitResponse(L, 2);
        // low E (82 Hz) -> amp 1, A4 (440 Hz) -> amp 2, high E (1319 Hz, 21st fret) -> amp 3
        check(dbAt(a, 82) > -0.5 && dbAt(b, 82) < -30 && dbAt(b, 440) > -3.0 && dbAt(a, 440) < -18 && dbAt(c, 1319) > -3.0 && dbAt(b, 1319) < -12,
              "NOTES: low E goes to amp 1 (%.1f dB), A4 to amp 2 (%.1f dB), high E (1319 Hz) to amp 3 (%.1f dB)", dbAt(a, 82), dbAt(b, 440), dbAt(c, 1319));
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

// mix_experiment: which mix law keeps AMPSURD's loudness predictable?
//
// Renders the guitar-like test signal through 2-5 captures, auto-aligns them, then mixes
// them with many fader settings under four candidate laws and measures the perceived
// loudness (ITU-R BS.1770 K-weighted) of every mix against the target:
//     target = percentage-weighted average loudness of the individual captures
// i.e. "70 % of amp A + 30 % of amp B should be about as loud as A and B are on their own".
//
// Usage: mix_experiment <a.nam> <b.nam> [c.nam] [d.nam] [e.nam]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Engine.h"
#include "ampsurd/PathAligner.h"

using namespace ampsurd;

static double loudnessPower(std::vector<double> y, double sr)
{
    CaptureAnalyzer::kWeightInPlace(y, sr);
    double p = 0;
    const size_t from = (size_t) (0.05 * sr);
    for (size_t i = from; i < y.size(); ++i) p += y[i] * y[i];
    return p / (double) (y.size() - from);
}

int main(int argc, char** argv)
{
    const int N = argc - 1;
    if (N < 2 || N > kNumSlots)
    {
        std::fprintf(stderr, "Usage: mix_experiment <2..5 .nam files>\n");
        return 2;
    }
    const double sr = 48000.0;
    const auto test = CaptureAnalyzer::makeTestSignal(sr);

    // Render + measure
    std::vector<std::vector<double>> y(N);
    std::vector<double> metaLoud;
    std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> meas {};
    for (int i = 0; i < N; ++i)
    {
        auto r = CaptureModel::load(argv[i + 1], sr, 256);
        if (!r.model) { std::fprintf(stderr, "load failed %s: %s\n", argv[i + 1], r.error.c_str()); return 2; }
        y[(size_t) i] = CaptureAnalyzer::render(*r.model, test, false);
        {
            // compare: level match from the file's loudness metadata (old) vs AMPSURD's measurement
            const auto viaMeta = CaptureAnalyzer::render(*CaptureModel::load(argv[i + 1], sr, 256).model, test, true);
            metaLoud.push_back(10 * std::log10(loudnessPower(viaMeta, sr)));
        }
        const double lg = CaptureAnalyzer::levelMatchGain(CaptureAnalyzer::loudnessDb(*CaptureAnalyzer::measure(y[(size_t) i], sr)));
        for (double& v : y[(size_t) i]) v *= lg; // AMPSURD level match
        meas[(size_t) i] = CaptureAnalyzer::measure(y[(size_t) i], sr);
        std::printf("capture %d: %-30s %s\n", i + 1, r.model->getInfo().displayName.c_str(), r.model->getInfo().architectureHint.c_str());
    }

    // Auto alignment against capture 1 (what the plugin does)
    std::array<double, kNumSlots> lag {}, pol { 1, 1, 1, 1, 1 }, rot {}, delay {};
    std::printf("\nauto alignment vs capture 1 (low-band correlation before -> after):\n");
    for (int i = 1; i < N; ++i)
    {
        const auto a = CaptureAnalyzer::estimateAlignment(*meas[0], *meas[(size_t) i]);
        lag[(size_t) i] = a.lagSamples;
        pol[(size_t) i] = a.polarity;
        std::printf("  capture %d: offset %+6.2f samples, polarity %+.0f, correlation %+.2f -> %+.2f %s\n", i + 1,
                    a.lagSamples, a.polarity, a.correlationBefore, a.correlationAfter, a.reliable ? "" : "(left unaligned)");
    }
    const double maxLag = *std::max_element(lag.begin(), lag.begin() + N);
    for (int i = 0; i < N; ++i) delay[(size_t) i] = maxLag - lag[(size_t) i];

    // Apply alignment exactly as the engine does
    for (int i = 0; i < N; ++i)
    {
        PathAligner al;
        al.prepare(sr, 1000);
        al.setTargets(delay[(size_t) i], pol[(size_t) i], 0, 0);
        al.snapToTargets();
        al.process(y[(size_t) i].data(), (int) y[(size_t) i].size());
    }

    std::array<std::array<CaptureAnalyzer::Spectrum, kNumSlots>, kNumSlots> cross;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            cross[(size_t) i][(size_t) j] = CaptureAnalyzer::crossSpectrum(*meas[(size_t) i], *meas[(size_t) j]);
    const auto kw = CaptureAnalyzer::loudnessWeights(sr);
    const auto covPow = CaptureAnalyzer::covariance(meas, cross, delay, pol, rot);
    const auto covK = CaptureAnalyzer::covariance(meas, cross, delay, pol, rot, &kw);

    std::vector<double> L(N);
    double peakMax = 0;
    for (int i = 0; i < N; ++i)
    {
        L[(size_t) i] = loudnessPower(y[(size_t) i], sr);
        for (double v : y[(size_t) i]) peakMax = std::max(peakMax, std::abs(v));
    }
    std::printf("\nindividual loudness, level match from file metadata: ");
    for (double v : metaLoud) std::printf("%+.1f ", v);
    std::printf("dB\nindividual loudness, AMPSURD measured level match: ");
    for (int i = 0; i < N; ++i) std::printf("%+.1f ", 10 * std::log10(L[(size_t) i]));
    std::printf("dB (K-weighted)\n\n");

    // Scenarios: every subset with equal faders + random fader settings
    std::vector<std::array<double, kNumSlots>> mixes;
    for (int mask = 1; mask < (1 << N); ++mask)
    {
        std::array<double, kNumSlots> w {};
        for (int i = 0; i < N; ++i) if (mask & (1 << i)) w[(size_t) i] = 1;
        mixes.push_back(w);
    }
    std::mt19937 rng(7);
    std::exponential_distribution<double> ex(1.0);
    for (int k = 0; k < 300; ++k)
    {
        std::array<double, kNumSlots> w {};
        for (int i = 0; i < N; ++i) w[(size_t) i] = (rng() % 4 == 0) ? 0.0 : ex(rng);
        mixes.push_back(w);
    }

    const char* names[4] = { "A  linear  (gains = percentages)", "B  constant power (p / sqrt(sum p^2))",
                             "C  AMPSURD, raw-power covariance", "D  AMPSURD, K-weighted covariance" };
    std::array<double, kNumSlots> validTrue {};
    std::array<bool, kNumSlots> valid {};
    for (int i = 0; i < N; ++i) valid[(size_t) i] = true;
    (void) validTrue;

    std::printf("%-40s %10s %10s %10s %12s\n", "law", "mean |err|", "max +err", "max -err", "peak vs max");
    for (int law = 0; law < 4; ++law)
    {
        double sumAbs = 0, maxPos = -1e9, maxNeg = 1e9, peakRatio = 0;
        int count = 0;
        for (const auto& w : mixes)
        {
            double s = 0;
            for (int i = 0; i < N; ++i) s += w[(size_t) i];
            if (s <= 0) continue;
            std::array<double, kNumSlots> p {};
            double sp2 = 0, target = 0;
            for (int i = 0; i < N; ++i) { p[(size_t) i] = w[(size_t) i] / s; sp2 += p[(size_t) i] * p[(size_t) i]; target += p[(size_t) i] * L[(size_t) i]; }

            double G = 1.0;
            if (law == 1) G = 1.0 / std::sqrt(sp2);
            if (law == 2) G = Engine::computeCompensation(p, covPow.C, valid);
            if (law == 3) G = Engine::computeCompensation(p, covK.C, valid);

            std::vector<double> mix(test.size(), 0.0);
            for (int i = 0; i < N; ++i)
                if (p[(size_t) i] > 0)
                    for (size_t k = 0; k < mix.size(); ++k) mix[k] += G * p[(size_t) i] * y[(size_t) i][k];
            double pk = 0;
            for (double v : mix) pk = std::max(pk, std::abs(v));
            const double err = 10 * std::log10(loudnessPower(mix, sr) / target);
            sumAbs += std::abs(err); maxPos = std::max(maxPos, err); maxNeg = std::min(maxNeg, err);
            peakRatio = std::max(peakRatio, pk / peakMax);
            ++count;
        }
        std::printf("%-40s %8.2f dB %+8.2f dB %+8.2f dB %+10.2f dB\n", names[law], sumAbs / count, maxPos, maxNeg,
                    20 * std::log10(peakRatio));
    }
    std::printf("\n(%zu mixes: every combination with equal faders + 300 random fader settings)\n", mixes.size());
    return 0;
}

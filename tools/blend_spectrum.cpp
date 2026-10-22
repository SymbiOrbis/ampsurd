// blend_spectrum: does a 50:50 blend keep the tone of the two captures, band by band?
//
// Renders the guitar-like test signal through two captures exactly as AMPSURD does (level match,
// auto alignment, loudness compensation G), then compares the blend's power in octave bands with the
// target: the average of the two captures' powers (what "half of A and half of B" should sound like).
// A negative difference in the high bands = the blend sounds duller than either capture alone.
// "+tone" = the same blend with AMPSURD's blend tone EQ (BlendToneEq, since 2026-10-22).
//
// Usage: blend_spectrum <a.nam> <b.nam> [mix of A in %, default 50]

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ampsurd/BlendToneEq.h"
#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Engine.h"
#include "ampsurd/PathAligner.h"

using namespace ampsurd;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// power of y in a one-octave band around fc (RBJ band-pass, constant 0 dB peak, applied twice)
double bandPower(const std::vector<double>& y, double fc, double sr)
{
    const double w = 2 * kPi * fc / sr, q = 1.414, al = std::sin(w) / (2 * q), a0 = 1 + al;
    const double b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos(w) / a0, a2 = (1 - al) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0, p = 0;
    const size_t from = (size_t) (0.1 * sr);
    for (size_t i = 0; i < y.size(); ++i)
    {
        const double x = y[i];
        const double o = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = o;
        const double o2 = b0 * o + b2 * u2 - a1 * v1 - a2 * v2;
        u2 = u1; u1 = o; v2 = v1; v1 = o2;
        if (i >= from) p += o2 * o2;
    }
    return p / (double) (y.size() - from);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "Usage: blend_spectrum <a.nam> <b.nam> [mix of A in %%]\n"); return 2; }
    const double pa = argc > 3 ? std::atof(argv[3]) / 100.0 : 0.5;
    const double sr = 48000.0;
    const auto test = CaptureAnalyzer::makeTestSignal(sr);
    std::array<std::vector<double>, 2> y;
    std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> meas {};
    for (int i = 0; i < 2; ++i)
    {
        auto r = CaptureModel::load(argv[i + 1], sr, 256);
        if (!r.model) { std::fprintf(stderr, "load failed: %s\n", r.error.c_str()); return 2; }
        y[(size_t) i] = CaptureAnalyzer::render(*r.model, test, false);
        const double lg = CaptureAnalyzer::levelMatchGain(CaptureAnalyzer::loudnessDb(*CaptureAnalyzer::measure(y[(size_t) i], sr)));
        for (double& v : y[(size_t) i]) v *= lg;
        meas[(size_t) i] = CaptureAnalyzer::measure(y[(size_t) i], sr);
    }
    const auto a = CaptureAnalyzer::estimateAlignment(*meas[0], *meas[1]);
    std::array<double, kNumSlots> delay {}, pol { 1, 1, 1, 1, 1 }, rot {};
    pol[1] = a.polarity;
    delay[0] = std::max(0.0, a.lagSamples);
    delay[1] = std::max(0.0, -a.lagSamples);
    std::printf("alignment: offset %+.2f samples (%.3f ms), polarity %+.0f, low-band correlation %+.2f -> %+.2f\n", a.lagSamples,
                a.lagSamples * 1000.0 / sr, a.polarity, a.correlationBefore, a.correlationAfter);
    for (int i = 0; i < 2; ++i)
    {
        PathAligner al;
        al.prepare(sr, 1000);
        al.setTargets(delay[(size_t) i], pol[(size_t) i], 0, 0);
        al.snapToTargets();
        al.process(y[(size_t) i].data(), (int) y[(size_t) i].size());
    }
    std::array<std::array<CaptureAnalyzer::Spectrum, kNumSlots>, kNumSlots> cross;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            cross[(size_t) i][(size_t) j] = CaptureAnalyzer::crossSpectrum(*meas[(size_t) i], *meas[(size_t) j]);
    const auto kw = CaptureAnalyzer::loudnessWeights(sr);
    const auto C = CaptureAnalyzer::covariance(meas, cross, delay, pol, rot, &kw);
    std::array<double, kNumSlots> p {};
    p[0] = pa;
    p[1] = 1.0 - pa;
    std::array<bool, kNumSlots> valid {};
    valid[0] = valid[1] = true;
    const double G = Engine::computeCompensation(p, C.C, valid);
    std::vector<double> blend(y[0].size());
    for (size_t i = 0; i < blend.size(); ++i) blend[i] = G * (p[0] * y[0][i] + p[1] * y[1][i]);
    std::printf("mix %.0f : %.0f, loudness compensation G = %+.2f dB\n", pa * 100, (1 - pa) * 100, 20 * std::log10(G));
    std::array<std::array<double, 2>, kNumSlots> centred {};
    for (auto& c : centred) c = { 1.0, 1.0 };
    const auto tone = Engine::computeToneCorrection(p, C.band, valid, centred, G);
    std::printf("blend tone EQ (dB per band):");
    for (double v : tone) std::printf(" %+.1f", v);
    std::printf("\n\n");
    std::vector<double> toned = blend;
    {
        BlendToneEq eq;
        eq.prepare(sr);
        eq.setTargets(tone);
        std::vector<double> warm((size_t) sr, 0.0);
        eq.process(warm.data(), nullptr, (int) warm.size()); // let the smoothed gains arrive
        eq.process(toned.data(), nullptr, (int) toned.size());
    }
    std::printf("%8s %8s %8s %10s %14s %12s %10s\n", "band Hz", "A dB", "B dB", "blend dB", "blend - target", "coherence", "+tone");
    for (double fc : { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
    {
        const double PA = bandPower(y[0], fc, sr), PB = bandPower(y[1], fc, sr), PM = bandPower(blend, fc, sr),
                     PT = bandPower(toned, fc, sr);
        const double target = p[0] * PA + p[1] * PB;
        // how coherently A and B add in this band: 1 = in phase, 0 = uncorrelated, -1 = cancel
        const double cross2 = (PM / (G * G) - p[0] * p[0] * PA - p[1] * p[1] * PB) / (2 * p[0] * p[1] * std::sqrt(PA * PB) + 1e-30);
        std::printf("%8.0f %+8.1f %+8.1f %+10.1f %+14.1f %+12.2f %+10.1f\n", fc, 10 * std::log10(PA), 10 * std::log10(PB), 10 * std::log10(PM),
                    10 * std::log10(PM / target), cross2, 10 * std::log10(PT / target));
    }
    return 0;
}

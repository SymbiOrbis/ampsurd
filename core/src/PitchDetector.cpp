#include "ampsurd/PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{
namespace
{
constexpr int kRingSize = 16384; // power of two
constexpr int kAnalysisLen = 4096; // at the host rate; decimated by 2 for analysis
}

std::string PitchDetector::Result::noteName() const
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((midiNote % 12) + 12) % 12];
}

int PitchDetector::Result::octave() const { return midiNote / 12 - 1; }

void PitchDetector::prepare(double sampleRate)
{
    sr = sampleRate;
    ring.assign((size_t) kRingSize, 0.0f);
    writePos.store(0);
    frame.assign((size_t) kAnalysisLen / 2, 0.0);
    diff.assign((size_t) kAnalysisLen / 4 + 2, 0.0);
}

void PitchDetector::push(const float* x, int n) noexcept
{
    if (ring.empty()) return;
    int w = writePos.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) w] = x[i];
        w = (w + 1) & (kRingSize - 1);
    }
    writePos.store(w, std::memory_order_release);
}

PitchDetector::Result PitchDetector::analyse(double a4)
{
    Result r;
    if (ring.empty()) return r;

    // latest kAnalysisLen samples, decimated by 2 (pairwise mean = gentle anti-alias)
    const int end = writePos.load(std::memory_order_acquire);
    const int half = kAnalysisLen / 2;
    double sumSq = 0.0, mean = 0.0;
    for (int i = 0; i < half; ++i)
    {
        const int a = (end - kAnalysisLen + 2 * i + kRingSize) & (kRingSize - 1);
        const int b = (a + 1) & (kRingSize - 1);
        frame[(size_t) i] = 0.5 * ((double) ring[(size_t) a] + (double) ring[(size_t) b]);
        mean += frame[(size_t) i];
    }
    mean /= half;
    for (auto& v : frame) { v -= mean; sumSq += v * v; }
    const double rms = std::sqrt(sumSq / half);
    r.levelDb = rms > 1e-9 ? 20.0 * std::log10(rms) : -120.0;
    if (r.levelDb < -60.0)
        return r; // nothing played

    const double fs = sr / 2.0;
    const int W = half / 2;                       // integration window
    const int tauMax = std::min(W, (int) (fs / 25.0));
    const int tauMin = std::max(2, (int) (fs / 1500.0));

    // YIN difference function + cumulative mean normalisation
    diff[0] = 1.0;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < W; ++j)
        {
            const double e = frame[(size_t) j] - frame[(size_t) (j + tau)];
            d += e * e;
        }
        running += d;
        diff[(size_t) tau] = running > 0.0 ? d * tau / running : 1.0;
    }

    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (diff[(size_t) tau] < 0.12)
        {
            while (tau + 1 < tauMax && diff[(size_t) (tau + 1)] < diff[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        // no clear period: accept the global minimum only if it is reasonably clear
        double m = 1.0;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (diff[(size_t) tau] < m) { m = diff[(size_t) tau]; best = tau; }
        if (m > 0.25) return r;
    }

    // parabolic interpolation
    double t = best;
    if (best > 1 && best + 1 <= tauMax)
    {
        const double y0 = diff[(size_t) (best - 1)], y1 = diff[(size_t) best], y2 = diff[(size_t) (best + 1)];
        const double den = y0 - 2.0 * y1 + y2;
        if (std::abs(den) > 1e-12)
            t += std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5);
    }

    r.frequencyHz = fs / t;

    // High notes have short periods (few samples at the decimated rate): refine at the full rate.
    if (best < 120)
    {
        std::vector<double> full((size_t) kAnalysisLen);
        for (int i = 0; i < kAnalysisLen; ++i)
            full[(size_t) i] = ring[(size_t) ((end - kAnalysisLen + i + kRingSize) & (kRingSize - 1))];
        const int centre = (int) std::lround(2.0 * t);
        const int Wf = 2048;
        auto dfun = [&](int tau) {
            double d = 0.0;
            for (int j = 0; j < Wf; ++j)
            {
                const double e = full[(size_t) j] - full[(size_t) (j + tau)];
                d += e * e;
            }
            return d;
        };
        int bt = centre;
        double bd = dfun(centre);
        for (int tau = std::max(2, centre - 3); tau <= centre + 3; ++tau)
        {
            const double d = dfun(tau);
            if (d < bd) { bd = d; bt = tau; }
        }
        const double y0 = dfun(bt - 1), y1 = bd, y2 = dfun(bt + 1);
        const double den = y0 - 2.0 * y1 + y2;
        const double tf = bt + (std::abs(den) > 1e-12 ? std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0);
        r.frequencyHz = sr / tf;
    }
    const double semis = 12.0 * std::log2(r.frequencyHz / a4) + 69.0;
    r.midiNote = (int) std::lround(semis);
    r.cents = (semis - r.midiNote) * 100.0;
    r.valid = true;
    return r;
}

} // namespace ampsurd

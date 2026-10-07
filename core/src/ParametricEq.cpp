#include "ampsurd/ParametricEq.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kStep = 16; // samples between coefficient updates

template <typename T>
T clampT(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }
} // namespace

std::array<EqBand, ParametricEq::kNumBands> ParametricEq::defaultBands()
{
    static constexpr float f[kNumBands] = { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f,
                                            1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    std::array<EqBand, kNumBands> b {};
    for (int i = 0; i < kNumBands; ++i)
        b[(size_t) i] = { f[i], 0.0f, 1.0f };
    return b;
}

void ParametricEq::prepare(double sampleRate)
{
    fs = sampleRate;
    // ~30 ms smoothing time constant, applied once per kStep samples
    smoothCoef = 1.0 - std::exp(-(double) kStep / (0.030 * fs));
    const auto d = defaultBands();
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& s = bands[(size_t) i];
        s.logF = s.tLogF = std::log(d[(size_t) i].freqHz);
        s.gain = s.tGain = 0.0;
        s.logQ = s.tLogQ = 0.0;
    }
    reset();
}

void ParametricEq::reset() noexcept
{
    for (auto& s : bands)
    {
        s.ic1 = s.ic2 = 0.0;
        s.logF = s.tLogF;
        s.gain = s.tGain;
        s.logQ = s.tLogQ;
        updateCoefficients(s);
    }
}

void ParametricEq::setBands(const std::array<EqBand, kNumBands>& t) noexcept
{
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& s = bands[(size_t) i];
        const auto& b = t[(size_t) i];
        s.tLogF = std::log((double) clampT(b.freqHz, kMinFreq, kMaxFreq));
        s.tGain = (double) clampT(b.gainDb, kMinGain, kMaxGain);
        s.tLogQ = std::log((double) clampT(b.q, kMinQ, kMaxQ));
    }
}

void ParametricEq::updateCoefficients(State& s) noexcept
{
    s.active = s.gain != 0.0;
    if (!s.active)
        return;
    const double fc = std::min(std::exp(s.logF), 0.45 * fs);
    const double q = std::exp(s.logQ);
    const double A = std::pow(10.0, s.gain / 40.0);
    const double g = std::tan(kPi * fc / fs);
    const double k = 1.0 / (q * A);
    s.a1 = 1.0 / (1.0 + g * (g + k));
    s.a2 = g * s.a1;
    s.a3 = g * s.a2;
    s.m1 = k * (A * A - 1.0);
}

void ParametricEq::process(double* x, int n) noexcept
{
    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min(kStep, n - start);
        double* p = x + start;

        for (auto& s : bands)
        {
            // advance smoothing; snap when close so that idle bands become exactly flat
            const bool moving = s.logF != s.tLogF || s.gain != s.tGain || s.logQ != s.tLogQ;
            if (moving)
            {
                s.logF += (s.tLogF - s.logF) * smoothCoef;
                s.gain += (s.tGain - s.gain) * smoothCoef;
                s.logQ += (s.tLogQ - s.logQ) * smoothCoef;
                if (std::abs(s.tLogF - s.logF) < 1e-5) s.logF = s.tLogF;
                if (std::abs(s.tGain - s.gain) < 1e-4) s.gain = s.tGain;
                if (std::abs(s.tLogQ - s.logQ) < 1e-5) s.logQ = s.tLogQ;
                updateCoefficients(s);
            }

            if (!s.active)
            {
                s.ic1 = s.ic2 = 0.0;
                continue;
            }

            const double a1 = s.a1, a2 = s.a2, a3 = s.a3, m1 = s.m1;
            double ic1 = s.ic1, ic2 = s.ic2;
            for (int i = 0; i < len; ++i)
            {
                const double v0 = p[i];
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                p[i] = v0 + m1 * v1;
            }
            // flush denormals
            s.ic1 = std::abs(ic1) < 1e-25 ? 0.0 : ic1;
            s.ic2 = std::abs(ic2) < 1e-25 ? 0.0 : ic2;
        }
    }
}

double ParametricEq::magnitudeDb(const std::array<EqBand, kNumBands>& bands, double freqHz, double sampleRate)
{
    double totalDb = 0.0;
    const double tw = std::tan(kPi * std::min(freqHz, 0.4999 * sampleRate) / sampleRate);
    for (const auto& b : bands)
    {
        if (b.gainDb == 0.0f)
            continue;
        const double fc = std::min((double) clampT(b.freqHz, kMinFreq, kMaxFreq), 0.45 * sampleRate);
        const double q = clampT(b.q, kMinQ, kMaxQ);
        const double A = std::pow(10.0, b.gainDb / 40.0);
        // bilinear transform maps exactly: analog bell at prewarped frequency ratio
        const double w = tw / std::tan(kPi * fc / sampleRate);
        const double re = 1.0 - w * w;
        const double numIm = w * A / q, denIm = w / (A * q);
        const double mag2 = (re * re + numIm * numIm) / (re * re + denIm * denIm);
        totalDb += 10.0 * std::log10(mag2);
    }
    return totalDb;
}

} // namespace ampsurd

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
    static constexpr float f[kNumBands] = { 20.0f, 63.0f, 125.0f, 250.0f, 500.0f,
                                            1000.0f, 2000.0f, 4000.0f, 8000.0f, 20000.0f };
    std::array<EqBand, kNumBands> b {};
    for (int i = 0; i < kNumBands; ++i)
        b[(size_t) i] = { f[i], 0.0f, bandType(i) == BandType::bell ? 1.0f : 0.707f };
    return b;
}

std::array<EqBand, ParametricEq::kNumBands> ParametricEq::neutralised(std::array<EqBand, kNumBands> b) noexcept
{
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& band = b[(size_t) i];
        band.gainDb = 0.0f;
        if (bandType(i) == BandType::lowCut) band.freqHz = kMinFreq;
        if (bandType(i) == BandType::highCut) band.freqHz = kMaxFreq;
    }
    return b;
}

bool ParametricEq::isFlat(const std::array<EqBand, kNumBands>& b) noexcept
{
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& band = b[(size_t) i];
        if (bandType(i) == BandType::bell ? band.gainDb != 0.0f : isCutActive(i, band.freqHz))
            return false;
    }
    return true;
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
        s.index = i;
        s.type = bandType(i);
        s.logF = s.tLogF = std::log(d[(size_t) i].freqHz);
        s.gain = s.tGain = 0.0;
        s.logQ = s.tLogQ = std::log(d[(size_t) i].q);
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
        s.tGain = s.type == BandType::bell ? (double) clampT(b.gainDb, kMinGain, kMaxGain) : 0.0;
        s.tLogQ = s.type == BandType::bell ? std::log((double) clampT(b.q, kMinQ, kMaxQ))
                                           : std::log((double) clampT(b.q, kMinCutQ, kMaxCutQ));
    }
}

void ParametricEq::updateCoefficients(State& s) noexcept
{
    const double f = std::exp(s.logF);
    s.active = s.type == BandType::bell ? s.gain != 0.0 : isCutActive(s.index, (float) f);
    if (!s.active)
        return;
    const double fc = std::min(f, 0.45 * fs);
    const double q = std::exp(s.logQ);
    const double g = std::tan(kPi * fc / fs);
    if (s.type == BandType::bell)
    {
        const double A = std::pow(10.0, s.gain / 40.0);
        s.k = 1.0 / (q * A);
        s.m1 = s.k * (A * A - 1.0);
    }
    else
    {
        s.k = 1.0 / q;
        s.m1 = 0.0;
    }
    s.a1 = 1.0 / (1.0 + g * (g + s.k));
    s.a2 = g * s.a1;
    s.a3 = g * s.a2;
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
                const bool wasActive = s.active;
                updateCoefficients(s);
                // A high cut leaving its 20 kHz end stop starts from the state it would have
                // settled into with the current input (output = input), not from silence - an
                // empty low-pass state would pull the output towards zero for a moment (a click).
                // (An empty low-cut state already passes the input unchanged.)
                if (s.active && !wasActive && s.type == BandType::highCut)
                {
                    s.ic1 = 0.0;
                    s.ic2 = p[0];
                }
            }

            if (!s.active)
            {
                s.ic1 = s.ic2 = 0.0;
                continue;
            }

            const double a1 = s.a1, a2 = s.a2, a3 = s.a3, m1 = s.m1, k = s.k;
            double ic1 = s.ic1, ic2 = s.ic2;
            for (int i = 0; i < len; ++i)
            {
                const double v0 = p[i];
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                switch (s.type)
                {
                    case BandType::bell:    p[i] = v0 + m1 * v1; break;
                    case BandType::lowCut:  p[i] = v0 - k * v1 - v2; break;
                    case BandType::highCut: p[i] = v2; break;
                }
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
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = bands[(size_t) i];
        const auto type = bandType(i);
        if (type == BandType::bell && b.gainDb == 0.0f) continue;
        if (type != BandType::bell && !isCutActive(i, b.freqHz)) continue;

        const double fc = std::min((double) clampT(b.freqHz, kMinFreq, kMaxFreq), 0.45 * sampleRate);
        // bilinear transform maps exactly: analog prototype at the prewarped frequency ratio
        const double w = tw / std::tan(kPi * fc / sampleRate);
        const double re = 1.0 - w * w;
        if (type == BandType::bell)
        {
            const double q = clampT(b.q, kMinQ, kMaxQ);
            const double A = std::pow(10.0, b.gainDb / 40.0);
            const double numIm = w * A / q, denIm = w / (A * q);
            totalDb += 10.0 * std::log10((re * re + numIm * numIm) / (re * re + denIm * denIm));
        }
        else
        {
            const double q = clampT(b.q, kMinCutQ, kMaxCutQ);
            const double den = re * re + (w / q) * (w / q);
            const double num = type == BandType::lowCut ? w * w * w * w : 1.0;
            totalDb += 10.0 * std::log10(std::max(1e-30, num / den));
        }
    }
    return totalDb;
}

} // namespace ampsurd

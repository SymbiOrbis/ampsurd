#include "ampsurd/PathAligner.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kPhases = 1024;
constexpr int kCentre = PathAligner::kBaseLatency - 1; // tap index of the centre for fraction 0

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}

// [phase][tap] windowed-sinc table, built once.
const std::vector<double>& sincTable()
{
    static const std::vector<double> table = [] {
        std::vector<double> t((size_t) (kPhases + 1) * PathAligner::kTaps);
        const double beta = 7.0, half = PathAligner::kTaps / 2.0, i0b = besselI0(beta);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = (double) p / kPhases;
            double sum = 0.0;
            for (int k = 0; k < PathAligner::kTaps; ++k)
            {
                const double x = (double) k - kCentre - frac;
                const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const double r = x / half;
                const double w = std::abs(r) >= 1.0 ? 0.0 : besselI0(beta * std::sqrt(1.0 - r * r)) / i0b;
                t[(size_t) p * PathAligner::kTaps + (size_t) k] = sinc * w;
                sum += sinc * w;
            }
            for (int k = 0; k < PathAligner::kTaps; ++k)
                t[(size_t) p * PathAligner::kTaps + (size_t) k] /= sum; // unity gain at DC
        }
        return t;
    }();
    return table;
}
} // namespace

// --- 90-degree all-pass pair (Olli Niemitalo). Each stage: y = a^2 (x + y[n-2]) - x[n-2]
double PathAligner::AllpassChain::process(double in) noexcept
{
    double v = in;
    for (size_t i = 0; i < 4; ++i)
    {
        const double y = c[i] * (v + y2[i]) - x2[i];
        x2[i] = x1[i]; x1[i] = v;
        y2[i] = y1[i]; y1[i] = y;
        v = y;
    }
    return v;
}

void PathAligner::AllpassChain::reset() noexcept
{
    x1.fill(0.0); x2.fill(0.0); y1.fill(0.0); y2.fill(0.0);
}

void PathAligner::prepare(double sampleRate, int maxDelaySamples)
{
    (void) sincTable();
    maxDelay = std::max(0, maxDelaySamples);
    int size = 1;
    while (size < maxDelay + kTaps + kBaseLatency + 4) size <<= 1;
    buffer.assign((size_t) size, 0.0);
    mask = size - 1;

    delayCoef = 1.0 - std::exp(-1.0 / (0.030 * sampleRate)); // 30 ms glide
    fastCoef = 1.0 - std::exp(-1.0 / (0.010 * sampleRate));  // 10 ms for polarity/phase/mix

    static constexpr double a[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    static constexpr double b[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
    for (size_t i = 0; i < 4; ++i)
    {
        chainA.c[i] = a[i] * a[i];
        chainB.c[i] = b[i] * b[i];
    }
    reset();
}

void PathAligner::reset() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0);
    writePos = 0;
    chainA.reset();
    chainB.reset();
    aDelayed = 0.0;
    snapToTargets();
}

void PathAligner::setTargets(double delaySamples, double polarity, double phaseRadians, double rotationMix) noexcept
{
    tDelay = std::clamp(delaySamples, 0.0, (double) maxDelay);
    tPol = polarity < 0.0 ? -1.0 : 1.0;
    tCos = std::cos(phaseRadians);
    tSin = std::sin(phaseRadians);
    tMix = std::clamp(rotationMix, 0.0, 1.0);
}

void PathAligner::snapToTargets() noexcept
{
    delay = tDelay; pol = tPol; cosT = tCos; sinT = tSin; mix = tMix;
}

void PathAligner::process(double* x, int n) noexcept
{
    const auto& table = sincTable();
    const bool network = mix > 0.0 || tMix > 0.0;

    for (int i = 0; i < n; ++i)
    {
        buffer[(size_t) writePos] = x[i];

        // --- fractional delay ---
        if (delay != tDelay)
        {
            delay += (tDelay - delay) * delayCoef;
            if (std::abs(tDelay - delay) < 1e-6) delay = tDelay;
        }
        const double total = delay + kBaseLatency - kCentre; // >= 1
        const double whole = std::floor(total);
        const int offset = (int) whole;
        const int phase = (int) std::lround((total - whole) * kPhases); // 0..kPhases
        const double* h = table.data() + (size_t) phase * kTaps;
        double y = 0.0;
        int idx = writePos - offset;
        for (int k = 0; k < kTaps; ++k)
            y += h[k] * buffer[(size_t) ((idx - k) & mask)];
        writePos = (writePos + 1) & mask;

        // --- polarity (smoothed so that a flip does not click) ---
        if (pol != tPol)
        {
            pol += (tPol - pol) * fastCoef;
            if (std::abs(tPol - pol) < 1e-6) pol = tPol;
        }
        y *= pol;

        // --- phase rotation ---
        if (network)
        {
            mix += (tMix - mix) * fastCoef;
            cosT += (tCos - cosT) * fastCoef;
            sinT += (tSin - sinT) * fastCoef;
            if (std::abs(tMix - mix) < 1e-6) mix = tMix;

            const double a = aDelayed;
            aDelayed = chainA.process(y);
            const double b = chainB.process(y);
            const double rotated = cosT * a + sinT * b;
            y = (1.0 - mix) * y + mix * rotated;

            if (mix == 0.0 && tMix == 0.0)
            {
                chainA.reset();
                chainB.reset();
                aDelayed = 0.0;
            }
        }
        else
        {
            cosT = tCos;
            sinT = tSin;
        }

        x[i] = y;
    }
}

} // namespace ampsurd

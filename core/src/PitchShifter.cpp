#include "ampsurd/PitchShifter.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
int pow2AtLeast(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}
} // namespace

void PitchShifter::prepare(double sampleRate)
{
    sr = sampleRate;
    const int size = pow2AtLeast((int) (0.1 * sr) + 8);
    buf.assign((size_t) size, 0.0);
    mask = size - 1;
    minPeriod = (int) std::lround(0.0025 * sr);   // 400 Hz and up: jump by a multiple of the period
    maxPeriod = (int) std::lround(0.0250 * sr);   // 40 Hz, and power chords: their common period is
                                                  // twice the root's (low E + B = 24 ms)
    defaultPeriod = (int) std::lround(0.0080 * sr);
    nominalLatency = (int) std::lround(kNominalLatencyMs * 0.001 * sr);
    decFactor = std::max(1, (int) std::lround(sr / 12000.0));
    const int decSize = pow2AtLeast(2 * maxPeriod / decFactor + 64);
    dec.assign((size_t) decSize, 0.0);
    decMask = decSize - 1;
    onStep = 1.0 / (0.010 * sr);
    reset();
}

void PitchShifter::reset() noexcept
{
    std::fill(buf.begin(), buf.end(), 0.0);
    std::fill(dec.begin(), dec.end(), 0.0);
    writePos = decPos = decCount = sinceEstimate = 0;
    decAcc = 0.0;
    period = defaultPeriod;
    delay = oldDelay = (double) nominalLatency;
    fading = false;
    fadePos = 0;
    onMix = wantOn ? 1.0 : 0.0;
}

void PitchShifter::setParameters(bool on, float semitones, float cents) noexcept
{
    wantOn = on;
    const double st = std::clamp((double) semitones, -12.0, 12.0) + std::clamp((double) cents, -100.0, 100.0) / 100.0;
    ratio = std::pow(2.0, st / 12.0);
}

double PitchShifter::read(double d) const noexcept
{
    // Catmull-Rom cubic between the samples around (writePos - d); needs d >= 2
    const double pos = (double) writePos - d;
    const double fl = std::floor(pos);
    const double t = pos - fl;
    const int i = (int) fl;
    const double ym1 = buf[(size_t) ((i - 1) & mask)], y0 = buf[(size_t) (i & mask)];
    const double y1 = buf[(size_t) ((i + 1) & mask)], y2 = buf[(size_t) ((i + 2) & mask)];
    const double c1 = 0.5 * (y1 - ym1);
    const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
    const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

void PitchShifter::estimatePeriod() noexcept
{
    // normalised autocorrelation of the last ~15 ms (decimated to ~12 kHz)
    const int minLag = std::max(2, minPeriod / decFactor), maxLag = maxPeriod / decFactor;
    const int window = maxLag;
    auto a = [this](int t) { return dec[(size_t) ((decPos - 1 - t) & decMask)]; };
    double e0 = 0.0;
    for (int t = 0; t < window; ++t) e0 += a(t) * a(t);
    if (e0 < 1e-10) { period = defaultPeriod; return; } // silence

    double best = -1.0;
    int bestLag = -1;
    // energy of the lagged window, updated incrementally
    double eL = 0.0;
    for (int t = 0; t < window; ++t) eL += a(t + minLag) * a(t + minLag);
    static constexpr int kMaxLags = 512;
    double corr[kMaxLags] {};
    for (int lag = minLag; lag <= maxLag && lag - minLag < kMaxLags; ++lag)
    {
        double c = 0.0;
        for (int t = 0; t < window; ++t) c += a(t) * a(t + lag);
        const double n = c / std::sqrt(e0 * eL + 1e-20);
        corr[lag - minLag] = n;
        if (n > best) { best = n; bestLag = lag; }
        // next lag: the window moves one decimated sample further into the past
        eL += a(window + lag) * a(window + lag) - a(lag) * a(lag);
        eL = std::max(eL, 0.0);
    }
    if (best < 0.35 || bestLag < 0) { period = defaultPeriod; return; } // noise: any length works
    // the shortest lag that is almost as good (prefers one period over two)
    for (int lag = minLag; lag <= bestLag; ++lag)
        if (corr[lag - minLag] >= 0.9 * best) { bestLag = lag; break; }
    // ... then up to the top of that correlation peak, with parabolic interpolation
    const int last = std::min(maxLag, minLag + kMaxLags - 1);
    while (bestLag < last && corr[bestLag + 1 - minLag] > corr[bestLag - minLag]) ++bestLag;
    double frac = 0.0;
    if (bestLag > minLag && bestLag < last)
    {
        const double a = corr[bestLag - 1 - minLag], b = corr[bestLag - minLag], c = corr[bestLag + 1 - minLag];
        const double den = a - 2.0 * b + c;
        if (den < 0.0) frac = std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }
    period = std::clamp((int) std::lround((bestLag + frac) * decFactor), minPeriod, maxPeriod);
}

double PitchShifter::refineJump(double fromDelay, int jump) const noexcept
{
    // the exact jump that makes the new read position match the old one best (backward segments)
    // compares one whole period (at most 15 ms) around the two positions; searches +-2 decimated steps
    const int seg = std::min(std::abs(jump), maxPeriod);
    const int from = (int) std::lround(fromDelay);
    double best = -2.0;
    int bestK = 0;
    const int range = std::min(2 * decFactor + 2, 39);
    double ncs[80] {};
    for (int k = -range; k <= range; ++k)
    {
        ncs[k + range] = -2.0;
        const int to = from + jump + k;
        if (to < 3 || to + seg >= mask) continue;
        double c = 0.0, ea = 0.0, eb = 0.0;
        for (int m = 0; m < seg; ++m)
        {
            const double va = buf[(size_t) ((writePos - from - m) & mask)];
            const double vb = buf[(size_t) ((writePos - to - m) & mask)];
            c += va * vb; ea += va * va; eb += vb * vb;
        }
        const double nc = c / std::sqrt(ea * eb + 1e-20);
        ncs[k + range] = nc;
        if (nc > best) { best = nc; bestK = k; }
    }
    // fractional part (parabola through the best match and its neighbours): periods are rarely whole samples
    double frac = 0.0;
    if (bestK > -range && bestK < range)
    {
        const double a = ncs[bestK - 1 + range], b = ncs[bestK + range], c = ncs[bestK + 1 + range];
        const double den = a - 2.0 * b + c;
        if (a > -2.0 && c > -2.0 && den < 0.0) frac = std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }
    return jump + bestK + frac;
}

void PitchShifter::process(double* x, int n) noexcept
{
    const bool idle = !wantOn && onMix == 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double in = x[i];
        buf[(size_t) writePos] = in;
        decAcc += in;
        if (++decCount == decFactor)
        {
            dec[(size_t) decPos] = decAcc / decFactor;
            decPos = (decPos + 1) & decMask;
            decAcc = 0.0;
            decCount = 0;
        }
        if (++sinceEstimate >= 256)
        {
            sinceEstimate = 0;
            if (!idle) estimatePeriod();
        }

        if (!idle)
        {
            if (onMix == 0.0 && wantOn)
            {
                delay = (double) nominalLatency; // switching on: start from the nominal delay
                fading = false;
            }
            delay += 1.0 - ratio;
            if (fading) oldDelay += 1.0 - ratio;

            if (!fading && ratio != 1.0)
            {
                const int xf = std::clamp(period, (int) (0.001 * sr), (int) (0.004 * sr));
                double jump = 0.0;
                if (ratio < 1.0)
                {
                    const double dMin = 0.0005 * sr + 3.0;
                    if (delay >= dMin + period) jump = -period; // reading too far behind: one period forward
                }
                else
                {
                    const double dMin = xf * (ratio - 1.0) + 3.0; // the old position must not overtake the input
                    if (delay <= dMin) jump = period;            // too close to the input: one period back
                }
                if (jump != 0.0)
                {
                    jump = refineJump(delay, (int) jump);
                    oldDelay = delay;
                    delay = std::clamp(delay + jump, 3.0, (double) (mask - 8));
                    fading = true;
                    fadePos = 0;
                    fadeLen = xf;
                }
            }

            double s = read(delay);
            if (fading)
            {
                const double g = 0.5 - 0.5 * std::cos(kPi * (fadePos + 0.5) / fadeLen);
                s = read(oldDelay) * (1.0 - g) + s * g;
                if (++fadePos >= fadeLen) fading = false;
            }

            const double target = wantOn ? 1.0 : 0.0;
            onMix = target > onMix ? std::min(target, onMix + onStep) : std::max(target, onMix - onStep);
            const double m = onMix * onMix * (3.0 - 2.0 * onMix);
            x[i] = in * (1.0 - m) + s * m;
        }
        writePos = (writePos + 1) & mask;
    }
}

} // namespace ampsurd

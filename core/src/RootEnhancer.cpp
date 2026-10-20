#include "ampsurd/RootEnhancer.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ampsurd
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kButterQ = 0.70710678118654752;
inline double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }
inline double onePole(double seconds, double sr) noexcept { return 1.0 - std::exp(-1.0 / (seconds * sr)); }
} // namespace

// ---------------------------------------------------------------------------------------------
// biquads (RBJ cookbook), transposed direct form II

void RootEnhancer::Biquad::setLowPass(double f, double q, double s) noexcept
{
    const double w = 2.0 * kPi * f / s, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
    b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;
    a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
}

void RootEnhancer::Biquad::setHighPass(double f, double q, double s) noexcept
{
    const double w = 2.0 * kPi * f / s, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
    b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
    a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
}

void RootEnhancer::Biquad::setLowShelf(double f, double gainDb, double s) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * kPi * f / s, c = std::cos(w), sn = std::sin(w);
    const double al = sn / 2.0 * std::sqrt(2.0), sq = 2.0 * std::sqrt(A) * al; // shelf slope S = 1
    const double a0 = (A + 1) + (A - 1) * c + sq;
    b0 = A * ((A + 1) - (A - 1) * c + sq) / a0;
    b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
    b2 = A * ((A + 1) - (A - 1) * c - sq) / a0;
    a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
    a2 = ((A + 1) + (A - 1) * c - sq) / a0;
}

void RootEnhancer::Biquad::setPeak(double f, double gainDb, double q, double s) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * kPi * f / s, c = std::cos(w), al = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + al / A;
    b0 = (1.0 + al * A) / a0; b1 = -2.0 * c / a0; b2 = (1.0 - al * A) / a0;
    a1 = -2.0 * c / a0; a2 = (1.0 - al / A) / a0;
}

double RootEnhancer::Biquad::magnitude(double hz, double s) const noexcept
{
    const std::complex<double> z1i = std::polar(1.0, -2.0 * kPi * hz / s), z2i = z1i * z1i;
    return std::abs((b0 + b1 * z1i + b2 * z2i) / (1.0 + a1 * z1i + a2 * z2i));
}

// ---------------------------------------------------------------------------------------------

double RootEnhancer::shape(double x) noexcept
{
    // positive: x / (1 + x) -> 1 (soft); negative: tanh(2.25 x) / 2.25 -> -0.44 (harder, earlier).
    // Both have slope 1 at 0 (continuous first derivative), the different curvature makes even harmonics.
    if (x >= 0.0) return x / (1.0 + x);
    return std::tanh(2.25 * x) / 2.25;
}

void RootEnhancer::prepare(double sampleRate, int)
{
    sr = sampleRate;
    dcCoef = std::exp(-2.0 * kPi * 8.0 / sr);
    powCoef = onePole(0.08, sr);
    envAtt = onePole(0.003, sr);
    envRel = onePole(0.060, sr);
    compAtt = onePole(0.002, sr);
    compRel = onePole(0.100, sr);
    fadeStep = 1.0 / (0.02 * sr);
    smooth = onePole(0.03, sr);
    configured = false;
    reset();
}

void RootEnhancer::reset() noexcept
{
    for (auto* b : { &lp1, &lp2, &satLp, &subLp1, &subLp2, &subPre, &tight, &eqSub, &eqLow, &eqLowMid })
        b->clear();
    dcX = dcY = 0.0;
    powIn = powSat = 0.0;
    env = 0.0;
    armed = false;
    square = 1.0;
    compEnv = 0.0;
    onGain = 0.0;
    active = false;
}

void RootEnhancer::configure(const RootSettings& s) noexcept
{
    const double f = std::clamp(s.freqHz, 40.0, 400.0);
    lp1.setLowPass(f, kButterQ, sr);
    lp2.setLowPass(f, kButterQ, sr);
    satLp.setLowPass(std::min(2.5 * f, 0.45 * sr), kButterQ, sr);
    subPre.setLowPass(0.8 * f, kButterQ, sr);
    subLp1.setLowPass(0.5 * f, kButterQ, sr);
    subLp2.setLowPass(0.5 * f, kButterQ, sr);
    tight.setHighPass(std::clamp(s.tightHz, 10.0, 200.0), kButterQ, sr);
    eqSub.setLowShelf(50.0, s.subDb, sr);
    eqLow.setPeak(100.0, s.lowDb, 1.0, sr);
    eqLowMid.setPeak(250.0, s.lowMidDb, 1.0, sr);
    current = s;
    configured = true;
}

void RootEnhancer::process(const double* di, double* L, double* R, int n, const RootSettings& s) noexcept
{
    if (!s.on && !active) return; // off and faded out: untouched (bit-transparent), no CPU
    if (!active) { reset(); active = true; growlNow = s.growl; levelNow = dbToGain(s.levelDb); }

    const bool filtersChanged = !configured || s.freqHz != current.freqHz || s.tightHz != current.tightHz || s.subDb != current.subDb
                                || s.lowDb != current.lowDb || s.lowMidDb != current.lowMidDb;
    if (filtersChanged) configure(s);
    current = s;

    const double growlTarget = std::clamp(s.growl, 0.0, 1.0);
    const double levelTarget = dbToGain(std::clamp(s.levelDb, -60.0, 24.0)) * (s.invert ? -1.0 : 1.0);
    // GROWL drives the curve: 4x .. 64x (+12 .. +36 dB) relative to the low band
    const double comp = std::clamp(s.comp, 0.0, 1.0);
    const double ratio = 1.0 + 7.0 * comp;
    const double thrDb = -12.0 - 18.0 * comp;
    const double thr = dbToGain(thrDb);
    const double makeup = comp > 0.001 ? dbToGain((1.0 - 1.0 / ratio) * (-thrDb) * 0.5) : 1.0;
    float peak = 0.0f;
    double grMax = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double target = s.on ? 1.0 : 0.0;
        if (onGain < target) onGain = std::min(target, onGain + fadeStep);
        else if (onGain > target) onGain = std::max(target, onGain - fadeStep);
        growlNow += (growlTarget - growlNow) * smooth;
        levelNow += (levelTarget - levelNow) * smooth;

        const double src = s.mode == RootMode::post ? (R != nullptr ? 0.5 * (L[i] + R[i]) : L[i]) : di[i];
        const double band = lp2.process(lp1.process(src));

        // GROWL: asymmetric saturation of the band, loudness kept (slow RMS match), DC removed
        const double drive = 4.0 * std::pow(16.0, growlNow);
        double sat = satLp.process(shape(drive * band));
        const double dc = sat - dcX + dcCoef * dcY; // 8 Hz DC blocker (the asymmetric curve makes DC)
        dcX = sat;
        dcY = dc;
        sat = dc;
        powIn += (band * band - powIn) * powCoef;
        powSat += (sat * sat - powSat) * powCoef;
        const double match = powSat > 1e-14 ? std::clamp(std::sqrt(powIn / powSat), 0.05, 8.0) : 1.0;
        double layer = (1.0 - growlNow) * band + growlNow * sat * match;

        if (s.mode == RootMode::sub)
        {
            // divider: toggles on every upward zero crossing of the cleaned band -> half the frequency
            const double x = subPre.process(band);
            const double a = std::abs(x);
            env += (a - env) * (a > env ? envAtt : envRel);
            const double hyst = 0.15 * env;
            if (x < -hyst) armed = true;
            else if (armed && x > hyst) { armed = false; square = -square; }
            const double sub = subLp2.process(subLp1.process(square)) * env * 1.4;
            layer += sub;
        }

        layer = tight.process(layer);
        layer = eqLowMid.process(eqLow.process(eqSub.process(layer)));

        if (comp > 0.001)
        {
            const double a = std::abs(layer);
            compEnv += (a - compEnv) * (a > compEnv ? compAtt : compRel);
            double g = 1.0;
            if (compEnv > thr) g = std::pow(thr / compEnv, 1.0 - 1.0 / ratio);
            grMax = std::max(grMax, -20.0 * std::log10(g));
            layer *= g * makeup;
        }

        layer *= levelNow * onGain;
        if (!std::isfinite(layer)) { layer = 0.0; reset(); active = true; }
        L[i] += layer;
        if (R != nullptr) R[i] += layer;
        peak = std::max(peak, (float) std::abs(layer));
    }

    if (peak > layerPeak.load(std::memory_order_relaxed)) layerPeak.store(peak, std::memory_order_relaxed);
    if ((float) grMax > grPeakDb.load(std::memory_order_relaxed)) grPeakDb.store((float) grMax, std::memory_order_relaxed);
    if (!s.on && onGain <= 0.0) { reset(); } // faded out: stop (bit-transparent from the next block)
}

double RootEnhancer::responseDb(const RootSettings& s, double hz, double sampleRate) noexcept
{
    Biquad a, b, t, e1, e2, e3;
    const double f = std::clamp(s.freqHz, 40.0, 400.0);
    a.setLowPass(f, kButterQ, sampleRate);
    b.setLowPass(f, kButterQ, sampleRate);
    t.setHighPass(std::clamp(s.tightHz, 10.0, 200.0), kButterQ, sampleRate);
    e1.setLowShelf(50.0, s.subDb, sampleRate);
    e2.setPeak(100.0, s.lowDb, 1.0, sampleRate);
    e3.setPeak(250.0, s.lowMidDb, 1.0, sampleRate);
    double m = a.magnitude(hz, sampleRate) * b.magnitude(hz, sampleRate) * t.magnitude(hz, sampleRate) * e1.magnitude(hz, sampleRate)
               * e2.magnitude(hz, sampleRate) * e3.magnitude(hz, sampleRate) * dbToGain(s.levelDb);
    return 20.0 * std::log10(std::max(m, 1e-9));
}

} // namespace ampsurd

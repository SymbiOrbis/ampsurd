#include "ampsurd/BlendToneEq.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kShelfQ = 0.707;
constexpr int kStep = 16;         // coefficients follow the smoothed gains every 16 samples
} // namespace

int BlendToneEq::bandOfHz(double hz) noexcept
{
    if (hz < kEdgesHz[0] || hz >= kEdgesHz[kBands]) return -1;
    for (int b = 0; b < kBands; ++b)
        if (hz < kEdgesHz[(size_t) b + 1]) return b;
    return -1;
}

double BlendToneEq::centreHz(int band) noexcept
{
    if (band == 0) return 75.0;            // low shelf region
    if (band == kBands - 1) return 13500.0; // high shelf region
    return std::sqrt(kEdgesHz[(size_t) band] * kEdgesHz[(size_t) band + 1]);
}

// first band: low shelf at 150 Hz, then bells as wide as their band, last band: high shelf at 9.6 kHz
BlendToneEq::Coefs BlendToneEq::design(int band, double gainDb, double fs) noexcept
{
    Coefs c;
    if (gainDb == 0.0) return c; // exact identity
    const double A = std::pow(10.0, gainDb / 40.0);
    double g, k;
    if (band == 0)
    {
        g = std::tan(kPi * std::min(kEdgesHz[1], 0.45 * fs) / fs) / std::sqrt(A);
        k = 1.0 / kShelfQ;
        c.m0 = 1.0; c.m1 = k * (A - 1.0); c.m2 = A * A - 1.0;
    }
    else if (band == kBands - 1)
    {
        g = std::tan(kPi * std::min(kEdgesHz[(size_t) kBands - 1], 0.45 * fs) / fs) * std::sqrt(A);
        k = 1.0 / kShelfQ;
        c.m0 = A * A; c.m1 = k * (1.0 - A) * A; c.m2 = 1.0 - A * A;
    }
    else
    {
        g = std::tan(kPi * std::min(centreHz(band), 0.45 * fs) / fs);
        const double q = centreHz(band) / (kEdgesHz[(size_t) band + 1] - kEdgesHz[(size_t) band]); // one band wide
        k = 1.0 / (q * A);
        c.m0 = 1.0; c.m1 = k * (A * A - 1.0); c.m2 = 0.0;
    }
    c.g = g;
    c.k = k;
    c.a1 = 1.0 / (1.0 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

double BlendToneEq::responseDb(const Gains& gains, double hz, double fs) noexcept
{
    // trapezoidal SVF = bilinear transform: s = j tan(w/2) / g (normalised); H = m0 + m1 BP + m2 LP
    double db = 0.0;
    const double t = std::tan(kPi * std::min(hz, 0.499 * fs) / fs);
    for (int b = 0; b < kBands; ++b)
    {
        const double gd = gains[(size_t) b];
        if (gd == 0.0) continue;
        const auto c = design(b, gd, fs);
        const std::complex<double> s(0.0, t / c.g);
        const auto den = s * s + c.k * s + 1.0;
        const auto H = c.m0 + c.m1 * s / den + c.m2 / den;
        db += 20.0 * std::log10(std::max(1e-12, std::abs(H)));
    }
    return db;
}

BlendToneEq::Gains BlendToneEq::fitGains(const Gains& want, double fs) noexcept
{
    Gains g = want;
    bool any = false;
    for (double v : want) any = any || v != 0.0;
    if (!any) return g;
    for (int it = 0; it < 12; ++it)
    {
        double worst = 0.0;
        Gains err {};
        for (int b = 0; b < kBands; ++b)
        {
            err[(size_t) b] = want[(size_t) b] - responseDb(g, centreHz(b), fs);
            worst = std::max(worst, std::abs(err[(size_t) b]));
        }
        if (worst < 0.02) break;
        for (int b = 0; b < kBands; ++b)
            g[(size_t) b] = std::clamp(g[(size_t) b] + 0.8 * err[(size_t) b], -2.0 * kMaxCorrectionDb, 2.0 * kMaxCorrectionDb);
    }
    return g;
}

void BlendToneEq::prepare(double sampleRate) noexcept
{
    sr = sampleRate;
    smooth = 1.0 - std::exp(-(double) kStep / (0.05 * sr)); // ~50 ms
    reset();
}

void BlendToneEq::reset() noexcept
{
    targets = {};
    filterTargets = {};
    now = {};
    coefs = {};
    stL = {};
    stR = {};
    active = false;
}

void BlendToneEq::setTargets(const Gains& want) noexcept
{
    Gains w {};
    for (int b = 0; b < kBands; ++b)
    {
        double v = std::clamp(want[(size_t) b], -kMaxCorrectionDb, kMaxCorrectionDb);
        if (std::abs(v) < 0.01) v = 0.0;
        w[(size_t) b] = v;
    }
    bool changed = false;
    for (int b = 0; b < kBands; ++b)
        changed = changed || std::abs(w[(size_t) b] - targets[(size_t) b]) > 0.005 || (w[(size_t) b] == 0.0) != (targets[(size_t) b] == 0.0);
    if (!changed) return;
    targets = w;
    filterTargets = fitGains(w, sr);
}

void BlendToneEq::process(double* L, double* R, int n) noexcept
{
    bool moving = false, any = false;
    for (int b = 0; b < kBands; ++b)
    {
        moving = moving || now[(size_t) b] != filterTargets[(size_t) b];
        any = any || now[(size_t) b] != 0.0 || filterTargets[(size_t) b] != 0.0;
    }
    if (!any)
    {
        if (active) { stL = {}; stR = {}; active = false; }
        return; // flat: untouched
    }
    active = true;
    for (int pos = 0; pos < n; pos += kStep)
    {
        if (moving)
        {
            moving = false;
            for (int b = 0; b < kBands; ++b)
            {
                double& v = now[(size_t) b];
                const double t = filterTargets[(size_t) b];
                v += (t - v) * smooth;
                if (std::abs(t - v) < 1e-4) v = t;
                moving = moving || v != t;
                coefs[(size_t) b] = design(b, v, sr);
            }
        }
        const int m = std::min(kStep, n - pos);
        for (int b = 0; b < kBands; ++b)
        {
            if (now[(size_t) b] == 0.0) { stL[(size_t) b] = {}; stR[(size_t) b] = {}; continue; }
            const auto& c = coefs[(size_t) b];
            auto run = [&](double* x, State& st) {
                for (int i = pos; i < pos + m; ++i)
                {
                    const double v0 = x[i], v3 = v0 - st.ic2;
                    const double v1 = c.a1 * st.ic1 + c.a2 * v3;
                    const double v2 = st.ic2 + c.a2 * st.ic1 + c.a3 * v3;
                    st.ic1 = 2.0 * v1 - st.ic1;
                    st.ic2 = 2.0 * v2 - st.ic2;
                    x[i] = c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
                }
            };
            run(L, stL[(size_t) b]);
            if (R != nullptr) run(R, stR[(size_t) b]);
        }
    }
}

} // namespace ampsurd

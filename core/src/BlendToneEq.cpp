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

namespace
{
// magnitude (dB) of designed filters at hz; trapezoidal SVF = bilinear: s = j tan(w/2) / g, H = m0 + m1 BP + m2 LP
template <typename CoefArray, typename Active>
double cascadeDb(const CoefArray& c, const Active& active, double hz, double fs)
{
    double db = 0.0;
    const double t = std::tan(kPi * std::min(hz, 0.499 * fs) / fs);
    for (size_t b = 0; b < c.size(); ++b)
    {
        if (!active[b]) continue;
        const std::complex<double> s(0.0, t / c[b].g);
        const auto den = s * s + c[b].k * s + 1.0;
        const auto H = c[b].m0 + c[b].m1 * s / den + c[b].m2 / den;
        db += 20.0 * std::log10(std::max(1e-12, std::abs(H)));
    }
    return db;
}
} // namespace

double BlendToneEq::responseDb(const Gains& gains, double hz, double fs) noexcept
{
    std::array<Coefs, kBands> c {};
    std::array<bool, kBands> on {};
    for (int b = 0; b < kBands; ++b)
    {
        on[(size_t) b] = gains[(size_t) b] != 0.0;
        if (on[(size_t) b]) c[(size_t) b] = design(b, gains[(size_t) b], fs);
    }
    return cascadeDb(c, on, hz, fs);
}

BlendToneEq::Gains BlendToneEq::fitGains(const Gains& want, double fs) noexcept
{
    Gains g = want;
    bool any = false;
    for (double v : want) any = any || v != 0.0;
    if (!any) return g;
    for (int it = 0; it < 12; ++it)
    {
        std::array<Coefs, kBands> c {};
        std::array<bool, kBands> on {};
        for (int b = 0; b < kBands; ++b)
        {
            on[(size_t) b] = g[(size_t) b] != 0.0;
            if (on[(size_t) b]) c[(size_t) b] = design(b, g[(size_t) b], fs); // once per iteration
        }
        double worst = 0.0;
        Gains err {};
        for (int b = 0; b < kBands; ++b)
        {
            err[(size_t) b] = want[(size_t) b] - cascadeDb(c, on, centreHz(b), fs);
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
        changed = changed || std::abs(w[(size_t) b] - targets[(size_t) b]) > 0.1 || (w[(size_t) b] == 0.0) != (targets[(size_t) b] == 0.0);
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

// ---------------------------------------------------------------------------------------------
void BlendToneTracker::prepare(double sampleRate) noexcept
{
    sr = sampleRate;
    for (int b = 0; b < BlendToneEq::kBands; ++b)
    {
        auto& f = filt[(size_t) b];
        const double lo = BlendToneEq::kEdgesHz[(size_t) b], hi = std::min(BlendToneEq::kEdgesHz[(size_t) b + 1], 0.45 * sr);
        if (b == 0 || b == BlendToneEq::kBands - 1)
        {
            // edge bands: 2nd-order Butterworth low-pass below 150 Hz / high-pass above 9.6 kHz
            const bool low = b == 0;
            const double fc = low ? BlendToneEq::kEdgesHz[1] : lo;
            const double w = 2.0 * kPi * std::min(fc, 0.45 * sr) / sr, c = std::cos(w), al = std::sin(w) / (2.0 * 0.7071), a0 = 1.0 + al;
            if (low) { f.b0 = (1 - c) / 2 / a0; f.b1 = (1 - c) / a0; f.b2 = f.b0; }
            else { f.b0 = (1 + c) / 2 / a0; f.b1 = -(1 + c) / a0; f.b2 = f.b0; }
            f.a1 = -2.0 * c / a0;
            f.a2 = (1.0 - al) / a0;
        }
        else
        {
            // band-pass (0 dB peak), one band wide
            const double fc = std::sqrt(lo * hi), q = fc / std::max(1.0, hi - lo);
            const double w = 2.0 * kPi * std::min(fc, 0.45 * sr) / sr, al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
            f.b0 = al / a0; f.b1 = 0.0; f.b2 = -al / a0;
            f.a1 = -2.0 * std::cos(w) / a0;
            f.a2 = (1.0 - al) / a0;
        }
    }
    forget = 1.0 / (6.0 * sr); // ~6 s of playing
    reset();
}

void BlendToneTracker::reset() noexcept
{
    st = {};
    cov = {};
    acc = {};
    tracks = {};
    conf = 0.0;
    activeSeconds = 0.0;
}

void BlendToneTracker::resetSlot(int slot) noexcept
{
    for (int b = 0; b < BlendToneEq::kBands; ++b)
        for (int j = 0; j < kSlots; ++j)
            cov[(size_t) b][(size_t) slot][(size_t) j] = cov[(size_t) b][(size_t) j][(size_t) slot] = 0.0;
    st[(size_t) slot] = {};
    tracks[(size_t) slot] = 0.0;
    conf = 0.0;
    activeSeconds = 0.0;
}

void BlendToneTracker::analyse(const std::array<const double*, kSlots>& paths, const std::array<double, kSlots>& scale, int n) noexcept
{
    std::array<int, kSlots> idx {};
    int k = 0;
    for (int i = 0; i < kSlots; ++i)
        if (paths[(size_t) i] != nullptr) idx[(size_t) k++] = i;
    if (k < 2 || n <= 0) return;

    for (auto& m : acc)
        for (auto& row : m) row.fill(0.0);
    std::array<std::array<double, BlendToneEq::kBands>, kSlots> yb {};
    for (int s = 0; s < n; ++s)
    {
        for (int a = 0; a < k; ++a)
        {
            const int i = idx[(size_t) a];
            const double x = paths[(size_t) i][s] * scale[(size_t) i];
            for (int b = 0; b < BlendToneEq::kBands; ++b)
            {
                const auto& f = filt[(size_t) b];
                auto& z = st[(size_t) i][(size_t) b];
                const double v = f.b0 * x + z.z1; // transposed direct form II
                z.z1 = f.b1 * x - f.a1 * v + z.z2;
                z.z2 = f.b2 * x - f.a2 * v;
                yb[(size_t) i][(size_t) b] = v;
            }
        }
        for (int b = 0; b < BlendToneEq::kBands; ++b)
        {
            auto& m = acc[(size_t) b];
            for (int a = 0; a < k; ++a)
            {
                const int i = idx[(size_t) a];
                const double yi = yb[(size_t) i][(size_t) b];
                for (int c = a; c < k; ++c)
                    m[(size_t) i][(size_t) idx[(size_t) c]] += yi * yb[(size_t) idx[(size_t) c]][(size_t) b];
            }
        }
    }
    // learn only from playing: mean power of the level-matched paths above -60 dBFS
    double e = 0.0;
    for (int a = 0; a < k; ++a)
        for (int b = 0; b < BlendToneEq::kBands; ++b) e += acc[(size_t) b][(size_t) idx[(size_t) a]][(size_t) idx[(size_t) a]];
    if (e / ((double) n * k) < 1e-6) return;
    const double w = std::min(1.0, (double) n * forget);
    for (int b = 0; b < BlendToneEq::kBands; ++b)
        for (int a = 0; a < k; ++a)
            for (int c = a; c < k; ++c)
            {
                const int i = idx[(size_t) a], j = idx[(size_t) c];
                double& v = cov[(size_t) b][(size_t) i][(size_t) j];
                // a plain average over the first seconds of a pair, then a ~6 s moving average
                const double seconds = std::min(tracks[(size_t) i], tracks[(size_t) j]) + (double) n / sr;
                const double wPair = std::max(w, (double) n / (seconds * sr));
                v += (acc[(size_t) b][(size_t) i][(size_t) j] / n - v) * wPair;
                cov[(size_t) b][(size_t) j][(size_t) i] = v;
            }
    for (int a = 0; a < k; ++a) tracks[(size_t) idx[(size_t) a]] += (double) n / sr;
    activeSeconds += (double) n / sr;
    conf = std::min(1.0, activeSeconds / 4.0);
}

} // namespace ampsurd

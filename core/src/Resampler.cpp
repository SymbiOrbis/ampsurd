#include "ampsurd/Resampler.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-16 * sum) break;
    }
    return sum;
}
} // namespace

// Implementation notes: output sample m sits at input position t = m * step. The kernel spans
// `taps` input samples (more when down-sampling, so that the cut-off follows the output rate).
// Pass band to 0.9 x the lower Nyquist, -6 dB at 0.95, stop band from the lower Nyquist on.
StreamResampler::StreamResampler(double fromRate, double toRate, int channels)
    : inRate(fromRate), outRate(toRate), numChannels(channels), identity(std::abs(fromRate - toRate) < 1e-9)
{
    hist.assign((size_t) channels, {});
    if (identity) return;
    step = inRate / outRate;
    const double r = std::min(1.0, outRate / inRate);
    const double cutoff = 0.95 * r; // fraction of the input Nyquist
    int taps = (int) std::ceil(128.0 / r);
    taps += taps & 1;
    const double beta = 10.0, i0b = besselI0(beta);
    table.assign((size_t) (kPhases + 1) * (size_t) taps, 0.0f);
    for (int p = 0; p <= kPhases; ++p)
    {
        const double frac = (double) p / kPhases;
        for (int j = 0; j < taps; ++j)
        {
            const double d = frac + taps / 2 - 1 - j; // distance from the output position, input samples
            const double a = kPi * cutoff * d;
            const double sinc = std::abs(a) < 1e-12 ? 1.0 : std::sin(a) / a;
            const double u = d / (taps / 2.0);
            const double w = std::abs(u) >= 1.0 ? 0.0 : besselI0(beta * std::sqrt(1.0 - u * u)) / i0b;
            table[(size_t) p * (size_t) taps + (size_t) j] = (float) (cutoff * sinc * w);
        }
    }
    scale = (double) taps; // reused as the tap count
    // history starts with taps/2 zeros so that output 0 is centred on input 0 (no delay)
    for (auto& h : hist) h.assign((size_t) (taps / 2), 0.0f);
}

void StreamResampler::process(const float* const* in, int n, std::vector<std::vector<float>>& out)
{
    if (out.size() < (size_t) numChannels) out.resize((size_t) numChannels);
    if (identity)
    {
        for (int c = 0; c < numChannels; ++c) out[(size_t) c].insert(out[(size_t) c].end(), in[c], in[c] + n);
        return;
    }
    for (int c = 0; c < numChannels; ++c) hist[(size_t) c].insert(hist[(size_t) c].end(), in[c], in[c] + n);
    produce(out, false);
}

void StreamResampler::finish(std::vector<std::vector<float>>& out)
{
    if (out.size() < (size_t) numChannels) out.resize((size_t) numChannels);
    if (identity) return;
    const int taps = (int) scale;
    for (auto& h : hist) h.insert(h.end(), (size_t) taps, 0.0f); // let the filter ring out
    produce(out, true);
}

void StreamResampler::produce(std::vector<std::vector<float>>& out, bool flushing)
{
    const int taps = (int) scale, half = taps / 2;
    const size_t len = hist[0].size();
    // when flushing, stop where the real input (before the padding) ends
    const double end = flushing ? (double) (len - (size_t) taps) : (double) len;
    while (true)
    {
        // position from the output count (exact, independent of how the input was chunked)
        const double t = (double) outCount * step + (double) half - (double) trimmed;
        const double fl = std::floor(t);
        const long last = (long) fl + half;
        if (flushing ? t >= end : last >= (long) len) break;
        const long first = (long) fl - half + 1;
        const double fp = (t - fl) * kPhases;
        const int p = std::min(kPhases - 1, (int) fp);
        const float mu = (float) (fp - p);
        const float* k0 = table.data() + (size_t) p * (size_t) taps;
        const float* k1 = k0 + taps;
        for (int c = 0; c < numChannels; ++c)
        {
            const float* x = hist[(size_t) c].data();
            double acc = 0.0;
            for (int j = 0; j < taps; ++j)
            {
                const long idx = first + j;
                if (idx < 0 || idx >= (long) len) continue;
                acc += (double) x[idx] * (double) (k0[j] + mu * (k1[j] - k0[j]));
            }
            out[(size_t) c].push_back((float) acc);
        }
        ++outCount;
    }
    // drop history that is no longer needed
    const long keepFrom = (long) std::floor((double) outCount * step + (double) half - (double) trimmed) - half - 1;
    if (keepFrom > 4096)
    {
        for (auto& h : hist) h.erase(h.begin(), h.begin() + keepFrom);
        trimmed += keepFrom;
    }
}

} // namespace ampsurd

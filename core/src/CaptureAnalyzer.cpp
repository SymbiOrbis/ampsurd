#include "ampsurd/CaptureAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Small deterministic RNG (xorshift) so the test signal is identical everywhere.
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    double next() // -1..1
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (double) (s >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }
};

// Karplus-Strong plucked string added into `out` starting at `start`.
void pluck(std::vector<double>& out, double sr, size_t start, double freq, double seconds, double velocity,
           double damping, double brightness, Rng& rng)
{
    const int period = std::max(2, (int) std::lround(sr / freq));
    std::vector<double> line((size_t) period);
    double lp = 0.0;
    for (auto& v : line)
    {
        lp += brightness * (rng.next() - lp); // pick: filtered noise burst
        v = lp;
    }
    const size_t len = std::min(out.size() - start, (size_t) (seconds * sr));
    double prev = 0.0;
    for (size_t i = 0; i < len; ++i)
    {
        const size_t idx = i % (size_t) period;
        const double cur = line[idx];
        const double nextVal = damping * 0.5 * (cur + prev);
        prev = cur;
        line[idx] = nextVal;
        // short fade-out at the end of the note (string muted)
        const double tail = std::min(1.0, (double) (len - i) / (0.01 * sr));
        out[start + i] += velocity * cur * tail;
    }
}

double hann(int i, int n) { return 0.5 - 0.5 * std::cos(2.0 * kPi * i / n); }
} // namespace

void CaptureAnalyzer::fft(std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& v : a) v /= (double) n;
}

std::vector<double> CaptureAnalyzer::makeTestSignal(double sr)
{
    std::vector<double> x((size_t) (3.0 * sr), 0.0);
    Rng rng;
    auto at = [&](double t) { return (size_t) (t * sr); };

    // single notes
    const double notes[] = { 82.41, 110.0, 146.83, 196.0 };
    for (int i = 0; i < 4; ++i)
        pluck(x, sr, at(0.05 + 0.2 * i), notes[i], 0.19, 0.6 + 0.1 * i, 0.996, 0.6, rng);
    // power chord E2-B2-E3, strummed
    const double power[] = { 82.41, 123.47, 164.81 };
    for (int i = 0; i < 3; ++i)
        pluck(x, sr, at(0.85 + 0.008 * i), power[i], 0.7, 0.8, 0.997, 0.7, rng);
    // palm-muted chugs (eighth notes at 150 bpm)
    for (int i = 0; i < 4; ++i)
        for (int s = 0; s < 2; ++s)
            pluck(x, sr, at(1.6 + 0.2 * i + 0.004 * s), s == 0 ? 82.41 : 123.47, 0.15, 0.9, 0.95, 0.35, rng);
    // open E major chord
    const double open[] = { 82.41, 123.47, 164.81, 207.65, 246.94, 329.63 };
    for (int i = 0; i < 6; ++i)
        pluck(x, sr, at(2.4 + 0.012 * i), open[i], 0.55, 0.5, 0.998, 0.8, rng);

    double peak = 0.0;
    for (double v : x) peak = std::max(peak, std::abs(v));
    if (peak > 0.0)
        for (double& v : x) v *= 0.5 / peak; // -6 dBFS
    return x;
}

std::vector<double> CaptureAnalyzer::render(CaptureModel& model, const std::vector<double>& signal, bool levelMatch)
{
    std::vector<double> out(signal.size(), 0.0);
    const int block = std::max(1, model.getPreparedBlockSize());
    for (size_t pos = 0; pos < signal.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t>((size_t) block, signal.size() - pos);
        model.process(signal.data() + pos, out.data() + pos, n, levelMatch);
    }
    return out;
}

std::shared_ptr<const CaptureAnalyzer::Measurement> CaptureAnalyzer::measure(const std::vector<double>& y, double sampleRate)
{
    auto m = std::make_shared<Measurement>();
    m->sampleRate = sampleRate;
    std::vector<std::complex<double>> buf((size_t) kFftSize);
    for (size_t start = 0; start + (size_t) kFftSize <= y.size(); start += (size_t) kHop)
    {
        for (int i = 0; i < kFftSize; ++i)
            buf[(size_t) i] = { y[start + (size_t) i] * hann(i, kFftSize), 0.0 };
        fft(buf, false);
        m->frames.emplace_back(buf.begin(), buf.begin() + kBins);
    }
    return m;
}

CaptureAnalyzer::Spectrum CaptureAnalyzer::crossSpectrum(const Measurement& a, const Measurement& b)
{
    Spectrum s((size_t) kBins, { 0.0, 0.0 });
    const size_t frames = std::min(a.frames.size(), b.frames.size());
    for (size_t f = 0; f < frames; ++f)
        for (int k = 0; k < kBins; ++k)
            s[(size_t) k] += a.frames[f][(size_t) k] * std::conj(b.frames[f][(size_t) k]);
    return s;
}

CaptureAnalyzer::Alignment CaptureAnalyzer::estimateAlignment(const Measurement& ref, const Measurement& other)
{
    Alignment r;
    const double sr = ref.sampleRate;
    const auto Sro = crossSpectrum(ref, other);
    const auto Srr = crossSpectrum(ref, ref);
    const auto Soo = crossSpectrum(other, other);

    // Band weights: 70-1200 Hz with raised-cosine edges.
    auto weight = [&](int k) {
        const double f = (double) k * sr / kFftSize;
        auto edge = [](double x) { return x <= 0 ? 0.0 : x >= 1 ? 1.0 : 0.5 - 0.5 * std::cos(kPi * x); };
        return edge((f - 50.0) / 40.0) * edge((1600.0 - f) / 800.0);
    };

    double prr = 0.0, poo = 0.0;
    std::vector<std::complex<double>> spec((size_t) kFftSize, { 0.0, 0.0 });
    for (int k = 1; k < kBins - 1; ++k)
    {
        const double w = weight(k);
        prr += w * Srr[(size_t) k].real();
        poo += w * Soo[(size_t) k].real();
        // R(tau) = Re sum_k w S_ro[k] e^{-j w_k tau}  ->  IFFT of conj(S_ro) (one-sided)
        spec[(size_t) k] = w * std::conj(Sro[(size_t) k]);
    }
    const double norm = std::sqrt(prr * poo);
    if (norm <= 0.0)
        return r;

    fft(spec, true);
    auto R = [&](int lag) { return kFftSize * spec[(size_t) ((lag + kFftSize) % kFftSize)].real() / norm; };

    const int maxLag = (int) std::ceil(kMaxAutoLagMs * 0.001 * sr);
    int best = 0;
    double bestAbs = -1.0;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        const double v = std::abs(R(lag));
        if (v > bestAbs) { bestAbs = v; best = lag; }
    }

    // Parabolic interpolation on the signed correlation around the peak.
    const double sign = R(best) < 0.0 ? -1.0 : 1.0;
    const double y0 = sign * R(best - 1), y1 = sign * R(best), y2 = sign * R(best + 1);
    const double den = y0 - 2.0 * y1 + y2;
    double frac = den < 0.0 ? 0.5 * (y0 - y2) / den : 0.0;
    frac = std::clamp(frac, -0.5, 0.5);

    r.lagSamples = best + frac;
    r.polarity = sign;
    r.correlationBefore = R(0);
    r.correlationAfter = std::min(1.0, y1 - 0.25 * (y0 - y2) * frac);
    r.reliable = r.correlationAfter >= kMinReliableCorrelation && std::abs(best) < maxLag;
    if (!r.reliable)
    {
        r.lagSamples = 0.0;
        r.polarity = 1.0;
    }
    return r;
}

CaptureAnalyzer::Covariance CaptureAnalyzer::covariance(
    const std::array<std::shared_ptr<const Measurement>, kNumSlots>& m,
    const std::array<std::array<Spectrum, kNumSlots>, kNumSlots>& cross,
    const std::array<double, kNumSlots>& delaySamples,
    const std::array<double, kNumSlots>& polarity,
    const std::array<double, kNumSlots>& phaseRadians,
    const std::vector<double>* binWeights)
{
    Covariance out;
    size_t frames = 0;
    for (const auto& mm : m)
        if (mm) frames = mm->frames.size();
    if (frames == 0)
        return out;

    double wsum = 0.0;
    for (int i = 0; i < kFftSize; ++i) wsum += hann(i, kFftSize) * hann(i, kFftSize);
    const double scale = 1.0 / ((double) frames * kFftSize * wsum);

    // per-slot phasor: delay, polarity, rotation  (X_i -> X_i * s_i * e^{-j w D_i} * e^{j theta_i})
    std::array<std::vector<std::complex<double>>, kNumSlots> ph;
    for (int i = 0; i < kNumSlots; ++i)
    {
        out.valid[(size_t) i] = m[(size_t) i] != nullptr;
        if (!out.valid[(size_t) i]) continue;
        ph[(size_t) i].resize((size_t) kBins);
        for (int k = 0; k < kBins; ++k)
        {
            const double w = 2.0 * kPi * k / kFftSize;
            ph[(size_t) i][(size_t) k] = polarity[(size_t) i] * std::polar(1.0, -w * delaySamples[(size_t) i] + phaseRadians[(size_t) i]);
        }
    }

    for (int i = 0; i < kNumSlots; ++i)
        for (int j = i; j < kNumSlots; ++j)
        {
            if (!out.valid[(size_t) i] || !out.valid[(size_t) j]) continue;
            const auto& S = cross[(size_t) i][(size_t) j];
            if (S.size() != (size_t) kBins) continue;
            double acc = 0.0;
            for (int k = 1; k < kBins - 1; ++k) // skip DC and Nyquist
            {
                const double w = binWeights ? (*binWeights)[(size_t) k] : 1.0;
                acc += 2.0 * w * (S[(size_t) k] * ph[(size_t) i][(size_t) k] * std::conj(ph[(size_t) j][(size_t) k])).real();
            }
            out.C[(size_t) i][(size_t) j] = out.C[(size_t) j][(size_t) i] = acc * scale;
        }
    return out;
}

namespace
{
struct Biquad { double b0, b1, b2, a1, a2; };

// BS.1770 K-weighting for any sample rate (same parametrisation as libebur128).
void kFilters(double fs, Biquad& shelf, Biquad& hp)
{
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                  2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        hp = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
}

double magSquared(const Biquad& b, double w)
{
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    const auto num = b.b0 + b.b1 * z1 + b.b2 * z2;
    const auto den = 1.0 + b.a1 * z1 + b.a2 * z2;
    return std::norm(num / den);
}
} // namespace

double CaptureAnalyzer::loudnessDb(const Measurement& m)
{
    const auto w = loudnessWeights(m.sampleRate);
    double wsum = 0.0;
    for (int i = 0; i < kFftSize; ++i) wsum += hann(i, kFftSize) * hann(i, kFftSize);
    double acc = 0.0;
    for (const auto& f : m.frames)
        for (int k = 1; k < kBins - 1; ++k)
            acc += 2.0 * w[(size_t) k] * std::norm(f[(size_t) k]);
    const double p = m.frames.empty() ? 0.0 : acc / ((double) m.frames.size() * kFftSize * wsum);
    return p > 1e-20 ? 10.0 * std::log10(p) : -200.0;
}

double CaptureAnalyzer::levelMatchGain(double loudness)
{
    const double db = std::clamp(kTargetLoudnessDb - loudness, -40.0, 40.0);
    return std::pow(10.0, db / 20.0);
}

std::shared_ptr<const CaptureAnalyzer::RigAnalysis>
CaptureAnalyzer::analyseRig(const std::array<std::shared_ptr<const Measurement>, kNumSlots>& meas)
{
    auto rig = std::make_shared<RigAnalysis>();
    rig->meas = meas;
    for (int i = 0; i < kNumSlots; ++i)
        if (meas[(size_t) i])
        {
            rig->sampleRate = meas[(size_t) i]->sampleRate;
            rig->loudnessDb[(size_t) i] = loudnessDb(*meas[(size_t) i]);
            if (rig->reference < 0) rig->reference = i;
        }
    rig->kWeights = loudnessWeights(rig->sampleRate);

    for (int i = 0; i < kNumSlots; ++i)
        for (int j = i; j < kNumSlots; ++j)
            if (meas[(size_t) i] && meas[(size_t) j])
            {
                rig->cross[(size_t) i][(size_t) j] = crossSpectrum(*meas[(size_t) i], *meas[(size_t) j]);
                if (i != j)
                {
                    rig->cross[(size_t) j][(size_t) i] = rig->cross[(size_t) i][(size_t) j];
                    for (auto& v : rig->cross[(size_t) j][(size_t) i]) v = std::conj(v);
                }
            }

    if (rig->reference >= 0)
    {
        double maxLag = 0.0;
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (!meas[(size_t) i]) continue;
            if (i == rig->reference)
            {
                rig->align[(size_t) i].reliable = true;
                rig->align[(size_t) i].correlationBefore = rig->align[(size_t) i].correlationAfter = 1.0;
                continue;
            }
            rig->align[(size_t) i] = estimateAlignment(*meas[(size_t) rig->reference], *meas[(size_t) i]);
            maxLag = std::max(maxLag, rig->align[(size_t) i].lagSamples);
        }
        for (int i = 0; i < kNumSlots; ++i)
            if (meas[(size_t) i])
                rig->autoDelay[(size_t) i] = maxLag - rig->align[(size_t) i].lagSamples;
    }
    return rig;
}

CaptureAnalyzer::Covariance CaptureAnalyzer::covariance(const RigAnalysis& rig,
                                                        const std::array<double, kNumSlots>& delaySamples,
                                                        const std::array<double, kNumSlots>& polarity,
                                                        const std::array<double, kNumSlots>& phaseRadians,
                                                        const std::array<double, kNumSlots>& levelGain)
{
    std::array<double, kNumSlots> scale {};
    for (int i = 0; i < kNumSlots; ++i) scale[(size_t) i] = polarity[(size_t) i] * levelGain[(size_t) i];
    return covariance(rig.meas, rig.cross, delaySamples, scale, phaseRadians, &rig.kWeights);
}

std::vector<double> CaptureAnalyzer::loudnessWeights(double sampleRate)
{
    Biquad s, h;
    kFilters(sampleRate, s, h);
    std::vector<double> w((size_t) kBins);
    for (int k = 0; k < kBins; ++k)
    {
        const double om = 2.0 * kPi * k / kFftSize;
        w[(size_t) k] = magSquared(s, om) * magSquared(h, om);
    }
    return w;
}

void CaptureAnalyzer::kWeightInPlace(std::vector<double>& x, double sampleRate)
{
    Biquad f[2];
    kFilters(sampleRate, f[0], f[1]);
    for (const auto& b : f)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (double& v : x)
        {
            const double y = b.b0 * v + b.b1 * x1 + b.b2 * x2 - b.a1 * y1 - b.a2 * y2;
            x2 = x1; x1 = v; y2 = y1; y1 = y;
            v = y;
        }
    }
}

} // namespace ampsurd

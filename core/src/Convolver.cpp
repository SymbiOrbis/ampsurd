#include "ampsurd/Convolver.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}

// ---------------------------------------------------------------------------------------------
Fft::Fft(int size) : n(size)
{
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    bitrev.resize((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev[(size_t) i] = r;
    }
    passes = 1 + bits;
    cosT.resize((size_t) n / 2);
    sinT.resize((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i)
    {
        cosT[(size_t) i] = std::cos(2.0 * kPi * i / n);
        sinT[(size_t) i] = std::sin(2.0 * kPi * i / n);
    }
}

void Fft::run(double* re, double* im, bool inverse) const noexcept
{
    for (int p = 0; p < passes; ++p) pass(re, im, p, inverse);
}

void Fft::pass(double* re, double* im, int index, bool inverse) const noexcept
{
    if (index == 0)
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = bitrev[(size_t) i];
            if (j > i)
            {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        return;
    }
    const double sgn = inverse ? 1.0 : -1.0;
    const int len = 1 << index;
    const int half = len >> 1, step = n / len;
    for (int start = 0; start < n; start += len)
        for (int k = 0; k < half; ++k)
        {
            const double wr = cosT[(size_t) (k * step)], wi = sgn * sinT[(size_t) (k * step)];
            const int a = start + k, b = a + half;
            const double tr = re[b] * wr - im[b] * wi;
            const double ti = re[b] * wi + im[b] * wr;
            re[b] = re[a] - tr;
            im[b] = im[a] - ti;
            re[a] += tr;
            im[a] += ti;
        }
    if (inverse && index == passes - 1)
    {
        const double s = 1.0 / n;
        for (int i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
    }
}

// ---------------------------------------------------------------------------------------------
// One uniformly partitioned overlap-save stage. Partition p covers IR samples
// [offset + p*B, offset + (p+1)*B). With offset == B the result for the next block is computed at
// the block boundary; with offset == 2B it is needed one block later and is computed gradually.
struct Convolver::Stage
{
    Stage(const double* ir, int irLen, int offset, int blockSize)
        : B(blockSize), N(2 * blockSize), bins(blockSize + 1), fft(2 * blockSize)
    {
        slack = offset / B - 1; // 0 or 1
        P = std::max(1, (irLen - offset + B - 1) / B);
        Hre.assign((size_t) P * (size_t) bins, 0.0);
        Him.assign(Hre.size(), 0.0);
        Xre.assign(Hre.size(), 0.0);
        Xim.assign(Hre.size(), 0.0);
        std::vector<double> tr((size_t) N), ti((size_t) N);
        for (int p = 0; p < P; ++p)
        {
            std::fill(tr.begin(), tr.end(), 0.0);
            std::fill(ti.begin(), ti.end(), 0.0);
            for (int k = 0; k < B; ++k)
            {
                const int idx = offset + p * B + k;
                if (idx < irLen) tr[(size_t) k] = ir[idx];
            }
            fft.forward(tr.data(), ti.data());
            std::copy(tr.begin(), tr.begin() + bins, Hre.begin() + (size_t) p * (size_t) bins);
            std::copy(ti.begin(), ti.begin() + bins, Him.begin() + (size_t) p * (size_t) bins);
        }
        prev.assign((size_t) B, 0.0);
        cur.assign((size_t) B, 0.0);
        outCur.assign((size_t) B, 0.0);
        outNext.assign((size_t) B, 0.0);
        accRe.assign((size_t) bins, 0.0);
        accIm.assign((size_t) bins, 0.0);
        tmpRe.assign((size_t) N, 0.0);
        tmpIm.assign((size_t) N, 0.0);
    }

    void reset() noexcept
    {
        std::fill(Xre.begin(), Xre.end(), 0.0);
        std::fill(Xim.begin(), Xim.end(), 0.0);
        std::fill(prev.begin(), prev.end(), 0.0);
        std::fill(cur.begin(), cur.end(), 0.0);
        std::fill(outCur.begin(), outCur.end(), 0.0);
        std::fill(outNext.begin(), outNext.end(), 0.0);
        pos = 0;
        newest = 0;
        phase = Phase::idle;
        step = unitsDone = 0;
    }

    // Adds this stage's output for in[0..n) to out.
    void add(const double* in, double* out, int n) noexcept
    {
        int i = 0;
        while (i < n)
        {
            const int c = std::min(n - i, B - pos);
            for (int k = 0; k < c; ++k)
            {
                cur[(size_t) (pos + k)] = in[i + k];
                out[i + k] += outCur[(size_t) (pos + k)];
            }
            pos += c;
            i += c;
            if (slack > 0)
                work(false);
            if (pos == B)
                boundary();
        }
    }

private:
    void mac(int p) noexcept
    {
        const int x = (newest - p + P) % P;
        const double* hr = Hre.data() + (size_t) p * (size_t) bins;
        const double* hi = Him.data() + (size_t) p * (size_t) bins;
        const double* xr = Xre.data() + (size_t) x * (size_t) bins;
        const double* xi = Xim.data() + (size_t) x * (size_t) bins;
        for (int k = 0; k < bins; ++k)
        {
            accRe[(size_t) k] += hr[k] * xr[k] - hi[k] * xi[k];
            accIm[(size_t) k] += hr[k] * xi[k] + hi[k] * xr[k];
        }
    }

    void spectrumToTmp() noexcept
    {
        for (int k = 0; k < bins; ++k)
        {
            tmpRe[(size_t) k] = accRe[(size_t) k];
            tmpIm[(size_t) k] = accIm[(size_t) k];
        }
        for (int k = 1; k < B; ++k)
        {
            tmpRe[(size_t) (N - k)] = accRe[(size_t) k];
            tmpIm[(size_t) (N - k)] = -accIm[(size_t) k];
        }
    }

    void storeSpectrum() noexcept
    {
        newest = (newest + 1) % P;
        std::copy(tmpRe.begin(), tmpRe.begin() + bins, Xre.begin() + (size_t) newest * (size_t) bins);
        std::copy(tmpIm.begin(), tmpIm.begin() + bins, Xim.begin() + (size_t) newest * (size_t) bins);
        std::fill(accRe.begin(), accRe.end(), 0.0);
        std::fill(accIm.begin(), accIm.end(), 0.0);
    }

    // Gradual work (slack stage): forward FFT passes, partition products, inverse FFT passes,
    // spread evenly so that everything is finished by 3/4 of the block.
    void work(bool finish) noexcept
    {
        if (phase == Phase::idle) return;
        const int passes = fft.numPasses();
        const int total = 2 * passes + P;
        const int span = (3 * B) / 4;
        const int target = finish ? total : std::min(total, (total * pos + span - 1) / span);
        while (unitsDone < target && phase != Phase::idle)
        {
            switch (phase)
            {
                case Phase::forward:
                    fft.pass(tmpRe.data(), tmpIm.data(), step++, false);
                    if (step == passes) { storeSpectrum(); phase = Phase::products; step = 0; }
                    break;
                case Phase::products:
                    mac(step++);
                    if (step == P) { spectrumToTmp(); phase = Phase::inverse; step = 0; }
                    break;
                case Phase::inverse:
                    fft.pass(tmpRe.data(), tmpIm.data(), step++, true);
                    if (step == passes)
                    {
                        std::copy(tmpRe.begin() + B, tmpRe.end(), outNext.begin());
                        phase = Phase::idle;
                    }
                    break;
                case Phase::idle: break;
            }
            ++unitsDone;
        }
    }

    void boundary() noexcept
    {
        if (slack > 0)
        {
            work(true);
            std::swap(outCur, outNext); // computed during this block for the next one
        }
        // spectrum of [previous block, this block]
        std::copy(prev.begin(), prev.end(), tmpRe.begin());
        std::copy(cur.begin(), cur.end(), tmpRe.begin() + B);
        std::fill(tmpIm.begin(), tmpIm.end(), 0.0);
        std::swap(prev, cur);
        pos = 0;
        if (slack == 0)
        {
            fft.forward(tmpRe.data(), tmpIm.data());
            storeSpectrum();
            for (int p = 0; p < P; ++p) mac(p);
            spectrumToTmp();
            fft.inverse(tmpRe.data(), tmpIm.data());
            std::copy(tmpRe.begin() + B, tmpRe.end(), outCur.begin());
        }
        else
        {
            phase = Phase::forward;
            step = 0;
            unitsDone = 0;
        }
    }

    int B, N, bins, P = 1, slack = 0;
    Fft fft;
    std::vector<double> Hre, Him, Xre, Xim, prev, cur, outCur, outNext, accRe, accIm, tmpRe, tmpIm;
    enum class Phase { idle, forward, products, inverse };
    Phase phase = Phase::idle;
    int pos = 0, newest = 0, step = 0, unitsDone = 0;
};

// ---------------------------------------------------------------------------------------------
Convolver::Convolver(std::vector<double> ir) : length((int) ir.size())
{
    head.assign((size_t) kHead, 0.0);
    hist.assign((size_t) (2 * kHead), 0.0);
    for (int i = 0; i < std::min(length, kHead); ++i)
        head[(size_t) (kHead - 1 - i)] = ir[(size_t) i];
    if (length > kHead)
        small = std::make_unique<Stage>(ir.data(), std::min(length, kSmallEnd), kHead, kSmallBlock);
    if (length > kSmallEnd)
        large = std::make_unique<Stage>(ir.data(), length, kSmallEnd, kLargeBlock);
    reset();
}

Convolver::~Convolver() = default;

void Convolver::reset() noexcept
{
    std::fill(hist.begin(), hist.end(), 0.0);
    histPos = 0;
    if (small) small->reset();
    if (large) large->reset();
}

void Convolver::process(const double* in, double* out, int n) noexcept
{
    if (length == 0)
    {
        if (out != in) std::copy(in, in + n, out);
        return;
    }
    // The stages read `in` before `out` is written for the same samples, so do them in chunks
    // through a small local buffer (out may alias in).
    constexpr int kChunk = 64;
    double x[kChunk], y[kChunk];
    for (int start = 0; start < n; start += kChunk)
    {
        const int c = std::min(kChunk, n - start);
        std::copy(in + start, in + start + c, x);
        for (int i = 0; i < c; ++i)
        {
            histPos = (histPos + 1) % kHead;
            hist[(size_t) histPos] = x[i];
            hist[(size_t) (histPos + kHead)] = x[i];
            const double* w = hist.data() + histPos + 1; // oldest .. newest
            double acc = 0.0;
            for (int k = 0; k < kHead; ++k) acc += head[(size_t) k] * w[k];
            y[i] = acc;
        }
        if (small) small->add(x, y, c);
        if (large) large->add(x, y, c);
        std::copy(y, y + c, out + start);
    }
}

// ---------------------------------------------------------------------------------------------
namespace
{
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-15 * sum) break;
    }
    return sum;
}
} // namespace

PreparedIr prepareImpulseResponse(const std::vector<double>& x, double fileRate, double targetRate, double maxSeconds)
{
    PreparedIr r;
    if (x.empty() || fileRate <= 0.0 || targetRate <= 0.0)
        return r;

    // 1. leading silence (below -60 dB of the peak) is pure delay: remove it, keep 2 samples
    double peak = 0.0;
    for (double v : x) peak = std::max(peak, std::abs(v));
    if (peak <= 0.0)
        return r;
    int first = 0;
    while (first < (int) x.size() && std::abs(x[(size_t) first]) < peak * 1e-3) ++first;
    first = std::max(0, first - 2);
    r.trimmedSamples = first;
    std::vector<double> src(x.begin() + first, x.end());

    // 2. resample (Kaiser-windowed sinc, band-limited to the lower of the two rates)
    if (std::abs(fileRate - targetRate) > 0.5)
    {
        r.resampled = true;
        const double ratio = targetRate / fileRate;
        const double cutoff = 0.97 * std::min(1.0, ratio);       // in units of the input Nyquist
        const double halfWidth = 24.0 / std::min(1.0, ratio);    // input samples
        const double beta = 9.0, i0b = besselI0(beta);
        const int outLen = (int) std::ceil((double) src.size() * ratio);
        std::vector<double> y((size_t) outLen, 0.0);
        for (int m = 0; m < outLen; ++m)
        {
            const double t = m / ratio;
            const int k0 = (int) std::ceil(t - halfWidth), k1 = (int) std::floor(t + halfWidth);
            double acc = 0.0;
            for (int k = std::max(0, k0); k <= std::min((int) src.size() - 1, k1); ++k)
            {
                const double d = t - k;
                const double a = kPi * cutoff * d;
                const double sinc = std::abs(a) < 1e-12 ? 1.0 : std::sin(a) / a;
                const double u = d / halfWidth;
                const double win = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0b;
                acc += src[(size_t) k] * cutoff * sinc * win;
            }
            y[(size_t) m] = acc;
        }
        src.swap(y);
    }

    // 3. limit the length (a short fade-out avoids a hard cut)
    const int maxLen = (int) std::lround(maxSeconds * targetRate);
    if ((int) src.size() > maxLen)
    {
        src.resize((size_t) maxLen);
        r.truncated = true;
        const int fade = std::min(maxLen, (int) std::lround(0.010 * targetRate));
        for (int i = 0; i < fade; ++i)
            src[(size_t) (maxLen - fade + i)] *= 0.5 * (1.0 + std::cos(kPi * (i + 1) / fade));
    }

    // 4. unit energy
    double e = 0.0;
    for (double v : src) e += v * v;
    if (e > 0.0)
    {
        const double g = 1.0 / std::sqrt(e);
        for (double& v : src) v *= g;
    }
    r.samples = std::move(src);
    return r;
}

} // namespace ampsurd

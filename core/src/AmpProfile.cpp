#include "ampsurd/AmpProfile.h"

#include "ampsurd/CaptureAnalyzer.h" // fft

#include <algorithm>
#include <cmath>
#include <complex>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<double> oneSweep(double peakDb)
{
    const double fs = ToneSweep::kRate, T = ToneSweep::kSeconds;
    const int n = (int) (T * fs);
    const double L = T / std::log(ToneSweep::kF2 / ToneSweep::kF1);
    const int fade = (int) (0.05 * fs);
    const double g = std::pow(10.0, peakDb / 20.0);
    std::vector<double> x((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        double w = 1.0;
        if (i < fade) w = 0.5 - 0.5 * std::cos(kPi * i / fade);
        else if (i >= n - fade) w = 0.5 - 0.5 * std::cos(kPi * (n - 1 - i) / fade);
        x[(size_t) i] = std::sin(2.0 * kPi * ToneSweep::kF1 * L * (std::exp(t / L) - 1.0)) * w * g;
    }
    return x;
}

// |H|^2 = |Y|^2 / |X|^2 of one sweep inside the recording, averaged over 1/6-octave bands around the grid
std::vector<double> sweepResponseDb(const std::vector<double>& rec, double fs, double sweepAt, double peakDb)
{
    const int N = 1 << 20; // 21.8 s at 48 kHz: one sweep (10 s) + latency + the amp's decay
    const int guard = (int) (0.05 * fs);
    const int from = std::max(0, (int) (sweepAt * fs) - guard);
    const int len = std::min((int) (12.5 * fs), (int) rec.size() - from); // never reaches the other sweep
    std::vector<std::complex<double>> Y((size_t) N), X((size_t) N);
    for (int i = 0; i < len; ++i) Y[(size_t) i] = rec[(size_t) (from + i)];
    const auto x = oneSweep(peakDb);
    const int xOff = (int) (sweepAt * fs) - from;
    for (size_t i = 0; i < x.size() && (int) i + xOff < N; ++i) X[i + (size_t) xOff] = x[i];
    CaptureAnalyzer::fft(Y, false);
    CaptureAnalyzer::fft(X, false);
    const auto& grid = ToneSweep::gridHz();
    std::vector<double> db(grid.size());
    const double binHz = fs / N, half = std::pow(2.0, 1.0 / 12.0); // +-1/12 octave = a 1/6-octave band
    for (size_t g = 0; g < grid.size(); ++g)
    {
        const int b0 = std::max(1, (int) std::floor(grid[g] / half / binHz)), b1 = std::min(N / 2, (int) std::ceil(grid[g] * half / binHz));
        double num = 0.0, den = 0.0;
        for (int b = b0; b <= b1; ++b) { num += std::norm(Y[(size_t) b]); den += std::norm(X[(size_t) b]); }
        db[g] = 10.0 * std::log10((num + 1e-30) / (den + 1e-30));
    }
    return db;
}
} // namespace

std::vector<double> ToneSweep::make()
{
    std::vector<double> y((size_t) (kLength * kRate), 0.0);
    const auto a = oneSweep(kLoudDb), b = oneSweep(kQuietDb);
    std::copy(a.begin(), a.end(), y.begin() + (long) (kLoudAt * kRate));
    std::copy(b.begin(), b.end(), y.begin() + (long) (kQuietAt * kRate));
    return y;
}

const std::vector<double>& ToneSweep::gridHz()
{
    static const std::vector<double> g = [] {
        std::vector<double> v;
        for (double f = 20.0; f <= 20000.0 * 1.0001; f *= std::pow(2.0, 1.0 / 6.0)) v.push_back(f);
        return v;
    }();
    return g;
}

bool ToneSweep::analyse(const std::vector<double>& rec, double fs, Response& out, std::string& error)
{
    if (std::abs(fs - kRate) > 0.5) { error = "The recording must be at 48 kHz (as the sweep file)."; return false; }
    if (rec.size() < (size_t) ((kQuietAt + kSeconds + 0.5) * fs)) { error = "The recording is too short: record the whole 27 s of the sweep file."; return false; }
    double sum = 0.0;
    for (double v : rec) sum += v * v;
    out.levelDb = 10.0 * std::log10(sum / (double) rec.size() + 1e-30);
    if (out.levelDb < -90.0) { error = "The recording is silent."; return false; }
    const auto loud = sweepResponseDb(rec, fs, kLoudAt, kLoudDb), quiet = sweepResponseDb(rec, fs, kQuietAt, kQuietDb);
    // a clean amp: both sweeps give the same shape (the quiet one 0 dB after its own level is taken out)
    const auto& grid = gridHz();
    std::vector<double> d;
    for (size_t g = 0; g < grid.size(); ++g)
        if (grid[g] >= 100.0 && grid[g] <= 8000.0) d.push_back(loud[g] - quiet[g]);
    std::vector<double> sorted = d;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
    double worst = 0.0;
    for (double v : d) worst = std::max(worst, std::abs(v - median));
    out.loudQuietDiffDb = std::max(worst, std::abs(median)); // shape AND level (a compressing amp keeps the shape)
    out.usedQuiet = out.loudQuietDiffDb > 1.0;
    out.db = out.usedQuiet ? quiet : loud;
    return true;
}

std::vector<double> ToneStack::curveDb(double bass, double mid, double treble) const
{
    const auto& grid = ToneSweep::gridHz();
    std::vector<double> sum(grid.size(), 0.0);
    const double pos[3] { bass, mid, treble };
    for (int k = 0; k < 3; ++k)
    {
        const auto& pts = knobs[(size_t) k];
        if (pts.empty()) continue;
        const double p = std::clamp(pos[k], 0.0, 10.0);
        // between the two measured positions around p (linear in dB); outside: the nearest one
        size_t i = 0;
        while (i + 1 < pts.size() && pts[i + 1].first <= p) ++i;
        if (p <= pts.front().first || pts.size() == 1)
        {
            for (size_t g = 0; g < grid.size(); ++g) sum[g] += pts.front().second[g];
            continue;
        }
        if (i + 1 >= pts.size())
        {
            for (size_t g = 0; g < grid.size(); ++g) sum[g] += pts.back().second[g];
            continue;
        }
        const double t = (p - pts[i].first) / std::max(1e-9, pts[i + 1].first - pts[i].first);
        for (size_t g = 0; g < grid.size(); ++g) sum[g] += (1.0 - t) * pts[i].second[g] + t * pts[i + 1].second[g];
    }
    return sum;
}

bool ToneStack::isFlat(const std::vector<double>& gridDb, double tol)
{
    for (double v : gridDb) if (std::abs(v) > tol) return false;
    return true;
}

std::vector<double> ToneStack::minPhaseFir(const std::vector<double>& gridDb, double fs, int length)
{
    const auto& grid = ToneSweep::gridHz();
    const int N = std::max(4096, length * 4); // cepstrum FFT (zero-padded: less aliasing)
    std::vector<std::complex<double>> a((size_t) N);
    std::vector<double> lg(grid.size());
    for (size_t g = 0; g < grid.size(); ++g) lg[g] = std::log2(grid[g]);
    for (int i = 0; i <= N / 2; ++i)
    {
        const double f = std::max(1.0, (double) i * fs / N);
        const double l = std::log2(f);
        double db;
        if (l <= lg.front()) db = gridDb.front();
        else if (l >= lg.back()) db = gridDb.back();
        else
        {
            const size_t j = (size_t) (std::upper_bound(lg.begin(), lg.end(), l) - lg.begin()) - 1;
            const double t = (l - lg[j]) / (lg[j + 1] - lg[j]);
            db = gridDb[j] + t * (gridDb[j + 1] - gridDb[j]);
        }
        a[(size_t) i] = db / 20.0 * std::log(10.0); // ln |H|
    }
    for (int i = N / 2 + 1; i < N; ++i) a[(size_t) i] = a[(size_t) (N - i)];
    CaptureAnalyzer::fft(a, true);
    for (int i = 0; i < N; ++i)
    {
        const double w = i == 0 || i == N / 2 ? 1.0 : i < N / 2 ? 2.0 : 0.0;
        a[(size_t) i] = a[(size_t) i].real() * w;
    }
    CaptureAnalyzer::fft(a, false);
    for (auto& v : a) v = std::exp(v);
    CaptureAnalyzer::fft(a, true);
    std::vector<double> h((size_t) length);
    const int fade = length / 8;
    for (int i = 0; i < length; ++i)
    {
        double w = 1.0;
        if (i >= length - fade) w = 0.5 + 0.5 * std::cos(kPi * (i - (length - fade)) / fade);
        h[(size_t) i] = a[(size_t) i].real() * w;
    }
    return h;
}

} // namespace ampsurd

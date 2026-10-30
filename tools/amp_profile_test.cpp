// amp_profile_test: AMPSURD amp profiles (.ampsurdamp) - the parts in core:
//   amp_profile_test <a.nam> <b.nam>
// 1. ToneSweep::make() = the sweep file sent to the owner (tests/data/AMPSURD_tone_sweep_48k.wav)
// 2. analysis of synthetic recordings: a known tone control (shelves / bell) through a clean "amp" with latency and
//    noise is measured back within 0.3 dB; a distorting amp -> the quiet sweep is used
// 3. ToneStack: interpolation between measured positions; the minimum-phase FIR has the wanted response
// 4. GAIN morph: on a step = that capture exactly; halfway = the average of the two; level / polarity applied
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "ToolUtils.h"
#include "ampsurd/AmpProfile.h"
#include "ampsurd/CaptureModel.h"

using namespace ampsurd;
static int failures = 0;
static void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// RBJ biquad, applied in place
static void biquad(std::vector<double>& x, const char* kind, double f0, double gainDb, double q, double fs)
{
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * M_PI * f0 / fs, a = std::sin(w) / (2 * q), c = std::cos(w);
    double b0, b1, b2, a0, a1, a2;
    const double sq = 2 * std::sqrt(A) * a;
    if (std::string(kind) == "ls") { b0 = A*((A+1)-(A-1)*c+sq); b1 = 2*A*((A-1)-(A+1)*c); b2 = A*((A+1)-(A-1)*c-sq); a0 = (A+1)+(A-1)*c+sq; a1 = -2*((A-1)+(A+1)*c); a2 = (A+1)+(A-1)*c-sq; }
    else if (std::string(kind) == "hs") { b0 = A*((A+1)+(A-1)*c+sq); b1 = -2*A*((A-1)+(A+1)*c); b2 = A*((A+1)+(A-1)*c-sq); a0 = (A+1)-(A-1)*c+sq; a1 = 2*((A-1)-(A+1)*c); a2 = (A+1)-(A-1)*c-sq; }
    else { b0 = 1 + a * A; b1 = -2 * c; b2 = 1 - a * A; a0 = 1 + a / A; a1 = -2 * c; a2 = 1 - a / A; }
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (auto& v : x)
    {
        const double y = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = v; y2 = y1; y1 = y; v = y;
    }
}
static double biquadDb(const char* kind, double f0, double gainDb, double q, double fs, double f)
{
    std::vector<double> imp(16384, 0.0);
    imp[0] = 1.0;
    biquad(imp, kind, f0, gainDb, q, fs);
    double re = 0, im = 0;
    for (size_t n = 0; n < imp.size(); ++n) { re += imp[n] * std::cos(2 * M_PI * f * n / fs); im -= imp[n] * std::sin(2 * M_PI * f * n / fs); }
    return 10 * std::log10(re * re + im * im);
}

int main(int argc, char** argv)
{
    const double fs = 48000.0;
    // 1. the sweep file
    {
        std::vector<float> file;
        double sr = 0;
        const auto mine = ToneSweep::make();
        const bool read = tools::loadWav(std::string(AMPSURD_SOURCE_DIR) + "/tests/data/AMPSURD_tone_sweep_48k.wav", file, sr);
        double d = read && file.size() == mine.size() ? 0.0 : 1.0;
        for (size_t i = 0; read && i < std::min(file.size(), mine.size()); ++i) d = std::max(d, std::abs((double) file[i] - mine[i]));
        check(read && sr == fs && d < 2e-7, "ToneSweep::make() = the sweep file sent to the owner (difference " + std::to_string(d) + ")");
    }
    // 2. synthetic recordings
    const auto sweep = ToneSweep::make();
    std::mt19937 rng(1);
    std::normal_distribution<double> noise(0.0, 1e-5); // about -100 dBFS
    auto record = [&](auto&& filter, bool distort) {
        std::vector<double> y(sweep.size() + 4000, 0.0);
        std::vector<double> x(sweep.begin(), sweep.end());
        x.resize(y.size(), 0.0);
        filter(x);
        for (size_t i = 0; i < y.size(); ++i)
        {
            double v = i >= 333 ? x[i - 333] : 0.0; // latency 333 samples
            if (distort) v = std::tanh(v * 12.0) / 12.0 * 3.0;
            y[i] = 0.5 * v + noise(rng);
        }
        return y;
    };
    ToneSweep::Response ref, bassMax, dist;
    std::string err;
    const bool okRef = ToneSweep::analyse(record([](std::vector<double>&) {}, false), fs, ref, err);
    const bool okBass = ToneSweep::analyse(record([&](std::vector<double>& x) { biquad(x, "ls", 150, 8, 0.7, fs); biquad(x, "pk", 600, -5, 1.0, fs); }, false), fs, bassMax, err);
    double worst = 0;
    const auto& grid = ToneSweep::gridHz();
    for (size_t g = 0; g < grid.size(); ++g)
        if (grid[g] >= 40 && grid[g] <= 15000)
        {
            const double want = biquadDb("ls", 150, 8, 0.7, fs, grid[g]) + biquadDb("pk", 600, -5, 1.0, fs, grid[g]);
            worst = std::max(worst, std::abs((bassMax.db[g] - ref.db[g]) - want));
        }
    check(okRef && okBass && !bassMax.usedQuiet && worst < 0.3, "ANALYSIS: a known tone curve is measured back (worst " + std::to_string(worst) + " dB, 40 Hz - 15 kHz)");
    const bool okDist = ToneSweep::analyse(record([](std::vector<double>&) {}, true), fs, dist, err);
    check(okDist && dist.usedQuiet, "ANALYSIS: a distorting amp -> the quiet sweep is used (shapes differ " + std::to_string(dist.loudQuietDiffDb) + " dB)");
    std::vector<double> shortRec(48000, 0.1);
    ToneSweep::Response tmp;
    check(!ToneSweep::analyse(shortRec, fs, tmp, err) && !ToneSweep::analyse(sweep, 44100.0, tmp, err), "ANALYSIS: too short / wrong rate refused");

    // 3. tone stack
    {
        ToneStack ts;
        std::vector<double> zero(grid.size(), 0.0), plus(grid.size()), minus(grid.size());
        for (size_t g = 0; g < grid.size(); ++g) { plus[g] = biquadDb("ls", 150, 8, 0.7, fs, grid[g]); minus[g] = -plus[g]; }
        ts.knobs[0] = { { 0.0, minus }, { 5.0, zero }, { 10.0, plus } };
        const auto noon = ts.curveDb(5, 5, 5), at75 = ts.curveDb(7.5, 5, 5), at10 = ts.curveDb(10, 5, 5);
        double e1 = 0, e2 = 0, e3 = 0;
        for (size_t g = 0; g < grid.size(); ++g) { e1 = std::max(e1, std::abs(noon[g])); e2 = std::max(e2, std::abs(at75[g] - 0.5 * plus[g])); e3 = std::max(e3, std::abs(at10[g] - plus[g])); }
        check(e1 < 1e-9 && e2 < 1e-9 && e3 < 1e-9 && ToneStack::isFlat(noon), "TONE STACK: noon = flat, between positions = interpolated, max = measured");
        const auto fir = ToneStack::minPhaseFir(at10, fs, 1024);
        double fw = 0;
        for (size_t g = 0; g < grid.size(); ++g)
            if (grid[g] >= 60 && grid[g] <= 16000)
            {
                double re = 0, im = 0;
                for (size_t n = 0; n < fir.size(); ++n) { re += fir[n] * std::cos(2 * M_PI * grid[g] * n / fs); im -= fir[n] * std::sin(2 * M_PI * grid[g] * n / fs); }
                fw = std::max(fw, std::abs(10 * std::log10(re * re + im * im) - at10[g]));
            }
        const auto flatFir = ToneStack::minPhaseFir(noon, fs, 1024);
        double unit = std::abs(flatFir[0] - 1.0);
        for (size_t n = 1; n < flatFir.size(); ++n) unit = std::max(unit, std::abs(flatFir[n]));
        check(fw < 0.5 && unit < 1e-6, "TONE STACK: minimum-phase FIR = the curve (worst " + std::to_string(fw) + " dB, 60 Hz - 16 kHz); noon = a single 1.0");
    }

    // 4. GAIN morph
    if (argc >= 3)
    {
        int lo = 0;
        double w = 0;
        std::vector<double> knobs { 2, 3, 4, 5, 6, 7, 8 };
        CaptureModel::morphPair(knobs, 2.0, lo, w); const bool p1 = lo == 0 && w == 0.0;
        CaptureModel::morphPair(knobs, 4.25, lo, w); const bool p2 = lo == 2 && std::abs(w - 0.25) < 1e-12;
        CaptureModel::morphPair(knobs, 9.0, lo, w); const bool p3 = lo == 5 && w == 1.0;
        CaptureModel::morphPair(knobs, 1.0, lo, w); const bool p4 = lo == 0 && w == 0.0;
        check(p1 && p2 && p3 && p4, "MORPH: the two captures around the GAIN position and their weights");

        auto load = [&](const char* f) { return std::move(CaptureModel::load(f, fs, 256).model); };
        auto render = [&](CaptureModel& m, double pos, const std::vector<double>& x) {
            m.setMorphPosition(pos);
            std::vector<double> y(x.size());
            for (size_t i = 0; i < x.size(); i += 256) m.process(x.data() + i, y.data() + i, (int) std::min<size_t>(256, x.size() - i), false);
            return y;
        };
        std::vector<double> x(48000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.2 * std::sin(2 * M_PI * 110 * i / fs) * std::exp(-(double) (i % 12000) / 6000.0);
        auto a = load(argv[1]), b = load(argv[2]);
        const auto ya = render(*a, 0, x), yb = render(*b, 0, x);
        std::vector<CaptureModel::MorphStep> steps;
        steps.push_back({ load(argv[1]), 2.0, 1.0, 1.0, 0 });
        steps.push_back({ load(argv[2]), 8.0, 0.5, -1.0, 0 });
        CaptureInfo info;
        info.displayName = "test amp";
        auto m = CaptureModel::makeMorph(std::move(steps), info, fs, 256);
        double dA = 0, dB = 0, dH = 0;
        auto settle = [&](double pos) { render(*m, pos, std::vector<double>(48000, 0.0)); m->prepare(fs, 256); m->setMorphPosition(pos); };
        settle(2.0);
        const auto mA = render(*m, 2.0, x);
        settle(8.0);
        const auto mB = render(*m, 8.0, x);
        settle(5.0);
        const auto mH = render(*m, 5.0, x);
        for (size_t i = 0; i < x.size(); ++i)
        {
            dA = std::max(dA, std::abs(mA[i] - ya[i]));
            dB = std::max(dB, std::abs(mB[i] - (-0.5 * yb[i])));
            dH = std::max(dH, std::abs(mH[i] - 0.5 * (ya[i] - 0.5 * yb[i])));
        }
        check(dA < 1e-6 && dB < 1e-6, "MORPH: on a step = that capture exactly (with its level and polarity)");
        check(dH < 1e-6, "MORPH: halfway = the average of the two (difference " + std::to_string(dH) + ", float rounding inside NAM)");
        // gliding across the whole range: no jumps
        std::vector<double> sweepX(96000);
        for (size_t i = 0; i < sweepX.size(); ++i) sweepX[i] = 0.2 * std::sin(2 * M_PI * 220 * i / fs);
        m->prepare(fs, 256);
        double maxStep = 0, ref = 0;
        std::vector<double> y(256), prev;
        double last = 0;
        for (size_t i = 0; i < sweepX.size(); i += 256)
        {
            m->setMorphPosition(2.0 + 6.0 * (double) i / sweepX.size());
            m->process(sweepX.data() + i, y.data(), 256, false);
            for (int k = 0; k < 256; ++k) { if (i > 4800) maxStep = std::max(maxStep, std::abs(y[k] - last)); last = y[k]; }
        }
        // reference: the largest sample step of the same tone at GAIN 2 and at GAIN 8 (held)
        for (double pos : { 2.0, 8.0 })
        {
            m->prepare(fs, 256);
            last = 0;
            for (size_t i = 0; i < sweepX.size(); i += 256)
            {
                m->setMorphPosition(pos);
                m->process(sweepX.data() + i, y.data(), 256, false);
                for (int k = 0; k < 256; ++k) { if (i > 4800) ref = std::max(ref, std::abs(y[k] - last)); last = y[k]; }
            }
        }
        check(maxStep < 1.5 * ref, "MORPH: turning GAIN across all steps while playing: no jumps (largest step " + std::to_string(maxStep)
                                       + " vs " + std::to_string(ref) + " held)");
    }
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}

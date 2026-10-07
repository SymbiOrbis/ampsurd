// engine_test: objective tests of the AMPSURD engine building blocks.
//
// Usage: engine_test <captureA.nam> <captureB.nam>
// Exit code 0 = all pass.

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Engine.h"
#include "ampsurd/ParametricEq.h"
#include "ampsurd/PathAligner.h"

using namespace ampsurd;
static const double kPi = 3.14159265358979323846;
static int failures = 0;

static void check(bool ok, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0)
{
    std::printf("[%s] ", ok ? "PASS" : "FAIL");
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    failures += ok ? 0 : 1;
}

// amplitude + phase of a sine at f in y (least squares over the second half)
static void fitSine(const std::vector<double>& y, double f, double sr, double& amp, double& phase)
{
    double s = 0, c = 0;
    const size_t start = y.size() / 2, n = y.size() - start;
    for (size_t i = start; i < y.size(); ++i)
    {
        const double t = 2 * kPi * f * (double) i / sr;
        s += y[i] * std::sin(t);
        c += y[i] * std::cos(t);
    }
    s *= 2.0 / (double) n; c *= 2.0 / (double) n;
    amp = std::hypot(s, c);
    phase = std::atan2(c, s); // y ~ amp * sin(t + phase)
}

static std::vector<double> sine(double f, double sr, size_t n)
{
    std::vector<double> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = 0.5 * std::sin(2 * kPi * f * (double) i / sr);
    return x;
}

static double wrapDeg(double d) { while (d > 180) d -= 360; while (d < -180) d += 360; return d; }

static double power(const std::vector<double>& y, size_t from = 0)
{
    double p = 0;
    for (size_t i = from; i < y.size(); ++i) p += y[i] * y[i];
    return p / (double) (y.size() - from);
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "Usage: engine_test <A.nam> <B.nam>\n"); return 2; }
    const double sr = 48000.0;

    // ---------------- 1. Parametric EQ ----------------
    {
        auto bands = ParametricEq::defaultBands();
        bands[2] = { 250.0f, 9.0f, 2.0f };
        bands[6] = { 2000.0f, -12.0f, 0.7f };
        bands[8] = { 8000.0f, 6.0f, 1.0f };
        bands[0] = { 100.0f, 0.0f, 0.707f };  // LOW CUT at 100 Hz
        bands[9] = { 6000.0f, 0.0f, 1.2f };   // HIGH CUT at 6 kHz
        double worst = 0;
        for (double f : { 40.0, 100.0, 250.0, 700.0, 2000.0, 5000.0, 8000.0, 12000.0 })
        {
            ParametricEq eq; eq.prepare(sr); eq.setBands(bands); eq.reset();
            auto x = sine(f, sr, 24000);
            eq.process(x.data(), (int) x.size());
            double a, ph; fitSine(x, f, sr, a, ph);
            const double meas = 20 * std::log10(a / 0.5), expect = ParametricEq::magnitudeDb(bands, f, sr);
            worst = std::max(worst, std::abs(meas - expect));
        }
        check(worst < 0.05, "EQ: bells + low cut + high cut: measured response matches the drawn curve (worst error %.3f dB)", worst);

        ParametricEq eq; eq.prepare(sr);
        auto x = sine(440, sr, 4800), y = x;
        eq.process(y.data(), (int) y.size());
        check(x == y, "EQ: default settings (cuts at their end stops, bells flat) -> output bit-identical to input");
        {
            auto b2 = ParametricEq::defaultBands();
            b2[0].freqHz = 100.0f;
            const double at50 = ParametricEq::magnitudeDb(b2, 50.0, sr), at25 = ParametricEq::magnitudeDb(b2, 25.0, sr);
            const double up = ParametricEq::magnitudeDb(b2, 2000.0, sr);
            check(at50 < -11.0 && at25 - at50 < -10.0 && std::abs(up) < 0.05,
                  "EQ: low cut never boosts above its corner and falls 12 dB/octave (-%.1f dB at 50 Hz, %.1f dB/oct, %.2f dB at 2 kHz)",
                  -at50, at25 - at50, up);
        }
    }

    // ---------------- 2. Fractional delay ----------------
    {
        const double D = 37.3;
        double worstPh = 0, worstMag = 0;
        for (double f : { 100.0, 1000.0, 5000.0, 12000.0, 18000.0 })
        {
            PathAligner al; al.prepare(sr, 400); al.setTargets(D, 1.0, 0.0, 0.0); al.snapToTargets();
            auto x = sine(f, sr, 24000);
            al.process(x.data(), (int) x.size());
            double a, ph; fitSine(x, f, sr, a, ph);
            const double expectPh = -360.0 * f * (D + PathAligner::kBaseLatency) / sr;
            worstPh = std::max(worstPh, std::abs(wrapDeg(ph * 180 / kPi - expectPh)));
            worstMag = std::max(worstMag, std::abs(20 * std::log10(a / 0.5)));
        }
        check(worstPh < 1.0 && worstMag < 0.2,
              "TIME: 37.3-sample delay accurate 100 Hz-18 kHz (phase error %.3f deg, level error %.3f dB)", worstPh, worstMag);
    }

    // ---------------- 3. Phase rotation ----------------
    {
        double worstPh = 0, worstMag = 0;
        for (double f : { 30.0, 100.0, 1000.0, 10000.0, 18000.0 })
        {
            PathAligner a0, a90;
            a0.prepare(sr, 64); a90.prepare(sr, 64);
            a0.setTargets(0, 1, 0.0, 1.0); a0.snapToTargets();
            a90.setTargets(0, 1, kPi / 2, 1.0); a90.snapToTargets();
            auto x0 = sine(f, sr, 48000), x1 = x0;
            a0.process(x0.data(), (int) x0.size());
            a90.process(x1.data(), (int) x1.size());
            double m0, p0, m1, p1; fitSine(x0, f, sr, m0, p0); fitSine(x1, f, sr, m1, p1);
            worstPh = std::max(worstPh, std::abs(wrapDeg((p1 - p0) * 180 / kPi - 90.0)));
            worstMag = std::max(worstMag, std::abs(20 * std::log10(m1 / m0)));
        }
        check(worstPh < 2.5 && worstMag < 0.01,
              "PHASE: 90 deg rotation is frequency-independent 30 Hz-18 kHz (error %.2f deg, level %.4f dB)", worstPh, worstMag);
    }

    // ---------------- 4. Auto alignment on a synthetic offset ----------------
    auto loadAt = [&](const char* path) {
        auto r = CaptureModel::load(path, sr, 256);
        if (!r.model) { std::fprintf(stderr, "load failed: %s\n", r.error.c_str()); std::exit(2); }
        return std::move(r.model);
    };
    const auto test = CaptureAnalyzer::makeTestSignal(sr);
    auto mA = loadAt(argv[1]);
    auto mB = loadAt(argv[2]);
    const auto yA = CaptureAnalyzer::render(*mA, test, true);
    const auto yB = CaptureAnalyzer::render(*mB, test, true);
    {
        auto ref = yA, moved = yA;
        PathAligner p0, p1;
        p0.prepare(sr, 400); p1.prepare(sr, 400);
        p0.setTargets(0, 1, 0, 0); p0.snapToTargets();
        p1.setTargets(37.3, -1, 0, 0); p1.snapToTargets();
        p0.process(ref.data(), (int) ref.size());
        p1.process(moved.data(), (int) moved.size());
        const auto a = CaptureAnalyzer::estimateAlignment(*CaptureAnalyzer::measure(ref, sr), *CaptureAnalyzer::measure(moved, sr));
        check(std::abs(a.lagSamples - 37.3) < 0.15 && a.polarity < 0,
              "AUTO: finds a 37.3-sample offset + inverted polarity (found %.2f samples, polarity %+.0f, corr %.3f -> %.3f)",
              a.lagSamples, a.polarity, a.correlationBefore, a.correlationAfter);
    }

    // ---------------- 5. Auto alignment between two real, different captures ----------------
    const auto measA = CaptureAnalyzer::measure(yA, sr), measB = CaptureAnalyzer::measure(yB, sr);
    const auto al = CaptureAnalyzer::estimateAlignment(*measA, *measB);
    std::printf("[INFO] real captures A vs B: offset %.2f samples (%.3f ms), polarity %+.0f, low-band correlation %.3f -> %.3f, %s\n",
                al.lagSamples, al.lagSamples / sr * 1000, al.polarity, al.correlationBefore, al.correlationAfter,
                al.reliable ? "aligned" : "left unaligned (too different)");

    // ---------------- 6. Mix law in the real engine ----------------
    {
        std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> meas {};
        const auto rawA = CaptureAnalyzer::render(*loadAt(argv[1]), test, false);
        const auto rawB = CaptureAnalyzer::render(*loadAt(argv[2]), test, false);
        meas[0] = CaptureAnalyzer::measure(rawA, sr);
        meas[1] = CaptureAnalyzer::measure(rawB, sr);
        const auto rig = CaptureAnalyzer::analyseRig(meas);
        std::array<double, kNumSlots> delays = rig->autoDelay, pol { 1, 1, 1, 1, 1 }, rot {}, lg { 1, 1, 1, 1, 1 };
        for (int i = 0; i < 2; ++i)
        {
            pol[(size_t) i] = rig->align[(size_t) i].polarity;
            lg[(size_t) i] = CaptureAnalyzer::levelMatchGain(rig->loudnessDb[(size_t) i]);
        }
        std::printf("[INFO] measured loudness A %.1f dB, B %.1f dB -> level match %+.1f / %+.1f dB\n",
                    rig->loudnessDb[0], rig->loudnessDb[1], 20 * std::log10(lg[0]), 20 * std::log10(lg[1]));

        double worst = 0;
        struct Case { double wA; double phaseDeg; };
        for (const Case c : { Case { 100, 0 }, Case { 70, 0 }, Case { 50, 0 }, Case { 30, 0 }, Case { 50, 120 } })
        {
            Engine eng;
            eng.getSlot(0).submit(loadAt(argv[1]));
            eng.getSlot(1).submit(loadAt(argv[2]));
            eng.prepare(sr, 256, 10.0); // adopts both without fade

            rot[1] = c.phaseDeg * kPi / 180.0;
            const auto cov = CaptureAnalyzer::covariance(*rig, delays, pol, rot, lg);
            eng.setCovariance(cov.C, cov.valid);

            EngineSettings s;
            s.slots[0].mix = (float) c.wA; s.slots[1].mix = (float) (100.0 - c.wA);
            for (int i = 0; i < 2; ++i)
            {
                s.slots[(size_t) i].delaySamples = delays[(size_t) i];
                s.slots[(size_t) i].polarity = pol[(size_t) i];
                s.slots[(size_t) i].levelGain = lg[(size_t) i];
            }
            s.slots[1].phaseRadians = rot[1];
            s.rotationActive = c.phaseDeg != 0.0;
            if (c.wA == 100.0) s.slots[1].mute = true;

            std::vector<double> out(test.size());
            for (size_t pos = 0; pos < test.size(); pos += 256)
            {
                const int n = (int) std::min<size_t>(256, test.size() - pos);
                eng.process(test.data() + pos, out.data() + pos, n, s);
            }
            // target: percentage-weighted average of the individual (level-matched) loudness = -18 dB
            CaptureAnalyzer::kWeightInPlace(out, sr);
            const double errDb = 10 * std::log10(power(out, 4800)) - CaptureAnalyzer::kTargetLoudnessDb;
            worst = std::max(worst, std::abs(errDb));
            std::printf("[INFO] mix %3.0f/%-3.0f phase %3.0f deg: compensation %+5.2f dB, loudness vs target %+5.2f dB\n",
                        c.wA, 100 - c.wA, c.phaseDeg, eng.getCompensationDb(), errDb);
        }
        check(worst < 0.5, "MIX: real engine output loudness matches the target in every setting (worst %.2f dB)", worst);
    }

    // ---------------- 6b. Frankenstein in the real engine ----------------
    {
        std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> meas {};
        meas[0] = CaptureAnalyzer::measure(CaptureAnalyzer::render(*loadAt(argv[1]), test, false), sr);
        meas[1] = CaptureAnalyzer::measure(CaptureAnalyzer::render(*loadAt(argv[2]), test, false), sr);
        const auto rig = CaptureAnalyzer::analyseRig(meas);
        Engine eng;
        eng.getSlot(0).submit(loadAt(argv[1]));
        eng.getSlot(1).submit(loadAt(argv[2]));
        eng.prepare(sr, 256, 10.0);
        EngineSettings s;
        for (int i = 0; i < 2; ++i)
        {
            s.slots[(size_t) i].delaySamples = rig->autoDelay[(size_t) i];
            s.slots[(size_t) i].polarity = rig->align[(size_t) i].polarity;
            s.slots[(size_t) i].levelGain = CaptureAnalyzer::levelMatchGain(rig->loudnessDb[(size_t) i]);
        }
        s.frankenstein.enabled = true;
        s.frankenstein.sections = 2;
        s.frankenstein.amp = { 0, 1, 0, 0, 0 };
        s.frankenstein.dividerHz[0] = 800.0f;
        s.frankenstein.width = 0.3f;
        std::vector<double> out(test.size());
        bool finite = true;
        for (size_t pos = 0; pos < test.size(); pos += 256)
        {
            const int n = (int) std::min<size_t>(256, test.size() - pos);
            if (pos == 256 * 100) s.frankenstein.enabled = false; // switch off and on while playing
            if (pos == 256 * 140) s.frankenstein.enabled = true;
            eng.process(test.data() + pos, out.data() + pos, n, s);
            for (int k = 0; k < n; ++k) finite = finite && std::isfinite(out[pos + (size_t) k]);
        }
        CaptureAnalyzer::kWeightInPlace(out, sr);
        const double errDb = 10 * std::log10(power(out, 256 * 160)) - CaptureAnalyzer::kTargetLoudnessDb;
        check(finite && std::abs(errDb) < 2.0 && std::abs(eng.getEffectivePercent(0) + eng.getEffectivePercent(1) - 100.0f) < 0.01,
              "FRANKENSTEIN: real captures split at 800 Hz, switched off/on while playing: clean, loudness %+.2f dB vs single-capture level, spectrum shares %.0f%% + %.0f%%",
              errDb, eng.getEffectivePercent(0), eng.getEffectivePercent(1));
    }

    // ---------------- 7. Percentages with mute / solo ----------------
    {
        EngineSettings s;
        std::array<bool, kNumSlots> loadedAll { true, true, true, true, false };
        s.slots[0].mix = 50; s.slots[1].mix = 30; s.slots[2].mix = 20; s.slots[3].mix = 0;
        auto p = Engine::computeProportions(s, loadedAll);
        const bool base = std::abs(p[0] - 0.5) < 1e-9 && std::abs(p[1] - 0.3) < 1e-9 && p[4] == 0.0;
        s.slots[1].mute = true;
        p = Engine::computeProportions(s, loadedAll);
        const bool muted = std::abs(p[0] - 50.0 / 70) < 1e-9 && p[1] == 0 && std::abs(p[2] - 20.0 / 70) < 1e-9;
        s.slots[2].solo = true;
        p = Engine::computeProportions(s, loadedAll);
        const bool solo = p[2] == 1.0 && p[0] == 0.0;
        s.slots[2].solo = false; s.slots[1].mute = false;
        p = Engine::computeProportions(s, loadedAll);
        const bool restored = std::abs(p[0] - 0.5) < 1e-9 && std::abs(p[1] - 0.3) < 1e-9;
        check(base && muted && solo && restored,
              "MUTE/SOLO: percentages renormalise over audible slots and return unchanged afterwards");
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

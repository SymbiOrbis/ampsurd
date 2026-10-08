// engine_test: objective tests of the AMPSURD engine building blocks.
//
// Usage: engine_test <captureA.nam> <captureB.nam>
// Exit code 0 = all pass.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Convolver.h"
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

    // ---------------- 6c. Global EQ on the complete blend ----------------
    {
        auto render = [&](const std::function<void(EngineSettings&, size_t)>& configure) {
            Engine eng;
            eng.getSlot(0).submit(loadAt(argv[1]));
            eng.prepare(sr, 256, 10.0);
            EngineSettings s;
            std::vector<double> out(test.size());
            for (size_t pos = 0; pos < test.size(); pos += 256)
            {
                const int n = (int) std::min<size_t>(256, test.size() - pos);
                configure(s, pos);
                eng.process(test.data() + pos, out.data() + pos, n, s);
            }
            return out;
        };
        auto globalBands = ParametricEq::defaultBands();
        globalBands[0].freqHz = 90.0f;                 // low cut
        globalBands[3] = { 250.0f, -4.0f, 1.0f };
        globalBands[7] = { 4000.0f, 3.0f, 0.8f };
        globalBands[9].freqHz = 8000.0f;               // high cut

        const auto plain = render([](EngineSettings&, size_t) {});
        const auto off = render([&](EngineSettings& s, size_t) { s.globalEq = globalBands; s.globalEqEnabled = false; });
        const auto on = render([&](EngineSettings& s, size_t) { s.globalEq = globalBands; s.globalEqEnabled = true; });
        // reference: the blend without Global EQ, through the (separately verified) EQ in the same blocks
        auto ref = plain;
        {
            ParametricEq eq; eq.prepare(sr);
            for (size_t pos = 0; pos < ref.size(); pos += 256)
            {
                eq.setBands(globalBands);
                eq.process(ref.data() + pos, (int) std::min<size_t>(256, ref.size() - pos));
            }
        }
        double diffOn = 0;
        for (size_t i = 0; i < on.size(); ++i) diffOn = std::max(diffOn, std::abs(on[i] - ref[i]));
        check(off == plain, "GLOBAL EQ: OFF (with bands set) -> output bit-identical to AMPSURD without Global EQ");
        check(diffOn < 1e-12, "GLOBAL EQ: ON -> output = blend through the verified EQ curve (max difference %.1e)", diffOn);

        // switching on/off while playing: no clicks (no sample step larger than in the steady states)
        const auto toggled = render([&](EngineSettings& s, size_t pos) {
            s.globalEq = globalBands;
            s.globalEqEnabled = (pos / (256 * 40)) % 2 == 1;
        });
        auto maxStep = [](const std::vector<double>& y) {
            double m = 0;
            for (size_t i = 2; i < y.size(); ++i) m = std::max(m, std::abs(y[i] - 2 * y[i - 1] + y[i - 2]));
            return m;
        };
        const double steady = std::max(maxStep(plain), maxStep(on)), tog = maxStep(toggled);
        if (std::getenv("AMPSURD_DEBUG"))
        {
            size_t at = 0; double m = 0;
            for (size_t i = 2; i < toggled.size(); ++i)
            {
                const double c = std::abs(toggled[i] - 2 * toggled[i - 1] + toggled[i - 2]);
                if (c > m) { m = c; at = i; }
            }
            std::printf("debug: worst curvature %.4f at sample %zu (block %zu, offset %zu); plain there %.4f, on there %.4f\n", m, at, at / 256, at % 256,
                        std::abs(plain[at] - 2 * plain[at - 1] + plain[at - 2]), std::abs(on[at] - 2 * on[at - 1] + on[at - 2]));
        }
        check(tog <= steady * 1.05, "GLOBAL EQ: switching on/off while playing is click-free (largest curvature %.4f vs %.4f in steady state)", tog, steady);

        // amp EQ OFF must also release that amp's low / high cut (fixed 2026-10-08)
        const auto ampOff = render([](EngineSettings& s, size_t) {
            s.slots[0].eq[0].freqHz = 200.0f;
            s.slots[0].eq[9].freqHz = 3000.0f;
            s.slots[0].eq[4].gainDb = 6.0f;
            s.slots[0].eqEnabled = false;
        });
        check(ampOff == plain, "EQ OFF on an amp: bells AND low/high cut bypassed -> bit-identical to a flat EQ");
    }

    // ---------------- 6d. Per-amp PAN (stereo output) ----------------
    {
        // pan law
        double worstPower = 0;
        for (float pan = -1.0f; pan <= 1.0001f; pan += 0.05f)
        {
            const auto g = Engine::panGains(pan);
            worstPower = std::max(worstPower, std::abs(g[0] * g[0] + g[1] * g[1] - 2.0));
        }
        const auto c = Engine::panGains(0.0f), hl = Engine::panGains(-1.0f), hr = Engine::panGains(1.0f);
        check(c[0] == 1.0 && c[1] == 1.0 && hl[1] == 0.0 && hr[0] < 1e-15 && worstPower < 1e-12,
              "PAN law: centre exactly 0 dB on both sides, hard left/right silent on the other side, constant power (error %.1e)", worstPower);

        std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> meas {};
        meas[0] = CaptureAnalyzer::measure(CaptureAnalyzer::render(*loadAt(argv[1]), test, false), sr);
        meas[1] = CaptureAnalyzer::measure(CaptureAnalyzer::render(*loadAt(argv[2]), test, false), sr);
        const auto rig = CaptureAnalyzer::analyseRig(meas);
        std::array<double, kNumSlots> delays = rig->autoDelay, pol { 1, 1, 1, 1, 1 }, rot {}, lg { 1, 1, 1, 1, 1 };
        for (int i = 0; i < 2; ++i)
        {
            pol[(size_t) i] = rig->align[(size_t) i].polarity;
            lg[(size_t) i] = CaptureAnalyzer::levelMatchGain(rig->loudnessDb[(size_t) i]);
        }
        const auto cov = CaptureAnalyzer::covariance(*rig, delays, pol, rot, lg);

        struct Out { std::vector<double> mono, L, R; };
        auto render = [&](float panA, float panB, bool frank, bool refMuteA = false, bool refMuteB = false) {
            Engine eng, engMono;
            for (auto* e : { &eng, &engMono })
            {
                e->getSlot(0).submit(loadAt(argv[1]));
                e->getSlot(1).submit(loadAt(argv[2]));
                e->prepare(sr, 256, 10.0);
                e->setCovariance(cov.C, cov.valid);
            }
            EngineSettings s;
            s.slots[0].mix = 60; s.slots[1].mix = 40;
            for (int i = 0; i < 2; ++i)
            {
                s.slots[(size_t) i].delaySamples = delays[(size_t) i];
                s.slots[(size_t) i].polarity = pol[(size_t) i];
                s.slots[(size_t) i].levelGain = lg[(size_t) i];
            }
            s.frankenstein.enabled = frank;
            s.frankenstein.amp = { 0, 1, 0, 0, 0 };
            s.frankenstein.dividerHz[0] = 700.0f;
            EngineSettings sMono = s;               // reference: no pan, mono engine call
            sMono.slots[0].mute = refMuteA;
            sMono.slots[1].mute = refMuteB;
            s.slots[0].pan = panA; s.slots[1].pan = panB;
            Out o { std::vector<double>(test.size()), std::vector<double>(test.size()), std::vector<double>(test.size()) };
            for (size_t pos = 0; pos < test.size(); pos += 256)
            {
                const int n = (int) std::min<size_t>(256, test.size() - pos);
                engMono.process(test.data() + pos, o.mono.data() + pos, n, sMono);
                eng.process(test.data() + pos, o.L.data() + pos, o.R.data() + pos, n, s);
            }
            return o;
        };
        auto stereoLoudnessErr = [&](Out o) {
            CaptureAnalyzer::kWeightInPlace(o.L, sr);
            CaptureAnalyzer::kWeightInPlace(o.R, sr);
            return 10 * std::log10(0.5 * (power(o.L, 4800) + power(o.R, 4800))) - CaptureAnalyzer::kTargetLoudnessDb;
        };

        const auto centred = render(0.0f, 0.0f, false);
        check(centred.L == centred.mono && centred.R == centred.mono,
              "PAN centred: left and right bit-identical to the mono engine output (nothing changes until you pan)");

        double worst = 0;
        for (auto pr : { std::pair<float, float> { 0.0f, 0.0f }, { -1.0f, 1.0f }, { -0.5f, 0.5f }, { -1.0f, -1.0f }, { 0.7f, -0.2f } })
        {
            const double e = stereoLoudnessErr(render(pr.first, pr.second, false));
            std::printf("[INFO] pan %+.1f / %+.1f: stereo loudness vs target %+5.2f dB\n", pr.first, pr.second, e);
            worst = std::max(worst, std::abs(e));
        }
        check(worst < 0.5, "PAN: loudness (both channels, BS.1770) stays on target whatever the panning (worst %.2f dB)", worst);

        // hard left / hard right: left = only amp A, right = only amp B (each a scaled copy of that amp alone)
        auto residual = [](const std::vector<double>& y, const std::vector<double>& x) {
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = 48000; i < y.size(); ++i) { xy += x[i] * y[i]; xx += x[i] * x[i]; yy += y[i] * y[i]; }
            const double k = xy / xx;
            double r = 0;
            for (size_t i = 48000; i < y.size(); ++i) r += (y[i] - k * x[i]) * (y[i] - k * x[i]);
            return 10 * std::log10(r / yy + 1e-300);
        };
        const auto onlyA = render(-1.0f, 1.0f, false, false, true), onlyB = render(-1.0f, 1.0f, false, true, false);
        const double resL = residual(onlyA.L, onlyA.mono), resR = residual(onlyB.R, onlyB.mono);
        check(resL < -100 && resR < -100,
              "PAN hard left / hard right: left = amp A only, right = amp B only (anything else %.0f / %.0f dB)", resL, resR);

        const auto fr = render(-0.6f, 0.6f, true);
        const double fe = stereoLoudnessErr(fr);
        double frankCentDiff = 0;
        const auto frc = render(0.0f, 0.0f, true);
        for (size_t i = 0; i < frc.L.size(); ++i) frankCentDiff = std::max(frankCentDiff, std::abs(frc.L[i] - frc.mono[i]) + std::abs(frc.R[i] - frc.mono[i]));
        check(frankCentDiff == 0.0 && std::abs(fe) < 2.0,
              "PAN + Frankenstein: centred = mono exactly; panned -60/+60 stereo loudness %+.2f dB vs single-capture level", fe);
    }

    // ---------------- 6e. Cabinet IR per slot ----------------
    {
        std::mt19937 rng(3);
        std::normal_distribution<double> nd(0.0, 1.0);
        std::vector<double> rawIr(3000);
        double lp = 0;
        for (size_t i = 0; i < rawIr.size(); ++i) { lp += 0.25 * (nd(rng) - lp); rawIr[i] = lp * std::exp(-(double) i / 500.0); }
        const auto ir = prepareImpulseResponse(rawIr, sr, sr).samples;
        auto irEq = ParametricEq::defaultBands();
        irEq[0].freqHz = 120.0f; irEq[5].gainDb = -5.0f;

        auto render = [&](bool withIr, bool irOn, bool irEqChanged) {
            Engine eng;
            eng.getSlot(0).submit(loadAt(argv[1]));
            eng.prepare(sr, 256, 10.0);
            if (withIr) eng.getIrSlot(0).submit(std::make_unique<Convolver>(ir));
            EngineSettings s;
            s.slots[0].irEnabled = irOn;
            if (irEqChanged) s.slots[0].irEq = irEq;
            std::vector<double> out(test.size());
            for (size_t pos = 0; pos < test.size(); pos += 256)
                eng.process(test.data() + pos, out.data() + pos, (int) std::min<size_t>(256, test.size() - pos), s);
            return out;
        };
        const auto plain = render(false, true, false);
        const auto withIr = render(true, true, false);
        auto ref = plain; // everything after the IR is linear and time-invariant once settled
        {
            Convolver c(ir);
            c.process(ref.data(), ref.data(), (int) ref.size());
        }
        double err = 0, peak = 0;
        for (size_t i = 48000; i < ref.size(); ++i) { err = std::max(err, std::abs(withIr[i] - ref[i])); peak = std::max(peak, std::abs(ref[i])); }
        check(err / peak < 1e-9, "IR: slot output = the capture through its cabinet IR (relative error %.1e)", err / peak);

        check(render(false, true, true) == plain, "IR EQ without an IR: no effect at all (bit-identical)");
        const auto bypassed = render(true, false, true);
        check(std::equal(bypassed.begin() + 48000, bypassed.end(), plain.begin() + 48000),
              "IR bypassed: IR and IR EQ both out -> bit-identical to no IR");

        const auto withEq = render(true, true, true);
        auto refEq = ref;
        {
            ParametricEq e; e.prepare(sr);
            for (size_t pos = 0; pos < refEq.size(); pos += 256) { e.setBands(irEq); e.process(refEq.data() + pos, (int) std::min<size_t>(256, refEq.size() - pos)); }
        }
        double errEq = 0;
        for (size_t i = 48000; i < refEq.size(); ++i) errEq = std::max(errEq, std::abs(withEq[i] - refEq[i]));
        check(errEq / peak < 1e-6, "IR EQ: applied after the IR, matches the verified EQ (relative error %.1e)", errEq / peak);
    }

    // ---------------- 6f. Frankenstein NOTES mode (split before the amps) ----------------
    {
        auto render = [&](bool frank, bool notes, std::array<int, 5> amps, bool toggle) {
            Engine eng;
            eng.getSlot(0).submit(loadAt(argv[1]));
            eng.getSlot(1).submit(loadAt(argv[2]));
            eng.prepare(sr, 256, 10.0);
            EngineSettings s;
            s.slots[1].mute = !frank; // without Frankenstein: amp A alone
            s.frankenstein.enabled = frank;
            s.frankenstein.beforeAmps = notes;
            s.frankenstein.sections = 2;
            s.frankenstein.amp = amps;
            s.frankenstein.dividerHz[0] = 500.0f;
            std::vector<double> out(test.size());
            bool finite = true;
            for (size_t pos = 0; pos < test.size(); pos += 256)
            {
                if (toggle) s.frankenstein.beforeAmps = (pos / (256 * 60)) % 2 == 1;
                const int n = (int) std::min<size_t>(256, test.size() - pos);
                eng.process(test.data() + pos, out.data() + pos, n, s);
                for (int k = 0; k < n; ++k) finite = finite && std::isfinite(out[pos + (size_t) k]);
            }
            return std::make_pair(out, finite);
        };
        auto loud = [&](std::vector<double> y) { CaptureAnalyzer::kWeightInPlace(y, sr); return 10 * std::log10(power(y, 48000)); };
        const auto single = render(false, false, { 0, 0, 0, 0, 0 }, false);
        const auto same = render(true, true, { 0, 0, 0, 0, 0 }, false);
        check(std::abs(loud(same.first) - loud(single.first)) < 0.5,
              "FRANKENSTEIN NOTES: the same amp in both note ranges sounds as loud as the amp alone (%+.2f dB)", loud(same.first) - loud(single.first));
        const auto split = render(true, true, { 0, 1, 0, 0, 0 }, true);
        check(split.second, "FRANKENSTEIN: switching TONE <-> NOTES every 0.3 s while playing: no invalid samples");
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

    // ---------------- 7. CPU: several cores + silent amps sleep ----------------
    {
        // a guitar-like test signal: plucked notes on changing pitches
        const int N = (int) (sr * 6.0);
        std::vector<double> x((size_t) N);
        double ph = 0;
        for (int i = 0; i < N; ++i)
        {
            const double t = std::fmod(i / sr, 0.4);
            ph += 110.0 * std::pow(2.0, ((i / (int) (0.4 * sr)) % 7) / 12.0) / sr;
            x[(size_t) i] = 0.3 * std::sin(2 * kPi * ph) * std::exp(-3.0 * t) + 0.1 * std::sin(6 * kPi * ph) * std::exp(-6.0 * t);
        }
        auto render = [&](int workers, const std::function<void(EngineSettings&, int)>& automate, std::vector<int>* active) {
            Engine eng;
            eng.setMultiCore(workers);
            eng.getSlot(0).submit(loadAt(argv[1]));
            eng.getSlot(1).submit(loadAt(argv[2]));
            eng.getSlot(2).submit(loadAt(argv[1]));
            eng.prepare(sr, 128, 10.0);
            EngineSettings st;
            st.slots[0].mix = 40; st.slots[1].mix = 30; st.slots[2].mix = 30;
            st.slots[1].pan = -0.5f; st.slots[2].pan = 0.5f;
            std::vector<double> L((size_t) N), R((size_t) N);
            for (int b = 0; b < N; b += 128)
            {
                if (automate) automate(st, b);
                eng.process(x.data() + b, L.data() + b, R.data() + b, std::min(128, N - b), st);
                if (active) active->push_back(eng.getActivePaths());
            }
            L.insert(L.end(), R.begin(), R.end());
            return L;
        };
        const auto single = render(0, nullptr, nullptr);
        const auto multi = render(4, nullptr, nullptr);
        check(single == multi, "MULTI-CORE: three amps on 4 worker threads -> output bit-identical to one core");

        // amp 3 muted at 1 s, unmuted at 3 s: it sleeps in between and comes back smoothly
        std::vector<int> active;
        const auto muted = render(4, [&](EngineSettings& st, int b) { st.slots[2].mute = b >= (int) sr && b < (int) (3 * sr); }, &active);
        const auto ref = render(4, [&](EngineSettings& st, int b) { st.slots[2].mute = b >= (int) sr; }, nullptr);
        const int sleptAt = (int) (std::find(active.begin(), active.end(), 2) - active.begin()) * 128;
        const bool wokeUp = active.back() == 3;
        // after the fade-in (3 s + 0.1 s warm-up + ~0.5 s glide) the output equals an engine that never slept
        double diff = 0, sig = 0, curv = 0, curvRef = 0;
        const auto never = render(4, nullptr, nullptr);
        for (int i = (int) (4.5 * sr); i < N; ++i) { diff += std::pow(muted[(size_t) i] - never[(size_t) i], 2); sig += never[(size_t) i] * never[(size_t) i]; }
        for (int i = (int) (2.9 * sr); i < (int) (3.8 * sr); ++i)
        {
            curv = std::max(curv, std::abs(muted[(size_t) i] - 2 * muted[(size_t) i - 1] + muted[(size_t) i - 2]));
            curvRef = std::max({ curvRef, std::abs(never[(size_t) i] - 2 * never[(size_t) i - 1] + never[(size_t) i - 2]),
                                 std::abs(ref[(size_t) i] - 2 * ref[(size_t) i - 1] + ref[(size_t) i - 2]) });
        }
        const double resDb = 10 * std::log10(diff / sig + 1e-30);
        check(sleptAt > 0 && sleptAt < (int) (2.5 * sr) && wokeUp,
              "SLEEP: a muted amp stops using CPU %.2f s after MUTE (fade-out + 0.5 s hold) and runs again after unmute", sleptAt / sr - 1.0);
        check(resDb < -100.0, "SLEEP: 1.5 s after unmute the sound is identical to an amp that never slept (difference %.0f dB)", resDb);
        check(curv <= curvRef * 1.05, "SLEEP: the amp fades back in without a click (curvature %.4f vs %.4f without sleeping)", curv, curvRef);
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

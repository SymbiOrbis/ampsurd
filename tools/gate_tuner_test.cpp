// gate_tuner_test: objective tests of the noise gate and the tuner.  Exit code 0 = all pass.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ampsurd/NoiseGate.h"
#include "ampsurd/PitchDetector.h"

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

int main()
{
    const double sr = 48000.0;

    // ---------------- Tuner ----------------
    {
        struct Note { const char* name; double hz; };
        const Note notes[] = { { "F#1 (8-string)", 46.25 }, { "B1 (7-string)", 61.735 }, { "E2", 82.407 }, { "A2", 110.0 },
                               { "G3", 196.0 }, { "E4", 329.63 }, { "E5 (12th fret)", 659.26 }, { "E6 (24th fret)", 1318.5 } };
        double worst = 0;
        int wrongNotes = 0;
        for (const auto& note : notes)
            for (double detune : { 0.0, +7.0, -23.0 })
            {
                PitchDetector pd;
                pd.prepare(sr);
                const double f = note.hz * std::pow(2.0, detune / 1200.0);
                std::vector<float> x(256);
                double t = 0;
                for (int b = 0; b < 60; ++b) // 0.32 s of a decaying, harmonic-rich string-like tone
                {
                    for (int i = 0; i < 256; ++i, t += 1.0 / sr)
                    {
                        double v = 0;
                        for (int h = 1; h <= 8; ++h) v += std::sin(2 * kPi * f * h * t) / h * std::exp(-h * 1.5 * t);
                        x[(size_t) i] = (float) (0.2 * v);
                    }
                    pd.push(x.data(), 256);
                }
                const auto r = pd.analyse();
                const double expectMidi = 69 + 12 * std::log2(note.hz / 440.0);
                if (!r.valid || r.midiNote != (int) std::lround(expectMidi)) ++wrongNotes;
                else worst = std::max(worst, std::abs(r.cents - (detune + (expectMidi - std::lround(expectMidi)) * 100)));
            }
        check(wrongNotes == 0 && worst < 1.0,
              "TUNER: correct note for F#1..E6 (46 Hz-1.3 kHz), in tune / +7 / -23 cents; worst error %.2f cents (%.0f wrong notes)",
              worst, wrongNotes);

        PitchDetector pd;
        pd.prepare(sr);
        std::mt19937 rng(3);
        std::normal_distribution<float> n(0.0f, 0.0003f); // -70 dBFS hiss
        std::vector<float> x(4096);
        for (auto& v : x) v = n(rng);
        pd.push(x.data(), (int) x.size());
        check(!pd.analyse().valid, "TUNER: shows nothing for interface hiss (no false notes)");
    }

    // ---------------- Gate ----------------
    {
        NoiseGate gate;
        gate.prepare(sr);
        gate.setParameters(true, NoiseGate::kDefaultThresholdDb, NoiseGate::kDefaultDecayMs);

        const int note = (int) (0.5 * sr), tail = (int) (1.0 * sr);
        std::vector<double> key(note + tail + note), target(key.size(), 1.0);
        std::mt19937 rng(5);
        std::normal_distribution<double> hiss(0.0, 0.0001); // -80 dBFS: below the -70 dB default threshold
        for (size_t i = 0; i < key.size(); ++i)
        {
            const bool playing = i < (size_t) note || i >= (size_t) (note + tail);
            key[i] = playing ? 0.1 * std::sin(2 * kPi * 110.0 * (double) i / sr) : hiss(rng);
        }
        gate.process(key.data(), target.data(), (int) key.size());

        bool untouched = true;
        for (int i = 0; i < note; ++i) untouched = untouched && target[(size_t) i] == 1.0;
        check(untouched, "GATE: while playing, the signal passes bit-identically (gain exactly 1.0)");

        // time from note end to -60 dB
        int closedAt = -1;
        for (int i = note; i < note + tail; ++i)
            if (target[(size_t) i] <= 0.001) { closedAt = i - note; break; }
        const double ms = closedAt * 1000.0 / sr;
        check(closedAt > 0 && ms > 100 && ms < 400,
              "GATE: after the note ends, fades smoothly to -60 dB in %.0f ms (decay 120 ms + hold/release)", ms);

        bool staysShut = true;
        for (int i = note + closedAt; i < note + tail; ++i) staysShut = staysShut && target[(size_t) i] <= 0.001;
        check(staysShut, "GATE: stays closed on -80 dBFS hiss between notes");

        int openAt = -1;
        for (int i = note + tail; i < (int) target.size(); ++i)
            if (target[(size_t) i] >= 0.99) { openAt = i - (note + tail); break; }
        check(openAt >= 0 && openAt < 56,
              "GATE: fully open (99 %%) %.2f ms after the next note starts - before the amp signal arrives (1.17 ms), so no attack is cut",
              openAt * 1000.0 / sr);

        NoiseGate off;
        off.prepare(sr);
        off.setParameters(false, NoiseGate::kDefaultThresholdDb, NoiseGate::kDefaultDecayMs);
        std::vector<double> t2(key.size(), 1.0);
        off.process(key.data(), t2.data(), (int) key.size());
        bool allOne = true;
        for (double v : t2) allOne = allOne && v == 1.0;
        check(allOne, "GATE: switched off -> bit-identical pass-through");
    }
    {
        // A loud, sustaining amp note while the clean guitar level hovers around the threshold
        // (fades below it for 40 ms, then swells back): the gate must not snap back open (click).
        const double sr = 48000.0;
        NoiseGate g;
        g.prepare(sr);
        g.setParameters(true, -70.0f, 120.0f);
        const double thr = std::pow(10.0, -70.0 / 20.0);
        const int N = (int) (sr * 1.0);
        std::vector<double> key((size_t) N), amp((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const double t = (double) i / sr;
            // DI level: 3x threshold, dips to 0.4x between 0.3 and 0.34 s (slow swells, 10 ms ramps)
            double lvl = 3.0;
            if (t > 0.29 && t < 0.35)
            {
                const double a = std::clamp((t - 0.29) / 0.01, 0.0, 1.0) * std::clamp((0.35 - t) / 0.01, 0.0, 1.0);
                lvl = 3.0 - 2.6 * a;
            }
            key[(size_t) i] = lvl * thr * std::sqrt(2.0) * std::sin(2 * kPi * 196.0 * t);
            amp[(size_t) i] = 0.4 * std::sin(2 * kPi * 196.0 * t) + 0.1 * std::sin(2 * kPi * 588.0 * t); // compressed, still loud
        }
        auto out = amp;
        for (int p = 0; p < N; p += 64) g.process(key.data() + p, out.data() + p, std::min(64, N - p));
        double worstStep = 0, steadyStep = 0;
        for (int i = 2; i < N; ++i)
        {
            const double c = std::abs(out[(size_t) i] - 2 * out[(size_t) i - 1] + out[(size_t) i - 2]);
            const double c0 = std::abs(amp[(size_t) i] - 2 * amp[(size_t) i - 1] + amp[(size_t) i - 2]);
            worstStep = std::max(worstStep, c);
            steadyStep = std::max(steadyStep, c0);
        }
        check(worstStep <= steadyStep * 1.05,
              "GATE: a fading note that swells back above the threshold is faded in smoothly, no click (curvature %.5f vs %.5f)", worstStep, steadyStep);
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

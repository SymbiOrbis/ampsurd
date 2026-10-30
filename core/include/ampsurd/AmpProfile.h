#pragma once

// AMPSURD amp profile (.ampsurdamp, 2026-10-30) - the parts that do not need JUCE:
//
// ToneSweep   the test signal the owner records through the amp (AMPSURD_tone_sweep_48k.wav: 1 s silence, a
//             10 s exponential sine sweep 20 Hz - 20 kHz at -12 dBFS, 3 s pause, the same sweep at -32 dBFS, 3 s)
//             and the analysis of such a recording: the amp's magnitude response on a 1/6-octave grid, from the
//             loud sweep when it stayed clean (it agrees with the quiet one), else from the quiet sweep.
// ToneStack   the measured tone controls (BASS, MID, TREBLE): for each knob its response at a few positions,
//             relative to all knobs at noon. curveDb() = the sum of the three interpolated curves (the knobs are
//             treated as independent); minPhaseFir() turns a curve into a short minimum-phase filter.
//
// Everything here is non-RT.

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace ampsurd
{

struct ToneSweep
{
    static constexpr double kRate = 48000.0, kSeconds = 10.0, kF1 = 20.0, kF2 = 20000.0;
    static constexpr double kLoudDb = -12.0, kQuietDb = -32.0, kLoudAt = 1.0, kQuietAt = 14.0, kLength = 27.0;
    static std::vector<double> make();          // the whole file (27 s at 48 kHz), exactly as sent to the owner
    static const std::vector<double>& gridHz(); // response grid: 1/6 octave, 20 Hz .. 20 kHz

    struct Response
    {
        std::vector<double> db;     // on gridHz()
        bool usedQuiet = false;     // the loud sweep was distorted: the quiet one was used
        double loudQuietDiffDb = 0; // how much the two sweeps' shapes differ (100 Hz - 8 kHz)
        double levelDb = -200;      // overall level of the recording
    };
    // a recording of the whole file through the amp (any extra latency up to 0.5 s); false + error if unusable
    static bool analyse(const std::vector<double>& recording, double sampleRate, Response& out, std::string& error);
};

struct ToneStack
{
    // knob 0 = BASS, 1 = MID, 2 = TREBLE; for each: (position 0..10, curve dB on ToneSweep::gridHz() relative to noon)
    std::array<std::vector<std::pair<double, std::vector<double>>>, 3> knobs;
    bool empty() const { return knobs[0].empty() && knobs[1].empty() && knobs[2].empty(); }
    std::vector<double> curveDb(double bass, double mid, double treble) const;
    // a minimum-phase FIR (unit at 0 dB: noon = a single 1.0) for a curve on the grid; length a power of two
    static std::vector<double> minPhaseFir(const std::vector<double>& gridDb, double sampleRate, int length = 1024);
    static bool isFlat(const std::vector<double>& gridDb, double toleranceDb = 0.05);
};

} // namespace ampsurd

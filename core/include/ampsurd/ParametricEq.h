#pragma once

// ParametricEq: 10 bands per capture path.
//   band 1      LOW CUT  (high-pass, 12 dB/octave, frequency + resonance Q). Off at 20 Hz.
//   bands 2-9   bells (frequency, gain, Q)
//   band 10     HIGH CUT (low-pass, 12 dB/octave, frequency + resonance Q). Off at 20 kHz.
//
// Uses Andrew Simper's trapezoidal state-variable filter, which stays stable and
// click-free while parameters move. Parameters are smoothed and coefficients are
// recomputed every 16 samples. A bell at exactly 0 dB, or a cut band at its end stop,
// costs nothing, and the whole EQ is bit-transparent when everything is at default.
//
// prepare(): non-RT. setBands()/process(): audio thread (no allocation).

#include <array>

namespace ampsurd
{

struct EqBand
{
    float freqHz = 1000.0f;
    float gainDb = 0.0f; // ignored by the two cut bands
    float q = 1.0f;
};

class ParametricEq
{
public:
    static constexpr int kNumBands = 10;
    static constexpr float kMinFreq = 20.0f, kMaxFreq = 20000.0f;
    static constexpr float kMinGain = -18.0f, kMaxGain = 18.0f;
    static constexpr float kMinQ = 0.3f, kMaxQ = 10.0f;
    static constexpr float kMinCutQ = 0.5f, kMaxCutQ = 2.0f; // resonance range of the cut bands

    enum class BandType { bell, lowCut, highCut };
    static BandType bandType(int band) noexcept
    {
        return band == 0 ? BandType::lowCut : band == kNumBands - 1 ? BandType::highCut : BandType::bell;
    }
    // A cut band at its end stop (20 Hz / 20 kHz) is switched off.
    static bool isCutActive(int band, float freqHz) noexcept
    {
        return bandType(band) == BandType::lowCut ? freqHz > 21.0f
             : bandType(band) == BandType::highCut ? freqHz < 19800.0f
                                                    : false;
    }

    static std::array<EqBand, kNumBands> defaultBands();

    void prepare(double sampleRate);
    void reset() noexcept;

    // Audio thread: targets for the next process() call.
    void setBands(const std::array<EqBand, kNumBands>& targets) noexcept;

    void process(double* x, int n) noexcept;

    // Magnitude response in dB of a set of bands (for drawing the curve, any thread).
    static double magnitudeDb(const std::array<EqBand, kNumBands>& bands, double freqHz, double sampleRate);

private:
    struct State
    {
        BandType type = BandType::bell;
        int index = 0;
        // smoothed parameters (log-frequency, dB, log-Q)
        double logF = 0, gain = 0, logQ = 0;
        double tLogF = 0, tGain = 0, tLogQ = 0;
        // coefficients and state
        double a1 = 0, a2 = 0, a3 = 0, m1 = 0, k = 0;
        double ic1 = 0, ic2 = 0;
        bool active = false;
    };

    void updateCoefficients(State& s) noexcept;

    double fs = 48000.0;
    double smoothCoef = 0.0; // per 16-sample step
    std::array<State, kNumBands> bands {};
};

} // namespace ampsurd

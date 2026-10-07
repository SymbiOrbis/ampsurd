#pragma once

// ParametricEq: 10 bell bands per capture path (frequency, gain, Q).
//
// Uses Andrew Simper's trapezoidal state-variable filter, which stays stable and
// click-free while parameters move. Parameters are smoothed and coefficients are
// recomputed every 16 samples. A band at exactly 0 dB costs nothing and the whole
// EQ is bit-transparent when all bands are flat.
//
// prepare(): non-RT. setBand()/process(): audio thread (no allocation).

#include <array>

namespace ampsurd
{

struct EqBand
{
    float freqHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
};

class ParametricEq
{
public:
    static constexpr int kNumBands = 10;
    static constexpr float kMinFreq = 20.0f, kMaxFreq = 20000.0f;
    static constexpr float kMinGain = -18.0f, kMaxGain = 18.0f;
    static constexpr float kMinQ = 0.3f, kMaxQ = 10.0f;

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
        // smoothed parameters (log-frequency, dB, log-Q)
        double logF = 0, gain = 0, logQ = 0;
        double tLogF = 0, tGain = 0, tLogQ = 0;
        // coefficients and state
        double a1 = 0, a2 = 0, a3 = 0, m1 = 0;
        double ic1 = 0, ic2 = 0;
        bool active = false;
    };

    void updateCoefficients(State& s) noexcept;

    double fs = 48000.0;
    double smoothCoef = 0.0; // per 16-sample step
    std::array<State, kNumBands> bands {};
};

} // namespace ampsurd

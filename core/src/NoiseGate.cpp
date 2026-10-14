#include "ampsurd/NoiseGate.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
}

void NoiseGate::prepare(double sampleRate)
{
    sr = sampleRate;
    envCoef = 1.0 - std::exp(-1.0 / (0.004 * sr));       // level detector: 4 ms RMS average
    slowCoef = 1.0 - std::exp(-1.0 / (0.030 * sr));
    releaseToOneCoef = 1.0 - std::exp(-1.0 / (0.010 * sr));
    shapeCoef = 1.0 - std::exp(-1.0 / (0.002 * sr));     // second smoothing stage: no corners in the gain curve
    holdSamples = (int) std::lround(kHoldMs * 0.001 * sr);
    fastSamples = std::max(1, (int) std::lround(0.0009 * sr));
    reopenSamples = std::max(1, (int) std::lround(kReopenMs * 0.001 * sr));
    setParameters(enabled, kDefaultThresholdDb, kDefaultDecayMs);
    reset();
}

void NoiseGate::reset() noexcept
{
    meanSquare = 0.0;
    meanSquareSlow = 0.0;
    gain = 1.0;
    shaped = 1.0;
    open = true;
    ramping = false;
    fastOpen = false;
    closePos = 0;
    holdCounter = holdSamples;
}

void NoiseGate::setParameters(bool on, float thresholdDb, float decayMs) noexcept
{
    enabled = on;
    openLevel = std::pow(10.0, (double) thresholdDb / 20.0);
    closeLevel = openLevel * std::pow(10.0, -kHysteresisDb / 20.0);
    decaySamples = std::max(1, (int) std::lround((double) decayMs * 0.001 * sr)); // -60 dB after DECAY
}

void NoiseGate::startRamp(double from, int samples) noexcept
{
    rampFrom = from;
    rampPos = 0;
    rampLen = std::max(1, samples);
    ramping = true;
}

void NoiseGate::process(const double* key, double* target, int n, double* target2) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        // short-term (4 ms) RMS level: ignores single noise spikes; a pick attack crosses the threshold almost at once
        const double sq = key[i] * key[i];
        meanSquare += (sq - meanSquare) * envCoef;
        meanSquareSlow += (sq - meanSquareSlow) * slowCoef;
        const double env = std::sqrt(meanSquare);
        // a pick attack: the 4 ms level jumps > 10 dB above the 30 ms average
        const bool attack = meanSquare > 10.0 * meanSquareSlow;

        if (!enabled)
        {
            open = true;
            ramping = fastOpen = false;
            if (gain < 1.0)
            {
                gain += (1.0 - gain) * releaseToOneCoef;
                if (gain > 0.99999) gain = 1.0;
            }
        }
        else
        {
            if (env >= openLevel)
            {
                if (!open)
                {
                    // a new note after silence opens at once (before the amp signal arrives); a note that
                    // was still sounding while the gate was closing fades back in gently
                    fastOpen = attack || gain < 1e-4;
                    startRamp(gain, fastOpen ? fastSamples : reopenSamples);
                    open = true;
                }
                else if (ramping && !fastOpen && attack)
                {
                    fastOpen = true; // picked again during a gentle re-open: finish quickly
                    startRamp(gain, fastSamples);
                }
                holdCounter = holdSamples;
            }
            else if (open && env < closeLevel)
            {
                if (holdCounter > 0)
                    --holdCounter;
                else
                {
                    open = false;
                    ramping = false;
                    closeFrom = gain;
                    closePos = 0;
                }
            }

            if (open)
            {
                if (ramping)
                {
                    ++rampPos;
                    const double x = std::min(1.0, (double) rampPos / (double) rampLen);
                    gain = rampFrom + (1.0 - rampFrom) * (0.5 - 0.5 * std::cos(kPi * x));
                    if (x >= 1.0) { gain = 1.0; ramping = false; }
                }
                else if (gain < 1.0)
                    startRamp(gain, reopenSamples);
            }
            else if (gain > 0.0)
            {
                // S-shaped fade in dB: -60 * (t / decay)^2 starts with zero slope (no corner when the gate
                // begins to act), reaches -60 dB after DECAY, then closes completely
                ++closePos;
                const double t = (double) closePos / (double) decaySamples;
                const double db = -60.0 * t * t;
                gain = db < -100.0 ? 0.0 : closeFrom * std::pow(10.0, db / 20.0);
            }
        }

        // The applied gain follows `gain` through a second smoothing stage, so closing and re-opening
        // have no sudden change of slope. A new pick attack (fast open) bypasses it: the gate must be
        // fully open before the amp's attack.
        if (open && fastOpen && gain > shaped) shaped = gain;
        else shaped += (gain - shaped) * shapeCoef;
        if (std::abs(shaped - gain) < 1e-9) shaped = gain;

        if (shaped != 1.0)
        {
            target[i] *= shaped;
            if (target2 != nullptr) target2[i] *= shaped;
        }
    }
}

} // namespace ampsurd

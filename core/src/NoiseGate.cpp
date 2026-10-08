#include "ampsurd/NoiseGate.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{

void NoiseGate::prepare(double sampleRate)
{
    sr = sampleRate;
    envAttack = 1.0 - std::exp(-1.0 / (0.004 * sr));     // level detector: 4 ms RMS average
    envRelease = envAttack;
    attackCoef = 1.0 - std::exp(-1.0 / (0.0002 * sr));   // opening: fully open in < 1.2 ms (before the amp signal arrives)
    releaseToOneCoef = 1.0 - std::exp(-1.0 / (0.010 * sr));
    reopenCoef = 1.0 - std::exp(-1.0 / (0.006 * sr));    // re-opening during a fading note: 6 ms
    slowCoef = 1.0 - std::exp(-1.0 / (0.030 * sr));
    shapeCoef = 1.0 - std::exp(-1.0 / (0.002 * sr));    // second smoothing stage: no corners in the gain curve
    holdSamples = (int) std::lround(0.015 * sr);        // 15 ms hold
    setParameters(enabled, kDefaultThresholdDb, kDefaultDecayMs);
    reset();
}

void NoiseGate::reset() noexcept
{
    env = 0.0;
    meanSquare = 0.0;
    meanSquareSlow = 0.0;
    gain = 1.0;
    shaped = 1.0;
    open = true;
    holdCounter = holdSamples;
}

void NoiseGate::setParameters(bool on, float thresholdDb, float decayMs) noexcept
{
    enabled = on;
    openLevel = std::pow(10.0, (double) thresholdDb / 20.0);
    closeLevel = openLevel * std::pow(10.0, -4.0 / 20.0); // 4 dB hysteresis
    // exponential fade reaching -60 dB after `decayMs`
    const double samples = std::max(1.0, (double) decayMs * 0.001 * sr);
    decayCoef = std::pow(10.0, -3.0 / samples);
}

void NoiseGate::process(const double* key, double* target, int n, double* target2) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        // short-term (4 ms) RMS level: ignores single noise spikes; a pick attack crosses the threshold almost at once
        const double sq = key[i] * key[i];
        meanSquare += (sq - meanSquare) * (sq > meanSquare ? envAttack : envRelease);
        meanSquareSlow += (sq - meanSquareSlow) * slowCoef;
        env = std::sqrt(meanSquare);

        if (!enabled)
        {
            open = true;
            fastOpen = false;
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
                // a new note after silence opens at once (before the amp signal arrives); a note that
                // was still sounding while the gate began to close is faded back in smoothly
                // (a pick attack: the 4 ms level jumps > 10 dB above the 30 ms average)
                if (!open) fastOpen = meanSquare > 10.0 * meanSquareSlow || gain < 1e-4;
                open = true;
                holdCounter = holdSamples;
            }
            else if (open && env < closeLevel)
            {
                if (holdCounter > 0) --holdCounter;
                else open = false;
            }

            if (open)
            {
                if (gain < 1.0)
                {
                    gain += (1.0 - gain) * (fastOpen ? attackCoef : reopenCoef);
                    if (gain > 0.99999) gain = 1.0;
                }
            }
            else
            {
                gain *= decayCoef;
                if (gain < 1e-6) gain = 0.0;
            }
        }

        // The applied gain follows `gain` through a second smoothing stage, so closing and re-opening
        // have no sudden change of slope (which is audible as a soft click on a sustaining note).
        // A new pick attack (fast open) bypasses it: the gate must be fully open before the amp's attack.
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

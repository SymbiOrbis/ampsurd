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
    holdSamples = (int) std::lround(0.015 * sr);        // 15 ms hold
    setParameters(enabled, kDefaultThresholdDb, kDefaultDecayMs);
    reset();
}

void NoiseGate::reset() noexcept
{
    env = 0.0;
    meanSquare = 0.0;
    gain = 1.0;
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

void NoiseGate::process(const double* key, double* target, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        // short-term (4 ms) RMS level: ignores single noise spikes; a pick attack crosses the threshold almost at once
        const double sq = key[i] * key[i];
        meanSquare += (sq - meanSquare) * (sq > meanSquare ? envAttack : envRelease);
        env = std::sqrt(meanSquare);

        if (!enabled)
        {
            open = true;
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
                    gain += (1.0 - gain) * attackCoef;
                    if (gain > 0.99999) gain = 1.0;
                }
            }
            else
            {
                gain *= decayCoef;
                if (gain < 1e-6) gain = 0.0;
            }
        }

        if (gain != 1.0)
            target[i] *= gain;
    }
}

} // namespace ampsurd

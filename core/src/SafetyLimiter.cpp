#include "monstrosity/SafetyLimiter.h"

#include <algorithm>
#include <cmath>

namespace monstrosity
{

void SafetyLimiter::prepare(double sampleRate, double lookaheadMs, double releaseMs, double ceilingDb)
{
    lookahead = std::max(1, (int) std::lround(sampleRate * lookaheadMs * 0.001));
    ceiling = std::pow(10.0, ceilingDb / 20.0);
    releaseCoef = 1.0 - std::exp(-1.0 / (std::max(1.0, releaseMs) * 0.001 * sampleRate));

    delay.assign((size_t) lookahead, 0.0);
    dqVal.assign((size_t) lookahead + 1, 1.0);
    dqIdx.assign((size_t) lookahead + 1, 0);
    avgBuf.assign((size_t) lookahead + 1, 1.0);
    reset();
}

void SafetyLimiter::reset() noexcept
{
    std::fill(delay.begin(), delay.end(), 0.0);
    std::fill(avgBuf.begin(), avgBuf.end(), 1.0);
    delayPos = 0;
    dqHead = dqSize = 0;
    sampleIndex = 0;
    released = 1.0;
    avgPos = 0;
    avgSum = (double) avgBuf.size();
    minGainSinceRead = 1.0;
}

double SafetyLimiter::getAndResetMaxReductionDb() noexcept
{
    const double g = minGainSinceRead;
    minGainSinceRead = 1.0;
    return g >= 1.0 ? 0.0 : -20.0 * std::log10(std::max(g, 1e-9));
}

void SafetyLimiter::process(double* x, int n) noexcept
{
    const int window = lookahead + 1;
    const int cap = (int) dqVal.size();

    // Re-sum the moving average exactly once per block so rounding errors cannot accumulate.
    {
        double s = 0.0;
        for (double v : avgBuf) s += v;
        avgSum = s;
    }

    for (int i = 0; i < n; ++i)
    {
        const double in = std::isfinite(x[i]) ? x[i] : 0.0;
        const double a = std::abs(in);
        const double required = a > ceiling ? ceiling / a : 1.0;

        // --- sliding-window minimum over the last L+1 required gains ---
        // drop from the back everything >= the new value (it can never be the minimum again)
        while (dqSize > 0)
        {
            const int back = (dqHead + dqSize - 1) % cap;
            if (dqVal[(size_t) back] >= required) --dqSize;
            else break;
        }
        {
            const int pos = (dqHead + dqSize) % cap;
            dqVal[(size_t) pos] = required;
            dqIdx[(size_t) pos] = sampleIndex;
            ++dqSize;
        }
        // drop from the front what has left the window
        while (dqIdx[(size_t) dqHead] <= sampleIndex - window)
        {
            dqHead = (dqHead + 1) % cap;
            --dqSize;
        }
        const double held = dqVal[(size_t) dqHead];

        // --- release: drop instantly, recover smoothly, never above `held` ---
        if (held <= released)
            released = held;
        else
        {
            released += (held - released) * releaseCoef;
            if (held >= 1.0 && released > 1.0 - 1e-7)
                released = 1.0; // snap so that the limiter is bit-transparent when idle
        }

        // --- moving average over L+1 samples ---
        avgSum += released - avgBuf[(size_t) avgPos];
        avgBuf[(size_t) avgPos] = released;
        avgPos = (avgPos + 1) % window;
        double gain = avgSum / (double) window;
        if (gain > 1.0) gain = 1.0;

        // --- apply to the signal delayed by L samples ---
        const double delayed = delay[(size_t) delayPos];
        delay[(size_t) delayPos] = in;
        delayPos = (delayPos + 1) % lookahead;

        double y = delayed * gain;
        // Mathematical backstop against floating-point rounding only (never audible: the
        // gain computation above already guarantees |y| <= ceiling up to ~1e-15).
        if (y > ceiling) y = ceiling;
        else if (y < -ceiling) y = -ceiling;
        x[i] = y;

        if (gain < minGainSinceRead) minGainSinceRead = gain;
        ++sampleIndex;
    }
}

} // namespace monstrosity

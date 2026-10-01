#pragma once

// SafetyLimiter: always-on output protection. Guarantees that no output sample exceeds
// the ceiling (default -1 dBFS), so MONSTROSITY never produces digital clipping.
//
// How it works (lookahead brick-wall limiter):
//   1. For every incoming sample compute the gain it would need: g = min(1, ceiling/|x|).
//   2. Hold the minimum of g over the lookahead window (sliding-window minimum).
//   3. Let the gain recover slowly (release), never faster than the held minimum allows.
//   4. Smooth with a moving average as long as the lookahead, and apply it to the signal
//      delayed by the lookahead.
// Because every value averaged in step 4 already includes the loudest upcoming sample,
// the gain is guaranteed to have reached the required value when that sample arrives.
// No clipping and no hard-clip distortion: the level is turned down smoothly instead.
//
// Below the ceiling the limiter is bit-transparent (gain exactly 1.0, signal only delayed).
// Costs `getLatencySamples()` of latency (1 ms by default), which the plugin reports to the DAW.
//
// prepare(): non-RT (allocates). process(): RT-safe, in place.

#include <vector>

namespace monstrosity
{

class SafetyLimiter
{
public:
    void prepare(double sampleRate, double lookaheadMs = 1.0, double releaseMs = 120.0, double ceilingDb = -1.0);
    void reset() noexcept;

    // In place. Any block length.
    void process(double* x, int n) noexcept;

    int getLatencySamples() const noexcept { return lookahead; }
    double getCeiling() const noexcept { return ceiling; }

    // Deepest gain reduction (dB, >= 0) since the last call. Any thread may read it via the plugin.
    double getAndResetMaxReductionDb() noexcept;

private:
    int lookahead = 48;   // L
    double ceiling = 0.891; // linear
    double releaseCoef = 0.0;

    // Delay line for the audio (L samples).
    std::vector<double> delay;
    int delayPos = 0;

    // Sliding-window minimum of required gains over the last L+1 samples (monotonic deque in a ring).
    std::vector<double> dqVal;
    std::vector<long long> dqIdx;
    int dqHead = 0, dqSize = 0;
    long long sampleIndex = 0;

    double released = 1.0;

    // Moving average of `released` over L+1 samples.
    std::vector<double> avgBuf;
    int avgPos = 0;
    double avgSum = 0.0;

    double minGainSinceRead = 1.0;
};

} // namespace monstrosity

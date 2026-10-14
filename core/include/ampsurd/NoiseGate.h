#pragma once

// NoiseGate, in the spirit of a BOSS NS-2 used in an amp's effects loop:
// it LISTENS to the clean guitar signal (the "key", before the amps) but SILENCES the
// signal after the amps (the "target"). So it also removes the hiss that high-gain captures
// add, and it opens on the clean pick attack instead of on distorted noise.
//
// Because AMPSURD's amp paths are delayed by at least ~1 ms relative to the DI, the gate
// effectively looks ahead: it is fully open before the pick attack reaches the output.
//
// Level detection: short-term RMS (4 ms), so hiss spikes do not open it.
// Controls: THRESHOLD (dB RMS of the key signal), DECAY (time to fade out after the note ends).
//
// Gentle action (no step-like closing / re-opening, no chattering on a fading note):
//   - 8 dB hysteresis and a 60 ms hold: a note that hovers around the threshold keeps the gate open;
//   - closing is an S-curve in dB: it starts at zero slope, reaches -60 dB after DECAY and then
//     closes completely, so there is no audible "corner" when the gate begins to act;
//   - a note that swells back while the gate is closing fades back in over 25 ms (raised cosine);
//   - a new pick attack opens at once with a 0.9 ms S-curve (before the amp's attack arrives).
// Fully open = gain exactly 1.0.
//
// prepare(): non-RT. setParameters()/process(): audio thread, no allocation.

namespace ampsurd
{

class NoiseGate
{
public:
    static constexpr float kDefaultThresholdDb = -70.0f; // subtle: only acts between notes
    static constexpr float kDefaultDecayMs = 120.0f;
    static constexpr double kHysteresisDb = 8.0;
    static constexpr double kHoldMs = 60.0;
    static constexpr double kReopenMs = 25.0;

    void prepare(double sampleRate);
    void reset() noexcept;
    void setParameters(bool enabled, float thresholdDb, float decayMs) noexcept;

    // key: detection signal (n samples). target: processed in place.
    void process(const double* key, double* target, int n, double* target2 = nullptr) noexcept; // target2: other channel (same gain)

    double getCurrentGain() const noexcept { return shaped; }
    bool isOpen() const noexcept { return open; }

private:
    void startRamp(double from, int samples) noexcept;

    double sr = 48000.0;
    bool enabled = true;
    double openLevel = 0.0005, closeLevel = 0.0003;
    double meanSquare = 0.0, envCoef = 0.0;
    double meanSquareSlow = 0.0, slowCoef = 0.0; // 30 ms average: tells a new pick attack from a fading note
    double gain = 1.0;                           // gate gain before the last smoothing stage
    double releaseToOneCoef = 0.0;               // switched off while closed: back to 1 in ~10 ms
    // closing: dB curve -60 * (t / decay)^2 (S-shaped start), then fully closed
    int closePos = 0, decaySamples = 5760;
    double closeFrom = 1.0;                      // gain when the closing started
    // opening: raised-cosine ramp from rampFrom to 1
    double rampFrom = 0.0;
    int rampPos = 0, rampLen = 1, fastSamples = 43, reopenSamples = 1200;
    bool ramping = false, fastOpen = false;
    double shaped = 1.0, shapeCoef = 0.0;        // the gain actually applied (second smoothing stage)
    int holdSamples = 0, holdCounter = 0;
    bool open = true;
};

} // namespace ampsurd

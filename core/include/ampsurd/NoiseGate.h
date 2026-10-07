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
// 4 dB hysteresis and 15 ms hold prevent chattering. Fully open = gain exactly 1.0.
//
// prepare(): non-RT. setParameters()/process(): audio thread, no allocation.

namespace ampsurd
{

class NoiseGate
{
public:
    static constexpr float kDefaultThresholdDb = -70.0f; // subtle: only acts between notes
    static constexpr float kDefaultDecayMs = 120.0f;

    void prepare(double sampleRate);
    void reset() noexcept;
    void setParameters(bool enabled, float thresholdDb, float decayMs) noexcept;

    // key: detection signal (n samples). target: processed in place.
    void process(const double* key, double* target, int n) noexcept;

    double getCurrentGain() const noexcept { return gain; }
    bool isOpen() const noexcept { return open; }

private:
    double sr = 48000.0;
    bool enabled = true;
    double openLevel = 0.0005, closeLevel = 0.0003;
    double env = 0.0, meanSquare = 0.0, envAttack = 0.0, envRelease = 0.0;
    double gain = 1.0, attackCoef = 0.0, decayCoef = 0.0, releaseToOneCoef = 0.0;
    int holdSamples = 0, holdCounter = 0;
    bool open = true;
};

} // namespace ampsurd

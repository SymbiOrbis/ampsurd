#pragma once

// PitchShifter: tunes the whole guitar up or down (-12 ... +12 semitones + cents) with as little delay
// as possible. Used on the clean guitar BEFORE the amps, so it sounds like a re-tuned guitar.
//
// Method (time domain, like "drop" pedals): the guitar runs through a short delay line that is read
// at a different speed (2^(semitones/12)). When the read position drifts too far, it jumps by one
// pitch period of the playing (found by autocorrelation, refined to the best-matching sample) and
// the old and new read positions are crossfaded (raised cosine). Jumping by a whole period keeps
// the waveform continuous, so chords and single notes stay smooth.
//
// Delay: on average about half a pitch period plus ~0.5 ms: ~2 ms on high notes, ~6-7 ms on the low
// E string, ~12 ms on low power chords (their common period is twice the root's); reported to the
// host as kNominalLatencyMs. OFF = untouched signal, no delay.
// Switching ON / OFF crossfades over 10 ms (no click).
//
// prepare(): non-RT. setParameters()/process(): audio thread, no allocation.

#include <vector>

namespace ampsurd
{

class PitchShifter
{
public:
    static constexpr double kNominalLatencyMs = 5.0;

    void prepare(double sampleRate);
    void reset() noexcept;
    void setParameters(bool on, float semitones, float cents) noexcept;
    void process(double* x, int n) noexcept;

    bool isOn() const noexcept { return wantOn; }
    int getLatencySamples() const noexcept { return wantOn ? nominalLatency : 0; }
    double getRatio() const noexcept { return ratio; }

private:
    double read(double delay) const noexcept;       // cubic interpolation, `delay` samples behind the write position
    void estimatePeriod() noexcept;                 // every 256 samples, on a decimated copy
    double refineJump(double fromDelay, int jump) const noexcept; // jump +- a few samples, fractional

    double sr = 48000.0;
    std::vector<double> buf;
    int mask = 0, writePos = 0;

    // decimated history for the period estimate
    std::vector<double> dec;
    int decMask = 0, decPos = 0, decFactor = 4, decCount = 0;
    double decAcc = 0.0;
    int sinceEstimate = 0;
    int period = 384;                               // current jump length (samples)

    bool wantOn = false;
    double ratio = 1.0;
    double delay = 0.0, oldDelay = 0.0;
    int fadePos = 0, fadeLen = 1;
    bool fading = false;
    double onMix = 0.0, onStep = 0.0;
    int minPeriod = 120, maxPeriod = 720, defaultPeriod = 384, nominalLatency = 240;
};

} // namespace ampsurd

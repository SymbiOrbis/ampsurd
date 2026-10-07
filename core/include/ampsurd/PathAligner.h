#pragma once

// PathAligner: time / polarity / phase adjustment of one capture path.
//
// Three distinct, honestly-named operations (see docs/REVIEW.md):
//   * TIME     - fractional-sample delay (16-tap Kaiser-windowed sinc, 1/1024-sample
//                resolution). Shifts every frequency by the same TIME, so its phase
//                effect grows with frequency. Smoothly glides when the value changes.
//   * POLARITY - multiplication by -1 (a 180 degree flip at every frequency).
//   * PHASE    - frequency-independent phase rotation by an angle (0..360 degrees) with no
//                time shift, using a 90-degree IIR all-pass pair (Niemitalo design, accurate
//                to about 2 degrees from 20 Hz to 20 kHz at 44.1/48 kHz).
//                Rotation needs every path to pass through the same all-pass network, so
//                the engine switches the network on for all paths together (`rotationMix`).
//
// A fixed base latency of kBaseLatency samples is always present (centre of the sinc).
//
// prepare(): non-RT. setTargets()/process(): audio thread, no allocation.

#include <array>
#include <vector>

namespace ampsurd
{

class PathAligner
{
public:
    static constexpr int kTaps = 16;
    static constexpr int kBaseLatency = 8;

    // maxDelaySamples: largest delay ever requested (excluding kBaseLatency).
    void prepare(double sampleRate, int maxDelaySamples);
    void reset() noexcept;

    // delaySamples >= 0 (fractional). polarity +1/-1. phaseRadians: rotation angle.
    // rotationMix: 0 = all-pass network off, 1 = on (must be the same for all paths).
    void setTargets(double delaySamples, double polarity, double phaseRadians, double rotationMix) noexcept;

    // Jump straight to the targets (no glide), e.g. right after prepare().
    void snapToTargets() noexcept;

    void process(double* x, int n) noexcept;

    double getCurrentDelay() const noexcept { return delay; }

private:
    struct AllpassChain
    {
        std::array<double, 4> c {};
        std::array<double, 4> x1 {}, x2 {}, y1 {}, y2 {};
        double process(double in) noexcept;
        void reset() noexcept;
    };

    std::vector<double> buffer;
    int mask = 0;
    int writePos = 0;
    int maxDelay = 0;

    double delay = 0.0, tDelay = 0.0;
    double pol = 1.0, tPol = 1.0;
    double cosT = 1.0, sinT = 0.0, tCos = 1.0, tSin = 0.0;
    double mix = 0.0, tMix = 0.0;
    double delayCoef = 0.0, fastCoef = 0.0;

    AllpassChain chainA, chainB;
    double aDelayed = 0.0; // one-sample delay on path A of the Hilbert pair
};

} // namespace ampsurd

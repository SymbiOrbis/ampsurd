#pragma once

// "Create Frankenstein": frequency-split blending.
//
// The spectrum (20 Hz - 20 kHz) is divided into 2-5 sections; each section plays one of the
// loaded amps. Dividers set where one amp hands over to the next; WIDTH (0-90 %) sets how much
// of each section is used for a gradual hand-over.
//
// DSP (low latency, perfect reconstruction):
//   * Every amp still processes the FULL guitar signal (amps are non-linear); the split is applied
//     to the amps' OUTPUTS (after alignment, EQ and level match).
//   * A Linkwitz-Riley (24 dB/oct) crossover tree with 8 crossover points splits each amp output
//     into 9 bands. Each divider uses two crossover points: below the first one the lower amp plays,
//     between them both amps play (shared so that both are exactly -6 dB at the divider), above the
//     second one the upper amp plays. WIDTH moves the two points apart (overlap zone); at WIDTH 0
//     they coincide and the hand-over is a plain 24 dB/oct Linkwitz-Riley crossover.
//   * Band weights of all amps sum to 1 in every band, and the tree's phase compensation (one shared
//     all-pass per crossover, applied Horner-style) makes the bands add up to an all-pass: if every
//     section plays the same amp, the output has exactly that amp's frequency response.
//   * Crossover frequencies and band weights glide (~25 ms), so dragging dividers, changing WIDTH,
//     muting or changing the number of sections never clicks.
//
// computeLayout(): any thread. FrankensteinMixer::prepare(): non-RT. setTarget()/process(): audio thread.

#include <array>

namespace ampsurd
{

constexpr int kFrankMaxSlots = 5;

struct FrankensteinSettings
{
    bool enabled = false;
    int sections = 2;                                  // 2..5
    float width = 0.3f;                                // 0..0.9 (share of each section used for hand-over)
    std::array<int, 5> amp { 0, 1, 2, 3, 4 };          // slot (0..4) playing in each section
    std::array<float, 4> dividerHz { 200.0f, 800.0f, 2500.0f, 6000.0f };
    // false = TONE: split the amps' OUTPUTS (every note through all amps, each amp plays its part
    //               of the spectrum).  true = NOTES: split the guitar signal BEFORE the amps, so each
    //               note range is played through its own amp (low notes -> section 1 amp, ...).
    bool beforeAmps = false;
};

struct FrankensteinLayout
{
    static constexpr int kDividers = 4;
    static constexpr int kCrossovers = 2 * kDividers;  // 8
    static constexpr int kBands = kCrossovers + 1;     // 9

    std::array<double, kCrossovers> crossoverHz {};    // ascending
    std::array<std::array<double, kBands>, kFrankMaxSlots> weight {}; // [slot][band], sums to 1 per band (or 0 = silence)

    // What the UI should draw: the audible sections after removing muted / unloaded amps.
    // Fixed-size (no allocation), because the layout is also computed on the audio thread.
    struct Section { int slot = 0; double loHz = 0, hiHz = 0; int sourceIndex = 0; };
    std::array<Section, 5> visibleSections {};
    int numVisible = 0;
    std::array<double, 4> visibleDividersHz {};      // between visible sections (numVisible - 1)
    std::array<bool, 4> dividerIsMerged {};          // true = created by removing a muted section (not draggable)
    std::array<double, kFrankMaxSlots> spectrumShare {}; // share of the log spectrum per slot (for the % display)
};

FrankensteinLayout computeFrankensteinLayout(const FrankensteinSettings&, const std::array<bool, kFrankMaxSlots>& audible);

class FrankensteinMixer
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    void setTarget(const FrankensteinLayout&) noexcept;   // audio thread
    void snapToTarget() noexcept;

    // amps[s] = processed output of slot s (n samples, may be nullptr if not loaded).
    // gains[s] = level-match gain per slot. Writes the blended signal to out.
    void process(const std::array<const double*, kFrankMaxSlots>& amps, const std::array<double, kFrankMaxSlots>& gains,
                 double* out, int n) noexcept
    {
        process(amps, gains, gains, out, nullptr, n);
    }
    // Stereo (per-amp PAN): gainsL/gainsR include the pan gains; outR may be nullptr (mono).
    // The band split is shared; each channel has its own phase compensation, so both channels
    // reconstruct perfectly.
    void process(const std::array<const double*, kFrankMaxSlots>& amps, const std::array<double, kFrankMaxSlots>& gainsL,
                 const std::array<double, kFrankMaxSlots>& gainsR, double* outL, double* outR, int n) noexcept;

    // NOTES mode: splits ONE signal (the guitar) into one band-limited input per slot,
    // outs[s] = sum over bands of weight[s][band] x band (phase-compensated); the inputs of all slots
    // add up to an all-pass of `in` (perfect reconstruction). outs[s] may be nullptr (slot unused).
    void processSplit(const double* in, const std::array<double*, kFrankMaxSlots>& outs, int n) noexcept;

private:
    struct Svf
    {
        double ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
    };
    struct Coef { double a1 = 0, a2 = 0, a3 = 0, k = 1.41421356237; };
    static Coef coef(double fc, double fs) noexcept;

    static constexpr int C = FrankensteinLayout::kCrossovers;
    static constexpr int B = FrankensteinLayout::kBands;

    double fs = 48000.0, smooth = 0.0;
    std::array<double, C> logF {}, tLogF {};
    std::array<Coef, C> coefs {};
    std::array<std::array<double, B>, kFrankMaxSlots> w {}, tW {};

    // per slot, per crossover: stage-1 SVF (gives LP2 and HP2), stage-2 LP2, stage-2 HP2
    std::array<std::array<std::array<Svf, 3>, C>, kFrankMaxSlots> split {};
    std::array<Svf, C> allpass {}, allpassR {};
    std::array<std::array<Svf, C>, kFrankMaxSlots> allpassSlot {}; // NOTES mode: one compensation chain per slot
    void glide() noexcept;
};

} // namespace ampsurd

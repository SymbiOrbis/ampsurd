#pragma once

// Engine: AMPSURD's five fixed capture paths and the mixer.
//
//   in ─┬─ slot 1: capture → align (time/polarity/phase) → EQ → mix gain ─┐
//       ├─ slot 2 ...                                                    ├─ Σ → out
//       └─ slot 5 ...                                                    ┘
//
// MIX LAW (see docs/REVIEW.md §5 and tools/mix_experiment):
//   1. Percentages: p_i = w_i / Σ w_j over audible loaded slots (mute/solo aware), Σ p_i = 1.
//      Stored fader values w_i are never modified by mute/solo.
//   2. Loudness compensation G keeps the mix as loud as the percentage-weighted average of
//      the individual captures:   G = sqrt( Σ p_i C_ii / Σ_ij p_i p_j C_ij )
//      where C is the measured, loudness-weighted (ITU-R BS.1770 K-weighting) covariance of
//      the aligned, level-matched capture outputs for a guitar-like test signal. G is a STATIC gain that only changes when the
//      mix, the captures or the alignment change - it never reacts to the playing, so
//      dynamics are untouched. Limited to [-6, +7] dB; until a capture has been measured it
//      is treated as fully coherent (G = 1, i.e. never louder than expected).
//   3. Path gain = p_i * G * levelGain_i, smoothed (no zipper noise, no clicks on mute/solo).
//
// Threading: prepare() non-RT with audio stopped; setCovariance() from any thread;
// process() on the audio thread only (lock-free, allocation-free).

#include <array>
#include <atomic>
#include <vector>

#include "ampsurd/CaptureSlot.h"
#include "ampsurd/Frankenstein.h"
#include "ampsurd/ParametricEq.h"
#include "ampsurd/PathAligner.h"

namespace ampsurd
{

constexpr int kNumSlots = 5;

struct SlotSettings
{
    float mix = 20.0f;          // stored fader value (relative weight), 0..100
    bool mute = false;
    bool solo = false;
    bool eqEnabled = true;
    std::array<EqBand, ParametricEq::kNumBands> eq = ParametricEq::defaultBands();
    double delaySamples = 0.0;  // total alignment delay applied to this path (>= 0)
    double polarity = 1.0;      // +1 / -1
    double phaseRadians = 0.0;  // frequency-independent phase rotation
    double levelGain = 1.0;     // level match (from the capture's measured loudness), linear
};

struct EngineSettings
{
    std::array<SlotSettings, kNumSlots> slots {};
    FrankensteinSettings frankenstein {};   // frequency-split blending replaces the fader blend when enabled
    bool rotationActive = false; // true when any path uses phase rotation
};

class Engine
{
public:
    static constexpr double kMaxCompensationDb = 7.0;
    static constexpr double kMinCompensationDb = -6.0;

    Engine();

    // Non-RT, audio stopped. maxDelayMs: largest alignment delay that will ever be requested.
    void prepare(double sampleRate, int maxBlockSize, double maxDelayMs);

    CaptureSlot& getSlot(int i) noexcept { return slots[(size_t) i]; }

    // Any thread. C[i][j] = covariance of aligned capture outputs; valid[i] = slot i measured.
    void setCovariance(const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                       const std::array<bool, kNumSlots>& valid) noexcept;

    // Audio thread. out may alias nothing; n <= maxBlockSize.
    void process(const double* in, double* out, int n, const EngineSettings& settings) noexcept;

    // Any thread (diagnostics / UI).
    float getEffectivePercent(int i) const noexcept { return effectivePercent[(size_t) i].load(std::memory_order_relaxed); }
    bool isSlotLoaded(int i) const noexcept { return loaded[(size_t) i].load(std::memory_order_relaxed); }
    float getCompensationDb() const noexcept { return compensationDb.load(std::memory_order_relaxed); }

    // Pure function used by process() and by the tests / experiment tool.
    static double computeCompensation(const std::array<double, kNumSlots>& p,
                                      const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                                      const std::array<bool, kNumSlots>& valid) noexcept;

    // Pure function: loaded && !muted && (no solo || soloed).
    static std::array<bool, kNumSlots> computeAudible(const EngineSettings& s, const std::array<bool, kNumSlots>& isLoaded) noexcept;

    // Pure function: effective percentages (sum to 1 over audible loaded slots).
    static std::array<double, kNumSlots> computeProportions(const EngineSettings& s,
                                                            const std::array<bool, kNumSlots>& isLoaded) noexcept;

private:
    std::array<CaptureSlot, kNumSlots> slots;
    std::array<PathAligner, kNumSlots> aligners;
    std::array<ParametricEq, kNumSlots> eqs;
    std::array<double, kNumSlots> gains {};

    std::array<std::array<std::atomic<double>, kNumSlots>, kNumSlots> cov;
    std::array<std::atomic<bool>, kNumSlots> covValid;

    std::array<std::atomic<float>, kNumSlots> effectivePercent;
    std::array<std::atomic<bool>, kNumSlots> loaded;
    std::atomic<float> compensationDb { 0.0f };

    std::vector<double> scratch, frankOut;
    FrankensteinMixer frankenstein;
    double frankMix = 0.0, frankCoef = 0.0;
    double gainCoef = 0.0;
    double sampleRate = 48000.0;
};

} // namespace ampsurd

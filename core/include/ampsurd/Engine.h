#pragma once

// Engine: AMPSURD's five fixed capture paths and the mixer.
//
//   in ─┬─ slot 1: capture → [IR → IR EQ] → align (time/polarity/phase) → EQ → mix gain ─┐
//       ├─ slot 2 ...                                                    ├─ Σ (blend or
//       └─ slot 5 ...                                                    ┘   Frankenstein) → GLOBAL EQ → out L/R
//
// PAN (per amp, stereo output): constant power with the centre at 0 dB (a centred amp is exactly as
// before, both channels identical); hard left/right = +3 dB on one channel, nothing on the other.
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
#include "ampsurd/IrSlot.h"
#include "ampsurd/ParallelRunner.h"
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
    float pan = 0.0f;           // -1 = hard left, 0 = centre, +1 = hard right
    double inputTrim = 1.0;     // input calibration: drives this capture at the level it was recorded at (linear)
    bool irEnabled = true;      // IR ON / BYPASS (only matters when an IR is loaded)
    bool irEqEnabled = true;    // the IR's own EQ (bypassed together with the IR)
    std::array<EqBand, ParametricEq::kNumBands> irEq = ParametricEq::defaultBands();
};

struct EngineSettings
{
    std::array<SlotSettings, kNumSlots> slots {};
    FrankensteinSettings frankenstein {};   // frequency-split blending replaces the fader blend when enabled
    bool globalEqEnabled = false;           // final tone-shaping EQ on the blended signal
    std::array<EqBand, ParametricEq::kNumBands> globalEq = ParametricEq::defaultBands();
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
    IrSlot& getIrSlot(int i) noexcept { return irSlots[(size_t) i]; }

    // Any thread. C[i][j] = covariance of aligned capture outputs; valid[i] = slot i measured.
    void setCovariance(const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                       const std::array<bool, kNumSlots>& valid) noexcept;

    // Audio thread. Outputs must not alias the input; n <= maxBlockSize.
    void process(const double* in, double* outL, double* outR, int n, const EngineSettings& settings) noexcept;
    // Mono convenience (tests / tools): left channel only.
    void process(const double* in, double* out, int n, const EngineSettings& settings) noexcept
    {
        process(in, out, nullptr, n, settings);
    }

    // Non-RT (call before / instead of prepare, audio stopped): use several CPU cores for the amp
    // paths. workers = -1: as many as useful on this computer, 0: single core.
    void setMultiCore(int workers);
    int getNumWorkers() const noexcept { return runner.numWorkers(); }
    // Any thread (diagnostics): how many amp paths are currently processed (silent ones sleep).
    int getActivePaths() const noexcept { return activePaths.load(std::memory_order_relaxed); }

    // Pan gains {left, right}: constant power, centre = exactly {1, 1}.
    static std::array<double, 2> panGains(float pan) noexcept;

    // Any thread (diagnostics / UI).
    float getEffectivePercent(int i) const noexcept { return effectivePercent[(size_t) i].load(std::memory_order_relaxed); }
    bool isSlotLoaded(int i) const noexcept { return loaded[(size_t) i].load(std::memory_order_relaxed); }
    float getCompensationDb() const noexcept { return compensationDb.load(std::memory_order_relaxed); }

    // Pure function used by process() and by the tests / experiment tool.
    static double computeCompensation(const std::array<double, kNumSlots>& p,
                                      const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                                      const std::array<bool, kNumSlots>& valid) noexcept;
    // With PAN: loudness = sum of both channels' powers (as ITU-R BS.1770 does), so amps panned apart
    // (which add less coherently) are compensated too. All centred -> identical to the mono version.
    static double computeCompensation(const std::array<double, kNumSlots>& p,
                                      const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                                      const std::array<bool, kNumSlots>& valid,
                                      const std::array<std::array<double, 2>, kNumSlots>& pan) noexcept;

    // Pure function: loaded && !muted && (no solo || soloed).
    static std::array<bool, kNumSlots> computeAudible(const EngineSettings& s, const std::array<bool, kNumSlots>& isLoaded) noexcept;

    // Pure function: effective percentages (sum to 1 over audible loaded slots).
    static std::array<double, kNumSlots> computeProportions(const EngineSettings& s,
                                                            const std::array<bool, kNumSlots>& isLoaded) noexcept;

private:
    std::array<CaptureSlot, kNumSlots> slots;
    std::array<IrSlot, kNumSlots> irSlots;
    std::array<ParametricEq, kNumSlots> irEqs;
    std::array<PathAligner, kNumSlots> aligners;
    std::array<ParametricEq, kNumSlots> eqs;
    ParametricEq globalEq, globalEqR;
    std::array<double, kNumSlots> gains {}, gainsR {};

    std::array<std::array<std::atomic<double>, kNumSlots>, kNumSlots> cov;
    std::array<std::atomic<bool>, kNumSlots> covValid;

    std::array<std::atomic<float>, kNumSlots> effectivePercent;
    std::array<std::atomic<bool>, kNumSlots> loaded;
    std::atomic<float> compensationDb { 0.0f };

    std::vector<double> scratch, frankOut, frankOutR, scratchR;
    FrankensteinMixer frankenstein;
    FrankensteinMixer splitter;       // Frankenstein NOTES mode: splits the guitar before the amps
    std::vector<double> splitIn;     // one input per slot (NOTES mode)
    bool notesRouting = false, splitterSnap = true;
    std::vector<double> trimBuf;                 // calibrated inputs (one per slot)
    std::array<double, kNumSlots> trimNow {};    // smoothed input trims (no zipper noise when changed)
    double trimCoef = 0.0;

    // Silent amps sleep: a path that cannot be heard (muted, 0 %, not in the Frankenstein layout)
    // stops being processed 0.5 s after it went silent; when it is needed again it runs 100 ms
    // unheard (fresh internal state) before it fades in, exactly like a newly loaded capture.
    std::array<bool, kNumSlots> asleep {};
    std::array<int, kNumSlots> holdLeft {}, warmLeft {};
    int holdSamples = 24000, warmSamples = 4800;
    std::atomic<int> activePaths { 0 };

    // parallel amp paths (one job per running path)
    ParallelRunner runner;
    int wantedWorkers = -1;
    struct PathJob
    {
        Engine* engine = nullptr;
        const EngineSettings* settings = nullptr;
        const double* in = nullptr;
        int n = 0, stride = 0;
        double rotationMix = 0.0;
        std::array<int, kNumSlots> slot {};
    } job;
    static void runPathJob(void* context, int index) noexcept;
    void processPath(int i, const EngineSettings& s, const double* in, int n, int stride, double rotationMix) noexcept;
    double routeGain = 1.0, routeStep = 0.001; // short dip while switching TONE <-> NOTES
    double frankMix = 0.0, frankCoef = 0.0;
    double gainCoef = 0.0;
    double sampleRate = 48000.0;
};

} // namespace ampsurd

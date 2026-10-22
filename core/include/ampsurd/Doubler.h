#pragma once

// DOUBLER (experimental module, docs/DOUBLER.md): makes one guitar sound like 2-4 takes of the same part
// played together, the classic metal / rock "double tracking" - from ONE guitar input, in real time.
//
// Own code (2026-10-22). Ideas from a tester's REAPER script ("Performance Doubler") and from how doublers
// such as TC Electronic's MIMIQ are described: doubles that are NOT static copies - their timing and
// pitch keep moving slowly and differently, every note starts with its own small intonation and pick
// strength, every take has its own tone - and every bigger change happens where nobody can hear it.
//
//   mix L/R ─┬──────────────────────────── dry (centre, or left with 1 double) ──────────────┐
//            └ mid ─ delay line ─┬ voice 1: moving delay ─ tone ─ HP (LOW MONO) ─ pan ─────(+)─ out L/R
//                                ├ voice 2 ...                                              │
//                                └ voice 3 ...                                              │
//   loudness kept: the highs of dry + doubles are scaled so the whole is as loud as the dry ┘
//
// * Each voice reads the delay line 8-35 ms behind (TIGHT sets how far and how much it wanders). Its
//   delay moves smoothly (a slowly varying speed = a pitch drift of a few cents), never jumps while sound
//   is read: a new timing and tone for the take is drawn only in a silence longer than the longest delay.
// * Drift runs at full speed while notes are being played and slows down in sustained notes (where
//   moving combs would be heard as phasing).
// * At every pick attack (onset detector) each voice gets, when it plays that attack, its own small pitch
//   offset (relaxes over ~0.3 s) and pick strength (+-dB, CHARACTER).
// * Tone per take (CHARACTER): high shelf +-dB and a short "pick position" comb, drawn per take.
// * LOW MONO: below this frequency only the dry signal plays (tight, centred lows); 0 = off.
// * The dry signal is not delayed: no latency.
// * Off: faded out in 30 ms, then not processed at all (bit-identical to a build without it).
//
// prepare(): non-RT. process(): audio thread, no allocation.

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace ampsurd
{

struct DoublerSettings
{
    bool on = false;
    int voices = 2;           // 1 .. 3 doubles
    double tight = 0.6;       // 0 = loose (late, wandering) .. 1 = tight
    double levelDb = -2.0;    // each double vs. the dry guitar
    double width = 1.0;       // 0 .. 1
    double lowMonoHz = 120.0; // 0 = off
    double character = 0.5;   // 0 .. 1: tone / attack differences between the takes
};

class Doubler
{
public:
    static constexpr int kMaxVoices = 3;

    // seed 0: a new random take every time the plugin starts
    void prepare(double sampleRate, int maxBlockSize, uint32_t seed = 0);
    void reset() noexcept;
    // Audio thread, in place (stereo).
    void process(double* L, double* R, int n, const DoublerSettings& s) noexcept;

    bool isActive() const noexcept { return mix > 0.0 || wantOn; }

    // diagnostics (any thread)
    float getVoiceDelayMs(int v) const noexcept { return delayMsNow[(size_t) v].load(std::memory_order_relaxed); }
    int getRedraws() const noexcept { return redraws.load(std::memory_order_relaxed); }
    int getOnsets() const noexcept { return onsets.load(std::memory_order_relaxed); }

private:
    struct Svf { double ic1 = 0, ic2 = 0; };
    struct Voice
    {
        double centre = 0.0, centreNew = 0.0; // samples
        double ou = 0.0, x1 = 0.0, x2 = 0.0;  // random wander (OU process, then 2 smoothing stages)
        double note = 0.0, noteTarget = 0.0;  // per-note intonation (delay offset, samples)
        double noteVel = 0.0;
        double gainNow = 1.0, gainTarget = 1.0;
        double shelfDb = 0.0, shelfGain = 0.0, combT = 0.0, combG = 0.0, combNorm = 1.0, target = 0.0;
        double hsLp = 0.0;                    // high shelf state
        std::array<Svf, 2> hp {};             // LOW MONO high-pass (LR4)
        int pending = -1;                     // samples until this voice plays a detected attack
        double pendCents = 0.0, pendGainDb = 0.0;
        double xfade = 1.0;                   // 1 = reading `centre` only (else crossfading to centreNew)
        double delayNow = 0.0;
    };

    double rnd() noexcept;       // 0..1
    double gauss() noexcept;
    void drawTake(Voice& v, int index, const DoublerSettings& s, bool first) noexcept;
    double read(double delaySamples) const noexcept; // windowed-sinc fractional read
    double readCubic(double delaySamples) const noexcept;
    void crossoverCoefs(double hz) noexcept;

    double sr = 48000.0;
    std::vector<double> line;   // power of two
    size_t mask = 0, wpos = 0;
    std::array<std::array<double, 16>, 257> sinc {};
    std::array<Voice, kMaxVoices> voices {};
    uint64_t rng = 1;
    uint32_t seedSetting = 0;

    // on / off
    double mix = 0.0, mixStep = 0.0;
    bool wantOn = false;

    // onset / activity / silence detection (on the mid signal)
    double envFast = 0.0, envSlow = 0.0, hpX = 0.0, hpY = 0.0, peak = 0.0, activity = 0.0, level = 0.0;
    bool armed = true;
    int refractory = 0, silent = 0;

    // crossover (LR4 = two Butterworth SVFs) for LOW MONO; dry is split too
    double xg = 0.0, xa1 = 0.0, xa2 = 0.0, xa3 = 0.0, xHz = -1.0;
    std::array<Svf, 2> dryLpL {}, dryLpR {}, dryHpL {}, dryHpR {};

    // smoothed parameters
    double widthNow = 1.0, levelNow = 1.0, kNow = 1.0;
    std::array<double, kMaxVoices> voiceOn {};
    int lastVoices = -1;
    double lmWeight = 0.0, dryPan = 0.0, dryL = 1.0, dryR = 1.0;
    std::array<double, kMaxVoices> panL {}, panR {};

    std::array<std::atomic<float>, kMaxVoices> delayMsNow {};
    std::atomic<int> redraws { 0 }, onsets { 0 };
};

} // namespace ampsurd

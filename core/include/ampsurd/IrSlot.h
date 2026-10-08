#pragma once

// IrSlot: the real-time home of one slot's cabinet IR (same threading contract as CaptureSlot).
//
//   - submit(): any non-audio thread. A Convolver built off the audio thread; an empty Convolver
//     removes the IR. Adopted at the next block with a 20 ms crossfade (old IR -> new IR, or to/from
//     the dry signal), so loading, replacing and removing IRs never clicks.
//   - process(x, n, enabled): audio thread, in place. `enabled` = the slot's IR ON/BYPASS switch;
//     switching crossfades over 20 ms. While bypassed (after the fade) or without an IR, the
//     convolver is not run at all (no CPU).
//   - collectGarbage(): non-audio thread, frees replaced convolvers.

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "ampsurd/Convolver.h"

namespace ampsurd
{

class IrSlot
{
public:
    IrSlot() = default;
    ~IrSlot();
    IrSlot(const IrSlot&) = delete;
    IrSlot& operator=(const IrSlot&) = delete;

    void prepare(double sampleRate, int maxBlockSize);          // non-RT, audio stopped
    void replaceNow(std::unique_ptr<Convolver> c);              // non-RT, audio stopped (sample-rate change)
    void submit(std::unique_ptr<Convolver> c);                  // any non-audio thread
    void collectGarbage();                                      // any non-audio thread

    void process(double* x, int n, bool enabled) noexcept;      // audio thread
    bool hasIr() const noexcept { return active != nullptr && !active->isEmpty(); } // audio thread
    bool isRunning() const noexcept { return running.load(std::memory_order_relaxed); } // any thread (diagnostics)

    static constexpr double kFadeSeconds = 0.020;
    // A convolver that starts from silence would play a short onset transient. A newly loaded or
    // re-enabled IR therefore first runs silently for up to this many samples (43 ms at 48 kHz),
    // until it has "heard" enough of the guitar, and only then fades in.
    static constexpr int kWarmUpSamples = 2048;

private:
    void retire(Convolver* c) noexcept;
    bool canRetire() const noexcept;

    Convolver* active = nullptr;
    Convolver* fadingOut = nullptr;
    int fadePos = 0, fadeLength = 1;
    double wet = 0.0, wetStep = 1.0;
    bool needsReset = false;
    int warm = 0;
    std::vector<double> dry, oldOut;
    std::atomic<Convolver*> pending { nullptr };
    std::atomic<bool> running { false };

    static constexpr int kRetireCapacity = 16;
    std::array<std::atomic<Convolver*>, kRetireCapacity> retired {};
    std::atomic<int> retireWrite { 0 }, retireRead { 0 };
};

} // namespace ampsurd

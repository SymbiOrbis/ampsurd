#pragma once

// CaptureSlot: the real-time home of one capture.
//
// Threading contract
//   - submit(), collectGarbage(), getters marked "any thread": message/loader threads.
//   - prepare(): non-RT, only while the audio thread is NOT running (prepareToPlay).
//   - process(): audio thread only. Lock-free, allocation-free, no file access.
//
// Swapping captures: the loader prepares a CaptureModel off the audio thread and
// submit()s it. At the start of the next block the audio thread adopts it and
// crossfades from the old capture over ~20 ms, so changing captures does not click.
// The old capture is handed back through a lock-free queue and deleted later on a
// non-audio thread by collectGarbage().

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "ampsurd/CaptureModel.h"

namespace ampsurd
{

class CaptureSlot
{
public:
    CaptureSlot();
    ~CaptureSlot();

    CaptureSlot(const CaptureSlot&) = delete;
    CaptureSlot& operator=(const CaptureSlot&) = delete;

    // Non-RT, audio stopped. Re-prepares every model the slot owns.
    void prepare(double sampleRate, int maxBlockSize);

    // Any non-audio thread. Pass a model prepared for the current sample rate/block size
    // (or CaptureModel::makeEmpty() to unload). Ownership transfers to the slot.
    void submit(std::unique_ptr<CaptureModel> model);

    // Any non-audio thread. Frees models the audio thread no longer uses.
    void collectGarbage();

    // Audio thread. Overwrites out[0..n). n <= maxBlockSize.
    void process(const Sample* in, Sample* out, int n, bool normalise) noexcept;

    // Audio thread: true when a real (non-empty) capture is currently playing.
    bool hasActiveCapture() const noexcept { return active != nullptr && !active->isEmpty(); }
    // Audio thread: a newly submitted capture is waiting to be adopted.
    bool hasPending() const noexcept { return pending.load(std::memory_order_relaxed) != nullptr; }

    // Any thread: latency of the capture currently playing (updated on the audio thread).
    int getLatencySamples() const noexcept { return latency.load(std::memory_order_relaxed); }

    static constexpr double kCrossfadeSeconds = 0.020;

private:
    void retire(CaptureModel* m) noexcept; // audio thread

    // Owned by the audio thread while it runs; touched elsewhere only in prepare()/destructor.
    CaptureModel* active = nullptr;
    CaptureModel* fadingOut = nullptr;
    int fadePos = 0;
    int fadeLength = 1;
    std::vector<Sample> fadeScratch;

    std::atomic<CaptureModel*> pending { nullptr };
    std::atomic<int> latency { 0 };

    // Single-producer (audio) / single-consumer (garbage collector) ring of retired models.
    static constexpr int kRetireCapacity = 16;
    std::array<std::atomic<CaptureModel*>, kRetireCapacity> retired {};
    std::atomic<int> retireWrite { 0 };
    std::atomic<int> retireRead { 0 };
};

} // namespace ampsurd

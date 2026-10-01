#include "monstrosity/CaptureSlot.h"

#include <algorithm>
#include <cmath>

namespace monstrosity
{

CaptureSlot::CaptureSlot() = default;

CaptureSlot::~CaptureSlot()
{
    collectGarbage();
    delete pending.exchange(nullptr);
    delete fadingOut;
    delete active;
}

void CaptureSlot::prepare(double sampleRate, int maxBlockSize)
{
    // The audio thread is stopped, so we may touch its state directly.
    collectGarbage();

    delete fadingOut;
    fadingOut = nullptr;

    if (auto* p = pending.exchange(nullptr))
    {
        delete active;
        active = p;
    }

    if (active != nullptr)
        active->prepare(sampleRate, maxBlockSize);

    fadeLength = std::max(1, (int) std::lround(sampleRate * kCrossfadeSeconds));
    fadePos = fadeLength; // nothing to fade
    fadeScratch.assign((size_t) std::max(1, maxBlockSize), 0.0);
    latency.store(active ? active->getLatencySamples() : 0, std::memory_order_relaxed);
}

void CaptureSlot::submit(std::unique_ptr<CaptureModel> model)
{
    // If an earlier submission was never picked up by the audio thread, it is
    // returned here and can be deleted safely on this (non-audio) thread.
    delete pending.exchange(model.release(), std::memory_order_acq_rel);
}

void CaptureSlot::collectGarbage()
{
    int r = retireRead.load(std::memory_order_relaxed);
    const int w = retireWrite.load(std::memory_order_acquire);
    while (r != w)
    {
        delete retired[(size_t) (r % kRetireCapacity)].exchange(nullptr, std::memory_order_acquire);
        ++r;
    }
    retireRead.store(r, std::memory_order_release);
}

void CaptureSlot::retire(CaptureModel* m) noexcept
{
    // Called only when there is room (checked before adopting a new model).
    const int w = retireWrite.load(std::memory_order_relaxed);
    retired[(size_t) (w % kRetireCapacity)].store(m, std::memory_order_release);
    retireWrite.store(w + 1, std::memory_order_release);
}

void CaptureSlot::process(const Sample* in, Sample* out, int n, bool normalise) noexcept
{
    // 1. Adopt a newly submitted capture, but only when no fade is running and there is
    //    room to hand the previous one back for deletion.
    if (fadingOut == nullptr && pending.load(std::memory_order_relaxed) != nullptr)
    {
        const int used = retireWrite.load(std::memory_order_relaxed) - retireRead.load(std::memory_order_acquire);
        if (used < kRetireCapacity)
        {
            if (auto* next = pending.exchange(nullptr, std::memory_order_acq_rel))
            {
                fadingOut = active; // may be nullptr: then we fade in from silence
                active = next;
                fadePos = 0;
                latency.store(active->getLatencySamples(), std::memory_order_relaxed);
            }
        }
    }

    // 2. Run the active capture.
    if (active != nullptr)
        active->process(in, out, n, normalise);
    else
        std::fill(out, out + n, 0.0);

    // 3. Crossfade (linear, ~20 ms) from the previous capture or from silence.
    if (fadePos < fadeLength)
    {
        const bool haveOld = fadingOut != nullptr;
        if (haveOld)
            fadingOut->process(in, fadeScratch.data(), n, normalise);

        const double inv = 1.0 / (double) fadeLength;
        for (int i = 0; i < n; ++i)
        {
            const double g = std::min(1.0, (double) (fadePos + i) * inv);
            out[i] = out[i] * g + (haveOld ? fadeScratch[(size_t) i] * (1.0 - g) : 0.0);
        }
        fadePos += n;
    }

    if (fadePos >= fadeLength && fadingOut != nullptr)
    {
        retire(fadingOut);
        fadingOut = nullptr;
    }
}

} // namespace monstrosity

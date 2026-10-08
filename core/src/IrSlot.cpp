#include "ampsurd/IrSlot.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{

IrSlot::~IrSlot()
{
    collectGarbage();
    delete pending.exchange(nullptr);
    delete fadingOut;
    delete active;
}

void IrSlot::prepare(double sampleRate, int maxBlockSize)
{
    collectGarbage();
    delete fadingOut;
    fadingOut = nullptr;
    if (auto* p = pending.exchange(nullptr))
    {
        delete active;
        active = p;
    }
    if (active) active->reset();
    fadeLength = std::max(1, (int) std::lround(sampleRate * kFadeSeconds));
    fadePos = fadeLength;
    wetStep = 1.0 / fadeLength;
    dry.assign((size_t) std::max(1, maxBlockSize), 0.0);
    oldOut.assign(dry.size(), 0.0);
    needsReset = false;
}

void IrSlot::replaceNow(std::unique_ptr<Convolver> c)
{
    delete active;
    active = c.release();
    if (active) active->reset();
}

void IrSlot::submit(std::unique_ptr<Convolver> c)
{
    delete pending.exchange(c.release(), std::memory_order_acq_rel);
}

void IrSlot::collectGarbage()
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

bool IrSlot::canRetire() const noexcept
{
    // room for two (the active one and a fading one)
    return retireWrite.load(std::memory_order_relaxed) - retireRead.load(std::memory_order_acquire) < kRetireCapacity - 1;
}

void IrSlot::retire(Convolver* c) noexcept
{
    if (c == nullptr) return;
    const int w = retireWrite.load(std::memory_order_relaxed);
    retired[(size_t) (w % kRetireCapacity)].store(c, std::memory_order_release);
    retireWrite.store(w + 1, std::memory_order_release);
}

void IrSlot::process(double* x, int n, bool enabled) noexcept
{
    // 1. adopt a newly submitted IR
    if (fadingOut == nullptr && pending.load(std::memory_order_relaxed) != nullptr && canRetire())
        if (auto* next = pending.exchange(nullptr, std::memory_order_acq_rel))
        {
            if (wet == 0.0)
            {
                retire(active); // not heard: no crossfade needed
                active = next;
                needsReset = true;
            }
            else
            {
                fadingOut = active; // nullptr = dry
                active = next;
                // the new IR warms up silently, then the crossfade starts
                fadePos = next->isEmpty() ? 0 : -std::min(next->getLength(), kWarmUpSamples);
            }
        }

    const bool haveIr = active != nullptr && !active->isEmpty();
    const bool haveOld = fadingOut != nullptr && !fadingOut->isEmpty();
    const double target = enabled ? 1.0 : 0.0;

    // 2. nothing to do: no IR at all, or bypassed and faded out -> the signal passes untouched
    if ((!haveIr && !haveOld && fadePos >= fadeLength) || (wet == 0.0 && target == 0.0))
    {
        if (fadingOut != nullptr) { retire(fadingOut); fadingOut = nullptr; fadePos = fadeLength; }
        wet = haveIr ? 0.0 : target; // without an IR the switch has no audible effect
        needsReset = true;
        running.store(false, std::memory_order_relaxed);
        return;
    }
    if (needsReset)
    {
        if (active) active->reset(); // start cleanly after being idle ...
        warm = haveIr ? std::min(active->getLength(), kWarmUpSamples) : 0; // ... and warm up before fading in
        needsReset = false;
    }
    running.store(true, std::memory_order_relaxed);

    // 3. convolve (dry copy kept for the crossfades)
    std::copy(x, x + n, dry.begin());
    if (haveIr) active->process(dry.data(), x, n);

    // 4. crossfade from the previous IR (or from dry)
    if (fadePos < fadeLength)
    {
        if (haveOld) fadingOut->process(dry.data(), oldOut.data(), n);
        const double* old = haveOld ? oldOut.data() : dry.data();
        for (int i = 0; i < n; ++i)
        {
            const double g = std::clamp((double) (fadePos + i + 1) / fadeLength, 0.0, 1.0); // < 0: warming up
            x[i] = g * x[i] + (1.0 - g) * old[i];
        }
        fadePos += n;
    }
    if (fadePos >= fadeLength && fadingOut != nullptr)
    {
        retire(fadingOut);
        fadingOut = nullptr;
    }

    // 5. IR ON / BYPASS
    if (wet != 1.0 || target != 1.0 || warm > 0)
        for (int i = 0; i < n; ++i)
        {
            if (warm > 0) --warm; // still warming up: stay dry
            else wet = target > wet ? std::min(target, wet + wetStep) : std::max(target, wet - wetStep);
            x[i] = wet * x[i] + (1.0 - wet) * dry[(size_t) i];
        }
}

} // namespace ampsurd

#include "ampsurd/Engine.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{

Engine::Engine()
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        for (int j = 0; j < kNumSlots; ++j)
            cov[(size_t) i][(size_t) j].store(i == j ? 1.0 : 0.0);
        covValid[(size_t) i].store(false);
        effectivePercent[(size_t) i].store(0.0f);
        loaded[(size_t) i].store(false);
    }
}

void Engine::prepare(double sr, int maxBlockSize, double maxDelayMs)
{
    sampleRate = sr;
    const int maxBlock = std::max(1, maxBlockSize);
    const int maxDelay = (int) std::ceil(maxDelayMs * 0.001 * sr) + 2;
    scratch.assign((size_t) maxBlock * kNumSlots, 0.0);
    for (int i = 0; i < kNumSlots; ++i)
    {
        slots[(size_t) i].prepare(sr, maxBlock);
        aligners[(size_t) i].prepare(sr, maxDelay);
        eqs[(size_t) i].prepare(sr);
        gains[(size_t) i] = 0.0;
    }
    gainCoef = 1.0 - std::exp(-1.0 / (0.025 * sr)); // 25 ms gain smoothing
}

void Engine::setCovariance(const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                           const std::array<bool, kNumSlots>& valid) noexcept
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        for (int j = 0; j < kNumSlots; ++j)
            cov[(size_t) i][(size_t) j].store(C[(size_t) i][(size_t) j], std::memory_order_relaxed);
        covValid[(size_t) i].store(valid[(size_t) i], std::memory_order_release);
    }
}

std::array<double, kNumSlots> Engine::computeProportions(const EngineSettings& s,
                                                         const std::array<bool, kNumSlots>& isLoaded) noexcept
{
    bool anySolo = false;
    for (int i = 0; i < kNumSlots; ++i)
        anySolo = anySolo || (isLoaded[(size_t) i] && s.slots[(size_t) i].solo);

    std::array<bool, kNumSlots> audible {};
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& ss = s.slots[(size_t) i];
        audible[(size_t) i] = isLoaded[(size_t) i] && !ss.mute && (!anySolo || ss.solo);
        if (audible[(size_t) i])
        {
            sum += std::max(0.0f, ss.mix);
            ++count;
        }
    }

    std::array<double, kNumSlots> p {};
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (!audible[(size_t) i])
            continue;
        p[(size_t) i] = sum > 1e-9 ? std::max(0.0f, s.slots[(size_t) i].mix) / sum : 1.0 / count;
    }
    return p;
}

double Engine::computeCompensation(const std::array<double, kNumSlots>& p,
                                   const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                                   const std::array<bool, kNumSlots>& valid) noexcept
{
    // Unmeasured slots: unit power, fully coherent with everything (=> no boost).
    auto var = [&](int i) { return valid[(size_t) i] ? std::max(0.0, C[(size_t) i][(size_t) i]) : 1.0; };

    double num = 0.0, den = 0.0;
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (p[(size_t) i] <= 0.0) continue;
        num += p[(size_t) i] * var(i);
        for (int j = 0; j < kNumSlots; ++j)
        {
            if (p[(size_t) j] <= 0.0) continue;
            const double cij = (i == j) ? var(i)
                             : (valid[(size_t) i] && valid[(size_t) j]) ? C[(size_t) i][(size_t) j]
                                                                        : std::sqrt(var(i) * var(j));
            den += p[(size_t) i] * p[(size_t) j] * cij;
        }
    }
    if (num <= 0.0)
        return 1.0;
    const double maxG = std::pow(10.0, kMaxCompensationDb / 20.0);
    const double minG = std::pow(10.0, kMinCompensationDb / 20.0);
    if (den <= num / (maxG * maxG))
        return maxG;
    return std::clamp(std::sqrt(num / den), minG, maxG);
}

void Engine::process(const double* in, double* out, int n, const EngineSettings& s) noexcept
{
    const int stride = (int) (scratch.size() / kNumSlots);
    if (n > stride)
    {
        std::fill(out, out + n, 0.0);
        return;
    }

    // 1. Run every capture (keeps their internal state warm, also when muted).
    std::array<bool, kNumSlots> isLoaded {};
    for (int i = 0; i < kNumSlots; ++i)
    {
        double* buf = scratch.data() + (size_t) i * (size_t) stride;
        slots[(size_t) i].process(in, buf, n, false); // level match is applied as levelGain
        isLoaded[(size_t) i] = slots[(size_t) i].hasActiveCapture();
        loaded[(size_t) i].store(isLoaded[(size_t) i], std::memory_order_relaxed);
    }

    // 2. Mix law.
    const auto p = computeProportions(s, isLoaded);
    std::array<std::array<double, kNumSlots>, kNumSlots> C {};
    std::array<bool, kNumSlots> valid {};
    for (int i = 0; i < kNumSlots; ++i)
    {
        valid[(size_t) i] = covValid[(size_t) i].load(std::memory_order_acquire);
        for (int j = 0; j < kNumSlots; ++j)
            C[(size_t) i][(size_t) j] = cov[(size_t) i][(size_t) j].load(std::memory_order_relaxed);
    }
    const double G = computeCompensation(p, C, valid);
    compensationDb.store((float) (20.0 * std::log10(G)), std::memory_order_relaxed);

    // 3. Align, EQ, gain, sum.
    std::fill(out, out + n, 0.0);
    const double rotationMix = s.rotationActive ? 1.0 : 0.0;
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& ss = s.slots[(size_t) i];
        double* buf = scratch.data() + (size_t) i * (size_t) stride;
        effectivePercent[(size_t) i].store((float) (100.0 * p[(size_t) i]), std::memory_order_relaxed);

        auto& al = aligners[(size_t) i];
        al.setTargets(ss.delaySamples, ss.polarity, ss.phaseRadians, rotationMix);
        al.process(buf, n);

        auto& eq = eqs[(size_t) i];
        auto bands = ss.eq;
        if (!ss.eqEnabled)
            for (auto& b : bands) b.gainDb = 0.0f;
        eq.setBands(bands);
        eq.process(buf, n);

        const double target = p[(size_t) i] * G * ss.levelGain;
        double g = gains[(size_t) i];
        if (g == 0.0 && target == 0.0)
            continue;
        for (int k = 0; k < n; ++k)
        {
            g += (target - g) * gainCoef;
            out[k] += g * buf[k];
        }
        if (std::abs(target - g) < 1e-9) g = target;
        gains[(size_t) i] = g;
    }
}

} // namespace ampsurd

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
        irSlots[(size_t) i].prepare(sr, maxBlock);
        irEqs[(size_t) i].prepare(sr);
        aligners[(size_t) i].prepare(sr, maxDelay);
        eqs[(size_t) i].prepare(sr);
        gains[(size_t) i] = 0.0;
        gainsR[(size_t) i] = 0.0;
    }
    gainCoef = 1.0 - std::exp(-1.0 / (0.025 * sr)); // 25 ms gain smoothing
    frankCoef = 1.0 - std::exp(-1.0 / (0.030 * sr)); // 30 ms crossfade blend <-> Frankenstein
    frankOut.assign((size_t) maxBlock, 0.0);
    frankOutR.assign((size_t) maxBlock, 0.0);
    scratchR.assign((size_t) maxBlock, 0.0);
    globalEq.prepare(sr);
    globalEqR.prepare(sr);
    frankenstein.prepare(sr, maxBlock);
    frankMix = 0.0;
    splitter.prepare(sr, maxBlock);
    splitIn.assign((size_t) maxBlock * kNumSlots, 0.0);
    trimBuf.assign((size_t) maxBlock, 0.0);
    trimNow.fill(-1.0); // snaps to the first requested trim
    trimCoef = 1.0 - std::exp(-1.0 / (0.020 * sr));
    notesRouting = false;
    splitterSnap = true;
    routeGain = 1.0;
    routeStep = 1.0 / (0.020 * sr);
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

std::array<bool, kNumSlots> Engine::computeAudible(const EngineSettings& s, const std::array<bool, kNumSlots>& isLoaded) noexcept
{
    bool anySolo = false;
    for (int i = 0; i < kNumSlots; ++i)
        anySolo = anySolo || (isLoaded[(size_t) i] && s.slots[(size_t) i].solo);
    std::array<bool, kNumSlots> a {};
    for (int i = 0; i < kNumSlots; ++i)
        a[(size_t) i] = isLoaded[(size_t) i] && !s.slots[(size_t) i].mute && (!anySolo || s.slots[(size_t) i].solo);
    return a;
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

std::array<double, 2> Engine::panGains(float pan) noexcept
{
    if (pan == 0.0f)
        return { 1.0, 1.0 }; // exactly unity: a centred amp sounds exactly as without PAN
    const double theta = (std::clamp((double) pan, -1.0, 1.0) + 1.0) * 0.25 * 3.14159265358979323846;
    return { std::sqrt(2.0) * std::cos(theta), std::sqrt(2.0) * std::sin(theta) };
}

double Engine::computeCompensation(const std::array<double, kNumSlots>& p,
                                   const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                                   const std::array<bool, kNumSlots>& valid) noexcept
{
    std::array<std::array<double, 2>, kNumSlots> centred {};
    for (auto& g : centred) g = { 1.0, 1.0 };
    return computeCompensation(p, C, valid, centred);
}

double Engine::computeCompensation(const std::array<double, kNumSlots>& p,
                                   const std::array<std::array<double, kNumSlots>, kNumSlots>& C,
                                   const std::array<bool, kNumSlots>& valid,
                                   const std::array<std::array<double, 2>, kNumSlots>& pan) noexcept
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
            // average over the two channels of the pan gains' products (centre: exactly 1)
            const double w = 0.5 * (pan[(size_t) i][0] * pan[(size_t) j][0] + pan[(size_t) i][1] * pan[(size_t) j][1]);
            den += p[(size_t) i] * p[(size_t) j] * cij * w;
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

void Engine::process(const double* in, double* out, double* outR, int n, const EngineSettings& s) noexcept
{
    const int stride = (int) (scratch.size() / kNumSlots);
    if (n > stride)
    {
        std::fill(out, out + n, 0.0);
        if (outR != nullptr) std::fill(outR, outR + n, 0.0);
        return;
    }

    // 0. Frankenstein NOTES mode: the guitar is split BEFORE the amps (each note range -> its amp).
    //    Switching between the modes changes what the amps receive, so the output dips for 20 ms.
    const bool notesWanted = s.frankenstein.enabled && s.frankenstein.beforeAmps;
    if (notesWanted != notesRouting && routeGain == 0.0)
    {
        notesRouting = notesWanted;
        if (notesRouting)
        {
            splitter.reset();
            splitterSnap = true;
            frankMix = 0.0;
        }
        else if (s.frankenstein.enabled)
        {
            frankenstein.reset();
            frankMix = 1.0; // continue directly in TONE mode (while silent)
            const auto lay = computeFrankensteinLayout(s.frankenstein, [&] {
                std::array<bool, kNumSlots> l {};
                for (int i = 0; i < kNumSlots; ++i) l[(size_t) i] = loaded[(size_t) i].load(std::memory_order_relaxed);
                return computeAudible(s, l);
            }());
            frankenstein.setTarget(lay);
            frankenstein.snapToTarget();
        }
        else
            frankMix = 0.0;
    }
    FrankensteinLayout notesLayout;
    if (notesRouting)
    {
        std::array<bool, kNumSlots> prevLoaded {};
        for (int i = 0; i < kNumSlots; ++i) prevLoaded[(size_t) i] = loaded[(size_t) i].load(std::memory_order_relaxed);
        notesLayout = computeFrankensteinLayout(s.frankenstein, computeAudible(s, prevLoaded));
        splitter.setTarget(notesLayout);
        if (splitterSnap) { splitter.snapToTarget(); splitterSnap = false; }
        std::array<double*, kFrankMaxSlots> ptrs {};
        for (int i = 0; i < kNumSlots; ++i) ptrs[(size_t) i] = splitIn.data() + (size_t) i * (size_t) stride;
        splitter.processSplit(in, ptrs, n);
    }

    // 1. Run every capture (keeps their internal state warm, also when muted).
    std::array<bool, kNumSlots> isLoaded {};
    for (int i = 0; i < kNumSlots; ++i)
    {
        double* buf = scratch.data() + (size_t) i * (size_t) stride;
        const double* slotIn = notesRouting ? splitIn.data() + (size_t) i * (size_t) stride : in;
        // input calibration (exactly 1.0 = untouched, bit-identical)
        const double trimTarget = std::clamp(s.slots[(size_t) i].inputTrim, 0.01, 100.0);
        double& tg = trimNow[(size_t) i];
        if (tg < 0.0) tg = trimTarget;
        if (tg != 1.0 || trimTarget != 1.0)
        {
            for (int k = 0; k < n; ++k)
            {
                tg += (trimTarget - tg) * trimCoef;
                trimBuf[(size_t) k] = slotIn[k] * tg;
            }
            if (std::abs(tg - trimTarget) < 1e-7 * trimTarget) tg = trimTarget;
            slotIn = trimBuf.data();
        }
        slots[(size_t) i].process(slotIn, buf, n, false); // level match is applied as levelGain
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
    std::array<std::array<double, 2>, kNumSlots> pan {};
    for (int i = 0; i < kNumSlots; ++i) pan[(size_t) i] = panGains(s.slots[(size_t) i].pan);
    const double G = computeCompensation(p, C, valid, pan);
    // Right channel is accumulated separately (or not at all for mono callers).
    double* R = outR != nullptr ? outR : scratchR.data();
    compensationDb.store((float) (20.0 * std::log10(G)), std::memory_order_relaxed);

    // 3. Align, EQ, gain, sum.
    const auto audibleNow = computeAudible(s, isLoaded);
    std::fill(out, out + n, 0.0);
    std::fill(R, R + n, 0.0);
    const double rotationMix = s.rotationActive ? 1.0 : 0.0;
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& ss = s.slots[(size_t) i];
        double* buf = scratch.data() + (size_t) i * (size_t) stride;
        effectivePercent[(size_t) i].store((float) (100.0 * p[(size_t) i]), std::memory_order_relaxed);

        // cabinet IR and its own EQ (both bypassed together; no CPU without an IR)
        auto& ir = irSlots[(size_t) i];
        ir.process(buf, n, ss.irEnabled);
        const bool irEqActive = ir.hasIr() && ss.irEnabled && ss.irEqEnabled;
        auto& ieq = irEqs[(size_t) i];
        ieq.setBands(irEqActive ? ss.irEq : ParametricEq::neutralised(ss.irEq));
        ieq.process(buf, n);

        auto& al = aligners[(size_t) i];
        al.setTargets(ss.delaySamples, ss.polarity, ss.phaseRadians, rotationMix);
        al.process(buf, n);

        auto& eq = eqs[(size_t) i];
        eq.setBands(ss.eqEnabled ? ss.eq : ParametricEq::neutralised(ss.eq)); // OFF also releases the cuts
        eq.process(buf, n);

        // NOTES mode: every amp plays its own note range in full (no fader blend / mix law)
        const double base = notesRouting ? (audibleNow[(size_t) i] ? ss.levelGain : 0.0) : p[(size_t) i] * G * ss.levelGain;
        const double target = base * pan[(size_t) i][0], targetR = base * pan[(size_t) i][1];
        double g = gains[(size_t) i], gr = gainsR[(size_t) i];
        if (g == 0.0 && target == 0.0 && gr == 0.0 && targetR == 0.0)
            continue;
        for (int k = 0; k < n; ++k)
        {
            g += (target - g) * gainCoef;
            gr += (targetR - gr) * gainCoef;
            out[k] += g * buf[k];
            R[k] += gr * buf[k];
        }
        if (std::abs(target - g) < 1e-9) g = target;
        if (std::abs(targetR - gr) < 1e-9) gr = targetR;
        gains[(size_t) i] = g;
        gainsR[(size_t) i] = gr;
    }

    // 4. "Create Frankenstein": frequency-split blending of the same aligned, EQ'd, level-matched paths.
    const bool frankOn = s.frankenstein.enabled && !notesRouting; // TONE mode: split after the amps
    if (frankOn || frankMix > 0.0)
    {
        const auto audible = computeAudible(s, isLoaded);
        const auto layout = computeFrankensteinLayout(s.frankenstein, audible);
        frankenstein.setTarget(layout);
        if (frankMix == 0.0)
        {
            frankenstein.reset();        // start from a clean state, no frequency sweep
            frankenstein.snapToTarget();
        }
        std::array<const double*, kFrankMaxSlots> amps {};
        std::array<double, kFrankMaxSlots> lg {}, lgR {};
        for (int i = 0; i < kNumSlots; ++i)
        {
            amps[(size_t) i] = isLoaded[(size_t) i] ? scratch.data() + (size_t) i * (size_t) stride : nullptr;
            lg[(size_t) i] = s.slots[(size_t) i].levelGain * pan[(size_t) i][0];
            lgR[(size_t) i] = s.slots[(size_t) i].levelGain * pan[(size_t) i][1];
        }
        frankenstein.process(amps, lg, lgR, frankOut.data(), frankOutR.data(), n);

        const double target = frankOn ? 1.0 : 0.0;
        for (int k = 0; k < n; ++k)
        {
            frankMix += (target - frankMix) * frankCoef;
            out[k] = (1.0 - frankMix) * out[k] + frankMix * frankOut[(size_t) k];
            R[k] = (1.0 - frankMix) * R[k] + frankMix * frankOutR[(size_t) k];
        }
        if (std::abs(target - frankMix) < 1e-6) frankMix = target;

        if (frankOn)
            for (int i = 0; i < kNumSlots; ++i) // percentages = share of the spectrum each amp plays
                effectivePercent[(size_t) i].store((float) (100.0 * layout.spectrumShare[(size_t) i]), std::memory_order_relaxed);
    }

    if (notesRouting)
        for (int i = 0; i < kNumSlots; ++i) // percentages = share of the note range each amp plays
            effectivePercent[(size_t) i].store((float) (100.0 * notesLayout.spectrumShare[(size_t) i]), std::memory_order_relaxed);

    // 4b. dip while the Frankenstein mode switches (amps receive different signals afterwards)
    {
        const double target = notesWanted == notesRouting ? 1.0 : 0.0;
        if (routeGain != 1.0 || target != 1.0)
            for (int k = 0; k < n; ++k)
            {
                routeGain = target > routeGain ? std::min(target, routeGain + routeStep) : std::max(target, routeGain - routeStep);
                const double g = routeGain * routeGain * (3.0 - 2.0 * routeGain);
                out[k] *= g;
                R[k] *= g;
            }
    }

    // 5. Global EQ on the complete blend (same EQ as the paths; OFF glides to flat, then costs nothing).
    const auto geq = s.globalEqEnabled ? s.globalEq : ParametricEq::neutralised(s.globalEq);
    globalEq.setBands(geq);
    globalEq.process(out, n);
    if (outR != nullptr)
    {
        globalEqR.setBands(geq);
        globalEqR.process(outR, n);
    }
}

} // namespace ampsurd

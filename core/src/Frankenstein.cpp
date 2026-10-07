#include "ampsurd/Frankenstein.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kLoHz = 20.0, kHiHz = 20000.0, kParkHz = 19000.0;
constexpr int kStep = 16;
}

FrankensteinLayout computeFrankensteinLayout(const FrankensteinSettings& s, const std::array<bool, kFrankMaxSlots>& audible)
{
    FrankensteinLayout L;
    const int K = std::clamp(s.sections, 2, 5);
    const double lo = std::log2(kLoHz), hi = std::log2(kHiHz);

    // section boundaries (log2 Hz), kept in order with a minimum gap
    std::array<double, 6> b {};
    b[0] = lo;
    b[(size_t) K] = hi;
    for (int k = 1; k < K; ++k)
    {
        const double v = std::log2(std::clamp((double) s.dividerHz[(size_t) k - 1], 25.0, 18000.0));
        b[(size_t) k] = std::max(v, b[(size_t) k - 1] + 1.0 / 12.0);
    }
    for (int k = K - 1; k >= 1; --k)
        b[(size_t) k] = std::min(b[(size_t) k], b[(size_t) k + 1] - 1.0 / 12.0);

    // remove sections whose amp is muted / not loaded; neighbours meet in the middle of the gap
    std::array<int, 5> kept {};
    int numKept = 0;
    for (int k = 0; k < K; ++k)
    {
        const int slot = s.amp[(size_t) k];
        if (slot >= 0 && slot < kFrankMaxSlots && audible[(size_t) slot])
            kept[(size_t) numKept++] = k;
    }

    struct Sec { int slot; double lo, hi; int src; };
    std::array<Sec, 5> secs {};
    std::array<bool, 5> merged {};
    int M = 0;
    for (int i = 0; i < numKept; ++i)
    {
        const int k = kept[(size_t) i];
        double sLo = i == 0 ? lo : 0.0, sHi = hi;
        if (i > 0)
        {
            const int pk = kept[(size_t) i - 1];
            sLo = (k == pk + 1) ? b[(size_t) k] : 0.5 * (b[(size_t) pk + 1] + b[(size_t) k]);
        }
        if (i + 1 < numKept)
        {
            const int nk = kept[(size_t) i + 1];
            sHi = (nk == k + 1) ? b[(size_t) nk] : 0.5 * (b[(size_t) k + 1] + b[(size_t) nk]);
        }
        const bool m = i > 0 && k != kept[(size_t) i - 1] + 1;
        if (M > 0 && secs[(size_t) M - 1].slot == s.amp[(size_t) k])
        {
            secs[(size_t) M - 1].hi = sHi; // same amp next to itself: no hand-over
            continue;
        }
        if (M > 0) merged[(size_t) M - 1] = m;
        secs[(size_t) M++] = { s.amp[(size_t) k], sLo, sHi, k };
    }

    L.numVisible = M;
    for (int i = 0; i < M; ++i)
    {
        const auto& sc = secs[(size_t) i];
        L.visibleSections[(size_t) i] = { sc.slot, std::exp2(sc.lo), std::exp2(sc.hi), sc.src };
        L.spectrumShare[(size_t) sc.slot] += (sc.hi - sc.lo) / (hi - lo);
    }
    for (int j = 1; j < M; ++j)
    {
        L.visibleDividersHz[(size_t) j - 1] = std::exp2(secs[(size_t) j].lo);
        L.dividerIsMerged[(size_t) j - 1] = merged[(size_t) j - 1];
    }

    // crossover points: two per divider, unused ones parked at the top
    for (int j = 0; j < FrankensteinLayout::kDividers; ++j)
    {
        if (j < M - 1)
        {
            const double D = secs[(size_t) j + 1].lo;
            const double left = D - secs[(size_t) j].lo, right = secs[(size_t) j + 1].hi - D;
            const double h = std::clamp((double) s.width, 0.0, 0.9) * 0.5 * std::min(left, right);
            L.crossoverHz[(size_t) (2 * j)] = std::exp2(D - 0.5 * h);
            L.crossoverHz[(size_t) (2 * j + 1)] = std::exp2(D + 0.5 * h);
        }
        else
        {
            L.crossoverHz[(size_t) (2 * j)] = L.crossoverHz[(size_t) (2 * j + 1)] = kParkHz;
        }
    }
    for (int c = 1; c < FrankensteinLayout::kCrossovers; ++c)
        L.crossoverHz[(size_t) c] = std::max(L.crossoverHz[(size_t) c], L.crossoverHz[(size_t) c - 1]);

    // band weights
    if (M == 0)
        return L; // everything muted: silence
    for (int band = 0; band < FrankensteinLayout::kBands; ++band)
    {
        const int j = band / 2; // divider this band belongs to
        if (band % 2 == 1 && j < M - 1)
        {
            // Overlap band between the two crossover points of divider j. Its share for the lower
            // amp, alpha(h) = 0.5 * (1 - 2^(-4h)) (h = half-width of the zone in octaves), makes both
            // amps exactly -6 dB at the divider for every WIDTH (fitted to the LR4 tree; at WIDTH 0
            // the band belongs to the upper amp and the tree reduces to one plain LR4 crossover).
            const double h = std::log2(L.crossoverHz[(size_t) (2 * j + 1)] / L.crossoverHz[(size_t) (2 * j)]);
            const double alpha = 0.5 * (1.0 - std::exp2(-4.0 * h));
            L.weight[(size_t) secs[(size_t) j].slot][(size_t) band] += alpha;
            L.weight[(size_t) secs[(size_t) j + 1].slot][(size_t) band] += 1.0 - alpha;
        }
        else
        {
            const int sec = std::min(j, M - 1);
            L.weight[(size_t) secs[(size_t) sec].slot][(size_t) band] += 1.0;
        }
    }
    return L;
}

// ---------------------------------------------------------------------------------------------
FrankensteinMixer::Coef FrankensteinMixer::coef(double fc, double sr) noexcept
{
    Coef c;
    const double g = std::tan(kPi * std::min(fc, 0.45 * sr) / sr);
    c.k = 1.41421356237309505; // Butterworth Q = 0.707 -> LR4 = two cascaded sections
    c.a1 = 1.0 / (1.0 + g * (g + c.k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

void FrankensteinMixer::prepare(double sampleRate, int)
{
    fs = sampleRate;
    smooth = 1.0 - std::exp(-(double) kStep / (0.025 * fs));
    for (int c = 0; c < C; ++c)
    {
        tLogF[(size_t) c] = logF[(size_t) c] = std::log(kParkHz);
        coefs[(size_t) c] = coef(kParkHz, fs);
    }
    for (auto& s : w) s.fill(0.0);
    for (auto& s : tW) s.fill(0.0);
    reset();
}

void FrankensteinMixer::reset() noexcept
{
    for (auto& slot : split)
        for (auto& cr : slot)
            for (auto& f : cr) f.reset();
    for (auto& a : allpass) a.reset();
    for (auto& a : allpassR) a.reset();
}

void FrankensteinMixer::setTarget(const FrankensteinLayout& L) noexcept
{
    for (int c = 0; c < C; ++c)
        tLogF[(size_t) c] = std::log(L.crossoverHz[(size_t) c]);
    tW = L.weight;
}

void FrankensteinMixer::snapToTarget() noexcept
{
    logF = tLogF;
    w = tW;
    for (int c = 0; c < C; ++c) coefs[(size_t) c] = coef(std::exp(logF[(size_t) c]), fs);
}

void FrankensteinMixer::process(const std::array<const double*, kFrankMaxSlots>& amps,
                                const std::array<double, kFrankMaxSlots>& gains, const std::array<double, kFrankMaxSlots>& gainsR,
                                double* out, double* outR, int n) noexcept
{
    auto tick = [](Svf& s, const Coef& c, double v0, double& v1, double& v2) {
        const double v3 = v0 - s.ic2;
        v1 = c.a1 * s.ic1 + c.a2 * v3;
        v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0 * v1 - s.ic1;
        s.ic2 = 2.0 * v2 - s.ic2;
    };

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min(kStep, n - start);

        // glide crossover frequencies and weights
        bool moved = false;
        for (int c = 0; c < C; ++c)
            if (logF[(size_t) c] != tLogF[(size_t) c])
            {
                logF[(size_t) c] += (tLogF[(size_t) c] - logF[(size_t) c]) * smooth;
                if (std::abs(tLogF[(size_t) c] - logF[(size_t) c]) < 1e-5) logF[(size_t) c] = tLogF[(size_t) c];
                coefs[(size_t) c] = coef(std::exp(logF[(size_t) c]), fs);
                moved = true;
            }
        (void) moved;
        for (int s = 0; s < kFrankMaxSlots; ++s)
            for (int b = 0; b < B; ++b)
            {
                double& v = w[(size_t) s][(size_t) b];
                const double t = tW[(size_t) s][(size_t) b];
                if (v != t)
                {
                    v += (t - v) * smooth;
                    if (std::abs(t - v) < 1e-6) v = t;
                }
            }

        for (int i = start; i < start + len; ++i)
        {
            // per-band weighted sums of all amps (left, right)
            std::array<double, B> bandSum {}, bandSumR {};
            for (int s = 0; s < kFrankMaxSlots; ++s)
            {
                if (amps[(size_t) s] == nullptr) continue;
                double rest = amps[(size_t) s][i];
                const double gl = gains[(size_t) s], gr = gainsR[(size_t) s];
                auto& st = split[(size_t) s];
                const auto& ws = w[(size_t) s];
                for (int c = 0; c < C; ++c)
                {
                    const Coef& k = coefs[(size_t) c];
                    double v1, v2;
                    tick(st[(size_t) c][0], k, rest, v1, v2);
                    const double lp1 = v2, hp1 = rest - k.k * v1 - v2;
                    double u1, u2;
                    tick(st[(size_t) c][1], k, lp1, u1, u2);
                    const double low = u2;
                    tick(st[(size_t) c][2], k, hp1, u1, u2);
                    rest = hp1 - k.k * u1 - u2;
                    bandSum[(size_t) c] += ws[(size_t) c] * low * gl;
                    bandSumR[(size_t) c] += ws[(size_t) c] * low * gr;
                }
                bandSum[(size_t) C] += ws[(size_t) C] * rest * gl;
                bandSumR[(size_t) C] += ws[(size_t) C] * rest * gr;
            }

            // Horner-style phase compensation: t = AP_c(t) + band_c, so every band passes the
            // all-passes of all crossovers above it and the bands sum to an all-pass.
            auto compensate = [&](std::array<Svf, C>& ap, const std::array<double, B>& bs) {
                double t = bs[0];
                for (int c = 1; c < C; ++c)
                {
                    double v1, v2;
                    tick(ap[(size_t) c], coefs[(size_t) c], t, v1, v2);
                    t = (t - 2.0 * coefs[(size_t) c].k * v1) + bs[(size_t) c];
                }
                return t + bs[(size_t) C];
            };
            out[i] = compensate(allpass, bandSum);
            if (outR != nullptr) outR[i] = compensate(allpassR, bandSumR);
        }
    }
}

} // namespace ampsurd

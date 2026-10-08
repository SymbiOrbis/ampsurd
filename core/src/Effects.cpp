#include "ampsurd/Effects.h"

#include <algorithm>
#include <cmath>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

int nextPow2(int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

double onePoleCoef(double hz, double sr) { return 1.0 - std::exp(-2.0 * kPi * std::min(hz, 0.45 * sr) / sr); }

// 4-point Hermite interpolation; x = fractional position in a power-of-two ring buffer
double hermite(const std::vector<double>& b, int mask, double x) noexcept
{
    const double fl = std::floor(x);
    const double t = x - fl;
    const int i = (int) fl;
    const double y0 = b[(size_t) ((i - 1) & mask)], y1 = b[(size_t) (i & mask)];
    const double y2 = b[(size_t) ((i + 1) & mask)], y3 = b[(size_t) ((i + 2) & mask)];
    if (t == 0.0) return y1;
    const double c1 = 0.5 * (y2 - y0);
    const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

// S-shaped (smoothstep) version of a linear 0..1 ramp: no corners at its start and end
double smoothstep(double w) noexcept { return w * w * (3.0 - 2.0 * w); }

double approach(double v, double target, double step) noexcept
{
    return target > v ? std::min(target, v + step) : std::max(target, v - step);
}
} // namespace

ReverbPreset reverbTypeDefaults(ReverbType t) noexcept
{
    switch (t)
    {
        case ReverbType::room:      return { 0.7f, 5.0f, 6000.0f };
        case ReverbType::hall:      return { 2.2f, 20.0f, 7000.0f };
        case ReverbType::plate:     return { 1.6f, 0.0f, 9000.0f };
        case ReverbType::cathedral: return { 5.5f, 45.0f, 5000.0f };
        case ReverbType::ambience:  return { 0.4f, 0.0f, 8000.0f };
    }
    return { 2.0f, 20.0f, 7000.0f };
}

const char* reverbTypeName(ReverbType t) noexcept
{
    switch (t)
    {
        case ReverbType::room:      return "ROOM";
        case ReverbType::hall:      return "HALL";
        case ReverbType::plate:     return "PLATE";
        case ReverbType::cathedral: return "CATHEDRAL";
        case ReverbType::ambience:  return "AMBIENCE";
    }
    return "";
}

// =============================================================================================
// Delay
// =============================================================================================
void Delay::prepare(double sampleRate, double maxSeconds)
{
    sr = sampleRate;
    const int size = nextPow2((int) std::ceil(maxSeconds * sr) + 8);
    bufL.assign((size_t) size, 0.0);
    bufR.assign((size_t) size, 0.0);
    mask = size - 1;
    xfadeLen = std::max(1, (int) std::lround(0.050 * sr)); // time changes crossfade over 50 ms
    rampStep = 1.0 / (0.010 * sr);
    smooth = 1.0 - std::exp(-1.0 / (0.020 * sr));
    hpCoef = std::exp(-2.0 * kPi * 80.0 / sr); // repeats lose the deep lows (no mud)
    reset();
}

void Delay::reset() noexcept
{
    std::fill(bufL.begin(), bufL.end(), 0.0);
    std::fill(bufR.begin(), bufR.end(), 0.0);
    writePos = 0;
    inGain = 0.0;
    lpL = lpR = hpL = hpR = hpInL = hpInR = 0.0;
    xfadePos = xfadeLen;
    idle = true;
    quiet = 0;
}

double Delay::read(const std::vector<double>& buf, double d) const noexcept
{
    return hermite(buf, mask, (double) writePos - std::max(2.0, d));
}

void Delay::process(const double* inL, const double* inR, double* outL, double* outR, int n, const DelaySettings& s) noexcept
{
    if (idle && !s.on) return;
    const double target = std::clamp(s.timeMs, 1.0, 2000.0) * sr / 1000.0;
    if (idle)
    {
        idle = false;
        quiet = 0;
        curDelay = newDelay = target;
        xfadePos = xfadeLen;
        levelS = s.level;
        fbS = s.feedback;
    }
    lpCoef = s.toneHz >= 19000.0f ? 1.0 : onePoleCoef(s.toneHz, sr);
    const double fbT = std::clamp((double) s.feedback, 0.0, 0.95), levT = std::clamp((double) s.level, 0.0, 1.0);
    const double inT = s.on ? 1.0 : 0.0;

    for (int i = 0; i < n; ++i)
    {
        // exact delay time; a change crossfades between the old and the new time (no pitch glide, no click)
        if (xfadePos >= xfadeLen && std::abs(target - curDelay) > 1e-6)
        {
            newDelay = target;
            xfadePos = 0;
        }
        double wL, wR;
        if (xfadePos < xfadeLen)
        {
            const double g = smoothstep((double) (xfadePos + 1) / xfadeLen);
            wL = (1.0 - g) * read(bufL, curDelay) + g * read(bufL, newDelay);
            wR = (1.0 - g) * read(bufR, curDelay) + g * read(bufR, newDelay);
            if (++xfadePos >= xfadeLen) curDelay = newDelay;
        }
        else
        {
            wL = read(bufL, curDelay);
            wR = read(bufR, curDelay);
        }
        // tone of the repeats: high cut + gentle low cut
        lpL += lpCoef * (wL - lpL);
        lpR += lpCoef * (wR - lpR);
        hpL = hpCoef * (hpL + lpL - hpInL); hpInL = lpL;
        hpR = hpCoef * (hpR + lpR - hpInR); hpInR = lpR;
        const double fL = hpL, fR = hpR;

        inGain = approach(inGain, inT, rampStep);
        fbS += (fbT - fbS) * smooth;
        levelS += (levT - levelS) * smooth;

        double writeL, writeR;
        if (s.pingPong)
        {
            writeL = 0.5 * (inL[i] + inR[i]) * smoothstep(inGain) + fbS * fR;
            writeR = fbS * fL;
        }
        else
        {
            writeL = inL[i] * smoothstep(inGain) + fbS * fL;
            writeR = inR[i] * smoothstep(inGain) + fbS * fR;
        }
        bufL[(size_t) writePos] = writeL;
        bufR[(size_t) writePos] = writeR;
        writePos = (writePos + 1) & mask;
        outL[i] += levelS * fL;
        outR[i] += levelS * fR;

        if (!s.on && inGain == 0.0 && std::abs(writeL) + std::abs(writeR) + std::abs(fL) + std::abs(fR) < 1e-8) ++quiet; // -160 dB
        else quiet = 0;
    }
    if (!s.on && quiet > mask + 1)
        reset(); // the whole line has gone silent: stop running
}

// =============================================================================================
// Reverb: 8-line feedback delay network (Householder matrix), input diffusion, early reflections,
// slow modulation of the line lengths (no metallic ringing), in-loop damping; decay = RT60.
// =============================================================================================
namespace
{
constexpr double kLineMs[Reverb::kLines] = { 29.7, 37.1, 41.1, 43.7, 53.3, 61.9, 67.1, 73.3 };
constexpr double kDiffMs[4] = { 4.77, 3.59, 12.73, 9.30 };
constexpr double kTapMs[8] = { 7.1, 11.3, 16.7, 19.9, 27.3, 31.1, 37.9, 43.1 };
constexpr double kMaxSize = 2.3;
constexpr double kModSamples48k = 12.0;

struct TypeShape { double size, diffusion, mod, er, late; };
TypeShape shape(ReverbType t)
{
    switch (t)
    {
        case ReverbType::room:      return { 0.55, 0.70, 0.30, 0.60, 0.80 };
        case ReverbType::hall:      return { 1.25, 0.72, 0.50, 0.35, 1.00 };
        case ReverbType::plate:     return { 0.85, 0.80, 0.40, 0.00, 1.00 };
        case ReverbType::cathedral: return { 2.20, 0.75, 0.60, 0.25, 1.00 };
        case ReverbType::ambience:  return { 0.30, 0.60, 0.20, 0.70, 0.50 };
    }
    return { 1.0, 0.7, 0.5, 0.3, 1.0 };
}
} // namespace

double Reverb::Allpass::process(double x, double g) noexcept
{
    const double d = buf[(size_t) pos];
    const double w = x + g * d;
    const double y = d - g * w;
    buf[(size_t) pos] = w;
    if (++pos >= len) pos = 0;
    return y;
}

void Reverb::prepare(double sampleRate)
{
    sr = sampleRate;
    const int preSize = nextPow2((int) std::ceil(0.30 * sr) + (int) std::ceil(0.05 * kMaxSize * sr) + 8);
    preL.assign((size_t) preSize, 0.0);
    preR.assign((size_t) preSize, 0.0);
    preMask = preSize - 1;
    for (int k = 0; k < 4; ++k)
    {
        const int maxLen = (int) std::ceil(kDiffMs[k] * kMaxSize * sr / 1000.0) + 2;
        diffL[(size_t) k].buf.assign((size_t) maxLen, 0.0);
        diffR[(size_t) k].buf.assign((size_t) maxLen, 0.0);
    }
    const double modMax = kModSamples48k * sr / 48000.0;
    for (int i = 0; i < kLines; ++i)
    {
        line[(size_t) i].assign((size_t) nextPow2((int) std::ceil(kLineMs[i] * kMaxSize * sr / 1000.0 + modMax) + 8), 0.0);
        lfoInc[(size_t) i] = (0.13 + 0.11 * i) / sr; // 0.13 .. 0.9 Hz, all different
        lfoPhase[(size_t) i] = 0.125 * i;
    }
    rampStep = 1.0 / (0.010 * sr);
    smooth = 1.0 - std::exp(-1.0 / (0.030 * sr));
    preXLen = std::max(1, (int) std::lround(0.050 * sr));
    preX = preXLen;
    configured = false;
    reset();
}

void Reverb::configure(ReverbType t) noexcept
{
    current = t;
    const auto sh = shape(t);
    diffusion = sh.diffusion;
    modDepth = sh.mod * kModSamples48k * sr / 48000.0;
    erLevel = sh.er;
    lateLevel = sh.late;
    for (int k = 0; k < 4; ++k)
    {
        const int l = std::max(1, (int) std::lround(kDiffMs[k] * sh.size * sr / 1000.0));
        diffL[(size_t) k].len = std::min(l, (int) diffL[(size_t) k].buf.size());
        diffR[(size_t) k].len = std::min(l + 7 + 3 * k, (int) diffR[(size_t) k].buf.size()); // decorrelated L/R
    }
    for (int i = 0; i < kLines; ++i)
        lineLen[(size_t) i] = (int) std::lround(kLineMs[i] * sh.size * sr / 1000.0);
    numTaps = sh.er > 0.0 ? 8 : 0;
    for (int k = 0; k < numTaps; ++k)
    {
        const double g = std::pow(0.82, k);
        // left and right reflections arrive at different times (wide, decorrelated)
        taps[(size_t) k] = { kTapMs[k] * sh.size * sr / 1000.0, (kTapMs[k] * 1.13 + 1.7) * sh.size * sr / 1000.0, g, g };
    }
    configured = true;
}

void Reverb::reset() noexcept
{
    std::fill(preL.begin(), preL.end(), 0.0);
    std::fill(preR.begin(), preR.end(), 0.0);
    prePos = 0;
    for (auto* d : { &diffL, &diffR })
        for (auto& a : *d) { std::fill(a.buf.begin(), a.buf.end(), 0.0); a.pos = 0; }
    for (int i = 0; i < kLines; ++i)
    {
        std::fill(line[(size_t) i].begin(), line[(size_t) i].end(), 0.0);
        linePos[(size_t) i] = 0;
        lp[(size_t) i] = 0.0;
    }
    inGain = 0.0;
    duck = 1.0;
    pendingType = -1;
    idle = true;
    quiet = 0;
}

void Reverb::process(const double* inL, const double* inR, double* outL, double* outR, int n, const ReverbSettings& s) noexcept
{
    if (idle && !s.on) return;
    if (idle)
    {
        idle = false;
        quiet = 0;
        if (!configured || s.type != current) configure(s.type);
        levelS = s.level;
    }
    // a new type: duck the tail (15 ms), switch, continue - no click
    if (s.type != current) pendingType = (int) s.type;

    const double rt = std::clamp((double) s.decaySeconds, 0.2, 12.0);
    std::array<double, kLines> g {};
    for (int i = 0; i < kLines; ++i) g[(size_t) i] = std::pow(10.0, -3.0 * lineLen[(size_t) i] / (rt * sr));
    const double damp = onePoleCoef(std::clamp((double) s.toneHz, 500.0, 20000.0), sr);
    const double preT = std::clamp((double) s.preDelayMs, 0.0, 250.0) * sr / 1000.0;
    const double inT = s.on ? 1.0 : 0.0, levT = std::clamp((double) s.level, 0.0, 1.0);
    const double duckStep = 1.0 / (0.015 * sr);

    const double norm = 1.0 / std::sqrt((double) kLines);

    for (int i = 0; i < n; ++i)
    {
        inGain = approach(inGain, inT, rampStep);
        levelS += (levT - levelS) * smooth;
        // pre-delay: a new value crossfades (S-shaped, 50 ms) from the old to the new time
        if (preX >= preXLen && std::abs(preT - preCur) > 1e-6) { preNew = preT; preX = 0; }
        const double preG = preX < preXLen ? smoothstep((double) (preX + 1) / preXLen) : 1.0;
        if (pendingType >= 0)
        {
            duck = approach(duck, 0.0, duckStep);
            if (duck == 0.0)
            {
                const auto t = (ReverbType) pendingType;
                const double keepPre = preCur, keepNew = preNew;
                const int keepX = preX;
                reset();      // the new space starts empty ...
                idle = false;
                inGain = 0.0; // ... and the guitar fades into it (no abrupt onset)
                preCur = keepPre; preNew = keepNew; preX = keepX;
                configure(t);
                duck = 0.0;
                for (int k = 0; k < kLines; ++k) g[(size_t) k] = std::pow(10.0, -3.0 * lineLen[(size_t) k] / (rt * sr));
            }
        }
        else
            duck = approach(duck, 1.0, duckStep);

        // pre-delay (input written once, read at the pre-delay and the early-reflection taps)
        preL[(size_t) prePos] = inL[i] * smoothstep(inGain);
        preR[(size_t) prePos] = inR[i] * smoothstep(inGain);
        auto tapRead = [&](const std::vector<double>& b, double d) { return hermite(b, preMask, prePos - (d < 2.0 ? std::round(d) : d)); };
        auto earlyAndPre = [&](double preD, double& pl, double& pr, double& el, double& er) {
            pl = tapRead(preL, preD);
            pr = tapRead(preR, preD);
            el = er = 0.0;
            for (int k = 0; k < numTaps; ++k)
            {
                const auto& t = taps[(size_t) k];
                el += t.gL * tapRead(preL, preD + t.delayL);
                er += t.gR * tapRead(preR, preD + t.delayR);
            }
        };
        double pL, pR, erL, erR;
        earlyAndPre(preCur, pL, pR, erL, erR);
        if (preX < preXLen)
        {
            double qL, qR, fL, fR;
            earlyAndPre(preNew, qL, qR, fL, fR);
            pL += preG * (qL - pL); pR += preG * (qR - pR);
            erL += preG * (fL - erL); erR += preG * (fR - erR);
            if (++preX >= preXLen) preCur = preNew;
        }
        prePos = (prePos + 1) & preMask;

        // input diffusion
        double dL = pL, dR = pR;
        for (int k = 0; k < 4; ++k)
        {
            dL = diffL[(size_t) k].process(dL, diffusion);
            dR = diffR[(size_t) k].process(dR, diffusion);
        }

        // read the (slowly modulated) lines, damp
        std::array<double, kLines> o {};
        double sum = 0.0;
        for (int k = 0; k < kLines; ++k)
        {
            auto& ph = lfoPhase[(size_t) k];
            ph += lfoInc[(size_t) k];
            if (ph >= 1.0) ph -= 1.0;
            const double d = lineLen[(size_t) k] - modDepth * 0.5 * (1.0 + std::sin(2.0 * kPi * ph));
            const auto& b = line[(size_t) k];
            const int m = (int) b.size() - 1;
            const double x = (double) linePos[(size_t) k] - std::max(2.0, d);
            const double fl = std::floor(x);
            const double t = x - fl;
            const double a = b[(size_t) ((int) fl & m)], c = b[(size_t) (((int) fl + 1) & m)];
            double v = a + t * (c - a);
            lp[(size_t) k] += damp * (v - lp[(size_t) k]);
            o[(size_t) k] = lp[(size_t) k];
            sum += o[(size_t) k];
        }
        // Householder feedback matrix, decay gains, inject the diffused input
        const double hh = 2.0 / kLines * sum;
        for (int k = 0; k < kLines; ++k)
        {
            auto& b = line[(size_t) k];
            // left and right are injected with orthogonal sign patterns (decorrelated outputs)
            constexpr double sa[kLines] = { 1, 1, 1, 1, 1, 1, 1, 1 }, sb[kLines] = { 1, -1, 1, -1, -1, 1, -1, 1 };
            const double inj = 0.35 * (dL * sa[k] * ((k & 2) ? -1.0 : 1.0) + dR * sb[k]);
            b[(size_t) linePos[(size_t) k]] = g[(size_t) k] * (o[(size_t) k] - hh) + inj;
            linePos[(size_t) k] = (linePos[(size_t) k] + 1) & ((int) b.size() - 1);
        }
        const double lateL = norm * (o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7]);
        const double lateR = norm * (o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7]);
        const double wL = erLevel * erL + lateLevel * lateL, wR = erLevel * erR + lateLevel * lateR;
        const double gOut = levelS * smoothstep(duck);
        outL[i] += gOut * wL;
        outR[i] += gOut * wR;

        if (!s.on && inGain == 0.0 && std::abs(wL) + std::abs(wR) + std::abs(sum) < 1e-10) ++quiet;
        else quiet = 0;
    }
    if (!s.on && quiet > (int64_t) (0.5 * sr))
    {
        reset(); // tail has died away: stop running
        configured = true;
    }
}

// =============================================================================================
// Flanger: short modulated delay with feedback, stereo (LFOs 90 degrees apart)
// =============================================================================================
void Flanger::prepare(double sampleRate)
{
    sr = sampleRate;
    const int size = nextPow2((int) std::ceil(0.02 * sr));
    bufL.assign((size_t) size, 0.0);
    bufR.assign((size_t) size, 0.0);
    mask = size - 1;
    rampStep = 1.0 / (0.020 * sr);
    smooth = 1.0 - std::exp(-1.0 / (0.030 * sr));
    reset();
}

void Flanger::reset() noexcept
{
    std::fill(bufL.begin(), bufL.end(), 0.0);
    std::fill(bufR.begin(), bufR.end(), 0.0);
    writePos = 0;
    phase = 0.0;
    wet = 0.0;
    lastL = lastR = 0.0;
}

void Flanger::process(double* L, double* R, int n, const FlangerSettings& s) noexcept
{
    if (wet == 0.0 && !s.on)
    {
        // off: only keep the short delay line filled with the input, so that switching on
        // starts smoothly (an empty line would make the delayed signal jump in)
        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) writePos] = L[i];
            bufR[(size_t) writePos] = R[i];
            writePos = (writePos + 1) & mask;
        }
        return;
    }
    if (wet == 0.0)
    {
        rateS = s.rateHz; depthS = s.depth; fbS = s.feedback; mixS = s.mix;
    }
    const double target = s.on ? 1.0 : 0.0;
    for (int i = 0; i < n; ++i)
    {
        rateS += (std::clamp((double) s.rateHz, 0.05, 5.0) - rateS) * smooth;
        depthS += (std::clamp((double) s.depth, 0.0, 1.0) - depthS) * smooth;
        fbS += (std::clamp((double) s.feedback, -0.9, 0.9) - fbS) * smooth;
        mixS += (std::clamp((double) s.mix, 0.0, 1.0) - mixS) * smooth;
        phase += rateS / sr;
        if (phase >= 1.0) phase -= 1.0;
        const double lfoL = 0.5 + 0.5 * std::sin(2.0 * kPi * phase);
        const double lfoR = 0.5 + 0.5 * std::sin(2.0 * kPi * (phase + 0.25));
        const double dL = (0.25 + depthS * 5.0 * lfoL) * sr / 1000.0;
        const double dR = (0.25 + depthS * 5.0 * lfoR) * sr / 1000.0;

        const double xL = L[i], xR = R[i];
        const double yL = hermite(bufL, mask, (double) writePos - std::max(2.0, dL));
        const double yR = hermite(bufR, mask, (double) writePos - std::max(2.0, dR));
        wet = approach(wet, target, rampStep);
        const double fb = fbS * smoothstep(wet); // feedback fades in and out with the effect
        bufL[(size_t) writePos] = xL + fb * yL;
        bufR[(size_t) writePos] = xR + fb * yR;
        writePos = (writePos + 1) & mask;

        const double w = smoothstep(wet);
        L[i] = xL + w * ((1.0 - mixS) * xL + mixS * yL - xL);
        R[i] = xR + w * ((1.0 - mixS) * xR + mixS * yR - xR);
    }
}

// =============================================================================================
void FxChain::prepare(double sampleRate, int maxBlockSize)
{
    flanger.prepare(sampleRate);
    for (auto& d : delays) d.prepare(sampleRate);
    reverb.prepare(sampleRate);
    inL.assign((size_t) std::max(1, maxBlockSize), 0.0);
    inR.assign(inL.size(), 0.0);
}

void FxChain::reset() noexcept
{
    flanger.reset();
    for (auto& d : delays) d.reset();
    reverb.reset();
}

bool FxChain::anyActive() const noexcept
{
    return flanger.isActive() || delays[0].isActive() || delays[1].isActive() || reverb.isActive();
}

void FxChain::process(double* L, double* R, int n, const FxSettings& s) noexcept
{
    if (n > (int) inL.size()) return;
    flanger.process(L, R, n, s.flanger);

    const bool d0 = s.delay[0].on || delays[0].isActive(), d1 = s.delay[1].on || delays[1].isActive();
    if (d0 || d1)
    {
        std::copy(L, L + n, inL.begin());
        std::copy(R, R + n, inR.begin());
        if (d0) delays[0].process(inL.data(), inR.data(), L, R, n, s.delay[0]);
        if (d1) delays[1].process(inL.data(), inR.data(), L, R, n, s.delay[1]);
    }
    if (s.reverb.on || reverb.isActive())
    {
        std::copy(L, L + n, inL.begin());
        std::copy(R, R + n, inR.begin());
        reverb.process(inL.data(), inR.data(), L, R, n, s.reverb);
    }
}

} // namespace ampsurd

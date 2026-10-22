#include "ampsurd/Doubler.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ampsurd
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kCentsPerLn = 1731.2340490667560; // 1200 / ln 2
constexpr int kTaps = 16, kPhases = 256, kHalf = 8;
constexpr int kTick = 32;                           // control rate: every 32 samples
constexpr double kMinDelayMs = 8.0;

double db2a(double db) { return std::pow(10.0, db / 20.0); }

double besselI0(double x)
{
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        t *= (x / (2.0 * k)) * (x / (2.0 * k));
        s += t;
    }
    return s;
}

// constant-power pan, centre = {1, 1}, hard = {sqrt 2, 0} (as the amp PAN)
void panGains(double p, double& gl, double& gr)
{
    const double a = (std::clamp(p, -1.0, 1.0) + 1.0) * 0.25 * kPi;
    gl = std::sqrt(2.0) * std::cos(a);
    gr = std::sqrt(2.0) * std::sin(a);
}

// one Butterworth SVF stage with shared coefficients; returns low / high
inline double svfLow(double x, double& ic1, double& ic2, double a1, double a2, double a3)
{
    const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    return v2;
}
inline double svfHigh(double x, double& ic1, double& ic2, double a1, double a2, double a3)
{
    const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    return x - std::sqrt(2.0) * v1 - v2;
}
} // namespace

double Doubler::rnd() noexcept
{
    // xorshift64*
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return (double) ((rng * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
}

double Doubler::gauss() noexcept
{
    const double u = std::max(1e-12, rnd()), v = rnd();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * v);
}

void Doubler::prepare(double sampleRate, int, uint32_t seed)
{
    sr = sampleRate;
    size_t size = 1;
    while ((double) size < 0.09 * sr + 64.0) size <<= 1;
    line.assign(size, 0.0);
    mask = size - 1;
    seedSetting = seed;

    // 16-tap Kaiser-windowed sinc (beta 7.5), one row per fractional position (0 .. 1)
    const double i0b = besselI0(7.5);
    for (int p = 0; p <= kPhases; ++p)
    {
        const double f = (double) p / kPhases;
        double sum = 0.0;
        for (int k = 0; k < kTaps; ++k)
        {
            const double t = (double) (k - (kHalf - 1)) - f; // tap k reads x[i - 7 + k]
            const double wx = t / kHalf;
            const double w = std::abs(wx) < 1.0 ? besselI0(7.5 * std::sqrt(1.0 - wx * wx)) / i0b : 0.0;
            const double s = std::abs(t) < 1e-9 ? 1.0 : std::sin(kPi * t) / (kPi * t);
            sinc[(size_t) p][(size_t) k] = s * w;
            sum += s * w;
        }
        for (auto& v : sinc[(size_t) p]) v /= sum;
    }
    mixStep = 1.0 / (0.030 * sr);
    mix = 0.0;
    wantOn = false;
    reset();
}

void Doubler::reset() noexcept
{
    std::fill(line.begin(), line.end(), 0.0);
    wpos = 0;
    rng = seedSetting != 0 ? 0x9E3779B97F4A7C15ULL ^ ((uint64_t) seedSetting * 0xD1B54A32D192ED03ULL)
                           : (uint64_t) std::chrono::high_resolution_clock::now().time_since_epoch().count() | 1ULL;
    for (int i = 0; i < 8; ++i) rnd();
    envFast = envSlow = hpX = hpY = peak = activity = level = 0.0;
    armed = true;
    refractory = 0;
    silent = 0;
    dryLpL = dryLpR = dryHpL = dryHpR = {};
    xHz = -1.0;
    lastVoices = -1;
    voiceOn = {};
    for (auto& v : voices) v = Voice {};
    for (auto& d : delayMsNow) d.store(0.0f, std::memory_order_relaxed);
}

void Doubler::crossoverCoefs(double hz) noexcept
{
    xHz = hz;
    const double g = std::tan(kPi * std::min(std::max(hz, 20.0), 0.45 * sr) / sr);
    xa1 = 1.0 / (1.0 + g * (g + std::sqrt(2.0)));
    xa2 = g * xa1;
    xa3 = g * xa2;
    xg = g;
}

// A new "take": timing centre, tone (shelf, pick-position comb). Called at start and in silences only.
void Doubler::drawTake(Voice& v, int index, const DoublerSettings& s, bool first) noexcept
{
    const double t = std::clamp(s.tight, 0.0, 1.0), ch = std::clamp(s.character, 0.0, 1.0);
    const double lo = (kMinDelayMs + 10.0 * (1.0 - t)) * 0.001 * sr; // tight 8-13 ms, loose 18-33 ms
    const double hi = (13.0 + 20.0 * (1.0 - t)) * 0.001 * sr;
    double c = lo + (hi - lo) * rnd();
    // keep the takes at least 2 ms apart from each other (otherwise two doubles comb with each other)
    for (int tries = 0; tries < 12; ++tries)
    {
        bool ok = true;
        for (int j = 0; j < index; ++j)
        {
            const double other = voices[(size_t) j].xfade < 1.0 ? voices[(size_t) j].centreNew : voices[(size_t) j].centre;
            ok = ok && std::abs(other - c) > 0.002 * sr;
        }
        if (ok) break;
        c = lo + (hi - lo) * rnd();
    }
    if (first)
    {
        v.centre = v.centreNew = c;
        v.xfade = 1.0;
    }
    else
    {
        v.centreNew = c;
        v.xfade = 0.0;
    }
    v.shelfDb = std::clamp(gauss(), -2.0, 2.0) * 1.25 * ch;        // +-2.5 dB at CHARACTER 100 %
    v.shelfGain = db2a(v.shelfDb) - 1.0;
    v.combT = (0.5 + 1.5 * rnd()) * 0.001 * sr;                     // 0.5 .. 2 ms
    v.combG = (0.04 + 0.16 * rnd()) * ch;
    // decorrelation: four all-passes at random frequencies (700 Hz - 7 kHz). A take is then no longer an
    // exact copy of the guitar, so its sum with the guitar has no regular comb (= no flanger sound) while
    // the delay moves. All-passes keep the tone; drawn per take (only in silences).
    for (auto& ap : v.ap)
    {
        const double fc = 700.0 * std::pow(10.0, rnd()), q = 0.7 + 1.3 * rnd();
        const double w = 2.0 * kPi * std::min(fc, 0.45 * sr) / sr, al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        ap.a1 = -2.0 * std::cos(w) / a0;
        ap.a2 = (1.0 - al) / a0;
    }
    v.combNorm = 1.0 / std::sqrt(1.0 + v.combG * v.combG);
}

double Doubler::readCubic(double d) const noexcept
{
    const double pos = (double) wpos - d;
    const double fl = std::floor(pos);
    const double f = pos - fl;
    const auto i = (int64_t) fl;
    const double xm = line[(size_t) (i - 1) & mask], x0 = line[(size_t) i & mask], x1 = line[(size_t) (i + 1) & mask],
                 x2 = line[(size_t) (i + 2) & mask];
    const double c1 = 0.5 * (x1 - xm), c2 = xm - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm) + 1.5 * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

double Doubler::read(double d) const noexcept
{
    const double pos = (double) wpos - d;
    const double fl = std::floor(pos);
    const double f = pos - fl;
    const auto i = (int64_t) fl;
    const double pf = f * kPhases;
    const int p0 = std::min(kPhases - 1, (int) pf);
    const double a = pf - p0;
    const auto& r0 = sinc[(size_t) p0];
    const auto& r1 = sinc[(size_t) p0 + 1];
    double acc = 0.0;
    for (int k = 0; k < kTaps; ++k)
    {
        const double h = r0[(size_t) k] + a * (r1[(size_t) k] - r0[(size_t) k]);
        acc += h * line[(size_t) (i - (kHalf - 1) + k) & mask];
    }
    return acc;
}

void Doubler::process(double* L, double* R, int n, const DoublerSettings& s) noexcept
{
    if (line.empty()) return;
    if (s.on && !wantOn && mix == 0.0) reset(); // fresh start: empty history, new takes
    wantOn = s.on;
    if (!wantOn && mix == 0.0) return; // off: untouched

    const int nv = std::clamp(s.voices, 1, kMaxVoices);
    const double t = std::clamp(s.tight, 0.0, 1.0), ch = std::clamp(s.character, 0.0, 1.0);
    if (lastVoices < 0)
    {
        // fresh start: takes for all voices (a later voice count / TIGHT change keeps them: new centres are
        // drawn in the next silence, never while sound is being read)
        for (int i = 0; i < kMaxVoices; ++i) drawTake(voices[(size_t) i], i, s, true);
    }
    const double wanderMs = 0.3 + 1.7 * (1.0 - t); // how far a take wanders around its centre (small: moving combs = flanger)
    const double wander = wanderMs * 0.001 * sr;
    const double noteSd = 1.0 + 4.0 * (1.0 - t);   // per-note intonation (cents, sd)
    const double attackSd = 1.5 * ch;              // per-note pick strength (dB, sd)

    // smoothing constants
    const double kPar = 1.0 - std::exp(-1.0 / (0.030 * sr));
    const double kWander = 1.0 - std::exp(-2.0 * kPi * 2.0 / sr);       // 2 Hz smoothing of the wander
    const double kGain = 1.0 - std::exp(-1.0 / (0.004 * sr));
    const double noteDecay = std::exp(-1.0 / (0.30 * sr));
    const double noteLeak = std::exp(-1.0 / (3.0 * sr));
    const double actDecay = std::exp(-1.0 / (0.6 * sr));
    const double kShelf = 1.0 - std::exp(-2.0 * kPi * 2500.0 / sr);
    const double hpA = 1.0 / (1.0 + 2.0 * kPi * 300.0 / sr);
    const double fastA = 1.0 - std::exp(-1.0 / (0.0007 * sr)), fastR = 1.0 - std::exp(-1.0 / (0.012 * sr));
    const double slowK = 1.0 - std::exp(-1.0 / (0.045 * sr));
    const double levK = 1.0 - std::exp(-1.0 / (0.005 * sr));
    const double peakDecay = std::exp(-1.0 / (2.0 * sr));
    const double xfStep = 1.0 / (0.005 * sr);
    const double tauOu = 2.5;
    const int silenceNeeded = (int) ((33.0 + 2.0 + 2.0 + 15.0) * 0.001 * sr); // longer than any read point

    const bool lowMono = s.lowMonoHz >= 20.0;
    const double xTarget = lowMono ? s.lowMonoHz : (xHz > 0 ? xHz : 120.0);
    if (xHz < 0.0) crossoverCoefs(xTarget);
    else if (std::abs(xTarget - xHz) > 0.01) crossoverCoefs(xHz * std::pow(xTarget / xHz, std::min(1.0, (double) n / (0.05 * sr))));

    const double levelT = db2a(std::clamp(s.levelDb, -24.0, 12.0));
    const double widthT = std::clamp(s.width, 0.0, 1.0);
    std::array<double, kMaxVoices> onT {};
    for (int i = 0; i < kMaxVoices; ++i) onT[(size_t) i] = i < nv ? 1.0 : 0.0;
    const double dryPanT = nv == 1 ? -widthT : (nv == 3 ? -0.25 * widthT : 0.0);
    if (lastVoices < 0)
    {
        // fresh start (the on / off fade covers it): parameters at their targets
        widthNow = widthT;
        levelNow = levelT;
        voiceOn = onT;
        lmWeight = lowMono ? 1.0 : 0.0;
        dryPan = dryPanT;
        panL.fill(1.0);
        panR.fill(1.0);
        panGains(dryPan, dryL, dryR);
    }
    lastVoices = nv;

    for (int k = 0; k < n; ++k)
    {
        // parameters (30 ms)
        widthNow += (widthT - widthNow) * kPar;
        levelNow += (levelT - levelNow) * kPar;
        double nOn = 0.0;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            voiceOn[(size_t) i] += (onT[(size_t) i] - voiceOn[(size_t) i]) * kPar;
            if (std::abs(voiceOn[(size_t) i] - onT[(size_t) i]) < 1e-6) voiceOn[(size_t) i] = onT[(size_t) i];
            nOn += voiceOn[(size_t) i];
        }
        lmWeight += ((lowMono ? 1.0 : 0.0) - lmWeight) * kPar;
        if (std::abs(lmWeight - (lowMono ? 1.0 : 0.0)) < 1e-6) lmWeight = lowMono ? 1.0 : 0.0;
        const double kT = 1.0 / std::sqrt(1.0 + nOn * levelNow * levelNow); // loudness kept (takes add as uncorrelated)
        kNow = kT;
        const double target = wantOn ? 1.0 : 0.0;
        mix = target > mix ? std::min(target, mix + mixStep) : std::max(target, mix - mixStep);

        // write the mid signal
        const double mid = 0.5 * (L[k] + R[k]);
        line[wpos & mask] = mid;

        // detection: onsets (rise of the high-passed envelope), activity, silence
        hpY = hpA * (hpY + mid - hpX);
        hpX = mid;
        const double a = std::abs(hpY);
        envFast += (a - envFast) * (a > envFast ? fastA : fastR);
        envSlow += (a - envSlow) * slowK;
        level += (std::abs(mid) - level) * levK;
        peak = std::max(level, peak * peakDecay);
        activity *= actDecay;
        if (refractory > 0) --refractory;
        if (armed && refractory == 0 && envFast > 2.2 * envSlow + 1e-5 && envFast > 0.003 * peak)
        {
            armed = false;
            refractory = (int) (0.05 * sr);
            activity = 1.0;
            onsets.fetch_add(1, std::memory_order_relaxed);
            for (int i = 0; i < kMaxVoices; ++i)
            {
                auto& v = voices[(size_t) i];
                v.pending = std::max(0, (int) (v.delayNow - 0.001 * sr)); // when this take plays the attack
                v.pendCents = std::clamp(gauss(), -2.0, 2.0) * noteSd * (i == 0 ? 1.0 : 0.8);
                v.pendGainDb = std::clamp(gauss(), -2.0, 2.0) * attackSd;
            }
        }
        else if (!armed && envFast < 1.3 * envSlow)
            armed = true;
        const bool quiet = level < 0.01 * peak || level < 1e-5;
        silent = quiet ? silent + 1 : 0;
        if (silent == silenceNeeded)
        {
            // everything any take can read is silent: new timing and tone for every take
            for (int i = 0; i < kMaxVoices; ++i) drawTake(voices[(size_t) i], i, s, false);
            redraws.fetch_add(1, std::memory_order_relaxed);
        }

        // control tick: the slow random wander (faster while notes are being played)
        if ((wpos & (kTick - 1)) == 0)
        {
            const double speed = 0.1 + 0.9 * activity; // nearly still in sustained notes
            const double dt = kTick / sr * speed;
            for (auto& v : voices)
            {
                v.ou += -v.ou * dt / tauOu + 0.75 * std::sqrt(2.0 * dt / tauOu) * gauss();
                v.target = std::tanh(v.ou);
            }
            // pans follow WIDTH at the control rate
            const double w = widthNow;
            for (int i = 0; i < kMaxVoices; ++i)
            {
                const double p = nv == 1 ? (i == 0 ? w : (i == 1 ? -w : 0.4 * w)) : (i == 0 ? -w : (i == 1 ? w : 0.4 * w));
                panGains(p, panL[(size_t) i], panR[(size_t) i]);
            }
            panGains(dryPan, dryL, dryR);
        }

        // takes
        double outL = 0.0, outR = 0.0;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            v.x1 += (v.target - v.x1) * kWander;
            v.x2 += (v.x1 - v.x2) * kWander;
            if (v.pending >= 0 && v.pending-- == 0)
            {
                v.noteVel = v.pendCents;
                v.gainTarget = db2a(v.pendGainDb);
            }
            v.noteVel *= noteDecay;
            v.note = v.note * noteLeak - v.noteVel / kCentsPerLn; // sharper = shorter delay
            v.gainNow += (v.gainTarget - v.gainNow) * kGain;

            const double off = wander * v.x2 + v.note;
            const double minD = kMinDelayMs * 0.001 * sr;
            double y;
            double d = std::max(minD, v.centre + off);
            if (v.xfade < 1.0)
            {
                const double d2 = std::max(minD, v.centreNew + off);
                const double c = 0.5 - 0.5 * std::cos(kPi * v.xfade);
                y = (1.0 - c) * read(d) + c * read(d2);
                const double yT = (1.0 - c) * readCubic(d + v.combT) + c * readCubic(d2 + v.combT);
                y = (y + v.combG * yT) * v.combNorm;
                v.xfade = std::min(1.0, v.xfade + xfStep);
                if (v.xfade >= 1.0) v.centre = v.centreNew;
                d = (1.0 - c) * d + c * d2;
            }
            else
            {
                y = (read(d) + v.combG * readCubic(d + v.combT)) * v.combNorm;
            }
            v.delayNow = d;
            if (voiceOn[(size_t) i] == 0.0 && onT[(size_t) i] == 0.0)
            {
                v.hp = {};
                continue;
            }
            for (auto& ap : v.ap) // all-pass: y = a2 x + a1 x1 + x2 - a1 y1 - a2 y2 (transposed DF II)
            {
                const double o = ap.a2 * y + ap.z1;
                ap.z1 = ap.a1 * y - ap.a1 * o + ap.z2;
                ap.z2 = y - ap.a2 * o;
                y = o;
            }
            // tone: high shelf (+-dB above ~2.5 kHz)
            v.hsLp += (y - v.hsLp) * kShelf;
            y += v.shelfGain * (y - v.hsLp);
            // LOW MONO: the takes have no lows (they stay with the dry guitar)
            const double h = svfHigh(svfHigh(y, v.hp[0].ic1, v.hp[0].ic2, xa1, xa2, xa3), v.hp[1].ic1, v.hp[1].ic2, xa1, xa2, xa3);
            y = lmWeight * h + (1.0 - lmWeight) * y;
            y *= v.gainNow * levelNow * voiceOn[(size_t) i];
            // pan: 1 double = right (dry left); 2 = left / right; 3rd = right of centre
            outL += panL[(size_t) i] * y;
            outR += panR[(size_t) i] * y;
        }

        // dry: lows untouched (LOW MONO), the rest scaled (loudness) and, with one double, panned left
        dryPan += (dryPanT - dryPan) * kPar;
        const double dl = dryL, dr = dryR;
        const double xl = L[k], xr = R[k];
        const double lowL = svfLow(svfLow(xl, dryLpL[0].ic1, dryLpL[0].ic2, xa1, xa2, xa3), dryLpL[1].ic1, dryLpL[1].ic2, xa1, xa2, xa3);
        const double lowR = svfLow(svfLow(xr, dryLpR[0].ic1, dryLpR[0].ic2, xa1, xa2, xa3), dryLpR[1].ic1, dryLpR[1].ic2, xa1, xa2, xa3);
        const double hiL = svfHigh(svfHigh(xl, dryHpL[0].ic1, dryHpL[0].ic2, xa1, xa2, xa3), dryHpL[1].ic1, dryHpL[1].ic2, xa1, xa2, xa3);
        const double hiR = svfHigh(svfHigh(xr, dryHpR[0].ic1, dryHpR[0].ic2, xa1, xa2, xa3), dryHpR[1].ic1, dryHpR[1].ic2, xa1, xa2, xa3);
        const double split = lmWeight;
        const double wetL = split * (lowL + kNow * dl * hiL) + (1.0 - split) * kNow * dl * xl + kNow * outL;
        const double wetR = split * (lowR + kNow * dr * hiR) + (1.0 - split) * kNow * dr * xr + kNow * outR;
        L[k] = (1.0 - mix) * xl + mix * wetL;
        R[k] = (1.0 - mix) * xr + mix * wetR;
        ++wpos;
    }
    for (int i = 0; i < kMaxVoices; ++i)
        delayMsNow[(size_t) i].store(i < nv ? (float) (voices[(size_t) i].delayNow / sr * 1000.0) : 0.0f, std::memory_order_relaxed);
    if (!wantOn && mix == 0.0) reset();
}

} // namespace ampsurd

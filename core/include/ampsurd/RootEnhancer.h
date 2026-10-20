#pragma once

// ROOT (EXPERIMENTAL): makes the lowest note of a distorted, low-tuned power chord sound deeper and
// more dominant - without a pitch shifter, octave tracking or FFT. It adds a mono LOW LAYER under the
// amps (like producers who layer a clean, compressed bass under a distorted bass):
//
//   source ─ LP 24 dB/oct @ FREQ ─┬─────────────── (1 - GROWL) ─┐
//                                 └─ asymmetric saturation ─ GROWL ┴─ TIGHT (HP) ─ BASS EQ ─ COMP ─ LEVEL ─ (+) L/R
//   SUB mode adds an octave-down tone from a zero-crossing divider (experimental, see below)
//
// Why it works (docs/ROOT_MODULE.md has the sources):
//   - A distorted root + fifth (f and 1.5 f) produces a difference tone at 0.5 f, one octave below the
//     root, and fills in the harmonic series of 0.5 f. Mostly EVEN-order (asymmetric) nonlinearities make
//     it; symmetric clipping (tanh) makes odd harmonics and only a weak 0.5 f. GROWL therefore uses an
//     asymmetric curve. This only happens for fifths (power chords), not for thirds / fourths.
//   - Saturation is applied only to the band below FREQ (60-250 Hz) of the CLEAN guitar (PARALLEL
//     mode), so the difference tones are clean (no mud from many chord tones mixing in a full-band
//     distortion) and no oversampling is needed: the band is far below Nyquist and a low-pass after the
//     curve removes what it generates above ~2.5 x FREQ. No added latency.
//
// Modes:
//   PARALLEL: source = the DI guitar (before the amps). Tight, clean root under the amps.
//   POST:     source = the amps' own output (mid). Thickens the low end the amps already have.
//   SUB:      EXPERIMENTAL. PARALLEL + a tone one octave below, made by halving the frequency of the
//             low band (flip-flop on its zero crossings, as in analogue octave dividers). Reliable on
//             single low notes; on chords the divider follows the strongest low component and may jump.
//
// Off (and faded out): the output is bit-identical to the input and nothing is processed.
// prepare(): non-RT. process(): audio thread, allocation- and lock-free.

#include <atomic>

namespace ampsurd
{

enum class RootMode { parallel = 0, post = 1, sub = 2 };

struct RootSettings
{
    bool on = false;
    RootMode mode = RootMode::parallel;
    double levelDb = 0.0;    // ROOT: level of the low layer, -24 .. +12 dB
    double freqHz = 120.0;   // FREQ: top of the low band, 60 .. 250 Hz
    double growl = 0.4;      // GROWL: 0 = clean low band, 1 = fully saturated (difference tones)
    double tightHz = 35.0;   // TIGHT: high-pass under the root, 20 .. 100 Hz
    double comp = 0.5;       // COMP: 0 = off, 1 = heavy, fast (palm-mute "woof" stays even)
    double subDb = 0.0;      // BASS EQ: low shelf at 50 Hz, -12 .. +12 dB
    double lowDb = 0.0;      //          peak at 100 Hz
    double lowMidDb = 0.0;   //          peak at 250 Hz
    bool invert = false;     // polarity of the layer (if it cancels the amps' low end instead of adding)
};

class RootEnhancer
{
public:
    void prepare(double sampleRate, int maxBlock);
    void reset() noexcept;

    // di: the guitar before the amps (mono); L / R: the amps' output, the layer is added to both.
    // R may be nullptr (mono).
    void process(const double* di, double* L, double* R, int n, const RootSettings& s) noexcept;

    bool isActive() const noexcept { return active; }
    // peak of the added layer since the last call (UI meter), any thread
    float getAndResetLayerPeak() noexcept { return layerPeak.exchange(0.0f); }
    float getAndResetGainReductionDb() noexcept { return grPeakDb.exchange(0.0f); }

    // The asymmetric saturation curve (smooth, continuous first derivative, |y| < 1): positive side
    // soft (x / (1 + x)), negative side clips harder and earlier, so even harmonics and the 0.5 f
    // difference tone appear. y(0) = 0, y'(0) = 1.
    static double shape(double x) noexcept;

    // Magnitude (dB) of the layer's linear filters at hz: low-pass FREQ, TIGHT high-pass, BASS EQ and
    // LEVEL (no saturation / compression) - for the panel's curve.
    static double responseDb(const RootSettings& s, double hz, double sampleRate) noexcept;

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0.0; }
        void setLowPass(double f, double q, double sr) noexcept;
        void setHighPass(double f, double q, double sr) noexcept;
        void setLowShelf(double f, double gainDb, double sr) noexcept;
        void setPeak(double f, double gainDb, double q, double sr) noexcept;
        double magnitude(double hz, double sr) const noexcept;
    };

private:
    void configure(const RootSettings& s) noexcept;

    double sr = 48000.0;
    RootSettings current;
    bool configured = false, active = false;

    Biquad lp1, lp2;            // 24 dB/oct low band (Linkwitz-Riley)
    Biquad satLp;               // removes what the curve generates far above the band
    Biquad subLp1, subLp2, subPre; // SUB: clean zero crossings / smoothing of the divider square
    Biquad tight, eqSub, eqLow, eqLowMid;
    double dcX = 0, dcY = 0, dcCoef = 0.999;

    // GROWL keeps the layer's loudness: slow RMS of the clean band vs. the saturated band
    double powIn = 0, powSat = 0, powCoef = 0;
    // SUB divider
    double env = 0, envAtt = 0, envRel = 0;
    bool armed = false;
    double square = 1.0;
    // COMP
    double compEnv = 0, compAtt = 0, compRel = 0;
    // fades
    double onGain = 0.0, fadeStep = 0.0, growlNow = 0.0, levelNow = 0.0, smooth = 0.0;

    std::atomic<float> layerPeak { 0.0f }, grPeakDb { 0.0f };
};

} // namespace ampsurd

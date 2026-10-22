#pragma once

// BlendToneEq: keeps the TONE of a fader blend, not only its loudness (2026-10-22).
//
// Why: different amps add together differently in different frequency ranges. Their lows / mids are
// similar (they add up almost in phase), their highs are much less alike and partly cancel. One broadband
// loudness compensation G (Engine, mix law) therefore left a 50:50 blend 2-4 dB darker in the highs than
// either amp alone (measured with tools/blend_spectrum).
//
// What: the mix law is applied once more in kBands frequency bands (the same measured covariance, split
// by frequency). corrections[b] = G_b / G (dB) is how much band b needs on top of the broadband G so that
// the blend has, in every band, the percentage-weighted average power of the amps it is made of.
// A static, smoothed EQ (low shelf, 8 bells one band wide, high shelf; Simper SVF, as ParametricEq) applies
// it to the blend. Like G it only changes when the mix, the captures or the alignment change - never
// with the playing. One amp alone, NOTES mode, or no measurement: all 0 dB -> bit-identical bypass.
//
// fitGains(): the bands overlap, so the filter gains are fitted (a few iterations) until the cascade's
// response at each band's centre equals the wanted correction.
//
// prepare(): non-RT. setTargets()/process(): audio thread, no allocation.

#include <array>

namespace ampsurd
{

class BlendToneEq
{
public:
    static constexpr int kBands = 10;
    static constexpr double kMaxCorrectionDb = 6.0;
    // band edges (Hz): octaves up to 2.4 kHz, half octaves above (where amps differ most)
    static constexpr std::array<double, kBands + 1> kEdgesHz { 20.0, 150.0, 300.0, 600.0, 1200.0, 2400.0, 3400.0, 4800.0, 6800.0, 9600.0, 20000.0 };
    static int bandOfHz(double hz) noexcept; // -1 outside 20 Hz .. 20 kHz
    static double centreHz(int band) noexcept;

    using Gains = std::array<double, kBands>;

    // Filter gains (dB) whose combined response hits `correctionsDb` at the band centres.
    static Gains fitGains(const Gains& correctionsDb, double sampleRate) noexcept;
    // Magnitude response (dB) of the filters with these gains.
    static double responseDb(const Gains& filterGainsDb, double hz, double sampleRate) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    // Audio thread: wanted corrections per band (dB). Fitted only when they change.
    void setTargets(const Gains& correctionsDb) noexcept;
    // Audio thread. R may be nullptr (mono).
    void process(double* L, double* R, int n) noexcept;

    bool isActive() const noexcept { return active; }
    const Gains& getTargets() const noexcept { return targets; }

private:
    struct Coefs { double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0, g = 1, k = 1; };
    struct State { double ic1 = 0, ic2 = 0; };
    static Coefs design(int band, double gainDb, double sampleRate) noexcept;

    double sr = 48000.0, smooth = 0.0;
    Gains targets {}, filterTargets {}, now {};
    std::array<Coefs, kBands> coefs {};
    std::array<State, kBands> stL {}, stR {};
    bool active = false;
};

} // namespace ampsurd

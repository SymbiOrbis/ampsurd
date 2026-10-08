#pragma once

// Effects after the noise gate (so their tails ring out):
//
//   gate → FLANGER → DELAY 1 ┐ (parallel, both fed by the flanger output)
//                   DELAY 2 ┘ → REVERB (fed by guitar + delays) → OUTPUT → limiter
//
// All stereo, no latency, all parameters smoothed or crossfaded (no clicks). Delays and reverb are
// "spill-over" effects: switching one OFF stops feeding it, its repeats / tail fade out naturally,
// and once silent it stops running (no CPU). With every effect off and silent the chain is
// bit-transparent.
//
// prepare(): non-RT. process(): audio thread, allocation- and lock-free.

#include <array>
#include <cstdint>
#include <vector>

namespace ampsurd
{

struct DelaySettings
{
    bool on = false;
    double timeMs = 500.0;     // exact: 1 .. 2000 ms
    float feedback = 0.35f;    // 0 .. 0.95
    float level = 0.35f;       // level of the repeats (dry stays at 100 %)
    float toneHz = 6000.0f;    // high cut of the repeats (>= 19 kHz: off)
    bool pingPong = false;
};

enum class ReverbType { room = 0, hall, plate, cathedral, ambience };
constexpr int kNumReverbTypes = 5;

struct ReverbSettings
{
    bool on = false;
    ReverbType type = ReverbType::hall;
    float decaySeconds = 2.2f; // RT60, 0.2 .. 12 s
    float preDelayMs = 20.0f;  // 0 .. 250 ms
    float toneHz = 7000.0f;    // high-frequency damping
    float level = 0.25f;       // reverb level (dry stays at 100 %)
};

struct FlangerSettings
{
    bool on = false;
    float rateHz = 0.25f;      // 0.05 .. 5 Hz
    float depth = 0.7f;        // 0 .. 1 (sweep range)
    float feedback = 0.5f;     // -0.9 .. 0.9
    float mix = 0.5f;          // 0 .. 1 (0.5 = deepest notches)
};

struct FxSettings
{
    FlangerSettings flanger;
    std::array<DelaySettings, 2> delay {};
    ReverbSettings reverb;
};

// type defaults for the UI (choosing a type sets these)
struct ReverbPreset { float decaySeconds, preDelayMs, toneHz; };
ReverbPreset reverbTypeDefaults(ReverbType t) noexcept;
const char* reverbTypeName(ReverbType t) noexcept;

// ---------------------------------------------------------------------------------------------
class Delay
{
public:
    void prepare(double sampleRate, double maxSeconds = 2.1);
    void reset() noexcept;
    // adds the repeats of in to out
    void process(const double* inL, const double* inR, double* outL, double* outR, int n, const DelaySettings&) noexcept;
    bool isActive() const noexcept { return !idle; }

private:
    double read(const std::vector<double>& buf, double delaySamples) const noexcept;
    double sr = 48000.0;
    std::vector<double> bufL, bufR;
    int mask = 0, writePos = 0;
    double curDelay = 0.0, newDelay = 0.0;
    int xfadePos = 0, xfadeLen = 1;
    double inGain = 0.0, levelS = 0.0, fbS = 0.0, rampStep = 0.0, smooth = 0.0;
    double lpL = 0, lpR = 0, hpL = 0, hpR = 0, hpInL = 0, hpInR = 0;
    double lpCoef = 1.0, hpCoef = 0.0;
    bool idle = true;
    int64_t quiet = 0;
};

// ---------------------------------------------------------------------------------------------
class Reverb
{
public:
    static constexpr int kLines = 8;
    void prepare(double sampleRate);
    void reset() noexcept;
    void process(const double* inL, const double* inR, double* outL, double* outR, int n, const ReverbSettings&) noexcept;
    bool isActive() const noexcept { return !idle; }

private:
    struct Allpass
    {
        std::vector<double> buf;
        int len = 1, pos = 0;
        double process(double x, double g) noexcept;
    };
    void configure(ReverbType t) noexcept;  // lengths for a type (buffers preallocated)
    double sr = 48000.0;
    ReverbType current = ReverbType::hall;
    bool configured = false;

    std::vector<double> preL, preR;
    int preMask = 0, prePos = 0;
    std::array<Allpass, 4> diffL, diffR;
    std::array<std::vector<double>, kLines> line;
    std::array<int, kLines> lineLen {}, linePos {};
    std::array<double, kLines> lp {}, lfoPhase {}, lfoInc {};
    double modDepth = 0.0, diffusion = 0.7;
    // early reflections (tapped from the pre-delayed input)
    struct Tap { double delayL, delayR, gL, gR; };
    std::array<Tap, 8> taps {};
    int numTaps = 0;
    double erLevel = 0.0, lateLevel = 1.0;

    double inGain = 0.0, levelS = 0.0, duck = 1.0, rampStep = 0.0, smooth = 0.0;
    double preCur = 0.0, preNew = 0.0; // pre-delay changes crossfade from the old to the new time
    int preX = 0, preXLen = 1;
    int pendingType = -1;
    bool idle = true;
    int64_t quiet = 0;
};

// ---------------------------------------------------------------------------------------------
class Flanger
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void process(double* L, double* R, int n, const FlangerSettings&) noexcept; // in place
    bool isActive() const noexcept { return wet > 0.0; }

private:
    double sr = 48000.0;
    std::vector<double> bufL, bufR;
    int mask = 0, writePos = 0;
    double phase = 0.0, wet = 0.0, rampStep = 0.0, smooth = 0.0;
    double rateS = 0.25, depthS = 0.7, fbS = 0.5, mixS = 0.5;
    double lastL = 0, lastR = 0;
};

// ---------------------------------------------------------------------------------------------
class FxChain
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(double* L, double* R, int n, const FxSettings&) noexcept;
    bool anyActive() const noexcept;

private:
    Flanger flanger;
    std::array<Delay, 2> delays;
    Reverb reverb;
    std::vector<double> inL, inR;
};

} // namespace ampsurd

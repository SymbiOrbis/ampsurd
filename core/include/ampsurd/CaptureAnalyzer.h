#pragma once

// CaptureAnalyzer: off-line (non-real-time) measurement of the loaded captures.
//
// Every capture is rendered with the same guitar-like DI test signal (plucked notes,
// power chords, palm mutes, an open chord). From the outputs we derive:
//   * cross-spectra between all captures (Welch, 4096-point FFT)
//   * AUTO alignment of each capture against a reference capture: the time offset (with
//     sub-sample precision) and polarity that maximise the low/low-mid band correlation
//     (70-1200 Hz, where cancellation between amps hurts most)
//   * the covariance matrix used by the Engine's loudness-compensated mix law, for any
//     combination of applied delays / polarities / phase rotations.
//
// Nothing here may run on the audio thread (it allocates and does FFTs).

#include <array>
#include <complex>
#include <memory>
#include <vector>

#include "ampsurd/Engine.h"

namespace ampsurd
{

class CaptureAnalyzer
{
public:
    static constexpr int kFftSize = 4096;
    static constexpr int kHop = kFftSize / 2;
    static constexpr int kBins = kFftSize / 2 + 1;
    static constexpr double kMaxAutoLagMs = 5.0;
    static constexpr double kMinReliableCorrelation = 0.3;

    using Spectrum = std::vector<std::complex<double>>; // kBins

    // Spectral frames of one rendered capture output.
    struct Measurement
    {
        double sampleRate = 48000.0;
        std::vector<Spectrum> frames;
    };

    struct Alignment
    {
        double lagSamples = 0.0;   // >0: this capture is LATER than the reference
        double polarity = 1.0;     // -1: inverted relative to the reference
        double correlationBefore = 0.0; // band-limited normalised correlation, as loaded (signed)
        double correlationAfter = 0.0;  // after applying lag + polarity
        bool reliable = false;     // false: captures too different, leave unaligned
    };

    using Matrix = std::array<std::array<double, kNumSlots>, kNumSlots>;

    // Guitar-like DI test signal, ~3 s, peak -6 dBFS. Deterministic.
    static std::vector<double> makeTestSignal(double sampleRate);

    // Render `signal` through a capture prepared at `sampleRate` (blocks of <= maxBlock).
    static std::vector<double> render(CaptureModel& model, const std::vector<double>& signal, bool levelMatch);

    static std::shared_ptr<const Measurement> measure(const std::vector<double>& output, double sampleRate);

    // Cross-spectrum S_ab[k] = sum over frames of X_a[k] * conj(X_b[k]).
    static Spectrum crossSpectrum(const Measurement& a, const Measurement& b);

    static Alignment estimateAlignment(const Measurement& reference, const Measurement& other);

    // Covariance of the processed outputs, including applied delays (samples), polarities and
    // phase rotations (radians). Missing measurements leave their rows/columns at 0 / invalid.
    struct Covariance
    {
        Matrix C {};
        std::array<bool, kNumSlots> valid {};
    };
    static Covariance covariance(const std::array<std::shared_ptr<const Measurement>, kNumSlots>& m,
                                 const std::array<std::array<Spectrum, kNumSlots>, kNumSlots>& cross,
                                 const std::array<double, kNumSlots>& delaySamples,
                                 const std::array<double, kNumSlots>& polarity,
                                 const std::array<double, kNumSlots>& phaseRadians,
                                 const std::vector<double>* binWeights = nullptr);

    // Per-bin power weights of the ITU-R BS.1770 "K" loudness weighting at a sample rate, so that
    // the covariance (and therefore the mix law) follows perceived loudness, not raw power.
    static std::vector<double> loudnessWeights(double sampleRate);

    // K-weighting filter (BS.1770) applied in place, for time-domain loudness measurements.
    static void kWeightInPlace(std::vector<double>& x, double sampleRate);

    // ---- Level match -------------------------------------------------------------------
    // Perceived loudness (K-weighted, dB) of a measured capture output, and the gain that brings
    // it to kTargetLoudnessDb. Measured by AMPSURD itself, so it also works for captures without
    // loudness metadata and is consistent between all capture types.
    static constexpr double kTargetLoudnessDb = -18.0;
    static double loudnessDb(const Measurement& m);
    static double levelMatchGain(double loudnessDb);

    // ---- Whole-rig analysis (everything the plugin needs, computed off the audio thread) ----
    struct RigAnalysis
    {
        std::array<std::shared_ptr<const Measurement>, kNumSlots> meas {};
        std::array<double, kNumSlots> loudnessDb {};
        std::array<std::array<Spectrum, kNumSlots>, kNumSlots> cross {};
        int reference = -1;                          // slot all others are aligned to
        std::array<Alignment, kNumSlots> align {};   // per slot, relative to the reference
        std::array<double, kNumSlots> autoDelay {};  // delay that AUTO applies (samples, >= 0)
        double sampleRate = 48000.0;
        std::vector<double> kWeights;
    };
    static std::shared_ptr<const RigAnalysis> analyseRig(const std::array<std::shared_ptr<const Measurement>, kNumSlots>& meas);

    // Covariance for the Engine from a rig analysis and the delays / polarities / phases / level
    // gains that are actually applied (loudness-weighted).
    static Covariance covariance(const RigAnalysis& rig,
                                 const std::array<double, kNumSlots>& delaySamples,
                                 const std::array<double, kNumSlots>& polarity,
                                 const std::array<double, kNumSlots>& phaseRadians,
                                 const std::array<double, kNumSlots>& levelGain);

    // In-place radix-2 FFT (size must be a power of two).
    static void fft(std::vector<std::complex<double>>& a, bool inverse);
};

} // namespace ampsurd

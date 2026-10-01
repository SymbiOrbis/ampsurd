#pragma once

// CaptureModel: one loaded, ready-to-run NAM capture.
//
// Loading and preparing happen OFF the audio thread (they parse JSON and allocate).
// Only process() may be called on the audio thread.
//
// Any .nam file that the official NeuralAmpModelerCore can load is accepted as-is:
// A2 Full, A2 Lite, slimmable A2 containers (Full+Lite in one file), A1 WaveNet
// (standard/lite/feather/nano), LSTM, ConvNet and Linear. No wrapper or conversion.

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nam { class DSP; }

namespace monstrosity
{

using Sample = double; // NAM Core's default NAM_SAMPLE type

struct CaptureInfo
{
    std::string filePath;
    std::string displayName;      // file name without extension (metadata name if present)
    std::string architecture;     // "architecture" field of the .nam file, e.g. WaveNet, SlimmableContainer, LSTM
    std::string architectureHint; // human-readable guess, e.g. "A2 (Full + Lite)", "A1 WaveNet"
    std::string fileVersion;      // .nam file format version
    double modelSampleRate = 48000.0;
    bool hasLoudness = false;
    double loudnessDb = 0.0;
    bool hasInputLevel = false;
    double inputLevelDbu = 0.0;
    bool slimmable = false;
    std::string metadataJson;     // untouched "metadata" object, kept for attribution
};

class CaptureModel
{
public:
    struct LoadResult
    {
        std::unique_ptr<CaptureModel> model;
        std::string error; // empty on success
    };

    // Non-RT. Loads the file and prepares it for the given host sample rate / max block size.
    static LoadResult load(const std::filesystem::path& file, double hostSampleRate, int maxBlockSize);

    // Non-RT. An "empty" model outputs silence (used to unload a slot without a special case).
    static std::unique_ptr<CaptureModel> makeEmpty(double hostSampleRate, int maxBlockSize);

    ~CaptureModel();

    // Non-RT (may allocate). Call whenever the host sample rate or max block size changes.
    void prepare(double hostSampleRate, int maxBlockSize);

    // RT-safe. numFrames must be <= the maxBlockSize given to prepare().
    // Writes processed audio to `out`; when normalise is true the capture's loudness
    // metadata is used to bring it to a common reference level.
    void process(const Sample* in, Sample* out, int numFrames, bool normalise) noexcept;

    bool isEmpty() const noexcept { return dsp == nullptr; }
    int getLatencySamples() const noexcept { return latencySamples; }
    double getPreparedSampleRate() const noexcept { return preparedSampleRate; }
    int getPreparedBlockSize() const noexcept { return preparedBlockSize; }
    float getNormalisationGain() const noexcept { return normalisationGain; }
    const CaptureInfo& getInfo() const noexcept { return info; }

    // Loudness reference used by the official NAM plugin's "Normalize" switch.
    static constexpr double kNormalisationTargetDb = -18.0;

private:
    CaptureModel();

    struct Resampler; // pimpl around AudioDSPTools' ResamplingContainer

    std::unique_ptr<nam::DSP> dsp;
    std::unique_ptr<Resampler> resampler;
    CaptureInfo info;

    bool needsResampling = false;
    int latencySamples = 0;
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    float normalisationGain = 1.0f;

    std::vector<Sample> inScratch;
};

} // namespace monstrosity

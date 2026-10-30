#include "ampsurd/CaptureModel.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "NAM/activations.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#include "json.hpp"

// AudioDSPTools' resampler was extracted from iPlug2 and still expects two iPlug constants.
namespace iplug
{
static constexpr double PI = 3.14159265358979323846;
}
#ifndef DEFAULT_BLOCK_SIZE
  #define DEFAULT_BLOCK_SIZE 1024
#endif
#include "ResamplingContainer/ResamplingContainer.h"

namespace ampsurd
{

// Lanczos resampler from AudioDSPTools - the same one the official NAM plugin uses.
// NAM captures are trained at a fixed rate (almost always 48 kHz); at any other host
// rate the capture is run at its native rate between an up- and a down-sampler.
struct CaptureModel::Resampler
{
    explicit Resampler(double modelRate) : container(modelRate) {}
    dsp::ResamplingContainer<Sample, 1, 12> container;
    std::function<void(Sample**, Sample**, int)> callback; // created once, never on the audio thread
};

namespace
{
std::string describeArchitecture(const nlohmann::json& j)
{
    const std::string arch = j.value("architecture", std::string("unknown"));

    auto describeWaveNet = [](const nlohmann::json& cfg) -> std::string {
        // A2 uses 23 layers with the 1,3,7,17,41,101,239 dilation pattern; Full = 8 channels, Lite = 3.
        if (cfg.contains("layers") && cfg["layers"].is_array() && !cfg["layers"].empty())
        {
            const auto& l0 = cfg["layers"][0];
            const auto nDil = l0.contains("dilations") ? l0["dilations"].size() : 0;
            const int ch = l0.value("channels", 0);
            if (nDil == 23)
                return ch == 8 ? "A2 Full" : (ch == 3 ? "A2 Lite" : "A2 (" + std::to_string(ch) + " ch)");
            return "A1 WaveNet (" + std::to_string(ch) + " ch)";
        }
        return "WaveNet";
    };

    if (arch == "WaveNet" && j.contains("config"))
        return describeWaveNet(j["config"]);

    if (arch == "SlimmableContainer" && j.contains("config") && j["config"].contains("submodels"))
    {
        std::string out = "Slimmable [";
        bool first = true;
        for (const auto& s : j["config"]["submodels"])
        {
            if (!first) out += " + ";
            first = false;
            if (s.contains("model"))
            {
                const auto& m = s["model"];
                out += (m.value("architecture", std::string()) == "WaveNet" && m.contains("config"))
                           ? describeWaveNet(m["config"])
                           : m.value("architecture", std::string("?"));
            }
        }
        return out + "]";
    }
    return arch;
}

std::string stemOf(const std::filesystem::path& p)
{
    auto s = p.stem().u8string();
    return std::string(s.begin(), s.end());
}
} // namespace

CaptureModel::CaptureModel() = default;
CaptureModel::~CaptureModel() = default;

std::atomic<bool>& CaptureModel::activationChosen()
{
    static std::atomic<bool> chosen { false };
    return chosen;
}

void CaptureModel::setFastTanh(bool on)
{
    activationChosen().store(true);
    if (on) nam::activations::Activation::enable_fast_tanh();
    else nam::activations::Activation::disable_fast_tanh();
}

CaptureModel::LoadResult CaptureModel::load(const std::filesystem::path& file, double hostSampleRate, int maxBlockSize, bool eco)
{
    LoadResult result;
    // Same activation as the official NAM plugin (it enables NAM Core's fast tanh at start-up), so a
    // capture in AMPSURD is sample-identical to the same capture in the NAM plugin. Set once, before
    // the first model is created (models take their activation function when they are built).
    if (!activationChosen().exchange(true))
        nam::activations::Activation::enable_fast_tanh();
    try
    {
        auto model = std::unique_ptr<CaptureModel>(new CaptureModel());

        // 1. Read the file's descriptive fields (informational only; loading is done by NAM Core below).
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
                throw std::runtime_error("Cannot open file");
            const auto j = nlohmann::json::parse(in);
            auto& info = model->info;
            {
                auto u8 = file.u8string();
                info.filePath.assign(u8.begin(), u8.end());
            }
            info.displayName = stemOf(file);
            info.architecture = j.value("architecture", std::string("unknown"));
            info.architectureHint = describeArchitecture(j);
            info.fileVersion = j.value("version", std::string());
            if (j.contains("metadata") && j["metadata"].is_object())
            {
                const auto& md = j["metadata"];
                info.metadataJson = md.dump();
                if (md.contains("name") && md["name"].is_string() && !md["name"].get<std::string>().empty())
                    info.displayName = md["name"].get<std::string>();
            }
        }

        // 2. Load with the official NeuralAmpModelerCore loader. Prewarm is deferred to prepare().
        nam::DspLoadOptions opts;
        opts.prewarm = false;
        model->dsp = nam::get_dsp(file, opts);
        if (!model->dsp)
            throw std::runtime_error("NeuralAmpModelerCore could not create a model from this file");

        if (model->dsp->NumInputChannels() != 1)
            throw std::runtime_error("Only mono-input captures are supported (this one has "
                                     + std::to_string(model->dsp->NumInputChannels()) + " inputs)");
        if (model->dsp->NumOutputChannels() != 1)
            throw std::runtime_error("Only mono-output captures are supported (this one has "
                                     + std::to_string(model->dsp->NumOutputChannels()) + " outputs)");

        auto& info = model->info;
        const double reported = model->dsp->GetExpectedSampleRate();
        info.modelSampleRate = reported > 0.0 ? reported : 48000.0; // same assumption as the official plugin
        info.hasLoudness = model->dsp->HasLoudness();
        info.loudnessDb = info.hasLoudness ? model->dsp->GetLoudness() : 0.0;
        info.hasInputLevel = model->dsp->HasInputLevel();
        info.inputLevelDbu = info.hasInputLevel ? model->dsp->GetInputLevel() : 0.0;

        // 3. Slimmable files (A2 Full + Lite in one file): full size, or Lite in ECO mode.
        if (auto* slim = dynamic_cast<nam::SlimmableModel*>(model->dsp.get()))
        {
            info.slimmable = true;
            info.ecoActive = eco;
            slim->SetSlimmableSize(eco ? 0.0 : 1.0); // ECO: the smallest (Lite) model, else the full one
        }

        model->normalisationGain = info.hasLoudness
                                       ? (float) std::pow(10.0, (kNormalisationTargetDb - info.loudnessDb) / 20.0)
                                       : 1.0f;

        model->prepare(hostSampleRate, maxBlockSize);
        result.model = std::move(model);
    }
    catch (const std::exception& e)
    {
        result.error = e.what();
        result.model.reset();
    }
    catch (...)
    {
        result.error = "Unknown error while loading capture";
        result.model.reset();
    }
    return result;
}

std::unique_ptr<CaptureModel> CaptureModel::makeEmpty(double hostSampleRate, int maxBlockSize)
{
    auto m = std::unique_ptr<CaptureModel>(new CaptureModel());
    m->prepare(hostSampleRate, maxBlockSize);
    return m;
}

std::unique_ptr<CaptureModel> CaptureModel::makeMorph(std::vector<MorphStep> s, const CaptureInfo& inf, double hostSampleRate, int maxBlockSize)
{
    auto m = std::unique_ptr<CaptureModel>(new CaptureModel());
    std::sort(s.begin(), s.end(), [](const MorphStep& a, const MorphStep& b) { return a.knob < b.knob; });
    m->steps = std::move(s);
    for (auto& st : m->steps) m->stepKnobs.push_back(st.knob);
    m->info = inf;
    m->prepare(hostSampleRate, maxBlockSize);
    return m;
}

void CaptureModel::morphPair(const std::vector<double>& knobs, double pos, int& lower, double& weightUpper) noexcept
{
    lower = 0;
    weightUpper = 0.0;
    const int n = (int) knobs.size();
    if (n <= 1) return;
    if (pos <= knobs.front()) return;
    if (pos >= knobs.back()) { lower = n - 2; weightUpper = 1.0; return; }
    while (lower < n - 2 && pos >= knobs[(size_t) lower + 1]) ++lower;
    const double span = knobs[(size_t) lower + 1] - knobs[(size_t) lower];
    weightUpper = span > 0.0 ? (pos - knobs[(size_t) lower]) / span : 0.0;
}

void CaptureModel::prepare(double hostSampleRate, int maxBlockSize)
{
    preparedSampleRate = hostSampleRate;
    preparedBlockSize = std::max(1, maxBlockSize);
    inScratch.assign((size_t) preparedBlockSize, 0.0);

    if (!steps.empty())
    {
        int maxLatency = 0;
        for (auto& st : steps)
        {
            st.model->prepare(hostSampleRate, preparedBlockSize);
            maxLatency = std::max(maxLatency, st.model->getLatencySamples() + st.delay);
        }
        stepDelayLines.assign(steps.size(), {});
        stepDelayPos.assign(steps.size(), 0);
        // `delay` aligns the steps as they sound (it was measured from their outputs, resampling latency included)
        for (size_t i = 0; i < steps.size(); ++i) stepDelayLines[i].assign((size_t) std::max(0, steps[i].delay) + 1, 0.0);
        latencySamples = maxLatency;
        morphA.assign((size_t) preparedBlockSize, 0.0);
        morphB.assign((size_t) preparedBlockSize, 0.0);
        morphCoef = 1.0 - std::exp(-(double) preparedBlockSize / (0.030 * hostSampleRate)); // ~30 ms glide (per block)
        morphNow = -1.0;
        return;
    }

    if (!dsp)
    {
        latencySamples = 0;
        return;
    }

    needsResampling = std::abs(hostSampleRate - info.modelSampleRate) > 0.5 && !dsp->SupportsArbitrarySampleRate();

    if (needsResampling)
    {
        resampler = std::make_unique<Resampler>(info.modelSampleRate);
        resampler->container.Reset(hostSampleRate, preparedBlockSize);
        nam::DSP* raw = dsp.get();
        resampler->callback = [raw](Sample** i, Sample** o, int n) { raw->process(i, o, n); };

        const double ratio = hostSampleRate / info.modelSampleRate;
        const int maxInner = (int) std::ceil((double) preparedBlockSize / ratio) + 8;
        dsp->ResetAndPrewarm(info.modelSampleRate, maxInner);
        latencySamples = resampler->container.GetLatency();
    }
    else
    {
        resampler.reset();
        dsp->ResetAndPrewarm(dsp->SupportsArbitrarySampleRate() ? hostSampleRate : info.modelSampleRate,
                             preparedBlockSize);
        latencySamples = 0;
    }
}

void CaptureModel::runStep(size_t i, const Sample* in, Sample* out, int n) noexcept
{
    auto& st = steps[i];
    st.model->process(in, out, n, false);
    auto& line = stepDelayLines[i];
    const int d = (int) line.size();
    int& pos = stepDelayPos[i];
    const double g = st.gain * st.polarity;
    for (int k = 0; k < n; ++k)
    {
        line[(size_t) pos] = out[k];
        if (++pos >= d) pos = 0;
        out[k] = line[(size_t) pos] * g; // `delay` samples old (d = delay + 1)
    }
}

void CaptureModel::processMorph(const Sample* in, Sample* out, int n) noexcept
{
    const double target = morphTarget.load(std::memory_order_relaxed);
    const double start = morphNow < 0.0 ? target : morphNow;
    double end = start + (target - start) * morphCoef;
    if (std::abs(target - end) < 1e-4) end = target;
    morphNow = end;
    // the pair is chosen at the block's middle; at a step both pairs give that step alone, so changing pairs is seamless
    int lower = 0;
    double w0 = 0.0, w1 = 0.0;
    morphPair(stepKnobs, 0.5 * (start + end), lower, w0);
    {
        int l2 = 0;
        double a = 0.0, b = 0.0;
        morphPair(stepKnobs, start, l2, a);
        w0 = l2 == lower ? a : (l2 < lower ? 0.0 : 1.0);
        morphPair(stepKnobs, end, l2, b);
        w1 = l2 == lower ? b : (l2 < lower ? 0.0 : 1.0);
    }
    if (steps.size() == 1) { runStep(0, in, out, n); return; }
    const bool needLower = w0 < 1.0 || w1 < 1.0, needUpper = w0 > 0.0 || w1 > 0.0;
    if (needLower) runStep((size_t) lower, in, morphA.data(), n);
    if (needUpper) runStep((size_t) lower + 1, in, morphB.data(), n);
    for (int k = 0; k < n; ++k)
    {
        const double w = w0 + (w1 - w0) * (n > 1 ? (double) k / (double) (n - 1) : 1.0);
        out[k] = (needLower ? (1.0 - w) * morphA[(size_t) k] : 0.0) + (needUpper ? w * morphB[(size_t) k] : 0.0);
    }
}

void CaptureModel::process(const Sample* in, Sample* out, int numFrames, bool normalise) noexcept
{
    if (!steps.empty())
    {
        if (numFrames > preparedBlockSize) { std::fill(out, out + std::max(0, numFrames), 0.0); return; }
        processMorph(in, out, numFrames);
        return;
    }
    if (!dsp || numFrames > preparedBlockSize)
    {
        std::fill(out, out + std::max(0, numFrames), 0.0);
        return;
    }

    std::copy(in, in + numFrames, inScratch.data());
    Sample* inPtr = inScratch.data();
    Sample* outPtr = out;

    try
    {
        if (needsResampling)
            resampler->container.ProcessBlock(&inPtr, &outPtr, numFrames, resampler->callback);
        else
            dsp->process(&inPtr, &outPtr, numFrames);
    }
    catch (...)
    {
        // Never let an exception escape into the host's audio thread.
        std::fill(out, out + numFrames, 0.0);
        return;
    }

    if (normalise && normalisationGain != 1.0f)
        for (int i = 0; i < numFrames; ++i)
            out[i] *= normalisationGain;
}

} // namespace ampsurd

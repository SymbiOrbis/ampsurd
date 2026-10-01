#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <filesystem>

namespace
{
const juce::Identifier kCapturePathId { "capturePath" };

std::filesystem::path toStdPath(const juce::File& f)
{
#if JUCE_WINDOWS
    return std::filesystem::path(f.getFullPathName().toWideCharPointer()); // keeps non-ASCII file names intact
#else
    return std::filesystem::path(f.getFullPathName().toStdString());
#endif
}

juce::String describe(const monstrosity::CaptureModel& m)
{
    const auto& i = m.getInfo();
    juce::String s;
    s << juce::String::fromUTF8(i.displayName.c_str()) << "\n";
    s << "Type: " << i.architectureHint << "   (file v" << i.fileVersion << ")\n";
    s << "Model rate: " << juce::String(i.modelSampleRate, 0) << " Hz   host rate: "
      << juce::String(m.getPreparedSampleRate(), 0) << " Hz   latency: " << m.getLatencySamples() << " samples\n";
    s << "Loudness: " << (i.hasLoudness ? juce::String(i.loudnessDb, 1) + " dB" : juce::String("not in file"));
    if (i.hasInputLevel)
        s << "   Calibrated input: " << juce::String(i.inputLevelDbu, 1) << " dBu";
    return s;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MonstrosityProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "input", 1 }, "Input",
                                                     NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
                                                     AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "output", 1 }, "Output",
                                                     NormalisableRange<float>(-40.0f, 12.0f, 0.1f), 0.0f,
                                                     AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "normalise", 1 }, "Normalise", true));
    return layout;
}

MonstrosityProcessor::MonstrosityProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "MONSTROSITY", createLayout())
{
    inputParam = params.getRawParameterValue("input");
    outputParam = params.getRawParameterValue("output");
    normaliseParam = params.getRawParameterValue("normalise");
    setStatus({}, "No capture loaded. Click LOAD and choose a .nam file.", false, false);
    startTimerHz(5);
}

MonstrosityProcessor::~MonstrosityProcessor()
{
    stopTimer();
    alive->store(false);
    loaderPool.removeAllJobs(true, 10000);
    slot.collectGarbage();
}

bool MonstrosityProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto ok = [](const juce::AudioChannelSet& s) {
        return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
    };
    return ok(in) && ok(out);
}

void MonstrosityProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int maxBlock = juce::jmax(1, samplesPerBlock);
    {
        std::lock_guard<std::mutex> lock(configMutex);
        currentSampleRate = sampleRate;
        currentMaxBlock = maxBlock;
        slot.prepare(sampleRate, maxBlock);
    }
    inBuffer.assign((size_t) maxBlock, 0.0);
    outBuffer.assign((size_t) maxBlock, 0.0);
    inputGain.reset(sampleRate, 0.02);
    outputGain.reset(sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inputParam->load()));
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));
    loadMeasurer.reset(sampleRate, maxBlock);
    setLatencySamples(slot.getLatencySamples());
}

void MonstrosityProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    juce::AudioProcessLoadMeasurer::ScopedTimer cpuTimer(loadMeasurer, numSamples);

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (numSamples == 0 || numOut == 0)
        return;

    inputGain.setTargetValue(juce::Decibels::decibelsToGain(inputParam->load()));
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));
    const bool normalise = normaliseParam->load() > 0.5f;

    // Guitar DI is taken from the first input channel; the result is copied to every output.
    const float* in = numIn > 0 ? buffer.getReadPointer(0) : nullptr;
    float* out = buffer.getWritePointer(0);

    float inPeak = 0.0f, outPeak = 0.0f;
    bool clip = false;
    const int maxBlock = (int) inBuffer.size();

    // Hosts may occasionally deliver more samples than announced: process in chunks.
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin(maxBlock, numSamples - start);
        for (int i = 0; i < n; ++i)
        {
            const float x = (in != nullptr ? in[start + i] : 0.0f) * inputGain.getNextValue();
            inPeak = juce::jmax(inPeak, std::abs(x));
            inBuffer[(size_t) i] = x;
        }

        slot.process(inBuffer.data(), outBuffer.data(), n, normalise);

        for (int i = 0; i < n; ++i)
        {
            const float y = (float) outBuffer[(size_t) i] * outputGain.getNextValue();
            out[start + i] = std::isfinite(y) ? y : 0.0f;
            const float a = std::abs(out[start + i]);
            outPeak = juce::jmax(outPeak, a);
            clip = clip || a > 1.0f;
        }
    }

    for (int ch = 1; ch < numOut; ++ch)
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);

    if (inPeak > inputPeak.load(std::memory_order_relaxed)) inputPeak.store(inPeak, std::memory_order_relaxed);
    if (outPeak > outputPeak.load(std::memory_order_relaxed)) outputPeak.store(outPeak, std::memory_order_relaxed);
    if (clip) clipped.store(true, std::memory_order_relaxed);
}

void MonstrosityProcessor::loadCapture(const juce::File& file)
{
    const int gen = ++loadGeneration;
    setStatus(file.getFullPathName(), "Loading " + file.getFileName() + " ...", true, false);

    auto aliveFlag = alive;
    loaderPool.addJob([this, file, gen, aliveFlag] {
        if (!aliveFlag->load())
            return;

        std::unique_ptr<monstrosity::CaptureModel> model;
        std::string error;
        {
            // Holding the config lock guarantees the model is prepared for the rate/block
            // size the audio thread is actually using when it receives it.
            std::lock_guard<std::mutex> lock(configMutex);
            if (gen != loadGeneration.load())
                return; // superseded by a newer request
            auto result = monstrosity::CaptureModel::load(toStdPath(file), currentSampleRate, currentMaxBlock);
            model = std::move(result.model);
            error = result.error;
            if (model)
            {
                const auto text = describe(*model);
                const int latency = model->getLatencySamples();
                slot.submit(std::move(model));
                setStatus(file.getFullPathName(), text, false, false);
                juce::MessageManager::callAsync([this, latency, aliveFlag] {
                    if (aliveFlag->load())
                        setLatencySamples(latency);
                });
                return;
            }
        }
        setStatus(file.getFullPathName(), "Could not load " + file.getFileName() + "\n" + juce::String(error), false, true);
    });
}

void MonstrosityProcessor::unloadCapture()
{
    const int gen = ++loadGeneration;
    setStatus({}, "No capture loaded. Click LOAD and choose a .nam file.", false, false);
    auto aliveFlag = alive;
    loaderPool.addJob([this, gen, aliveFlag] {
        if (!aliveFlag->load())
            return;
        std::lock_guard<std::mutex> lock(configMutex);
        if (gen != loadGeneration.load())
            return;
        slot.submit(monstrosity::CaptureModel::makeEmpty(currentSampleRate, currentMaxBlock));
    });
    setLatencySamples(0);
}

void MonstrosityProcessor::timerCallback()
{
    slot.collectGarbage();
}

void MonstrosityProcessor::setStatus(const juce::String& path, const juce::String& text, bool loading, bool error)
{
    const juce::ScopedLock sl(statusLock);
    status.capturePath = path;
    status.text = text;
    status.loading = loading;
    status.error = error;
}

MonstrosityProcessor::Status MonstrosityProcessor::getStatus() const
{
    const juce::ScopedLock sl(statusLock);
    return status;
}

void MonstrosityProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    // A MONSTROSITY state references the .nam file; the capture itself is never embedded.
    state.setProperty(kCapturePathId, getStatus().capturePath, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void MonstrosityProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(params.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    params.replaceState(state);

    const juce::String path = state.getProperty(kCapturePathId).toString();
    if (path.isEmpty())
        return;

    const juce::File file(path);
    if (file.existsAsFile())
        loadCapture(file);
    else
        setStatus(path, "Capture file not found:\n" + path, false, true);
}

juce::AudioProcessorEditor* MonstrosityProcessor::createEditor()
{
    return new MonstrosityEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MonstrosityProcessor();
}

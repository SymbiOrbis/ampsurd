#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "monstrosity/CaptureModel.h"
#include "monstrosity/CaptureSlot.h"

// Milestone 1: guitar in -> one NAM capture -> out.
// The engine (monstrosity_core) is already slot-based so Milestone 2 adds slots, not rewrites.
class MonstrosityProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    MonstrosityProcessor();
    ~MonstrosityProcessor() override;

    // --- juce::AudioProcessor ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MONSTROSITY"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // --- MONSTROSITY (message thread) ---
    void loadCapture(const juce::File& file);
    void unloadCapture();

    struct Status
    {
        juce::String capturePath;
        juce::String text;  // multi-line description for the (temporary) UI
        bool loading = false;
        bool error = false;
    };
    Status getStatus() const;

    float getCpuLoadPercent() const { return (float) loadMeasurer.getLoadAsPercentage(); }
    float getAndResetOutputPeak() { return outputPeak.exchange(0.0f); }
    float getAndResetInputPeak() { return inputPeak.exchange(0.0f); }
    bool getAndResetClip() { return clipped.exchange(false); }

    juce::AudioProcessorValueTreeState params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override; // frees retired captures off the audio thread
    void setStatus(const juce::String& path, const juce::String& text, bool loading, bool error);

    // Audio engine (one slot for Milestone 1).
    monstrosity::CaptureSlot slot;

    // Guards the sample-rate / block-size configuration so a capture is never prepared
    // for a stale configuration. Taken only by prepareToPlay() and the loader thread,
    // never by the audio thread.
    std::mutex configMutex;
    double currentSampleRate = 48000.0;
    int currentMaxBlock = 512;

    std::vector<monstrosity::Sample> inBuffer, outBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* normaliseParam = nullptr;

    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<bool> clipped { false };

    mutable juce::CriticalSection statusLock; // message + loader threads only
    Status status;

    std::atomic<int> loadGeneration { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions{}.withThreadName("MONSTROSITY loader").withNumberOfThreads(1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonstrosityProcessor)
};

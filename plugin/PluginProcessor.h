#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Engine.h"
#include "ampsurd/SafetyLimiter.h"

// AMPSURD: one guitar DI -> five fixed NAM capture slots in parallel -> blended into one tone.
//
// Threads
//   audio   : processBlock (engine, gains, limiter). Never locks, allocates or touches files.
//   loader  : one background thread. Loads captures, measures them (alignment / loudness)
//             BEFORE they are swapped in, re-measures when the input gain or sample rate changes.
//   message : UI, presets, linked faders, latency reporting, frees retired captures.
class AmpsurdProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kNumSlots = ampsurd::kNumSlots;
    static constexpr int kNumBands = ampsurd::ParametricEq::kNumBands;
    static constexpr double kReserveMs = 1.0;     // headroom for FREE time offsets of -1 ms
    static constexpr double kMaxManualMs = 1.0;   // FREE time range: +/- 1 ms

    AmpsurdProcessor();
    ~AmpsurdProcessor() override;

    // --- juce::AudioProcessor ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParamObj; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AMPSURD"; }
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

    // --- parameter IDs ---
    static juce::String slotParamId(int slot, const char* name);                  // e.g. "s1_mix"
    static juce::String bandParamId(int slot, int band, const char* name);        // e.g. "s1_b3_gain"

    // --- slots (message thread) ---
    enum class SlotState { empty, loading, loaded, missing, error };
    struct SlotStatus
    {
        SlotState state = SlotState::empty;
        juce::String path;      // full path of the .nam file (also kept when missing)
        juce::String fileName;  // file name without .nam - THE identity shown in the UI
        juce::String info;      // details for tooltips / diagnostics
    };
    SlotStatus getSlotStatus(int slot) const;
    void loadCapture(int slot, const juce::File& file);
    void unloadCapture(int slot);

    // Linked mix faders: moving one fader rescales the others so the stored mix stays at 100 %.
    void beginMixGesture(int slot);
    void setMixLinked(int slot, float newPercent);
    void endMixGesture(int slot);
    float getEffectivePercent(int slot) const { return engine.getEffectivePercent(slot); }
    bool isSlotAudible(int slot) const;

    // Alignment diagnostics for the EDIT panel.
    struct AlignInfo
    {
        bool measured = false, isReference = false, reliable = false;
        double autoOffsetMs = 0.0, polarity = 1.0, corrBefore = 0.0, corrAfter = 0.0, loudnessDb = 0.0;
    };
    AlignInfo getAlignInfo(int slot) const;
    void resetAlignment(int slot); // back to AUTO with zero manual offsets

    // --- presets: one preset = the complete rig ---
    juce::File getPresetFolder() const;
    juce::Array<juce::File> listPresets() const;
    bool savePreset(const juce::File& file);
    bool loadPreset(const juce::File& file);
    void loadInitPreset();
    juce::String getCurrentPresetName() const;
    juce::File getCurrentPresetFile() const;

    // --- meters / diagnostics (any thread) ---
    float getCpuLoadPercent() const { return (float) loadMeasurer.getLoadAsPercentage(); }
    float getAndResetOutputPeak() { return outputPeak.exchange(0.0f); }
    float getAndResetInputPeak() { return inputPeak.exchange(0.0f); }
    float getAndResetLimiterReductionDb() { return limiterReductionDb.exchange(0.0f); }
    float getCompensationDb() const { return engine.getCompensationDb(); }
    double getSampleRateForUi() const { return currentSampleRate.load(); }

    juce::AudioProcessorValueTreeState params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;

    struct SlotParams
    {
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* eqOn = nullptr;
        std::atomic<float>* alignMode = nullptr; // 0 AUTO, 1 FREE
        std::atomic<float>* timeMs = nullptr;
        std::atomic<float>* phaseDeg = nullptr;
        std::array<std::atomic<float>*, kNumBands> freq {}, gain {}, q {};
    };
    std::array<SlotParams, kNumSlots> slotParams;

    // values derived from the measurements (written by loader/message thread, read by audio thread)
    struct Derived
    {
        std::atomic<double> autoDelayMs { 0.0 };
        std::atomic<double> polarity { 1.0 };
        std::atomic<double> levelGain { 1.0 };
    };
    std::array<Derived, kNumSlots> derived;

    // effective alignment of a slot from parameters + measurements (used by audio + analysis code)
    struct EffectiveAlign { double delaySamples, polarity, phaseRadians; };
    EffectiveAlign effectiveAlign(int slot, double sampleRate) const noexcept;

    // loader-thread jobs
    void loadJob(int slot, juce::File file, int generation, bool adjustMix);
    void remeasureJob(int generation);
    void applyRig(std::shared_ptr<const ampsurd::CaptureAnalyzer::RigAnalysis> rig); // any non-audio thread
    void updateCovariance(bool force);                                                // any non-audio thread
    void renormaliseMixAfterChange(int changedSlot, bool added);
    void setSlotStatus(int slot, const SlotStatus& s);
    void restoreState(const juce::ValueTree& state, const juce::File& presetFile);
    juce::ValueTree createStateTree() const;
    void updateLatency();

    ampsurd::Engine engine;
    ampsurd::SafetyLimiter limiter;

    std::mutex configMutex; // sample rate / block size; never taken by the audio thread
    std::atomic<double> currentSampleRate { 48000.0 };
    int currentMaxBlock = 512;

    // measurements (loader + message threads)
    mutable std::mutex analysisMutex;
    std::array<std::shared_ptr<const ampsurd::CaptureAnalyzer::Measurement>, kNumSlots> measurements {};
    std::shared_ptr<const ampsurd::CaptureAnalyzer::RigAnalysis> rig;
    std::atomic<int> rigVersion { 0 };
    double measuredInputGainDb = 0.0;
    std::mutex derivedMutex;
    std::array<double, 4 * kNumSlots + 4> lastCovKey {};

    std::array<std::atomic<int>, kNumSlots> slotGeneration {};
    std::atomic<int> remeasureGeneration { 0 };
    std::atomic<bool> needsRemeasure { false };
    double inputGainChangedAt = 0.0;
    float lastSeenInputGainDb = 0.0f;

    mutable juce::CriticalSection statusLock;
    std::array<SlotStatus, kNumSlots> slotStatus;
    juce::File currentPresetFile;
    juce::String currentPresetName { "Init" };

    // linked-fader gesture snapshot
    int mixGestureSlot = -1;
    std::array<float, kNumSlots> mixGestureSnapshot {};

    // audio-thread state
    std::vector<ampsurd::Sample> inBuffer, outBuffer, dryBuffer;
    std::vector<float> dryDelay;
    int dryDelayPos = 0;
    double bypassMix = 0.0, bypassCoef = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* levelMatchParam = nullptr;
    juce::AudioProcessorParameter* bypassParamObj = nullptr;
    ampsurd::EngineSettings settings;

    std::atomic<int> totalLatency { 0 };
    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, limiterReductionDb { 0.0f };

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions {}.withThreadName("AMPSURD loader").withNumberOfThreads(1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpsurdProcessor)
};

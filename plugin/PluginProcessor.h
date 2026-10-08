#pragma once

#include <functional>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Effects.h"
#include "ampsurd/Engine.h"
#include "ampsurd/NoiseGate.h"
#include "ampsurd/PitchDetector.h"
#include "ampsurd/SafetyLimiter.h"
#include "player/PlayerRecorder.h"

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
    // EQ "targets" 0..4 are the amps, kGlobalEq is the Global EQ on the complete blend.
    static constexpr int kGlobalEq = kNumSlots;
    static juce::String slotParamId(int slot, const char* name);                  // e.g. "s1_mix", "geq_eqOn"
    static juce::String bandParamId(int slot, int band, const char* name);        // e.g. "s1_b3_gain", "geq_b3_gain"

    // EQ targets kIrEqBase + slot are the IR EQs (one per slot, applied after that slot's IR).
    static constexpr int kIrEqBase = kGlobalEq + 1;
    static constexpr int irEqTarget(int slot) { return kIrEqBase + slot; }

    // --- cabinet IR per slot (message thread) ---
    enum class IrState { none, loading, loaded, missing, error };
    struct IrStatus
    {
        IrState state = IrState::none;
        juce::String path, fileName, info;
    };
    IrStatus getIrStatus(int slot) const;
    void loadIr(int slot, const juce::File& file);
    void removeIr(int slot);
    bool isIrOn(int slot) const { return slotParams[(size_t) slot].irOn->load() > 0.5f; }

    // --- player / recorder: standalone app only (nullptr in a DAW) ---
    PlayerRecorder* getPlayer() { return player.get(); }

    // --- effects after the gate (Global EQ / FX panel) ---
    static juce::StringArray delayNoteNames();
    static double delayNoteBeats(int index);            // length of a note value in quarter notes
    double getTempoBpm() const { return tempoBpm.load(); }   // host tempo, or the TEMPO parameter
    bool hostProvidesTempo() const { return hostTempo.load(); }
    bool isFxActive(int which) const;                   // 0 delay 1, 1 delay 2, 2 reverb, 3 flanger (ON switch)

    // --- Global EQ ---
    bool isGlobalEqOn() const { return globalEqParams.eqOn->load() > 0.5f; }
    std::array<ampsurd::EqBand, ampsurd::ParametricEq::kNumBands> getEqBands(int target) const; // any EQ target

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

    // --- Create Frankenstein (message thread) ---
    void setFrankenstein(bool on);                  // on: sets up a sensible split first if needed
    void setFrankensteinSections(int sections);     // 2..5, redistributes dividers
    ampsurd::FrankensteinLayout getFrankensteinLayout() const; // what is heard right now (mute/solo aware)
    bool isFrankensteinOn() const { return frankOnParam->load() > 0.5f; }
    // live spectrum of the guitar (clean DI, after INPUT), magnitudes in dB for bins of `binHz` each
    void getInputSpectrum(std::vector<float>& magsDb, double& binHz);

    // Alignment diagnostics for the EDIT panel.
    struct AlignInfo
    {
        bool measured = false, isReference = false, reliable = false;
        double autoOffsetMs = 0.0, polarity = 1.0, corrBefore = 0.0, corrAfter = 0.0, loudnessDb = 0.0;
    };
    AlignInfo getAlignInfo(int slot) const;
    void resetAlignment(int slot); // back to AUTO with zero manual offsets

    // --- input calibration (like the official NAM plugin's "Calibrate input") ---
    // Most captures store the input level (dBu) they were recorded at. With calibration on and the
    // interface's input level entered once, each capture is driven exactly as during its recording:
    // input trim = interface level - capture level. A setting of this computer, not of a preset.
    bool isInputCalibrationOn() const { return calibrateOn.load(); }
    double getInterfaceInputDbu() const { return interfaceDbu.load(); }
    void setInputCalibration(bool on, double interfaceLevelDbu); // message thread; saved for all instances
    double getCalibrationTrimDb(int slot) const;                  // 0 when off / capture has no input level
    double getCaptureInputDbu(int slot) const { return captureDbu[(size_t) slot].load(); } // NaN = not stored

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
    // Dropouts since start: audio blocks AMPSURD did not finish in time, plus (standalone) the audio
    // driver's own count of missed buffers. Any rise while playing = an audible click from CPU / driver.
    int getDropoutCount() const
    {
        int n = loadMeasurer.getXRunCount();
        if (deviceXRuns) n += juce::jmax(0, deviceXRuns());
        return n;
    }
    void setDeviceXRunProvider(std::function<int()> f) { deviceXRuns = std::move(f); } // standalone, message thread
    float getAndResetOutputPeak() { return outputPeak.exchange(0.0f); }
    float getAndResetInputPeak() { return inputPeak.exchange(0.0f); }
    float getAndResetLimiterReductionDb() { return limiterReductionDb.exchange(0.0f); }
    float getCompensationDb() const { return engine.getCompensationDb(); }

    // --- gate + tuner ---
    ampsurd::PitchDetector::Result analyseTuner() { return tuner.analyse(); } // message thread
    void setTunerVisible(bool v) { tunerVisible.store(v); }                    // "mute while tuning" only while shown
    float getAndResetGateKeyPeak() { return gateKeyPeak.exchange(0.0f); }
    float getGateGain() const { return gateGain.load(); }
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
        std::atomic<float>* pan = nullptr;      // -100 (L) .. 0 .. +100 (R)
        std::atomic<float>* irOn = nullptr;     // IR ON / BYPASS
        std::array<std::atomic<float>*, kNumBands> freq {}, gain {}, q {};
    };
    std::array<SlotParams, kNumSlots> slotParams;
    struct EqParams
    {
        std::atomic<float>* eqOn = nullptr;
        std::array<std::atomic<float>*, kNumBands> freq {}, gain {}, q {};
    };
    EqParams globalEqParams;
    std::array<EqParams, kNumSlots> irEqParams;

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
    void irJob(int slot, juce::File file, int generation);
    void measureSlotJob(int slot);          // re-measure one slot (IR changed), from its cached render
    std::shared_ptr<const ampsurd::CaptureAnalyzer::Measurement> measureWithIr(int slot, const std::vector<double>& raw, double sr);
    void storeMeasurement(int slot, std::shared_ptr<const ampsurd::CaptureAnalyzer::Measurement> m, double gainDb);
    void setIrStatus(int slot, const IrStatus& s);
    void applyRig(std::shared_ptr<const ampsurd::CaptureAnalyzer::RigAnalysis> rig); // any non-audio thread
    void updateCovariance(bool force);                                                // any non-audio thread
    void renormaliseMixAfterChange(int changedSlot, bool added);
    void setSlotStatus(int slot, const SlotStatus& s);
    void restoreState(const juce::ValueTree& state, const juce::File& presetFile);
    juce::ValueTree createStateTree() const;
    void updateLatency();

    ampsurd::Engine engine;
    ampsurd::SafetyLimiter limiter;
    ampsurd::NoiseGate gate;
    ampsurd::PitchDetector tuner;
    std::vector<float> rawBuffer;
    std::atomic<bool> tunerVisible { false };
    double muteMix = 0.0;
    std::atomic<float> gateKeyPeak { 0.0f }, gateGain { 1.0f };
    std::atomic<float>* gateOnParam = nullptr;
    std::atomic<float>* gateThresholdParam = nullptr;
    std::atomic<float>* gateDecayParam = nullptr;
    std::atomic<float>* tunerMuteParam = nullptr;
    std::atomic<float>* frankOnParam = nullptr;
    std::atomic<float>* frankModeParam = nullptr;
    static constexpr int kSpecRing = 8192;
    std::vector<float> specRing = std::vector<float>((size_t) kSpecRing, 0.0f); // DI for the note band (audio thread writes)
    std::atomic<int> specWrite { 0 };
    std::atomic<float>* frankSectionsParam = nullptr;
    std::atomic<float>* frankWidthParam = nullptr;
    std::array<std::atomic<float>*, 5> frankAmpParam {};
    std::array<std::atomic<float>*, 4> frankDivParam {};
    ampsurd::FrankensteinSettings readFrankenstein() const noexcept;
    ampsurd::FxSettings readFx() const noexcept;
    ampsurd::FxChain fx;
    std::unique_ptr<PlayerRecorder> player;
    struct DelayParams { std::atomic<float>* on, *time, *sync, *note, *feedback, *level, *tone, *pingPong; };
    std::array<DelayParams, 2> delayParams {};
    std::atomic<float>* revOn = nullptr, *revType = nullptr, *revDecay = nullptr, *revPre = nullptr, *revTone = nullptr, *revLevel = nullptr;
    std::atomic<float>* flOn = nullptr, *flRate = nullptr, *flDepth = nullptr, *flFeedback = nullptr, *flMix = nullptr;
    std::atomic<float>* tempoParam = nullptr;
    std::atomic<double> tempoBpm { 120.0 };
    std::atomic<bool> hostTempo { false };

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

    // cabinet IRs: the file's samples (for sample-rate changes) and the prepared IR at the current
    // rate (for measuring) - guarded by configMutex. Cached raw capture renders (analysisMutex) let an
    // IR change re-measure a slot without running the capture again.
    struct IrSource { std::vector<double> samples; double rate = 48000.0; };
    std::array<std::shared_ptr<const IrSource>, kNumSlots> irSources {};
    std::array<std::shared_ptr<const std::vector<double>>, kNumSlots> irPrepared {};
    std::array<std::shared_ptr<const std::vector<double>>, kNumSlots> rawRenders {};
    std::array<std::atomic<int>, kNumSlots> irGeneration {};
    std::array<IrStatus, kNumSlots> irStatus;
    std::array<bool, kNumSlots> lastIrOn {};
    std::atomic<int> remeasureGeneration { 0 };
    std::atomic<bool> needsRemeasure { false };
    double inputGainChangedAt = 0.0;
    float lastSeenInputGainDb = 0.0f;
    std::atomic<bool> calibrateOn { false };
    std::atomic<double> interfaceDbu { 12.0 };
    std::array<std::atomic<double>, kNumSlots> captureDbu {};
    std::atomic<int> calibrationVersion { 0 }, measuredCalibrationVersion { 0 };
    static double trimDbFor(bool on, double interfaceLevel, double captureLevel);

    mutable juce::CriticalSection statusLock;
    std::array<SlotStatus, kNumSlots> slotStatus;
    juce::File currentPresetFile;
    juce::String currentPresetName { "Init" };

    // linked-fader gesture snapshot
    int mixGestureSlot = -1;
    std::array<float, kNumSlots> mixGestureSnapshot {};

    // audio-thread state
    std::vector<ampsurd::Sample> inBuffer, outBuffer, outBufferR, dryBuffer;
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
    std::function<int()> deviceXRuns;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, limiterReductionDb { 0.0f };

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions {}.withThreadName("AMPSURD loader").withNumberOfThreads(1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpsurdProcessor)
};

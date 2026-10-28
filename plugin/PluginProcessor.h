#pragma once

#include <functional>

#include <array>
#include <map>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Effects.h"
#if AMPSURD_DOUBLER
  #include "ampsurd/Doubler.h"
#endif
#if AMPSURD_ROOT
  #include "ampsurd/RootEnhancer.h"
#endif
#include "ampsurd/CabIrModel.h"
#include "ampsurd/Engine.h"
#include "ampsurd/NoiseGate.h"
#include "ampsurd/PitchDetector.h"
#include "ampsurd/SafetyLimiter.h"
#include "ampsurd/PitchShifter.h"
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
    bool acceptsMidi() const override { return true; } // footswitches / controllers (program change, learned CCs)
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
    static constexpr int kNumEqTargets = kIrEqBase + kNumSlots;
    // how many bands an EQ shows (2 = low + high cut only ... 10 = all); hidden bells are flat
    int getEqBandCount(int target) const;
    bool isEqBandVisible(int target, int band) const;
    void setEqBandCount(int target, int count); // message thread

    // --- cabinet IR per slot (message thread) ---
    enum class IrState { none, loading, loaded, missing, error };
    struct IrStatus
    {
        IrState state = IrState::none;
        juce::String path, fileName, info;
    };
    IrStatus getIrStatus(int slot) const;
    void loadIr(int slot, const juce::File& file);
    // AMPSURD IRs (owner's synthetic 4x12 set, calculated - core/CabIrModel): any point of the speaker x mic x
    // position area; quick enough to follow the mouse (older requests are skipped). Presets store the point.
    void loadCabIr(int slot, ampsurd::CabPoint p);
    bool getCabIrPoint(int slot, ampsurd::CabPoint& p) const; // false = no AMPSURD IR in this slot
    void setIrPreview(bool on) { irPreview.store(on); }        // hovering the IR map: listening, not undo steps
    void removeIr(int slot);
    bool isIrOn(int slot) const { return slotParams[(size_t) slot].irOn->load() > 0.5f; }

    // --- player / recorder: standalone app only (nullptr in a DAW) ---
    PlayerRecorder* getPlayer() { return player.get(); }

    // --- effects after the gate (Global EQ / FX panel) ---
    static juce::StringArray delayNoteNames();
    static double delayNoteBeats(int index);            // length of a note value in quarter notes
    double getTempoBpm() const { return tempoBpm.load(); }   // host tempo, or the TEMPO parameter
    bool hostProvidesTempo() const { return hostTempo.load(); }
    // --- ROOT (experimental low-end module; compiled only with AMPSURD_ROOT_MODULE) ---
    static constexpr bool hasRootModule() { return AMPSURD_ROOT != 0; }
   #if AMPSURD_ROOT
    ampsurd::RootSettings readRoot() const noexcept;
    bool isRootOn() const { return rootParams.on->load() > 0.5f; }
    float getAndResetRootLayerPeak() { return root.getAndResetLayerPeak(); }
    float getAndResetRootGainReductionDb() { return root.getAndResetGainReductionDb(); }
   #else
    bool isRootOn() const { return false; }
   #endif
    // --- DOUBLER (experimental; compiled only with AMPSURD_DOUBLER_MODULE) ---
    static constexpr bool hasDoublerModule() { return AMPSURD_DOUBLER != 0; }
   #if AMPSURD_DOUBLER
    ampsurd::DoublerSettings readDoubler() const noexcept;
    bool isDoublerOn() const { return dblParams.on->load() > 0.5f; }
    float getDoublerDelayMs(int voice) const { return doubler.getVoiceDelayMs(voice); }
   #else
    bool isDoublerOn() const { return false; }
   #endif
    bool isFxActive(int which) const;                   // 0 delay 1, 1 delay 2, 2 reverb, 3 flanger, 4 pitch (ON switch)

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
        bool canEco = false;    // the file contains an A2 Lite model (ECO button available)
        bool ecoActive = false; // the Lite model is running
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
    bool isSlotMuted(int slot) const { return slotParams[(size_t) slot].mute->load() > 0.5f; }
    // MUTE moves the amp's fader to 0 % and the others to their real shares; un-muting brings back the
    // amp's previous share (the others are rescaled). Applied for the button and for host automation.
    float getMutedShare(int slot) const { return mutedShare[(size_t) slot]; }

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

    // --- per-amp INPUT / OUTPUT (EDIT panel) ---
    // INPUT drives the capture harder or softer; its loudness in the mix is kept automatically (measured
    // INPUT -> loudness curve per capture). OUTPUT is the deliberate level change of the amp.
    bool isDriveCompensationReady(int slot) const;
    double getDriveCompensationDb(int slot) const { return derived[(size_t) slot].driveCompDb.load(); }

    // --- input calibration (like the official NAM plugin's "Calibrate input") ---
    // Most captures store the input level (dBu) they were recorded at. With calibration on and the
    // interface's input level entered once, each capture is driven exactly as during its recording:
    // input trim = interface level - capture level. A setting of this computer, not of a preset.
    bool isInputCalibrationOn() const { return calibrateOn.load(); }
    double getInterfaceInputDbu() const { return interfaceDbu.load(); }
    void setInputCalibration(bool on, double interfaceLevelDbu); // message thread; saved for all instances
    double getCalibrationTrimDb(int slot) const;                  // 0 when off / capture has no input level
    double getCaptureInputDbu(int slot) const { return captureDbu[(size_t) slot].load(); } // NaN = not stored

    // --- several CPU cores for the amp paths (a setting of this computer) ---
    bool isMultiCoreOn() const { return multiCore.load(); }
    void setMultiCore(bool on); // message thread
    int getNumWorkerThreads() const { return engine.getNumWorkers(); }
    int getActivePaths() const { return engine.getActivePaths(); }

    // --- newly loaded A2 captures start with ECO (their Lite model), as in the TONE3000 app (a setting of
    //     this computer; presets keep their own ECO state) ---
    bool isEcoByDefault() const { return ecoByDefault.load(); }
    void setEcoByDefault(bool on);

    // --- which interface input the guitar is on (a setting of this computer): 0 = input 1, 1 = input 2.
    //     E.g. Focusrite Scarlett Solo: input 1 = XLR (mic), input 2 = instrument jack. Falls back to
    //     input 1 when the host / device gives only one input channel. ---
    int getGuitarInput() const { return guitarInput.load(); }
    void setGuitarInput(int index); // message thread
    bool hasSecondInput() const { return getTotalNumInputChannels() > 1; }
    // Standalone app only: the guitar input as an input of the audio device (any of its inputs, e.g.
    // input 5 of an 8-channel interface) and the app's own Audio / MIDI settings window. Set by the
    // standalone app; empty in a DAW (there the track's routing decides, plus input 1 / 2 above).
    struct StandaloneAudio
    {
        std::function<juce::StringArray()> inputNames;   // the device's input channel names
        std::function<int()> guitarDeviceInput;          // index into inputNames, -1 = unknown
        std::function<void(int)> setGuitarDeviceInput;   // makes that device input the guitar input
        std::function<void()> showAudioSettings;
    };
    void setStandaloneAudio(StandaloneAudio a) { standaloneAudio = std::move(a); }
    const StandaloneAudio* getStandaloneAudio() const { return standaloneAudio.inputNames ? &standaloneAudio : nullptr; }
    // raw (before INPUT gain) peaks of the selected input and of the other one, since the last call:
    // the editor offers "Guitar on input 2?" when only the other input has a signal
    float getAndResetSelectedInputRawPeak() { return selectedInputRawPeak.exchange(0.0f); }
    float getAndResetOtherInputRawPeak() { return otherInputRawPeak.exchange(0.0f); }

    // --- the dragon in the background (a setting of this computer) ---
    bool isDragonShown() const { return showDragon.load(); }
    void setDragonShown(bool on);

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
    // Dropouts: audio blocks AMPSURD did not finish in time, plus (standalone) the audio driver's own count
    // of missed buffers. Any rise while playing = an audible click from CPU / driver. Not counted: the first
    // 3 s after the audio starts and 2 s after an amp was loaded (the driver / the first block of a new
    // capture are often late once - nothing you hear while playing). Message thread (editor timer).
    int getDropoutCount() const
    {
        int n = loadMeasurer.getXRunCount();
        if (deviceXRuns) n += juce::jmax(0, deviceXRuns());
        const int last = dropLastRaw.exchange(n);
        if (juce::Time::getMillisecondCounter() < dropGraceUntil.load() && n > last) dropBaseline += n - last;
        return juce::jmax(0, n - dropBaseline.load());
    }
    void startDropoutGrace(juce::uint32 ms) { dropGraceUntil.store(juce::jmax(dropGraceUntil.load(), juce::Time::getMillisecondCounter() + ms)); }
    void setDeviceXRunProvider(std::function<int()> f) { deviceXRuns = std::move(f); } // standalone, message thread
    mutable std::atomic<int> dropLastRaw { 0 }, dropBaseline { 0 };
    std::atomic<juce::uint32> dropGraceUntil { 0 };
    // MUTE in the master row: the whole output (also the player's backing) fades to silence. Not a parameter:
    // never saved with a preset or a project, so nothing ever loads muted.
    void setOutputMuted(bool m) { outputMuted.store(m); }
    bool isOutputMuted() const { return outputMuted.load(); }
    std::atomic<bool> outputMuted { false };
    float getAndResetOutputPeak() { return outputPeak.exchange(0.0f); }
    float getAndResetOutputPeakL() { return outputPeakL.exchange(0.0f); } // left / right output (meter)
    float getAndResetOutputPeakR() { return outputPeakR.exchange(0.0f); }
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

    // --- EQ clipboard (session only): COPY one EQ, PASTE it into any other (amp, IR, Global EQ) ---
    void copyEq(int target);
    void clearEqClipboard() { eqClip.source = -1; eqClip.pasted = 0; }
    // an unused copy ends by itself (owner 2026-10-28: an accidental COPY was "sticky"); message thread, ~30 Hz
    void expireEqClipboard(double afterMs = 20000.0)
    {
        if (eqClip.source >= 0 && juce::Time::getMillisecondCounterHiRes() - eqClip.copiedAt >= afterMs) clearEqClipboard();
    }
    int getEqClipboardSource() const { return eqClip.source; } // -1 = nothing copied
    bool wasEqPastedInto(int target) const { return target >= 0 && target < 32 && ((eqClip.pasted >> target) & 1u) != 0; }
    bool pasteEq(int target);                                   // EQ on/off, band count, all bands; one undo step;
                                                                // the copy ends with it (one paste per copy, 2026-10-28)

    // --- EQ presets (owner 2026-10-28): any EQ (amp, IR, Global) can load a factory curve (chosen by the owner,
    //     embedded: plugin/assets/eq/*.ampsurdeq) or the user's own, saved under any name in Documents/AMPSURD/EQ.
    //     Loading is one undo step (like PASTE). A preset = EQ on / off, band count, every band. ---
    static juce::File getEqPresetFolder();
    static juce::Array<juce::File> listEqPresets();   // the user's, by name
    static juce::StringArray listFactoryEqPresets();  // names of the embedded ones
    bool saveEqPreset(int target, const juce::File& file) const;
    bool loadEqPreset(int target, const juce::File& file);
    bool loadFactoryEqPreset(int target, const juce::String& name);

    // --- move an amp to another slot by mouse (message thread): the two slots swap everything (capture, IR,
    //     mix share, mute / solo, EQs, alignment, pan, levels, ECO); Frankenstein sections follow the amps ---
    void swapSlots(int a, int b);

    // --- undo / redo, A/B compare (message thread) ---
    // ONE history (owner 2026-10-24: no categories of actions): a settled change of the sound / rig (nothing
    // moved for 0.4 s) is a step, and so is every step of the player (a recording, an edge move, deleting a
    // track, BOUNCE, loading a backing). undo() / redo() (Ctrl+Z / Ctrl+Y, the header arrows) take back / redo
    // whatever was done last. Captures / IRs that stay the same are not reloaded. A and B are two complete
    // rigs of this session (not saved in presets).
    bool canUndo() const { return !undoOrder.empty() || eqClip.source >= 0; } // a pending EQ copy is undone first
    bool canRedo() const { return !redoOrder.empty(); }
    void undo();
    void redo();
    void notePlayerStep(); // the player made a step (message thread)
    int getAbSlot() const { return abCurrent; }       // 0 = A, 1 = B
    void selectAb(int which);
    void copyAbToOther();                             // the current rig becomes the other one too

    // --- MIDI: footswitches / controllers (message thread; settings of this computer) ---
    struct MidiAction { juce::String id, name; bool continuous = false; };
    static const std::vector<MidiAction>& midiActions();
    int getMidiCcFor(const juce::String& actionId) const;     // -1 = not assigned
    void startMidiLearn(const juce::String& actionId);        // the next CC is assigned to this action
    juce::String getMidiLearnAction() const { return midiLearnAction; }
    void cancelMidiLearn() { midiLearnAction.clear(); }
    void clearMidiAssignments();
    bool isProgramChangeOn() const { return midiProgramChange; } // Program Change n -> n-th preset
    void setProgramChangeOn(bool on);
    bool isFootswitchLatching() const { return midiLatching; }   // latching (on / off) vs momentary (press)
    void setFootswitchLatching(bool on);
    juce::String getMidiStatus() const;                        // last MIDI action, shown for 3 s
    void selectPresetByIndex(int index);                       // 0 = first preset in the folder (sorted)
    void stepPreset(int delta);                                // next / previous preset (wraps)

    // --- preset packs (.ampsurdpack = zip: preset + its captures + IRs + licence note) ---
    juce::File getPacksFolder() const;
    bool exportPack(const juce::File& packFile, juce::String& error);
    bool importPack(const juce::File& packFile, juce::String& error); // unpacks into the packs folder, loads the preset

    // The 30 Hz housekeeping (normally run by the message-thread timer): for tests / tools without a message loop.
    void runHousekeeping() { timerCallback(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;

    struct SlotParams
    {
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* eqOn = nullptr;
        std::atomic<float>* eco = nullptr;       // ECO: run the A2 Lite model of slimmable files
        std::atomic<float>* alignMode = nullptr; // 0 AUTO, 1 FREE
        std::atomic<float>* timeMs = nullptr;
        std::atomic<float>* phaseDeg = nullptr;
        std::atomic<float>* pan = nullptr;      // -100 (L) .. 0 .. +100 (R)
        std::atomic<float>* irOn = nullptr;     // IR ON / BYPASS
        std::atomic<float>* drive = nullptr;    // per-amp INPUT (dB): drives this capture harder / softer
        std::atomic<float>* level = nullptr;    // per-amp OUTPUT (dB): deliberate level change in the mix
        std::array<std::atomic<float>*, kNumBands> freq {}, gain {}, q {};
    };
    std::array<SlotParams, kNumSlots> slotParams;
    struct EqParams
    {
        std::atomic<float>* eqOn = nullptr;
        std::array<std::atomic<float>*, kNumBands> freq {}, gain {}, q {};
    };
    EqParams globalEqParams;
    std::array<std::atomic<float>*, kNumEqTargets> bandCountParams {};
    float paramValueRaw(const juce::String& id) const;
    std::array<EqParams, kNumSlots> irEqParams;

    // values derived from the measurements (written by loader/message thread, read by audio thread)
    struct Derived
    {
        std::atomic<double> autoDelayMs { 0.0 };
        std::atomic<double> polarity { 1.0 };
        std::atomic<double> levelGain { 1.0 };
        std::atomic<double> driveCompDb { 0.0 };   // keeps the amp's loudness while its INPUT is turned
        std::atomic<double> measuredDriveDb { 0.0 }; // INPUT of the amp when it was last measured
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
    void requestDriveCurve(int slot);       // (re)compute the INPUT -> loudness curve in the background
    void driveCurveJob(int slot, int generation);
    void updateDriveCompensation();         // message thread (timer)
    double slotOutputGain(int slot, bool levelMatch) const noexcept; // level match * INPUT compensation * OUTPUT
    std::shared_ptr<const ampsurd::CaptureAnalyzer::Measurement> measureWithIr(int slot, const std::vector<double>& raw, double sr);
    void storeMeasurement(int slot, std::shared_ptr<const ampsurd::CaptureAnalyzer::Measurement> m, double gainDb);
    void setIrStatus(int slot, const IrStatus& s);
    void applyRig(std::shared_ptr<const ampsurd::CaptureAnalyzer::RigAnalysis> rig); // any non-audio thread
    void updateCovariance(bool force);                                                // any non-audio thread
    void renormaliseMixAfterChange(int changedSlot, bool added);
    void applyMuteToFaders(int slot, bool muted);   // message thread (timer)
    bool mixesWith(int slot) const;                 // loaded / loading and not muted: takes part in the linked faders
    void setSlotStatus(int slot, const SlotStatus& s);
    void restoreState(const juce::ValueTree& state, const juce::File& presetFile);
    juce::File resolveFileRef(const juce::String& ref, const juce::File& presetFile) const;
    static juce::String fileNameOfRef(const juce::String& ref);
    juce::ValueTree createStateTree() const;
    void updateLatency();

    ampsurd::Engine engine;
    ampsurd::SafetyLimiter limiter;
    ampsurd::NoiseGate gate;
    ampsurd::PitchShifter pitch;
    std::atomic<float>* pitchOnParam = nullptr, *pitchSemiParam = nullptr, *pitchCentsParam = nullptr;
    ampsurd::PitchDetector tuner;
    std::vector<float> rawBuffer;
    std::atomic<bool> tunerVisible { false };
    double muteMix = 0.0;
    double outMuteMix = 0.0; // MUTE (master row): whole output to silence, 20 ms fade
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
   #if AMPSURD_ROOT
    // ROOT (experimental, docs/ROOT_MODULE.md): low layer under the amps, before the gate
    ampsurd::RootEnhancer root;
    struct RootParams { std::atomic<float>* on = nullptr, *mode = nullptr, *level = nullptr, *freq = nullptr, *growl = nullptr,
                        *tight = nullptr, *comp = nullptr, *sub = nullptr, *low = nullptr, *lowMid = nullptr, *invert = nullptr; } rootParams;
   #endif
   #if AMPSURD_DOUBLER
    // DOUBLER (experimental, docs/DOUBLER.md): after the gate, before the effects
    ampsurd::Doubler doubler;
    struct DblParams { std::atomic<float>* on = nullptr, *voices = nullptr, *tight = nullptr, *level = nullptr, *width = nullptr,
                       *lowMono = nullptr, *character = nullptr; } dblParams;
   #endif
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

    // INPUT -> loudness curves (analysisMutex); computed on their own low-priority thread
    struct DriveCurveState
    {
        bool valid = false;
        ampsurd::CaptureAnalyzer::DriveCurve curve;
        double baseDb = 0.0;    // INPUT + calibration it was measured at
        bool eco = false, ir = false;
        const void* irData = nullptr;
        juce::String path;
    };
    std::array<DriveCurveState, kNumSlots> driveCurves {};
    std::array<std::atomic<int>, kNumSlots> curveGeneration {};
    std::array<float, kNumSlots> lastSeenDrive {};
    std::array<double, kNumSlots> driveChangedAt {};
    std::array<float, kNumSlots> queuedDrive {};

    // cabinet IRs: the file's samples (for sample-rate changes) and the prepared IR at the current
    // rate (for measuring) - guarded by configMutex. Cached raw capture renders (analysisMutex) let an
    // IR change re-measure a slot without running the capture again.
    struct IrSource { std::vector<double> samples; double rate = 48000.0; };
    std::atomic<bool> irPreview { false };
    std::array<std::shared_ptr<const IrSource>, kNumSlots> irSources {};
    bool applyEqPreset(int target, const juce::XmlElement& xml);
    void adoptIr(int slot, std::shared_ptr<IrSource> src, std::vector<double> prepared, const IrStatus& status); // configMutex held
    std::array<std::shared_ptr<const std::vector<double>>, kNumSlots> irPrepared {};
    std::array<std::shared_ptr<const std::vector<double>>, kNumSlots> rawRenders {};
    std::array<std::atomic<int>, kNumSlots> irGeneration {};
    std::array<IrStatus, kNumSlots> irStatus;
    std::array<bool, kNumSlots> lastIrOn {};
    std::array<bool, kNumSlots> lastEco {};
    std::atomic<int> remeasureGeneration { 0 };
    std::atomic<bool> needsRemeasure { false };
    double inputGainChangedAt = 0.0;
    float lastSeenInputGainDb = 0.0f;
    std::atomic<bool> calibrateOn { false };
    std::atomic<bool> multiCore { true };
    std::atomic<bool> showDragon { true };
    std::atomic<bool> ecoByDefault { false }; // Full A2 by default since 2026-10-28
    std::atomic<int> guitarInput { 0 };
    std::atomic<bool> needsRecovery { false };
    std::atomic<int> recoveries { 0 };

public:
    int getRecoveryCount() const { return recoveries.load(); } // tests / diagnostics
    void requestRecoveryForTest() { needsRecovery.store(true); } // tests: as if NaN appeared inside

private:
    StandaloneAudio standaloneAudio;
    std::atomic<float> selectedInputRawPeak { 0.0f }, otherInputRawPeak { 0.0f };
    std::atomic<double> interfaceDbu { 12.0 };
    std::array<std::atomic<double>, kNumSlots> captureDbu {};
    std::atomic<int> calibrationVersion { 0 }, measuredCalibrationVersion { 0 };
    static double trimDbFor(bool on, double interfaceLevel, double captureLevel);

    mutable juce::CriticalSection statusLock;
    std::array<SlotStatus, kNumSlots> slotStatus;
    juce::File currentPresetFile;
    juce::String currentPresetName { "Init" };

    struct EqClip
    {
        int source = -1;
        unsigned pasted = 0; // EQs this copy was pasted into (their button shows COPY again)
        double copiedAt = 0.0;
        float eqOn = 1.0f, bands = 10.0f;
        std::array<float, kNumBands> freq {}, gain {}, q {};
    } eqClip;

    // undo / A-B
    std::vector<juce::ValueTree> undoStack, redoStack;
    std::vector<char> undoOrder, redoOrder; // 'S' = a sound / rig step, 'P' = a player step (newest last)
    std::atomic<bool> resetUndo { false }; // the host loaded a state: start the history there
    juce::ValueTree committedState;
    juce::uint64 committedHash = 0, pendingHash = 0;
    double pendingSince = 0.0;
    juce::uint64 stateHash() const;
    void trackUndo(double nowMs);
    void applySnapshot(const juce::ValueTree& t);
    std::array<juce::ValueTree, 2> abStates;
    int abCurrent = 0;

    // MIDI (the audio thread queues, the timer acts)
    struct MidiEvent { int type = 0, number = 0, value = 0; }; // type 0 = CC, 1 = program change
    static constexpr int kMidiFifo = 256;
    std::array<MidiEvent, kMidiFifo> midiEvents {};
    juce::AbstractFifo midiFifo { kMidiFifo };
    std::map<int, juce::String> midiMap; // CC number -> action id
    juce::String midiLearnAction;
    bool midiProgramChange = true, midiLatching = false;
    std::array<int, 128> lastCcValue {};
    juce::String midiStatus;
    double midiStatusAt = 0.0;
    void handleMidiEvents();
    void applyMidiAction(const juce::String& id, int cc, int value);
    void loadMidiSettings();
    void saveMidiSettings() const;

    // MUTE -> fader 0: the share the amp had before it was muted (saved in presets)
    std::array<float, kNumSlots> mutedShare {};
    std::array<bool, kNumSlots> lastMute {};
    std::atomic<bool> resyncMute { true }; // after a preset / state load: take the mute states as they are

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
    std::atomic<float> outputPeakL { 0.0f }, outputPeakR { 0.0f };

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions {}.withThreadName("AMPSURD loader").withNumberOfThreads(1) };
    juce::ThreadPool curvePool { juce::ThreadPoolOptions {}.withThreadName("AMPSURD level curves").withNumberOfThreads(1)
                                     .withDesiredThreadPriority(juce::Thread::Priority::background) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpsurdProcessor)
};

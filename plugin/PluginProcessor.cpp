#include "PluginProcessor.h"

#include "ampsurd/CabIrModel.h"
#include "AmpProfileFile.h"

#include <limits>

#include "ampsurd/Convolver.h"
#include "ampsurd/ThreadCpuClock.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using ampsurd::CaptureAnalyzer;
using ampsurd::CaptureModel;

namespace
{
const juce::Identifier kSlotsId { "SLOTS" }, kSlotId { "SLOT" }, kIndexId { "index" }, kPathId { "path" },
    kPresetNameId { "presetName" }, kFormatId { "ampsurdFormat" };
constexpr int kFormatVersion = 1;
constexpr double kPi = 3.14159265358979323846;

std::filesystem::path toStdPath(const juce::File& f)
{
#if JUCE_WINDOWS
    return std::filesystem::path(f.getFullPathName().toWideCharPointer()); // keeps non-ASCII names intact
#else
    return std::filesystem::path(f.getFullPathName().toStdString());
#endif
}

juce::String describe(const CaptureModel& m, const juce::File& f)
{
    const auto& i = m.getInfo();
    juce::String s;
    s << f.getFileName() << "\n";
    if (m.isMorph())
    {
        s << i.architectureHint << "\n" << f.getParentDirectory().getFullPathName();
        return s;
    }
    s << i.architectureHint << " - file format " << i.fileVersion << "\n";
    s << "Model " << juce::String(i.modelSampleRate, 0) << " Hz";
    if (std::abs(i.modelSampleRate - m.getPreparedSampleRate()) > 0.5)
        s << " (resampled from " << juce::String(m.getPreparedSampleRate(), 0) << " Hz)";
    if (i.hasInputLevel) s << "\nRecorded at input level " << juce::String(i.inputLevelDbu, 2) << " dBu";
    if (i.slimmable) s << (i.ecoActive ? "\nECO: the A2 Lite model is running (less CPU)" : "\nContains an A2 Lite model: ECO available");
    s << "\n" << f.getParentDirectory().getFullPathName();
    return s;
}

void setParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}
} // namespace

// ---------------------------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------------------------
static juce::String eqPrefix(int target)
{
    if (target >= AmpsurdProcessor::kIrEqBase)
        return "s" + juce::String(target - AmpsurdProcessor::kIrEqBase + 1) + "_ir";
    return target == AmpsurdProcessor::kGlobalEq ? juce::String("geq") : "s" + juce::String(target + 1);
}

juce::String AmpsurdProcessor::slotParamId(int slot, const char* name)
{
    return eqPrefix(slot) + "_" + name;
}

juce::String AmpsurdProcessor::bandParamId(int slot, int band, const char* name)
{
    return eqPrefix(slot) + "_b" + juce::String(band + 1) + "_" + name;
}

std::array<ampsurd::EqBand, ampsurd::ParametricEq::kNumBands> AmpsurdProcessor::getEqBands(int target) const
{
    std::array<ampsurd::EqBand, kNumBands> b {};
    const EqParams* e = target == kGlobalEq ? &globalEqParams : target >= kIrEqBase ? &irEqParams[(size_t) (target - kIrEqBase)] : nullptr;
    const auto& freq = e ? e->freq : slotParams[(size_t) target].freq;
    const auto& gain = e ? e->gain : slotParams[(size_t) target].gain;
    const auto& q = e ? e->q : slotParams[(size_t) target].q;
    for (int i = 0; i < kNumBands; ++i)
        b[(size_t) i] = { freq[(size_t) i]->load(), gain[(size_t) i]->load(), q[(size_t) i]->load() };
    // bells beyond the chosen band count are hidden and flat (low / high cut always stay)
    const int count = getEqBandCount(target);
    for (int i = count - 1; i < kNumBands - 1; ++i)
        b[(size_t) i].gainDb = 0.0f;
    return b;
}

int AmpsurdProcessor::getEqBandCount(int target) const
{
    if (target < 0 || target >= kNumEqTargets || bandCountParams[(size_t) target] == nullptr) return kNumBands;
    return juce::jlimit(2, (int) kNumBands, juce::roundToInt(bandCountParams[(size_t) target]->load()));
}

bool AmpsurdProcessor::isEqBandVisible(int target, int band) const
{
    return band == 0 || band == kNumBands - 1 || band <= getEqBandCount(target) - 2;
}

void AmpsurdProcessor::setEqBandCount(int target, int count)
{
    count = juce::jlimit(2, (int) kNumBands, count);
    if (count == getEqBandCount(target)) return;
    setParam(params, slotParamId(target, "bands"), (float) count);
    // hidden bells: flat. Visible bells nobody has used yet: spread evenly over the guitar range,
    // so that e.g. 3 bells sit at low / mid / high instead of 63 / 125 / 250 Hz.
    const int bells = count - 2;
    for (int i = 1; i < kNumBands - 1; ++i)
    {
        if (i > bells) { setParam(params, bandParamId(target, i, "gain"), 0.0f); continue; }
        if (paramValueRaw(bandParamId(target, i, "gain")) != 0.0f) continue;
        const double f = bells == 8 ? (double) ampsurd::ParametricEq::defaultBands()[(size_t) i].freqHz
                                    : 80.0 * std::pow(8000.0 / 80.0, (i - 0.5) / bells);
        setParam(params, bandParamId(target, i, "freq"), (float) f);
    }
}

float AmpsurdProcessor::paramValueRaw(const juce::String& id) const
{
    auto* v = params.getRawParameterValue(id);
    return v != nullptr ? v->load() : 0.0f;
}

juce::StringArray AmpsurdProcessor::delayNoteNames()
{
    return { "1/1", "1/2", "1/2 dotted", "1/2 triplet", "1/4", "1/4 dotted", "1/4 triplet",
             "1/8", "1/8 dotted", "1/8 triplet", "1/16", "1/16 dotted", "1/16 triplet" };
}

double AmpsurdProcessor::delayNoteBeats(int i)
{
    static constexpr double beats[] = { 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0 };
    return beats[juce::jlimit(0, 12, i)];
}

namespace
{
// parameter text with units; typed values may include the unit ("756 ms", "7.5k")
juce::AudioParameterFloatAttributes unitAttr(const juce::String& unit, int decimals)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel(unit)
        .withStringFromValueFunction([unit, decimals](float v, int) { return juce::String(v, decimals) + " " + unit; })
        .withValueFromStringFunction([](const juce::String& t) {
            auto x = t.trim().toLowerCase();
            const bool k = x.containsChar('k') && !x.contains("ms");
            return (float) (x.getDoubleValue() * (k ? 1000.0 : 1.0));
        });
}
} // namespace

// The ten EQ bands (same ranges for every amp and for the Global EQ).
static void addEqBandParams(juce::AudioProcessorParameterGroup& group, int target, const juce::String& namePrefix)
{
    using namespace juce;
    const auto defaults = ampsurd::ParametricEq::defaultBands();
    // how many of the ten bands this EQ shows: 2 = low + high cut only, 10 = all (default)
    group.addChild(std::make_unique<AudioParameterInt>(ParameterID { AmpsurdProcessor::slotParamId(target, "bands"), 1 },
                                                       namePrefix + "EQ Bands", 2, ampsurd::ParametricEq::kNumBands,
                                                       ampsurd::ParametricEq::kNumBands));
    for (int b = 0; b < ampsurd::ParametricEq::kNumBands; ++b)
    {
        const auto type = ampsurd::ParametricEq::bandType(b);
        const String bn = namePrefix + (type == ampsurd::ParametricEq::BandType::lowCut ? String("Low Cut ")
                                        : type == ampsurd::ParametricEq::BandType::highCut ? String("High Cut ")
                                                                                              : "EQ" + String(b + 1) + " ");
        NormalisableRange<float> fr(20.0f, 20000.0f, 0.1f);
        fr.setSkewForCentre(632.0f);
        NormalisableRange<float> qr(0.3f, 10.0f, 0.001f);
        qr.setSkewForCentre(1.7f);
        group.addChild(std::make_unique<AudioParameterFloat>(ParameterID { AmpsurdProcessor::bandParamId(target, b, "freq"), 1 }, bn + "Freq",
                                                             fr, defaults[(size_t) b].freqHz,
                                                             AudioParameterFloatAttributes().withLabel("Hz")));
        group.addChild(std::make_unique<AudioParameterFloat>(ParameterID { AmpsurdProcessor::bandParamId(target, b, "gain"), 1 }, bn + "Gain",
                                                             NormalisableRange<float>(-18.0f, 18.0f, 0.01f), 0.0f,
                                                             AudioParameterFloatAttributes().withLabel("dB")));
        group.addChild(std::make_unique<AudioParameterFloat>(ParameterID { AmpsurdProcessor::bandParamId(target, b, "q"), 1 }, bn + "Q",
                                                             qr, defaults[(size_t) b].q));
    }
}

// AMPSURD amp GAIN on the amp's clock: 0 = 7:00, 5 = 12:00, 10 = 17:00 (24 h, like the recordings' names), 5-minute steps
static juce::String gainClockText(float knob)
{
    int minutes = juce::roundToInt(juce::jlimit(0.0f, 10.0f, knob) * 60.0f / 5.0f) * 5 + 7 * 60;
    return juce::String(minutes / 60) + ":" + juce::String(minutes % 60).paddedLeft('0', 2);
}

static float gainFromClockText(const juce::String& raw)
{
    const auto t = raw.trim();
    if (!t.containsChar(':')) return juce::jlimit(0.0f, 10.0f, t.getFloatValue()); // a plain 0..10 number
    int h = t.upToFirstOccurrenceOf(":", false, false).getIntValue();
    const int m = t.fromFirstOccurrenceOf(":", false, false).getIntValue();
    if (h < 7) h += 12; // 3:00 = 15:00 (a knob never points to 3 am)
    return juce::jlimit(0.0f, 10.0f, (float) (h - 7) + (float) m / 60.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout AmpsurdProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto db = AudioParameterFloatAttributes().withLabel("dB");

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "input", 1 }, "Input",
                                                     NormalisableRange<float>(-30.0f, 15.0f, 0.1f), 0.0f, db));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "output", 1 }, "Output",
                                                     NormalisableRange<float>(-40.0f, 12.0f, 0.1f), 0.0f, db));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "levelMatch", 1 }, "Level match", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "gateOn", 1 }, "Gate", true));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "gateThreshold", 1 }, "Gate Threshold",
                                                     NormalisableRange<float>(-96.0f, -20.0f, 0.1f),
                                                     ampsurd::NoiseGate::kDefaultThresholdDb, db));
    {
        NormalisableRange<float> dr(5.0f, 2000.0f, 1.0f);
        dr.setSkewForCentre(150.0f);
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "gateDecay", 1 }, "Gate Decay", dr,
                                                         ampsurd::NoiseGate::kDefaultDecayMs,
                                                         AudioParameterFloatAttributes().withLabel("ms")));
    }
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "tunerMute", 1 }, "Mute While Tuning", false));

    // Create Frankenstein: frequency-split blending
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "frankOn", 1 }, "Frankenstein", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "twoPlayers", 1 }, "Two Players", false));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "frankMode", 1 }, "Frankenstein Split",
                                                      StringArray { "Tone (after the amps)", "Notes (before the amps)" }, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { "frankSections", 1 }, "Frankenstein Sections", 2, 5, 2));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "frankWidth", 1 }, "Frankenstein Width",
                                                     NormalisableRange<float>(0.0f, 90.0f, 0.1f), 30.0f,
                                                     AudioParameterFloatAttributes().withLabel("%")));
    for (int k = 0; k < 5; ++k)
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "frankAmp" + String(k + 1), 1 },
                                                          "Frankenstein Section " + String(k + 1) + " Amp",
                                                          StringArray { "Amp 1", "Amp 2", "Amp 3", "Amp 4", "Amp 5" }, k));
    static constexpr float divDefaults[4] = { 150.0f, 600.0f, 2000.0f, 5000.0f };
    for (int k = 0; k < 4; ++k)
    {
        NormalisableRange<float> r(30.0f, 16000.0f, 0.1f);
        r.setSkewForCentre(700.0f);
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "frankDiv" + String(k + 1), 1 },
                                                         "Frankenstein Divider " + String(k + 1), r, divDefaults[k],
                                                         AudioParameterFloatAttributes().withLabel("Hz")));
    }

    for (int s = 0; s < kNumSlots; ++s)
    {
        const String n = "Amp " + String(s + 1) + " ";
        auto group = std::make_unique<AudioProcessorParameterGroup>("slot" + String(s + 1), "Amp " + String(s + 1), " | ");
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, "mix"), 1 }, n + "Mix",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 20.0f,
                                                              AudioParameterFloatAttributes().withLabel("%")));
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(s, "mute"), 1 }, n + "Mute", false));
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(s, "solo"), 1 }, n + "Solo", false));
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(s, "eqOn"), 1 }, n + "EQ On", true));
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(s, "eco"), 1 }, n + "ECO (A2 Lite)", false));
        group->addChild(std::make_unique<AudioParameterChoice>(ParameterID { slotParamId(s, "align"), 1 }, n + "Alignment",
                                                               StringArray { "AUTO", "FREE" }, 0));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, "time"), 1 }, n + "Time",
                                                              NormalisableRange<float>(-(float) kMaxManualMs, (float) kMaxManualMs, 0.001f), 0.0f,
                                                              AudioParameterFloatAttributes().withLabel("ms")));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, "phase"), 1 }, n + "Phase",
                                                              NormalisableRange<float>(-180.0f, 180.0f, 0.1f), 0.0f,
                                                              AudioParameterFloatAttributes().withLabel("deg")));
        group->addChild(std::make_unique<AudioParameterFloat>(
            ParameterID { slotParamId(s, "pan"), 1 }, n + "Pan", NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                const int i = juce::roundToInt(v);
                return i == 0 ? juce::String("C") : (i < 0 ? "L" : "R") + juce::String(std::abs(i));
            })));
        // per-amp INPUT (drive, loudness kept automatically) and OUTPUT (level in the mix), EDIT panel
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, "drive"), 1 }, n + "Input",
                                                              NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, unitAttr("dB", 1)));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, "level"), 1 }, n + "Output",
                                                              NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, unitAttr("dB", 1)));
        // AMPSURD amp knobs (.ampsurdamp, owner 2026-10-30): GAIN morphs between the gain captures, BASS / MID / TREBLE
        // are the measured tone controls; 0..10 like the amp's own scale (7 o'clock = 0, noon = 5)
        for (const char* k : { "ampGain", "ampBass", "ampMid", "ampTreble" })
        {
            const String label = juce::String(k).substring(3);
            // GAIN reads as the amp knob's clock position (owner 2026-11-01: "9:00" .. "15:00", as the recordings are named)
            auto attr = juce::String(k) == "ampGain"
                            ? AudioParameterFloatAttributes()
                                  .withStringFromValueFunction([](float v, int) { return gainClockText(v); })
                                  .withValueFromStringFunction([](const String& t) { return gainFromClockText(t); })
                            : AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(v, 1); });
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, k), 1 }, n + "Amp " + label,
                                                                  NormalisableRange<float>(0.0f, 10.0f, 0.01f), 5.0f, attr));
        }
        // pedal captures before the amp (serial chain, owner 2026-10-28): on / bypass and output level each
        for (int k = 0; k < kNumPedals; ++k)
        {
            const String pk = "ped" + String(k + 1);
            group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(s, (pk + "On").toRawUTF8()), 1 },
                                                                 n + "Pedal " + String(k + 1) + " On", true));
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, (pk + "Level").toRawUTF8()), 1 },
                                                                  n + "Pedal " + String(k + 1) + " Level",
                                                                  NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, unitAttr("dB", 1)));
        }
        // TWO PLAYERS (owner 2026-11-12): whose amp - off = player 1, on = player 2
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(s, "player2"), 1 }, n + "Player 2", false));
        // this amp's own noise gate (owner 2026-11-12), in addition to the global gate; off by default
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(s, "gateOn"), 1 }, n + "Gate", false));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, "gateThreshold"), 1 }, n + "Gate Threshold",
                                                              NormalisableRange<float>(-96.0f, -20.0f, 0.1f),
                                                              ampsurd::NoiseGate::kDefaultThresholdDb, db));
        {
            NormalisableRange<float> dr(5.0f, 2000.0f, 1.0f);
            dr.setSkewForCentre(150.0f);
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { slotParamId(s, "gateDecay"), 1 }, n + "Gate Decay", dr,
                                                                  ampsurd::NoiseGate::kDefaultDecayMs,
                                                                  AudioParameterFloatAttributes().withLabel("ms")));
        }
        addEqBandParams(*group, s, n);
        // cabinet IR of this slot and its own EQ (same ten bands)
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(s, "irOn"), 1 }, n + "IR On", true));
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(irEqTarget(s), "eqOn"), 1 }, n + "IR EQ On", true));
        addEqBandParams(*group, irEqTarget(s), n + "IR ");
        layout.add(std::move(group));
    }

    // Global EQ: same ten bands, on the complete blend (after Frankenstein, before the limiter)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup>("globalEq", "Global EQ", " | ");
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { slotParamId(kGlobalEq, "eqOn"), 1 }, "Global EQ On", false));
        addEqBandParams(*group, kGlobalEq, "Global ");
        layout.add(std::move(group));
    }

    // Effects after the gate: two delays, reverb, flanger (+ tempo for SYNC without a host tempo)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup>("fx", "Effects", " | ");
        auto pct = [](float v, int) { return juce::String(juce::roundToInt(v)) + " %"; };
        auto pctAttr = AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(pct);
        for (int d = 0; d < 2; ++d)
        {
            const String id = "fxD" + String(d + 1), nm = "Delay " + String(d + 1) + " ";
            group->addChild(std::make_unique<AudioParameterBool>(ParameterID { id + "On", 1 }, nm + "On", false));
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { id + "Time", 1 }, nm + "Time",
                                                                  NormalisableRange<float>(1.0f, 2000.0f, 0.1f, 0.5f), d == 0 ? 500.0f : 750.0f, unitAttr("ms", 1)));
            group->addChild(std::make_unique<AudioParameterBool>(ParameterID { id + "Sync", 1 }, nm + "Sync", false));
            group->addChild(std::make_unique<AudioParameterChoice>(ParameterID { id + "Note", 1 }, nm + "Note", delayNoteNames(), d == 0 ? 4 : 5));
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { id + "Feedback", 1 }, nm + "Feedback",
                                                                  NormalisableRange<float>(0.0f, 95.0f, 0.1f), 30.0f, pctAttr));
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { id + "Level", 1 }, nm + "Level",
                                                                  NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f, pctAttr));
            NormalisableRange<float> tr(500.0f, 20000.0f, 1.0f);
            tr.setSkewForCentre(4000.0f);
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { id + "Tone", 1 }, nm + "Tone", tr, 6000.0f, unitAttr("Hz", 0)));
            group->addChild(std::make_unique<AudioParameterBool>(ParameterID { id + "PingPong", 1 }, nm + "Ping-Pong", false));
        }
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "tempo", 1 }, "Tempo",
                                                              NormalisableRange<float>(40.0f, 240.0f, 0.1f), 120.0f, unitAttr("BPM", 1)));
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { "fxRevOn", 1 }, "Reverb On", false));
        group->addChild(std::make_unique<AudioParameterChoice>(ParameterID { "fxRevType", 1 }, "Reverb Type",
                                                               StringArray { "Room", "Hall", "Plate", "Cathedral", "Ambience" }, 1));
        {
            NormalisableRange<float> dr(0.2f, 12.0f, 0.01f);
            dr.setSkewForCentre(2.0f);
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "fxRevDecay", 1 }, "Reverb Decay", dr, 2.2f, unitAttr("s", 2)));
        }
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "fxRevPreDelay", 1 }, "Reverb Pre-Delay",
                                                              NormalisableRange<float>(0.0f, 250.0f, 0.1f), 20.0f, unitAttr("ms", 1)));
        {
            NormalisableRange<float> tr(500.0f, 20000.0f, 1.0f);
            tr.setSkewForCentre(4000.0f);
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "fxRevTone", 1 }, "Reverb Tone", tr, 7000.0f, unitAttr("Hz", 0)));
        }
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "fxRevLevel", 1 }, "Reverb Level",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f, pctAttr));
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { "fxFlOn", 1 }, "Flanger On", false));
        {
            NormalisableRange<float> rr(0.05f, 5.0f, 0.01f);
            rr.setSkewForCentre(0.5f);
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "fxFlRate", 1 }, "Flanger Rate", rr, 0.25f, unitAttr("Hz", 2)));
        }
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "fxFlDepth", 1 }, "Flanger Depth",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f, pctAttr));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "fxFlFeedback", 1 }, "Flanger Feedback",
                                                              NormalisableRange<float>(-90.0f, 90.0f, 0.1f), 50.0f, pctAttr));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "fxFlMix", 1 }, "Flanger Mix",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, pctAttr));
        // PITCH: tunes the whole guitar (before the amps)
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { "pitchOn", 1 }, "Pitch On", false));
        group->addChild(std::make_unique<AudioParameterInt>(ParameterID { "pitchSemi", 1 }, "Pitch Semitones", -12, 12, 0,
                                                            AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) {
                                                                return (v > 0 ? "+" : "") + String(v) + " st";
                                                            })));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "pitchCents", 1 }, "Pitch Cents",
                                                              NormalisableRange<float>(-50.0f, 50.0f, 1.0f), 0.0f, unitAttr("cents", 0)));
        layout.add(std::move(group));
    }
   #if AMPSURD_ROOT
    // ROOT (experimental): low layer that makes the lowest note of a power chord deeper (docs/ROOT_MODULE.md)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup>("root", "Root", " | ");
        auto pct = [](float v, int) { return juce::String(juce::roundToInt(v)) + " %"; };
        auto pctAttr = AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(pct);
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { "rootOn", 1 }, "Root On", false));
        group->addChild(std::make_unique<AudioParameterChoice>(ParameterID { "rootMode", 1 }, "Root Mode",
                                                               StringArray { "Parallel", "Post", "Sub" }, 0));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "rootLevel", 1 }, "Root Level",
                                                              NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, unitAttr("dB", 1)));
        {
            NormalisableRange<float> fr(60.0f, 250.0f, 1.0f);
            fr.setSkewForCentre(120.0f);
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "rootFreq", 1 }, "Root Freq", fr, 120.0f, unitAttr("Hz", 0)));
        }
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "rootGrowl", 1 }, "Root Growl",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 40.0f, pctAttr));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "rootTight", 1 }, "Root Tight",
                                                              NormalisableRange<float>(20.0f, 100.0f, 1.0f), 35.0f, unitAttr("Hz", 0)));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "rootComp", 1 }, "Root Comp",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, pctAttr));
        for (auto [id, nm] : { std::pair<const char*, const char*> { "rootSub", "Root EQ Sub 50 Hz" }, { "rootLow", "Root EQ Low 100 Hz" },
                               { "rootLowMid", "Root EQ Low-Mid 250 Hz" } })
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, nm, NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
                                                                  unitAttr("dB", 1)));
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { "rootInvert", 1 }, "Root Polarity Invert", false));
        layout.add(std::move(group));
    }
   #endif
   #if AMPSURD_DOUBLER
    // DOUBLER (experimental): 1-3 "second takes" of the guitar from one input (docs/DOUBLER.md)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup>("doubler", "Doubler", " | ");
        auto pct = [](float v, int) { return juce::String(juce::roundToInt(v)) + " %"; };
        auto pctAttr = AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(pct);
        group->addChild(std::make_unique<AudioParameterBool>(ParameterID { "dblOn", 1 }, "Doubler On", false));
        group->addChild(std::make_unique<AudioParameterChoice>(ParameterID { "dblVoices", 1 }, "Doubler Doubles",
                                                               StringArray { "1", "2", "3" }, 1));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "dblTight", 1 }, "Doubler Tight",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 60.0f, pctAttr));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "dblLevel", 1 }, "Doubler Level",
                                                              NormalisableRange<float>(-18.0f, 6.0f, 0.1f), -2.0f, unitAttr("dB", 1)));
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "dblWidth", 1 }, "Doubler Width",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, pctAttr));
        {
            NormalisableRange<float> fr(0.0f, 300.0f, 1.0f);
            auto hz = [](float v, int) { return v < 20.0f ? juce::String("OFF") : juce::String(juce::roundToInt(v)) + " Hz"; };
            group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "dblLowMono", 1 }, "Doubler Low Mono", fr, 120.0f,
                                                                  AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(hz)));
        }
        group->addChild(std::make_unique<AudioParameterFloat>(ParameterID { "dblCharacter", 1 }, "Doubler Character",
                                                              NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, pctAttr));
        layout.add(std::move(group));
    }
   #endif
    return layout;
}

// ---------------------------------------------------------------------------------------------
AmpsurdProcessor::AmpsurdProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "AMPSURD", createLayout())
{
    inputParam = params.getRawParameterValue("input");
    outputParam = params.getRawParameterValue("output");
    bypassParam = params.getRawParameterValue("bypass");
    levelMatchParam = params.getRawParameterValue("levelMatch");
    for (auto& d : captureDbu) d.store(std::numeric_limits<double>::quiet_NaN());
    for (auto& a : pedalDbu)
        for (auto& d : a) d.store(std::numeric_limits<double>::quiet_NaN());
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "AMPSURD"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        juce::PropertiesFile pf(o);
        calibrateOn.store(pf.getBoolValue("calibrateInput", false));
        interfaceDbu.store(pf.getDoubleValue("interfaceInputDbu", 12.0));
       #if JUCE_MAC
        // macOS: one core by default. Extra worker threads are not yet in the host's audio workgroup,
        // so Apple Silicon may run them on its efficiency cores; one fast performance core is safer.
        multiCore.store(pf.getBoolValue("multiCore", false));
       #else
        multiCore.store(pf.getBoolValue("multiCore", true));
       #endif
        showDragon.store(pf.getBoolValue("showDragon", true));
        ecoByDefault.store(pf.getBoolValue("ecoByDefault", false)); // Full A2 by default (owner 2026-10-28; a saved choice is kept)
        guitarInput.store(juce::jlimit(0, 1, pf.getIntValue("guitarInput", 0)));
    }
    engine.setMultiCore(multiCore.load() ? -1 : 0);
    loadMidiSettings();
    bypassParamObj = params.getParameter("bypass");
    gateOnParam = params.getRawParameterValue("gateOn");
    gateThresholdParam = params.getRawParameterValue("gateThreshold");
    gateDecayParam = params.getRawParameterValue("gateDecay");
    tunerMuteParam = params.getRawParameterValue("tunerMute");
    frankOnParam = params.getRawParameterValue("frankOn");
    twoPlayersParam = params.getRawParameterValue("twoPlayers");
    frankModeParam = params.getRawParameterValue("frankMode");
    frankSectionsParam = params.getRawParameterValue("frankSections");
    frankWidthParam = params.getRawParameterValue("frankWidth");
    for (int k = 0; k < 5; ++k) frankAmpParam[(size_t) k] = params.getRawParameterValue("frankAmp" + juce::String(k + 1));
    for (int k = 0; k < 4; ++k) frankDivParam[(size_t) k] = params.getRawParameterValue("frankDiv" + juce::String(k + 1));

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& sp = slotParams[(size_t) s];
        sp.mix = params.getRawParameterValue(slotParamId(s, "mix"));
        sp.mute = params.getRawParameterValue(slotParamId(s, "mute"));
        sp.solo = params.getRawParameterValue(slotParamId(s, "solo"));
        sp.eqOn = params.getRawParameterValue(slotParamId(s, "eqOn"));
        sp.alignMode = params.getRawParameterValue(slotParamId(s, "align"));
        sp.timeMs = params.getRawParameterValue(slotParamId(s, "time"));
        sp.phaseDeg = params.getRawParameterValue(slotParamId(s, "phase"));
        sp.pan = params.getRawParameterValue(slotParamId(s, "pan"));
        sp.irOn = params.getRawParameterValue(slotParamId(s, "irOn"));
        sp.eco = params.getRawParameterValue(slotParamId(s, "eco"));
        sp.drive = params.getRawParameterValue(slotParamId(s, "drive"));
        sp.level = params.getRawParameterValue(slotParamId(s, "level"));
        sp.ampGain = params.getRawParameterValue(slotParamId(s, "ampGain"));
        sp.gateOn = params.getRawParameterValue(slotParamId(s, "gateOn"));
        sp.player2 = params.getRawParameterValue(slotParamId(s, "player2"));
        sp.gateThreshold = params.getRawParameterValue(slotParamId(s, "gateThreshold"));
        sp.gateDecay = params.getRawParameterValue(slotParamId(s, "gateDecay"));
        sp.ampTone[0] = params.getRawParameterValue(slotParamId(s, "ampBass"));
        sp.ampTone[1] = params.getRawParameterValue(slotParamId(s, "ampMid"));
        sp.ampTone[2] = params.getRawParameterValue(slotParamId(s, "ampTreble"));
        for (int k = 0; k < kNumPedals; ++k)
        {
            sp.pedalOn[(size_t) k] = params.getRawParameterValue(slotParamId(s, ("ped" + juce::String(k + 1) + "On").toRawUTF8()));
            sp.pedalLevel[(size_t) k] = params.getRawParameterValue(slotParamId(s, ("ped" + juce::String(k + 1) + "Level").toRawUTF8()));
        }
        auto& ie = irEqParams[(size_t) s];
        ie.eqOn = params.getRawParameterValue(slotParamId(irEqTarget(s), "eqOn"));
        for (int b = 0; b < kNumBands; ++b)
        {
            ie.freq[(size_t) b] = params.getRawParameterValue(bandParamId(irEqTarget(s), b, "freq"));
            ie.gain[(size_t) b] = params.getRawParameterValue(bandParamId(irEqTarget(s), b, "gain"));
            ie.q[(size_t) b] = params.getRawParameterValue(bandParamId(irEqTarget(s), b, "q"));
        }
        for (int b = 0; b < kNumBands; ++b)
        {
            sp.freq[(size_t) b] = params.getRawParameterValue(bandParamId(s, b, "freq"));
            sp.gain[(size_t) b] = params.getRawParameterValue(bandParamId(s, b, "gain"));
            sp.q[(size_t) b] = params.getRawParameterValue(bandParamId(s, b, "q"));
        }
    }

    for (int d = 0; d < 2; ++d)
    {
        const juce::String id = "fxD" + juce::String(d + 1);
        auto& dp = delayParams[(size_t) d];
        dp.on = params.getRawParameterValue(id + "On");
        dp.time = params.getRawParameterValue(id + "Time");
        dp.sync = params.getRawParameterValue(id + "Sync");
        dp.note = params.getRawParameterValue(id + "Note");
        dp.feedback = params.getRawParameterValue(id + "Feedback");
        dp.level = params.getRawParameterValue(id + "Level");
        dp.tone = params.getRawParameterValue(id + "Tone");
        dp.pingPong = params.getRawParameterValue(id + "PingPong");
    }
    tempoParam = params.getRawParameterValue("tempo");
    revOn = params.getRawParameterValue("fxRevOn");
    revType = params.getRawParameterValue("fxRevType");
    revDecay = params.getRawParameterValue("fxRevDecay");
    revPre = params.getRawParameterValue("fxRevPreDelay");
    revTone = params.getRawParameterValue("fxRevTone");
    revLevel = params.getRawParameterValue("fxRevLevel");
    flOn = params.getRawParameterValue("fxFlOn");
    pitchOnParam = params.getRawParameterValue("pitchOn");
    pitchSemiParam = params.getRawParameterValue("pitchSemi");
    pitchCentsParam = params.getRawParameterValue("pitchCents");
    flRate = params.getRawParameterValue("fxFlRate");
    flDepth = params.getRawParameterValue("fxFlDepth");
    flFeedback = params.getRawParameterValue("fxFlFeedback");
    flMix = params.getRawParameterValue("fxFlMix");
   #if AMPSURD_ROOT
    rootParams.on = params.getRawParameterValue("rootOn");
    rootParams.mode = params.getRawParameterValue("rootMode");
    rootParams.level = params.getRawParameterValue("rootLevel");
    rootParams.freq = params.getRawParameterValue("rootFreq");
    rootParams.growl = params.getRawParameterValue("rootGrowl");
    rootParams.tight = params.getRawParameterValue("rootTight");
    rootParams.comp = params.getRawParameterValue("rootComp");
    rootParams.sub = params.getRawParameterValue("rootSub");
    rootParams.low = params.getRawParameterValue("rootLow");
    rootParams.lowMid = params.getRawParameterValue("rootLowMid");
    rootParams.invert = params.getRawParameterValue("rootInvert");
   #endif
   #if AMPSURD_DOUBLER
    dblParams.on = params.getRawParameterValue("dblOn");
    dblParams.voices = params.getRawParameterValue("dblVoices");
    dblParams.tight = params.getRawParameterValue("dblTight");
    dblParams.level = params.getRawParameterValue("dblLevel");
    dblParams.width = params.getRawParameterValue("dblWidth");
    dblParams.lowMono = params.getRawParameterValue("dblLowMono");
    dblParams.character = params.getRawParameterValue("dblCharacter");
   #endif

    globalEqParams.eqOn = params.getRawParameterValue(slotParamId(kGlobalEq, "eqOn"));
    for (int t = 0; t < kNumEqTargets; ++t)
        bandCountParams[(size_t) t] = params.getRawParameterValue(slotParamId(t, "bands"));
    for (int b = 0; b < kNumBands; ++b)
    {
        globalEqParams.freq[(size_t) b] = params.getRawParameterValue(bandParamId(kGlobalEq, b, "freq"));
        globalEqParams.gain[(size_t) b] = params.getRawParameterValue(bandParamId(kGlobalEq, b, "gain"));
        globalEqParams.q[(size_t) b] = params.getRawParameterValue(bandParamId(kGlobalEq, b, "q"));
    }

    // The player / recorder exists only in the standalone app (a DAW has its own tracks).
    if (wrapperType == wrapperType_Standalone || juce::SystemStats::getEnvironmentVariable("AMPSURD_FORCE_PLAYER", "0") == "1")
    {
        player = std::make_unique<PlayerRecorder>();
        // one history for the sound and the player: a recording, an edge move, a BOUNCE... is an undo step too
        player->onNewStep = [this] { notePlayerStep(); };
    }

    prepareToPlay(48000.0, 512); // valid state before the host calls prepareToPlay
    startTimerHz(30);
}

AmpsurdProcessor::~AmpsurdProcessor()
{
    if (player) player->onNewStep = nullptr; // no steps reported into a processor that is going away
    stopTimer();
    alive->store(false);
    curvePool.removeAllJobs(true, 20000);
    loaderPool.removeAllJobs(true, 20000);
    for (int s = 0; s < kNumSlots; ++s)
    {
        engine.getSlot(s).collectGarbage();
        for (int k = 0; k < kNumPedals; ++k) engine.getPedal(s, k).collectGarbage();
    }
}

bool AmpsurdProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto ok = [](const juce::AudioChannelSet& s) {
        return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
    };
    return ok(layouts.getMainInputChannelSet()) && ok(layouts.getMainOutputChannelSet());
}

void AmpsurdProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startDropoutGrace(3000); // starting audio: the first blocks / the driver are often late once
    const int maxBlock = juce::jmax(1, samplesPerBlock);
    const bool rateChanged = std::abs(sampleRate - currentSampleRate.load()) > 0.5;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        currentSampleRate.store(sampleRate);
        currentMaxBlock = maxBlock;
        engine.prepare(sampleRate, maxBlock, kReserveMs + CaptureAnalyzer::kMaxAutoLagMs + kMaxManualMs + 1.0);
        if (rateChanged)
            for (int sl = 0; sl < kNumSlots; ++sl)
                if (const auto src = irSources[(size_t) sl])
                {
                    auto prep = ampsurd::prepareImpulseResponse(src->samples, src->rate, sampleRate);
                    irPrepared[(size_t) sl] = std::make_shared<const std::vector<double>>(prep.samples);
                    engine.getIrSlot(sl).replaceNow(std::make_unique<ampsurd::Convolver>(std::move(prep.samples)));
                }
    }
    inBuffer.assign((size_t) maxBlock, 0.0);
    outBuffer.assign((size_t) maxBlock, 0.0);
    outBufferR.assign((size_t) maxBlock, 0.0);
    dryBuffer.assign((size_t) maxBlock, 0.0);
    rawBuffer.assign((size_t) maxBlock, 0.0f);
    input2Buffer.assign((size_t) maxBlock, 0.0f);
    rawBuffer2.assign((size_t) maxBlock, 0.0f);
    for (auto* b : { &inBuffer2, &outBuffer2, &outBuffer2R, &dryBuffer2, &gainBuffer }) b->assign((size_t) maxBlock, 0.0);
    gate.prepare(sampleRate);
    gate2.prepare(sampleRate);
    pitch.prepare(sampleRate);
    pitch2.prepare(sampleRate);
    tuner.prepare(sampleRate);
    fx.prepare(sampleRate, maxBlock);
    fx2.prepare(sampleRate, maxBlock);
    limiter2.prepare(sampleRate);
   #if AMPSURD_ROOT
    root.prepare(sampleRate, maxBlock);
   #endif
   #if AMPSURD_DOUBLER
    doubler.prepare(sampleRate, maxBlock);
   #endif
    if (player) player->prepare(sampleRate, maxBlock);
    dryDelay.assign((size_t) (sampleRate * 0.1) + 1, 0.0f); // up to 100 ms of latency
    dryDelay2.assign(dryDelay.size(), 0.0f);
    dryDelayPos = 0;
    inputGain.reset(sampleRate, 0.02);
    outputGain.reset(sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inputParam->load()));
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));
    bypassCoef = 1.0 - std::exp(-1.0 / (0.015 * sampleRate));
    bypassMix = bypassParam->load() > 0.5f ? 1.0 : 0.0;
    limiter.prepare(sampleRate);
    loadMeasurer.reset(sampleRate, maxBlock);

    bool anyMeasured = false;
    {
        std::lock_guard<std::mutex> lock(analysisMutex);
        for (auto& m : measurements) anyMeasured = anyMeasured || m != nullptr;
    }
    if (rateChanged && anyMeasured)
        needsRemeasure.store(true); // alignment is measured in samples at the old rate

    totalLatency.store(-1);
    updateLatency();
    const auto setup = juce::String(sampleRate / 1000.0, sampleRate == std::round(sampleRate / 1000.0) * 1000.0 ? 0 : 1) + " kHz, "
                     + juce::String(maxBlock) + " samples (" + juce::String(1000.0 * maxBlock / sampleRate, 1) + " ms)";
    if (setup != lastAudioSetup)
    {
        if (startLogged) eventLog.add("AUDIO", "audio set to " + setup);
        lastAudioSetup = setup;
    }
}

// ---------------------------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------------------------
AmpsurdProcessor::EffectiveAlign AmpsurdProcessor::effectiveAlign(int s, double sampleRate) const noexcept
{
    const auto& sp = slotParams[(size_t) s];
    const auto& d = derived[(size_t) s];
    const bool free = sp.alignMode->load() > 0.5f;
    const double ms = kReserveMs + d.autoDelayMs.load(std::memory_order_relaxed) + (free ? (double) sp.timeMs->load() : 0.0);
    return { std::max(0.0, ms * 0.001 * sampleRate), d.polarity.load(std::memory_order_relaxed),
             free ? (double) sp.phaseDeg->load() * kPi / 180.0 : 0.0 };
}

void AmpsurdProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // footswitches / controllers: queued for the message thread (preset changes, learned CCs)
    for (const auto meta : midiMessages)
    {
        const auto m = meta.getMessage();
        MidiEvent e;
        if (m.isController()) e = { 0, m.getControllerNumber(), m.getControllerValue() };
        else if (m.isProgramChange()) e = { 1, m.getProgramChangeNumber(), 0 };
        else continue;
        int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
        midiFifo.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 > 0) midiEvents[(size_t) s1] = e;
        midiFifo.finishedWrite(n1);
    }
    midiMessages.clear();

    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    juce::AudioProcessLoadMeasurer::ScopedTimer cpuTimer(loadMeasurer, numSamples);
    // the log: a block that took longer than it lasts = a dropout (lock-free record, the text is made later)
    struct DropWatch
    {
        AmpsurdProcessor& p;
        const int n;
        const double start = juce::Time::getMillisecondCounterHiRes();
        const ampsurd::ThreadCpuClock::Mark cpuStart = ampsurd::ThreadCpuClock::now();
        const double waitStart = p.engine.helperWaitMsTotal();
        ~DropWatch()
        {
            const double sr = p.currentSampleRate.load();
            if (n <= 0 || sr <= 0.0) return;
            const double work = juce::Time::getMillisecondCounterHiRes() - start, budget = 1000.0 * n / sr;
            if (work > (double) p.recentWorkMs.load(std::memory_order_relaxed)) p.recentWorkMs.store((float) work, std::memory_order_relaxed);
            if (work <= budget) return;
            ampsurd::EventLog::Drop d;
            d.atMs = start;
            d.workMs = (float) work;
            d.budgetMs = (float) budget;
            d.ranMs = (float) ampsurd::ThreadCpuClock::ranMs(cpuStart, ampsurd::ThreadCpuClock::now(), work);
            d.helperWaitMs = (float) (p.engine.helperWaitMsTotal() - waitStart);
            d.samples = n;
            const auto& bt = p.blockTimes;
            for (int s = 0; s < 5 && s < ampsurd::kNumSlots; ++s)
                for (int k = 0; k < 4; ++k) d.slotMs[s][k] = bt.ran[(size_t) s] ? bt.path[(size_t) s][(size_t) k] : -1.0f;
            d.pathsMs = bt.paths;
            d.mixMs = bt.mix;
            d.inputMs = p.partMs[0];
            d.effectsMs = p.partMs[1];
            d.outputMs = p.partMs[2];
            d.grace = juce::Time::getMillisecondCounter() < p.dropGraceUntil.load();
            p.eventLog.pushDrop(d);
        }
    } dropWatch { *this, numSamples };
    blockTimes = {};
    partMs = {};
    auto clockMs = [] { return juce::Time::getMillisecondCounterHiRes(); };

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (numSamples == 0 || numOut == 0)
        return;

    const double sr = currentSampleRate.load(std::memory_order_relaxed);
    inputGain.setTargetValue(juce::Decibels::decibelsToGain(inputParam->load()));
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));
    const double bypassTarget = bypassParam->load() > 0.5f ? 1.0 : 0.0;
    const bool levelMatch = levelMatchParam->load() > 0.5f;
    gate.setParameters(gateOnParam->load() > 0.5f, gateThresholdParam->load(), gateDecayParam->load());
    pitch.setParameters(pitchOnParam->load() > 0.5f, pitchSemiParam->load(), pitchCentsParam->load());
    gate2.setParameters(gateOnParam->load() > 0.5f, gateThresholdParam->load(), gateDecayParam->load());
    pitch2.setParameters(pitchOnParam->load() > 0.5f, pitchSemiParam->load(), pitchCentsParam->load());
    const double muteTarget = (tunerMuteParam->load() > 0.5f && tunerVisible.load(std::memory_order_relaxed)) ? 1.0 : 0.0;
    float keyPeak = 0.0f;

    // Engine settings from parameters (lock-free reads) + measured values.
    bool rotation = false;
    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto& sp = slotParams[(size_t) s];
        auto& ss = settings.slots[(size_t) s];
        ss.mix = sp.mix->load();
        ss.mute = sp.mute->load() > 0.5f;
        ss.solo = sp.solo->load() > 0.5f;
        ss.eqEnabled = sp.eqOn->load() > 0.5f;
        ss.eq = getEqBands(s);
        ss.pan = sp.pan->load() / 100.0f;
        ss.irEnabled = sp.irOn->load() > 0.5f;
        ss.irEqEnabled = irEqParams[(size_t) s].eqOn->load() > 0.5f;
        ss.irEq = getEqBands(irEqTarget(s));
        const auto a = effectiveAlign(s, sr);
        ss.delaySamples = a.delaySamples;
        ss.polarity = a.polarity;
        ss.phaseRadians = a.phaseRadians;
        ss.levelGain = slotOutputGain(s, levelMatch) * ampFadeGain(s, sp.ampGain->load());
        ss.inputTrim = std::pow(10.0, (getCalibrationTrimDb(s) + (double) sp.drive->load()) / 20.0);
        ss.ampGain = sp.ampGain->load(); // only an AMPSURD amp uses it
        ss.gateOn = sp.gateOn->load() > 0.5f;
        ss.gateThresholdDb = sp.gateThreshold->load();
        ss.gateDecayMs = sp.gateDecay->load();
        ss.player = sp.player2->load() > 0.5f ? 1 : 0;
        for (int k = 0; k < kNumPedals; ++k)
        {
            ss.pedalOn[(size_t) k] = sp.pedalOn[(size_t) k]->load() > 0.5f;
            ss.pedalLevel[(size_t) k] = std::pow(10.0, (double) sp.pedalLevel[(size_t) k]->load() / 20.0);
        }
        rotation = rotation || (engine.isSlotLoaded(s) && std::abs(a.phaseRadians) > 1e-4);
    }
    settings.rotationActive = rotation;
    settings.frankenstein = readFrankenstein();

    // tempo for delay SYNC: the host's, or the TEMPO parameter (standalone / no host tempo)
    {
        double bpm = tempoParam->load();
        bool fromHost = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0) { bpm = *b; fromHost = true; }
        tempoBpm.store(juce::jlimit(20.0, 400.0, bpm), std::memory_order_relaxed);
        hostTempo.store(fromHost, std::memory_order_relaxed);
    }
    const auto fxSettings = readFx();
   #if AMPSURD_ROOT
    const auto rootSettings = readRoot();
   #endif
   #if AMPSURD_DOUBLER
    const auto dblSettings = readDoubler();
   #endif
    settings.globalEqEnabled = isGlobalEqOn();
    settings.globalEq = getEqBands(kGlobalEq);

    // the guitar's input channel (setting); the other input is only measured, for the editor's hint
    const int inCh = (guitarInput.load(std::memory_order_relaxed) == 1 && numIn > 1) ? 1 : 0;
    const float* in = numIn > 0 ? buffer.getReadPointer(inCh) : nullptr;
    if (numIn > 0)
    {
        const float sel = buffer.getMagnitude(inCh, 0, numSamples);
        const float other = numIn > 1 ? buffer.getMagnitude(1 - inCh, 0, numSamples) : 0.0f;
        if (sel > selectedInputRawPeak.load(std::memory_order_relaxed)) selectedInputRawPeak.store(sel, std::memory_order_relaxed);
        if (other > otherInputRawPeak.load(std::memory_order_relaxed)) otherInputRawPeak.store(other, std::memory_order_relaxed);
    }
    // TWO PLAYERS: player 2 = the other input (standalone: the input chosen as INPUT 2; in a DAW: the second input channel)
    const bool two = twoPlayersParam->load() > 0.5f && numIn > 1 && (player == nullptr || player->isInput2Available());
    settings.twoPlayers = two;
    twoPlayersActive.store(two, std::memory_order_relaxed);
    if (player) player->setTwoPlayers(two);
    const float* in2p = two ? buffer.getReadPointer(1 - inCh) : nullptr;
    if (!two) tunerOnPlayer2.store(false, std::memory_order_relaxed);
    float* out = buffer.getWritePointer(0);
    float* outR = numOut > 1 ? buffer.getWritePointer(1) : nullptr; // stereo: per-amp PAN
    const int maxBlock = (int) inBuffer.size();
    const int latency = juce::jlimit(0, (int) dryDelay.size() - 1, totalLatency.load(std::memory_order_relaxed));
    float inPeak = 0.0f, outPeak = 0.0f, outPeakL = 0.0f, outPeakR = 0.0f;

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin(maxBlock, numSamples - start);
        const double tIn = clockMs();
        for (int i = 0; i < n; ++i)
        {
            // invalid input (NaN / inf from a misbehaving driver or host) would poison every filter and
            // amp state for good: silence it; absurd levels are clipped to +36 dBFS
            float raw = in != nullptr ? in[start + i] : 0.0f;
            raw = std::isfinite(raw) ? juce::jlimit(-64.0f, 64.0f, raw) : 0.0f;
            rawBuffer[(size_t) i] = raw;
            // dry signal delayed by the plugin latency, for click-free, time-aligned bypass
            dryDelay[(size_t) dryDelayPos] = raw;
            int rp = dryDelayPos - latency;
            if (rp < 0) rp += (int) dryDelay.size();
            dryBuffer[(size_t) i] = dryDelay[(size_t) rp];
            dryDelayPos = (dryDelayPos + 1) % (int) dryDelay.size();

            const double ig = inputGain.getNextValue();
            gainBuffer[(size_t) i] = ig;
            const float x = raw * (float) ig;
            inPeak = juce::jmax(inPeak, std::abs(x));
            inBuffer[(size_t) i] = x;
        }
        if (two)
        {
            // player 2: the same INPUT, the same delayed dry signal for BYPASS
            float pk2 = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                float raw = in2p[start + i];
                raw = std::isfinite(raw) ? juce::jlimit(-64.0f, 64.0f, raw) : 0.0f;
                rawBuffer2[(size_t) i] = raw;
                const int wp = (dryDelayPos - n + i + (int) dryDelay2.size()) % (int) dryDelay2.size(); // the same positions as player 1's
                dryDelay2[(size_t) wp] = raw;
                int rp = wp - latency;
                if (rp < 0) rp += (int) dryDelay2.size();
                dryBuffer2[(size_t) i] = dryDelay2[(size_t) rp];
                const double x = raw * gainBuffer[(size_t) i];
                pk2 = juce::jmax(pk2, (float) std::abs(x));
                inBuffer2[(size_t) i] = x;
            }
            if (pk2 > player2InputPeak.load(std::memory_order_relaxed)) player2InputPeak.store(pk2, std::memory_order_relaxed);
            // the tuner follows the player who plays (the other one louder for a moment: switch)
            float p1 = 0.0f, p2 = 0.0f;
            for (int i = 0; i < n; ++i) { p1 = juce::jmax(p1, std::abs(rawBuffer[(size_t) i])); p2 = juce::jmax(p2, std::abs(rawBuffer2[(size_t) i])); }
            const double fall = std::exp(-(double) n / (0.4 * sr));
            tunerEnv1 = juce::jmax((double) p1, tunerEnv1 * fall);
            tunerEnv2 = juce::jmax((double) p2, tunerEnv2 * fall);
            const bool on2 = tunerOnPlayer2.load(std::memory_order_relaxed);
            if (!on2 && tunerEnv2 > 0.01 && tunerEnv2 > 3.0 * tunerEnv1) tunerOnPlayer2.store(true, std::memory_order_relaxed);
            if (on2 && tunerEnv1 > 0.01 && tunerEnv1 > 3.0 * tunerEnv2) tunerOnPlayer2.store(false, std::memory_order_relaxed);
        }
        // tuner works on the clean DI, also when bypassed (the real strings); TWO PLAYERS: the player who plays
        tuner.push(two && tunerOnPlayer2.load(std::memory_order_relaxed) ? rawBuffer2.data() : rawBuffer.data(), n);
        pitch.process(inBuffer.data(), n); // PITCH: the whole guitar re-tuned before the amps
        if (two) pitch2.process(inBuffer2.data(), n);
        {
            int w = specWrite.load(std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                specRing[(size_t) w] = (float) inBuffer[(size_t) i];
                w = (w + 1) & (kSpecRing - 1);
            }
            specWrite.store(w, std::memory_order_release);
        }
        keyPeak = juce::jmax(keyPeak, inPeak);

        double* L = outBuffer.data();
        double* R = outBufferR.data();
        const double tEngine = clockMs();
        partMs[0] += (float) (tEngine - tIn);
        double* L2 = outBuffer2.data();
        double* R2 = outBuffer2R.data();
        engine.process(inBuffer.data(), L, R, n, settings, two ? inBuffer2.data() : nullptr, two ? L2 : nullptr, two ? R2 : nullptr);
        {
            // the log: add this part of the block to the block's breakdown (a host block can be split in several)
            const auto& t = engine.lastTimes();
            for (int s = 0; s < ampsurd::kNumSlots; ++s)
            {
                blockTimes.ran[(size_t) s] = blockTimes.ran[(size_t) s] || t.ran[(size_t) s];
                for (int k = 0; k < ampsurd::Engine::BlockTimes::kParts; ++k) blockTimes.path[(size_t) s][(size_t) k] += t.path[(size_t) s][(size_t) k];
            }
            blockTimes.paths += t.paths;
            blockTimes.mix += t.mix;
        }
        const double tFx = clockMs();
       #if AMPSURD_ROOT
        root.process(inBuffer.data(), L, R, n, rootSettings); // ROOT layer: under the amps, silenced by the gate
       #endif
        gate.process(inBuffer.data(), L, n, R); // NS-2 style: listen to the DI, silence after the amps
       #if AMPSURD_DOUBLER
        doubler.process(L, R, n, dblSettings); // DOUBLER: after the gate (clean silences), before delays / reverb
       #endif
        fx.process(L, R, n, fxSettings);           // flanger, delays, reverb: after the gate, so tails ring out
        if (two)
        {
            // player 2: the same gate and effects (ROOT / DOUBLER: player 1 only)
            gate2.process(inBuffer2.data(), L2, n, R2);
            fx2.process(L2, R2, n, fxSettings);
        }
        const double tOut = clockMs();
        partMs[1] += (float) (tOut - tFx);

        for (int i = 0; i < n; ++i)
        {
            const double og = outputGain.getNextValue();
            L[i] *= og;
            R[i] *= og;
            if (two) { L2[i] *= og; R2[i] *= og; }
        }
        {
            // last line of defence: if anything inside produced NaN / inf, the state is reset from the
            // message thread (timerCallback) - the sound comes back instead of staying silent
            bool finite = true;
            for (int i = 0; i < n && finite; ++i) finite = std::isfinite(L[i]) && std::isfinite(R[i]);
            for (int i = 0; two && i < n && finite; ++i) finite = std::isfinite(L2[i]) && std::isfinite(R2[i]);
            if (!finite) needsRecovery.store(true, std::memory_order_relaxed);
        }
        limiter.process(L, R, n); // never above -1 dBFS, stereo-linked
        if (two) limiter2.process(L2, R2, n);

        for (int i = 0; i < n; ++i)
        {
            bypassMix += (bypassTarget - bypassMix) * bypassCoef;
            if (std::abs(bypassTarget - bypassMix) < 1e-6) bypassMix = bypassTarget;
            muteMix += (muteTarget - muteMix) * bypassCoef;
            if (std::abs(muteTarget - muteMix) < 1e-6) muteMix = muteTarget;
            const double dry = bypassMix * dryBuffer[(size_t) i];
            L[i] = ((1.0 - bypassMix) * L[i] + dry) * (1.0 - muteMix);
            R[i] = ((1.0 - bypassMix) * R[i] + dry) * (1.0 - muteMix);
            if (two)
            {
                const double dry2 = bypassMix * dryBuffer2[(size_t) i];
                L2[i] = ((1.0 - bypassMix) * L2[i] + dry2) * (1.0 - muteMix);
                R2[i] = ((1.0 - bypassMix) * R2[i] + dry2) * (1.0 - muteMix);
            }
        }
        // standalone: record the guitar (and INPUT 2), add backing track + takes, limit the sum
        if (player)
        {
            // INPUT 2 of the player = the interface's other input (a mic, a keyboard...), as it comes in
            const float* in2 = nullptr;
            if (!two && numIn > 1 && player->isInput2Available())
            {
                const float* src = buffer.getReadPointer(1 - inCh) + start;
                for (int i = 0; i < n; ++i)
                    input2Buffer[(size_t) i] = std::isfinite(src[i]) ? juce::jlimit(-64.0f, 64.0f, src[i]) : 0.0f;
                in2 = input2Buffer.data();
            }
            player->process(L, R, n, latency, isNonRealtime(), in2, two ? L2 : nullptr, two ? R2 : nullptr);
        }
        else if (two)
            for (int i = 0; i < n; ++i) // in a DAW: both players in the one output
            {
                L[i] += L2[i];
                R[i] += R2[i];
            }
        {
            // MUTE: everything (amps, effects, player) to silence
            const double target = outputMuted.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            if (target != 0.0 || outMuteMix != 0.0)
                for (int i = 0; i < n; ++i)
                {
                    // linear 20 ms fade: exactly silent when muted
                    const double step = 1.0 / (0.020 * currentSampleRate.load(std::memory_order_relaxed));
                    outMuteMix = target > outMuteMix ? juce::jmin(target, outMuteMix + step) : juce::jmax(target, outMuteMix - step);
                    L[i] *= 1.0 - outMuteMix;
                    R[i] *= 1.0 - outMuteMix;
                }
        }
        for (int i = 0; i < n; ++i)
        {
            double y = L[i];
            const double yR = R[i];
            if (outR != nullptr)
                outR[start + i] = (float) yR;
            else
                y = 0.5 * (y + yR); // mono output bus: fold down (centred amps unchanged)
            out[start + i] = (float) y;
            outPeak = juce::jmax(outPeak, (float) std::abs(y), (float) std::abs(yR));
            outPeakL = juce::jmax(outPeakL, (float) std::abs(outR != nullptr ? L[i] : y));
            outPeakR = juce::jmax(outPeakR, (float) std::abs(outR != nullptr ? yR : y));
        }
        partMs[2] += (float) (clockMs() - tOut);
    }

    for (int ch = 2; ch < numOut; ++ch)
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);

    if (inPeak > inputPeak.load(std::memory_order_relaxed)) inputPeak.store(inPeak, std::memory_order_relaxed);
    if (keyPeak > gateKeyPeak.load(std::memory_order_relaxed)) gateKeyPeak.store(keyPeak, std::memory_order_relaxed);
    gateGain.store((float) gate.getCurrentGain(), std::memory_order_relaxed);
    if (outPeak > outputPeak.load(std::memory_order_relaxed)) outputPeak.store(outPeak, std::memory_order_relaxed);
    if (outPeakL > outputPeakL.load(std::memory_order_relaxed)) outputPeakL.store(outPeakL, std::memory_order_relaxed);
    if (outPeakR > outputPeakR.load(std::memory_order_relaxed)) outputPeakR.store(outPeakR, std::memory_order_relaxed);
    const float gr = (float) limiter.getAndResetMaxReductionDb();
    if (gr > limiterReductionDb.load(std::memory_order_relaxed)) limiterReductionDb.store(gr, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------------
// Loading, measuring, alignment (non-audio threads)
// ---------------------------------------------------------------------------------------------
void AmpsurdProcessor::setSlotStatus(int slot, const SlotStatus& s)
{
    bool problem = false;
    {
        const juce::ScopedLock sl(statusLock);
        const auto& old = slotStatus[(size_t) slot];
        problem = (s.state == SlotState::missing || s.state == SlotState::error) && (old.state != s.state || old.path != s.path);
        slotStatus[(size_t) slot] = s;
    }
    if (problem) eventLog.add("PROBLEM", "amp " + juce::String(slot + 1) + ": " + (s.info.isNotEmpty() ? s.info : s.path));
}

AmpsurdProcessor::SlotStatus AmpsurdProcessor::getSlotStatus(int slot) const
{
    const juce::ScopedLock sl(statusLock);
    return slotStatus[(size_t) slot];
}

void AmpsurdProcessor::loadCapture(int slot, const juce::File& file)
{
    const auto previous = getSlotStatus(slot).state;
    const bool wasEmpty = previous == SlotState::empty || previous == SlotState::missing || previous == SlotState::error;
    // a capture loaded by hand starts as the setting says (ECO only matters for files with a Lite model)
    if (file != juce::File(getSlotStatus(slot).path))
    {
        setParam(params, slotParamId(slot, "eco"), ecoByDefault.load() ? 1.0f : 0.0f);
        lastEco[(size_t) slot] = ecoByDefault.load();
    }
    const int gen = ++slotGeneration[(size_t) slot];
    setSlotStatus(slot, { SlotState::loading, file.getFullPathName(), file.getFileNameWithoutExtension(), "Loading..." });
    if (wasEmpty)
    {
        if (isSlotMuted(slot)) // a new capture in an empty slot plays (a leftover MUTE would hide it)
        {
            setParam(params, slotParamId(slot, "mute"), 0.0f);
            lastMute[(size_t) slot] = false;
        }
        mutedShare[(size_t) slot] = 0.0f;
        renormaliseMixAfterChange(slot, true); // inaudible: the slot does not play until it is loaded
    }

    auto aliveFlag = alive;
    loaderPool.addJob([this, slot, file, gen, aliveFlag] {
        if (aliveFlag->load())
            loadJob(slot, file, gen, true);
    });
}

void AmpsurdProcessor::loadJob(int slot, juce::File file, int gen, bool /*adjustMix*/)
{
    ampsurd::EventLog::Scope busy(eventLog, "loading " + file.getFileName() + " into amp " + juce::String(slot + 1));
    std::lock_guard<std::mutex> lock(configMutex);
    if (gen != slotGeneration[(size_t) slot].load())
        return; // superseded

    const double sr = currentSampleRate.load();
    const bool eco = slotParams[(size_t) slot].eco->load() > 0.5f;
    std::shared_ptr<const ampsurd::ToneStack> tone;
    auto result = loadSlotModel(slot, file, sr, currentMaxBlock, eco, &tone);
    if (!result.model)
    {
        setSlotStatus(slot, { SlotState::error, file.getFullPathName(), file.getFileNameWithoutExtension(),
                              "Could not load this file:\n" + juce::String(result.error) });
        auto aliveFlag = alive;
        juce::MessageManager::callAsync([this, slot, aliveFlag] {
            if (aliveFlag->load()) renormaliseMixAfterChange(slot, false);
        });
        return;
    }

    // Measure BEFORE the capture is heard, so alignment and level match are ready when it fades in.
    // (Driven exactly as it will be played: INPUT + input calibration.)
    const double gainDb = inputParam->load();
    const auto& mi = result.model->getInfo();
    const double newDbu = mi.hasInputLevel ? mi.inputLevelDbu : std::numeric_limits<double>::quiet_NaN();
    auto test = CaptureAnalyzer::makeTestSignal(sr);
    const double driveDb = slotParams[(size_t) slot].drive->load();
    const double g = std::pow(10.0, (gainDb + trimDbFor(calibrateOn.load(), interfaceDbu.load(), effectiveDbu(slot, newDbu)) + driveDb) / 20.0);
    for (double& v : test) v *= g;
    test = renderPedals(slot, std::move(test), sr, currentMaxBlock); // pedal captures before the amp, if any
    auto raw = std::make_shared<const std::vector<double>>(CaptureAnalyzer::render(*result.model, test, false));
    const auto meas = measureWithIr(slot, *raw, sr); // with the slot's cabinet IR, if any
    result.model->prepare(sr, currentMaxBlock); // clear the state left by the measurement

    if (gen != slotGeneration[(size_t) slot].load())
        return; // a newer load / preset replaced this slot while we were measuring

    {
        std::lock_guard<std::mutex> al(analysisMutex);
        rawRenders[(size_t) slot] = raw;
    }
    storeMeasurement(slot, meas, gainDb);
    derived[(size_t) slot].measuredDriveDb.store(driveDb);

    const auto info = describe(*result.model, file);
    captureDbu[(size_t) slot].store(newDbu);
    const bool canEco = mi.slimmable, ecoActive = mi.ecoActive;
    const bool isAmp = result.model->isMorph();
    const double gainMin = result.model->getMorphMin(), gainMax = result.model->getMorphMax();
    const double fadeFloor = isAmp ? result.model->getMorphSilenceFloor() : -1.0;
    const double fadeTop = isAmp && !result.model->getMorphKnobs().empty() ? result.model->getMorphKnobs().front() : 0.0;
    const auto gainKnobs = result.model->getMorphKnobs();
    lastEco[(size_t) slot] = eco;
    engine.getSlot(slot).submit(std::move(result.model));
    {
        const juce::ScopedLock sl(statusLock);
        toneStacks[(size_t) slot] = tone;
        toneShown[(size_t) slot] = "?"; // the tone filter is (re)made by the timer
    }
    SlotStatus newStatus { SlotState::loaded, file.getFullPathName(), isAmp ? juce::String(mi.displayName) : file.getFileNameWithoutExtension(), info, canEco, ecoActive };
    newStatus.isAmp = isAmp;
    newStatus.gainMin = gainMin;
    newStatus.gainMax = gainMax;
    derived[(size_t) slot].fadeTop.store(fadeTop);
    derived[(size_t) slot].fadeFloor.store(fadeFloor);
    for (double k : gainKnobs) newStatus.gainSteps.add(k);
    newStatus.hasTone = tone != nullptr && !tone->empty();
    for (int k = 0; k < 3 && tone != nullptr; ++k) if (!tone->knobs[(size_t) k].empty()) newStatus.toneMask |= 1 << k;
    setSlotStatus(slot, newStatus);
    startDropoutGrace(2000); // the new capture's first blocks
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        auto& c = driveCurves[(size_t) slot];
        if (c.path != file.getFullPathName()) c.valid = false; // another capture: new curve (ECO reload keeps the old one meanwhile)
    }
    requestDriveCurve(slot);

    auto aliveFlag = alive;
    juce::MessageManager::callAsync([this, aliveFlag] {
        if (aliveFlag->load()) updateLatency();
    });
}

void AmpsurdProcessor::unloadCapture(int slot)
{
    derived[(size_t) slot].fadeFloor.store(-1.0); // no amp fade once empty
    if (getIrStatus(slot).state != IrState::none)
        removeIr(slot); // REMOVE empties the whole slot: capture, cabinet IR and pedals
    for (int k = 0; k < kNumPedals; ++k)
        if (getPedalStatus(slot, k).state != SlotState::empty) removePedal(slot, k);
    const auto previous = getSlotStatus(slot).state;
    const int gen = ++slotGeneration[(size_t) slot];
    setSlotStatus(slot, {});
    if (previous != SlotState::empty)
        renormaliseMixAfterChange(slot, false);

    auto aliveFlag = alive;
    loaderPool.addJob([this, slot, gen, aliveFlag] {
        if (!aliveFlag->load()) return;
        std::lock_guard<std::mutex> lock(configMutex);
        if (gen != slotGeneration[(size_t) slot].load()) return;
        engine.getSlot(slot).submit(CaptureModel::makeEmpty(currentSampleRate.load(), currentMaxBlock));
        {
            const juce::ScopedLock sl(statusLock);
            toneStacks[(size_t) slot] = nullptr;
            toneShown[(size_t) slot] = "?";
        }
        captureDbu[(size_t) slot].store(std::numeric_limits<double>::quiet_NaN());
        std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> snapshot;
        {
            std::lock_guard<std::mutex> al(analysisMutex);
            measurements[(size_t) slot] = nullptr;
            rawRenders[(size_t) slot] = nullptr;
            snapshot = measurements;
        }
        applyRig(CaptureAnalyzer::analyseRig(snapshot));
        juce::MessageManager::callAsync([this, aliveFlag] {
            if (aliveFlag->load()) updateLatency();
        });
    });
}

void AmpsurdProcessor::remeasureJob(int gen)
{
    ampsurd::EventLog::Scope busy(eventLog, "measuring the amps (level match, alignment)");
    std::lock_guard<std::mutex> lock(configMutex);
    const double sr = currentSampleRate.load();
    const double gainDb = inputParam->load();
    const int calVersion = calibrationVersion.load();
    const auto test = CaptureAnalyzer::makeTestSignal(sr);

    std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> fresh {};
    std::array<std::shared_ptr<const std::vector<double>>, kNumSlots> renders {};
    std::array<int, kNumSlots> gens {};
    std::array<double, kNumSlots> drives {};
    for (int s = 0; s < kNumSlots; ++s)
    {
        if (gen != remeasureGeneration.load()) return;
        drives[(size_t) s] = slotParams[(size_t) s].drive->load();
        const auto st = getSlotStatus(s);
        gens[(size_t) s] = slotGeneration[(size_t) s].load();
        if (st.state != SlotState::loaded) continue;
        auto r = loadSlotModel(s, juce::File(st.path), sr, currentMaxBlock, slotParams[(size_t) s].eco->load() > 0.5f);
        if (!r.model) continue;
        const auto& mi = r.model->getInfo();
        const double g = std::pow(10.0, (gainDb + drives[(size_t) s] + trimDbFor(calibrateOn.load(), interfaceDbu.load(),
                                                            effectiveDbu(s, mi.hasInputLevel ? mi.inputLevelDbu : std::numeric_limits<double>::quiet_NaN()))) / 20.0);
        auto driven = test;
        for (double& v : driven) v *= g;
        driven = renderPedals(s, std::move(driven), sr, currentMaxBlock);
        renders[(size_t) s] = std::make_shared<const std::vector<double>>(CaptureAnalyzer::render(*r.model, driven, false));
        fresh[(size_t) s] = measureWithIr(s, *renders[(size_t) s], sr);
    }

    std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> snapshot;
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        for (int s = 0; s < kNumSlots; ++s)
            if (fresh[(size_t) s] && gens[(size_t) s] == slotGeneration[(size_t) s].load())
            {
                measurements[(size_t) s] = fresh[(size_t) s];
                rawRenders[(size_t) s] = renders[(size_t) s];
            }
        measuredInputGainDb = gainDb;
        snapshot = measurements;
    }
    measuredCalibrationVersion.store(calVersion);
    applyRig(CaptureAnalyzer::analyseRig(snapshot));
    for (int s = 0; s < kNumSlots; ++s)
        if (fresh[(size_t) s] && gens[(size_t) s] == slotGeneration[(size_t) s].load())
            derived[(size_t) s].measuredDriveDb.store(drives[(size_t) s]); // together with the new level gains
    for (int s = 0; s < kNumSlots; ++s)
        if (fresh[(size_t) s]) requestDriveCurve(s); // skipped inside when INPUT / calibration did not change
}

// ---------------------------------------------------------------------------------------------
// Per-amp INPUT / OUTPUT
// ---------------------------------------------------------------------------------------------
double AmpsurdProcessor::ampFadeGain(int s, double gain) const noexcept
{
    // an AMPSURD amp that "starts from silence": below its lowest capture GAIN works like a volume knob (see CaptureModel)
    const double floor = derived[(size_t) s].fadeFloor.load(std::memory_order_relaxed), top = derived[(size_t) s].fadeTop.load(std::memory_order_relaxed);
    if (floor < 0.0 || gain >= top || top - floor <= 1e-9) return 1.0;
    const double t = (gain - floor) / (top - floor);
    return t <= 0.0 ? 0.0 : t * t;
}

double AmpsurdProcessor::slotOutputGain(int s, bool levelMatch) const noexcept
{
    const auto& d = derived[(size_t) s];
    const double base = levelMatch ? d.levelGain.load(std::memory_order_relaxed) : 1.0;
    const double db = d.driveCompDb.load(std::memory_order_relaxed) + (double) slotParams[(size_t) s].level->load();
    return base * std::pow(10.0, db / 20.0);
}

bool AmpsurdProcessor::isDriveCompensationReady(int slot) const
{
    std::lock_guard<std::mutex> al(analysisMutex);
    return driveCurves[(size_t) slot].valid;
}

double AmpsurdProcessor::getDriveCurveAmpGain(int slot) const
{
    std::lock_guard<std::mutex> al(analysisMutex);
    const auto& c = driveCurves[(size_t) slot];
    return c.valid ? c.ampGain : -1.0;
}

void AmpsurdProcessor::requestDriveCurve(int slot)
{
    const int gen = ++curveGeneration[(size_t) slot];
    auto aliveFlag = alive;
    curvePool.addJob([this, slot, gen, aliveFlag] {
        if (aliveFlag->load()) driveCurveJob(slot, gen);
    });
}

void AmpsurdProcessor::driveCurveJob(int slot, int gen)
{
    // what the curve depends on: the capture (and ECO), INPUT + calibration, the cabinet IR
    double sr = 0.0, baseDb = 0.0, ampGain = 5.0;
    int block = 0;
    std::shared_ptr<const std::vector<double>> ir;
    bool eco = false;
    juce::String path, pedals;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        if (gen != curveGeneration[(size_t) slot].load()) return;
        pedals = pedalSignature(slot);
        const auto st = getSlotStatus(slot);
        if (st.state != SlotState::loaded) return;
        path = st.path;
        sr = currentSampleRate.load();
        block = currentMaxBlock;
        eco = slotParams[(size_t) slot].eco->load() > 0.5f;
        baseDb = inputParam->load() + getCalibrationTrimDb(slot);
        if (isIrOn(slot)) ir = irPrepared[(size_t) slot];
        if (st.isAmp) ampGain = slotParams[(size_t) slot].ampGain->load(); // loadSlotModel() sets the morph to it
    }
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        const auto& c = driveCurves[(size_t) slot];
        if (c.valid && std::abs(c.baseDb - baseDb) < 0.25 && c.eco == eco && c.irData == (const void*) ir.get() && c.pedals == pedals
            && std::abs(c.ampGain - ampGain) < 0.25)
            return; // nothing changed
    }
    ampsurd::EventLog::Scope busy(eventLog, "measuring amp " + juce::String(slot + 1) + "'s INPUT curve");
    auto r = loadSlotModel(slot, juce::File(path), sr, block, eco);
    if (!r.model) return;
    auto test = CaptureAnalyzer::makeTestSignal(sr);
    const double g = std::pow(10.0, baseDb / 20.0);
    for (double& v : test) v *= g;
    CaptureAnalyzer::DriveCurve curve;
    const bool hasPedals = pedals.containsChar('1');
    const bool done = CaptureAnalyzer::driveCurve(*r.model, test, ir.get(), curve, [this, slot, gen] {
        return gen != curveGeneration[(size_t) slot].load() || !alive->load();
    }, [this, slot, sr, block, hasPedals](std::vector<double>& x) {
        if (!hasPedals) return;
        std::lock_guard<std::mutex> lock(configMutex);
        x = renderPedals(slot, std::move(x), sr, block); // INPUT drives the first pedal
    });
    if (!done) return;
    std::lock_guard<std::mutex> al(analysisMutex);
    if (gen != curveGeneration[(size_t) slot].load()) return;
    auto& c = driveCurves[(size_t) slot];
    c.valid = true;
    c.curve = curve;
    c.baseDb = baseDb;
    c.ampGain = ampGain;
    c.eco = eco;
    c.irData = ir.get();
    c.pedals = pedals;
    c.ir = ir != nullptr;
    c.path = path;
    // GAIN moved while this curve was being made: make it again at the new position
    if (getSlotStatus(slot).isAmp && std::abs(slotParams[(size_t) slot].ampGain->load() - ampGain) >= 0.25)
        requestDriveCurve(slot);
}

void AmpsurdProcessor::updateDriveCompensation()
{
    const bool levelMatch = levelMatchParam->load() > 0.5f;
    std::lock_guard<std::mutex> al(analysisMutex);
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& d = derived[(size_t) s];
        const auto& c = driveCurves[(size_t) s];
        double comp = 0.0;
        if (c.valid && getSlotStatus(s).state == SlotState::loaded)
        {
            const double now = CaptureAnalyzer::loudnessAtDrive(c.curve, slotParams[(size_t) s].drive->load());
            // LEVEL MATCH on: the measured level gain already holds the loudness at the measured INPUT;
            // off: the amp stays as loud as at INPUT 0 dB
            const double ref = CaptureAnalyzer::loudnessAtDrive(c.curve, levelMatch ? d.measuredDriveDb.load() : 0.0);
            comp = juce::jlimit(-30.0, 30.0, ref - now);
        }
        d.driveCompDb.store(comp);
    }
}

void AmpsurdProcessor::setDragonShown(bool on)
{
    showDragon.store(on);
    juce::PropertiesFile::Options o;
    o.applicationName = "AMPSURD"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile pf(o);
    pf.setValue("showDragon", on);
    pf.saveIfNeeded();
}

void AmpsurdProcessor::setEcoByDefault(bool on)
{
    ecoByDefault.store(on);
    juce::PropertiesFile::Options o;
    o.applicationName = "AMPSURD"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile pf(o);
    pf.setValue("ecoByDefault", on);
    pf.saveIfNeeded();
}

void AmpsurdProcessor::setGuitarInput(int index)
{
    index = juce::jlimit(0, 1, index);
    guitarInput.store(index);
    juce::PropertiesFile::Options o;
    o.applicationName = "AMPSURD"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile pf(o);
    pf.setValue("guitarInput", index);
    pf.saveIfNeeded();
}

void AmpsurdProcessor::setMultiCore(bool on)
{
    if (on == multiCore.load()) return;
    multiCore.store(on);
    suspendProcessing(true); // the audio callback is not running while the worker threads change
    engine.setMultiCore(on ? -1 : 0);
    suspendProcessing(false);
    juce::PropertiesFile::Options o;
    o.applicationName = "AMPSURD"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile pf(o);
    pf.setValue("multiCore", on);
    pf.saveIfNeeded();
}

double AmpsurdProcessor::trimDbFor(bool on, double interfaceLevel, double captureLevel)
{
    if (!on || std::isnan(captureLevel)) return 0.0;
    return juce::jlimit(-24.0, 24.0, interfaceLevel - captureLevel);
}

double AmpsurdProcessor::getCalibrationTrimDb(int slot) const
{
    return trimDbFor(calibrateOn.load(std::memory_order_relaxed), interfaceDbu.load(std::memory_order_relaxed),
                     effectiveDbu(slot, captureDbu[(size_t) slot].load(std::memory_order_relaxed)));
}

void AmpsurdProcessor::setInputCalibration(bool on, double interfaceLevelDbu)
{
    interfaceLevelDbu = juce::jlimit(-20.0, 30.0, interfaceLevelDbu);
    if (on == calibrateOn.load() && interfaceLevelDbu == interfaceDbu.load()) return;
    calibrateOn.store(on);
    interfaceDbu.store(interfaceLevelDbu);
    ++calibrationVersion; // the captures are re-measured (level match / alignment at the new drive)
    juce::PropertiesFile::Options o;
    o.applicationName = "AMPSURD"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile pf(o);
    pf.setValue("calibrateInput", on);
    pf.setValue("interfaceInputDbu", interfaceLevelDbu);
    pf.saveIfNeeded();
}

// ---------------------------------------------------------------------------------------------
// Cabinet IRs
// ---------------------------------------------------------------------------------------------
std::shared_ptr<const CaptureAnalyzer::Measurement> AmpsurdProcessor::measureWithIr(int slot, const std::vector<double>& raw, double sr)
{
    // Called with configMutex held. The slot is measured as it is heard: capture -> IR (when on).
    const auto ir = irPrepared[(size_t) slot];
    if (ir && !ir->empty() && isIrOn(slot))
    {
        ampsurd::Convolver c(*ir);
        std::vector<double> y(raw.size());
        c.process(raw.data(), y.data(), (int) raw.size());
        return CaptureAnalyzer::measure(y, sr);
    }
    return CaptureAnalyzer::measure(raw, sr);
}

void AmpsurdProcessor::storeMeasurement(int slot, std::shared_ptr<const CaptureAnalyzer::Measurement> m, double gainDb)
{
    std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> snapshot;
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        measurements[(size_t) slot] = std::move(m);
        measuredInputGainDb = gainDb;
        snapshot = measurements;
    }
    applyRig(CaptureAnalyzer::analyseRig(snapshot));
}

void AmpsurdProcessor::measureSlotJob(int slot)
{
    std::lock_guard<std::mutex> lock(configMutex);
    std::shared_ptr<const std::vector<double>> raw;
    double gainDb = 0.0;
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        raw = rawRenders[(size_t) slot];
        gainDb = measuredInputGainDb;
    }
    if (!raw || getSlotStatus(slot).state != SlotState::loaded)
        return; // nothing measured yet: the capture's own load will include the IR
    storeMeasurement(slot, measureWithIr(slot, *raw, currentSampleRate.load()), gainDb);
    requestDriveCurve(slot); // the IR is part of the INPUT -> loudness curve
}

AmpsurdProcessor::IrStatus AmpsurdProcessor::getIrStatus(int slot) const
{
    const juce::ScopedLock sl(statusLock);
    return irStatus[(size_t) slot];
}

void AmpsurdProcessor::setIrStatus(int slot, const IrStatus& st)
{
    bool problem = false;
    {
        const juce::ScopedLock sl(statusLock);
        const auto& old = irStatus[(size_t) slot];
        problem = (st.state == IrState::missing || st.state == IrState::error) && (old.state != st.state || old.path != st.path);
        irStatus[(size_t) slot] = st;
    }
    if (problem) eventLog.add("PROBLEM", "IR of amp " + juce::String(slot + 1) + ": " + (st.info.isNotEmpty() ? st.info : st.path));
}

void AmpsurdProcessor::loadIr(int slot, const juce::File& file)
{
    const int gen = ++irGeneration[(size_t) slot];
    setIrStatus(slot, { IrState::loading, file.getFullPathName(), file.getFileNameWithoutExtension(), "Loading..." });
    auto aliveFlag = alive;
    loaderPool.addJob([this, slot, file, gen, aliveFlag] {
        if (aliveFlag->load()) irJob(slot, file, gen);
    });
}

void AmpsurdProcessor::irJob(int slot, juce::File file, int gen)
{
    ampsurd::EventLog::Scope busy(eventLog, "loading the IR " + file.getFileName() + " for amp " + juce::String(slot + 1));
    std::lock_guard<std::mutex> lock(configMutex);
    if (gen != irGeneration[(size_t) slot].load()) return;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    auto fail = [&](const juce::String& why) {
        setIrStatus(slot, { IrState::error, file.getFullPathName(), file.getFileNameWithoutExtension(), why });
    };
    if (!reader) { fail("Could not read this file as audio (WAV, AIFF, FLAC)."); return; }
    const double fileRate = reader->sampleRate;
    const auto len = (int) std::min<juce::int64>(reader->lengthInSamples, (juce::int64) (fileRate * 10.0));
    if (len <= 0 || fileRate <= 0) { fail("The file contains no audio."); return; }
    juce::AudioBuffer<float> buf((int) reader->numChannels, len);
    reader->read(&buf, 0, len, 0, true, true);
    auto src = std::make_shared<IrSource>();
    src->rate = fileRate;
    src->samples.resize((size_t) len);
    for (int i = 0; i < len; ++i) src->samples[(size_t) i] = buf.getSample(0, i); // mono: first (left) channel

    const double sr = currentSampleRate.load();
    auto prep = ampsurd::prepareImpulseResponse(src->samples, fileRate, sr);
    if (prep.samples.empty()) { fail("The file is silent."); return; }

    juce::String info = juce::String(fileRate / 1000.0, 1) + " kHz,  " + juce::String(len) + " samples ("
                      + juce::String(len * 1000.0 / fileRate, 1) + " ms)";
    if (reader->numChannels > 1) info << "\n" << (int) reader->numChannels << " channels: the left channel is used";
    if (prep.resampled) info << "\nconverted to " << juce::String(sr / 1000.0, 1) << " kHz";
    if (prep.trimmedSamples > 0) info << "\n" << prep.trimmedSamples << " samples of silence at the start removed";
    if (prep.truncated) info << "\nshortened to 1 s";

    if (gen != irGeneration[(size_t) slot].load()) return;
    adoptIr(slot, src, std::move(prep.samples), { IrState::loaded, file.getFullPathName(), file.getFileNameWithoutExtension(), info });
}

// configMutex held: the prepared IR goes to the engine (crossfaded), then the slot is re-measured with it
void AmpsurdProcessor::adoptIr(int slot, std::shared_ptr<IrSource> src, std::vector<double> prepared, const IrStatus& status)
{
    const double sr = currentSampleRate.load();
    irSources[(size_t) slot] = src;
    irPrepared[(size_t) slot] = std::make_shared<const std::vector<double>>(prepared);
    engine.getIrSlot(slot).submit(std::make_unique<ampsurd::Convolver>(std::move(prepared)));
    setIrStatus(slot, status);
    lastIrOn[(size_t) slot] = isIrOn(slot);

    // re-measure the slot as it now sounds (level match, alignment, mix law)
    std::shared_ptr<const std::vector<double>> raw;
    double gainDb = 0.0;
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        raw = rawRenders[(size_t) slot];
        gainDb = measuredInputGainDb;
    }
    if (raw && getSlotStatus(slot).state == SlotState::loaded)
    {
        storeMeasurement(slot, measureWithIr(slot, *raw, sr), gainDb);
        requestDriveCurve(slot);
    }
}

// ---------------------------------------------------------------------------------------------
// Pedal captures before the amps
// ---------------------------------------------------------------------------------------------
AmpsurdProcessor::PedalStatus AmpsurdProcessor::getPedalStatus(int slot, int k) const
{
    const juce::ScopedLock sl(statusLock);
    return pedalStatus[(size_t) slot][(size_t) k];
}

void AmpsurdProcessor::setPedalStatus(int slot, int k, const PedalStatus& st)
{
    bool problem = false;
    {
        const juce::ScopedLock sl(statusLock);
        const auto& old = pedalStatus[(size_t) slot][(size_t) k];
        problem = (st.state == SlotState::missing || st.state == SlotState::error) && (old.state != st.state || old.path != st.path);
        pedalStatus[(size_t) slot][(size_t) k] = st;
    }
    if (problem) eventLog.add("PROBLEM", "pedal " + juce::String(k + 1) + " of amp " + juce::String(slot + 1) + ": " + (st.info.isNotEmpty() ? st.info : st.path));
}

juce::String AmpsurdProcessor::pedalSignature(int slot) const
{
    juce::String sig;
    for (int k = 0; k < kNumPedals; ++k)
    {
        const auto st = getPedalStatus(slot, k);
        const bool on = st.state == SlotState::loaded && slotParams[(size_t) slot].pedalOn[(size_t) k]->load() > 0.5f;
        sig << (on ? "1" : "0") << "|" << (on ? st.path : juce::String()) << "|"
            << (on ? juce::String(slotParams[(size_t) slot].pedalLevel[(size_t) k]->load(), 1) : juce::String()) << ";";
    }
    return sig;
}

double AmpsurdProcessor::effectiveDbu(int slot, double ampDbu) const
{
    for (int k = 0; k < kNumPedals; ++k)
        if (getPedalStatus(slot, k).state == SlotState::loaded && slotParams[(size_t) slot].pedalOn[(size_t) k]->load() > 0.5f)
            return pedalDbu[(size_t) slot][(size_t) k].load(); // the guitar goes into this pedal first
    return ampDbu;
}

std::vector<double> AmpsurdProcessor::renderPedals(int slot, std::vector<double> x, double sr, int block) const
{
    for (int k = 0; k < kNumPedals; ++k)
    {
        const auto st = getPedalStatus(slot, k);
        if (st.state != SlotState::loaded || slotParams[(size_t) slot].pedalOn[(size_t) k]->load() <= 0.5f) continue;
        auto r = CaptureModel::load(toStdPath(juce::File(st.path)), sr, block, false);
        if (!r.model) continue;
        auto y = CaptureAnalyzer::render(*r.model, x, false);
        const double lv = std::pow(10.0, (double) slotParams[(size_t) slot].pedalLevel[(size_t) k]->load() / 20.0);
        for (auto& v : y) v *= lv;
        x.swap(y);
    }
    return x;
}

void AmpsurdProcessor::loadPedal(int slot, int k, const juce::File& file)
{
    const int gen = ++pedalGeneration[(size_t) slot][(size_t) k];
    setPedalStatus(slot, k, { SlotState::loading, file.getFullPathName(), file.getFileNameWithoutExtension(), "Loading..." });
    auto aliveFlag = alive;
    loaderPool.addJob([this, slot, k, file, gen, aliveFlag] {
        if (!aliveFlag->load()) return;
        ampsurd::EventLog::Scope busy(eventLog, "loading the pedal " + file.getFileName() + " for amp " + juce::String(slot + 1));
        {
            std::lock_guard<std::mutex> lock(configMutex);
            if (gen != pedalGeneration[(size_t) slot][(size_t) k].load()) return;
            auto r = CaptureModel::load(toStdPath(file), currentSampleRate.load(), currentMaxBlock, false);
            if (!r.model)
            {
                setPedalStatus(slot, k, { SlotState::error, file.getFullPathName(), file.getFileNameWithoutExtension(),
                                          "Could not load this file:\n" + juce::String(r.error) });
                return;
            }
            const auto& mi = r.model->getInfo();
            pedalDbu[(size_t) slot][(size_t) k].store(mi.hasInputLevel ? mi.inputLevelDbu : std::numeric_limits<double>::quiet_NaN());
            const auto info = describe(*r.model, file);
            engine.getPedal(slot, k).submit(std::move(r.model));
            setPedalStatus(slot, k, { SlotState::loaded, file.getFullPathName(), file.getFileNameWithoutExtension(), info });
        }
        juce::MessageManager::callAsync([this, slot, aliveFlag] {
            if (!aliveFlag->load()) return;
            lastPedalSig[(size_t) slot] = pedalSignature(slot);
            requestSlotRemeasure(slot);
            updateLatency();
        });
    });
}

void AmpsurdProcessor::removePedal(int slot, int k)
{
    ++pedalGeneration[(size_t) slot][(size_t) k];
    const bool had = getPedalStatus(slot, k).state == SlotState::loaded;
    setPedalStatus(slot, k, {});
    pedalDbu[(size_t) slot][(size_t) k].store(std::numeric_limits<double>::quiet_NaN());
    auto aliveFlag = alive;
    loaderPool.addJob([this, slot, k, aliveFlag] {
        if (!aliveFlag->load()) return;
        std::lock_guard<std::mutex> lock(configMutex);
        engine.getPedal(slot, k).submit(CaptureModel::makeEmpty(currentSampleRate.load(), currentMaxBlock));
    });
    lastPedalSig[(size_t) slot] = pedalSignature(slot);
    if (had) requestSlotRemeasure(slot);
    updateLatency();
}

void AmpsurdProcessor::requestSlotRemeasure(int slot)
{
    if (getSlotStatus(slot).state != SlotState::loaded) return; // a capture being loaded is measured with the pedals anyway
    const int gen = ++slotRemeasureGeneration[(size_t) slot];
    auto aliveFlag = alive;
    loaderPool.addJob([this, slot, gen, aliveFlag] {
        if (aliveFlag->load()) slotRemeasureJob(slot, gen);
    });
}

void AmpsurdProcessor::slotRemeasureJob(int slot, int gen)
{
    ampsurd::EventLog::Scope busy(eventLog, "measuring amp " + juce::String(slot + 1) + " with its pedals");
    std::lock_guard<std::mutex> lock(configMutex);
    if (gen != slotRemeasureGeneration[(size_t) slot].load()) return;
    const auto st = getSlotStatus(slot);
    if (st.state != SlotState::loaded) return;
    const int slotGen = slotGeneration[(size_t) slot].load();
    const double sr = currentSampleRate.load();
    auto r = loadSlotModel(slot, juce::File(st.path), sr, currentMaxBlock, slotParams[(size_t) slot].eco->load() > 0.5f);
    if (!r.model) return;
    const auto& mi = r.model->getInfo();
    const double gainDb = inputParam->load();
    const double driveDb = slotParams[(size_t) slot].drive->load();
    const double g = std::pow(10.0, (gainDb + driveDb + trimDbFor(calibrateOn.load(), interfaceDbu.load(),
                                     effectiveDbu(slot, mi.hasInputLevel ? mi.inputLevelDbu : std::numeric_limits<double>::quiet_NaN()))) / 20.0);
    auto test = CaptureAnalyzer::makeTestSignal(sr);
    for (double& v : test) v *= g;
    test = renderPedals(slot, std::move(test), sr, currentMaxBlock);
    auto raw = std::make_shared<const std::vector<double>>(CaptureAnalyzer::render(*r.model, test, false));
    const auto meas = measureWithIr(slot, *raw, sr);
    if (gen != slotRemeasureGeneration[(size_t) slot].load() || slotGen != slotGeneration[(size_t) slot].load()) return;
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        rawRenders[(size_t) slot] = raw;
    }
    storeMeasurement(slot, meas, gainDb);
    derived[(size_t) slot].measuredDriveDb.store(driveDb);
    requestDriveCurve(slot);
}

// ---------------------------------------------------------------------------------------------
// AMPSURD amps (.ampsurdamp)
// ---------------------------------------------------------------------------------------------
CaptureModel::LoadResult AmpsurdProcessor::loadSlotModel(int slot, const juce::File& file, double sr, int block, bool eco,
                                                         std::shared_ptr<const ampsurd::ToneStack>* tone)
{
    if (!file.hasFileExtension(".ampsurdamp")) return CaptureModel::load(toStdPath(file), sr, block, eco);
    CaptureModel::LoadResult res;
    ampsurd::amp::Profile prof;
    juce::String err;
    if (!ampsurd::amp::read(file, prof, err)) { res.error = err.toStdString(); return res; }
    std::vector<CaptureModel::MorphStep> steps;
    for (auto& st : prof.steps)
    {
        auto r = CaptureModel::load(toStdPath(st.nam), sr, block, eco);
        if (!r.model) { res.error = (st.nam.getFileName() + ": ").toStdString() + r.error; return res; }
        steps.push_back({ std::move(r.model), st.knob, 1.0, 1.0, 0 });
    }
    // the steps' level, polarity, timing (sub-sample) and blend loss - measured once per file, rate and ECO
    const juce::String key = file.getFullPathName() + "|" + juce::String(file.getLastModificationTime().toMilliseconds()) + "|"
                           + juce::String(sr) + "|" + (eco ? "eco" : "full") + "|v2";
    std::vector<std::array<double, 4>> cal;
    {
        std::lock_guard<std::mutex> l(morphCacheMutex);
        if (auto it = morphCache.find(key); it != morphCache.end()) cal = it->second;
    }
    size_t ref = 0;
    for (size_t i = 0; i < steps.size(); ++i)
        if (std::abs(steps[i].knob - 5.0) < std::abs(steps[ref].knob - 5.0)) ref = i;
    if (cal.size() != steps.size())
    {
        CaptureAnalyzer::calibrateMorph(steps, sr, block, std::pow(10.0, inputParam->load() / 20.0));
        cal.clear();
        for (auto& st : steps) cal.push_back({ st.gain, st.polarity, st.delay, st.pairLoss });
        std::lock_guard<std::mutex> l(morphCacheMutex);
        morphCache[key] = cal;
    }
    for (size_t i = 0; i < steps.size(); ++i)
    {
        steps[i].gain = cal[i][0];
        steps[i].polarity = cal[i][1];
        steps[i].delay = cal[i][2];
        steps[i].pairLoss = cal[i][3];
    }
    auto info = steps[ref].model->getInfo();
    info.filePath = toStdPath(file).string();
    info.displayName = prof.name.toStdString();
    info.architecture = "AMPSURD amp";
    info.architectureHint = ("AMPSURD amp: " + juce::String((int) steps.size()) + " gain captures"
                             + (prof.tone != nullptr && !prof.tone->empty() ? juce::String(" + tone controls") : juce::String())
                             + (prof.author.isNotEmpty() ? " - " + prof.author : juce::String())).toStdString();
    info.slimmable = std::all_of(steps.begin(), steps.end(), [](const CaptureModel::MorphStep& m) { return m.model->getInfo().slimmable; });
    info.ecoActive = eco && info.slimmable;
    res.model = CaptureModel::makeMorph(std::move(steps), info, sr, block);
    res.model->setMorphSilenceFloor(prof.silenceFloor);
    res.model->setMorphPosition(slotParams[(size_t) slot].ampGain->load());
    if (tone != nullptr) *tone = prof.tone;
    return res;
}

void AmpsurdProcessor::updateToneStacks()
{
    const double sr = currentSampleRate.load();
    for (int s = 0; s < kNumSlots; ++s)
    {
        std::shared_ptr<const ampsurd::ToneStack> ts;
        juce::String shown;
        {
            const juce::ScopedLock sl(statusLock);
            ts = toneStacks[(size_t) s];
            shown = toneShown[(size_t) s];
        }
        const auto& sp = slotParams[(size_t) s];
        const juce::String want = ts == nullptr || ts->empty() ? juce::String("none")
                                : juce::String(sp.ampTone[0]->load(), 2) + "|" + juce::String(sp.ampTone[1]->load(), 2) + "|"
                                      + juce::String(sp.ampTone[2]->load(), 2) + "|" + juce::String(sr);
        if (want == shown) continue;
        if (ts == nullptr || ts->empty())
            engine.getToneStack(s).submit(std::make_unique<ampsurd::Convolver>(std::vector<double> {}));
        else
        {
            const auto curve = ts->curveDb(sp.ampTone[0]->load(), sp.ampTone[1]->load(), sp.ampTone[2]->load());
            if (ampsurd::ToneStack::isFlat(curve)) engine.getToneStack(s).submit(std::make_unique<ampsurd::Convolver>(std::vector<double> {}));
            else engine.getToneStack(s).submit(std::make_unique<ampsurd::Convolver>(ampsurd::ToneStack::minPhaseFir(curve, sr, 1024)));
        }
        const juce::ScopedLock sl(statusLock);
        toneShown[(size_t) s] = want;
    }
}

void AmpsurdProcessor::loadCabIr(int slot, ampsurd::CabPoint p)
{
    p = ampsurd::CabIrModel::clamp(p);
    const int gen = ++irGeneration[(size_t) slot];
    const auto ref = juce::String(ampsurd::CabIrModel::toRef(p));
    const auto name = "AMPSURD " + juce::String(ampsurd::CabIrModel::describe(p));
    // shown at once (the IR follows within a few ms; while dragging, older requests are skipped)
    const auto cur = getIrStatus(slot);
    setIrStatus(slot, { cur.state == IrState::loaded ? IrState::loaded : IrState::loading, ref, name, cur.info });
    auto aliveFlag = alive;
    loaderPool.addJob([this, slot, p, gen, ref, name, aliveFlag] {
        if (!aliveFlag->load() || gen != irGeneration[(size_t) slot].load()) return;
        ampsurd::EventLog::Scope busy(eventLog, "calculating an AMPSURD IR for amp " + juce::String(slot + 1));
        const double sr = currentSampleRate.load();
        auto src = std::make_shared<IrSource>();
        src->rate = sr;
        src->samples = ampsurd::CabIrModel::make(p, sr); // designed at the session rate: no resampling
        std::lock_guard<std::mutex> lock(configMutex);
        if (gen != irGeneration[(size_t) slot].load()) return;
        auto prep = ampsurd::prepareImpulseResponse(src->samples, sr, sr);
        const juce::String info = "Calculated by AMPSURD (synthetic 4x12: speaker, microphone and its position)\n"
                                + juce::String(ampsurd::CabIrModel::kIrLength) + " samples at " + juce::String(sr / 1000.0, 1) + " kHz";
        adoptIr(slot, src, std::move(prep.samples), { IrState::loaded, ref, name, info });
    });
}

bool AmpsurdProcessor::getCabIrPoint(int slot, ampsurd::CabPoint& p) const
{
    return ampsurd::CabIrModel::fromRef(getIrStatus(slot).path.toStdString(), p);
}

void AmpsurdProcessor::removeIr(int slot)
{
    ++irGeneration[(size_t) slot];
    setIrStatus(slot, {});
    auto aliveFlag = alive;
    loaderPool.addJob([this, slot, aliveFlag] {
        if (!aliveFlag->load()) return;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            irSources[(size_t) slot] = nullptr;
            irPrepared[(size_t) slot] = nullptr;
            engine.getIrSlot(slot).submit(std::make_unique<ampsurd::Convolver>(std::vector<double> {}));
        }
        measureSlotJob(slot);
    });
}

void AmpsurdProcessor::applyRig(std::shared_ptr<const CaptureAnalyzer::RigAnalysis> newRig)
{
    {
        std::lock_guard<std::mutex> dl(derivedMutex);
        for (int s = 0; s < kNumSlots; ++s)
        {
            auto& d = derived[(size_t) s];
            if (newRig && newRig->meas[(size_t) s])
            {
                d.autoDelayMs.store(newRig->autoDelay[(size_t) s] / newRig->sampleRate * 1000.0);
                d.polarity.store(newRig->align[(size_t) s].polarity);
                d.levelGain.store(CaptureAnalyzer::levelMatchGain(newRig->loudnessDb[(size_t) s]));
            }
            else
            {
                d.autoDelayMs.store(0.0);
                d.polarity.store(1.0);
                d.levelGain.store(1.0);
            }
        }
        std::lock_guard<std::mutex> al(analysisMutex);
        rig = std::move(newRig);
        ++rigVersion;
    }
    updateCovariance(true);
}

void AmpsurdProcessor::updateCovariance(bool force)
{
    std::lock_guard<std::mutex> dl(derivedMutex);
    std::shared_ptr<const CaptureAnalyzer::RigAnalysis> r;
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        r = rig;
    }
    const bool levelMatch = levelMatchParam->load() > 0.5f;
    const double sr = r ? r->sampleRate : currentSampleRate.load();

    std::array<double, kNumSlots> delays {}, pol {}, phase {}, lg {};
    std::array<double, 4 * kNumSlots + 4> key {};
    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto a = effectiveAlign(s, sr);
        delays[(size_t) s] = a.delaySamples;
        pol[(size_t) s] = a.polarity;
        phase[(size_t) s] = a.phaseRadians;
        lg[(size_t) s] = slotOutputGain(s, levelMatch);
        key[(size_t) (4 * s)] = delays[(size_t) s];
        key[(size_t) (4 * s + 1)] = pol[(size_t) s];
        key[(size_t) (4 * s + 2)] = phase[(size_t) s];
        key[(size_t) (4 * s + 3)] = lg[(size_t) s];
    }
    key[4 * kNumSlots] = (double) rigVersion.load();
    key[4 * kNumSlots + 1] = levelMatch ? 1.0 : 0.0;
    if (!force && key == lastCovKey)
        return;
    lastCovKey = key;

    if (!r)
    {
        engine.setCovariance({}, {});
        return;
    }
    const auto cov = CaptureAnalyzer::covariance(*r, delays, pol, phase, lg);
    engine.setCovariance(cov.C, cov.valid, cov.band);
}

AmpsurdProcessor::AlignInfo AmpsurdProcessor::getAlignInfo(int slot) const
{
    AlignInfo info;
    std::lock_guard<std::mutex> al(analysisMutex);
    if (!rig || !rig->meas[(size_t) slot])
        return info;
    const auto& a = rig->align[(size_t) slot];
    info.measured = true;
    info.isReference = rig->reference == slot;
    info.reliable = a.reliable;
    info.autoOffsetMs = rig->autoDelay[(size_t) slot] / rig->sampleRate * 1000.0;
    info.polarity = a.polarity;
    info.corrBefore = a.correlationBefore;
    info.corrAfter = a.correlationAfter;
    info.loudnessDb = rig->loudnessDb[(size_t) slot];
    return info;
}

void AmpsurdProcessor::resetAlignment(int slot)
{
    setParam(params, slotParamId(slot, "align"), 0.0f);
    setParam(params, slotParamId(slot, "time"), 0.0f);
    setParam(params, slotParamId(slot, "phase"), 0.0f);
}

// ---------------------------------------------------------------------------------------------
// Linked mix faders (message thread)
// ---------------------------------------------------------------------------------------------
namespace
{
bool countsAsLoaded(AmpsurdProcessor::SlotState s)
{
    return s == AmpsurdProcessor::SlotState::loaded || s == AmpsurdProcessor::SlotState::loading;
}
} // namespace

bool AmpsurdProcessor::isSlotAudible(int slot) const
{
    bool anySolo = false;
    for (int s = 0; s < kNumSlots; ++s)
        anySolo = anySolo || (countsAsLoaded(getSlotStatus(s).state) && slotParams[(size_t) s].solo->load() > 0.5f);
    const auto& sp = slotParams[(size_t) slot];
    return countsAsLoaded(getSlotStatus(slot).state) && sp.mute->load() < 0.5f && (!anySolo || sp.solo->load() > 0.5f);
}

bool AmpsurdProcessor::mixesWith(int slot) const
{
    return countsAsLoaded(getSlotStatus(slot).state) && !isSlotMuted(slot);
}

void AmpsurdProcessor::renormaliseMixAfterChange(int changed, bool added)
{
    // muted amps keep their fader at 0 % and are not part of the 100 %
    std::vector<int> others;
    for (int s = 0; s < kNumSlots; ++s)
        if (s != changed && mixesWith(s))
            others.push_back(s);

    float newShare = 0.0f;
    if (added)
        newShare = 100.0f / (float) (others.size() + 1);
    const float remaining = 100.0f - newShare;

    float sum = 0.0f;
    for (int s : others) sum += slotParams[(size_t) s].mix->load();
    for (int s : others)
    {
        const float v = sum > 0.01f ? slotParams[(size_t) s].mix->load() * remaining / sum : remaining / (float) others.size();
        setParam(params, slotParamId(s, "mix"), v);
    }
    setParam(params, slotParamId(changed, "mix"), added ? newShare : 0.0f);
}

void AmpsurdProcessor::applyMuteToFaders(int slot, bool muted)
{
    if (!countsAsLoaded(getSlotStatus(slot).state))
        return;
    std::vector<int> others;
    float sum = 0.0f;
    for (int s = 0; s < kNumSlots; ++s)
        if (s != slot && mixesWith(s))
        {
            others.push_back(s);
            sum += slotParams[(size_t) s].mix->load();
        }
    const float current = slotParams[(size_t) slot].mix->load();
    float share = 0.0f;
    if (muted)
    {
        if (current > 0.01f) mutedShare[(size_t) slot] = current; // remembered for un-muting
    }
    else
    {
        // previous share; an amp muted in an older preset still has its fader up: keep it
        share = current > 0.01f ? current
              : (mutedShare[(size_t) slot] > 0.01f ? mutedShare[(size_t) slot] : 100.0f / (float) (others.size() + 1));
        if (sum < 0.01f) share = 100.0f; // the only amp that plays
        share = juce::jlimit(0.0f, 100.0f, share);
        mutedShare[(size_t) slot] = 0.0f;
    }
    if (sum >= 0.01f)
        for (int s : others)
            setParam(params, slotParamId(s, "mix"), slotParams[(size_t) s].mix->load() * (100.0f - share) / sum);
    setParam(params, slotParamId(slot, "mix"), share);
}

void AmpsurdProcessor::beginMixGesture(int slot)
{
    mixGestureSlot = slot;
    for (int s = 0; s < kNumSlots; ++s)
    {
        mixGestureSnapshot[(size_t) s] = slotParams[(size_t) s].mix->load();
        if (countsAsLoaded(getSlotStatus(s).state))
            if (auto* p = params.getParameter(slotParamId(s, "mix"))) p->beginChangeGesture();
    }
}

void AmpsurdProcessor::setMixLinked(int slot, float v)
{
    std::vector<int> others;
    for (int s = 0; s < kNumSlots; ++s)
        if (s != slot && mixesWith(s))
            others.push_back(s);
    if (others.empty())
        v = 100.0f;
    v = juce::jlimit(0.0f, 100.0f, v);

    const bool snap = mixGestureSlot == slot;
    float sum = 0.0f;
    for (int s : others) sum += snap ? mixGestureSnapshot[(size_t) s] : slotParams[(size_t) s].mix->load();
    for (int s : others)
    {
        const float base = snap ? mixGestureSnapshot[(size_t) s] : slotParams[(size_t) s].mix->load();
        const float nv = sum > 0.01f ? base * (100.0f - v) / sum : (100.0f - v) / (float) others.size();
        setParam(params, slotParamId(s, "mix"), nv);
    }
    setParam(params, slotParamId(slot, "mix"), v);
}

void AmpsurdProcessor::endMixGesture(int)
{
    for (int s = 0; s < kNumSlots; ++s)
        if (countsAsLoaded(getSlotStatus(s).state))
            if (auto* p = params.getParameter(slotParamId(s, "mix"))) p->endChangeGesture();
    mixGestureSlot = -1;
}

// ---------------------------------------------------------------------------------------------
// Create Frankenstein (message thread)
// ---------------------------------------------------------------------------------------------
void AmpsurdProcessor::getInputSpectrum(std::vector<float>& magsDb, double& binHz)
{
    constexpr int N = 4096;
    static const ampsurd::Fft fft(N);
    std::vector<double> re((size_t) N), im((size_t) N, 0.0);
    const int w = specWrite.load(std::memory_order_acquire);
    for (int i = 0; i < N; ++i)
    {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (N - 1));
        re[(size_t) i] = hann * specRing[(size_t) ((w - N + i) & (kSpecRing - 1))];
    }
    fft.forward(re.data(), im.data());
    magsDb.resize((size_t) N / 2);
    for (int k = 0; k < N / 2; ++k)
        magsDb[(size_t) k] = (float) (10.0 * std::log10(re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k] + 1e-20) - 20.0 * std::log10(N / 4.0));
    binHz = currentSampleRate.load() / N;
}

ampsurd::FxSettings AmpsurdProcessor::readFx() const noexcept
{
    ampsurd::FxSettings f;
    const double bpm = tempoBpm.load(std::memory_order_relaxed);
    for (int d = 0; d < 2; ++d)
    {
        const auto& dp = delayParams[(size_t) d];
        auto& ds = f.delay[(size_t) d];
        ds.on = dp.on->load() > 0.5f;
        ds.timeMs = dp.sync->load() > 0.5f ? 60000.0 / bpm * delayNoteBeats((int) dp.note->load()) : (double) dp.time->load();
        ds.feedback = dp.feedback->load() / 100.0f;
        ds.level = dp.level->load() / 100.0f;
        ds.toneHz = dp.tone->load();
        ds.pingPong = dp.pingPong->load() > 0.5f;
    }
    f.reverb.on = revOn->load() > 0.5f;
    f.reverb.type = (ampsurd::ReverbType) juce::jlimit(0, ampsurd::kNumReverbTypes - 1, (int) revType->load());
    f.reverb.decaySeconds = revDecay->load();
    f.reverb.preDelayMs = revPre->load();
    f.reverb.toneHz = revTone->load();
    f.reverb.level = revLevel->load() / 100.0f;
    f.flanger.on = flOn->load() > 0.5f;
    f.flanger.rateHz = flRate->load();
    f.flanger.depth = flDepth->load() / 100.0f;
    f.flanger.feedback = flFeedback->load() / 100.0f;
    f.flanger.mix = flMix->load() / 100.0f;
    return f;
}

#if AMPSURD_ROOT
ampsurd::RootSettings AmpsurdProcessor::readRoot() const noexcept
{
    ampsurd::RootSettings r;
    r.on = rootParams.on->load() > 0.5f;
    r.mode = (ampsurd::RootMode) juce::jlimit(0, 2, (int) rootParams.mode->load());
    r.levelDb = rootParams.level->load();
    r.freqHz = rootParams.freq->load();
    r.growl = rootParams.growl->load() / 100.0;
    r.tightHz = rootParams.tight->load();
    r.comp = rootParams.comp->load() / 100.0;
    r.subDb = rootParams.sub->load();
    r.lowDb = rootParams.low->load();
    r.lowMidDb = rootParams.lowMid->load();
    r.invert = rootParams.invert->load() > 0.5f;
    return r;
}
#endif

#if AMPSURD_DOUBLER
ampsurd::DoublerSettings AmpsurdProcessor::readDoubler() const noexcept
{
    ampsurd::DoublerSettings d;
    d.on = dblParams.on->load() > 0.5f;
    d.voices = 1 + juce::jlimit(0, 2, (int) dblParams.voices->load());
    d.tight = dblParams.tight->load() / 100.0;
    d.levelDb = dblParams.level->load();
    d.width = dblParams.width->load() / 100.0;
    d.lowMonoHz = dblParams.lowMono->load() < 20.0f ? 0.0 : dblParams.lowMono->load();
    d.character = dblParams.character->load() / 100.0;
    return d;
}
#endif

bool AmpsurdProcessor::isFxActive(int which) const
{
    switch (which)
    {
        case 0: return delayParams[0].on->load() > 0.5f;
        case 1: return delayParams[1].on->load() > 0.5f;
        case 2: return revOn->load() > 0.5f;
        case 3: return flOn->load() > 0.5f;
        case 4: return pitchOnParam->load() > 0.5f;
        default: return false;
    }
}

ampsurd::FrankensteinSettings AmpsurdProcessor::readFrankenstein() const noexcept
{
    ampsurd::FrankensteinSettings f;
    f.enabled = frankOnParam->load() > 0.5f;
    f.beforeAmps = frankModeParam->load() > 0.5f;
    f.sections = juce::jlimit(2, 5, (int) std::lround(frankSectionsParam->load()));
    f.width = juce::jlimit(0.0f, 0.9f, frankWidthParam->load() / 100.0f);
    for (int k = 0; k < 5; ++k) f.amp[(size_t) k] = juce::jlimit(0, 4, (int) std::lround(frankAmpParam[(size_t) k]->load()));
    for (int k = 0; k < 4; ++k) f.dividerHz[(size_t) k] = frankDivParam[(size_t) k]->load();
    return f;
}

ampsurd::FrankensteinLayout AmpsurdProcessor::getFrankensteinLayout() const
{
    std::array<bool, 5> audible {};
    for (int s = 0; s < kNumSlots; ++s) audible[(size_t) s] = isSlotAudible(s);
    return ampsurd::computeFrankensteinLayout(readFrankenstein(), audible);
}

void AmpsurdProcessor::setFrankensteinSections(int sections)
{
    sections = juce::jlimit(2, 5, sections);
    // amps: keep existing assignments, give new sections loaded amps that are not used yet
    std::vector<int> loadedSlots;
    for (int s = 0; s < kNumSlots; ++s)
        if (countsAsLoaded(getSlotStatus(s).state)) loadedSlots.push_back(s);
    const int oldK = juce::jlimit(2, 5, (int) std::lround(frankSectionsParam->load()));
    for (int k = oldK; k < sections; ++k)
    {
        int pick = k % kNumSlots;
        for (int s : loadedSlots)
        {
            bool used = false;
            for (int j = 0; j < k; ++j) used = used || (int) std::lround(frankAmpParam[(size_t) j]->load()) == s;
            if (!used) { pick = s; break; }
        }
        setParam(params, "frankAmp" + juce::String(k + 1), (float) pick);
    }
    // dividers evenly spaced (log) between 120 Hz and 4.5 kHz
    for (int j = 0; j < sections - 1; ++j)
    {
        const double t = (j + 1.0) / sections;
        setParam(params, "frankDiv" + juce::String(j + 1), (float) (120.0 * std::pow(4500.0 / 120.0, t)));
    }
    setParam(params, "frankSections", (float) sections);
}

void AmpsurdProcessor::setFrankenstein(bool on)
{
    if (on)
    {
        // first use (or the assigned amps are gone): one section per loaded amp, in slot order
        std::vector<int> loadedSlots;
        for (int s = 0; s < kNumSlots; ++s)
            if (countsAsLoaded(getSlotStatus(s).state)) loadedSlots.push_back(s);
        const auto f = readFrankenstein();
        bool valid = true;
        for (int k = 0; k < f.sections; ++k)
            valid = valid && countsAsLoaded(getSlotStatus(f.amp[(size_t) k]).state);
        bool distinct = false;
        for (int k = 1; k < f.sections; ++k) distinct = distinct || f.amp[(size_t) k] != f.amp[0];
        if (!valid || !distinct)
        {
            const int K = juce::jlimit(2, 5, (int) loadedSlots.size());
            for (int k = 0; k < K; ++k)
                setParam(params, "frankAmp" + juce::String(k + 1),
                         (float) (loadedSlots.empty() ? k : loadedSlots[(size_t) (k % (int) loadedSlots.size())]));
            setParam(params, "frankSections", (float) K);
            setFrankensteinSections(K);
        }
    }
    setParam(params, "frankOn", on ? 1.0f : 0.0f);
}

// ---------------------------------------------------------------------------------------------
// Housekeeping (message thread)
// ---------------------------------------------------------------------------------------------
void AmpsurdProcessor::updateLatency()
{
    int resampler = 0;
    for (int s = 0; s < kNumSlots; ++s)
    {
        int path = engine.getSlot(s).getLatencySamples();
        for (int k = 0; k < kNumPedals; ++k)
            if (getPedalStatus(s, k).state == SlotState::loaded && slotParams[(size_t) s].pedalOn[(size_t) k]->load() > 0.5f)
                path += engine.getPedal(s, k).getLatencySamples(); // a pedal at another sample rate is resampled too
        resampler = std::max(resampler, path);
    }
    const double sr = currentSampleRate.load();
    const int total = resampler + ampsurd::PathAligner::kBaseLatency + (int) std::lround(kReserveMs * 0.001 * sr)
                      + limiter.getLatencySamples()
                      + (pitchOnParam->load() > 0.5f ? (int) std::lround(ampsurd::PitchShifter::kNominalLatencyMs * 0.001 * sr) : 0);
    if (totalLatency.exchange(total) != total)
        setLatencySamples(total);
}

void AmpsurdProcessor::timerCallback()
{
    drainEventLog();
    if (needsRecovery.exchange(false))
    {
        // NaN / inf inside the signal chain: prepare everything again (fresh amp, filter and effect states)
        suspendProcessing(true);
        prepareToPlay(currentSampleRate.load(), currentMaxBlock);
        suspendProcessing(false);
        recoveries.fetch_add(1);
    }
    for (int s = 0; s < kNumSlots; ++s)
    {
        engine.getSlot(s).collectGarbage();
        for (int k = 0; k < kNumPedals; ++k) engine.getPedal(s, k).collectGarbage();
    }

    for (int s = 0; s < kNumSlots; ++s) engine.getToneStack(s).collectGarbage();
    updateToneStacks();
    updateCovariance(false);
    updateLatency();
    handleMidiEvents();
    trackUndo(juce::Time::getMillisecondCounterHiRes());

    // MUTE (button or automation): fader to 0 %, the others to their real shares; un-mute restores
    if (resyncMute.exchange(false))
        for (int sl = 0; sl < kNumSlots; ++sl) lastMute[(size_t) sl] = isSlotMuted(sl);
    for (int sl = 0; sl < kNumSlots; ++sl)
    {
        const bool m = isSlotMuted(sl);
        if (m == lastMute[(size_t) sl]) continue;
        lastMute[(size_t) sl] = m;
        applyMuteToFaders(sl, m);
    }

    // ECO switched: reload the capture with the Lite (or full) model - crossfaded like any swap
    for (int sl = 0; sl < kNumSlots; ++sl)
    {
        const bool eco = slotParams[(size_t) sl].eco->load() > 0.5f;
        if (eco == lastEco[(size_t) sl]) continue;
        lastEco[(size_t) sl] = eco;
        const auto st = getSlotStatus(sl);
        if (st.state == SlotState::loaded && st.canEco)
            loadCapture(sl, juce::File(st.path));
    }

    // IR ON / BYPASS changes how the slot sounds: re-measure it (level match, alignment, mix law)
    for (int sl = 0; sl < kNumSlots; ++sl)
    {
        engine.getIrSlot(sl).collectGarbage();
        const bool on = isIrOn(sl);
        if (on != lastIrOn[(size_t) sl])
        {
            lastIrOn[(size_t) sl] = on;
            if (getIrStatus(sl).state == IrState::loaded)
            {
                auto aliveFlag = alive;
                loaderPool.addJob([this, sl, aliveFlag] {
                    if (aliveFlag->load()) measureSlotJob(sl);
                });
            }
        }
    }

    updateDriveCompensation();

    // Captures are non-linear: when the input gain changes, re-measure (debounced).
    const float g = inputParam->load();
    const double now = juce::Time::getMillisecondCounterHiRes();
    // the same for an amp's own INPUT (alignment and mix law follow its new sound)
    for (int sl = 0; sl < kNumSlots; ++sl)
    {
        const float dv = slotParams[(size_t) sl].drive->load();
        if (std::abs(dv - lastSeenDrive[(size_t) sl]) > 0.01f)
        {
            lastSeenDrive[(size_t) sl] = dv;
            driveChangedAt[(size_t) sl] = now;
        }
        if (getSlotStatus(sl).state == SlotState::loaded && std::abs(dv - derived[(size_t) sl].measuredDriveDb.load()) > 0.5
            && std::abs(dv - queuedDrive[(size_t) sl]) > 0.01f && now - driveChangedAt[(size_t) sl] > 400.0)
        {
            queuedDrive[(size_t) sl] = dv; // queued once
            needsRemeasure.store(true);
        }
        // AMPSURD amp: each GAIN position compresses differently, so the INPUT -> loudness curve follows GAIN
        const auto st = getSlotStatus(sl);
        const float ag = slotParams[(size_t) sl].ampGain->load();
        if (std::abs(ag - lastSeenAmpGain[(size_t) sl]) > 0.001f)
        {
            lastSeenAmpGain[(size_t) sl] = ag;
            ampGainChangedAt[(size_t) sl] = now;
        }
        if (st.state == SlotState::loaded && st.isAmp && std::abs(ag - queuedAmpGain[(size_t) sl]) > 0.001f
            && now - ampGainChangedAt[(size_t) sl] > 400.0)
        {
            bool stale = false, ready = false;
            {
                std::lock_guard<std::mutex> al(analysisMutex);
                const auto& c = driveCurves[(size_t) sl];
                ready = c.valid;
                stale = c.valid && std::abs(c.ampGain - ag) >= 0.25;
            }
            if (ready) queuedAmpGain[(size_t) sl] = ag; // not ready: a curve is being made, look again next tick
            if (stale) requestDriveCurve(sl);
        }
    }
    if (std::abs(g - lastSeenInputGainDb) > 0.01f)
    {
        lastSeenInputGainDb = g;
        inputGainChangedAt = now;
    }
    bool anyMeasured = false;
    double measuredGain = 0.0;
    {
        std::lock_guard<std::mutex> al(analysisMutex);
        for (auto& m : measurements) anyMeasured = anyMeasured || m != nullptr;
        measuredGain = measuredInputGainDb;
    }
    if (anyMeasured && std::abs(g - measuredGain) > 0.5 && now - inputGainChangedAt > 400.0)
        needsRemeasure.store(true);
    if (anyMeasured && calibrationVersion.load() != measuredCalibrationVersion.load())
    {
        measuredCalibrationVersion.store(calibrationVersion.load()); // queued once
        needsRemeasure.store(true);
    }

    // a pedal switched on / off or its level moved: measure that amp again once it has settled (0.3 s)
    for (int sl = 0; sl < kNumSlots; ++sl)
    {
        const auto sig = pedalSignature(sl);
        if (lastPedalSig[(size_t) sl].isEmpty()) lastPedalSig[(size_t) sl] = sig; // first look
        if (sig != lastPedalSig[(size_t) sl])
        {
            if (pedalSigChangedAt[(size_t) sl] <= 0.0) pedalSigChangedAt[(size_t) sl] = now;
            else if (now - pedalSigChangedAt[(size_t) sl] > 300.0)
            {
                lastPedalSig[(size_t) sl] = sig;
                pedalSigChangedAt[(size_t) sl] = 0.0;
                requestSlotRemeasure(sl);
                updateLatency();
            }
        }
        else pedalSigChangedAt[(size_t) sl] = 0.0;
    }

    if (needsRemeasure.exchange(false))
    {
        const int gen = ++remeasureGeneration;
        auto aliveFlag = alive;
        loaderPool.addJob([this, gen, aliveFlag] {
            if (aliveFlag->load()) remeasureJob(gen);
        });
        std::lock_guard<std::mutex> al(analysisMutex);
        measuredInputGainDb = g; // do not queue the same re-measurement twice
    }
}

// ---------------------------------------------------------------------------------------------
// State and presets: one preset = the complete rig
// ---------------------------------------------------------------------------------------------
juce::ValueTree AmpsurdProcessor::createStateTree() const
{
    auto state = const_cast<juce::AudioProcessorValueTreeState&>(params).copyState();
    if (auto old = state.getChildWithName(kSlotsId); old.isValid())
        state.removeChild(old, nullptr);
    state.setProperty(kFormatId, kFormatVersion, nullptr);
    state.setProperty(kPresetNameId, getCurrentPresetName(), nullptr);

    juce::ValueTree slots(kSlotsId);
    for (int s = 0; s < kNumSlots; ++s)
    {
        juce::ValueTree t(kSlotId);
        t.setProperty(kIndexId, s + 1, nullptr);
        // Only a REFERENCE to the capture is stored, never the capture data itself.
        t.setProperty(kPathId, getSlotStatus(s).path, nullptr);
        t.setProperty("irPath", getIrStatus(s).path, nullptr); // reference to the cabinet IR file (or empty)
        for (int k = 0; k < kNumPedals; ++k)
            if (getPedalStatus(s, k).path.isNotEmpty())
                t.setProperty("pedal" + juce::String(k + 1) + "Path", getPedalStatus(s, k).path, nullptr);
        if (mutedShare[(size_t) s] > 0.01f)
            t.setProperty("mutedShare", mutedShare[(size_t) s], nullptr); // fader position to return to on un-mute
        slots.appendChild(t, nullptr);
    }
    state.appendChild(slots, nullptr);
    return state;
}

juce::String AmpsurdProcessor::fileNameOfRef(const juce::String& ref)
{
    return ref.fromLastOccurrenceOf("/", false, false).fromLastOccurrenceOf("\\", false, false);
}

juce::File AmpsurdProcessor::resolveFileRef(const juce::String& ref, const juce::File& presetFile) const
{
    // A preset refers to captures / IRs by full path, or (in a preset pack) by file name only.
    // Not found at the path: look next to the preset, then in the AMPSURD folders (packs) by file name.
    if (juce::File::isAbsolutePath(ref))
        if (const juce::File f(ref); f.existsAsFile()) return f;
    const auto name = fileNameOfRef(ref);
    if (name.isEmpty()) return {};
    if (presetFile != juce::File())
        if (const auto sib = presetFile.getSiblingFile(name); sib.existsAsFile()) return sib;
    for (const auto& dir : { getPacksFolder(), getPresetFolder().getParentDirectory() })
        if (dir.isDirectory())
            for (const auto& entry : juce::RangedDirectoryIterator(dir, true, name, juce::File::findFiles))
                return entry.getFile();
    return juce::File::isAbsolutePath(ref) ? juce::File(ref) : juce::File();
}

void AmpsurdProcessor::restoreState(const juce::ValueTree& tree, const juce::File& presetFile)
{
    if (!tree.isValid() || tree.getType() != params.state.getType())
        return;

    auto paramsOnly = tree.createCopy();
    if (auto old = paramsOnly.getChildWithName(kSlotsId); old.isValid())
        paramsOnly.removeChild(old, nullptr);
    // Parameters that did not exist when the preset was saved (e.g. Global EQ in older presets)
    // are set to their defaults, so a preset always sounds the same, whatever was loaded before.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (!paramsOnly.getChildWithProperty("id", rp->paramID).isValid())
            {
                juce::ValueTree child("PARAM");
                child.setProperty("id", rp->paramID, nullptr);
                child.setProperty("value", rp->convertFrom0to1(rp->getDefaultValue()), nullptr);
                paramsOnly.appendChild(child, nullptr);
            }
    params.replaceState(paramsOnly);
    resyncMute.store(true); // the loaded MUTE states are taken as they are (no fader moves)

    {
        const juce::ScopedLock sl(statusLock);
        currentPresetName = tree.getProperty(kPresetNameId, "Init").toString();
    }

    const auto slots = tree.getChildWithName(kSlotsId);
    for (int s = 0; s < kNumSlots; ++s)
    {
        juce::String path, irPath;
        std::array<juce::String, kNumPedals> pedalPaths;
        float share = 0.0f;
        for (const auto& t : slots)
            if ((int) t.getProperty(kIndexId) == s + 1)
            {
                path = t.getProperty(kPathId).toString();
                irPath = t.getProperty("irPath").toString();
                share = (float) (double) t.getProperty("mutedShare", 0.0);
                for (int k = 0; k < kNumPedals; ++k) pedalPaths[(size_t) k] = t.getProperty("pedal" + juce::String(k + 1) + "Path").toString();
            }
        mutedShare[(size_t) s] = share;

        // pedal captures first (queued before the amp on the same loader thread, so it is measured with them)
        for (int k = 0; k < kNumPedals; ++k)
        {
            const auto& pp = pedalPaths[(size_t) k];
            const auto cur = getPedalStatus(s, k);
            if (pp.isEmpty()) { if (cur.state != SlotState::empty) removePedal(s, k); continue; }
            const juce::File f = resolveFileRef(pp, presetFile);
            if (f.existsAsFile() && (cur.state == SlotState::loaded || cur.state == SlotState::loading) && juce::File(cur.path) == f) continue;
            if (f.existsAsFile()) loadPedal(s, k, f);
            else
            {
                removePedal(s, k);
                setPedalStatus(s, k, { SlotState::missing, pp, fileNameOfRef(pp).upToLastOccurrenceOf(".", false, false), "Pedal file not found:\n" + pp });
            }
        }

        // cabinet IR (queued after the capture on the same loader thread)
        auto restoreIr = [this, s, irPath, presetFile] {
            if (irPath.isEmpty())
            {
                if (getIrStatus(s).state != IrState::none) removeIr(s);
                return;
            }
            {
                ampsurd::CabPoint cp;
                if (ampsurd::CabIrModel::fromRef(irPath.toStdString(), cp)) // an AMPSURD IR: calculated, no file
                {
                    const auto cur = getIrStatus(s);
                    if ((cur.state == IrState::loaded || cur.state == IrState::loading) && cur.path == irPath) return;
                    loadCabIr(s, cp);
                    return;
                }
            }
            const juce::File f = resolveFileRef(irPath, presetFile);
            const auto cur = getIrStatus(s);
            if (f.existsAsFile() && (cur.state == IrState::loaded || cur.state == IrState::loading) && juce::File(cur.path) == f)
                return; // the same IR is already there (undo, A/B, presets sharing an IR): keep it
            if (f.existsAsFile())
                loadIr(s, f);
            else
            {
                removeIr(s);
                setIrStatus(s, { IrState::missing, irPath, fileNameOfRef(irPath).upToLastOccurrenceOf(".", false, false), "IR file not found:\n" + irPath });
            }
        };

        if (path.isEmpty())
        {
            if (getSlotStatus(s).state != SlotState::empty)
            {
                ++slotGeneration[(size_t) s];
                setSlotStatus(s, {});
                auto aliveFlag = alive;
                const int gen = slotGeneration[(size_t) s].load();
                loaderPool.addJob([this, s, gen, aliveFlag] {
                    if (!aliveFlag->load()) return;
                    std::lock_guard<std::mutex> lock(configMutex);
                    if (gen != slotGeneration[(size_t) s].load()) return;
                    engine.getSlot(s).submit(CaptureModel::makeEmpty(currentSampleRate.load(), currentMaxBlock));
                    std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> snapshot;
                    {
                        std::lock_guard<std::mutex> al(analysisMutex);
                        measurements[(size_t) s] = nullptr;
                        snapshot = measurements;
                    }
                    applyRig(CaptureAnalyzer::analyseRig(snapshot));
                });
            }
            restoreIr();
            continue;
        }

        const juce::File file = resolveFileRef(path, presetFile);

        if (file.existsAsFile())
        {
            const auto cur = getSlotStatus(s);
            if ((cur.state == SlotState::loaded || cur.state == SlotState::loading) && juce::File(cur.path) == file)
            {
                restoreIr(); // the same capture is already loaded: keep it (ECO / INPUT changes are picked up by the timer)
                continue;
            }
            const int gen = ++slotGeneration[(size_t) s];
            setSlotStatus(s, { SlotState::loading, file.getFullPathName(), file.getFileNameWithoutExtension(), "Loading..." });
            auto aliveFlag = alive;
            loaderPool.addJob([this, s, file, gen, aliveFlag] {
                if (aliveFlag->load()) loadJob(s, file, gen, false);
            });
        }
        else
        {
            ++slotGeneration[(size_t) s];
            setSlotStatus(s, { SlotState::missing, path, fileNameOfRef(path).upToLastOccurrenceOf(".", false, false),
                               "Capture file not found:\n" + path });
            auto aliveFlag = alive;
            const int gen = slotGeneration[(size_t) s].load();
            loaderPool.addJob([this, s, gen, aliveFlag] {
                if (!aliveFlag->load()) return;
                std::lock_guard<std::mutex> lock(configMutex);
                if (gen != slotGeneration[(size_t) s].load()) return;
                engine.getSlot(s).submit(CaptureModel::makeEmpty(currentSampleRate.load(), currentMaxBlock));
                std::array<std::shared_ptr<const CaptureAnalyzer::Measurement>, kNumSlots> snapshot;
                {
                    std::lock_guard<std::mutex> al(analysisMutex);
                    measurements[(size_t) s] = nullptr;
                    snapshot = measurements;
                }
                applyRig(CaptureAnalyzer::analyseRig(snapshot));
            });
        }
        restoreIr();
    }
}

void AmpsurdProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = createStateTree();
    state.setProperty("presetFile", getCurrentPresetFile().getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AmpsurdProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto tree = juce::ValueTree::fromXml(*xml);
    const juce::File presetFile(tree.getProperty("presetFile").toString());
    {
        const juce::ScopedLock sl(statusLock);
        currentPresetFile = presetFile;
    }
    restoreState(tree, presetFile);
    resetUndo.store(true); // a project / session was opened: undo starts from here
}

juce::File AmpsurdProcessor::getPresetFolder() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("AMPSURD").getChildFile("Presets");
}

juce::Array<juce::File> AmpsurdProcessor::listPresets() const
{
    auto files = getPresetFolder().findChildFiles(juce::File::findFiles, false, "*.ampsurd");
    files.sort();
    return files;
}

bool AmpsurdProcessor::savePreset(const juce::File& fileIn)
{
    auto file = fileIn.withFileExtension(".ampsurd");
    {
        const juce::ScopedLock sl(statusLock);
        currentPresetName = file.getFileNameWithoutExtension();
        currentPresetFile = file;
    }
    auto state = createStateTree();
    auto xml = state.createXml();
    if (!xml) return false;
    file.getParentDirectory().createDirectory();
    return xml->writeTo(file);
}

bool AmpsurdProcessor::loadPreset(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (!xml) return false;
    auto tree = juce::ValueTree::fromXml(*xml);
    if (tree.getType() != params.state.getType()) return false;
    {
        const juce::ScopedLock sl(statusLock);
        currentPresetFile = file;
    }
    tree.setProperty(kPresetNameId, file.getFileNameWithoutExtension(), nullptr);
    restoreState(tree, file);
    return true;
}

void AmpsurdProcessor::loadInitPreset()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            rp->setValueNotifyingHost(rp->getDefaultValue());
    for (int s = 0; s < kNumSlots; ++s)
        if (getSlotStatus(s).state != SlotState::empty)
            unloadCapture(s);
    mutedShare = {};
    resyncMute.store(true);
    const juce::ScopedLock sl(statusLock);
    currentPresetName = "Init";
    currentPresetFile = juce::File();
}

juce::String AmpsurdProcessor::getCurrentPresetName() const
{
    const juce::ScopedLock sl(statusLock);
    return currentPresetName;
}

juce::File AmpsurdProcessor::getCurrentPresetFile() const
{
    const juce::ScopedLock sl(statusLock);
    return currentPresetFile;
}

juce::AudioProcessorEditor* AmpsurdProcessor::createEditor()
{
    return new AmpsurdEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmpsurdProcessor();
}

// ---------------------------------------------------------------------------------------------
// The log (EventLog.h): dropouts get what was going on at that moment
// ---------------------------------------------------------------------------------------------
juce::String AmpsurdProcessor::describeDrop(const ampsurd::EventLog::Drop& d)
{
    juce::String t = "AMPSURD needed " + juce::String(d.workMs, 1) + " ms for a block that lasts " + juce::String(d.budgetMs, 1) + " ms ("
                   + juce::String(d.samples) + " samples)";
    const auto busy = eventLog.activitiesAround(d.atMs - 50.0, d.atMs + d.workMs + 50.0);
    if (busy.isNotEmpty()) t << " - at the same time: " << busy;
    juce::StringArray twoCaptures;
    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto st = getSlotStatus(s);
        if (!st.isAmp || st.state != SlotState::loaded || st.gainSteps.size() < 2) continue;
        const double g = slotParams[(size_t) s].ampGain->load();
        bool onStep = false;
        for (double k : st.gainSteps) onStep = onStep || std::abs(g - k) < 0.01;
        if (!onStep) twoCaptures.add("amp " + juce::String(s + 1));
    }
    if (!twoCaptures.isEmpty())
        t << " - GAIN between two recorded positions on " << twoCaptures.joinIntoString(", ") << " (two captures play there)";
    t << " - CPU " << juce::roundToInt(getCpuLoadPercent()) << " %";
    // how much of that time AMPSURD really ran (owner 2026-11-07): tells "the computer paused AMPSURD" from
    // "AMPSURD was too slow"
    const bool known = d.ranMs >= 0.0f;
    const float waited = juce::jmax(0.0f, d.helperWaitMs);
    const float ran = known ? juce::jmax(0.0f, d.ranMs - waited) : 0.0f; // the helper wait is spinning, not work
    const bool paused = known && ran < 0.5f * d.workMs && ran <= d.budgetMs;
    const bool helperPaused = waited > 0.5f * d.workMs;
    if (known)
    {
        t << " - AMPSURD itself worked " << juce::String(ran, 1) << " ms of it";
        if (waited >= 0.1f) t << ", waited " << juce::String(waited, 1) << " ms for a helper core";
    }
    // which part took the time (owner 2026-11-09: "which part took how long")
    if (d.pathsMs >= 0.0f)
    {
        struct Part { juce::String what; float ms; };
        std::vector<Part> parts;
        int amps = 0;
        float ampSum = 0.0f;
        for (int s = 0; s < 5 && s < kNumSlots; ++s)
        {
            if (d.slotMs[s][0] < 0.0f) continue;
            ++amps;
            const auto st = getSlotStatus(s);
            const juce::String a = "amp " + juce::String(s + 1) + " ";
            const juce::String capture = st.isAmp && st.gainSteps.size() >= 2 ? "GAIN captures" : "capture";
            const juce::String names[4] = { "pedals", capture, "tone + cabinet IR", "EQ + alignment" };
            for (int k = 0; k < 4; ++k)
            {
                parts.push_back({ a + names[k], d.slotMs[s][k] });
                ampSum += d.slotMs[s][k];
            }
        }
        parts.push_back({ "mixing + global EQ", d.mixMs });
        parts.push_back({ "input + PITCH", d.inputMs });
        parts.push_back({ "effects (gate, delays, reverb ...)", d.effectsMs });
        parts.push_back({ "output (limiter, player)", d.outputMs });
        std::stable_sort(parts.begin(), parts.end(), [](const Part& x, const Part& y) { return x.ms > y.ms; });
        juce::StringArray list;
        for (const auto& pt : parts)
            if (pt.ms >= 0.1f && list.size() < 5) list.add(pt.what + " " + juce::String(pt.ms, 1) + " ms");
        if (!list.isEmpty()) t << " - where the time went: " << list.joinIntoString(", ");
        if (amps > 1 && ampSum > 1.2f * d.pathsMs && d.pathsMs >= 0.1f)
            t << " (the amps ran side by side on several cores: " << juce::String(d.pathsMs, 1) << " ms for all of them)";
        const float outside = d.workMs - (d.inputMs + d.pathsMs + d.mixMs + d.effectsMs + d.outputMs);
        if (outside >= 0.5f && outside >= 0.1f * d.workMs)
            t << " - " << juce::String(outside, 1) << " ms outside these parts (settings, MIDI, meters)";
    }
    if (d.grace) t << " - not counted (right after starting the audio or loading an amp)";
    else if (helperPaused)
        t << ". Likely cause: the computer paused one of AMPSURD's helper threads (another program, power saving, a driver)"
             " - not AMPSURD's own work";
    else if (paused)
        t << ". Likely cause: the computer paused AMPSURD (another program, power saving, a driver, USB) - not AMPSURD's own work";
    else if (busy.isNotEmpty()) t << ". Likely cause: that background work took the CPU from the audio (once it ends, it should stop)";
    else if (known && ran > 3.0f * d.budgetMs)
        // the audio thread was busy the whole time (not paused): a sudden spike, not a steady overload. On Windows a driver
        // interrupting the thread is counted as the thread's own time, so this can still be a driver (owner 2026-11-09:
        // "my PC froze" at that moment)
        t << ". A sudden spike - AMPSURD's audio thread was busy that long: its own work, or a driver interrupting it"
             " (if the whole computer froze at that moment too, it was a driver)";
    else if (d.budgetMs < 3.0f) t << ". Likely cause: a very small buffer - a larger buffer size (e.g. 256) gives AMPSURD more time";
    else if (!known && d.workMs > 3.0f * d.budgetMs) t << ". Likely cause: the computer paused AMPSURD (another program, power saving, a driver)";
    else t << ". Likely cause: too much for this CPU at this buffer size - ECO on the amps, fewer amps or a larger buffer";
    return t;
}

void AmpsurdProcessor::drainEventLog()
{
    if (!startLogged)
    {
        startLogged = true;
        eventLog.add("START", juce::String("AMPSURD " JucePlugin_VersionString) + " - "
                                  + (wrapperType == wrapperType_Standalone ? juce::String("standalone app")
                                                                            : "plugin (" + juce::String(getWrapperTypeDescription(wrapperType)) + ") in "
                                                                                  + juce::PluginHostType().getHostDescription())
                                  + " - " + juce::SystemStats::getOperatingSystemName() + " - " + lastAudioSetup);
    }
    eventLog.drain([this](const ampsurd::EventLog::Drop& d) { return describeDrop(d); });
    // the audio driver's own missed buffers (standalone): AMPSURD was in time, the driver / computer was not
    if (deviceXRuns)
    {
        const int n = deviceXRuns();
        if (lastDriverXRuns >= 0 && n > lastDriverXRuns)
        {
            const double sr = currentSampleRate.load();
            const float worst = recentWorkMs.load();
            const double budget = sr > 0.0 ? 1000.0 * currentMaxBlock / sr : 0.0;
            const auto busy = eventLog.activitiesAround(juce::Time::getMillisecondCounterHiRes() - 1500.0, juce::Time::getMillisecondCounterHiRes());
            juce::String t = "the audio driver missed " + juce::String(n - lastDriverXRuns) + " buffer(s)";
            if (worst <= budget) t << " - AMPSURD was in time (longest block " << juce::String(worst, 1) << " of " << juce::String(budget, 1) << " ms)";
            if (busy.isNotEmpty()) t << " - at the same time: " << busy;
            t << ". Likely cause: the driver or the computer (other programs, power saving, USB), or a buffer that is too small";
            if (juce::Time::getMillisecondCounter() < dropGraceUntil.load()) t << " - not counted (right after starting / loading)";
            eventLog.add("DROPOUT", t);
        }
        lastDriverXRuns = n;
    }
    recentWorkMs.store(0.0f);
}

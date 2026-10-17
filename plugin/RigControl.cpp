// AmpsurdProcessor, part 2: undo / redo and A/B compare, MIDI footswitches / controllers, preset packs.
// Everything here runs on the message thread, except the MIDI queue written by processBlock().

#include "PluginProcessor.h"

#include <cstring>

namespace
{
const juce::Identifier kSlots { "SLOTS" }, kPath { "path" }, kIrPath { "irPath" };

void setParamValue(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "AMPSURD";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

constexpr int kMaxUndo = 50;
constexpr double kSettleMs = 400.0;
} // namespace

// =============================================================================================
// Undo / redo, A/B
// =============================================================================================
juce::uint64 AmpsurdProcessor::stateHash() const
{
    // FNV-1a over every parameter value and the capture / IR files: cheap enough for 30 Hz
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h](const void* data, size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    for (auto* p : getParameters())
    {
        const float v = p->getValue();
        mix(&v, sizeof(v));
    }
    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto a = getSlotStatus(s).path.toStdString(), b = getIrStatus(s).path.toStdString();
        mix(a.data(), a.size());
        mix("|", 1);
        mix(b.data(), b.size());
        mix("|", 1);
    }
    return h;
}

void AmpsurdProcessor::trackUndo(double now)
{
    if (resetUndo.exchange(false))
    {
        undoStack.clear();
        redoStack.clear();
        committedState = {};
    }
    const auto h = stateHash();
    if (!committedState.isValid())
    {
        committedState = createStateTree();
        committedHash = pendingHash = h;
        return;
    }
    if (h == committedHash) { pendingHash = h; return; }
    if (h != pendingHash) { pendingHash = h; pendingSince = now; return; }
    if (now - pendingSince < kSettleMs || mixGestureSlot >= 0) return; // still moving

    undoStack.push_back(committedState);
    if ((int) undoStack.size() > kMaxUndo) undoStack.erase(undoStack.begin());
    redoStack.clear();
    committedState = createStateTree();
    committedHash = h;
}

void AmpsurdProcessor::applySnapshot(const juce::ValueTree& t)
{
    restoreState(t, getCurrentPresetFile());
    committedState = t.createCopy();
    committedHash = pendingHash = stateHash();
}

void AmpsurdProcessor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back(createStateTree());
    const auto t = undoStack.back();
    undoStack.pop_back();
    applySnapshot(t);
}

void AmpsurdProcessor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back(createStateTree());
    const auto t = redoStack.back();
    redoStack.pop_back();
    applySnapshot(t);
}

void AmpsurdProcessor::selectAb(int which)
{
    which = juce::jlimit(0, 1, which);
    if (which == abCurrent) return;
    abStates[(size_t) abCurrent] = createStateTree();
    abCurrent = which;
    if (!abStates[(size_t) which].isValid()) // first visit: starts as a copy of the other rig
        abStates[(size_t) which] = abStates[(size_t) (1 - which)].createCopy();
    restoreState(abStates[(size_t) which], getCurrentPresetFile()); // becomes an undo step via trackUndo
}

void AmpsurdProcessor::copyAbToOther()
{
    abStates[(size_t) (1 - abCurrent)] = createStateTree();
}

// =============================================================================================
// Moving amps between slots
// =============================================================================================
void AmpsurdProcessor::swapSlots(int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= kNumSlots || b >= kNumSlots) return;
    auto t = createStateTree();
    const juce::String pa = "s" + juce::String(a + 1) + "_", pb = "s" + juce::String(b + 1) + "_";
    for (auto child : t)
    {
        if (!child.hasType("PARAM")) continue;
        const auto id = child.getProperty("id").toString();
        if (id.startsWith(pa)) child.setProperty("id", pb + id.substring(pa.length()), nullptr);
        else if (id.startsWith(pb)) child.setProperty("id", pa + id.substring(pb.length()), nullptr);
        else if (id.startsWith("frankAmp")) // Frankenstein sections keep their amp
        {
            const int v = juce::roundToInt((double) child.getProperty("value"));
            if (v == a) child.setProperty("value", b, nullptr);
            else if (v == b) child.setProperty("value", a, nullptr);
        }
    }
    if (auto slots = t.getChildWithName(kSlots); slots.isValid())
        for (auto slot : slots)
        {
            const int idx = (int) slot.getProperty("index");
            if (idx == a + 1) slot.setProperty("index", b + 1, nullptr);
            else if (idx == b + 1) slot.setProperty("index", a + 1, nullptr);
        }
    restoreState(t, getCurrentPresetFile()); // one undo step, like any other change
}

// =============================================================================================
// MIDI
// =============================================================================================
const std::vector<AmpsurdProcessor::MidiAction>& AmpsurdProcessor::midiActions()
{
    static const std::vector<MidiAction> actions = [] {
        std::vector<MidiAction> a {
            { "presetNext", "Next preset" }, { "presetPrev", "Previous preset" }, { "abSwitch", "A / B" },
            { "bypass", "Bypass" }, { "gateOn", "Gate on / off" }, { "frankOn", "Create Frankenstein on / off" },
            { "fxD1On", "Delay 1 on / off" }, { "fxD2On", "Delay 2 on / off" }, { "fxRevOn", "Reverb on / off" },
            { "fxFlOn", "Flanger on / off" }, { "pitchOn", "Pitch on / off" }, { "tunerMute", "Mute while tuning" },
        };
        for (int s = 1; s <= kNumSlots; ++s)
            a.push_back({ "s" + juce::String(s) + "_mute", "Amp " + juce::String(s) + " mute" });
        for (int s = 1; s <= kNumSlots; ++s)
            a.push_back({ "s" + juce::String(s) + "_solo", "Amp " + juce::String(s) + " solo" });
        a.push_back({ "input", "Input level (controller)", true });
        a.push_back({ "output", "Output level (controller / expression pedal)", true });
        for (int s = 1; s <= kNumSlots; ++s)
            a.push_back({ "s" + juce::String(s) + "_drive", "Amp " + juce::String(s) + " input (controller)", true });
        return a;
    }();
    return actions;
}

int AmpsurdProcessor::getMidiCcFor(const juce::String& id) const
{
    for (const auto& [cc, act] : midiMap)
        if (act == id) return cc;
    return -1;
}

void AmpsurdProcessor::startMidiLearn(const juce::String& id)
{
    midiLearnAction = id;
    midiStatus = "MIDI learn: press the footswitch / move the controller";
    midiStatusAt = juce::Time::getMillisecondCounterHiRes() + 1e9; // stays until learned / cancelled
}

void AmpsurdProcessor::clearMidiAssignments()
{
    midiMap.clear();
    midiLearnAction.clear();
    saveMidiSettings();
}

void AmpsurdProcessor::setProgramChangeOn(bool on)
{
    midiProgramChange = on;
    saveMidiSettings();
}

void AmpsurdProcessor::setFootswitchLatching(bool on)
{
    midiLatching = on;
    saveMidiSettings();
}

juce::String AmpsurdProcessor::getMidiStatus() const
{
    if (midiLearnAction.isNotEmpty())
    {
        for (const auto& a : midiActions())
            if (a.id == midiLearnAction) return "MIDI learn \"" + a.name + "\": press the footswitch / move the controller";
    }
    return juce::Time::getMillisecondCounterHiRes() - midiStatusAt < 3000.0 ? midiStatus : juce::String();
}

void AmpsurdProcessor::loadMidiSettings()
{
    juce::PropertiesFile pf(settingsOptions());
    midiProgramChange = pf.getBoolValue("midiProgramChange", true);
    midiLatching = pf.getBoolValue("midiLatching", false);
    midiMap.clear();
    for (const auto& item : juce::StringArray::fromTokens(pf.getValue("midiMap"), ";", ""))
    {
        const int cc = item.upToFirstOccurrenceOf("=", false, false).getIntValue();
        const auto act = item.fromFirstOccurrenceOf("=", false, false);
        if (cc >= 0 && cc < 128 && act.isNotEmpty()) midiMap[cc] = act;
    }
    lastCcValue.fill(-1);
}

void AmpsurdProcessor::saveMidiSettings() const
{
    juce::StringArray items;
    for (const auto& [cc, act] : midiMap) items.add(juce::String(cc) + "=" + act);
    juce::PropertiesFile pf(settingsOptions());
    pf.setValue("midiMap", items.joinIntoString(";"));
    pf.setValue("midiProgramChange", midiProgramChange);
    pf.setValue("midiLatching", midiLatching);
    pf.saveIfNeeded();
}

void AmpsurdProcessor::selectPresetByIndex(int index)
{
    const auto files = listPresets();
    if (index < 0 || index >= files.size()) return;
    loadPreset(files[index]);
    midiStatus = "Preset " + juce::String(index + 1) + ": " + files[index].getFileNameWithoutExtension();
    midiStatusAt = juce::Time::getMillisecondCounterHiRes();
}

void AmpsurdProcessor::stepPreset(int delta)
{
    const auto files = listPresets();
    if (files.isEmpty()) return;
    const int cur = files.indexOf(getCurrentPresetFile());
    const int n = files.size();
    const int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n;
    selectPresetByIndex(next);
}

void AmpsurdProcessor::applyMidiAction(const juce::String& id, int cc, int value)
{
    const auto* action = [&]() -> const MidiAction* {
        for (const auto& a : midiActions())
            if (a.id == id) return &a;
        return nullptr;
    }();
    if (action == nullptr) return;

    const int last = lastCcValue[(size_t) cc];
    lastCcValue[(size_t) cc] = value;
    if (action->continuous)
    {
        if (auto* p = params.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost((float) value / 127.0f);
            p->endChangeGesture();
        }
        return;
    }
    // momentary footswitch: one press = 127 then 0 on release -> act on the press only
    // latching footswitch: alternates 127 / 0 -> every change is a press; on / off follow the value
    const bool press = midiLatching ? value != last : (value >= 64 && last < 64);
    if (!press) return;
    midiStatus = action->name;
    midiStatusAt = juce::Time::getMillisecondCounterHiRes();

    if (id == "presetNext") { stepPreset(1); return; }
    if (id == "presetPrev") { stepPreset(-1); return; }
    if (id == "abSwitch") { selectAb(1 - abCurrent); midiStatus = abCurrent == 0 ? "A" : "B"; return; }

    auto* p = params.getParameter(id);
    if (p == nullptr) return;
    const bool on = midiLatching ? value >= 64 : p->getValue() < 0.5f;
    if (id == "frankOn") { setFrankenstein(on); return; }
    setParamValue(params, id, on ? 1.0f : 0.0f);
    midiStatus = action->name + (on ? ": on" : ": off");
}

void AmpsurdProcessor::handleMidiEvents()
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    midiFifo.prepareToRead(midiFifo.getNumReady(), start1, size1, start2, size2);
    std::vector<MidiEvent> events;
    for (int i = 0; i < size1; ++i) events.push_back(midiEvents[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i) events.push_back(midiEvents[(size_t) (start2 + i)]);
    midiFifo.finishedRead(size1 + size2);

    for (const auto& e : events)
    {
        if (e.type == 1)
        {
            if (midiProgramChange && midiLearnAction.isEmpty()) selectPresetByIndex(e.number);
            continue;
        }
        const int cc = juce::jlimit(0, 127, e.number);
        if (midiLearnAction.isNotEmpty())
        {
            for (auto it = midiMap.begin(); it != midiMap.end();)
                it = it->second == midiLearnAction ? midiMap.erase(it) : std::next(it);
            midiMap[cc] = midiLearnAction;
            for (const auto& a : midiActions())
                if (a.id == midiLearnAction) midiStatus = "CC " + juce::String(cc) + " -> " + a.name;
            midiStatusAt = juce::Time::getMillisecondCounterHiRes();
            midiLearnAction.clear();
            lastCcValue[(size_t) cc] = e.value; // the learning press itself does not act
            saveMidiSettings();
            continue;
        }
        if (const auto it = midiMap.find(cc); it != midiMap.end())
            applyMidiAction(it->second, cc, e.value);
        else
            lastCcValue[(size_t) cc] = e.value;
    }
}

// =============================================================================================
// Preset packs
// =============================================================================================
juce::File AmpsurdProcessor::getPacksFolder() const
{
    return getPresetFolder().getParentDirectory().getChildFile("Packs");
}

bool AmpsurdProcessor::exportPack(const juce::File& packFileIn, juce::String& error)
{
    const auto packFile = packFileIn.withFileExtension(".ampsurdpack");
    const auto name = packFile.getFileNameWithoutExtension();
    auto state = createStateTree();
    state.setProperty("presetName", name, nullptr);

    juce::ZipFile::Builder zip;
    juce::StringArray used, lines;
    auto addFile = [&](const juce::String& path) -> juce::String {
        const juce::File f(path);
        if (!juce::File::isAbsolutePath(path) || !f.existsAsFile()) return {};
        juce::String entry = f.getFileName();
        for (int k = 2; used.contains(entry, true); ++k)
            entry = f.getFileNameWithoutExtension() + " (" + juce::String(k) + ")" + f.getFileExtension();
        used.add(entry);
        zip.addFile(f, 9, entry);
        lines.add("  " + entry);
        return entry;
    };
    // the preset refers to its files by name only (no folders from this computer)
    if (auto slots = state.getChildWithName(kSlots); slots.isValid())
        for (auto slot : slots)
        {
            for (const auto& id : { kPath, kIrPath })
            {
                const auto path = slot.getProperty(id).toString();
                if (path.isEmpty()) continue;
                const auto entry = addFile(path);
                if (entry.isEmpty()) { error = "File not found: " + path; return false; }
                slot.setProperty(id, entry, nullptr);
            }
        }

    const auto xml = state.createXml();
    if (xml == nullptr) { error = "Could not write the preset."; return false; }
    juce::MemoryOutputStream presetData;
    xml->writeTo(presetData);
    zip.addEntry(new juce::MemoryInputStream(presetData.getMemoryBlock(), true), 9, name + ".ampsurd", juce::Time::getCurrentTime());

    juce::String readme;
    readme << "AMPSURD preset pack: " << name << "\r\n\r\n"
           << "Import: AMPSURD -> preset menu -> Import pack... It is unpacked into Documents\\AMPSURD\\Packs\\" << name
           << " and loaded.\r\n\r\nFiles:\r\n  " << name << ".ampsurd (the preset)\r\n" << lines.joinIntoString("\r\n") << "\r\n\r\n"
           << "The captures (.nam) and IRs remain under their creators' licences. Share a pack only when the\r\n"
           << "licences of all files in it allow it.\r\n";
    auto readmeBlock = juce::MemoryBlock(readme.toRawUTF8(), readme.getNumBytesAsUTF8());
    zip.addEntry(new juce::MemoryInputStream(readmeBlock, true), 9, "README.txt", juce::Time::getCurrentTime());

    juce::TemporaryFile tmp(packFile);
    {
        juce::FileOutputStream os(tmp.getFile());
        if (!os.openedOk() || !zip.writeToStream(os, nullptr)) { error = "Could not write " + packFile.getFullPathName(); return false; }
    }
    if (!tmp.overwriteTargetFileWithTemporary()) { error = "Could not write " + packFile.getFullPathName(); return false; }
    return true;
}

bool AmpsurdProcessor::importPack(const juce::File& packFile, juce::String& error)
{
    juce::ZipFile zip(packFile);
    if (zip.getNumEntries() == 0) { error = "This is not an AMPSURD pack (or it is empty)."; return false; }
    int presetEntry = -1;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto n = zip.getEntry(i)->filename;
        if (n.containsAnyOf("/\\") || n.startsWith(".")) { error = "The pack contains folders - not an AMPSURD pack."; return false; }
        if (n.endsWithIgnoreCase(".ampsurd")) presetEntry = i;
    }
    if (presetEntry < 0) { error = "The pack contains no preset."; return false; }

    auto dest = getPacksFolder().getChildFile(juce::File::createLegalFileName(packFile.getFileNameWithoutExtension()));
    for (int k = 2; dest.exists(); ++k)
        dest = getPacksFolder().getChildFile(juce::File::createLegalFileName(packFile.getFileNameWithoutExtension()) + " " + juce::String(k));
    if (!dest.createDirectory()) { error = "Could not create " + dest.getFullPathName(); return false; }
    if (const auto r = zip.uncompressTo(dest, false); r.failed()) { error = r.getErrorMessage(); return false; }
    const auto preset = dest.getChildFile(zip.getEntry(presetEntry)->filename);
    if (!loadPreset(preset)) { error = "The preset in the pack could not be read."; return false; }
    return true;
}

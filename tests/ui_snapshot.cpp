// ui_snapshot: renders the real AMPSURD editor to PNG files (no DAW, no screen needed).
// Also exercises the processor end to end: loading, linked faders, presets.
//
// Usage: ui_snapshot <outDir> <a.nam> [b.nam ... up to 5]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

#include "../plugin/PluginEditor.h"
#include "../plugin/PluginProcessor.h"

static void pump(int ms)
{
    // the plugin library is built without modal loops; background loading needs no message pump
    juce::Thread::sleep(ms);
}

static void save(juce::Component& c, const juce::File& f, float scale = 1.0f)
{
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, scale);
    f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::cout << "wrote " << f.getFullPathName() << "\n";
}

static void setP(AmpsurdProcessor& p, const juce::String& id, float v)
{
    if (auto* rp = p.params.getParameter(id)) rp->setValueNotifyingHost(rp->convertTo0to1(v));
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::cerr << "Usage: ui_snapshot <outDir> <a.nam> [...]\n"; return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out(argv[1]);
    out.createDirectory();

    auto proc = std::make_unique<AmpsurdProcessor>();
    proc->prepareToPlay(48000.0, 256);

    // 1. empty rig
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        e->refreshAll();
        save(*ed, out.getChildFile("01_empty.png"));
    }

    // 2. load captures (one per argument) and check the linked mix
    const int n = juce::jmin(argc - 2, 5);
    for (int i = 0; i < n; ++i)
        proc->loadCapture(i, juce::File(argv[i + 2]));
    for (int t = 0; t < 400; ++t)
    {
        bool busy = false;
        for (int i = 0; i < n; ++i)
            busy = busy || proc->getSlotStatus(i).state == AmpsurdProcessor::SlotState::loading;
        if (!busy) break;
        pump(50);
    }
    // run some audio so the engine adopts the captures
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < 40; ++b) { buf.clear(); proc->processBlock(buf, midi); }

    float total = 0;
    for (int i = 0; i < 5; ++i) total += proc->params.getRawParameterValue(AmpsurdProcessor::slotParamId(i, "mix"))->load();
    std::cout << "after loading " << n << " captures, stored mix total = " << total << " %\n";

    // move fader 1 to 42 % (linked)
    proc->beginMixGesture(0);
    proc->setMixLinked(0, 42.0f);
    proc->endMixGesture(0);
    for (int b = 0; b < 4; ++b) { buf.clear(); proc->processBlock(buf, midi); }
    std::cout << "linked faders:";
    total = 0;
    for (int i = 0; i < 5; ++i)
    {
        const float v = proc->params.getRawParameterValue(AmpsurdProcessor::slotParamId(i, "mix"))->load();
        total += v;
        std::cout << " " << juce::String(v, 1) << "%";
    }
    std::cout << "  (total " << total << " %)\n";

    // a little EQ on slot 2 and a mute on slot 4
    setP(*proc, AmpsurdProcessor::bandParamId(1, 1, "gain"), -4.5f);
    setP(*proc, AmpsurdProcessor::bandParamId(1, 4, "gain"), 3.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(1, 4, "q"), 2.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(1, 7, "gain"), -6.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(1, 7, "freq"), 3500.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(1, 0, "freq"), 90.0f);    // low cut
    setP(*proc, AmpsurdProcessor::bandParamId(1, 9, "freq"), 7500.0f);  // high cut
    if (n >= 4) setP(*proc, AmpsurdProcessor::slotParamId(3, "mute"), 1.0f);
    for (int b = 0; b < 4; ++b) { buf.clear(); proc->processBlock(buf, midi); }

    // play an A string 4 cents flat into the plugin so the tuner and gate meter have something to show
    {
        double t = 0;
        for (int b = 0; b < 80; ++b)
        {
            for (int i = 0; i < 256; ++i, t += 1.0 / 48000.0)
            {
                double v = 0;
                for (int h = 1; h <= 6; ++h) v += std::sin(2 * juce::MathConstants<double>::pi * 110.0 * std::pow(2.0, -4.0 / 1200.0) * h * t) / h;
                buf.setSample(0, i, (float) (0.15 * v));
                buf.setSample(1, i, (float) (0.15 * v));
            }
            proc->processBlock(buf, midi);
        }
    }
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        setP(*proc, AmpsurdProcessor::slotParamId(0, "pan"), -40.0f);
        setP(*proc, AmpsurdProcessor::slotParamId(2, "pan"), 35.0f);
        e->refreshAll();
        save(*ed, out.getChildFile("02_loaded.png"));
        e->selectSlot(1);
        e->refreshAll();
        save(*ed, out.getChildFile("03_edit_slot2.png"));
        setP(*proc, AmpsurdProcessor::slotParamId(1, "align"), 1.0f);
        setP(*proc, AmpsurdProcessor::slotParamId(1, "time"), 0.25f);
        setP(*proc, AmpsurdProcessor::slotParamId(1, "phase"), 45.0f);
        e->refreshAll();
        save(*ed, out.getChildFile("04_edit_free.png"));
        ed->setSize(1800, 1200);
        e->refreshAll();
        save(*ed, out.getChildFile("05_scaled_1800.png"));
    }

    // Create Frankenstein
    {
        proc->setFrankenstein(true);
        proc->setFrankensteinSections(3);
        setP(*proc, "frankWidth", 40.0f);
        setP(*proc, "frankAmp1", 4.0f); // section 1: amp 5 (lows)
        setP(*proc, "frankAmp2", 0.0f); // section 2: amp 1 (mids)
        setP(*proc, "frankAmp3", 2.0f); // section 3: amp 3 (highs)
        for (int b = 0; b < 8; ++b) { buf.clear(); proc->processBlock(buf, midi); }
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        e->refreshAll();
        save(*ed, out.getChildFile("07_frankenstein.png"));
        setP(*proc, AmpsurdProcessor::slotParamId(0, "mute"), 1.0f); // mute the middle section's amp
        for (int b = 0; b < 8; ++b) { buf.clear(); proc->processBlock(buf, midi); }
        e->refreshAll();
        save(*ed, out.getChildFile("08_frankenstein_muted.png"));
        float share = 0;
        for (int i = 0; i < 5; ++i) share += proc->getEffectivePercent(i);
        std::cout << "Frankenstein: spectrum shares total " << share << " %, amp 5 " << proc->getEffectivePercent(4)
                  << " %, amp 3 " << proc->getEffectivePercent(2) << " %\n";
        setP(*proc, AmpsurdProcessor::slotParamId(0, "mute"), 0.0f);
    }

    // Cabinet IR (file given in AMPSURD_TEST_IR)
    const juce::File irFile(juce::SystemStats::getEnvironmentVariable("AMPSURD_TEST_IR", ""));
    if (irFile.existsAsFile())
    {
        proc->setFrankenstein(false);
        proc->loadIr(1, irFile);
        for (int t = 0; t < 200 && proc->getIrStatus(1).state != AmpsurdProcessor::IrState::loaded; ++t) pump(20);
        for (int b = 0; b < 20; ++b) { buf.clear(); proc->processBlock(buf, midi); }
        const int T = AmpsurdProcessor::irEqTarget(1);
        setP(*proc, AmpsurdProcessor::bandParamId(T, 0, "freq"), 70.0f);
        setP(*proc, AmpsurdProcessor::bandParamId(T, 6, "gain"), -3.5f);
        setP(*proc, AmpsurdProcessor::bandParamId(T, 9, "freq"), 7500.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        e->refreshAll();
        save(*ed, out.getChildFile("13_slots_with_ir.png"));
        e->showIr(1);
        e->refreshAll();
        save(*ed, out.getChildFile("14_ir_editor.png"));
        e->showIr(3);
        e->refreshAll();
        save(*ed, out.getChildFile("15_ir_editor_empty.png"));
        std::cout << "IR: " << proc->getIrStatus(1).info << "\n";
    }

    // Global EQ
    {
        proc->setFrankenstein(false);
        for (int b = 0; b < 8; ++b) { buf.clear(); proc->processBlock(buf, midi); }
        const int G = AmpsurdProcessor::kGlobalEq;
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        e->refreshAll();
        save(*ed, out.getChildFile("09_global_eq_off.png"));
        setP(*proc, AmpsurdProcessor::slotParamId(G, "eqOn"), 1.0f);
        setP(*proc, AmpsurdProcessor::bandParamId(G, 0, "freq"), 80.0f);   // low cut 80 Hz
        setP(*proc, AmpsurdProcessor::bandParamId(G, 3, "gain"), -3.0f);   // 250 Hz -3 dB
        setP(*proc, AmpsurdProcessor::bandParamId(G, 7, "gain"), 2.5f);    // 4 kHz +2.5 dB
        setP(*proc, AmpsurdProcessor::bandParamId(G, 7, "q"), 0.8f);
        setP(*proc, AmpsurdProcessor::bandParamId(G, 9, "freq"), 9000.0f); // high cut 9 kHz
        e->refreshAll();
        save(*ed, out.getChildFile("10_global_eq_on.png"));
        e->showGlobalEq(true);
        e->refreshAll();
        save(*ed, out.getChildFile("11_global_eq_panel.png"));
        setP(*proc, AmpsurdProcessor::bandParamId(1, 4, "gain"), 4.0f);    // amp 2: 500 Hz +4 dB
        setP(*proc, AmpsurdProcessor::bandParamId(1, 6, "gain"), -5.0f);   // amp 2: 2 kHz -5 dB
        e->selectSlot(1);
        e->refreshAll();
        save(*ed, out.getChildFile("12_edit_with_global_eq.png"));
        // effects
        setP(*proc, "fxD1On", 1.0f);
        setP(*proc, "fxD1Time", 500.0f);
        setP(*proc, "fxD2On", 1.0f);
        setP(*proc, "fxD2Time", 756.0f);
        setP(*proc, "fxD2PingPong", 1.0f);
        setP(*proc, "fxRevOn", 1.0f);
        e->showGlobalEq(true);
        e->showFxPage(1);
        e->refreshAll();
        save(*ed, out.getChildFile("16_fx_delay.png"));
        setP(*proc, "fxRevType", 3.0f);
        setP(*proc, "fxRevDecay", 5.5f);
        setP(*proc, "fxRevPreDelay", 45.0f);
        e->showFxPage(2);
        e->refreshAll();
        save(*ed, out.getChildFile("17_fx_reverb.png"));
        e->showGlobalEq(false);
        e->refreshAll();
        save(*ed, out.getChildFile("18_fx_button.png"));
    }

    // 3. preset round trip
    const auto presetFile = out.getChildFile("Test rig.ampsurd");
    proc->savePreset(presetFile);
    auto proc2 = std::make_unique<AmpsurdProcessor>();
    proc2->prepareToPlay(48000.0, 256);
    proc2->loadPreset(presetFile);
    for (int t = 0; t < 400; ++t)
    {
        bool busy = false;
        for (int i = 0; i < 5; ++i)
            busy = busy || proc2->getSlotStatus(i).state == AmpsurdProcessor::SlotState::loading;
        if (!busy) break;
        pump(50);
    }
    int same = 0, checked = 0;
    for (auto* prm : proc->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
        {
            ++checked;
            if (std::abs(rp->getValue() - proc2->params.getParameter(rp->getParameterID())->getValue()) < 1e-6f) ++same;
        }
    int slotsSame = 0;
    for (int i = 0; i < 5; ++i)
        slotsSame += proc->getSlotStatus(i).path == proc2->getSlotStatus(i).path
                     && proc->getSlotStatus(i).state == proc2->getSlotStatus(i).state;
    for (int t = 0; t < 200 && proc2->getIrStatus(1).state == AmpsurdProcessor::IrState::loading; ++t) pump(20);
    int irSame = 0;
    for (int i = 0; i < 5; ++i)
        irSame += proc->getIrStatus(i).path == proc2->getIrStatus(i).path && proc->getIrStatus(i).state == proc2->getIrStatus(i).state;
    std::cout << "preset round trip: " << irSame << "/5 IR slots restored (slot 2 IR: '" << proc2->getIrStatus(1).fileName << "')\n";
    std::cout << "preset round trip: " << same << "/" << checked << " parameters, " << slotsSame << "/5 slots restored, name '"
              << proc2->getCurrentPresetName() << "'\n";

    // 3b. an older preset without Global EQ parameters must load with the Global EQ at its defaults
    {
        auto xml = juce::XmlDocument::parse(presetFile);
        for (int i = xml->getNumChildElements() - 1; i >= 0; --i)
            if (xml->getChildElement(i)->getStringAttribute("id").startsWith("geq_"))
                xml->removeChildElement(xml->getChildElement(i), true);
        if (auto* slotsXml = xml->getChildByName("SLOTS"))
            for (auto* sx : slotsXml->getChildIterator()) sx->removeAttribute("irPath"); // presets from before IRs
        const auto oldPreset = out.getChildFile("Old.ampsurd");
        xml->writeTo(oldPreset);
        proc2->loadPreset(oldPreset); // proc2 currently has the Global EQ ON from the round trip
        const bool on = proc2->isGlobalEqOn();
        const bool flat = ampsurd::ParametricEq::isFlat(proc2->getEqBands(AmpsurdProcessor::kGlobalEq));
        std::cout << "older preset without Global EQ: " << (!on && flat ? "Global EQ off and flat (ok)" : "UNEXPECTED") << "\n";
        const bool noIr = proc2->getIrStatus(1).state == AmpsurdProcessor::IrState::none;
        std::cout << "older preset without IR: " << (noIr ? "slot 2 has no IR (ok)" : "UNEXPECTED") << "\n";
        oldPreset.deleteFile();
    }

    // 4. preset with a missing capture
    {
        auto xml = juce::XmlDocument::parse(presetFile);
        if (auto* slots = xml->getChildByName("SLOTS"))
            if (auto* first = slots->getChildByName("SLOT"))
                first->setAttribute("path", "C:/Captures/Rectifier_CH3_Modern_SM57_A2.nam");
        const auto missingPreset = out.getChildFile("Missing.ampsurd");
        xml->writeTo(missingPreset);
        proc2->loadPreset(missingPreset);
        pump(1500);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc2->createEditor());
        dynamic_cast<AmpsurdEditor*>(ed.get())->refreshAll();
        save(*ed, out.getChildFile("06_missing_file.png"));
        std::cout << "missing capture slot state: "
                  << (proc2->getSlotStatus(0).state == AmpsurdProcessor::SlotState::missing ? "MISSING (ok)" : "unexpected") << "\n";
    }

    proc2.reset();
    proc.reset();
    return 0;
}

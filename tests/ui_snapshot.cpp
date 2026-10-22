// ui_snapshot: renders the real AMPSURD editor to PNG files (no DAW, no screen needed).
// Also exercises the processor end to end: loading, linked faders, presets.
//
// Usage: ui_snapshot <outDir> <a.nam> [b.nam ... up to 5]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <limits>
#include <iostream>

#include "../plugin/PluginEditor.h"
#include "../plugin/UpdateCheck.h"
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

    {
        using ampsurd::isNewerVersion;
        const bool ok = isNewerVersion("v0.9.1", "0.9.0") && isNewerVersion("0.10.0", "0.9.9") && isNewerVersion("v1.0.0", "0.9.0")
                        && !isNewerVersion("v0.9.0", "0.9.0") && !isNewerVersion("0.8.9", "0.9.0") && isNewerVersion("0.9.0.1", "0.9.0");
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " UPDATES: version comparison (0.9.1 > 0.9.0, 0.10.0 > 0.9.9, equal is not newer)\n";
    }
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

    // MUTE moves the fader to 0 % (the others show their real shares); un-mute restores the old share
    if (n >= 3)
    {
        auto mix = [&](int i) { return proc->params.getRawParameterValue(AmpsurdProcessor::slotParamId(i, "mix"))->load(); };
        proc->runHousekeeping(); // the 30 Hz timer has been running in the app (takes the current MUTE states)
        const float a0 = mix(0), a1 = mix(1), a2 = mix(2);
        setP(*proc, AmpsurdProcessor::slotParamId(0, "mute"), 1.0f);
        proc->runHousekeeping();
        const float m0 = mix(0), m1 = mix(1), m2 = mix(2);
        const bool mutedOk = m0 == 0.0f && std::abs(m1 + m2 - 100.0f) < 0.05f && std::abs(m1 / m2 - a1 / a2) < 1e-3f;
        // saved in a preset while muted, the old share comes back after loading it
        const auto presetFile = out.getChildFile("Muted.ampsurd");
        proc->savePreset(presetFile);
        proc->loadPreset(presetFile);
        for (int t = 0; t < 400; ++t)
        {
            bool busy = false;
            for (int i = 0; i < n; ++i) busy = busy || proc->getSlotStatus(i).state == AmpsurdProcessor::SlotState::loading;
            if (!busy) break;
            pump(50);
        }
        proc->runHousekeeping();
        const bool reloadedOk = mix(0) == 0.0f && proc->isSlotMuted(0);
        setP(*proc, AmpsurdProcessor::slotParamId(0, "mute"), 0.0f);
        proc->runHousekeeping();
        const bool restoredOk = std::abs(mix(0) - a0) < 0.05f && std::abs(mix(1) - a1) < 0.05f && std::abs(mix(2) - a2) < 0.05f;
        std::cout << (mutedOk && reloadedOk && restoredOk ? "[PASS]" : "[FAIL]")
                  << " MUTE: amp 1 muted -> faders " << m0 << " / " << m1 << " / " << m2 << " % (real shares); saved + reloaded muted"
                  << (reloadedOk ? "" : " (WRONG)") << "; un-muted -> " << mix(0) << " / " << mix(1) << " / " << mix(2)
                  << " % (before: " << a0 << " / " << a1 << " / " << a2 << ")\n";
        for (int b = 0; b < 4; ++b) { buf.clear(); proc->processBlock(buf, midi); }
    }

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
        setP(*proc, AmpsurdProcessor::slotParamId(1, "drive"), 6.0f); // per-amp INPUT / OUTPUT in EDIT
        setP(*proc, AmpsurdProcessor::slotParamId(1, "level"), -2.0f);
        for (int t = 0; t < 600 && !proc->isDriveCompensationReady(1); ++t) pump(50);
        proc->runHousekeeping();
        {
            const double c = proc->getDriveCompensationDb(1);
            std::cout << (proc->isDriveCompensationReady(1) && std::abs(c) > 1e-3 && std::abs(c) < 12.0 ? "[PASS]" : "[FAIL]")
                      << " INPUT: amp 2 driven +6 dB -> loudness compensation " << juce::String(c, 2).toStdString() << " dB\n";
        }
        e->refreshAll();
        save(*ed, out.getChildFile("03_edit_slot2.png"));
        {
            // EQ band count: 4 = low cut + 2 bells + high cut; hidden bells are flat, unused bells spread out
            const auto before = proc->getEqBands(1);
            proc->setEqBandCount(1, 4);
            const auto after = proc->getEqBands(1);
            bool hiddenFlat = true;
            for (int b = 3; b < 9; ++b) hiddenFlat = hiddenFlat && after[(size_t) b].gainDb == 0.0f;
            std::cout << (proc->getEqBandCount(1) == 4 && hiddenFlat && !proc->isEqBandVisible(1, 3) && proc->isEqBandVisible(1, 9) ? "[PASS]" : "[FAIL]")
                      << " EQ BANDS: 4 bands -> bells 3..8 hidden and flat, cuts kept (bell 1 at " << after[1].freqHz
                      << " Hz, bell 2 at " << after[2].freqHz << " Hz)\n";
            (void) before;
            e->refreshAll();
            save(*ed, out.getChildFile("03b_edit_4_bands.png"));
            proc->setEqBandCount(1, 10);
            e->refreshAll();
        }
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

    // GUITAR INPUT hint: guitar only on interface input 2 while input 1 is selected
    {
        const int saved = proc->getGuitarInput();
        proc->setGuitarInput(0);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        juce::AudioBuffer<float> b2(2, 256);
        for (int k = 0; k < 40; ++k)
        {
            b2.clear();
            for (int i = 0; i < 256; ++i) b2.setSample(1, i, 0.2f * (float) std::sin(0.0144 * (k * 256 + i)));
            proc->processBlock(b2, midi);
            e->refreshAll();
        }
        std::function<juce::TextButton*(juce::Component&)> findHint = [&](juce::Component& c) -> juce::TextButton* {
            if (auto* tb = dynamic_cast<juce::TextButton*>(&c))
                if (tb->getButtonText().startsWith("GUITAR ON INPUT")) return tb;
            for (auto* ch : c.getChildren())
                if (auto* r = findHint(*ch)) return r;
            return nullptr;
        };
        auto* hint = findHint(*ed);
        const bool shown = hint != nullptr && hint->isVisible() && hint->getButtonText().contains("INPUT 2");
        save(*ed, out.getChildFile("16_guitar_input_hint.png"));
        if (hint != nullptr && hint->onClick) hint->onClick(); // what a mouse click runs
        const bool switched = hint != nullptr && proc->getGuitarInput() == 1 && !hint->isVisible();
        std::cout << (shown && switched ? "[PASS]" : "[FAIL]")
                  << " GUITAR INPUT: signal only on input 2 -> \"GUITAR ON INPUT 2?\" shown, one click selects input 2\n";
        proc->setGuitarInput(saved);
    }

   #if AMPSURD_ROOT
    // ROOT panel (experimental): open from the master row, ON, a power chord, the layer's curve and meter
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        setP(*proc, "rootOn", 1.0f);
        setP(*proc, "rootLow", 4.0f);
        juce::AudioBuffer<float> b3(2, 256);
        e->showRoot(true);
        for (int k = 0; k < 40; ++k)
        {
            b3.clear();
            for (int i = 0; i < 256; ++i)
            {
                const double t = (k * 256 + i) / 48000.0;
                b3.setSample(0, i, (float) (0.15 * (std::sin(2 * 3.14159265 * 61.735 * t) + std::sin(2 * 3.14159265 * 92.499 * t))));
            }
            proc->processBlock(b3, midi);
            e->refreshAll();
        }
        save(*ed, out.getChildFile("17_root.png"));
        std::function<juce::Component*(juce::Component&, const juce::String&)> findText = [&](juce::Component& c, const juce::String& t) -> juce::Component* {
            if (auto* tb = dynamic_cast<juce::TextButton*>(&c))
                if (tb->getButtonText() == t && tb->isVisible()) return tb;
            for (auto* ch : c.getChildren())
                if (ch->isVisible())
                    if (auto* r = findText(*ch, t)) return r;
            return nullptr;
        };
        const bool panel = findText(*ed, "PARALLEL") != nullptr && findText(*ed, "FRANKENSTEIN") != nullptr;
        e->showRoot(false);
        e->refreshAll();
        const bool closed = findText(*ed, "PARALLEL") == nullptr;
        std::cout << (panel && closed && proc->isRootOn() ? "[PASS]" : "[FAIL]")
                  << " ROOT: panel opens from the master row (FRANKENSTEIN next to it), closes; ROOT stays on\n";
        setP(*proc, "rootOn", 0.0f);
        setP(*proc, "rootLow", 0.0f);
    }
   #endif

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
        setP(*proc, "pitchOn", 1.0f);
        setP(*proc, "pitchSemi", -2.0f);
        e->showFxPage(4);
        e->refreshAll();
        save(*ed, out.getChildFile("17b_fx_pitch.png"));
        // PITCH adds its delay to the reported latency (and removes it when off)
        juce::AudioBuffer<float> pb(2, 256);
        pb.clear(); // (an AudioBuffer is not cleared when it is created)
        juce::MidiBuffer pm;
        proc->processBlock(pb, pm);
        e->refreshAll();
        juce::Thread::sleep(100);
        const int withPitch = proc->getLatencySamples();
        setP(*proc, "pitchOn", 0.0f);
        e->refreshAll();
        juce::Thread::sleep(100);
        std::cout << "[INFO] latency with PITCH " << withPitch << " samples, without " << proc->getLatencySamples() << "\n";
        e->showGlobalEq(false);
        e->refreshAll();
        save(*ed, out.getChildFile("18_fx_button.png"));
    }

    // Player / recorder (standalone only; AMPSURD_FORCE_PLAYER=1 shows it here)
    if (auto* pl = proc->getPlayer())
    {
        const auto wavFile = out.getChildFile("backing_demo.wav");
        {
            juce::AudioBuffer<float> b(2, 48000 * 6);
            for (int i = 0; i < b.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c) b.setSample(c, i, 0.2f * (float) std::sin(2 * juce::MathConstants<double>::pi * 110 * i / 48000.0));
            std::unique_ptr<juce::OutputStream> os = wavFile.createOutputStream();
            juce::WavAudioFormat wav;
            auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
            w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
        }
        proc->setNonRealtime(true);
        pl->loadBacking(wavFile);
        pl->record();
        for (int k = 0; k < 48000 * 2 / 256; ++k) { buf.clear(); proc->processBlock(buf, midi); }
        pl->stop();
        pl->play();
        for (int k = 0; k < 48000 / 256; ++k) { buf.clear(); proc->processBlock(buf, midi); }
        pl->pause();
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        e->showPlayer(true);
        e->refreshAll();
        save(*ed, out.getChildFile("19_player.png"));
        pl->stop();
        proc->setNonRealtime(false);
        wavFile.deleteFile();
    }

    // ROBUST: a block of garbage (NaN, inf, 1e30) from a misbehaving driver must not silence AMPSURD
    {
        juce::AudioBuffer<float> g(2, 256);
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 2; ++c)
                g.setSample(c, i, i % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : i % 3 == 1 ? std::numeric_limits<float>::infinity() : 1e30f);
        proc->processBlock(g, midi);
        proc->runHousekeeping();
        float peak = 0.0f;
        bool finite = true;
        for (int k = 0; k < 200; ++k)
        {
            juce::AudioBuffer<float> b(2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const float v = 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 110 * (k * 256 + i) / 48000.0);
                b.setSample(0, i, v);
                b.setSample(1, i, v);
            }
            proc->processBlock(b, midi);
            if (k % 8 == 0) proc->runHousekeeping();
            for (int i = 0; i < 256; ++i) finite = finite && std::isfinite(b.getSample(0, i)) && std::isfinite(b.getSample(1, i));
            if (k > 150) peak = juce::jmax(peak, b.getMagnitude(0, 0, 256));
        }
        // the second line of defence (NaN made inside, e.g. by a model): everything is prepared again
        proc->requestRecoveryForTest();
        proc->runHousekeeping();
        float after = 0.0f;
        for (int k = 0; k < 120; ++k)
        {
            juce::AudioBuffer<float> b(2, 256);
            for (int i = 0; i < 256; ++i)
                for (int c = 0; c < 2; ++c)
                    b.setSample(c, i, 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 110 * (k * 256 + i) / 48000.0));
            proc->processBlock(b, midi);
            if (k > 80) after = juce::jmax(after, b.getMagnitude(0, 0, 256));
        }
        std::cout << (proc->getRecoveryCount() == 1 && after > 0.01f ? "[PASS]" : "[FAIL]")
                  << " RECOVERY: after a reset of the signal chain the amps play again (peak " << after << ")\n";
        std::cout << (finite && peak > 0.01f ? "[PASS]" : "[FAIL]") << " ROBUST: NaN / inf / 1e30 input block -> output stays finite, the amps play again (peak "
                  << peak << ", recoveries " << proc->getRecoveryCount() << ")\n";
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

    // 3c. undo / redo, A/B, MIDI learn, preset pack (on proc2, which holds the test rig)
    {
        auto& p2 = *proc2;
        auto val = [&](const char* id) { return p2.params.getRawParameterValue(id)->load(); };
        auto settle = [&] { p2.runHousekeeping(); pump(450); p2.runHousekeeping(); };
        settle();
        const float thr0 = val("gateThreshold");
        setP(p2, "gateThreshold", -50.0f);
        settle();
        setP(p2, "output", -6.0f);
        settle();
        p2.undo();
        const bool undo1 = val("output") == 0.0f && val("gateThreshold") == -50.0f;
        const bool kept = p2.getSlotStatus(0).state == AmpsurdProcessor::SlotState::loaded; // same capture: not reloaded
        p2.undo();
        const bool undo2 = val("gateThreshold") == thr0;
        p2.redo();
        p2.redo();
        const bool redo2 = val("gateThreshold") == -50.0f && val("output") == -6.0f;
        std::cout << (undo1 && undo2 && redo2 && kept ? "[PASS]" : "[FAIL]")
                  << " UNDO: two changes undone and redone; the loaded captures stay loaded (no reload)\n";
        {
            // the keyboard: Ctrl+Z / Ctrl+Y in the editor
            std::unique_ptr<juce::AudioProcessorEditor> ed(p2.createEditor());
            setP(p2, "output", -9.0f);
            settle();
            ed->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0));
            const bool kz = val("output") == -6.0f;
            ed->keyPressed(juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0));
            const bool ky = val("output") == -9.0f;
            ed->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0));
            std::cout << (kz && ky ? "[PASS]" : "[FAIL]") << " UNDO keys: Ctrl+Z undoes, Ctrl+Y redoes\n";
        }

        {
            // move amp 1 to slot 3 (swap): capture, pan and mix share travel with it
            const auto n1 = p2.getSlotStatus(0).path, n3 = p2.getSlotStatus(2).path;
            const float pan1 = val("s1_pan"), pan3 = val("s3_pan"), mix1 = val("s1_mix");
            p2.swapSlots(0, 2);
            for (int t = 0; t < 400 && (p2.getSlotStatus(0).state == AmpsurdProcessor::SlotState::loading
                                       || p2.getSlotStatus(2).state == AmpsurdProcessor::SlotState::loading); ++t) pump(50);
            const bool moved = p2.getSlotStatus(2).path == n1 && p2.getSlotStatus(0).path == n3 && val("s3_pan") == pan1
                               && val("s1_pan") == pan3 && val("s3_mix") == mix1
                               && p2.getSlotStatus(2).state == AmpsurdProcessor::SlotState::loaded;
            p2.swapSlots(0, 2); // back
            for (int t = 0; t < 400 && (p2.getSlotStatus(0).state == AmpsurdProcessor::SlotState::loading
                                       || p2.getSlotStatus(2).state == AmpsurdProcessor::SlotState::loading); ++t) pump(50);
            std::cout << (moved && p2.getSlotStatus(0).path == n1 ? "[PASS]" : "[FAIL]")
                      << " MOVE: amp 1 dragged to slot 3 -> capture, pan and mix share moved with it (and back)\n";
            settle();
        }
        {
            // EQ COPY / PASTE: amp 1 -> amp 3 and -> Global EQ; SURE? expires after 3 s; undo restores
            using AP = AmpsurdProcessor;
            setP(p2, AP::slotParamId(0, "bands"), 6.0f);
            setP(p2, AP::bandParamId(0, 1, "gain"), 5.0f);
            setP(p2, AP::bandParamId(0, 2, "freq"), 900.0f);
            setP(p2, AP::bandParamId(0, 2, "q"), 2.0f);
            setP(p2, AP::bandParamId(0, 9, "freq"), 7000.0f);
            settle();
            auto same = [&](int a, int b) {
                bool ok = p2.getEqBandCount(a) == p2.getEqBandCount(b)
                          && val(AP::slotParamId(a, "eqOn").toRawUTF8()) == val(AP::slotParamId(b, "eqOn").toRawUTF8());
                for (int k = 0; k < 10; ++k)
                    for (const char* f : { "freq", "gain", "q" })
                    {
                        // equal up to float rounding of the parameter's range conversion (inaudible, < 0.001 %)
                        const float x = val(AP::bandParamId(a, k, f).toRawUTF8()), y = val(AP::bandParamId(b, k, f).toRawUTF8());
                        ok = ok && std::abs(x - y) <= 1e-5f * std::max(1.0f, std::abs(x));
                    }
                return ok;
            };
            ampsurd::ui::EqCopyButton c1(p2), c3(p2), cg(p2);
            c1.setTarget(0);
            c3.setTarget(2);
            cg.setTarget(AP::kGlobalEq);
            c1.clicked();                                     // COPY
            const bool copied = p2.getEqClipboardSource() == 0 && c1.getButtonText() == "COPIED";
            c3.clicked();                                     // PASTE -> SURE?
            const bool sure = c3.getButtonText() == "SURE?" && !same(0, 2);
            c3.clicked();                                     // paste
            const bool amp3 = same(0, 2) && c3.getButtonText() == "COPY" && c1.getButtonText() == "COPIED";
            settle();
            const bool globalBefore = !same(0, AP::kGlobalEq);
            cg.clicked();                                     // SURE? ...
            pump(3200);                                       // ... expires
            cg.clicked();                                     // only arms again
            const bool noPaste = !same(0, AP::kGlobalEq) && cg.getButtonText() == "SURE?";
            cg.clicked();                                     // paste
            const bool global = same(0, AP::kGlobalEq);
            settle();
            p2.undo();                                        // the Global EQ paste is one step
            const bool undone = !same(0, AP::kGlobalEq) && same(0, 2);
            c1.clicked();                                     // COPIED: cancel
            const bool cleared = p2.getEqClipboardSource() < 0;
            if (!(copied && sure && amp3 && globalBefore && noPaste && global && undone && cleared))
                std::cout << "  (copied " << copied << " sure " << sure << " amp3 " << amp3 << " globalBefore " << globalBefore << " noPaste "
                          << noPaste << " global " << global << " undone " << undone << " cleared " << cleared << ")\n";
            std::cout << (copied && sure && amp3 && globalBefore && noPaste && global && undone && cleared ? "[PASS]" : "[FAIL]")
                      << " EQ COPY / PASTE: amp 1 -> amp 3 and -> Global EQ identical (bands, count, on), SURE? expires after 3 s"
                         " without pasting, undo restores, COPIED click clears; a pasted EQ shows COPY again\n";
            settle();
        }
        p2.selectAb(1);                // B = copy of A
        setP(p2, "output", 3.0f);      // change B
        p2.selectAb(0);
        const bool aBack = val("output") == -6.0f;
        p2.selectAb(1);
        const bool bKept = val("output") == 3.0f;
        p2.selectAb(0);
        std::cout << (aBack && bKept ? "[PASS]" : "[FAIL]") << " A/B: B changed, A unchanged, switching back and forth keeps both\n";

        // MIDI learn: CC 80 -> amp 1 mute (momentary footswitch: 127 on press, 0 on release)
        auto sendCc = [&](int cc, int v) {
            juce::AudioBuffer<float> b(2, 256);
            b.clear();
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::controllerEvent(1, cc, v), 0);
            p2.processBlock(b, m);
            p2.runHousekeeping();
        };
        const bool wasMuted = p2.isSlotMuted(0);
        p2.startMidiLearn("s1_mute");
        sendCc(80, 127);
        sendCc(80, 0);
        const bool learned = p2.getMidiCcFor("s1_mute") == 80 && p2.isSlotMuted(0) == wasMuted;
        sendCc(80, 127);
        sendCc(80, 0);
        const bool toggled = p2.isSlotMuted(0) != wasMuted;
        sendCc(80, 127);
        sendCc(80, 0);
        const bool back = p2.isSlotMuted(0) == wasMuted;
        p2.clearMidiAssignments(); // leave this computer's settings as they were
        std::cout << (learned && toggled && back ? "[PASS]" : "[FAIL]") << " MIDI: CC 80 learned for Amp 1 mute; each footswitch press toggles it\n";

        // preset pack: export (preset + captures + README), import (unpacked, loaded, captures found by name)
        const auto pack = out.getChildFile("Pack test.ampsurdpack");
        juce::String err;
        const bool exported = p2.exportPack(pack, err);
        juce::ZipFile zip(pack);
        int nams = 0;
        bool hasPreset = false, hasReadme = false, noFolders = true;
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto n = zip.getEntry(i)->filename;
            nams += n.endsWithIgnoreCase(".nam");
            hasPreset = hasPreset || n == "Pack test.ampsurd";
            hasReadme = hasReadme || n == "README.txt";
            noFolders = noFolders && !n.containsAnyOf("/\\");
        }
        bool imported = false, fromPack = false;
        juce::File unpacked;
        if (exported)
        {
            imported = p2.importPack(pack, err);
            for (int t = 0; t < 400 && p2.getSlotStatus(0).state == AmpsurdProcessor::SlotState::loading; ++t) pump(50);
            unpacked = juce::File(p2.getSlotStatus(0).path).getParentDirectory();
            fromPack = unpacked.isAChildOf(p2.getPacksFolder()) && p2.getSlotStatus(0).state == AmpsurdProcessor::SlotState::loaded;
        }
        std::cout << (exported && nams == n && hasPreset && hasReadme && noFolders && imported && fromPack ? "[PASS]" : "[FAIL]")
                  << " PACK: exported " << zip.getNumEntries() << " files (" << nams << " captures + preset + README), imported and loaded from "
                  << unpacked.getFullPathName() << (err.isNotEmpty() ? " - " + err : juce::String()) << "\n";
        if (fromPack) unpacked.deleteRecursively();
        pack.deleteFile();
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

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
#include "../plugin/AmpProfileFile.h"
#include "../plugin/ui/CameraWindow.h"
#include "../plugin/UpdateCheck.h"
#include "../plugin/PluginProcessor.h"
#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/ThreadCpuClock.h"
#include "ampsurd/ParallelRunner.h"

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


// ------------------------------------------------------------------------------------------------
// Website screenshots (AMPSURD_WEB_SHOTS=1): a demo rig, the whole window and precise crops of the main
// features, rendered at 2x. ui_snapshot <outDir> <a.nam> <b.nam> <c.nam>
// ------------------------------------------------------------------------------------------------
template <typename T>
static T* findVisible(juce::Component& c)
{
    if (auto* t = dynamic_cast<T*>(&c)) if (c.isVisible()) return t;
    for (auto* ch : c.getChildren())
        if (ch->isVisible())
            if (auto* r = findVisible<T>(*ch)) return r;
    return nullptr;
}

static void writePng(const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::cout << "wrote " << f.getFileName() << " (" << img.getWidth() << " x " << img.getHeight() << ")\n";
}

static void writeWav(const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream();
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(b.getNumChannels()).withBitsPerSample(24));
    w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

static int webShots(const juce::File& out, int argc, char** argv)
{
    out.createDirectory();
    const double sr = 48000.0;
    const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ampsurd-web-demo");
    tmp.deleteRecursively();
    tmp.createDirectory();
    // the NAM Core example captures under plain names (what they are: examples, not real amp names)
    const char* names[3] = { "NAM example A2", "NAM example LSTM", "NAM example WaveNet" };
    juce::Array<juce::File> caps;
    for (int i = 0; i < juce::jmin(3, argc - 2); ++i)
    {
        const auto dst = tmp.getChildFile(juce::String(names[i]) + ".nam");
        juce::File(argv[i + 2]).copyFileTo(dst);
        caps.add(dst);
    }
    // a synthetic demo cabinet IR (decaying, filtered noise)
    const auto irFile = tmp.getChildFile("Demo cabinet IR.wav");
    {
        juce::AudioBuffer<float> ir(1, 7200);
        juce::Random rnd(7);
        double lp = 0, hp = 0, prev = 0;
        for (int i = 0; i < ir.getNumSamples(); ++i)
        {
            const double n = rnd.nextFloat() * 2.0 - 1.0;
            lp += (n - lp) * 0.35;                       // ~4-5 kHz roll-off
            hp = 0.995 * (hp + lp - prev); prev = lp;     // no DC
            ir.setSample(0, i, (float) (hp * std::exp(-i / 700.0) * 0.6));
        }
        writeWav(irFile, ir, sr);
    }
    // a backing track: a simple bass line and a hi-hat-like tick, 12 s
    const auto backFile = tmp.getChildFile("Demo backing.wav");
    {
        juce::AudioBuffer<float> b(2, (int) (12 * sr));
        juce::Random rnd(3);
        const double notes[4] = { 41.2, 41.2, 49.0, 36.7 };
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr, beat = std::fmod(t, 0.5), bar = std::floor(t / 2.0);
            const double f = notes[(int) bar % 4];
            const double bass = 0.25 * std::sin(2 * juce::MathConstants<double>::pi * f * t) * std::exp(-beat * 3.0);
            const double tick = beat < 0.03 ? 0.08 * (rnd.nextFloat() * 2 - 1) * (1.0 - beat / 0.03) : 0.0;
            b.setSample(0, i, (float) (bass + tick));
            b.setSample(1, i, (float) (bass + tick));
        }
        writeWav(backFile, b, sr);
    }

    auto proc = std::make_unique<AmpsurdProcessor>();
    proc->prepareToPlay(sr, 256);
    proc->setNonRealtime(true);
    for (int i = 0; i < caps.size(); ++i) proc->loadCapture(i, caps[i]);
    for (int t = 0; t < 600; ++t)
    {
        bool busy = false;
        for (int i = 0; i < caps.size(); ++i) busy = busy || proc->getSlotStatus(i).state == AmpsurdProcessor::SlotState::loading;
        if (!busy) break;
        pump(50);
    }
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, 256);
    const auto test = ampsurd::CaptureAnalyzer::makeTestSignal(sr);
    size_t testPos = 0;
    // plays the guitar-like test signal (meters, gate, tuner, learning) for `seconds`
    auto play = [&](double seconds, std::function<float(size_t)> src = {}) {
        for (int k = 0; k < (int) (seconds * sr / 256); ++k)
        {
            buf.clear();
            for (int i = 0; i < 256; ++i)
            {
                buf.setSample(0, i, src ? src(testPos) : (float) test[testPos % test.size()]);
                ++testPos;
            }
            proc->processBlock(buf, midi);
        }
    };
    play(0.5);
    for (int t = 0; t < 600 && !(proc->getAlignInfo(0).measured && proc->getAlignInfo(caps.size() - 1).measured); ++t)
    {
        play(0.02);
        pump(20);
    }
    // the demo rig: mix 45 / 35 / 20, pans, an IR on amp 2, Global EQ, gate
    proc->beginMixGesture(0); proc->setMixLinked(0, 45.0f); proc->endMixGesture(0);
    proc->beginMixGesture(1); proc->setMixLinked(1, 35.0f); proc->endMixGesture(1);
    setP(*proc, AmpsurdProcessor::slotParamId(0, "pan"), -30.0f);
    setP(*proc, AmpsurdProcessor::slotParamId(2, "pan"), 30.0f);
    proc->loadIr(1, irFile);
    for (int t = 0; t < 200 && proc->getIrStatus(1).state != AmpsurdProcessor::IrState::loaded; ++t) { play(0.02); pump(20); }
    const int G = AmpsurdProcessor::kGlobalEq;
    setP(*proc, AmpsurdProcessor::slotParamId(G, "eqOn"), 1.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(G, 0, "freq"), 80.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(G, 3, "gain"), -2.5f);
    setP(*proc, AmpsurdProcessor::bandParamId(G, 7, "gain"), 2.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(G, 9, "freq"), 10000.0f);
    setP(*proc, "gateOn", 1.0f);
    setP(*proc, "gateThreshold", -58.0f);
    for (int t = 0; t < 30; ++t) { play(0.05); proc->runHousekeeping(); }

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
    auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
    auto settle = [&](double seconds, std::function<float(size_t)> src = {}) {
        for (int t = 0; t < (int) (seconds / 0.033); ++t) { play(0.033, src); proc->runHousekeeping(); e->refreshAll(); }
    };
    // full window, or the part of it that `c` covers (2x)
    auto shot = [&](const juce::String& file, juce::Component* c = nullptr, int pad = 0) {
        e->refreshAll();
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        if (c != nullptr)
        {
            auto r = ed->getLocalArea(c, c->getLocalBounds()).expanded(pad).getIntersection(ed->getLocalBounds());
            img = img.getClippedImage(r * 2);
        }
        writePng(img, out.getChildFile(file));
    };
    auto shotUnion = [&](const juce::String& file, juce::Rectangle<int> r) {
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        writePng(img.getClippedImage(r.getIntersection(ed->getLocalBounds()) * 2), out.getChildFile(file));
    };

    // 1. the whole window, the default view (GLOBAL: tuner, Global EQ, effects); the tuner sees a held A string
    auto noteA = [&](size_t i) { const double t = (double) i / sr;
        double v = 0; for (int h = 1; h <= 6; ++h) v += std::sin(2 * juce::MathConstants<double>::pi * 110.0 * h * t) / h;
        return (float) (0.25 * v); };
    settle(1.0);
    settle(0.6, noteA);
    shot("01_full_window.png");
    {
        // 2. the five amp slots (faders, PAN, EDIT / IR / SOLO / MUTE)
        juce::Rectangle<int> r;
        std::function<void(juce::Component&)> slots = [&](juce::Component& c) {
            if (dynamic_cast<ampsurd::ui::SlotComponent*>(&c) != nullptr) r = r.isEmpty() ? ed->getLocalArea(&c, c.getLocalBounds()) : r.getUnion(ed->getLocalArea(&c, c.getLocalBounds()));
            for (auto* ch : c.getChildren()) slots(*ch);
        };
        slots(*ed);
        shotUnion("02_amp_slots.png", r.expanded(4));
    }
    shot("03_global_tuner_eq_effects.png", findVisible<ampsurd::ui::GlobalEqPanel>(*ed), 2);
    shot("04_tuner.png", findVisible<ampsurd::ui::TunerPanel>(*ed), 4);
    settle(0.6);
    shot("05_gate_input_output.png", findVisible<ampsurd::ui::MasterPanel>(*ed), 2);
    shot("06_header_presets_undo_ab.png", findVisible<ampsurd::ui::HeaderBar>(*ed), 2);

    // 3. EDIT: an amp's EQ, INPUT / OUTPUT, alignment; then COPY on amp 1 -> PASTE on amp 2
    setP(*proc, AmpsurdProcessor::slotParamId(0, "eqOn"), 1.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(0, 2, "gain"), 3.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(0, 5, "gain"), -4.0f);
    setP(*proc, AmpsurdProcessor::bandParamId(0, 7, "gain"), 2.5f);
    setP(*proc, AmpsurdProcessor::bandParamId(0, 0, "freq"), 90.0f);
    e->selectSlot(0);
    settle(0.5);
    shot("07_amp_edit_eq.png", findVisible<ampsurd::ui::EditPanel>(*ed), 2);
    proc->copyEq(0);
    e->selectSlot(1);
    settle(0.3);
    shot("08_eq_copy_paste.png", findVisible<ampsurd::ui::EditPanel>(*ed), 2);
    e->selectSlot(-1);

    // 3b. CANCEL COPY on the copied EQ (amp 1)
    e->selectSlot(0);
    settle(0.2);
    shot("08b_eq_cancel_copy.png", findVisible<ampsurd::ui::EditPanel>(*ed), 2);
    proc->clearEqClipboard();
    e->selectSlot(-1);

    // 4. the cabinet IR on amp 2 with its own EQ
    e->showIr(1);
    settle(0.3);
    shot("09_cabinet_ir.png", findVisible<ampsurd::ui::IrPanel>(*ed), 2);
    e->showIr(-1);

    // 5. effects (in GLOBAL): delays, reverb, PITCH
    setP(*proc, "fxD1On", 1.0f); setP(*proc, "fxD1Time", 380.0f);
    setP(*proc, "fxD2On", 1.0f); setP(*proc, "fxD2Time", 756.0f); setP(*proc, "fxD2PingPong", 1.0f);
    setP(*proc, "fxRevOn", 1.0f); setP(*proc, "fxRevType", 3.0f); setP(*proc, "fxRevDecay", 4.5f); setP(*proc, "fxRevPreDelay", 30.0f);
    e->showGlobalEq(true);
    e->showFxPage(0);
    settle(0.3);
    shot("10_effects_delay.png", findVisible<ampsurd::ui::GlobalEqPanel>(*ed), 2);
    e->showFxPage(2);
    settle(0.2);
    shot("11_effects_reverb.png", findVisible<ampsurd::ui::GlobalEqPanel>(*ed), 2);
    setP(*proc, "pitchOn", 1.0f); setP(*proc, "pitchSemi", -2.0f);
    e->showFxPage(4);
    settle(0.2);
    shot("12_pitch_retune.png", findVisible<ampsurd::ui::GlobalEqPanel>(*ed), 2);
    setP(*proc, "pitchOn", 0.0f);
    setP(*proc, "fxD1On", 0.0f); setP(*proc, "fxD2On", 0.0f); setP(*proc, "fxRevOn", 0.0f);
    e->showFxPage(0);

    // 6. Frankenstein: lows from one amp, mids from another, highs from a third
    proc->setFrankenstein(true);
    proc->setFrankensteinSections(3);
    setP(*proc, "frankWidth", 40.0f);
    setP(*proc, "frankAmp1", 1.0f);
    setP(*proc, "frankAmp2", 0.0f);
    setP(*proc, "frankAmp3", 2.0f);
    settle(0.5);
    shot("13_frankenstein.png", findVisible<ampsurd::ui::FrankensteinPanel>(*ed), 2);
    proc->setFrankenstein(false);
    settle(0.3);

   #if AMPSURD_ROOT
    // 7. ROOT (experimental): a low power chord
    setP(*proc, "rootOn", 1.0f);
    setP(*proc, "rootLow", 3.0f);
    e->showRoot(true);
    auto chord = [&](size_t i) { const double t = (double) i / sr;
        return (float) (0.15 * (std::sin(2 * juce::MathConstants<double>::pi * 61.735 * t) + std::sin(2 * juce::MathConstants<double>::pi * 92.499 * t))); };
    settle(0.8, chord);
    shot("14_root.png", findVisible<ampsurd::ui::RootPanel>(*ed), 2);
    e->showRoot(false);
    setP(*proc, "rootOn", 0.0f);
   #endif
   #if AMPSURD_DOUBLER
    // 8. DOUBLER (experimental)
    setP(*proc, "dblOn", 1.0f);
    e->showDoubler(true);
    settle(1.0);
    shot("15_doubler.png", findVisible<ampsurd::ui::DoublerPanel>(*ed), 2);
    e->showDoubler(false);
    setP(*proc, "dblOn", 0.0f);
   #endif

    // 8b. AMPSURD IRs: the map of the synthetic 4x12 IRs, amp 3 between BRIT 30 + DYN and BRIT 30 + RIBBON, the RIBBON row
    // slid three boxes (0.9.8 / 0.9.9: rows slide and move, greyscale boxes with the mm in each)
    {
        e->showAmpsurdIrs(2);
        auto& mx = e->getCabIrMatrix();
        mx.resetRows();
        mx.slideRow(1, 3);
        mx.choose(ampsurd::CabIrModel::atMap(mx.getOrder(), mx.getOffsets(), 6.0, 0.4));
        mx.flush();
        settle(0.8);
        shot("18_ampsurd_irs.png", &mx, 2);
        e->showAmpsurdIrs(-1);
        e->showIr(-1);
    }

    // 8c. a pedal capture before amp 1 (stacked frame behind the slot) and an AMPSURD amp in slot 4 (GAIN / BASS / MID /
    // TREBLE knobs); the amp file comes from AMPSURD_WEB_AMP (an .ampsurdamp), else this shot is skipped
    {
        const auto pedal = tmp.getChildFile("Demo boost pedal.nam");
        juce::File(argv[3]).copyFileTo(pedal);
        proc->loadPedal(0, 0, pedal);
        const juce::File amp(juce::SystemStats::getEnvironmentVariable("AMPSURD_WEB_AMP", ""));
        if (amp.existsAsFile())
        {
            proc->loadCapture(3, amp);
            for (int t = 0; t < 1500 && proc->getSlotStatus(3).state != AmpsurdProcessor::SlotState::loaded; ++t) { play(0.02); pump(20); }
            proc->beginMixGesture(3); proc->setMixLinked(3, 25.0f); proc->endMixGesture(3);
            setP(*proc, AmpsurdProcessor::slotParamId(3, "ampGain"), 6.5f);
            setP(*proc, AmpsurdProcessor::slotParamId(3, "ampBass"), 6.0f);
            setP(*proc, AmpsurdProcessor::slotParamId(3, "ampTreble"), 4.0f);
        }
        for (int t = 0; t < 300 && proc->getPedalStatus(0, 0).state != AmpsurdProcessor::SlotState::loaded; ++t) { play(0.02); pump(20); }
        settle(1.5);
        juce::Rectangle<int> r;
        std::function<void(juce::Component&)> slots = [&](juce::Component& c) {
            if (dynamic_cast<ampsurd::ui::SlotComponent*>(&c) != nullptr) r = r.isEmpty() ? ed->getLocalArea(&c, c.getLocalBounds()) : r.getUnion(ed->getLocalArea(&c, c.getLocalBounds()));
            for (auto* ch : c.getChildren()) slots(*ch);
        };
        slots(*ed);
        shotUnion("19_slots_pedal_and_amp.png", r.expanded(14));
        e->selectSlot(0);
        settle(0.4);
        shot("20_pedals_edit.png", findVisible<ampsurd::ui::EditPanel>(*ed), 2);
        e->selectSlot(-1);
    }

    // 8d. the log: a dropout with what was going on and the likely cause, a problem
    {
        auto& lg = proc->getEventLog();
        lg.clear();
        const int act = lg.beginActivity("loading Demo AMPSURD amp.ampsurdamp into amp 4");
        ampsurd::EventLog::Drop d;
        d.atMs = juce::Time::getMillisecondCounterHiRes();
        d.workMs = 6.8f;
        d.budgetMs = 2.7f;
        d.samples = 128;
        lg.pushDrop(d);
        lg.endActivity(act);
        proc->runHousekeeping();
        lg.add("PROBLEM", "IR of amp 5: IR file not found: D:/IRs/My cab 412.wav");
        e->showLog(true);
        settle(0.4);
        shot("21_log.png", &e->getLogPanel(), 2);
        e->showLog(false);
    }

    // 9. player / recorder with metronome: a backing track, a recorded guitar take
    if (auto* pl = proc->getPlayer())
    {
        pl->loadBacking(backFile);
        pl->setMetronome(true, 120.0, 4, false, -12.0f, 1);
        pl->toStart();
        pl->record();
        settle(5.0);
        pl->stop();
        pl->renderNow();
        pl->setPositionSeconds(2.2);
        pl->play();
        settle(1.0);
        pl->pause();
        e->showPlayer(true);
        settle(0.2);
        shot("16_player_recorder.png", findVisible<ampsurd::ui::PlayerPanel>(*ed), 2);
        shot("16b_full_window_player.png");
        e->showPlayer(false);
    }

    // 10. the preset menu (rendered with AMPSURD's own look) - presets saved for the demo
    {
        const auto folder = proc->getPresetFolder();
        folder.createDirectory();
        for (auto n : { "Clean and crunch", "Demo rig", "Rhythm wall" }) proc->savePreset(folder.getChildFile(juce::String(n) + ".ampsurd"));
        struct Item { juce::String text; bool sep = false, ticked = false, hi = false; };
        std::vector<Item> items { { "Init (empty rig)" }, { {}, true } };
        for (auto n : { "Clean and crunch", "Demo rig", "Rhythm wall" }) items.push_back({ n, false, juce::String(n) == "Demo rig" });
        items.push_back({ {}, true });
        items.push_back({ "Export pack (preset + captures + IRs)...", false, false, true });
        items.push_back({ "Import pack..." });
        items.push_back({ {}, true });
        items.push_back({ "Open preset folder" });
        items.push_back({ "Open packs folder" });
        auto& lf = ed->getLookAndFeel();
        const int w = 320;
        int h = 6;
        std::vector<int> heights;
        for (auto& it : items) { int iw = 0, ih = 0; lf.getIdealPopupMenuItemSize(it.text, it.sep, -1, iw, ih); heights.push_back(ih); h += ih; }
        h += 6;
        juce::Image img(juce::Image::ARGB, w * 2, h * 2, true);
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(2.0f));
        lf.drawPopupMenuBackground(g, w, h);
        int y = 6;
        for (size_t i = 0; i < items.size(); ++i)
        {
            lf.drawPopupMenuItem(g, { 0, y, w, heights[i] }, items[i].sep, true, items[i].hi, items[i].ticked, false, items[i].text, {}, nullptr, nullptr);
            y += heights[i];
        }
        writePng(img, out.getChildFile("17_preset_menu.png"));
    }
    tmp.deleteRecursively();
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::cerr << "Usage: ui_snapshot <outDir> <a.nam> [...]\n"; return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out(argv[1]);
    out.createDirectory();
    if (juce::SystemStats::getEnvironmentVariable("AMPSURD_WEB_SHOTS", "0") == "1")
        return webShots(out, argc, argv); // website screenshots only (set AMPSURD_FORCE_PLAYER=1 too)

    {
        using ampsurd::isNewerVersion;
        const bool ok = isNewerVersion("v0.9.1", "0.9.0") && isNewerVersion("0.10.0", "0.9.9") && isNewerVersion("v1.0.0", "0.9.0")
                        && !isNewerVersion("v0.9.0", "0.9.0") && !isNewerVersion("0.8.9", "0.9.0") && isNewerVersion("0.9.0.1", "0.9.0");
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " UPDATES: version comparison (0.9.1 > 0.9.0, 0.10.0 > 0.9.9, equal is not newer)\n";
    }
    {
        // GitHub's answer with the new kind of releases: a normal (not pre-release) release with fixed file names,
        // an older pre-release, a newer draft (ignored)
        const auto json = juce::JSON::parse(R"([
          {"tag_name":"v0.9.3","draft":true,"prerelease":false,"html_url":"https://github.com/SymbiOrbis/ampsurd/releases/tag/v0.9.3"},
          {"tag_name":"v0.9.2","draft":false,"prerelease":false,"html_url":"https://github.com/SymbiOrbis/ampsurd/releases/tag/v0.9.2",
           "assets":[{"name":"AMPSURD-Windows-Setup.exe"},{"name":"AMPSURD-Windows.zip"},{"name":"AMPSURD-macOS.pkg"},{"name":"AMPSURD-macOS.zip"}]},
          {"tag_name":"v0.9.1","draft":false,"prerelease":true,"html_url":"https://github.com/SymbiOrbis/ampsurd/releases/tag/v0.9.1",
           "assets":[{"name":"AMPSURD-Setup-0.9.1.exe"}]}
        ])");
        const auto info = ampsurd::latestFromReleases(json);
        const bool ok = info.ok && info.latest == "0.9.2" && info.pageUrl.endsWith("/v0.9.2") && ampsurd::isNewerVersion(info.latest, "0.9.1");
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " UPDATES: newest release found by its version (normal release with fixed file names "
                  << "and an older pre-release; draft ignored): " << info.latest << "\n";
    }
    // ALIGN AT ANY RATE (tester 2026-10-28, Mac: "with several amps it sounds phasey"): the automatic alignment
    // must find the same time offset and polarity at 44.1, 48 and 96 kHz
    if (argc >= 4)
    {
        double ref = 0.0, refPol = 1.0, worst = 0.0;
        bool allMeasured = true, polSame = true;
        juce::String detail;
        for (double rate : { 48000.0, 44100.0, 96000.0 })
        {
            auto p = std::make_unique<AmpsurdProcessor>();
            p->prepareToPlay(rate, 256);
            p->setNonRealtime(true);
            p->loadCapture(0, juce::File(argv[2]));
            p->loadCapture(1, juce::File(argv[3]));
            juce::MidiBuffer m;
            juce::AudioBuffer<float> b(2, 256);
            for (int t = 0; t < 900 && !(p->getAlignInfo(0).measured && p->getAlignInfo(1).measured); ++t)
            {
                b.clear();
                p->processBlock(b, m);
                p->runHousekeeping();
                pump(10);
            }
            const auto a0 = p->getAlignInfo(0), a1 = p->getAlignInfo(1);
            allMeasured = allMeasured && a0.measured && a1.measured;
            const double off = a1.autoOffsetMs - a0.autoOffsetMs, pol = a1.polarity * a0.polarity;
            if (rate == 48000.0) { ref = off; refPol = pol; }
            else { worst = juce::jmax(worst, std::abs(off - ref)); polSame = polSame && pol == refPol; }
            detail << juce::String(rate / 1000.0, 1) << " kHz: " << juce::String(off, 3) << " ms" << (pol < 0 ? " inv" : "") << "  ";
        }
        const bool ok = allMeasured && polSame && worst < 0.03;
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " ALIGN AT ANY RATE: offset amp 2 vs amp 1 " << detail << "(max diff "
                  << juce::String(worst, 3) << " ms)\n";
    }

    // LATENCY (owner 2026-10-28: "no significant latency on any modern PC / Mac"): what AMPSURD itself adds (the
    // audio interface's buffers come on top) - two Full A2 amps, everything at default, PITCH off
    if (argc >= 4)
    {
        juce::String detail;
        double worstMs = 0.0;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            auto p = std::make_unique<AmpsurdProcessor>();
            p->prepareToPlay(rate, 128);
            p->loadCapture(0, juce::File(argv[2]));
            p->loadCapture(1, juce::File(argv[3]));
            juce::MidiBuffer m;
            juce::AudioBuffer<float> b(2, 128);
            for (int t = 0; t < 300; ++t) { b.clear(); p->processBlock(b, m); p->runHousekeeping(); pump(5); }
            const double ms = p->getLatencySamples() * 1000.0 / rate;
            worstMs = juce::jmax(worstMs, ms);
            detail << juce::String(rate / 1000.0, 1) << " kHz " << juce::String(ms, 2) << " ms (" << p->getLatencySamples() << " samples)  ";
        }
        std::cout << (worstMs < 3.5 ? "[PASS]" : "[FAIL]") << " LATENCY added by AMPSURD: " << detail << "\n";
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

    // BLEND TONE through the whole plugin: a 50:50 blend of the first two amps vs. each amp alone, octave bands
    if (n >= 2)
    {
        for (int t = 0; t < 600 && !(proc->getAlignInfo(0).measured && proc->getAlignInfo(1).measured); ++t)
        {
            buf.clear();
            proc->processBlock(buf, midi);
            pump(20);
        }
        for (int t = 0; t < 50; ++t) { buf.clear(); proc->processBlock(buf, midi); pump(10); } // covariance -> engine
        const auto test = ampsurd::CaptureAnalyzer::makeTestSignal(48000.0);
        auto render = [&](bool muteA, bool muteB) {
            setP(*proc, AmpsurdProcessor::slotParamId(0, "mute"), muteA ? 1.0f : 0.0f);
            setP(*proc, AmpsurdProcessor::slotParamId(1, "mute"), muteB ? 1.0f : 0.0f);
            if (!muteA && !muteB)
            {
                proc->beginMixGesture(0);
                proc->setMixLinked(0, 50.0f);
                proc->endMixGesture(0);
            }
            juce::AudioBuffer<float> b(2, 256);
            for (int k = 0; k < 60; ++k) { b.clear(); proc->processBlock(b, midi); } // settle (fades, tone EQ)
            std::vector<double> y;
            for (size_t p = 0; p < test.size(); p += 256)
            {
                b.clear();
                const int m = (int) std::min<size_t>(256, test.size() - p);
                for (int i = 0; i < m; ++i) b.setSample(0, i, (float) test[p + (size_t) i]);
                proc->processBlock(b, midi);
                for (int i = 0; i < m; ++i) y.push_back(0.5 * (b.getSample(0, i) + b.getSample(1, i)));
            }
            return y;
        };
        auto band = [](const std::vector<double>& y, double fc) {
            const double w = 2 * 3.14159265358979 * fc / 48000.0, q = 1.414, al = std::sin(w) / (2 * q), a0 = 1 + al;
            const double b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos(w) / a0, a2 = (1 - al) / a0;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0, pw = 0;
            for (size_t i = 0; i < y.size(); ++i)
            {
                const double o = b0 * y[i] + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = y[i]; y2 = y1; y1 = o;
                const double o2 = b0 * o + b2 * u2 - a1 * v1 - a2 * v2;
                u2 = u1; u1 = o; v2 = v1; v1 = o2;
                if (i > 4800) pw += o2 * o2;
            }
            return pw;
        };
        const auto a = render(false, true), b = render(true, false), mix = render(false, false);
        double worst = 0;
        std::cout << "[INFO] plugin 50:50 blend vs. the average of its amps (dB):";
        for (double fc : { 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
        {
            const double d = 10 * std::log10(band(mix, fc) / (0.5 * (band(a, fc) + band(b, fc))));
            worst = std::max(worst, std::abs(d));
            std::cout << " " << (int) fc << " Hz " << juce::String(d, 2);
        }
        std::cout << "\n" << (worst < 1.5 ? "[PASS]" : "[FAIL]") << " BLEND TONE in the plugin: 50:50 within " << worst
                  << " dB of its amps in every octave 250 Hz - 8 kHz\n";
        setP(*proc, AmpsurdProcessor::slotParamId(0, "mute"), 0.0f);
        setP(*proc, AmpsurdProcessor::slotParamId(1, "mute"), 0.0f);
        for (int k = 0; k < 20; ++k) { buf.clear(); proc->processBlock(buf, midi); }
    }

    // MUTE (master row): output to silence after a short fade, back when off; never saved with the state
    {
        auto tone = [&](juce::AudioBuffer<float>& b, int k) {
            b.clear();
            for (int i = 0; i < 256; ++i) b.setSample(0, i, 0.2f * (float) std::sin(2 * 3.14159265 * 110.0 * (k * 256 + i) / 48000.0));
        };
        juce::AudioBuffer<float> b(2, 256);
        float before = 0, muted = 1, after = 0;
        for (int k = 0; k < 20; ++k) { tone(b, k); proc->processBlock(b, midi); before = b.getMagnitude(0, 0, 256); }
        proc->setOutputMuted(true);
        juce::MemoryBlock state;
        proc->getStateInformation(state);
        for (int k = 0; k < 20; ++k) { tone(b, k); proc->processBlock(b, midi); muted = juce::jmax(b.getMagnitude(0, 0, 256), b.getMagnitude(1, 0, 256)); }
        proc->setOutputMuted(false);
        for (int k = 0; k < 20; ++k) { tone(b, k); proc->processBlock(b, midi); after = b.getMagnitude(0, 0, 256); }
        const bool notSaved = !state.toString().contains("utMute");
        std::cout << (before > 0.01f && muted < 1e-6f && after > 0.01f && notSaved ? "[PASS]" : "[FAIL]") << " MUTE: output " << before << " -> "
                  << muted << " muted -> " << after << "; not saved with the state\n";
    }

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
        // FRANKENSTEIN button (tester 2026-10-28: "nothing happened, I pressed it ~5x"): while EDIT or GLOBAL is open,
        // one press must show it (and keep it on); pressing it when it is shown switches it off
        auto frankShown = [&] { e->refreshAll(); auto* f = findVisible<ampsurd::ui::FrankensteinPanel>(*ed); return f != nullptr; };
        e->selectSlot(0);
        const bool hiddenByEdit = proc->isFrankensteinOn() && !frankShown();
        e->frankensteinPressed();
        const bool shownFromEdit = proc->isFrankensteinOn() && frankShown();
        e->showGlobalEq(true);
        e->frankensteinPressed();
        const bool shownFromGlobal = proc->isFrankensteinOn() && frankShown();
        e->frankensteinPressed();
        const bool offWhenShown = !proc->isFrankensteinOn() && !frankShown();
        e->selectSlot(0);
        e->frankensteinPressed();
        const bool onFromEdit = proc->isFrankensteinOn() && frankShown();
        const bool ok = hiddenByEdit && shownFromEdit && shownFromGlobal && offWhenShown && onFromEdit;
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " FRANKENSTEIN button: one press shows it from EDIT / GLOBAL (" << hiddenByEdit << shownFromEdit
                  << shownFromGlobal << "), a press while shown switches it off (" << offWhenShown << "), off + EDIT -> on and shown ("
                  << onFromEdit << ")\n";
    }

    // AMPSURD IRs (owner 2026-10-28): the map of the synthetic 4x12 IRs - choose, hover (no undo steps), presets,
    // packs, saved WAV = the owner's file
    {
        proc->setFrankenstein(false);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        auto& mx = e->getCabIrMatrix();
        auto settleIr = [&](int ms) {
            for (int t = 0; t < ms / 20; ++t)
            {
                buf.clear();
                proc->processBlock(buf, midi);
                proc->runHousekeeping();
                e->refreshAll();
                pump(20);
            }
        };
        proc->removeIr(0);
        settleIr(600);
        e->showAmpsurdIrs(0);
        const bool shown = mx.isVisible() && mx.getSlot() == 0;
        mx.setHoverToListen(true);
        mx.setSnap(false);
        // the map's corners are the set's corners
        const auto tl = mx.pointAt(mx.positionOf({ 0, 25 }).toInt()), br = mx.pointAt(mx.positionOf({ 8, 100 }).toInt());
        // (0.9.8: a point is the pair form on the map - check its place and what is heard there)
        const bool mapOk = std::abs(tl.y) < 0.05 && std::abs(tl.x) < 0.05 && tl.row == 0.0 && std::abs(tl.mm - 25) < 0.3 && tl.t < 0.05
                           && std::abs(br.y - 8) < 0.05 && std::abs(br.x - 15) < 0.05 && br.rowB == 8 && br.t > 0.95 && std::abs(br.mmB - 100) < 0.3;
        if (!mapOk) std::cout << "  map corners: " << ampsurd::CabIrModel::toRef(tl) << " / " << ampsurd::CabIrModel::toRef(br) << "\n";
        mx.choose({ 3.0, 45.0 }); // HEAVY 77 + DYN, 45 mm
        mx.flush();
        settleIr(900);
        ampsurd::CabPoint got;
        const bool chosen = proc->getCabIrPoint(0, got) && got.row == 3.0 && got.mm == 45.0
                            && proc->getIrStatus(0).state == AmpsurdProcessor::IrState::loaded && proc->isIrOn(0);
        // hovering: heard, but no undo step; leaving the map goes back to the chosen point
        mx.preview({ 7.5, 80.0 });
        mx.flush();
        settleIr(900);
        const bool previewHeard = proc->getCabIrPoint(0, got) && got.row == 7.0 && got.rowB == 8 && std::abs(got.t - 0.5) < 1e-6 && got.mm == 80.0;
        mx.endPreview();
        settleIr(900);
        const bool back = proc->getCabIrPoint(0, got) && got.row == 3.0 && got.mm == 45.0;
        e->showAmpsurdIrs(0);
        save(*ed, out.getChildFile("22_ampsurd_irs.png"));
        proc->undo(); // one step: choosing the IR (the preview was none) -> no IR
        settleIr(600);
        const bool undoOk = proc->getIrStatus(0).state == AmpsurdProcessor::IrState::none;
        proc->redo();
        settleIr(900);
        const bool redoOk = proc->getCabIrPoint(0, got) && got.row == 3.0;
        // presets keep the point (no file); packs need no file for it
        const auto pf = out.getChildFile("ampsurd_ir_test.ampsurd");
        proc->savePreset(pf);
        proc->removeIr(0);
        settleIr(300);
        proc->loadPreset(pf);
        settleIr(1500);
        const bool presetOk = proc->getCabIrPoint(0, got) && got.row == 3.0 && got.mm == 45.0
                              && proc->getIrStatus(0).state == AmpsurdProcessor::IrState::loaded;
        juce::String packErr;
        const auto pack = out.getChildFile("ampsurd_ir_test.ampsurdpack");
        const bool packOk = proc->exportPack(pack, packErr);
        bool packHasWav = false;
        {
            juce::ZipFile z(pack);
            for (int i = 0; i < z.getNumEntries(); ++i) packHasWav = packHasWav || z.getEntry(i)->filename.endsWithIgnoreCase(".wav");
        }
        pack.deleteFile();
        pf.deleteFile();
        // SAVE IR = the owner's file (same level, same samples)
        bool fileOk = false;
        const juce::File ref = juce::File(juce::String(AMPSURD_SOURCE_DIR)).getChildFile("tests/data/ampsurd_irs/48k/AMPSURD-412-30-57-P025mm.wav");
        if (ref.existsAsFile())
        {
            juce::String err;
            const auto mine = out.getChildFile("AMPSURD-412-30-57-P025mm.wav");
            if (ampsurd::ui::CabIrMatrix::writeIrFile({ 0, 25 }, 48000.0, mine, err))
            {
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> a(fm.createReaderFor(ref)), b(fm.createReaderFor(mine));
                if (a && b && a->lengthInSamples == b->lengthInSamples)
                {
                    juce::AudioBuffer<float> x(1, (int) a->lengthInSamples), y(1, (int) b->lengthInSamples);
                    a->read(&x, 0, x.getNumSamples(), 0, true, false);
                    b->read(&y, 0, y.getNumSamples(), 0, true, false);
                    float d = 0.0f;
                    for (int i = 0; i < x.getNumSamples(); ++i) d = juce::jmax(d, std::abs(x.getSample(0, i) - y.getSample(0, i)));
                    fileOk = d <= 2.5f / 8388608.0f; // within 24-bit rounding
                    std::cout << "  saved IR vs the owner's file: largest difference " << d * 8388608.0f << " LSB (24-bit)\n";
                }
            }
            mine.deleteFile();
        }
        // the sound: not silent, not louder than the safety limit
        float peak = 0.0f;
        for (int k = 0; k < 100; ++k)
        {
            juce::AudioBuffer<float> b(2, 256);
            for (int i = 0; i < 256; ++i) b.setSample(0, i, 0.2f * (float) std::sin(2 * juce::MathConstants<double>::pi * 110 * (k * 256 + i) / 48000.0));
            b.setSample(1, 0, 0.0f);
            proc->processBlock(b, midi);
            if (k > 50) peak = juce::jmax(peak, b.getMagnitude(0, 0, 256));
        }
        // SLIDING / MOVING ROWS (owner 2026-11-01): the chosen point stays on the screen, the rows move under it
        bool rowsOk = false;
        {
            mx.choose({ 3.0, 45.0 }); // display row 3 = HEAVY 77 + DYN, box 4 = 45 mm
            mx.flush();
            settleIr(600);
            mx.slideRow(3, 2); // HEAVY 77 DYN two boxes right: box 4 now shows 35 mm
            mx.flush();
            settleIr(900);
            ampsurd::CabPoint a, b;
            const bool slid = proc->getCabIrPoint(0, a) && a.row == 3.0 && a.mm == 35.0 && a.hasLayout && a.offset[3] == 2;
            mx.moveRow(5, 4); // HEAVY 77 DYN WIDE (display row 5) up above HEAVY 77 RIBBON
            mx.choose(ampsurd::CabIrModel::atMap(mx.getOrder(), mx.getOffsets(), 4.0, 3.5)); // between HEAVY 77 DYN and HEAVY 77 DYN WIDE
            mx.flush();
            settleIr(900);
            const bool moved = proc->getCabIrPoint(0, b) && mx.getOrder()[4] == ampsurd::CabIrModel::rowOf(1, 1) && b.row == 3.0
                               && b.mm == 35.0 && b.rowB == ampsurd::CabIrModel::rowOf(1, 1) && b.mmB == 45.0 && std::abs(b.t - 0.5) < 1e-6;
            e->showAmpsurdIrs(0);
            save(*ed, out.getChildFile("22b_ampsurd_irs_rows.png"));
            // a preset keeps the map; RESET ROWS = the default map again
            const auto pf2 = out.getChildFile("ampsurd_ir_rows.ampsurd");
            proc->savePreset(pf2);
            mx.resetRows();
            mx.flush();
            settleIr(900);
            ampsurd::CabPoint c, d;
            const bool reset = proc->getCabIrPoint(0, c) && c.offset == std::array<int, 9> {} && c.order[4] == 4;
            proc->loadPreset(pf2);
            settleIr(1500);
            const bool kept = proc->getCabIrPoint(0, d) && d == b;
            pf2.deleteFile();
            // with the mouse on the boxes: dragging sideways slides that row, a click chooses
            auto mev = [&](juce::Point<int> p, juce::Point<int> down, bool dragged) {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), juce::ModifierKeys(),
                                        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &mx, &mx, juce::Time::getCurrentTime(), down.toFloat(),
                                        juce::Time::getCurrentTime(), 1, dragged);
            };
            const auto area = mx.getMapArea();
            const int cw = area.getWidth() / 16, rh = area.getHeight() / 9;
            const juce::Point<int> start { area.getX() + cw * 3 + cw / 2, area.getY() + rh / 2 }; // display row 0, box 3
            const auto before = mx.getChosen();
            mx.mouseDown(mev(start, start, false));
            mx.mouseDrag(mev(start + juce::Point<int> { cw, 2 }, start, true));
            mx.mouseDrag(mev(start + juce::Point<int> { 2 * cw, 3 }, start, true));
            mx.mouseUp(mev(start + juce::Point<int> { 2 * cw, 3 }, start, true));
            const bool mouseSlide = mx.getOffsets()[0] == 2 && mx.getChosen().x == before.x && mx.getChosen().y == before.y;
            const juce::Point<int> clickAt { area.getX() + cw * 10 + cw / 2, area.getY() + rh * 6 + rh / 2 }; // box 10, row 6
            mx.mouseDown(mev(clickAt, clickAt, false));
            mx.mouseUp(mev(clickAt, clickAt, false));
            const bool mouseClick = std::abs(mx.getChosen().x - 10.0) < 0.05 && std::abs(mx.getChosen().y - 6.0) < 0.05;
            mx.resetRows();
            std::cout << "  rows with the mouse: drag sideways slides the row " << mouseSlide << ", a click chooses " << mouseClick << "\n";
            rowsOk = slid && moved && reset && kept && mouseSlide && mouseClick;
            std::cout << "  rows: slid " << slid << ", moved " << moved << " (" << ampsurd::CabIrModel::describe(b) << "), reset " << reset
                      << ", preset keeps the map " << kept << "\n";
        }
        e->showAmpsurdIrs(-1);
        const bool closed = !mx.isVisible();
        const bool ok = shown && mapOk && chosen && previewHeard && back && undoOk && redoOk && presetOk && packOk && !packHasWav
                        && (fileOk || !ref.existsAsFile()) && peak > 0.001f && peak < 0.9f && closed && rowsOk;
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " AMPSURD IRs: map " << shown << mapOk << ", choose " << chosen << ", hover heard "
                  << previewHeard << " and back " << back << ", one undo step (undo " << undoOk << " redo " << redoOk << "), preset "
                  << presetOk << ", pack without files " << (packOk && !packHasWav) << ", SAVE IR = owner's file " << fileOk
                  << ", sound peak " << peak << ", closes " << closed << ", rows " << rowsOk << "\n";
        proc->removeIr(0);
        settleIr(300);
    }

    // PEDALS (serial chain, owner 2026-10-28): a capture BEFORE amp 1; measured with it; bypass; presets; packs
    if (argc >= 4)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        auto run = [&](int ms) {
            for (int t = 0; t < ms / 20; ++t) { buf.clear(); proc->processBlock(buf, midi); proc->runHousekeeping(); e->refreshAll(); pump(20); }
        };
        // a steady A note through the rig; RMS of the left output
        auto rms = [&] {
            double sum = 0.0;
            int cnt = 0;
            for (int k = 0; k < 120; ++k)
            {
                juce::AudioBuffer<float> b(2, 256);
                for (int i = 0; i < 256; ++i)
                {
                    const double t = (k * 256 + i) / 48000.0;
                    b.setSample(0, i, (float) (0.15 * std::sin(2 * juce::MathConstants<double>::pi * 110.0 * t) + 0.05 * std::sin(2 * juce::MathConstants<double>::pi * 330.0 * t)));
                    b.setSample(1, i, 0.0f);
                }
                proc->processBlock(b, midi);
                if (k >= 40) for (int i = 0; i < 256; ++i) { sum += b.getSample(0, i) * b.getSample(0, i); ++cnt; }
            }
            return std::sqrt(sum / juce::jmax(1, cnt));
        };
        setP(*proc, "gateOn", 0.0f);
        run(300);
        const int latBefore = proc->getLatencySamples();
        const double noPedal = rms();
        const auto alignBefore = proc->getAlignInfo(0);
        proc->loadPedal(0, 0, juce::File(argv[3]));
        for (int t = 0; t < 300 && proc->getPedalStatus(0, 0).state != AmpsurdProcessor::SlotState::loaded; ++t) run(20);
        run(2500); // re-measured with the pedal
        const bool loaded = proc->getPedalStatus(0, 0).state == AmpsurdProcessor::SlotState::loaded;
        const auto alignAfter = proc->getAlignInfo(0);
        const double withPedal = rms();
        e->selectSlot(0);
        proc->loadPedal(0, 1, juce::File(argv[2])); // a second pedal (bypassed) for the stacked frames
        for (int t = 0; t < 300 && proc->getPedalStatus(0, 1).state != AmpsurdProcessor::SlotState::loaded; ++t) run(20);
        setP(*proc, AmpsurdProcessor::slotParamId(0, "ped2On"), 0.0f);
        run(400);
        ampsurd::ui::SlotComponent* slot1 = nullptr;
        std::function<void(juce::Component&)> findSlot = [&](juce::Component& c) {
            if (auto* sc = dynamic_cast<ampsurd::ui::SlotComponent*>(&c)) if (slot1 == nullptr) slot1 = sc;
            for (auto* ch : c.getChildren()) findSlot(*ch);
        };
        findSlot(*ed);
        const auto layers = slot1 != nullptr ? slot1->getPedalLayers() : std::array<int, 2> {};
        const bool frames = layers[0] == 2 && layers[1] == 1; // next to the amp: pedal 2 (bypassed, dimmed), behind it: pedal 1
        std::cout << (frames ? "[PASS]" : "[FAIL]") << " PEDAL FRAMES: two pedals = two frames behind the slot, the bypassed one dimmed ("
                  << layers[0] << "," << layers[1] << ")\n";
        // PEDAL 1 / PEDAL 2 on the slot (owner 2026-11-06): shown for loaded pedals, lit = on, a click turns it on / off
        {
            auto* b1 = slot1 != nullptr ? slot1->getPedalButton(0) : nullptr;
            auto* b2 = slot1 != nullptr ? slot1->getPedalButton(1) : nullptr;
            const bool shownOk = b1 != nullptr && b2 != nullptr && b1->isVisible() && b2->isVisible()
                                 && b1->getToggleState() && !b2->getToggleState();
            auto pOn = [&](const char* id) { return proc->params.getRawParameterValue(AmpsurdProcessor::slotParamId(0, id))->load() > 0.5f; };
            bool clickOk = false, placeOk = false;
            if (shownOk)
            {
                b1->setToggleState(!b1->getToggleState(), juce::sendNotificationSync);
                pump(50);
                const bool off = !pOn("ped1On");
                b2->setToggleState(!b2->getToggleState(), juce::sendNotificationSync);
                pump(50);
                const bool on2 = pOn("ped2On");
                b1->setToggleState(!b1->getToggleState(), juce::sendNotificationSync);
                b2->setToggleState(!b2->getToggleState(), juce::sendNotificationSync);
                pump(50);
                clickOk = off && on2 && pOn("ped1On") && !pOn("ped2On");
                placeOk = !b1->getBounds().intersects(b2->getBounds()) && slot1->getLocalBounds().contains(b1->getBounds())
                          && slot1->getLocalBounds().contains(b2->getBounds())
                          && b1->getBottom() <= 170 && b1->getY() >= slot1->getNameArea().getBottom();
            }
            run(400);
            if (slot1 != nullptr)
            {
                juce::PNGImageFormat png;
                const auto f = out.getChildFile("23b_slot_pedal_buttons.png");
                f.deleteFile();
                juce::FileOutputStream os(f);
                png.writeImageToStream(slot1->createComponentSnapshot(slot1->getLocalBounds(), true, 2.0f), os);
            }
            const bool ok = shownOk && clickOk && placeOk;
            std::cout << (ok ? "[PASS]" : "[FAIL]") << " PEDAL BUTTONS ON THE SLOT: shown " << shownOk << ", click turns on / off "
                      << clickOk << ", placed " << placeOk << "\n";
        }
        // the new AMPSURD logo in the header (owner 2026-11-07): loaded, larger than the old text, left of PLAYER / REC
        {
            auto* hb = findVisible<ampsurd::ui::HeaderBar>(*ed);
            const auto la = hb != nullptr ? hb->logoArea() : juce::Rectangle<float>();
            const bool logoOk = hb != nullptr && hb->hasLogo() && la.getRight() <= 205.0f && la.getX() >= 20.0f && la.getY() >= 0.0f
                                && la.getBottom() <= (float) hb->getHeight() && la.getWidth() >= 170.0f;
            if (hb != nullptr)
            {
                juce::PNGImageFormat png;
                const auto f = out.getChildFile("00_header_logo.png");
                f.deleteFile();
                juce::FileOutputStream os(f);
                png.writeImageToStream(hb->createComponentSnapshot(hb->getLocalBounds().withWidth(420), true, 2.0f), os);
            }
            std::cout << (logoOk ? "[PASS]" : "[FAIL]") << " HEADER LOGO: the new logo is drawn at " << la.toString() << "\n";
        }
        save(*ed, out.getChildFile("23_pedal_chain.png"));
        proc->removePedal(0, 1);
        setP(*proc, AmpsurdProcessor::slotParamId(0, "ped2On"), 1.0f);
        run(2500);
        // bypass -> the amp alone again
        setP(*proc, AmpsurdProcessor::slotParamId(0, "ped1On"), 0.0f);
        run(2500);
        const double bypassed = rms();
        setP(*proc, AmpsurdProcessor::slotParamId(0, "ped1On"), 1.0f);
        run(2500);
        // preset keeps the pedal; pack carries its file
        const auto pf = out.getChildFile("pedal_test.ampsurd");
        proc->savePreset(pf);
        proc->removePedal(0, 0);
        run(300);
        const bool removed = proc->getPedalStatus(0, 0).state == AmpsurdProcessor::SlotState::empty;
        proc->loadPreset(pf);
        for (int t = 0; t < 300 && proc->getPedalStatus(0, 0).state != AmpsurdProcessor::SlotState::loaded; ++t) run(20);
        const bool presetOk = proc->getPedalStatus(0, 0).state == AmpsurdProcessor::SlotState::loaded
                              && juce::File(proc->getPedalStatus(0, 0).path) == juce::File(argv[3]);
        juce::String err;
        const auto pack = out.getChildFile("pedal_test.ampsurdpack");
        bool packHasPedal = false;
        if (proc->exportPack(pack, err))
        {
            juce::ZipFile z(pack);
            for (int i = 0; i < z.getNumEntries(); ++i) packHasPedal = packHasPedal || z.getEntry(i)->filename == juce::File(argv[3]).getFileName();
        }
        pack.deleteFile();
        pf.deleteFile();
        // REMOVE (the whole slot) removes its pedals too
        proc->removePedal(0, 0);
        run(2500);
        const double afterRemove = rms();
        const int latAfter = proc->getLatencySamples();
        e->selectSlot(-1);
        const bool soundOk = withPedal > 0.0 && std::abs(20.0 * std::log10(withPedal / noPedal)) > 0.05
                             && std::abs(20.0 * std::log10(bypassed / noPedal)) < 0.3 && std::abs(20.0 * std::log10(afterRemove / noPedal)) < 0.3;
        const bool ok = loaded && alignAfter.measured && soundOk && removed && presetOk && packHasPedal && latAfter == latBefore;
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " PEDALS: a capture before amp 1 loads (" << loaded << "), the amp is measured with it ("
                  << alignAfter.measured << ", loudness " << juce::String(alignBefore.loudnessDb, 1) << " -> " << juce::String(alignAfter.loudnessDb, 1)
                  << " dB), sound " << juce::String(20.0 * std::log10(noPedal), 1) << " / with pedal " << juce::String(20.0 * std::log10(withPedal), 1)
                  << " / bypassed " << juce::String(20.0 * std::log10(bypassed), 1) << " / removed " << juce::String(20.0 * std::log10(afterRemove), 1)
                  << " dBFS, preset " << presetOk << ", pack carries it " << packHasPedal << ", latency " << latBefore << " -> " << latAfter << "\n";
        setP(*proc, "gateOn", 1.0f);
    }

    // LOG (owner 2026-11-01): a dropout lists what was going on and the likely cause; problems are listed; the log
    // opens from DROPOUTS in the footer and shows the entries
    {
        auto& lg = proc->getEventLog();
        lg.clear();
        const int act = lg.beginActivity("loading Test amp.ampsurdamp into amp 2");
        ampsurd::EventLog::Drop d;
        d.atMs = juce::Time::getMillisecondCounterHiRes();
        d.workMs = 7.5f;
        d.budgetMs = 2.7f;
        d.samples = 128;
        lg.pushDrop(d);
        lg.endActivity(act);
        proc->runHousekeeping();
        const auto t1 = lg.getText();
        bool dropOk = t1.contains("DROPOUT") && t1.contains("7.5 ms") && t1.contains("loading Test amp.ampsurdamp into amp 2")
                            && t1.contains("Likely cause: that background work");
        // a storm: 50 more within the same second are summed up in one line, not listed one by one
        for (int i = 0; i < 50; ++i) { d.atMs += 5.0; lg.pushDrop(d); }
        proc->runHousekeeping();
        const int before = lg.getNumEntries();
        pump(1100);
        proc->runHousekeeping();
        const bool stormOk = before == 1 && lg.getNumEntries() == 2 && lg.getText().contains("50 more dropout(s)");
        dropOk = dropOk && stormOk;
        proc->loadIr(4, out.getChildFile("no such ir.wav"));
        for (int t = 0; t < 100 && !lg.getText().contains("PROBLEM"); ++t) { proc->runHousekeeping(); pump(20); }
        const bool problemOk = lg.getText().contains("PROBLEM") && lg.getText().contains("IR of amp 5");
        proc->removeIr(4);
        std::unique_ptr<juce::AudioProcessorEditor> led(proc->createEditor());
        auto* le = dynamic_cast<AmpsurdEditor*>(led.get());
        le->setSize(1200, 800);
        le->showLog(true);
        le->refreshAll();
        pump(300);
        const bool shownOk = le->getLogPanel().isVisible() && le->getLogPanel().getShownText().contains("loading Test amp");
        save(*led, out.getChildFile("31_log.png"));
        le->showLog(false);
        const bool fileOk = lg.getFile().existsAsFile() && lg.getFile().loadFileAsString().contains("loading Test amp");
        const bool ok = dropOk && problemOk && shownOk && !le->getLogPanel().isVisible() && fileOk;
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " LOG: a dropout with what was going on and the likely cause (" << dropOk
                  << "), a missing IR as a problem (" << problemOk << "), shown in the log panel (" << shownOk << "), written to the file ("
                  << fileOk << ")\n";
        if (!ok) std::cout << t1 << "\n";

        // how long AMPSURD really ran (owner 2026-11-07): a busy 30 ms runs ~30 ms, a 30 ms sleep runs ~0 ms;
        // a dropout block where AMPSURD ran little = "the computer paused AMPSURD"
        {
            auto spin = [](double ms) {
                const auto a = ampsurd::ThreadCpuClock::now();
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                volatile double x = 0.0;
                while (juce::Time::getMillisecondCounterHiRes() - t0 < ms) x = x + 1.0;
                return ampsurd::ThreadCpuClock::ranMs(a, ampsurd::ThreadCpuClock::now(), juce::Time::getMillisecondCounterHiRes() - t0);
            };
            auto nap = [](double ms) {
                const auto a = ampsurd::ThreadCpuClock::now();
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                juce::Thread::sleep((int) ms);
                return ampsurd::ThreadCpuClock::ranMs(a, ampsurd::ThreadCpuClock::now(), juce::Time::getMillisecondCounterHiRes() - t0);
            };
            const double busy = spin(30.0), idle = nap(30.0);
            const bool clockOk = busy > 20.0 && busy <= 31.0 && idle >= 0.0 && idle < 3.0;
            pump(1100);
            lg.clear();
            ampsurd::EventLog::Drop p;
            p.atMs = juce::Time::getMillisecondCounterHiRes();
            p.workMs = 44.7f;
            p.budgetMs = 2.7f;
            p.samples = 128;
            p.ranMs = 1.2f;
            lg.pushDrop(p);
            proc->runHousekeeping();
            const auto tp = lg.getText();
            const bool pausedOk = tp.contains("worked 1.2 ms of it") && tp.contains("the computer paused AMPSURD");
            pump(1100);
            lg.clear();
            p.atMs = juce::Time::getMillisecondCounterHiRes();
            p.ranMs = 44.0f;
            lg.pushDrop(p);
            proc->runHousekeeping();
            const auto ts = lg.getText();
            const bool slowOk = ts.contains("worked 44.0 ms of it") && !ts.contains("paused") && ts.contains("very small buffer");
            pump(1100);
            lg.clear();
            p.atMs = juce::Time::getMillisecondCounterHiRes();
            p.ranMs = 44.0f;
            p.helperWaitMs = 40.0f;
            lg.pushDrop(p);
            proc->runHousekeeping();
            const auto th = lg.getText();
            const bool helperOk = th.contains("waited 40.0 ms for a helper core") && th.contains("paused one of AMPSURD's helper threads");
            const bool ok2 = clockOk && pausedOk && slowOk && helperOk;
            std::cout << (ok2 ? "[PASS]" : "[FAIL]") << " LOG: real run time (busy 30 ms ran " << busy << " ms, a 30 ms sleep ran " << idle
                      << " ms); paused " << pausedOk << ", really slow " << slowOk << ", helper paused " << helperOk << "\n";
            if (!ok2) std::cout << tp << "\n" << ts << "\n" << th << "\n";
            lg.clear();
        }
    }

    // TWO GAIN CAPTURES ON TWO CORES (owner 2026-11-07): an AMPSURD amp between two GAIN positions hands the upper
    // capture to a free core. The sound must be exactly the same as on one core, and a helper must really take it.
    {
        auto makeAmp = [&] {
            std::vector<ampsurd::CaptureModel::MorphStep> st(2);
            st[0].model = std::move(ampsurd::CaptureModel::load(argv[2], 48000.0, 128).model);
            st[1].model = std::move(ampsurd::CaptureModel::load(argv[3], 48000.0, 128).model);
            st[0].knob = 2.0;
            st[1].knob = 8.0;
            st[1].delay = 1.3;
            st[0].pairLoss = 0.1;
            ampsurd::CaptureInfo info;
            info.displayName = "two cores test";
            auto m = ampsurd::CaptureModel::makeMorph(std::move(st), info, 48000.0, 128);
            if (m) m->setMorphPosition(4.5);
            return m;
        };
        auto serial = makeAmp(), parallel = makeAmp();
        bool sameOk = serial != nullptr && parallel != nullptr;
        int taken = 0;
        double worst = 0.0;
        if (sameOk)
        {
            ampsurd::ParallelRunner runner;
            runner.start(2);
            juce::Random rnd(7);
            std::vector<double> in(128), a(128), b(128);
            struct Ctx { ampsurd::CaptureModel* m; const double* in; double* out; } ctx { parallel.get(), in.data(), b.data() };
            for (int blk = 0; blk < 400; ++blk)
            {
                for (auto& x : in) x = 0.2 * (rnd.nextDouble() * 2.0 - 1.0) * std::sin(0.01 * blk);
                if (blk == 200) { serial->setMorphPosition(6.0); parallel->setMorphPosition(6.0); } // moving GAIN too
                serial->process(in.data(), a.data(), 128, false);
                runner.run(1, [](void* c, int) noexcept {
                    auto& x = *static_cast<Ctx*>(c);
                    x.m->process(x.in, x.out, 128, false);
                }, &ctx, true);
                for (int k = 0; k < 128; ++k) worst = std::max(worst, std::abs(a[(size_t) k] - b[(size_t) k]));
            }
            taken = runner.sideTakenByOthers();
            runner.stop();
            sameOk = worst == 0.0;
        }
        const bool ok = sameOk && taken > 0;
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " TWO GAIN CAPTURES ON TWO CORES: same sound as one core (largest difference "
                  << worst << "), a helper core took the upper capture in " << taken << " of 400 blocks\n";
    }

    // AMPSURD AMP (.ampsurdamp, owner 2026-10-30): built from a folder of "recordings" named as in the recording sheet
    // (two captures as GAIN 9:00 / 3:00, synthetic tone sweeps with known shelves), loaded into slot 4: knobs on the
    // slot, GAIN morphs, BASS / TREBLE change the sound as measured, presets keep the knobs
    if (argc >= 4)
    {
        auto run = [&](int ms) { for (int t = 0; t < ms / 20; ++t) { buf.clear(); proc->processBlock(buf, midi); proc->runHousekeeping(); pump(20); } };
        using AP = AmpsurdProcessor;
        const auto dir = out.getChildFile("amp recordings");
        dir.deleteRecursively();
        dir.createDirectory();
        juce::File(argv[2]).copyFileTo(dir.getChildFile("G1_gain_0745.nam")); // recorded down to 7:45 -> from silence
        juce::File(argv[3]).copyFileTo(dir.getChildFile("CH1_G7_gain_1500.nam"));
        const auto sweep = ampsurd::ToneSweep::make();
        auto shelf = [](std::vector<double> x, bool low, double gainDb) {
            const double fs = 48000.0, f0 = low ? 150.0 : 3000.0, A = std::pow(10.0, gainDb / 40.0), w = 2 * juce::MathConstants<double>::pi * f0 / fs;
            const double a = std::sin(w) / (2 * 0.7), c = std::cos(w), sq = 2 * std::sqrt(A) * a;
            double b0, b1, b2, a0, a1, a2;
            if (low) { b0 = A*((A+1)-(A-1)*c+sq); b1 = 2*A*((A-1)-(A+1)*c); b2 = A*((A+1)-(A-1)*c-sq); a0 = (A+1)+(A-1)*c+sq; a1 = -2*((A-1)+(A+1)*c); a2 = (A+1)+(A-1)*c-sq; }
            else { b0 = A*((A+1)+(A-1)*c+sq); b1 = -2*A*((A-1)+(A+1)*c); b2 = A*((A+1)+(A-1)*c-sq); a0 = (A+1)-(A-1)*c+sq; a1 = 2*((A-1)-(A+1)*c); a2 = (A+1)-(A-1)*c-sq; }
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : x) { const double y = (b0*v + b1*x1 + b2*x2 - a1*y1 - a2*y2) / a0; x2 = x1; x1 = v; y2 = y1; y1 = y; v = y; }
            return x;
        };
        auto writeRec = [&](const juce::String& name, const std::vector<double>& x) {
            juce::AudioBuffer<float> b(1, (int) x.size() + 2000);
            b.clear();
            for (size_t i = 0; i < x.size(); ++i) b.setSample(0, (int) i + 200, (float) (0.5 * x[i])); // latency 200 samples
            writeWav(dir.getChildFile(name + ".wav"), b, 48000.0);
        };
        writeRec("T00_ref_all_noon", sweep);
        writeRec("CH1_T13_ref_all_noon", sweep); // a channel number in front is ignored
        writeRec("T01_bass_min", shelf(sweep, true, -9.0));
        writeRec("T02_bass_0900", shelf(sweep, true, -5.0));
        writeRec("T03_bass_1500", shelf(sweep, true, 5.0));
        writeRec("T04_bass_max", shelf(sweep, true, 9.0));
        writeRec("T09_treble_min", shelf(sweep, false, -8.0));
        writeRec("T12_treble_max", shelf(sweep, false, 8.0));
        // THE CAPTURE CHECK (owner 2026-11-05): a faulty capture stops the build - tiny NAM "Linear" captures with known
        // faults: one 4 ms late, a duplicate, a swapped gain order, a broken file, a clipped sweep; a clean set builds
        bool checkOk = false, checkNoFadeFrom9 = false;
        juce::String checkWhy;
        {
            auto linearNam = [](const juce::File& f, int impulseAt, double gain, double loudness = 0.0) {
                juce::String w;
                for (int i = 0; i < 256; ++i) w << (i ? "," : "") << (i == impulseAt ? "1.0" : "0.0");
                f.replaceWithText("{\"version\":\"0.5.4\",\"architecture\":\"Linear\",\"sample_rate\":48000,"
                                  "\"config\":{\"receptive_field\":256,\"bias\":false,\"in_channels\":1,\"out_channels\":1},"
                                  "\"metadata\":{\"gain\":" + juce::String(gain, 2)
                                  + (loudness != 0.0 ? ",\"loudness\":" + juce::String(loudness, 2) : juce::String()) + "},\"weights\":[" + w + "]}");
            };
            const auto cdir = out.getChildFile("amp check");
            auto fresh = [&] { cdir.deleteRecursively(); cdir.createDirectory(); };
            auto tryBuild = [&](juce::String& err) {
                juce::String rep;
                return ampsurd::amp::build(cdir, "Check", "ui_snapshot", out.getChildFile("check.ampsurdamp"), rep, err);
            };
            juce::String e1, e2, e3, e4, e5, e6, e7, e8;
            fresh(); // 1. clean: three captures, rising gain -> builds
            linearNam(cdir.getChildFile("G1_gain_0900.nam"), 10, 0.2); linearNam(cdir.getChildFile("G2_gain_1100.nam"), 10, 0.4);
            linearNam(cdir.getChildFile("G3_gain_1300.nam"), 10, 0.6);
            const bool clean = tryBuild(e1);
            ampsurd::amp::Profile cp;
            juce::String ce;
            checkNoFadeFrom9 = clean && ampsurd::amp::read(out.getChildFile("check.ampsurdamp"), cp, ce) && cp.silenceFloor < 0.0;
            linearNam(cdir.getChildFile("G4_gain_1500.nam"), 210, 0.8); // 2. + one 200 samples (4.2 ms) off -> stops
            const bool late = !tryBuild(e2) && e2.contains("NOT built") && e2.contains("G4_gain_1500") && e2.contains("ms");
            fresh(); // 3. the same capture twice
            linearNam(cdir.getChildFile("G1_gain_0900.nam"), 10, 0.2); linearNam(cdir.getChildFile("G2_gain_1100.nam"), 10, 0.4);
            cdir.getChildFile("G1_gain_0900.nam").copyFileTo(cdir.getChildFile("G3_gain_1300.nam"));
            const bool dup = !tryBuild(e3) && e3.contains("same capture");
            fresh(); // 4. gain falls: files named wrong
            linearNam(cdir.getChildFile("G1_gain_0900.nam"), 10, 0.2); linearNam(cdir.getChildFile("G2_gain_1100.nam"), 10, 0.6);
            linearNam(cdir.getChildFile("G3_gain_1300.nam"), 10, 0.4);
            const bool order = !tryBuild(e4) && e4.contains("less gain");
            fresh(); // 5. a broken file
            linearNam(cdir.getChildFile("G1_gain_0900.nam"), 10, 0.2); linearNam(cdir.getChildFile("G2_gain_1100.nam"), 10, 0.4);
            cdir.getChildFile("G3_gain_1300.nam").replaceWithText("{ not a capture");
            const bool broken = !tryBuild(e5) && e5.contains("does not load");
            fresh(); // 6. good captures, but a tone sweep clips
            linearNam(cdir.getChildFile("G1_gain_0900.nam"), 10, 0.2); linearNam(cdir.getChildFile("G2_gain_1100.nam"), 10, 0.4);
            auto clip = sweep;
            for (auto& v : clip) v = juce::jlimit(-1.0, 1.0, v * 8.0);
            juce::AudioBuffer<float> cb(1, (int) clip.size());
            for (size_t i = 0; i < clip.size(); ++i) cb.setSample(0, (int) i, (float) clip[i]);
            writeWav(cdir.getChildFile("T00_ref_all_noon.wav"), cb, 48000.0);
            const bool clipStops = !tryBuild(e6) && e6.contains("clip");
            // 7. loudness (owner 2026-11-08: at least -37 dB): a too quiet 7:30 is left out (GAIN fades from silence up
            // to 8:00); 8. a too quiet 8:00 stops the build; -36.9 dB passes
            fresh();
            linearNam(cdir.getChildFile("G0_gain_0730.nam"), 10, 0.1, -39.9); linearNam(cdir.getChildFile("G1_gain_0800.nam"), 10, 0.2, -36.9);
            linearNam(cdir.getChildFile("G2_gain_1100.nam"), 10, 0.4, -20.0); linearNam(cdir.getChildFile("G3_gain_1300.nam"), 10, 0.6, -15.0);
            ampsurd::amp::Profile qp;
            juce::String qe;
            const bool quietLeftOut = tryBuild(e7) && ampsurd::amp::read(out.getChildFile("check.ampsurdamp"), qp, qe) && qp.steps.size() == 3
                                      && std::abs(qp.steps[0].knob - 1.0) < 1e-6 && qp.silenceFloor == 0.0;
            linearNam(cdir.getChildFile("G1_gain_0800.nam"), 10, 0.2, -41.8);
            const bool quietStops = !tryBuild(e8) && e8.contains("too quiet") && e8.contains("G1_gain_0800");
            cdir.deleteRecursively();
            out.getChildFile("check.ampsurdamp").deleteFile();
            checkOk = clean && late && dup && order && broken && clipStops && quietLeftOut && quietStops;
            checkWhy << "clean builds " << (int) clean << " (" << e1 << "), 4 ms off stops " << (int) late << ", duplicate stops " << (int) dup
                     << ", swapped gain stops " << (int) order << ", broken file stops " << (int) broken << ", clipped sweep stops " << (int) clipStops
                     << ", too quiet 7:30 left out " << (int) quietLeftOut << ", too quiet 8:00 stops " << (int) quietStops;
            if (!checkOk) checkWhy << "\n" << e2 << "\n" << e3 << "\n" << e4 << "\n" << e5 << "\n" << e6 << "\n" << e7 << "\n" << e8;
        }
        std::cout << (checkOk ? "[PASS]" : "[FAIL]") << " AMP BUILDER CAPTURE CHECK: " << checkWhy << "\n";
        const auto ampFile = out.getChildFile("Test amp.ampsurdamp");
        juce::String report, err;
        const bool built = ampsurd::amp::build(dir, "Test amp", "ui_snapshot", ampFile, report, err);
        ampsurd::amp::Profile prof;
        const bool readOk = ampsurd::amp::read(ampFile, prof, err) && prof.steps.size() == 2 && prof.steps[0].knob == 0.75 && prof.steps[1].knob == 8.0 && prof.silenceFloor == 0.0
                            && prof.tone != nullptr && prof.tone->knobs[0].size() == 5 && prof.tone->knobs[2].size() == 3 && prof.tone->knobs[1].empty();
        // the measured bass max at 60 Hz ~ +9 dB shelf
        double bassMax60 = 0.0;
        if (readOk)
        {
            const auto c = prof.tone->curveDb(10, 5, 5);
            const auto& grid = ampsurd::ToneSweep::gridHz();
            size_t g60 = 0;
            for (size_t g = 0; g < grid.size(); ++g) if (std::abs(grid[g] - 60) < std::abs(grid[g60] - 60)) g60 = g;
            bassMax60 = c[g60];
        }
        proc->loadCapture(3, ampFile);
        for (int t = 0; t < 600 && proc->getSlotStatus(3).state != AP::SlotState::loaded; ++t) run(20);
        run(1500);
        const auto st = proc->getSlotStatus(3);
        const bool loaded = st.state == AP::SlotState::loaded && st.isAmp && st.hasTone && st.toneMask == 5 && st.fileName == "Test amp";
        // solo amp 4 and listen to an 80 Hz tone: BASS max is about +8 dB louder there than noon (quiet enough that
        // the output limiter stays out of it: GAIN 5 is a blend of two very different captures, made up to +2.8 dB)
        setP(*proc, AP::slotParamId(3, "solo"), 1.0f);
        setP(*proc, "gateOn", 0.0f);
        float peak80 = 0.0f;
        auto level80 = [&] {
            double re = 0, im = 0;
            for (int k = 0; k < 140; ++k)
            {
                juce::AudioBuffer<float> b(2, 256);
                for (int i = 0; i < 256; ++i) { const double t = (k * 256 + i) / 48000.0; b.setSample(0, i, (float) (0.005 * std::sin(2 * juce::MathConstants<double>::pi * 80.0 * t))); b.setSample(1, i, 0.0f); }
                proc->processBlock(b, midi);
                if (k >= 60) peak80 = juce::jmax(peak80, b.getMagnitude(0, 0, 256));
                if (k >= 60) for (int i = 0; i < 256; ++i) { const double t = (k * 256 + i) / 48000.0; re += b.getSample(0, i) * std::cos(2 * juce::MathConstants<double>::pi * 80.0 * t); im += b.getSample(0, i) * std::sin(2 * juce::MathConstants<double>::pi * 80.0 * t); }
            }
            return 20.0 * std::log10(std::sqrt(re * re + im * im) + 1e-12);
        };
        proc->runHousekeeping();
        const double noon = level80();
        setP(*proc, AP::slotParamId(3, "ampBass"), 10.0f);
        proc->runHousekeeping();
        run(100);
        const double bassUp = level80();
        std::cout << "  amp test: output peak at 80 Hz with BASS max " << peak80 << "\n";
        setP(*proc, AP::slotParamId(3, "ampBass"), 5.0f);
        proc->runHousekeeping();
        run(100);
        // GAIN: 9:00 = the first capture, 3:00 = the second -> the sound differs; halfway = a blend
        setP(*proc, AP::slotParamId(3, "ampGain"), 2.0f);
        run(200);
        const double g2 = level80();
        setP(*proc, AP::slotParamId(3, "ampGain"), 8.0f);
        run(200);
        const double g8 = level80();
        // from silence (owner 2026-11-05): the lowest capture is 7:45, so GAIN reaches down to 7:00 = silence and fades
        // like a volume knob in between: half way (7:22) = -12 dB, 7:00 = nothing; a set that starts at 9:00 does not fade
        setP(*proc, AP::slotParamId(3, "ampGain"), 0.75f);
        run(200);
        const double gLow = level80();
        setP(*proc, AP::slotParamId(3, "ampGain"), 0.375f);
        run(200);
        const double gHalf = level80();
        setP(*proc, AP::slotParamId(3, "ampGain"), 0.0f);
        run(200);
        const double g0 = level80();
        const bool fadeOk = std::abs((gLow - gHalf) - 12.04) < 1.0 && g0 < gLow - 60.0 && checkNoFadeFrom9;
        std::cout << (fadeOk ? "[PASS]" : "[FAIL]") << " AMPSURD AMP FROM SILENCE: GAIN 7:45 " << juce::String(gLow, 1) << " dB, 7:22 "
                  << juce::String(gHalf, 1) << " dB (" << juce::String(gHalf - gLow, 1) << "), 7:00 " << juce::String(g0, 1)
                  << " dB (silence); a set from 9:00 keeps its range (" << checkNoFadeFrom9 << ")\n";
        setP(*proc, AP::slotParamId(3, "ampGain"), 8.0f);
        run(200);
        // the amp's INPUT compensation follows GAIN (0.9.6): the INPUT -> loudness curve is made again at the new GAIN
        setP(*proc, AP::slotParamId(3, "drive"), 6.0f);
        auto curveAt = [&](double gain) {
            for (int t = 0; t < 1500 && std::abs(proc->getDriveCurveAmpGain(3) - gain) > 0.01; ++t) run(20);
            run(200);
            return std::abs(proc->getDriveCurveAmpGain(3) - gain) < 0.01;
        };
        const bool curve8 = curveAt(8.0);
        const double comp8 = proc->getDriveCompensationDb(3);
        setP(*proc, AP::slotParamId(3, "ampGain"), 2.0f);
        const bool curve2 = curveAt(2.0);
        const double comp2 = proc->getDriveCompensationDb(3);
        setP(*proc, AP::slotParamId(3, "ampGain"), 8.0f);
        setP(*proc, AP::slotParamId(3, "drive"), 0.0f);
        const bool followOk = curve8 && curve2 && std::abs(comp8) < 12.0 && std::abs(comp2) < 12.0;
        std::cout << (followOk ? "[PASS]" : "[FAIL]") << " AMPSURD AMP INPUT: the loudness compensation follows GAIN (curve at GAIN 8: "
                  << curve8 << ", at GAIN 2: " << curve2 << "; amp INPUT +6 dB compensated " << juce::String(comp8, 2) << " / "
                  << juce::String(comp2, 2) << " dB)\n";
        // the knobs on the slot + a screenshot
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        e->refreshAll();
        int knobsShown = 0;
        bool gainRange = false;
        std::function<void(juce::Component&)> count = [&](juce::Component& c) {
            if (auto* sl = dynamic_cast<juce::Slider*>(&c)) if (sl->isVisible() && sl->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
            {
                ++knobsShown;
                if (sl->getMinimum() == 0.0 && sl->getMaximum() == 8.0) // GAIN 7:00 (silence) .. 15:00 (the highest capture), as the clock
                    gainRange = sl->getTextFromValue(0.0) == "7:00" && sl->getTextFromValue(8.0) == "15:00" && sl->getTextFromValue(5.5) == "12:30";
            }
            for (auto* ch : c.getChildren()) count(*ch);
        };
        count(*ed);
        // the whole name on the slot (owner 2026-11-06): long amp names get a smaller font, not "..."
        bool namesFit = false;
        {
            ampsurd::ui::SlotComponent* slot3 = nullptr;
            std::function<void(juce::Component&)> findSlot = [&](juce::Component& c) {
                if (auto* sc = dynamic_cast<ampsurd::ui::SlotComponent*>(&c)) if (sc->getSlotIndex() == 3) slot3 = sc;
                for (auto* ch : c.getChildren()) findSlot(*ch);
            };
            findSlot(*ed);
            if (slot3 != nullptr)
            {
                const auto area = slot3->getNameArea().toFloat();
                namesFit = area.getHeight() < 60.0f; // the short name space above the knobs
                juce::String detail;
                for (const juce::String n : { "Framus Cobra + Gerhat Screamer CH3", "EVH 5150 III CH1 + Gerhat Screamer", "EVH 5150 III CH1" })
                {
                    const auto f = ampsurd::ui::FilenameLayout::fit(n, area);
                    namesFit = namesFit && !f.truncated;
                    detail << n << " -> " << f.lines.joinIntoString(" / ") << " (" << juce::String(f.size, 0) << " pt); ";
                }
                std::cout << "  amp names on the slot: " << detail << "\n";
            }
        }
        setP(*proc, AP::slotParamId(3, "ampGain"), 6.5f);
        setP(*proc, AP::slotParamId(3, "ampTreble"), 7.0f);
        e->refreshAll();
        save(*ed, out.getChildFile("24_ampsurd_amp.png"));
        // presets keep the amp and its knobs
        const auto pf = out.getChildFile("amp_test.ampsurd");
        proc->savePreset(pf);
        setP(*proc, AP::slotParamId(3, "ampTreble"), 5.0f);
        proc->loadPreset(pf);
        run(500);
        const bool presetOk = std::abs(proc->params.getRawParameterValue(AP::slotParamId(3, "ampTreble"))->load() - 7.0f) < 0.01f
                              && proc->getSlotStatus(3).path == ampFile.getFullPathName();
        pf.deleteFile();
        const bool soundOk = std::abs((bassUp - noon) - bassMax60) < 1.5 && bassMax60 > 7.0 && std::abs(g2 - g8) > 0.05;
        const bool ok = built && readOk && loaded && soundOk && knobsShown == 3 && presetOk && gainRange && namesFit; // GAIN, BASS, TREBLE (no MID measured)
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " AMPSURD AMP: built from recordings (" << built << "), read back (" << readOk
                  << ": 2 gain steps, bass 5 / treble 3 positions), loaded as an amp with tone (" << loaded << "), knobs on the slot: GAIN + the measured BASS, TREBLE ("
                  << knobsShown << "), BASS max at 80 Hz " << juce::String(bassUp - noon, 1) << " dB (measured curve " << juce::String(bassMax60, 1)
                  << " dB at 60 Hz), GAIN 9:00 vs 3:00 differ (" << juce::String(g2, 1) << " / " << juce::String(g8, 1) << " dB), preset keeps it ("
                  << presetOk << ")\n";
        if (!ok) std::cout << report << err << "\n";
        setP(*proc, AP::slotParamId(3, "solo"), 0.0f);
        setP(*proc, "gateOn", 1.0f);
        for (const char* id : { "ampGain", "ampBass", "ampMid", "ampTreble" }) setP(*proc, AP::slotParamId(3, id), 5.0f);
        proc->unloadCapture(3);
        run(500);
        dir.deleteRecursively();
    }

    // AMPSURD TONES (.ampsurdtone, owner 2026-10-30): one amp's sound - capture, pedal, IR, EQs, INPUT / OUTPUT -
    // saved, then loaded into another slot; the rig (mix, PAN) is not touched
    if (argc >= 4)
    {
        auto run = [&](int ms) { for (int t = 0; t < ms / 20; ++t) { buf.clear(); proc->processBlock(buf, midi); proc->runHousekeeping(); pump(20); } };
        using AP = AmpsurdProcessor;
        auto v = [&](const juce::String& id) { return proc->params.getRawParameterValue(id)->load(); };
        proc->loadPedal(0, 0, juce::File(argv[3]));
        proc->loadCabIr(0, { 4.5, 52.5 });
        setP(*proc, AP::slotParamId(0, "drive"), 4.5f);
        setP(*proc, AP::slotParamId(0, "level"), -3.0f);
        setP(*proc, AP::slotParamId(0, "ped1Level"), 2.0f);
        setP(*proc, AP::bandParamId(0, 3, "gain"), 5.5f);
        setP(*proc, AP::bandParamId(AP::irEqTarget(0), 6, "gain"), -4.0f);
        setP(*proc, AP::slotParamId(AP::irEqTarget(0), "eqOn"), 1.0f);
        run(2500);
        const auto toneFile = out.getChildFile("ui_snapshot test tone.ampsurdtone");
        juce::String err;
        const bool saved = proc->saveTone(0, toneFile, err);
        // into slot 3 (empty), whose PAN / mix must stay
        setP(*proc, AP::slotParamId(2, "pan"), 40.0f);
        const float pan3 = v(AP::slotParamId(2, "pan"));
        const bool loaded = proc->loadTone(2, toneFile, err);
        for (int t = 0; t < 300 && proc->getSlotStatus(2).state != AP::SlotState::loaded; ++t) run(20);
        run(3000);
        ampsurd::CabPoint cp;
        const bool same = proc->getSlotStatus(2).path == proc->getSlotStatus(0).path
                          && proc->getPedalStatus(2, 0).path == proc->getPedalStatus(0, 0).path
                          && proc->getCabIrPoint(2, cp) && std::abs(cp.row - 4.5) < 1e-6 && std::abs(cp.mm - 52.5) < 1e-6
                          && std::abs(v(AP::slotParamId(2, "drive")) - 4.5f) < 0.01f && std::abs(v(AP::slotParamId(2, "level")) + 3.0f) < 0.01f
                          && std::abs(v(AP::slotParamId(2, "ped1Level")) - 2.0f) < 0.01f
                          && std::abs(v(AP::bandParamId(2, 3, "gain")) - 5.5f) < 0.01f
                          && std::abs(v(AP::bandParamId(AP::irEqTarget(2), 6, "gain")) + 4.0f) < 0.01f
                          && v(AP::slotParamId(AP::irEqTarget(2), "eqOn")) > 0.5f;
        const bool rigKept = v(AP::slotParamId(2, "pan")) == pan3;
        juce::String badErr;
        const bool rejects = !proc->loadTone(2, out.getChildFile("not a tone.ampsurdtone"), badErr) && badErr.isNotEmpty();
        std::cout << (saved && loaded && same && rigKept && rejects ? "[PASS]" : "[FAIL]") << " TONES: amp 1 saved as a tone (" << saved
                  << "), loaded into amp 3 (" << loaded << ") = the same capture, pedal, AMPSURD IR, EQs, INPUT / OUTPUT (" << same
                  << "), amp 3's PAN kept (" << rigKept << "), a wrong file is refused (" << rejects << ") " << err << "\n";
        toneFile.deleteFile();
        proc->unloadCapture(2);
        proc->removePedal(0, 0);
        proc->removeIr(0);
        for (const char* id : { "drive", "level", "ped1Level" }) setP(*proc, AP::slotParamId(0, id), 0.0f);
        setP(*proc, AP::bandParamId(0, 3, "gain"), 0.0f);
        setP(*proc, AP::slotParamId(2, "pan"), 0.0f);
        run(1500);
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
   #if AMPSURD_DOUBLER
    // DOUBLER panel (experimental): open from the master row; with the two amps centred the output is mono
    // (L == R) when it is off, and stereo (second takes left / right) when it is on
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
        auto play = [&](int blocks) {
            double diff = 0.0, sum = 0.0;
            bool finite = true;
            juce::AudioBuffer<float> b3(2, 256);
            for (int k = 0; k < blocks; ++k)
            {
                b3.clear();
                for (int i = 0; i < 256; ++i)
                {
                    const double t = (k * 256 + i) / 48000.0, ph = std::fmod(t, 0.4);
                    const double env = ph < 0.3 ? std::exp(-ph * 6.0) * std::min(1.0, ph / 0.003) : 0.0; // notes with gaps
                    b3.setSample(0, i, (float) (0.3 * env * std::sin(2 * 3.14159265 * 110.0 * t)));
                }
                proc->processBlock(b3, midi);
                for (int i = 0; i < 256; ++i)
                {
                    const float l = b3.getSample(0, i), r = b3.getSample(1, i);
                    finite = finite && std::isfinite(l) && std::isfinite(r);
                    if (k >= blocks / 2) { diff += (l - r) * (l - r); sum += l * l + r * r; }
                }
                e->refreshAll();
            }
            return finite ? diff / std::max(1e-20, sum) : -1.0;
        };
        const bool offByDefault = !proc->isDoublerOn();
        const float pan0 = proc->params.getRawParameterValue(AmpsurdProcessor::slotParamId(0, "pan"))->load();
        setP(*proc, AmpsurdProcessor::slotParamId(0, "pan"), 0.0f); // amps centred: what is left / right comes from the doubler
        play(20);
        const double offSide = play(60);
        setP(*proc, "dblOn", 1.0f);
        e->showDoubler(true);
        const double onSide = play(160);
        save(*ed, out.getChildFile("21_doubler.png"));
        std::function<juce::Component*(juce::Component&, const juce::String&)> findText = [&](juce::Component& c, const juce::String& t) -> juce::Component* {
            if (auto* tb = dynamic_cast<juce::TextButton*>(&c))
                if (tb->getButtonText() == t && tb->isVisible()) return tb;
            for (auto* ch : c.getChildren())
                if (ch->isVisible())
                    if (auto* r = findText(*ch, t)) return r;
            return nullptr;
        };
        std::function<bool(juce::Component&)> panelShown = [&](juce::Component& c) -> bool {
            if (dynamic_cast<ampsurd::ui::DoublerPanel*>(&c) != nullptr) return c.isVisible();
            for (auto* ch : c.getChildren())
                if (panelShown(*ch)) return true;
            return false;
        };
        const bool panel = panelShown(*ed) && findText(*ed, "DOUBLER") != nullptr && findText(*ed, "ROOT") != nullptr;
        e->showDoubler(false);
        e->refreshAll();
        const bool closedToGlobal = !panelShown(*ed);
        setP(*proc, "dblOn", 0.0f);
        play(20);
        const double offAgain = play(60);
        setP(*proc, AmpsurdProcessor::slotParamId(0, "pan"), pan0);
        std::cout << (offByDefault && offSide >= 0.0 && onSide > offSide + 0.03 && std::abs(offAgain - offSide) < 0.01 && panel && closedToGlobal ? "[PASS]" : "[FAIL]")
                  << " DOUBLER: off by default, on -> wider: second takes left / right (side/total " << onSide << "), off again as before; panel opens from the master row and closes"
                  << " (off " << offSide << ", again " << offAgain << ", default " << offByDefault << ", panel " << panel << ", closed " << closedToGlobal << ")\n";
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
        // the X before the GUITAR track asks first; DELETE removes the take, CANCEL keeps it
        {
            std::vector<ampsurd::ui::DeleteBox*> boxes;
            std::function<void(juce::Component&)> collect = [&](juce::Component& c) {
                if (auto* d = dynamic_cast<ampsurd::ui::DeleteBox*>(&c)) boxes.push_back(d);
                for (auto* ch : c.getChildren()) if (ch->isVisible()) collect(*ch);
            };
            auto findButton = [&](const juce::String& t) -> juce::TextButton* {
                std::function<juce::TextButton*(juce::Component&)> f = [&](juce::Component& c) -> juce::TextButton* {
                    if (auto* tb = dynamic_cast<juce::TextButton*>(&c)) if (tb->getButtonText() == t && tb->isVisible()) return tb;
                    for (auto* ch : c.getChildren()) if (ch->isVisible()) if (auto* r = f(*ch)) return r;
                    return nullptr;
                };
                return f(*ed);
            };
            e->refreshAll();
            collect(*ed);
            pl->renderNow();
            const bool hadTake = pl->hasTake();
            bool ok = boxes.size() == 2 && hadTake && findButton("CLEAR TAKE") == nullptr;
            if (ok)
            {
                boxes[1]->onClick(); // GUITAR (the second track)
                auto* no = findButton("CANCEL");
                ok = no != nullptr && findButton("DELETE") != nullptr;
                if (no) no->onClick();
                ok = ok && pl->hasTake() && findButton("DELETE") == nullptr;
                boxes[1]->onClick();
                save(*ed, out.getChildFile("19b_player_delete.png"));
                if (auto* yes = findButton("DELETE")) yes->onClick();
                pl->renderNow();
                ok = ok && !pl->hasTake() && pl->hasBacking();
                proc->undo(); // Ctrl+Z: the one history
                pl->renderNow();
                ok = ok && pl->hasTake();
            }
            ok = ok && findButton("UNDO") == nullptr; // the player has no own UNDO any more
            std::cout << (ok ? "[PASS]" : "[FAIL]") << " DELETE BOXES: X before each track, no CLEAR TAKE; X GUITAR asks (CANCEL keeps it, "
                      << "DELETE removes it, UNDO brings it back) (boxes " << boxes.size() << ", take " << hadTake << ")\n";
        }
        // ONE history: sound changes and player steps undone / redone in the order they were made
        {
            auto out0 = [&] { return proc->params.getRawParameterValue("output")->load(); };
            auto settle = [&] { proc->runHousekeeping(); pump(450); proc->runHousekeeping(); };
            settle();
            const float o0 = out0();
            setP(*proc, "output", -4.0f);
            settle();                               // step 1: sound
            pl->clearTake();
            pl->renderNow();                        // step 2: player (the take is gone)
            setP(*proc, "output", -8.0f);
            settle();                               // step 3: sound
            proc->undo();
            pl->renderNow();
            const bool u1 = out0() == -4.0f && !pl->hasTake();
            proc->undo();
            pl->renderNow();
            const bool u2 = out0() == -4.0f && pl->hasTake();
            proc->undo();
            pl->renderNow();
            const bool u3 = out0() == o0 && pl->hasTake();
            proc->redo();
            proc->redo();
            pl->renderNow();
            const bool r2 = out0() == -4.0f && !pl->hasTake();
            proc->redo();
            const bool r3 = out0() == -8.0f;
            std::cout << (u1 && u2 && u3 && r2 && r3 ? "[PASS]" : "[FAIL]") << " ONE HISTORY: sound, take deleted, sound -> Ctrl+Z x3 in reverse order, "
                      << "Ctrl+Y x3 forward (" << u1 << u2 << u3 << r2 << r3 << ")\n";
            setP(*proc, "output", o0);
        }
        proc->setNonRealtime(false);
        wavFile.deleteFile();
    }

    // camera window (VIDEO takes): here without a camera it explains itself; the player has the VIDEO button
    {
        PlayerRecorder dummy;
        dummy.prepare(48000.0, 256);
        VideoCapture cam;
        ampsurd::ui::CameraView view(cam, dummy);
        ampsurd::ui::LookAndFeel lf;
        view.setLookAndFeel(&lf);
        view.setSize(600, 590); // as the real window
        view.showNotice(juce::String::charToString((juce::juce_wchar) 0x2713) + "  VIDEO SAVED: AMPSURD video 2026-10-24 15-20-11", 6.0);
        save(view, out.getChildFile("20_camera_window.png"));
        // owner 2026-10-24: no SAVE MP4 button (every take is saved automatically); WATCH lists the last take + saved videos
        bool saveBtn = false;
        juce::ComboBox* watch = nullptr;
        for (auto* c : view.getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*>(c)) saveBtn = saveBtn || b->getButtonText().containsIgnoreCase("SAVE");
            if (auto* cb = dynamic_cast<juce::ComboBox*>(c)) if (cb->getNumItems() > 0 && cb->getItemText(0).startsWith("Last take")) watch = cb;
        }
        const int mp4s = dummy.getRecordingsFolder().findChildFiles(juce::File::findFiles, false, "*.mp4").size();
        const bool watchOk = watch != nullptr && watch->getNumItems() == 1 + mp4s && watch->getSelectedId() == 1
                             && view.selectedSavedVideo() == juce::File();
        std::cout << ((!saveBtn && watchOk) ? "[PASS] " : "[FAIL] ") << "CAMERA WINDOW: no SAVE button, WATCH = last take + "
                  << mp4s << " saved video(s)\n";
        view.setLookAndFeel(nullptr);
        std::cout << "[INFO] camera supported here: " << (VideoCapture::isSupported() ? "yes" : "no") << ", cameras: "
                  << VideoCapture::cameras().size() << "\n";
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
            const bool copied = p2.getEqClipboardSource() == 0 && c1.getButtonText() == "CANCEL COPY";
            c3.clicked();                                     // PASTE -> SURE?
            const bool sure = c3.getButtonText() == "SURE?" && !same(0, 2);
            c3.clicked();                                     // paste: the copy ends with it (one paste per copy)
            c1.update();
            const bool amp3 = same(0, 2) && c3.getButtonText() == "COPY" && c1.getButtonText() == "COPY" && p2.getEqClipboardSource() < 0;
            settle();
            const bool globalBefore = !same(0, AP::kGlobalEq);
            c1.clicked();                                     // COPY again
            cg.clicked();                                     // SURE? ...
            pump(3200);                                       // ... expires
            cg.clicked();                                     // only arms again
            const bool noPaste = !same(0, AP::kGlobalEq) && cg.getButtonText() == "SURE?";
            cg.clicked();                                     // paste
            const bool global = same(0, AP::kGlobalEq);
            settle();
            p2.undo();                                        // the Global EQ paste is one step
            const bool undone = !same(0, AP::kGlobalEq) && same(0, 2);
            // getting rid of an accidental COPY (owner 2026-10-28): the button, Ctrl+Z, Esc, 20 s
            std::unique_ptr<juce::AudioProcessorEditor> ed(p2.createEditor());
            c1.clicked();
            c1.clicked();                                     // CANCEL COPY
            const bool byButton = p2.getEqClipboardSource() < 0;
            c1.clicked();
            const bool undoBefore = !same(0, AP::kGlobalEq);
            p2.undo();                                        // Ctrl+Z takes back the copy, not the paste before it
            const bool byUndo = p2.getEqClipboardSource() < 0 && same(0, 2) && undoBefore;
            c1.clicked();
            ed->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            const bool byEsc = p2.getEqClipboardSource() < 0;
            c1.clicked();
            p2.expireEqClipboard(20000.0);
            const bool kept = p2.getEqClipboardSource() == 0;
            p2.expireEqClipboard(0.0);                        // = 20 s later
            const bool byTime = p2.getEqClipboardSource() < 0;
            const bool cleared = byButton && byUndo && byEsc && kept && byTime;
            if (!(copied && sure && amp3 && globalBefore && noPaste && global && undone && cleared))
                std::cout << "  (copied " << copied << " sure " << sure << " amp3 " << amp3 << " globalBefore " << globalBefore << " noPaste "
                          << noPaste << " global " << global << " undone " << undone << " button " << byButton << " undo " << byUndo
                          << " esc " << byEsc << " kept " << kept << " time " << byTime << ")\n";
            std::cout << (copied && sure && amp3 && globalBefore && noPaste && global && undone && cleared ? "[PASS]" : "[FAIL]")
                      << " EQ COPY / PASTE: amp 1 -> amp 3 and -> Global EQ identical, SURE? expires after 3 s, one paste per copy, undo"
                         " restores; a copy ends with CANCEL COPY, Ctrl+Z, Esc or after 20 s\n";
            settle();
        }
        {
            // EQ PRESETS (owner 2026-10-28): save amp 1's EQ under a name, change it, load it back = identical, one undo step
            using AP = AmpsurdProcessor;
            const auto f = AP::getEqPresetFolder().getChildFile("ui_snapshot test curve.ampsurdeq");
            const float g1 = val(AP::bandParamId(0, 1, "gain").toRawUTF8()), f2 = val(AP::bandParamId(0, 2, "freq").toRawUTF8());
            const int n = p2.getEqBandCount(0);
            const bool saved = p2.saveEqPreset(0, f) && AP::listEqPresets().contains(f);
            setP(p2, AP::bandParamId(0, 1, "gain"), -7.0f);
            setP(p2, AP::slotParamId(0, "bands"), 4.0f);
            settle();
            const bool loaded = p2.loadEqPreset(0, f);
            settle();
            const bool same = std::abs(val(AP::bandParamId(0, 1, "gain").toRawUTF8()) - g1) < 0.01f
                              && std::abs(val(AP::bandParamId(0, 2, "freq").toRawUTF8()) - f2) < 0.5f && p2.getEqBandCount(0) == n;
            p2.undo();
            const bool undone = std::abs(val(AP::bandParamId(0, 1, "gain").toRawUTF8()) + 7.0f) < 0.01f;
            p2.redo();
            // into another EQ (the Global EQ) too
            const bool toGlobal = p2.loadEqPreset(AP::kGlobalEq, f)
                                  && std::abs(val(AP::bandParamId(AP::kGlobalEq, 1, "gain").toRawUTF8()) - g1) < 0.01f;
            settle();
            f.deleteFile();
            const auto factory = AP::listFactoryEqPresets();
            bool factoryOk = true;
            for (auto& name : factory) factoryOk = factoryOk && p2.loadFactoryEqPreset(2, name);
            std::cout << (saved && loaded && same && undone && toGlobal && factoryOk ? "[PASS]" : "[FAIL]")
                      << " EQ PRESETS: save under a name, load back identical (" << saved << loaded << same << "), one undo step ("
                      << undone << "), into the Global EQ (" << toGlobal << "), " << factory.size() << " factory curve(s) load ("
                      << factoryOk << ")\n";
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
        // SAVE AS: the file dialog offers .ampsurd and .ampsurdpack; a .ampsurdpack name writes the pack (preset +
        // captures + README, as PRESET -> Export pack), a .ampsurd name the preset
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed2(p2.createEditor());
            ampsurd::ui::HeaderBar* hb = nullptr;
            juce::String tip;
            std::function<void(juce::Component&)> find = [&](juce::Component& c) {
                if (auto* h = dynamic_cast<ampsurd::ui::HeaderBar*>(&c)) hb = h;
                if (auto* tb = dynamic_cast<juce::TextButton*>(&c)) if (tb->getButtonText() == "SAVE AS") tip = tb->getTooltip();
                for (auto* ch : c.getChildren()) find(*ch);
            };
            find(*ed2);
            const juce::String patterns = ampsurd::ui::HeaderBar::kSaveAsPatterns;
            const auto pack2 = out.getChildFile("Save as pack.ampsurdpack"), preset2f = out.getChildFile("Save as preset.ampsurd");
            pack2.deleteFile();
            preset2f.deleteFile();
            juce::String e2, e3;
            const bool ok2 = hb != nullptr && hb->saveAsFile(pack2, e2);
            const bool ok3 = hb != nullptr && hb->saveAsFile(preset2f, e3);
            int nams2 = 0;
            bool preset2 = false, readme2 = false;
            if (ok2)
            {
                juce::ZipFile z2(pack2);
                for (int k = 0; k < z2.getNumEntries(); ++k)
                {
                    const auto fn = z2.getEntry(k)->filename;
                    nams2 += fn.endsWithIgnoreCase(".nam");
                    preset2 = preset2 || fn.endsWithIgnoreCase(".ampsurd");
                    readme2 = readme2 || fn == "README.txt";
                }
            }
            const bool presetIsPreset = ok3 && preset2f.existsAsFile() && juce::ZipFile(preset2f).getNumEntries() == 0;
            std::cout << (patterns.contains("*.ampsurd") && patterns.contains("*.ampsurdpack") && ok2 && nams2 >= 1 && preset2 && readme2
                                  && presetIsPreset && tip == "Save the preset, or a pack with its NAM captures and IRs"
                              ? "[PASS]" : "[FAIL]")
                      << " SAVE AS: types '" << patterns << "'; .ampsurdpack -> pack with " << nams2 << " capture(s), preset " << preset2
                      << ", README " << readme2 << "; .ampsurd -> preset " << presetIsPreset << " " << e2 << e3 << "\n";
            pack2.deleteFile();
            preset2f.deleteFile();
        }
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

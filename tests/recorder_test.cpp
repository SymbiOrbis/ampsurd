// recorder_test: the standalone player / recorder through the real AMPSURD processor.
// Exit 0 = all pass.  Needs no audio device: blocks are processed directly.

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>

#include "../plugin/PluginEditor.h"
#include "../plugin/PluginProcessor.h"

static int failures = 0;
static void check(bool ok, const juce::String& msg)
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << msg << "\n";
    failures += ok ? 0 : 1;
}

static void setP(AmpsurdProcessor& p, const juce::String& id, float v)
{
    if (auto* rp = p.params.getParameter(id)) rp->setValueNotifyingHost(rp->convertTo0to1(v));
}

static juce::File writeWav(const juce::File& f, double rate, const juce::AudioBuffer<float>& b, int bits = 24)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream();
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(rate).withNumChannels(b.getNumChannels()).withBitsPerSample(bits));
    w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    return f;
}

// click track: one-sample clicks every 0.5 s
static juce::AudioBuffer<float> clicks(double rate, double seconds, float amp = 0.5f)
{
    juce::AudioBuffer<float> b(2, (int) (rate * seconds));
    b.clear();
    for (double t = 0.5; t < seconds - 0.1; t += 0.5)
        for (int c = 0; c < 2; ++c) b.setSample(c, (int) std::lround(t * rate), amp);
    return b;
}

// positions of the clicks: the maximum of each burst above the threshold
static std::vector<int> peaks(const juce::AudioBuffer<float>& b, float thr)
{
    std::vector<int> p;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs(b.getSample(0, i)) > thr && (p.empty() || i - p.back() > 1000))
        {
            int best = i;
            for (int k = i; k < std::min(b.getNumSamples(), i + 64); ++k)
                if (std::abs(b.getSample(0, k)) > std::abs(b.getSample(0, best))) best = k;
            p.push_back(best);
            i = best;
        }
    return p;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ampsurd_recorder_test");
    tmp.deleteRecursively();
    tmp.createDirectory();

    const double sr = 48000.0;
    const int block = 256;
    auto proc = std::make_unique<AmpsurdProcessor>();
    proc->setNonRealtime(true); // faster than real time: the player waits for disk reads
    proc->prepareToPlay(sr, block);
    auto* pl = proc->getPlayer();
    if (!pl) { std::cout << "player missing (set AMPSURD_FORCE_PLAYER=1)\n"; return 2; }
    setP(*proc, "bypass", 1.0f);  // the guitar passes through AMPSURD (with its exact latency)
    setP(*proc, "gateOn", 0.0f);
    const int deviceLatency = 300; // simulated audio interface: input + output latency
    pl->setDeviceLatencyProvider([] { return deviceLatency; });
    const int limLat = pl->getLimiterLatency();

    juce::MidiBuffer midi;
    // runs the processor for `seconds`; input(t) = guitar DI at processing sample t; returns the output
    auto run = [&](double seconds, const std::function<float(juce::int64)>& input) {
        const int total = (int) (seconds * sr);
        juce::AudioBuffer<float> out(2, total);
        juce::AudioBuffer<float> io(2, block);
        static juce::int64 clock = 0;
        for (int p = 0; p < total; p += block)
        {
            const int n = std::min(block, total - p);
            juce::AudioBuffer<float> chunk(2, n);
            for (int i = 0; i < n; ++i) chunk.setSample(0, i, input ? input(clock + i) : 0.0f), chunk.setSample(1, i, 0.0f);
            proc->processBlock(chunk, midi);
            out.copyFrom(0, p, chunk, 0, 0, n);
            out.copyFrom(1, p, chunk, 1, 0, n);
            clock += n;
        }
        return out;
    };
    auto waitIdle = [&] { for (int i = 0; i < 6000 && pl->isBusy(); ++i) juce::Thread::sleep(5); };

    // 1. backing playback: clicks come out where they belong (after the sum limiter's 1 ms)
    const auto back48 = writeWav(tmp.getChildFile("clicks48.wav"), 48000, clicks(48000, 4.0));
    check(pl->loadBacking(back48).isEmpty() && pl->hasBacking(), "BACKING: WAV loaded");
    run(0.05, nullptr); // settle
    pl->play();
    const auto o1 = run(2.0, nullptr);
    const auto p1 = peaks(o1, 0.2f);
    bool exact = p1.size() >= 3;
    for (size_t k = 0; k < p1.size() && k < 3; ++k) exact = exact && p1[k] == (int) (24000 * (k + 1)) + limLat;
    check(exact, "BACKING: clicks heard exactly at 0.5 / 1.0 / 1.5 s (+" + juce::String(limLat) + " samples safety limiter)");
    pl->stop();

    // 2. recording: a musician plays exactly with the clicks they hear. Each click mixed at song
    //    position c is heard at c + limLat + output latency; the guitar arrives input-latency later,
    //    i.e. at processing time c + limLat + deviceLatency. The take must line up with the clicks.
    run(0.05, nullptr);
    juce::int64 recStartClock = -1;
    pl->record();
    const juce::int64 base = [] { return (juce::int64) 0; }();
    (void) base;
    // processing clock at song position 0 = the clock when recording starts (next block)
    static juce::int64 songZero = -1;
    run(4.0, [&](juce::int64 clock) {
        if (songZero < 0) songZero = clock;
        const juce::int64 songPos = clock - songZero;
        const juce::int64 heardAt = songPos - limLat - deviceLatency; // the backing sample the player reacts to now
        return (heardAt > 0 && heardAt % 24000 == 0) ? 0.3f : 0.0f;
    });
    (void) recStartClock;
    pl->stop();
    pl->renderNow();
    check(pl->hasTake(), "RECORD: take recorded (" + juce::String(pl->getTakeLengthSeconds(), 2) + " s), shift "
                         + juce::String(pl->getCompensationMs(), 2) + " ms = AMPSURD latency + device latency + limiter");
    juce::AudioBuffer<float> guitar;
    int startOff = 0;
    pl->renderMix(false, guitar, startOff);
    const auto gp = peaks(guitar, 0.1f);
    bool aligned = gp.size() >= 5;
    juce::String where;
    for (size_t k = 0; k < gp.size() && k < 7; ++k)
    {
        where << gp[k] << " ";
        aligned = aligned && gp[k] == (int) (24000 * (k + 1));
    }
    check(aligned, "RECORD: every guitar note lands exactly on its click (samples " + where + "= 24000 x k)");

    // 3. playback of backing + take + live guitar
    pl->play();
    const auto o3 = run(1.2, [](juce::int64) { return 0.0f; });
    pl->stop();
    const float atClick = std::abs(o3.getSample(0, 24000 + limLat));
    check(std::abs(atClick - 0.8f) < 0.02f, "PLAYBACK: backing click and recorded note together at 0.5 s (" + juce::String(atClick, 3) + " = 0.5 + 0.3)");

    // 4. exports: formats and rates
    struct Case { int fmt; double rate; bool withBacking; juce::String ext; int bits; };
    for (const Case c : { Case { 0, 44100, true, ".wav", 16 }, Case { 1, 48000, false, ".wav", 24 }, Case { 2, 96000, true, ".wav", 32 },
                          Case { 3, 44100, true, ".flac", 16 }, Case { 4, 96000, false, ".flac", 24 } })
    {
        const auto f = tmp.getChildFile("export_" + juce::String(c.fmt) + c.ext);
        bool done = false;
        juce::String err;
        PlayerRecorder::ExportOptions o { c.fmt, c.rate, c.withBacking };
        pl->exportAudio(f, o, [&](const juce::String& e) { err = e; done = true; });
        waitIdle();
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
        bool ok = r != nullptr && err.isEmpty();
        float peak = 0.0f;
        std::vector<int> pk;
        if (ok)
        {
            juce::AudioBuffer<float> b((int) r->numChannels, (int) r->lengthInSamples);
            r->read(&b, 0, (int) r->lengthInSamples, 0, true, true);
            peak = b.getMagnitude(0, b.getNumSamples());
            pk = peaks(b, 0.1f);
            ok = std::abs(r->sampleRate - c.rate) < 1 && (int) r->bitsPerSample == c.bits && r->numChannels == 2
                 && std::abs((double) r->lengthInSamples / r->sampleRate - pl->getLengthSeconds()) < 0.01 && peak <= 0.8913f
                 && !pk.empty() && std::abs(pk[0] - 0.5 * c.rate) <= 1.0;
        }
        check(ok, PlayerRecorder::formatNames()[c.fmt] + " " + juce::String(c.rate / 1000.0, 1) + " kHz, "
                      + (c.withBacking ? "guitar + backing" : "guitar only") + ": readable, right format and length, first note at "
                      + (pk.empty() ? juce::String("-") : juce::String(pk[0] / c.rate, 4)) + " s, peak "
                      + juce::String(juce::Decibels::gainToDecibels(peak), 2) + " dBFS" + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
    }

    // 5. pause / resume while recording: one seamless take
    pl->stop();
    pl->removeBacking();
    pl->clearTake();
    pl->renderNow();
    pl->record();
    run(1.0, [](juce::int64 t) { return 0.1f * (float) std::sin(t * 0.05); });
    pl->pause();
    run(0.5, [](juce::int64) { return 0.0f; });
    pl->pause(); // resume
    run(1.0, [](juce::int64 t) { return 0.1f * (float) std::sin(t * 0.05); });
    pl->stop();
    pl->renderNow();
    check(std::abs(pl->getTakeLengthSeconds() - 2.0) < 0.02, "PAUSE: recording paused and resumed -> one take of "
                                                              + juce::String(pl->getTakeLengthSeconds(), 3) + " s (2 s played)");

    // 6. BOUNCE: backing + take become the new backing; a new take layers on top
    pl->loadBacking(back48);
    pl->clearTake();
    pl->renderNow();
    pl->record();
    songZero = -1;
    run(2.0, [&](juce::int64 clock) {
        if (songZero < 0) songZero = clock;
        const juce::int64 heardAt = clock - songZero - limLat - deviceLatency;
        return (heardAt > 0 && heardAt % 24000 == 0) ? 0.2f : 0.0f;
    });
    pl->stop();
    pl->renderNow();
    const auto berr = pl->bounceNow();
    check(berr.isEmpty() && pl->hasBacking() && !pl->hasTake() && pl->getBackingName().startsWith("Bounce"),
          "BOUNCE: backing + take became the new backing track '" + pl->getBackingName() + "' " + berr);
    pl->record();
    songZero = -1;
    run(2.0, [&](juce::int64 clock) {
        if (songZero < 0) songZero = clock;
        const juce::int64 heardAt = clock - songZero - limLat - deviceLatency;
        return (heardAt > 0 && heardAt % 24000 == 0) ? 0.15f : 0.0f;
    });
    pl->stop();
    pl->renderNow();
    juce::AudioBuffer<float> mix;
    pl->renderMix(true, mix, startOff);
    const float layered = mix.getSample(0, 24000);
    check(std::abs(layered - 0.85f) < 0.02f, "LAYERS: click + first take + second take all at 0.5 s (" + juce::String(layered, 3) + " = 0.5 + 0.2 + 0.15)");

    // 7. never clips: full-scale backing + loud guitar
    {
        juce::AudioBuffer<float> loud(2, (int) (sr * 2));
        for (int i = 0; i < loud.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c) loud.setSample(c, i, 0.99f * (float) std::sin(2 * juce::MathConstants<double>::pi * 110 * i / sr));
        pl->stop();
        pl->loadBacking(writeWav(tmp.getChildFile("loud.wav"), sr, loud, 24));
        pl->setBackingGainDb(6.0f);
        pl->play();
        const auto o = run(1.5, [](juce::int64 t) { return 0.9f * (float) std::sin(t * 0.01); });
        pl->stop();
        check(o.getMagnitude(0, o.getNumSamples()) <= 0.8913f, "NO CLIPPING: backing +6 dB at full scale + loud guitar -> peak "
                                                                  + juce::String(juce::Decibels::gainToDecibels(o.getMagnitude(0, o.getNumSamples())), 2) + " dBFS");
        pl->setBackingGainDb(0.0f);
    }

    // 8. a 44.1 kHz backing track in a 48 kHz session: export lines up exactly, playback within a few samples
    {
        pl->stop();
        pl->loadBacking(writeWav(tmp.getChildFile("clicks441.wav"), 44100, clicks(44100, 3.0)));
        run(0.05, nullptr); // let the previous test's last millisecond leave the limiter
        pl->play();
        const auto o = run(1.2, nullptr);
        pl->stop();
        const auto pk = peaks(o, 0.15f);
        juce::AudioBuffer<float> m;
        pl->renderMix(true, m, startOff);
        const auto pm = peaks(m, 0.15f);
        const int live = pk.empty() ? -1 : pk[0] - limLat, exported = pm.empty() ? -1 : pm[0];
        check(exported >= 23999 && exported <= 24001 && std::abs(live - exported) <= 4,
              "44.1 kHz BACKING: click at sample " + juce::String(exported) + " in the export, " + juce::String(live) + " live (expected 24000)");
    }

    // 9. corrections: punch in / out while the take plays, crossfades, moving the start edge, undo
    {
        pl->stop();
        pl->removeBacking();
        pl->setTakeGainDb(0.0f);
        pl->clearTake();
        pl->renderNow();
        auto sine = [&](double f) { return [f, sr](juce::int64 t) { return 0.2f * (float) std::sin(2 * juce::MathConstants<double>::pi * f * (double) t / sr); }; };
        pl->toStart();
        pl->record(); // first take: 220 Hz for 3 s
        run(3.0, sine(220.0));
        pl->stop();
        pl->renderNow();
        // correction: play from the start, punch in at 1.0 s, out at 2.0 s, playing 330 Hz all along
        pl->toStart();
        pl->play();
        run(1.0, sine(330.0));
        pl->record();          // punch in
        run(1.0, sine(330.0));
        pl->record();          // punch out (playback goes on)
        run(0.5, sine(330.0));
        pl->stop();
        pl->renderNow();

        auto freqAt = [&](double seconds) {
            juce::AudioBuffer<float> g;
            int off = 0;
            pl->renderMix(false, g, off);
            const int a = (int) (seconds * sr) - off, len = (int) (0.1 * sr);
            int zc = 0;
            for (int i = a + 1; i < a + len && i < g.getNumSamples(); ++i)
                if ((g.getSample(0, i - 1) < 0.0f) != (g.getSample(0, i) < 0.0f)) ++zc;
            return zc * 5.0; // crossings per 0.1 s -> Hz
        };
        auto smooth = [&] {
            juce::AudioBuffer<float> g;
            int off = 0;
            pl->renderMix(false, g, off);
            float worst = 0.0f;
            for (int i = 1; i < g.getNumSamples(); ++i) worst = std::max(worst, std::abs(g.getSample(0, i) - g.getSample(0, i - 1)));
            return worst;
        };
        const float maxStep = 1.15f * (float) (2 * juce::MathConstants<double>::pi * 330.0 / sr * 0.2 * std::sqrt(2.0));
        const auto& cl = pl->getClips();
        const double inS = cl.size() == 2 ? (double) cl[1].in / sr : -1, outS = cl.size() == 2 ? (double) cl[1].out / sr : -1;
        const double f05 = freqAt(0.5), f15 = freqAt(1.5), f25 = freqAt(2.5);
        check(cl.size() == 2 && std::abs(f05 - 220) < 15 && std::abs(f15 - 330) < 15 && std::abs(f25 - 220) < 15,
              "PUNCH: correction recorded " + juce::String(inS, 3) + "-" + juce::String(outS, 3) + " s; heard: "
                  + juce::String(f05, 0) + " Hz / " + juce::String(f15, 0) + " Hz / " + juce::String(f25, 0) + " Hz (220 / 330 / 220)");
        const float st1 = smooth();
        check(st1 <= maxStep, "PUNCH: crossfades without a click (largest sample step " + juce::String(st1, 4) + " <= "
                                  + juce::String(maxStep, 4) + " = a smooth 330 Hz crossfade)");

        // drag the correction's start 0.3 s earlier: the pass was recorded from 0 s, so that audio exists
        if (cl.size() == 2) pl->setClipEdges(1, cl[1].in - (juce::int64) (0.3 * sr), cl[1].out);
        pl->renderNow();
        const double fEarly = freqAt(0.8);
        check(std::abs(fEarly - 330) < 15 && smooth() <= maxStep,
              "EDGE: start moved 0.3 s earlier (before the punch-in point) -> the correction is heard from 0.7 s (" + juce::String(fEarly, 0) + " Hz at 0.8 s), still smooth");
        const auto edgesEarly = pl->getClips()[1];
        if (const char* dir = std::getenv("RECORDER_SNAPSHOT")) // picture of the timeline (2 clips, backing loaded)
        {
            pl->loadBacking(back48);
            pl->setPositionSeconds(1.2);
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
            auto* e = dynamic_cast<AmpsurdEditor*>(ed.get());
            e->showPlayer(true);
            for (int k = 0; k < 20; ++k) { juce::Thread::sleep(100); e->refreshAll(); }
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::File out(juce::String(dir) + "/player_timeline.png");
            out.deleteFile();
            juce::FileOutputStream os(out);
            juce::PNGImageFormat().writeImageToStream(img, os);
            ed.reset();
            pl->removeBacking();
        }
        pl->setClipEdges(1, edgesEarly.in + (juce::int64) (0.6 * sr), edgesEarly.out);
        pl->renderNow();
        const double fLate = freqAt(1.1);
        check(std::abs(fLate - 220) < 15 && smooth() <= maxStep,
              "EDGE: start moved later -> the original take plays until 1.3 s (" + juce::String(fLate, 0) + " Hz at 1.1 s), still smooth");
        pl->undo();
        pl->renderNow();
        check(pl->getClips()[1].in == edgesEarly.in && std::abs(freqAt(0.8) - 330) < 15, "UNDO: the previous edge position is back");
        pl->undo();
        pl->undo();
        pl->renderNow();
        check(pl->getClips().size() == 1 && std::abs(freqAt(1.5) - 220) < 15, "UNDO: undoing the correction restores the original take");
        // a plain playback pass leaves no clip and no file behind
        const int files = pl->getRecordingsFolder().getChildFile("Takes").getNumberOfChildFiles(juce::File::findFiles);
        pl->toStart();
        pl->play();
        run(0.5, sine(330.0));
        pl->stop();
        check(pl->getClips().size() == 1 && pl->getRecordingsFolder().getChildFile("Takes").getNumberOfChildFiles(juce::File::findFiles) == files,
              "PLAY without REC: nothing is added (the pass recording is discarded)");
    }

    proc.reset();
    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "\nALL PASS\n" : "\n" + juce::String(failures) + " FAILURE(S)\n");
    return failures == 0 ? 0 : 1;
}

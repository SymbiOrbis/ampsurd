#include "PlayerRecorder.h"

#include <cmath>

#include "ampsurd/Resampler.h"

namespace
{
juce::String timestamp() { return juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"); }

// Reads a file as a stream of stereo samples at the session rate (resampled when needed).
class SessionStream
{
public:
    SessionStream(juce::AudioFormatManager& fm, const juce::File& f, double sessionRate)
    {
        reader.reset(fm.createReaderFor(f));
        if (reader)
            resampler = std::make_unique<ampsurd::StreamResampler>(reader->sampleRate, sessionRate, 2);
        fifo.assign(2, {});
    }
    bool ok() const { return reader != nullptr; }
    juce::int64 sessionLength(double sessionRate) const
    {
        return reader ? (juce::int64) std::ceil((double) reader->lengthInSamples * sessionRate / reader->sampleRate) : 0;
    }
    // fills n samples (zeros after the end)
    void read(float* L, float* R, int n)
    {
        while ((int) (fifo[0].size() - fifoPos) < n && !finished)
        {
            const int chunk = 16384;
            juce::AudioBuffer<float> b(2, chunk);
            const auto remaining = reader->lengthInSamples - filePos;
            const int got = (int) std::min<juce::int64>(chunk, remaining);
            if (got > 0)
            {
                reader->read(&b, 0, got, filePos, true, true);
                if (reader->numChannels == 1) b.copyFrom(1, 0, b, 0, 0, got); // mono file: both sides
                filePos += got;
                const float* p[2] = { b.getReadPointer(0), b.getReadPointer(1) };
                compact();
                resampler->process(p, got, fifo);
            }
            else
            {
                compact();
                resampler->finish(fifo);
                finished = true;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const bool has = fifoPos + (size_t) i < fifo[0].size();
            L[i] = has ? fifo[0][fifoPos + (size_t) i] : 0.0f;
            R[i] = has ? fifo[1][fifoPos + (size_t) i] : 0.0f;
        }
        fifoPos = std::min(fifo[0].size(), fifoPos + (size_t) n);
    }

private:
    void compact()
    {
        if (fifoPos > 0)
            for (auto& c : fifo) c.erase(c.begin(), c.begin() + (long) fifoPos);
        fifoPos = 0;
    }
    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<ampsurd::StreamResampler> resampler;
    std::vector<std::vector<float>> fifo;
    size_t fifoPos = 0;
    juce::int64 filePos = 0;
    bool finished = false;
};
} // namespace

juce::StringArray PlayerRecorder::formatNames()
{
    return { "WAV 16-bit (CD)", "WAV 24-bit", "WAV 32-bit float", "FLAC 16-bit", "FLAC 24-bit" };
}

juce::Array<double> PlayerRecorder::exportRates() { return { 44100.0, 48000.0, 96000.0 }; }

PlayerRecorder::PlayerRecorder()
{
    formats.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg Vorbis, MP3 (decoding) (+ Windows Media on Windows)
    readThread.startThread();
    writeThread.startThread();
}

PlayerRecorder::~PlayerRecorder()
{
    alive->store(false);
    jobs.removeAllJobs(true, 30000);
    renderJobs.removeAllJobs(true, 30000);
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        const juce::ScopedLock sl(writerLock);
        old = std::move(writer);
    }
    old.reset();
    swapTrack(backing, nullptr);
    swapTrack(take, nullptr);
    readThread.stopThread(2000);
    writeThread.stopThread(2000);
}

juce::File PlayerRecorder::getRecordingsFolder() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("AMPSURD").getChildFile("Recordings");
}

void PlayerRecorder::prepare(double sr, int block)
{
    const bool rateChanged = std::abs(sr - sampleRate) > 0.5;
    if (rateChanged)
    {
        // clip positions are session samples: keep them at the same times
        const double k = sr / sampleRate;
        auto scale = [k](std::vector<Clip>& list) {
            for (auto& c : list)
            {
                c.fileStart = (juce::int64) std::llround((double) c.fileStart * k);
                c.fileLength = (juce::int64) std::llround((double) c.fileLength * k);
                c.in = (juce::int64) std::llround((double) c.in * k);
                c.out = (juce::int64) std::llround((double) c.out * k);
            }
        };
        scale(clips);
        for (auto& u : undoStack) scale(u);
        takeStart.store((juce::int64) std::llround((double) takeStart.load() * k));
    }
    sampleRate = sr;
    maxBlock = std::max(1, block);
    bufA.setSize(2, maxBlock);
    bufB.setSize(2, maxBlock);
    recBuf.setSize(2, maxBlock);
    limiter.prepare(sr);
    if (rateChanged || backing || take)
    {
        closePunch();
        endPass();
        state.store((int) State::stopped);
        songPos.store(0);
        if (backingReady.load()) loadBacking(backingFile);
        if (takeReady.load()) loadTakeForPlayback();
    }
}

// ---------------------------------------------------------------------------------------------
std::unique_ptr<PlayerRecorder::Track> PlayerRecorder::makeTrack(const juce::File& f, juce::String& error, juce::int64& sessionLength)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f));
    if (!reader) { error = "Could not read this file (WAV, FLAC, MP3, OGG, AIFF)."; return nullptr; }
    if (reader->lengthInSamples <= 0) { error = "The file contains no audio."; return nullptr; }
    auto t = std::make_unique<Track>();
    t->fileRate = reader->sampleRate;
    t->ratio = t->fileRate / sampleRate;
    sessionLength = (juce::int64) std::ceil((double) reader->lengthInSamples / t->ratio);
    auto* src = new juce::AudioFormatReaderSource(reader.release(), true);
    t->buffering = std::make_unique<juce::BufferingAudioSource>(src, readThread, true, 1 << 17, 2, true);
    if (std::abs(t->ratio - 1.0) > 1e-9)
    {
        t->resampler = std::make_unique<juce::ResamplingAudioSource>(t->buffering.get(), false, 2);
        t->resampler->setResamplingRatio(t->ratio);
    }
    t->out().prepareToPlay(maxBlock, sampleRate); // fills the read-ahead buffer (blocking, message thread)
    return t;
}

void PlayerRecorder::swapTrack(std::unique_ptr<Track>& slot, std::unique_ptr<Track> next)
{
    std::unique_ptr<Track> old;
    {
        const juce::ScopedLock sl(trackLock);
        old = std::move(slot);
        slot = std::move(next);
    }
    if (old) old->out().releaseResources(); // deleted outside the lock
}

void PlayerRecorder::seek(Track& t, juce::int64 pos)
{
    {
        const juce::ScopedLock sl(trackLock);
        t.buffering->setNextReadPosition((juce::int64) std::llround((double) pos * t.ratio));
        if (t.resampler) t.resampler->flushBuffers();
    }
    waitReady(t, 1000);
}

void PlayerRecorder::waitReady(Track& t, int timeoutMs)
{
    // block (message thread) until the read-ahead holds the first second, so playback starts complete
    juce::AudioBuffer<float> dummy(2, 1);
    const int need = (int) std::ceil(sampleRate * t.ratio);
    juce::AudioSourceChannelInfo probe(&dummy, 0, 1);
    probe.numSamples = need;
    t.buffering->waitForNextAudioBlockReady(probe, (juce::uint32) timeoutMs);
}

void PlayerRecorder::readTrack(Track& t, juce::AudioBuffer<float>& buf, int n, bool offline) noexcept
{
    if (offline)
    {
        juce::AudioSourceChannelInfo probe(&buf, 0, n);
        probe.numSamples = (int) std::ceil(n * t.ratio) + 16;
        t.buffering->waitForNextAudioBlockReady(probe, 2000);
    }
    juce::AudioSourceChannelInfo info(&buf, 0, n);
    t.out().getNextAudioBlock(info);
}

// ---------------------------------------------------------------------------------------------
void PlayerRecorder::process(double* L, double* R, int n, int ampsurdLatency, bool offline) noexcept
{
    lastAmpsurdLatency.store(ampsurdLatency, std::memory_order_relaxed);
    const auto st = (State) state.load(std::memory_order_acquire);
    const bool running = st == State::playing || st == State::recording;

    if (running)
    {
        const juce::int64 pos = songPos.load(std::memory_order_relaxed);

        // 1. record the guitar exactly as AMPSURD outputs it (before the tracks are added). Every pass
        //    is recorded while a take exists (so a correction can be extended to before its punch-in).
        if (passActive.load(std::memory_order_acquire))
        {
            if (passStartPending.exchange(false))
            {
                const juce::int64 start = pos - pendingCompensation.load();
                recSkip = start < 0 ? -start : 0;
                passStart.store(std::max<juce::int64>(0, start));
            }
            int skip = (int) std::min<juce::int64>(recSkip, n);
            recSkip -= skip;
            const int count = n - skip;
            if (count > 0)
            {
                auto* a = recBuf.getWritePointer(0);
                auto* b = recBuf.getWritePointer(1);
                for (int i = 0; i < count; ++i)
                {
                    a[i] = (float) L[skip + i];
                    b[i] = (float) R[skip + i];
                }
                const float* ptrs[2] = { a, b };
                const juce::ScopedLock sl(writerLock);
                if (writer) writer->write(ptrs, count);
            }
        }

        const double tB = backingReady.load() ? juce::Decibels::decibelsToGain((double) backingGainDb.load(), -100.0) : 0.0;
        // while a correction is being recorded, the old take is silent from the punch-in on
        const bool muted = st == State::recording && pos >= muteTakeFrom.load(std::memory_order_relaxed);
        const double tT = muted ? 0.0 : juce::Decibels::decibelsToGain((double) takeGainDb.load(), -100.0);

        const juce::ScopedLock tl(trackLock); // held only briefly by the message thread (swap / seek)

        // 2. backing track
        if (backingReady.load() && backing)
        {
            readTrack(*backing, bufA, n, offline);
            const float* a = bufA.getReadPointer(0);
            const float* b = bufA.getReadPointer(1);
            float pk = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                gB += (tB - gB) * 0.002;
                L[i] += gB * a[i];
                R[i] += gB * b[i];
                pk = std::max(pk, std::max(std::abs(a[i]), std::abs(b[i])) * (float) gB);
            }
            if (pk > backingPeak.load(std::memory_order_relaxed)) backingPeak.store(pk, std::memory_order_relaxed);
        }

        // 3. the take (the composite of all clips)
        if (takeReady.load() && take)
        {
            const juce::int64 ts = takeStart.load();
            const int off = (int) juce::jlimit<juce::int64>(0, n, ts - pos);
            if (off < n)
            {
                readTrack(*take, bufB, n - off, offline);
                const float* a = bufB.getReadPointer(0);
                const float* b = bufB.getReadPointer(1);
                float pk = 0.0f;
                for (int i = 0; i < n - off; ++i)
                {
                    gT += (tT - gT) * 0.002;
                    L[off + i] += gT * a[i];
                    R[off + i] += gT * b[i];
                    pk = std::max(pk, std::max(std::abs(a[i]), std::abs(b[i])) * (float) gT);
                }
                if (pk > takePeak.load(std::memory_order_relaxed)) takePeak.store(pk, std::memory_order_relaxed);
            }
        }
        juce::int64 expected = pos; // only advance if the message thread did not move the position meanwhile
        songPos.compare_exchange_strong(expected, pos + n, std::memory_order_relaxed);
    }

    // 4. the sum never clips (always in the path, so the latency is constant)
    limiter.process(L, R, n);
}

// ---------------------------------------------------------------------------------------------
juce::String PlayerRecorder::loadBacking(const juce::File& f)
{
    juce::String err;
    juce::int64 len = 0;
    auto t = makeTrack(f, err, len);
    if (!t) return err;
    seek(*t, songPos.load());
    backingReady.store(false);
    swapTrack(backing, std::move(t));
    backingFile = f;
    backingLength.store(len);
    backingReady.store(true);
    backingThumb.setSource(new juce::FileInputSource(f));
    return {};
}

void PlayerRecorder::removeBacking()
{
    backingReady.store(false);
    swapTrack(backing, nullptr);
    backingFile = juce::File();
    backingLength.store(0);
    backingThumb.clear();
}

void PlayerRecorder::positionTransports(juce::int64 pos)
{
    if (backing) seek(*backing, pos);
    if (take) seek(*take, std::max<juce::int64>(0, pos - takeStart.load()));
}

juce::int64 PlayerRecorder::compensationSamples() const
{
    return (juce::int64) std::lround(getCompensationMs() * sampleRate / 1000.0);
}

void PlayerRecorder::startPass()
{
    if (passActive.load()) return;
    const auto folder = getRecordingsFolder().getChildFile("Takes");
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile("Take " + timestamp(), ".wav");
    std::unique_ptr<juce::OutputStream> os = file.createOutputStream();
    if (!os) return;
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(32)
                                         .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (!w) return;
    passThumb.reset(2, sampleRate);
    auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(w.release(), writeThread, 1 << 17);
    tw->setDataReceiver(&passThumb); // live waveform while recording
    {
        const juce::ScopedLock sl(writerLock);
        writer = std::move(tw);
    }
    passFile = file;
    passPunches.clear();
    punchIn = -1;
    pendingCompensation.store((int) compensationSamples());
    passStart.store(std::max<juce::int64>(0, songPos.load() - pendingCompensation.load()));
    passStartPending.store(true);
    passActive.store(true, std::memory_order_release);
}

void PlayerRecorder::closePunch()
{
    if (getState() != State::recording && getState() != State::pausedRec) return;
    const juce::int64 out = songPos.load() - compensationSamples();
    passPunches.push_back({ punchIn, out });
    punchIn = -1;
    muteTakeFrom.store(std::numeric_limits<juce::int64>::max());
}

void PlayerRecorder::endPass()
{
    if (!passActive.exchange(false)) return;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        const juce::ScopedLock sl(writerLock); // short: the audio thread only waits for a pointer swap
        old = std::move(writer);
    }
    old.reset(); // flushes and closes the file, outside the lock

    juce::int64 length = 0;
    if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor(passFile) })
        length = r->lengthInSamples;
    const juce::int64 fs = passStart.load();
    std::vector<Clip> added;
    const auto minLen = (juce::int64) (0.05 * sampleRate);
    for (const auto& [pin, pout] : passPunches)
    {
        Clip c { passFile, fs, length, pin < 0 ? fs : pin, pout };
        c.in = juce::jlimit(fs, fs + length, c.in);
        c.out = juce::jlimit(fs, fs + length, c.out);
        if (c.out - c.in >= minLen) added.push_back(c);
    }
    passPunches.clear();
    if (added.empty())
    {
        passFile.deleteFile(); // nothing was punched in: the pass is not needed
        return;
    }
    auto next = clips;
    next.insert(next.end(), added.begin(), added.end());
    commitClips(std::move(next));
}

void PlayerRecorder::play()
{
    const auto st = getState();
    if (st == State::recording || st == State::playing || busy.load()) return;
    if (st == State::pausedRec) { pause(); return; } // CONTINUE
    if (st == State::stopped) playStartPos = songPos.load();
    positionTransports(songPos.load());
    if (takeReady.load() || !clips.empty()) startPass(); // a correction can be punched in at any moment
    state.store((int) State::playing, std::memory_order_release);
}

void PlayerRecorder::pause()
{
    switch (getState())
    {
        case State::playing:
            state.store((int) State::pausedPlay); // the pass keeps its file: CONTINUE goes on seamlessly
            break;
        case State::recording:
            state.store((int) State::pausedRec);
            break;
        case State::pausedPlay:
            positionTransports(songPos.load());
            state.store((int) State::playing);
            break;
        case State::pausedRec:
            positionTransports(songPos.load());
            state.store((int) State::recording);
            break;
        case State::stopped: break;
    }
}

void PlayerRecorder::stop()
{
    closePunch();
    state.store((int) State::stopped, std::memory_order_release);
    endPass();
    songPos.store(playStartPos);
    positionTransports(playStartPos);
}

void PlayerRecorder::toStart()
{
    setPositionSeconds(0.0);
    playStartPos = 0;
}

void PlayerRecorder::setPositionSeconds(double seconds)
{
    const auto st = getState();
    if (st == State::recording || st == State::pausedRec) return;
    const auto pos = std::max<juce::int64>(0, (juce::int64) std::llround(seconds * sampleRate));
    if (st == State::pausedPlay) endPass(); // the pass file would no longer match the timeline
    if (st == State::stopped) playStartPos = pos;
    songPos.store(pos);
    positionTransports(pos);
    if (st == State::playing)
    {
        endPass();
        if (takeReady.load() || !clips.empty()) startPass();
    }
}

double PlayerRecorder::getCompensationMs() const
{
    const int dev = deviceLatency ? deviceLatency() : 0;
    const double samples = lastAmpsurdLatency.load() + dev + limiter.getLatencySamples();
    return samples * 1000.0 / sampleRate + offsetMs.load();
}

void PlayerRecorder::record()
{
    if (busy.load()) return;
    switch (getState())
    {
        case State::recording: // second press: punch out, playback continues
            closePunch();
            state.store((int) State::playing, std::memory_order_release);
            return;
        case State::pausedRec:
            return;
        case State::playing:
            if (!passActive.load()) startPass();
            break;
        case State::pausedPlay:
            positionTransports(songPos.load());
            break;
        case State::stopped:
            playStartPos = songPos.load();
            positionTransports(songPos.load());
            startPass();
            break;
    }
    if (!passActive.load()) return;
    const bool firstTake = clips.empty() && !takeReady.load();
    punchIn = firstTake ? -1 : std::max<juce::int64>(0, songPos.load() - compensationSamples());
    muteTakeFrom.store(songPos.load());
    state.store((int) State::recording, std::memory_order_release);
}

// ---------------------------------------------------------------------------------------------
// Clips -> composite take
// ---------------------------------------------------------------------------------------------
void PlayerRecorder::commitClips(std::vector<Clip> next)
{
    undoStack.push_back(clips);
    if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
    clips = std::move(next);
    scheduleRender();
}

void PlayerRecorder::setClipEdges(int index, juce::int64 in, juce::int64 out)
{
    if (index < 0 || index >= (int) clips.size()) return;
    auto next = clips;
    auto& c = next[(size_t) index];
    const auto minLen = (juce::int64) (0.05 * sampleRate);
    c.in = juce::jlimit(c.fileStart, c.fileEnd() - minLen, in);
    c.out = juce::jlimit(c.in + minLen, c.fileEnd(), out);
    if (c.in == clips[(size_t) index].in && c.out == clips[(size_t) index].out) return;
    commitClips(std::move(next));
}

void PlayerRecorder::removeClip(int index)
{
    if (index < 0 || index >= (int) clips.size()) return;
    auto next = clips;
    next.erase(next.begin() + index);
    commitClips(std::move(next));
}

void PlayerRecorder::undo()
{
    if (undoStack.empty() || getState() == State::recording || getState() == State::pausedRec) return;
    clips = undoStack.back();
    undoStack.pop_back();
    scheduleRender();
}

void PlayerRecorder::clearTake()
{
    const auto st = getState();
    if (st == State::recording || st == State::pausedRec) return;
    if (!clips.empty()) commitClips({});
    else scheduleRender();
}

void PlayerRecorder::scheduleRender()
{
    const int gen = ++renderGeneration;
    auto list = clips;
    auto aliveFlag = alive;
    renderJobs.addJob([this, gen, list, aliveFlag] {
        juce::File f;
        juce::int64 start = 0;
        const auto err = aliveFlag->load() && gen == renderGeneration.load() ? renderComposite(list, f, start) : juce::String("skipped");
        juce::MessageManager::callAsync([this, gen, f, start, err, aliveFlag] {
            if (!aliveFlag->load()) return;
            if (gen == renderGeneration.load()) // a newer edit replaces this result
            {
                if (err.isEmpty()) adoptComposite(f, start);
                else if (err == "empty") adoptComposite({}, 0);
                renderError = err.isEmpty() || err == "empty" ? juce::String() : err;
                adoptedGeneration.store(gen);
            }
            else
                f.deleteFile();
        });
    });
}

juce::String PlayerRecorder::renderNow()
{
    // blocking version (tests, no message loop): renders the current clips here; pending background
    // renders see a newer generation and are dropped
    ++renderGeneration;
    renderJobs.removeAllJobs(false, 10000);
    juce::File f;
    juce::int64 start = 0;
    const auto err = renderComposite(clips, f, start);
    if (err.isEmpty()) adoptComposite(f, start);
    else if (err == "empty") adoptComposite({}, 0);
    adoptedGeneration.store(renderGeneration.load());
    return err == "empty" ? juce::String() : err;
}

void PlayerRecorder::adoptComposite(const juce::File& f, juce::int64 start)
{
    const auto old = takeFile;
    const bool oldIsComposite = old.getFileName().startsWith("Track ");
    takeFile = f;
    takeStart.store(start);
    if (f.existsAsFile())
    {
        loadTakeForPlayback();
        takeThumb.setSource(new juce::FileInputSource(f));
    }
    else
    {
        takeReady.store(false);
        swapTrack(take, nullptr);
        takeLength.store(0);
        takeThumb.clear();
    }
    if (oldIsComposite && old != f) old.deleteFile(); // composites are rebuilt from the clips at any time
}

juce::String PlayerRecorder::renderComposite(const std::vector<Clip>& list, juce::File& result, juce::int64& start)
{
    if (list.empty()) return "empty";
    const auto X = (juce::int64) std::lround(kCrossfadeSeconds * sampleRate);
    struct Fades { juce::int64 a0, a1, b0, b1; };
    std::vector<Fades> fades;
    juce::int64 t0 = std::numeric_limits<juce::int64>::max(), t1 = 0;
    for (const auto& c : list)
    {
        Fades f {};
        // centred on the edge when the recording extends beyond it, otherwise just inside
        if (c.in - X / 2 >= c.fileStart) { f.a0 = c.in - X / 2; f.a1 = f.a0 + X; }
        else { f.a0 = c.in; f.a1 = c.in + X; }
        if (c.out + X / 2 <= c.fileEnd()) { f.b1 = c.out + X / 2; f.b0 = f.b1 - X; }
        else { f.b1 = c.out; f.b0 = c.out - X; }
        f.b0 = std::max(f.b0, f.a1);
        fades.push_back(f);
        t0 = std::min(t0, f.a0);
        t1 = std::max(t1, f.b1);
    }
    t0 = std::max<juce::int64>(0, t0);

    struct Source { std::unique_ptr<SessionStream> s; juce::int64 next; };
    std::vector<Source> src;
    for (const auto& c : list)
    {
        auto st = std::make_unique<SessionStream>(formats, c.file, sampleRate);
        if (!st->ok()) return "A recording is missing: " + c.file.getFileName();
        src.push_back({ std::move(st), c.fileStart });
    }

    const auto folder = getRecordingsFolder().getChildFile("Takes");
    folder.createDirectory();
    result = folder.getNonexistentChildFile("Track " + timestamp(), ".wav");
    std::unique_ptr<juce::OutputStream> os = result.createOutputStream();
    if (!os) return "Cannot write " + result.getFullPathName();
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(32)
                                         .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (!w) return "Cannot write the track.";

    const int chunk = 32768;
    std::vector<float> accL(chunk), accR(chunk), xl(chunk), xr(chunk), skipBuf(chunk);
    const double halfPi = juce::MathConstants<double>::halfPi;
    for (juce::int64 pos = t0; pos < t1; pos += chunk)
    {
        if (!alive->load()) return "Cancelled.";
        const int n = (int) std::min<juce::int64>(chunk, t1 - pos);
        std::fill(accL.begin(), accL.begin() + n, 0.0f);
        std::fill(accR.begin(), accR.begin() + n, 0.0f);
        for (size_t k = 0; k < list.size(); ++k)
        {
            const auto& f = fades[k];
            const juce::int64 from = std::max(pos, f.a0), to = std::min(pos + n, f.b1);
            if (from >= to) continue;
            auto& sk = src[k];
            while (sk.next < from) // skip forward in the file (sequential reading)
            {
                const int m = (int) std::min<juce::int64>(chunk, from - sk.next);
                sk.s->read(skipBuf.data(), skipBuf.data(), m);
                sk.next += m;
            }
            const int m = (int) (to - from);
            sk.s->read(xl.data(), xr.data(), m);
            sk.next += m;
            for (int i = 0; i < m; ++i)
            {
                const juce::int64 t = from + i;
                double wgt = 1.0;
                if (t < f.a1) wgt = (double) (t - f.a0 + 0.5) / (double) (f.a1 - f.a0);
                else if (t >= f.b0) wgt = (double) (f.b1 - t - 0.5) / (double) (f.b1 - f.b0);
                wgt = juce::jlimit(0.0, 1.0, wgt);
                const auto gIn = (float) std::sin(wgt * halfPi), gOut = (float) std::cos(wgt * halfPi); // equal power
                const size_t o = (size_t) (t - pos);
                accL[o] = accL[o] * gOut + xl[(size_t) i] * gIn;
                accR[o] = accR[o] * gOut + xr[(size_t) i] * gIn;
            }
        }
        const float* p[2] = { accL.data(), accR.data() };
        if (!w->writeFromFloatArrays(p, 2, n)) return "Writing failed (disk full?)";
    }
    w.reset();
    start = t0;
    return {};
}

void PlayerRecorder::loadTakeForPlayback()
{
    takeReady.store(false);
    swapTrack(take, nullptr);
    juce::String err;
    juce::int64 len = 0;
    auto t = makeTrack(takeFile, err, len);
    if (!t) { takeLength.store(0); return; }
    seek(*t, std::max<juce::int64>(0, songPos.load() - takeStart.load()));
    takeLength.store(len);
    swapTrack(take, std::move(t));
    takeReady.store(true);
}

void PlayerRecorder::poll()
{
    const auto st = getState();
    if (st == State::playing && !passActive.load() && songPos.load() >= (juce::int64) (getLengthSeconds() * sampleRate) + (juce::int64) (0.2 * sampleRate))
        stop(); // end of the material (playback only; while a pass is recorded you can play on)
    if (st == State::playing && passActive.load() && songPos.load() >= (juce::int64) (getLengthSeconds() * sampleRate) + (juce::int64) (2.0 * sampleRate))
        stop();
}

double PlayerRecorder::getPositionSeconds() const { return (double) songPos.load() / sampleRate; }
double PlayerRecorder::getBackingLengthSeconds() const { return backingReady.load() ? (double) backingLength.load() / sampleRate : 0.0; }
double PlayerRecorder::getTakeLengthSeconds() const { return takeReady.load() ? (double) takeLength.load() / sampleRate : 0.0; }
double PlayerRecorder::getLengthSeconds() const
{
    double t = takeReady.load() ? (double) (takeStart.load() + takeLength.load()) / sampleRate : 0.0;
    const auto st = getState();
    if (st == State::recording || st == State::pausedRec)
        t = std::max(t, getPositionSeconds());
    return std::max(getBackingLengthSeconds(), t);
}

// ---------------------------------------------------------------------------------------------
// Rendering: the mix as heard (backing x vol + take x vol, limited), at the session rate
// ---------------------------------------------------------------------------------------------
namespace
{
struct MixSpec
{
    bool backing = false, take = false;
    juce::File backingFile, takeFile;
    double backingGain = 1.0, takeGain = 1.0;
    juce::int64 takeStart = 0, start = 0, end = 0;
};

// renders [start, end) of the timeline in chunks; sink(L, R, n) gets the limited result
juce::String renderTimeline(juce::AudioFormatManager& fm, const MixSpec& m, double sr,
                            const std::function<void(const float*, const float*, int)>& sink, std::atomic<float>& progress,
                            const std::shared_ptr<std::atomic<bool>>& alive)
{
    std::unique_ptr<SessionStream> back, take;
    if (m.backing) { back = std::make_unique<SessionStream>(fm, m.backingFile, sr); if (!back->ok()) return "The backing track cannot be read."; }
    if (m.take) { take = std::make_unique<SessionStream>(fm, m.takeFile, sr); if (!take->ok()) return "The recording cannot be read."; }

    ampsurd::SafetyLimiter lim;
    lim.prepare(sr);
    const int la = lim.getLatencySamples();
    const int chunk = 8192;
    std::vector<float> bl(chunk), br(chunk), tl(chunk), tr(chunk);
    std::vector<double> L(chunk), R(chunk);
    std::vector<float> oL(chunk), oR(chunk);
    juce::int64 pos = m.start, dropped = 0;
    const juce::int64 total = m.end - m.start + la;
    juce::int64 done = 0;
    // skip backing before the start (guitar-only exports without backing start at the take)
    while (done < total)
    {
        if (!alive->load()) return "Cancelled.";
        const int n = (int) std::min<juce::int64>(chunk, total - done);
        std::fill(L.begin(), L.begin() + n, 0.0);
        std::fill(R.begin(), R.begin() + n, 0.0);
        if (back)
        {
            back->read(bl.data(), br.data(), n);
            for (int i = 0; i < n; ++i) { L[(size_t) i] += m.backingGain * bl[(size_t) i]; R[(size_t) i] += m.backingGain * br[(size_t) i]; }
        }
        if (take)
        {
            // the take begins at takeStart on the timeline
            const int off = (int) juce::jlimit<juce::int64>(0, n, m.takeStart - pos);
            if (off < n)
            {
                take->read(tl.data(), tr.data(), n - off);
                for (int i = 0; i < n - off; ++i) { L[(size_t) (off + i)] += m.takeGain * tl[(size_t) i]; R[(size_t) (off + i)] += m.takeGain * tr[(size_t) i]; }
            }
        }
        lim.process(L.data(), R.data(), n);
        // drop the limiter's look-ahead at the beginning, so the result starts exactly at `start`
        const int skip = (int) std::min<juce::int64>(la - dropped, n);
        dropped += skip;
        for (int i = skip; i < n; ++i) { oL[(size_t) (i - skip)] = (float) L[(size_t) i]; oR[(size_t) (i - skip)] = (float) R[(size_t) i]; }
        if (n - skip > 0) sink(oL.data(), oR.data(), n - skip);
        pos += n;
        done += n;
        progress.store((float) done / (float) std::max<juce::int64>(1, total));
    }
    return {};
}
} // namespace

juce::String PlayerRecorder::renderMix(bool withBacking, juce::AudioBuffer<float>& out, int& startOffset)
{
    MixSpec m;
    m.backing = withBacking && backingReady.load();
    m.take = takeReady.load();
    m.backingFile = backingFile;
    m.takeFile = takeFile;
    m.backingGain = juce::Decibels::decibelsToGain((double) backingGainDb.load(), -100.0);
    m.takeGain = juce::Decibels::decibelsToGain((double) takeGainDb.load(), -100.0);
    m.takeStart = takeStart.load();
    const juce::int64 takeEnd = m.take ? m.takeStart + takeLength.load() : 0;
    m.start = backingReady.load() ? 0 : (m.take ? m.takeStart : 0);
    m.end = std::max(m.backing ? backingLength.load() : 0, takeEnd);
    startOffset = (int) m.start;
    if (m.end <= m.start) return "Nothing to export yet.";
    out.setSize(2, (int) (m.end - m.start));
    int w = 0;
    std::atomic<float> prog { 0.0f };
    return renderTimeline(formats, m, sampleRate, [&](const float* l, const float* r, int n) {
        out.copyFrom(0, w, l, n);
        out.copyFrom(1, w, r, n);
        w += n;
    }, prog, alive);
}

juce::String PlayerRecorder::runExport(const juce::File& dest, ExportOptions o)
{
    MixSpec m;
    m.backing = o.withBacking && backingReady.load();
    m.take = takeReady.load();
    m.backingFile = backingFile;
    m.takeFile = takeFile;
    m.backingGain = juce::Decibels::decibelsToGain((double) backingGainDb.load(), -100.0);
    m.takeGain = juce::Decibels::decibelsToGain((double) takeGainDb.load(), -100.0);
    m.takeStart = takeStart.load();
    const juce::int64 takeEnd = m.take ? m.takeStart + takeLength.load() : 0;
    // guitar alone still starts at the backing's start (it lines up with it in a DAW)
    m.start = backingReady.load() ? 0 : (m.take ? m.takeStart : 0);
    m.end = std::max(m.backing ? backingLength.load() : 0, takeEnd);
    if (m.end <= m.start) return "Nothing to export yet.";

    const bool flac = o.format >= 3;
    const int bits = o.format == 0 || o.format == 3 ? 16 : (o.format == 2 ? 32 : 24);
    dest.deleteFile();
    std::unique_ptr<juce::OutputStream> os = dest.createOutputStream();
    if (!os) return "Cannot write " + dest.getFullPathName();
    auto opts = juce::AudioFormatWriterOptions {}.withSampleRate(o.rate).withNumChannels(2).withBitsPerSample(bits);
    if (bits == 32) opts = opts.withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    std::unique_ptr<juce::AudioFormatWriter> w;
    if (flac) { juce::FlacAudioFormat f; w = f.createWriterFor(os, opts); }
    else { juce::WavAudioFormat f; w = f.createWriterFor(os, opts); }
    if (!w) return "This format cannot be written.";

    ampsurd::StreamResampler rs(sampleRate, o.rate, 2);
    std::vector<std::vector<float>> conv(2);
    juce::Random rnd(12345);
    const float lsb = bits == 32 ? 0.0f : 1.0f / (float) (1 << (bits - 1));
    juce::String err;
    auto write = [&] {
        const int n = (int) conv[0].size();
        if (n == 0) return;
        if (lsb > 0.0f) // TPDF dither (+-1 LSB) before reducing to 16 / 24 bit
            for (auto& ch : conv)
                for (auto& v : ch) v += (rnd.nextFloat() - rnd.nextFloat()) * lsb;
        const float* p[2] = { conv[0].data(), conv[1].data() };
        if (!w->writeFromFloatArrays(p, 2, n)) err = "Writing failed (disk full?)";
        conv[0].clear();
        conv[1].clear();
    };
    const auto renderErr = renderTimeline(formats, m, sampleRate, [&](const float* l, const float* r, int n) {
        const float* p[2] = { l, r };
        rs.process(p, n, conv);
        write();
    }, progress, alive);
    if (renderErr.isNotEmpty()) return renderErr;
    rs.finish(conv);
    write();
    w.reset();
    return err;
}

void PlayerRecorder::exportAudio(const juce::File& dest, ExportOptions o, std::function<void(const juce::String&)> done)
{
    if (busy.exchange(true)) return;
    progress.store(0.0f);
    auto aliveFlag = alive;
    jobs.addJob([this, dest, o, done, aliveFlag] {
        const auto err = runExport(dest, o);
        busy.store(false);
        juce::MessageManager::callAsync([done, err, aliveFlag] {
            if (aliveFlag->load() && done) done(err);
        });
    });
}

juce::File PlayerRecorder::newBounceFile() const
{
    const auto folder = getRecordingsFolder().getChildFile("Bounces");
    folder.createDirectory();
    return folder.getNonexistentChildFile("Bounce " + timestamp(), ".wav");
}

juce::String PlayerRecorder::adoptBounce(const juce::File& file)
{
    stop();
    // the bounce becomes the backing track; the take is now part of it
    ++renderGeneration;
    clips.clear();
    undoStack.clear();
    takeReady.store(false);
    swapTrack(take, nullptr);
    takeThumb.clear();
    backingGainDb.store(0.0f);
    return loadBacking(file);
}

void PlayerRecorder::bounce(std::function<void(const juce::String&)> done)
{
    if (!takeReady.load()) { if (done) done("Record a take first."); return; }
    const auto file = newBounceFile();
    ExportOptions o;
    o.format = 2; // 32-bit float at the session rate: no loss
    o.rate = sampleRate;
    o.withBacking = true;
    exportAudio(file, o, [this, file, done](const juce::String& err) {
        const auto e = err.isEmpty() ? adoptBounce(file) : err;
        if (done) done(e);
    });
}

juce::String PlayerRecorder::bounceNow()
{
    if (!takeReady.load()) return "Record a take first.";
    const auto file = newBounceFile();
    ExportOptions o;
    o.format = 2;
    o.rate = sampleRate;
    o.withBacking = true;
    const auto err = runExport(file, o);
    return err.isEmpty() ? adoptBounce(file) : err;
}

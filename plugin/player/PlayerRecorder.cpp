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
    for (auto& l : lanes)
    {
        l.takeThumb = std::make_unique<juce::AudioThumbnail>(256, formats, thumbCache);
        l.passThumb = std::make_unique<juce::AudioThumbnail>(256, formats, thumbCache);
    }
    lanes[0].armed.store(true); // GUITAR is armed from the start, INPUT 2 is armed by hand (owner 2026-11-12)
    readThread.startThread();
    writeThread.startThread();
}

PlayerRecorder::~PlayerRecorder()
{
    alive->store(false);
    jobs.removeAllJobs(true, 30000);
    renderJobs.removeAllJobs(true, 30000);
    for (auto& l : lanes)
    {
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
        {
            const juce::ScopedLock sl(writerLock);
            old = std::move(l.writer);
        }
        old.reset();
        swapTrack(l.take, nullptr);
    }
    swapTrack(backing, nullptr);
    readThread.stopThread(2000);
    writeThread.stopThread(2000);
    for (auto& l : lanes) { l.takeThumb.reset(); l.passThumb.reset(); } // before the thumbnail cache goes
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
        for (int t = 0; t < kTracks; ++t)
        {
            auto& l = lanes[(size_t) t];
            scale(l.clips);
            scale(l.renderedClips);
            for (auto& u : undoSnaps) scale(u.clips[(size_t) t]);
            for (auto& u : redoSnaps) scale(u.clips[(size_t) t]);
            l.takeStart.store((juce::int64) std::llround((double) l.takeStart.load() * k));
        }
    }
    sampleRate = sr;
    maxBlock = std::max(1, block);
    bufA.setSize(2, maxBlock);
    bufB.setSize(2, maxBlock);
    recBuf.setSize(2, maxBlock);
    limiter.prepare(sr);
    clickBlock.assign((size_t) maxBlock, 0.0);
    clickDelay.assign((size_t) limiter.getLatencySamples() + 1, 0.0);
    clickDelayPos = 0;
    bool anyTake = false;
    for (auto& l : lanes) anyTake = anyTake || l.take != nullptr;
    if (rateChanged || backing || anyTake)
    {
        closePunches();
        endPasses();
        state.store((int) State::stopped);
        songPos.store(0);
        if (backingReady.load()) loadBacking(backingFile);
        for (int t = 0; t < kTracks; ++t)
            if (lanes[(size_t) t].takeReady.load()) loadTakeForPlayback(t);
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
void PlayerRecorder::process(double* L, double* R, int n, int ampsurdLatency, bool offline, const float* in2) noexcept
{
    lastAmpsurdLatency.store(ampsurdLatency, std::memory_order_relaxed);
    const auto st = (State) state.load(std::memory_order_acquire);
    const bool running = st == State::playing || st == State::recording;
    auto& gl = lanes[0]; // GUITAR
    auto& il = lanes[1]; // INPUT 2
    const auto pan = ampsurd::Engine::panGains(gl.pan.load(std::memory_order_relaxed));
    const double vol = juce::Decibels::decibelsToGain((double) gl.gainDb.load(std::memory_order_relaxed), -100.0);
    // GUITAR PAN = where your guitar sits in the stereo image, always (also before REC, so you hear where
    // it will be). It is applied BEFORE the guitar is recorded: panned left -> the take is in the left
    // channel (stereo layering: one take left, BOUNCE, the next one right). Centre = untouched.
    {
        const double tl = pan[0], tr = pan[1];
        if (!(juce::exactlyEqual(panLiveL, tl) && juce::exactlyEqual(panLiveR, tr) && juce::exactlyEqual(tl, 1.0) && juce::exactlyEqual(tr, 1.0)))
        {
            for (int i = 0; i < n; ++i)
            {
                panLiveL += (tl - panLiveL) * 0.002;
                panLiveR += (tr - panLiveR) * 0.002;
                L[i] *= panLiveL;
                R[i] *= panLiveR;
            }
            if (std::abs(tl - panLiveL) < 1e-7) panLiveL = tl;
            if (std::abs(tr - panLiveR) < 1e-7) panLiveR = tr;
        }
    }
    // GUITAR VOL: the live guitar on the monitors while recording (incl. count-in and pause), like the take
    // it becomes; otherwise unchanged. Applied AFTER the guitar was recorded (the file keeps its level).
    const bool liveAsTrack = st == State::recording || st == State::pausedRec;
    auto monitorLive = [&](int from) {
        const double tl = liveAsTrack ? vol : 1.0, tr = tl;
        if (juce::exactlyEqual(liveL, tl) && juce::exactlyEqual(liveR, tr) && juce::exactlyEqual(tl, 1.0)) return; // untouched (bit-transparent)
        for (int i = from; i < n; ++i)
        {
            liveL += (tl - liveL) * 0.002;
            liveR += (tr - liveR) * 0.002;
            L[i] *= liveL;
            R[i] *= liveR;
        }
        if (std::abs(tl - liveL) < 1e-7) liveL = tl;
        if (std::abs(tr - liveR) < 1e-7) liveR = tr;
    };
    // INPUT 2 (a mic, a keyboard...): heard while it is armed, at INPUT 2 VOL and PAN. Recorded with its PAN
    // (like the guitar), never with VOL.
    const auto pan2 = ampsurd::Engine::panGains(il.pan.load(std::memory_order_relaxed));
    if (in2 != nullptr)
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::max(pk, std::abs(in2[i]));
        if (pk > input2Peak.load(std::memory_order_relaxed)) input2Peak.store(pk, std::memory_order_relaxed);
    }
    auto monitorInput2 = [&] {
        const bool on = in2 != nullptr && il.armed.load(std::memory_order_relaxed) && input2Listen.load(std::memory_order_relaxed);
        const double v2 = juce::Decibels::decibelsToGain((double) il.gainDb.load(std::memory_order_relaxed), -100.0);
        const double tl = on ? pan2[0] * v2 : 0.0, tr = on ? pan2[1] * v2 : 0.0;
        if (in2 == nullptr) { mon2L = mon2R = 0.0; return; }
        if (juce::exactlyEqual(tl, 0.0) && juce::exactlyEqual(tr, 0.0) && juce::exactlyEqual(mon2L, 0.0) && juce::exactlyEqual(mon2R, 0.0)) return;
        for (int i = 0; i < n; ++i)
        {
            mon2L += (tl - mon2L) * 0.002;
            mon2R += (tr - mon2R) * 0.002;
            L[i] += mon2L * (double) in2[i];
            R[i] += mon2R * (double) in2[i];
        }
        if (std::abs(tl - mon2L) < 1e-7) mon2L = tl;
        if (std::abs(tr - mon2R) < 1e-7) mon2R = tr;
    };

    // count-in (REC from STOP): only clicks; the song does not move and nothing is recorded yet
    if (running && countInLeft.load(std::memory_order_relaxed) > 0)
    {
        const juce::int64 left = countInLeft.load(std::memory_order_relaxed);
        monitorLive(0);
        monitorInput2();
        addClicks(L, R, n, (double) (countInEnd - left), 2);
        countInLeft.store(std::max<juce::int64>(0, left - n), std::memory_order_relaxed);
        limiter.process(L, R, n);
        mixClicks(L, R, n);
        return;
    }

    if (running)
    {
        const juce::int64 pos = songPos.load(std::memory_order_relaxed);

        // 1. record: GUITAR as AMPSURD outputs it, with GUITAR PAN (before the tracks are added); INPUT 2 as it
        //    comes in, with its PAN. Every pass is recorded while a take exists (so a correction can be extended
        //    to before its punch-in).
        for (int t = 0; t < kTracks; ++t)
        {
            auto& ln = lanes[(size_t) t];
            if (!ln.passActive.load(std::memory_order_acquire)) continue;
            if (ln.passStartPending.exchange(false))
            {
                passWallMs.store(juce::Time::currentTimeMillis(), std::memory_order_relaxed); // video sync
                passWallPos.store(pos, std::memory_order_relaxed);
                const juce::int64 start = pos - ln.pendingCompensation.load();
                ln.recSkip = start < 0 ? -start : 0;
                ln.passStart.store(std::max<juce::int64>(0, start));
            }
            const int skip = (int) std::min<juce::int64>(ln.recSkip, n);
            ln.recSkip -= skip;
            const int count = n - skip;
            if (count <= 0) continue;
            auto* a = recBuf.getWritePointer(0);
            auto* b = recBuf.getWritePointer(1);
            if (t == 0)
                for (int i = 0; i < count; ++i)
                {
                    a[i] = (float) L[skip + i];
                    b[i] = (float) R[skip + i];
                }
            else
                for (int i = 0; i < count; ++i)
                {
                    const double x = in2 != nullptr ? (double) in2[skip + i] : 0.0;
                    a[i] = (float) (x * pan2[0]);
                    b[i] = (float) (x * pan2[1]);
                }
            const float* ptrs[2] = { a, b };
            const juce::ScopedLock sl(writerLock);
            if (ln.writer)
            {
                // real time: the 2.7 s write-ahead buffer is always enough. Offline (faster than real
                // time: tests, bounces) wait for the disk instead of dropping audio.
                bool ok = ln.writer->write(ptrs, count);
                for (int tries = 0; !ok && offline && tries < 2000; ++tries)
                {
                    juce::Thread::sleep(1);
                    ok = ln.writer->write(ptrs, count);
                }
            }
        }

        monitorLive(0); // after the recording: the file keeps the guitar's level
        monitorInput2();

        const double tB = backingReady.load() ? juce::Decibels::decibelsToGain((double) backingGainDb.load(), -100.0) : 0.0;

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

        // 3. the takes (the composite of all clips of each track)
        for (auto& ln : lanes)
        {
            if (!(ln.takeReady.load() && ln.take)) continue;
            // while a correction is being recorded on this track, its old take is silent from the punch-in on
            const bool muted = st == State::recording && ln.rec.load(std::memory_order_relaxed)
                               && pos >= ln.muteTakeFrom.load(std::memory_order_relaxed);
            const double tT = muted ? 0.0 : juce::Decibels::decibelsToGain((double) ln.gainDb.load(), -100.0);
            const juce::int64 ts = ln.takeStart.load();
            const int off = (int) juce::jlimit<juce::int64>(0, n, ts - pos);
            if (off >= n) continue;
            readTrack(*ln.take, bufB, n - off, offline);
            const float* a = bufB.getReadPointer(0);
            const float* b = bufB.getReadPointer(1);
            float pk = 0.0f;
            for (int i = 0; i < n - off; ++i)
            {
                ln.g += (tT - ln.g) * 0.002; // the take already has its pan (recorded with it)
                L[off + i] += ln.g * a[i];
                R[off + i] += ln.g * b[i];
                pk = std::max(pk, std::max(std::abs(a[i]), std::abs(b[i])) * (float) ln.g);
            }
            if (pk > ln.peak.load(std::memory_order_relaxed)) ln.peak.store(pk, std::memory_order_relaxed);
        }
        // 4. metronome (after the guitar was recorded: heard, never in the recording)
        if (clickOn.load(std::memory_order_relaxed)) addClicks(L, R, n, (double) pos, 1);
        juce::int64 expected = pos; // only advance if the message thread did not move the position meanwhile
        songPos.compare_exchange_strong(expected, pos + n, std::memory_order_relaxed);
    }
    else if (liveAsTrack)
    {
        monitorLive(0); // REC paused: the guitar sounds as it will on the track
        monitorInput2();
    }
    else if (st == State::stopped && clickOn.load(std::memory_order_relaxed))
    {
        monitorLive(0);
        monitorInput2();
        addClicks(L, R, n, (double) freeClickPos, 0); // practising without the transport
        freeClickPos += n;
    }
    else
    {
        monitorLive(0); // glides back to unchanged after recording
        monitorInput2();
        currentBeat.store(-1, std::memory_order_relaxed);
        clickSource = -1;
    }

    // 5. the sum never clips (always in the path, so the latency is constant); the click comes on top
    limiter.process(L, R, n);
    mixClicks(L, R, n);
}

void PlayerRecorder::mixClicks(double* L, double* R, int n) noexcept
{
    const int d = (int) clickDelay.size();
    constexpr double ceiling = 0.891; // -1 dBFS, like the limiter
    for (int i = 0; i < n; ++i)
    {
        // delayed by the limiter's look-ahead: the click stays exactly in time with the music
        const double raw = clickDelay[(size_t) clickDelayPos];
        clickDelay[(size_t) clickDelayPos] = clickBlock[(size_t) i];
        clickBlock[(size_t) i] = 0.0;
        if (++clickDelayPos >= d) clickDelayPos = 0;
        if (raw == 0.0) continue;
        // loud, but never above -1 dBFS by itself (soft), and only into the headroom the music leaves
        double c = 0.7 * std::tanh(raw / 0.7);
        const double lo = -ceiling - juce::jmin(L[i], R[i]), hi = ceiling - juce::jmax(L[i], R[i]);
        c = lo > hi ? 0.0 : std::min(hi, std::max(lo, c));
        L[i] += c;
        R[i] += c;
    }
}

// ---------------------------------------------------------------------------------------------
// Metronome
// ---------------------------------------------------------------------------------------------
void PlayerRecorder::setMetronome(bool on, double bpm, int beatsPerBar, bool compound, float volumeDb, int bars)
{
    if (on && !clickOn.load()) { freeClickPos = 0; lastBeatIndex = std::numeric_limits<juce::int64>::min(); }
    clickBpm.store(juce::jlimit(20.0, 400.0, bpm));
    clickBeats.store(juce::jlimit(1, 16, beatsPerBar));
    clickCompound.store(compound);
    clickVolDb.store(volumeDb);
    countInBars.store(juce::jlimit(0, 4, bars));
    clickOn.store(on);
}

int PlayerRecorder::getCountInBeatsLeft() const
{
    const auto left = countInLeft.load();
    if (left <= 0) return 0;
    const double spb = sampleRate * 60.0 / clickBpm.load();
    return (int) std::ceil((double) left / spb - 1e-9);
}

void PlayerRecorder::addClicks(double* L, double* R, int n, double s0, int source) noexcept
{
    if (source != clickSource) { clickSource = source; lastBeatIndex = std::numeric_limits<juce::int64>::min(); }
    const double spb = sampleRate * 60.0 / clickBpm.load(std::memory_order_relaxed);
    const int beats = clickBeats.load(std::memory_order_relaxed);
    const bool compound = clickCompound.load(std::memory_order_relaxed);
    const double vol = 0.5 * juce::Decibels::decibelsToGain((double) clickVolDb.load(std::memory_order_relaxed), -100.0);
    const int clickLen = (int) (0.045 * sampleRate);
    const double decay = std::exp(-1.0 / (0.009 * sampleRate)), attack = 1.0 / std::max(1.0, 0.001 * sampleRate);
    for (int i = 0; i < n; ++i)
    {
        const double sPos = s0 + i;
        const auto beat = (juce::int64) std::floor(sPos / spb + 1e-9);
        if (beat != lastBeatIndex)
        {
            const bool onTheBeat = sPos - (double) beat * spb < 1.0 + 1e-6; // not after a jump into the middle of a beat
            lastBeatIndex = beat;
            if (!onTheBeat) continue;
            const int inBar = (int) (((beat % beats) + beats) % beats);
            const bool accent = inBar == 0 && beats > 1, second = compound && beats == 6 && inBar == 3; // 1 beat = all clicks alike
            clickFreq = accent ? 1600.0 : second ? 1250.0 : 1000.0;
            clickAmp = vol * (accent ? 1.0 : second ? 0.8 : 0.6);
            clickPos = 0;
            clickSoundNow = clickSound.load(std::memory_order_relaxed);
            // HI-HAT: the same noise every time (so EVEN clicks are identical); accent = a little longer
            hatSeed = 0x9E3779B9u;
            hatX1 = hatX2 = 0.0;
            hatDecay = std::exp(-1.0 / ((accent ? 0.035 : second ? 0.028 : 0.022) * sampleRate));
            currentBeat.store(inBar, std::memory_order_relaxed);
        }
        if (clickPos >= 0)
        {
            double v = 0.0;
            if (clickSoundNow == 1)
            {
                // white noise, twice differentiated (+12 dB/octave: bright, like a closed hi-hat), fast decay
                hatSeed ^= hatSeed << 13;
                hatSeed ^= hatSeed >> 17;
                hatSeed ^= hatSeed << 5;
                const double x = (double) (hatSeed & 0xffffff) / (double) 0x800000 - 1.0;
                const double d2 = x - 2.0 * hatX1 + hatX2;
                hatX2 = hatX1;
                hatX1 = x;
                v = 0.5 * d2 * clickAmp * std::min(1.0, clickPos * attack) * std::pow(hatDecay, (double) clickPos);
            }
            else
            {
                const double env = clickAmp * std::min(1.0, clickPos * attack) * std::pow(decay, (double) clickPos);
                v = env * std::sin(2.0 * juce::MathConstants<double>::pi * clickFreq * clickPos / sampleRate);
            }
            clickBlock[(size_t) i] += v; // mixed after the limiter (mixClicks)
            if (++clickPos >= clickLen) clickPos = -1;
        }
    }
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
    for (auto& l : lanes)
        if (l.take) seek(*l.take, std::max<juce::int64>(0, pos - l.takeStart.load()));
}

juce::int64 PlayerRecorder::compensationSamples(int track) const
{
    return (juce::int64) std::lround(getCompensationMs(track) * sampleRate / 1000.0);
}

void PlayerRecorder::startPass(int t)
{
    auto& ln = lanes[(size_t) t];
    if (ln.passActive.load()) return;
    const auto folder = getRecordingsFolder().getChildFile("Takes");
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile((t == 0 ? "Take " : "Input 2 take ") + timestamp(), ".wav");
    std::unique_ptr<juce::OutputStream> os = file.createOutputStream();
    if (!os) return;
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(32)
                                         .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (!w) return;
    ln.passThumb->reset(2, sampleRate);
    auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(w.release(), writeThread, 1 << 17);
    tw->setDataReceiver(ln.passThumb.get()); // live waveform while recording
    {
        const juce::ScopedLock sl(writerLock);
        ln.writer = std::move(tw);
    }
    ln.passFile = file;
    ln.passPunches.clear();
    ln.punchIn = -1;
    ln.pendingCompensation.store((int) compensationSamples(t));
    ln.passStart.store(std::max<juce::int64>(0, songPos.load() - ln.pendingCompensation.load()));
    ln.passStartPending.store(true);
    ln.passActive.store(true, std::memory_order_release);
}

void PlayerRecorder::startArmedPasses(bool onlyWithMaterial)
{
    for (int t = 0; t < kTracks; ++t)
        if (lanes[(size_t) t].armed.load() && (!onlyWithMaterial || hasMaterial(t)))
            startPass(t);
}

void PlayerRecorder::closePunches()
{
    if (getState() != State::recording && getState() != State::pausedRec) return;
    for (int t = 0; t < kTracks; ++t)
    {
        auto& ln = lanes[(size_t) t];
        if (!ln.rec.exchange(false)) continue;
        const juce::int64 out = songPos.load() - compensationSamples(t);
        ln.passPunches.push_back({ ln.punchIn, out });
        ln.punchIn = -1;
        ln.muteTakeFrom.store(std::numeric_limits<juce::int64>::max());
    }
}

void PlayerRecorder::endPasses(int onlyTrack)
{
    std::array<std::vector<Clip>, kTracks> added;
    bool any = false;
    for (int t = 0; t < kTracks; ++t)
    {
        if (onlyTrack >= 0 && t != onlyTrack) continue;
        auto& ln = lanes[(size_t) t];
        if (!ln.passActive.exchange(false)) continue;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
        {
            const juce::ScopedLock sl(writerLock); // short: the audio thread only waits for a pointer swap
            old = std::move(ln.writer);
        }
        old.reset(); // flushes and closes the file, outside the lock

        juce::int64 length = 0;
        if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor(ln.passFile) })
            length = r->lengthInSamples;
        const juce::int64 fs = ln.passStart.load();
        const auto minLen = (juce::int64) (0.05 * sampleRate);
        for (const auto& [pin, pout] : ln.passPunches)
        {
            Clip c { ln.passFile, fs, length, pin < 0 ? fs : pin, pout };
            c.in = juce::jlimit(fs, fs + length, c.in);
            c.out = juce::jlimit(fs, fs + length, c.out);
            if (c.out - c.in >= minLen) added[(size_t) t].push_back(c);
        }
        ln.passPunches.clear();
        if (added[(size_t) t].empty())
            ln.passFile.deleteFile(); // nothing was punched in: the pass is not needed
        else
            any = true;
    }
    if (!any) return;
    pushStep(); // tracks recorded together = one step
    for (int t = 0; t < kTracks; ++t)
        lanes[(size_t) t].clips.insert(lanes[(size_t) t].clips.end(), added[(size_t) t].begin(), added[(size_t) t].end());
    scheduleRender();
}

bool PlayerRecorder::setArmed(int t, bool on)
{
    if (!validTrack(t)) return false;
    const auto st = getState();
    if (st == State::recording || st == State::pausedRec) return false;
    auto& ln = lanes[(size_t) t];
    if (ln.armed.load() == on) return true;
    ln.armed.store(on);
    if (!on)
        endPasses(t); // corrections punched while playing are kept
    else if ((st == State::playing || st == State::pausedPlay) && hasMaterial(t))
        startPass(t); // ready for a punch-in
    return true;
}

void PlayerRecorder::play()
{
    const auto st = getState();
    if (st == State::recording || st == State::playing || busy.load()) return;
    if (st == State::pausedRec) { pause(); return; } // CONTINUE
    if (st == State::stopped) playStartPos = songPos.load();
    positionTransports(songPos.load());
    startArmedPasses(true); // a correction can be punched in at any moment
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
    countInLeft.store(0);
    closePunches();
    state.store((int) State::stopped, std::memory_order_release);
    endPasses();
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
    if (st == State::pausedPlay) endPasses(); // the pass file would no longer match the timeline
    if (st == State::stopped) playStartPos = pos;
    songPos.store(pos);
    positionTransports(pos);
    if (st == State::playing)
    {
        endPasses();
        startArmedPasses(true);
    }
}

double PlayerRecorder::getCompensationMs(int track) const
{
    // INPUT 2 does not pass through AMPSURD: its own latency is not part of it
    const int dev = deviceLatency ? deviceLatency() : 0;
    const double samples = (track == 0 ? lastAmpsurdLatency.load() : 0) + dev + limiter.getLatencySamples();
    return samples * 1000.0 / sampleRate + offsetMs.load();
}

void PlayerRecorder::record()
{
    if (busy.load()) return;
    switch (getState())
    {
        case State::recording: // second press: punch out, playback continues
            closePunches();
            state.store((int) State::playing, std::memory_order_release);
            return;
        case State::pausedRec:
            return;
        case State::playing:
            startArmedPasses(false);
            break;
        case State::pausedPlay:
            positionTransports(songPos.load());
            startArmedPasses(false);
            break;
        case State::stopped:
            playStartPos = songPos.load();
            positionTransports(songPos.load());
            startArmedPasses(false);
            if (countInBars.load() > 0) // the count-in ends exactly on the beat grid at the start position
            {
                const double spb = sampleRate * 60.0 / clickBpm.load();
                countInEnd = songPos.load();
                lastBeatIndex = std::numeric_limits<juce::int64>::min();
                const int perBar = clickBeats.load() > 1 ? clickBeats.load() : 4; // EVEN: a count-in bar = 4 clicks
                countInLeft.store((juce::int64) std::llround(spb * perBar * countInBars.load()));
            }
            break;
    }
    bool any = false;
    for (int t = 0; t < kTracks; ++t)
    {
        auto& ln = lanes[(size_t) t];
        if (!ln.armed.load() || !ln.passActive.load()) continue;
        const bool firstTake = ln.clips.empty() && !ln.takeReady.load();
        ln.punchIn = firstTake ? -1 : std::max<juce::int64>(0, songPos.load() - compensationSamples(t));
        ln.muteTakeFrom.store(songPos.load());
        ln.rec.store(true);
        any = true;
    }
    if (!any) { countInLeft.store(0); return; } // nothing armed (or no file could be written)
    state.store((int) State::recording, std::memory_order_release);
}

// ---------------------------------------------------------------------------------------------
// Clips -> composite take
// ---------------------------------------------------------------------------------------------
PlayerRecorder::Snapshot PlayerRecorder::snapshot() const
{
    Snapshot s { backingReady.load() ? backingFile : juce::File(), backingGainDb.load(), {}, takeVideo, videoFrame0Timeline, takeScreen };
    for (int t = 0; t < kTracks; ++t) s.clips[(size_t) t] = lanes[(size_t) t].clips;
    return s;
}

void PlayerRecorder::pushStep()
{
    undoSnaps.push_back(snapshot());
    if (undoSnaps.size() > 100) undoSnaps.erase(undoSnaps.begin());
    redoSnaps.clear();
    if (onNewStep) onNewStep();
}

void PlayerRecorder::restore(const Snapshot& s)
{
    stop();
    const juce::File cur = backingReady.load() ? backingFile : juce::File();
    if (s.backing != cur)
    {
        if (s.backing.existsAsFile()) loadBacking(s.backing);
        else removeBacking();
    }
    backingGainDb.store(s.backingGainDb);
    for (int t = 0; t < kTracks; ++t) lanes[(size_t) t].clips = s.clips[(size_t) t];
    takeVideo = s.video;
    videoFrame0Timeline = s.videoFrame0;
    takeScreen = s.screen;
    scheduleRender();
}

bool PlayerRecorder::nextUndoIsBounce() const
{
    if (undoSnaps.empty()) return false;
    return undoSnaps.back().backing != (backingReady.load() ? backingFile : juce::File());
}

juce::String PlayerRecorder::loadBackingAsStep(const juce::File& f)
{
    const auto before = snapshot();
    const auto err = loadBacking(f);
    if (err.isEmpty())
    {
        undoSnaps.push_back(before);
        redoSnaps.clear();
        if (onNewStep) onNewStep();
    }
    return err;
}

void PlayerRecorder::deleteBacking()
{
    if (!backingReady.load()) return;
    pushStep();
    removeBacking();
}

void PlayerRecorder::commitClips(int t, std::vector<Clip> next)
{
    pushStep();
    lanes[(size_t) t].clips = std::move(next);
    scheduleRender();
}

void PlayerRecorder::setClipEdges(int index, juce::int64 in, juce::int64 out, int t)
{
    if (!validTrack(t)) return;
    const auto& clips = lanes[(size_t) t].clips;
    if (index < 0 || index >= (int) clips.size()) return;
    auto next = clips;
    auto& c = next[(size_t) index];
    const auto minLen = (juce::int64) (0.05 * sampleRate);
    c.in = juce::jlimit(c.fileStart, c.fileEnd() - minLen, in);
    c.out = juce::jlimit(c.in + minLen, c.fileEnd(), out);
    if (c.in == clips[(size_t) index].in && c.out == clips[(size_t) index].out) return;
    commitClips(t, std::move(next));
}

void PlayerRecorder::removeClip(int index, int t)
{
    if (!validTrack(t)) return;
    const auto& clips = lanes[(size_t) t].clips;
    if (index < 0 || index >= (int) clips.size()) return;
    auto next = clips;
    next.erase(next.begin() + index);
    commitClips(t, std::move(next));
}

bool PlayerRecorder::undo()
{
    if (getState() == State::recording || getState() == State::pausedRec || undoSnaps.empty()) return false;
    redoSnaps.push_back(snapshot());
    const auto s = std::move(undoSnaps.back());
    undoSnaps.pop_back();
    restore(s);
    return true;
}

bool PlayerRecorder::redo()
{
    if (getState() == State::recording || getState() == State::pausedRec || redoSnaps.empty()) return false;
    undoSnaps.push_back(snapshot());
    const auto s = std::move(redoSnaps.back());
    redoSnaps.pop_back();
    restore(s);
    return true;
}

void PlayerRecorder::attachVideo(const juce::File& videoFile, juce::int64 firstFrameEpochMs)
{
    // the sound the performer played to at wall time W is heard (and lined up) at timeline position
    // passWallPos + (W - passWallMs - output latency) * sr; output latency ~ half the device latency + limiter
    const int dev = deviceLatency ? deviceLatency() : 0;
    const double outMs = (0.5 * dev + limiter.getLatencySamples()) * 1000.0 / sampleRate;
    videoFrame0Timeline = (double) passWallPos.load() + ((double) (firstFrameEpochMs - passWallMs.load()) - outMs) * sampleRate / 1000.0;
    takeVideo = videoFile;
    takeScreen = {}; // a new take: its screen recording (if any) is attached after this
}

void PlayerRecorder::clearTake(int track)
{
    const auto st = getState();
    if (st == State::recording || st == State::pausedRec) return;
    bool changed = false;
    for (int t = 0; t < kTracks; ++t)
        if ((track < 0 || t == track) && !lanes[(size_t) t].clips.empty()) changed = true;
    if (changed) pushStep(); // UNDO brings the track (and its video) back
    for (int t = 0; t < kTracks; ++t)
        if (track < 0 || t == track) lanes[(size_t) t].clips.clear();
    bool left = false;
    for (auto& l : lanes) left = left || !l.clips.empty();
    if (!left) { takeVideo = juce::File(); takeScreen = {}; } // the video goes with the last track
    scheduleRender();
}

void PlayerRecorder::scheduleRender()
{
    const int gen = ++renderGeneration;
    std::array<std::vector<Clip>, kTracks> lists;
    std::array<bool, kTracks> todo {};
    bool any = false;
    for (int t = 0; t < kTracks; ++t)
    {
        lists[(size_t) t] = lanes[(size_t) t].clips;
        todo[(size_t) t] = !(lists[(size_t) t] == lanes[(size_t) t].renderedClips);
        any = any || todo[(size_t) t];
    }
    if (!any) { adoptedGeneration.store(gen); renderError = {}; return; } // e.g. UNDO back to what is playing
    auto aliveFlag = alive;
    renderJobs.addJob([this, gen, lists, todo, aliveFlag] {
        std::array<juce::File, kTracks> files;
        std::array<juce::int64, kTracks> starts {};
        std::array<juce::String, kTracks> errs;
        for (int t = 0; t < kTracks; ++t)
            if (todo[(size_t) t])
                errs[(size_t) t] = aliveFlag->load() && gen == renderGeneration.load()
                                       ? renderComposite(t, lists[(size_t) t], files[(size_t) t], starts[(size_t) t]) : juce::String("skipped");
        juce::MessageManager::callAsync([this, gen, lists, todo, files, starts, errs, aliveFlag] {
            if (!aliveFlag->load()) return;
            if (gen == renderGeneration.load()) // a newer edit replaces this result
            {
                renderError = {};
                for (int t = 0; t < kTracks; ++t)
                {
                    if (!todo[(size_t) t]) continue;
                    const auto& err = errs[(size_t) t];
                    if (err.isEmpty()) adoptComposite(t, files[(size_t) t], starts[(size_t) t]);
                    else if (err == "empty") adoptComposite(t, {}, 0);
                    if (err.isEmpty() || err == "empty") lanes[(size_t) t].renderedClips = lists[(size_t) t];
                    else if (renderError.isEmpty()) renderError = err;
                }
                adoptedGeneration.store(gen);
            }
            else
                for (auto& f : files) f.deleteFile();
        });
    });
}

juce::String PlayerRecorder::renderNow()
{
    // blocking version (tests, no message loop): renders the current clips here; pending background
    // renders see a newer generation and are dropped
    ++renderGeneration;
    renderJobs.removeAllJobs(false, 10000);
    juce::String result;
    for (int t = 0; t < kTracks; ++t)
    {
        auto& l = lanes[(size_t) t];
        if (l.clips == l.renderedClips && (l.takeReady.load() || l.clips.empty())) continue;
        juce::File f;
        juce::int64 start = 0;
        const auto err = renderComposite(t, l.clips, f, start);
        if (err.isEmpty()) adoptComposite(t, f, start);
        else if (err == "empty") adoptComposite(t, {}, 0);
        if (err.isEmpty() || err == "empty") l.renderedClips = l.clips;
        else if (result.isEmpty()) result = err;
    }
    renderError = result;
    adoptedGeneration.store(renderGeneration.load());
    return result;
}

void PlayerRecorder::adoptComposite(int t, const juce::File& f, juce::int64 start)
{
    auto& l = lanes[(size_t) t];
    const auto old = l.takeFile;
    const bool oldIsComposite = old.getFileName().startsWith("Track ") || old.getFileName().startsWith("Input 2 track ");
    l.takeFile = f;
    l.takeStart.store(start);
    if (f.existsAsFile())
    {
        loadTakeForPlayback(t);
        l.takeThumb->setSource(new juce::FileInputSource(f));
    }
    else
    {
        l.takeReady.store(false);
        swapTrack(l.take, nullptr);
        l.takeLength.store(0);
        l.takeThumb->clear();
    }
    if (oldIsComposite && old != f) old.deleteFile(); // composites are rebuilt from the clips at any time
}

juce::String PlayerRecorder::renderComposite(int track, const std::vector<Clip>& list, juce::File& result, juce::int64& start)
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
    result = folder.getNonexistentChildFile((track == 0 ? "Track " : "Input 2 track ") + timestamp() + " " + juce::String(renderGeneration.load()), ".wav");
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

void PlayerRecorder::loadTakeForPlayback(int t)
{
    auto& l = lanes[(size_t) t];
    l.takeReady.store(false);
    swapTrack(l.take, nullptr);
    juce::String err;
    juce::int64 len = 0;
    auto tr = makeTrack(l.takeFile, err, len);
    if (!tr) { l.takeLength.store(0); return; }
    seek(*tr, std::max<juce::int64>(0, songPos.load() - l.takeStart.load()));
    l.takeLength.store(len);
    swapTrack(l.take, std::move(tr));
    l.takeReady.store(true);
}

void PlayerRecorder::poll()
{
    const auto st = getState();
    bool passing = false;
    for (auto& l : lanes) passing = passing || l.passActive.load();
    if (st == State::playing && !passing && songPos.load() >= (juce::int64) (getLengthSeconds() * sampleRate) + (juce::int64) (0.2 * sampleRate))
        stop(); // end of the material (playback only; while a pass is recorded you can play on)
    if (st == State::playing && passing && songPos.load() >= (juce::int64) (getLengthSeconds() * sampleRate) + (juce::int64) (2.0 * sampleRate))
        stop();
}

double PlayerRecorder::getPositionSeconds() const { return (double) songPos.load() / sampleRate; }
double PlayerRecorder::getBackingLengthSeconds() const { return backingReady.load() ? (double) backingLength.load() / sampleRate : 0.0; }
double PlayerRecorder::getTakeLengthSeconds(int t) const
{
    const auto& l = lanes[(size_t) t];
    return l.takeReady.load() ? (double) l.takeLength.load() / sampleRate : 0.0;
}
double PlayerRecorder::getLengthSeconds() const
{
    double t = 0.0;
    for (auto& l : lanes)
        if (l.takeReady.load()) t = std::max(t, (double) (l.takeStart.load() + l.takeLength.load()) / sampleRate);
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
    bool backing = false;
    juce::File backingFile;
    double backingGain = 1.0;
    struct Take { bool on = false; juce::File file; double gain = 1.0; juce::int64 start = 0; };
    std::array<Take, PlayerRecorder::kTracks> takes;
    juce::int64 start = 0, end = 0;
};

// renders [start, end) of the timeline in chunks; sink(L, R, n) gets the limited result
juce::String renderTimeline(juce::AudioFormatManager& fm, const MixSpec& m, double sr,
                            const std::function<void(const float*, const float*, int)>& sink, std::atomic<float>& progress,
                            const std::shared_ptr<std::atomic<bool>>& alive)
{
    std::unique_ptr<SessionStream> back;
    std::array<std::unique_ptr<SessionStream>, PlayerRecorder::kTracks> takes;
    if (m.backing) { back = std::make_unique<SessionStream>(fm, m.backingFile, sr); if (!back->ok()) return "The backing track cannot be read."; }
    for (size_t t = 0; t < takes.size(); ++t)
        if (m.takes[t].on)
        {
            takes[t] = std::make_unique<SessionStream>(fm, m.takes[t].file, sr);
            if (!takes[t]->ok()) return "The recording cannot be read.";
        }

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
    // the backing starts at 0 (m.start is 0 whenever there is a backing track)
    std::array<juce::int64, PlayerRecorder::kTracks> consumed {}; // samples of each take read so far
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
        for (size_t t = 0; t < takes.size(); ++t)
        {
            if (!takes[t]) continue;
            const auto& tk = m.takes[t];
            // a take that begins before `start` (the export starts at the earliest take): skip its beginning
            while (consumed[t] < pos - tk.start)
            {
                const int k = (int) std::min<juce::int64>(chunk, pos - tk.start - consumed[t]);
                takes[t]->read(tl.data(), tr.data(), k);
                consumed[t] += k;
            }
            // the take begins at its start on the timeline
            const int off = (int) juce::jlimit<juce::int64>(0, n, tk.start - pos);
            if (off < n)
            {
                takes[t]->read(tl.data(), tr.data(), n - off);
                consumed[t] += n - off;
                for (int i = 0; i < n - off; ++i)
                {
                    L[(size_t) (off + i)] += tk.gain * tl[(size_t) i];
                    R[(size_t) (off + i)] += tk.gain * tr[(size_t) i];
                }
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
    m.backingFile = backingFile;
    m.backingGain = juce::Decibels::decibelsToGain((double) backingGainDb.load(), -100.0);
    juce::int64 firstTake = std::numeric_limits<juce::int64>::max(), takeEnd = 0;
    for (int t = 0; t < kTracks; ++t)
    {
        auto& l = lanes[(size_t) t];
        auto& tk = m.takes[(size_t) t];
        tk.on = l.takeReady.load();
        tk.file = l.takeFile;
        tk.gain = juce::Decibels::decibelsToGain((double) l.gainDb.load(), -100.0);
        tk.start = l.takeStart.load();
        if (tk.on) { firstTake = std::min(firstTake, tk.start); takeEnd = std::max(takeEnd, tk.start + l.takeLength.load()); }
    }
    const bool anyTake = takeEnd > 0;
    m.start = backingReady.load() ? 0 : (anyTake ? firstTake : 0);
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
    m.backingFile = backingFile;
    m.backingGain = juce::Decibels::decibelsToGain((double) backingGainDb.load(), -100.0);
    juce::int64 firstTake = std::numeric_limits<juce::int64>::max(), takeEnd = 0;
    for (int t = 0; t < kTracks; ++t)
    {
        auto& l = lanes[(size_t) t];
        auto& tk = m.takes[(size_t) t];
        tk.on = l.takeReady.load();
        tk.file = l.takeFile;
        tk.gain = juce::Decibels::decibelsToGain((double) l.gainDb.load(), -100.0);
        tk.start = l.takeStart.load();
        if (tk.on) { firstTake = std::min(firstTake, tk.start); takeEnd = std::max(takeEnd, tk.start + l.takeLength.load()); }
    }
    const bool anyTake = takeEnd > 0;
    // the recordings alone still start at the backing's start (they line up with it in a DAW)
    m.start = backingReady.load() ? 0 : (anyTake ? firstTake : 0);
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
    pushStep(); // UNDO takes the bounce back: the backing and the recorded tracks as they were
    // the bounce becomes the backing track; the takes are now part of it
    ++renderGeneration;
    takeVideo = juce::File();
    takeScreen = {};
    for (auto& l : lanes)
    {
        l.clips.clear();
        l.renderedClips.clear();
        l.takeReady.store(false);
        swapTrack(l.take, nullptr);
        l.takeFile = juce::File();
        l.takeLength.store(0);
        l.takeThumb->clear();
    }
    adoptedGeneration.store(renderGeneration.load());
    backingGainDb.store(0.0f);
    return loadBacking(file);
}

void PlayerRecorder::bounce(std::function<void(const juce::String&)> done)
{
    if (!hasAnyTake()) { if (done) done("Record a take first."); return; }
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
    if (!hasAnyTake()) return "Record a take first.";
    const auto file = newBounceFile();
    ExportOptions o;
    o.format = 2;
    o.rate = sampleRate;
    o.withBacking = true;
    const auto err = runExport(file, o);
    return err.isEmpty() ? adoptBounce(file) : err;
}

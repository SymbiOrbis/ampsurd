#pragma once

// PlayerRecorder: backing track + guitar recording, standalone app only.
//
//   AMPSURD guitar output (L/R) ──┬──────────────► record (the take, 32-bit float WAV, session rate)
//                                 └─► + backing × vol + take × vol ─► safety limiter (stereo, -1 dBFS) ─► out
//
// Timing: the take is placed on the song timeline so that it lines up with the backing you HEARD
// while playing:  shift = AMPSURD latency + audio device input + output latency + 1 ms (the sum
// limiter's look-ahead) + the user's OFFSET (ms).
// The live guitar is always audible (also while the take plays back). BOUNCE renders backing + take
// into a new backing track, so further takes can be layered on top.
//
// Threads: process() = audio thread (no allocation; short locks shared with transport changes, as in
// JUCE's own transport/recorder classes). Everything else = message thread; exports and bounces run
// on a background thread.

#include <atomic>
#include <functional>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "ampsurd/SafetyLimiter.h"

class PlayerRecorder
{
public:
    enum class State { stopped, playing, recording, pausedPlay, pausedRec };
    struct ExportOptions
    {
        int format = 0;          // index into formatNames()
        double rate = 44100.0;   // 44100 / 48000 / 96000
        bool withBacking = true; // false: guitar only (aligned to the backing's start)
    };
    static juce::StringArray formatNames();   // WAV 16-bit (CD) ... FLAC 24-bit
    static juce::Array<double> exportRates(); // 44.1, 48, 96 kHz

    PlayerRecorder();
    ~PlayerRecorder();

    // non-RT, audio stopped
    void prepare(double sampleRate, int maxBlockSize);
    int getLimiterLatency() const { return limiter.getLatencySamples(); }

    // audio thread: L/R = AMPSURD's guitar output; adds the tracks and limits the sum (in place)
    // offline = rendering faster than real time (tests, DAW bounce): wait for disk reads instead of
    // playing silence when the read-ahead is late
    void process(double* L, double* R, int n, int ampsurdLatencySamples, bool offline = false) noexcept;

    // --- message thread ---
    juce::String loadBacking(const juce::File&); // empty = ok, otherwise an error to show
    void removeBacking();
    void play();
    void pause();   // pause / resume
    void stop();    // stop; a running recording is finished; back to the start
    void record();  // record a new take from the current position (replaces the current take)
    void toStart();
    void poll();    // ~30 Hz: stops at the end of the material

    State getState() const { return (State) state.load(); }
    double getPositionSeconds() const;
    double getLengthSeconds() const;   // backing or take end, whichever is later
    bool hasBacking() const { return backingReady.load(); }
    bool hasTake() const { return takeReady.load(); }
    juce::String getBackingName() const { return backingFile.getFileNameWithoutExtension(); }
    double getBackingLengthSeconds() const;
    double getTakeLengthSeconds() const;
    juce::File getTakeFile() const { return takeFile; }

    void setBackingGainDb(float db) { backingGainDb.store(db); }
    void setTakeGainDb(float db) { takeGainDb.store(db); }
    float getBackingGainDb() const { return backingGainDb.load(); }
    float getTakeGainDb() const { return takeGainDb.load(); }
    float getAndResetBackingPeak() { return backingPeak.exchange(0.0f); }
    float getAndResetTakePeak() { return takePeak.exchange(0.0f); }

    // device input + output latency (from the audio device, in samples)
    void setDeviceLatencyProvider(std::function<int()> f) { deviceLatency = std::move(f); }
    void setOffsetMs(float ms) { offsetMs.store(ms); }
    float getOffsetMs() const { return offsetMs.load(); }
    double getCompensationMs() const; // what the next / current take is shifted by

    void exportAudio(const juce::File& dest, ExportOptions, std::function<void(const juce::String& error)> done);
    void bounce(std::function<void(const juce::String& error)> done);
    juce::String bounceNow(); // same, blocking (tests)
    bool isBusy() const { return busy.load(); }
    float getProgress() const { return progress.load(); }

    juce::File getRecordingsFolder() const;

    // for tests: render what an export would contain, at the session rate (no file)
    juce::String renderMix(bool withBacking, juce::AudioBuffer<float>& out, int& startOffset);

private:
    void finishRecording();
    void loadTakeForPlayback();
    void positionTransports(juce::int64 pos);
    juce::String runExport(const juce::File& dest, ExportOptions o);
    juce::File newBounceFile() const;
    juce::String adoptBounce(const juce::File& f);

    juce::AudioFormatManager formats;
    juce::TimeSliceThread readThread { "AMPSURD player" }, writeThread { "AMPSURD recorder" };

    // one playable file: reader -> read-ahead buffer (background thread) -> resampler (if needed)
    struct Track
    {
        std::unique_ptr<juce::BufferingAudioSource> buffering; // owns the reader source
        std::unique_ptr<juce::ResamplingAudioSource> resampler;
        double fileRate = 48000.0, ratio = 1.0;               // file samples per session sample
        juce::AudioSource& out() { return resampler ? (juce::AudioSource&) *resampler : (juce::AudioSource&) *buffering; }
    };
    std::unique_ptr<Track> makeTrack(const juce::File&, juce::String& error, juce::int64& sessionLength);
    void seek(Track&, juce::int64 sessionPos);           // message thread, takes trackLock
    void waitReady(Track&, int timeoutMs);
    void readTrack(Track&, juce::AudioBuffer<float>&, int n, bool offline) noexcept; // audio thread, trackLock held
    void swapTrack(std::unique_ptr<Track>& slot, std::unique_ptr<Track> next);

    juce::File backingFile, takeFile;
    juce::CriticalSection trackLock;
    std::unique_ptr<Track> backing, take;
    std::atomic<bool> backingReady { false }, takeReady { false };
    std::atomic<juce::int64> backingLength { 0 }; // session samples
    std::atomic<juce::int64> takeStart { 0 }, takeLength { 0 };

    juce::CriticalSection writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;

    std::atomic<int> state { (int) State::stopped };
    std::atomic<juce::int64> songPos { 0 };
    std::atomic<bool> recordStartPending { false };
    std::atomic<int> pendingCompensation { 0 };
    juce::int64 recSkip = 0;

    std::function<int()> deviceLatency;
    std::atomic<float> offsetMs { 0.0f }, backingGainDb { 0.0f }, takeGainDb { 0.0f };
    std::atomic<float> backingPeak { 0.0f }, takePeak { 0.0f };
    double gB = 1.0, gT = 1.0;

    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::atomic<int> lastAmpsurdLatency { 0 };
    juce::AudioBuffer<float> bufA, bufB, recBuf;
    ampsurd::SafetyLimiter limiter;

    juce::ThreadPool jobs { juce::ThreadPoolOptions {}.withThreadName("AMPSURD export").withNumberOfThreads(1) };
    std::atomic<bool> busy { false };
    std::atomic<float> progress { 0.0f };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};

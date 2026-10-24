#pragma once

// PlayerRecorder: backing track + guitar recording, standalone app only.
//
//   AMPSURD guitar output (L/R) ─ GUITAR PAN ─┬──► record (the take, 32-bit float WAV, session rate)
//                                 └─► + backing × vol + take × vol ─► safety limiter (stereo, -1 dBFS) ─► out
//
// Timing: the take is placed on the song timeline so that it lines up with the backing you HEARD
// while playing:  shift = AMPSURD latency + audio device input + output latency + 1 ms (the sum
// limiter's look-ahead) + the user's OFFSET (ms).
// The live guitar is always audible (also while the take plays back). BOUNCE renders backing + take
// into a new backing track, so further takes can be layered on top.
//
// The guitar track = a list of CLIPS (recorded passes), later ones on top of earlier ones:
//   - the first REC records the take; REC while a take exists = PUNCH-IN (a correction) from the
//     current position, REC again (or STOP / PAUSE...CONTINUE keeps it running) = PUNCH-OUT;
//   - every pass is recorded from the moment PLAY starts, so a correction's start edge can later be
//     dragged earlier (to before the punch-in point) or later (to cut a quiet start);
//   - the clips are rendered into one composite take in the background with 10 ms equal-power
//     crossfades at every edge, so the joins never click. UNDO restores the previous clip list.
//
// Threads: process() = audio thread (no allocation; short locks shared with transport changes, as in
// JUCE's own transport/recorder classes). Everything else = message thread; exports and bounces run
// on a background thread.

#include <atomic>
#include <limits>
#include <vector>
#include <functional>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "ampsurd/Engine.h"
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
    void record();  // no take: record it; take: punch in (or out, while recording) at the current position
    void toStart();
    void setPositionSeconds(double s); // navigate (not while recording)
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
    double getTakeStartSeconds() const { return (double) takeStart.load() / sampleRate; }
    double getSampleRate() const { return sampleRate; }

    // --- the guitar track's clips (message thread). Positions in session samples on the timeline. ---
    struct Clip
    {
        juce::File file;             // the recorded pass (32-bit float WAV, session rate)
        juce::int64 fileStart = 0;   // timeline position of the file's first sample
        juce::int64 fileLength = 0;  // samples in the file
        juce::int64 in = 0, out = 0; // the part that is heard: [in, out)
        juce::int64 fileEnd() const { return fileStart + fileLength; }
    };
    const std::vector<Clip>& getClips() const { return clips; }
    // moves the edges of clip `index` (clamped to the recorded audio, at least 50 ms long); re-renders
    void setClipEdges(int index, juce::int64 in, juce::int64 out);
    void removeClip(int index);
    // History of the player's steps (recordings, edge moves, deleting a track, BOUNCE, loading a backing): one
    // snapshot (backing + VOL, the guitar track's clips, its video) per step. UNDO / REDO move through it.
    // Since 2026-10-24 the processor keeps ONE history for the sound and the player (Ctrl+Z / Ctrl+Y and the
    // header arrows undo whatever was done last); onNewStep tells it that the player made a step.
    bool canUndo() const { return !undoSnaps.empty(); }
    bool canRedo() const { return !redoSnaps.empty(); }
    bool nextUndoIsBounce() const; // the next UNDO brings another backing track back (takes back a BOUNCE / delete / load)
    bool undo();                   // false: nothing to undo, or recording
    bool redo();
    std::function<void()> onNewStep;
    void clearRedo() { redoSnaps.clear(); } // a newer step elsewhere (the sound) ends the redo branch
    juce::String loadBackingAsStep(const juce::File& f); // LOAD BACKING by the user: an undoable step
    void deleteBacking();                                // the X / REMOVE: an undoable step
    void clearTake();                  // remove the whole guitar track (files stay in Recordings/Takes)

    // --- VIDEO takes (standalone; camera: VideoCapture). A video take is one continuous recording from
    //     STOP; its camera file is kept with the take. Sync: the audio thread notes the wall clock when the
    //     pass starts; with the camera's first-frame time this gives the timeline position of video frame 0.
    //     VIDEO OFFSET (ms, + = the sound later) fine-tunes it by hand. ---
    void attachVideo(const juce::File& videoFile, juce::int64 firstFrameEpochMs); // message thread, after stop
    void dropVideo() { takeVideo = juce::File(); }
    bool takeHasVideo() const { return takeVideo.existsAsFile() && takeReady.load(); }
    juce::File getTakeVideo() const { return takeVideo; }
    void setVideoOffsetMs(float ms) { videoOffsetMs.store(ms); }
    float getVideoOffsetMs() const { return videoOffsetMs.load(); }
    // seconds from video frame 0 to the first sample of audio that starts at timeline position `audioStart`
    double videoAudioLeadSeconds(juce::int64 audioStart) const
    {
        return videoLeadSeconds(videoFrame0Timeline, audioStart, sampleRate, videoOffsetMs.load());
    }
    // pure function (tests): + = the audio starts after video frame 0
    static double videoLeadSeconds(double frame0TimelineSamples, juce::int64 audioStart, double sr, double offsetMs)
    {
        return ((double) audioStart - frame0TimelineSamples) / sr + offsetMs / 1000.0;
    }
    bool isRendering() const { return adoptedGeneration.load() != renderGeneration.load(); }
    juce::String getRenderError() const { return renderError; }
    juce::String renderNow();          // blocking (tests): waits for the composite
    static constexpr double kCrossfadeSeconds = 0.010;

    // live recording: where the current pass starts and where the punch-in is (timeline samples, -1 = none)
    juce::int64 getPassStart() const { return passActive.load() ? passStart.load() : -1; }
    juce::int64 getPunchIn() const { return getState() == State::recording ? punchIn : -1; }

    // waveforms (message thread)
    juce::AudioThumbnail& getBackingThumbnail() { return backingThumb; }
    juce::AudioThumbnail& getTakeThumbnail() { return takeThumb; }
    juce::AudioThumbnail& getPassThumbnail() { return passThumb; }

    void setBackingGainDb(float db) { backingGainDb.store(db); }
    void setTakeGainDb(float db) { takeGainDb.store(db); }
    // GUITAR PAN (-1 = left .. +1 = right; same law as the amps' PAN): where your guitar sits, always, and
    // it is RECORDED with it (panned left -> the take is in the left channel; for stereo layering with
    // BOUNCE). The take plays back as recorded. GUITAR VOL: the take's playback level, and the live guitar
    // on the monitors while recording; never changes the recorded file.
    void setTakePan(float p) { takePan.store(juce::jlimit(-1.0f, 1.0f, p)); }
    float getTakePan() const { return takePan.load(); }
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

    // ---- Metronome (heard, never recorded: added after the guitar is written to disk) ----
    // ON: clicks while stopped (free running), playing and recording; the grid starts at the song's
    // start (bar 1, beat 1 = 0:00). Count-in: REC from STOP first plays 1 or 2 bars of clicks.
    // Message thread (any time); the audio thread reads atomics.
    void setMetronome(bool on, double bpm, int beatsPerBar, bool compound, float volumeDb, int countInBars);
    // click sound: 0 = BEEP (short tone), 1 = HI-HAT (short bright noise: cuts through distorted guitar)
    void setClickSound(int s) { clickSound.store(juce::jlimit(0, 1, s)); }
    bool isCountingIn() const { return countInLeft.load(std::memory_order_relaxed) > 0; }
    int getCountInBeatsLeft() const;
    int getCurrentBeat() const { return currentBeat.load(std::memory_order_relaxed); } // 0-based in the bar, -1 = silent
    // Audio thread helper (public for tests): adds clicks for samples [s0, s0 + n) of the beat grid.
    void addClicks(double* L, double* R, int n, double s0, int source) noexcept; // source: 0 free, 1 song, 2 count-in

private:
    void startPass();
    void endPass();                     // closes the pass file and adds its punched parts as clips
    void closePunch();
    void commitClips(std::vector<Clip> next); // undo point + render
    void scheduleRender();
    juce::String renderComposite(const std::vector<Clip>& list, juce::File& result, juce::int64& start);
    void adoptComposite(const juce::File& f, juce::int64 start);
    juce::int64 compensationSamples() const;
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
    juce::int64 playStartPos = 0;       // STOP returns here

    // the pass being recorded
    std::atomic<bool> passActive { false }, passStartPending { false };
    std::atomic<juce::int64> passWallMs { 0 }, passWallPos { 0 }; // wall clock (epoch ms) and song position when the pass started
    juce::File takeVideo;
    double videoFrame0Timeline = 0.0;
    std::atomic<float> videoOffsetMs { 0.0f };
    std::atomic<juce::int64> passStart { 0 };       // timeline position of the pass file's first sample
    std::atomic<int> pendingCompensation { 0 };
    juce::int64 recSkip = 0;
    juce::File passFile;
    juce::int64 punchIn = -1;                        // timeline; -1 = from the pass start (first take)
    std::vector<std::pair<juce::int64, juce::int64>> passPunches;
    std::atomic<juce::int64> muteTakeFrom { std::numeric_limits<juce::int64>::max() }; // song position

    // clips + composite
    std::vector<Clip> clips;
    struct Snapshot
    {
        juce::File backing;                    // none = no backing track
        float backingGainDb = 0.0f;
        std::vector<Clip> clips;
        juce::File video;
        double videoFrame0 = 0.0;
    };
    std::vector<Snapshot> undoSnaps, redoSnaps;
    Snapshot snapshot() const;
    void restore(const Snapshot&);
    void pushStep();                           // before a change: remember the state, new branch (no redo)
    std::atomic<int> renderGeneration { 0 }, adoptedGeneration { 0 };
    juce::String renderError;
    juce::ThreadPool renderJobs { juce::ThreadPoolOptions {}.withThreadName("AMPSURD composite").withNumberOfThreads(1) };

    juce::AudioThumbnailCache thumbCache { 8 };
    juce::AudioThumbnail backingThumb { 256, formats, thumbCache }, takeThumb { 256, formats, thumbCache }, passThumb { 256, formats, thumbCache };

    std::function<int()> deviceLatency;
    std::atomic<float> offsetMs { 0.0f }, backingGainDb { 0.0f }, takeGainDb { 0.0f };
    std::atomic<float> backingPeak { 0.0f }, takePeak { 0.0f };
    double gB = 1.0, gT = 1.0;
    std::atomic<float> takePan { 0.0f };
    double liveL = 1.0, liveR = 1.0;      // smoothed monitor gains of the live guitar (recording only)
    double panLiveL = 1.0, panLiveR = 1.0; // smoothed GUITAR PAN on the live guitar (before recording)
    // metronome
    std::atomic<bool> clickOn { false }, clickCompound { false };
    std::atomic<double> clickBpm { 120.0 };
    std::atomic<int> clickBeats { 4 }, countInBars { 0 }, currentBeat { -1 };
    std::atomic<float> clickVolDb { -12.0f };
    std::atomic<juce::int64> countInLeft { 0 };
    juce::int64 countInEnd = 0, freeClickPos = 0, lastBeatIndex = std::numeric_limits<juce::int64>::min();
    int clickPos = -1;            // samples since the current click started (-1: none)
    int clickSource = -1;         // which grid the last clicks came from (a change restarts the grid)
    double clickFreq = 1000.0, clickAmp = 0.0;
    std::atomic<int> clickSound { 0 };
    int clickSoundNow = 0;
    juce::uint32 hatSeed = 1;
    double hatX1 = 0.0, hatX2 = 0.0, hatDecay = 1.0;

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

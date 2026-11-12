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
// TWO RECORDING TRACKS (owner 2026-11-12): track 0 = GUITAR (AMPSURD's output, as above), track 1 = INPUT 2
// (a second input of the audio interface - a mic, a keyboard... - recorded as it comes in, without AMPSURD).
// Each track has an ARM switch: REC records the armed tracks (GUITAR is armed by default, INPUT 2 is armed by
// hand). Both armed = recorded together; GUITAR off + INPUT 2 on = INPUT 2 is recorded to the guitar take
// (one after the other). An armed INPUT 2 is heard (monitored) at its VOL / PAN. Each track has its own clips,
// punch-ins, composite, VOL and PAN; INPUT 2 is lined up without AMPSURD's latency (it does not pass through it).
//
// Threads: process() = audio thread (no allocation; short locks shared with transport changes, as in
// JUCE's own transport/recorder classes). Everything else = message thread; exports and bounces run
// on a background thread.

#include <array>
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
#include "ScreenOverlay.h"

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
    // playing silence when the read-ahead is late. input2 = the second interface input (nullptr: none)
    void process(double* L, double* R, int n, int ampsurdLatencySamples, bool offline = false, const float* input2 = nullptr) noexcept;

    // --- the recording tracks ---
    static constexpr int kTracks = 2;   // 0 = GUITAR, 1 = INPUT 2
    static juce::String trackName(int t) { return t == 0 ? "GUITAR" : "INPUT 2"; }
    // ARM (message thread): REC records the armed tracks. Not while recording (false = not changed).
    bool setArmed(int track, bool on);
    bool isArmed(int track) const { return validTrack(track) && lanes[(size_t) track].armed.load(); }
    bool anyArmed() const { return isArmed(0) || isArmed(1); }
    bool isTrackRecording(int track) const { return validTrack(track) && lanes[(size_t) track].rec.load(); }
    // INPUT 2 exists (the app has a second interface input selected for it); without it, INPUT 2 records silence
    void setInput2Available(bool yes) { input2Available.store(yes); }
    bool isInput2Available() const { return input2Available.load(); }

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
    bool hasTake(int track = 0) const { return validTrack(track) && lanes[(size_t) track].takeReady.load(); }
    bool hasAnyTake() const { return hasTake(0) || hasTake(1); }
    bool hasMaterial(int track) const { return hasTake(track) || !getClips(track).empty(); } // a take, or one being rendered
    juce::String getBackingName() const { return backingFile.getFileNameWithoutExtension(); }
    double getBackingLengthSeconds() const;
    double getTakeLengthSeconds(int track = 0) const;
    juce::File getTakeFile(int track = 0) const { return lanes[(size_t) track].takeFile; }
    double getTakeStartSeconds(int track = 0) const { return (double) lanes[(size_t) track].takeStart.load() / sampleRate; }
    double getSampleRate() const { return sampleRate; }

    // --- the guitar track's clips (message thread). Positions in session samples on the timeline. ---
    struct Clip
    {
        bool operator==(const Clip& o) const { return file == o.file && fileStart == o.fileStart && fileLength == o.fileLength && in == o.in && out == o.out; }
        juce::File file;             // the recorded pass (32-bit float WAV, session rate)
        juce::int64 fileStart = 0;   // timeline position of the file's first sample
        juce::int64 fileLength = 0;  // samples in the file
        juce::int64 in = 0, out = 0; // the part that is heard: [in, out)
        juce::int64 fileEnd() const { return fileStart + fileLength; }
    };
    const std::vector<Clip>& getClips(int track = 0) const { return lanes[(size_t) track].clips; }
    // moves the edges of clip `index` (clamped to the recorded audio, at least 50 ms long); re-renders
    void setClipEdges(int index, juce::int64 in, juce::int64 out, int track = 0);
    void removeClip(int index, int track = 0);
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
    void clearTake(int track = -1);    // remove a whole track, -1 = both (files stay in Recordings/Takes)

    // --- VIDEO takes (standalone; camera: VideoCapture). A video take is one continuous recording from
    //     STOP; its camera file is kept with the take. Sync: the audio thread notes the wall clock when the
    //     pass starts; with the camera's first-frame time this gives the timeline position of video frame 0.
    //     VIDEO OFFSET (ms, + = the sound later) fine-tunes it by hand. ---
    void attachVideo(const juce::File& videoFile, juce::int64 firstFrameEpochMs); // message thread, after stop
    void dropVideo() { takeVideo = juce::File(); takeScreen = {}; }
    // SCREEN BOX: the take's screen recording and when / where its box is shown (times = camera video time).
    // Attached after a video take (with attachVideo); setTakeScreen after the take = the box moved (no undo step).
    void setTakeScreen(const ampsurd::screen::Overlay& o) { takeScreen = o; }
    const ampsurd::screen::Overlay& getTakeScreen() const { return takeScreen; }
    bool takeHasVideo() const { return takeVideo.existsAsFile() && hasAnyTake(); }
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
    juce::int64 getPassStart(int track = 0) const { const auto& l = lanes[(size_t) track]; return l.passActive.load() ? l.passStart.load() : -1; }
    juce::int64 getPunchIn(int track = 0) const { return getState() == State::recording && isTrackRecording(track) ? lanes[(size_t) track].punchIn : -1; }

    // waveforms (message thread)
    juce::AudioThumbnail& getBackingThumbnail() { return backingThumb; }
    juce::AudioThumbnail& getTakeThumbnail(int track = 0) { return *lanes[(size_t) track].takeThumb; }
    juce::AudioThumbnail& getPassThumbnail(int track = 0) { return *lanes[(size_t) track].passThumb; }

    void setBackingGainDb(float db) { backingGainDb.store(db); }
    void setTakeGainDb(float db, int track = 0) { lanes[(size_t) track].gainDb.store(db); }
    // GUITAR PAN (-1 = left .. +1 = right; same law as the amps' PAN): where your guitar sits, always, and
    // it is RECORDED with it (panned left -> the take is in the left channel; for stereo layering with
    // BOUNCE). The take plays back as recorded. GUITAR VOL: the take's playback level, and the live guitar
    // on the monitors while recording; never changes the recorded file.
    // INPUT 2 PAN / VOL work the same way for the second input (PAN recorded, VOL = playback and monitoring)
    void setTakePan(float p, int track = 0) { lanes[(size_t) track].pan.store(juce::jlimit(-1.0f, 1.0f, p)); }
    float getTakePan(int track = 0) const { return lanes[(size_t) track].pan.load(); }
    float getBackingGainDb() const { return backingGainDb.load(); }
    float getTakeGainDb(int track = 0) const { return lanes[(size_t) track].gainDb.load(); }
    float getAndResetBackingPeak() { return backingPeak.exchange(0.0f); }
    float getAndResetTakePeak(int track = 0) { return lanes[(size_t) track].peak.exchange(0.0f); }
    float getAndResetInput2Peak() { return input2Peak.exchange(0.0f); } // the live INPUT 2 (raw), for its meter
    // INPUT 2 LISTEN (owner 2026-11-12): off = the live INPUT 2 is not on the speakers (a microphone would feed back);
    // it is still recorded when armed. On (default) = heard while armed, as before.
    void setInput2Listen(bool on) { input2Listen.store(on); }
    bool isInput2Listen() const { return input2Listen.load(); }

    // device input + output latency (from the audio device, in samples)
    void setDeviceLatencyProvider(std::function<int()> f) { deviceLatency = std::move(f); }
    void setOffsetMs(float ms) { offsetMs.store(ms); }
    float getOffsetMs() const { return offsetMs.load(); }
    double getCompensationMs(int track = 0) const; // what the next / current take is shifted by

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
    static bool validTrack(int t) { return t >= 0 && t < kTracks; }
    void startPass(int track);
    void startArmedPasses(bool onlyWithMaterial); // a pass for every armed track (only those with a take: false)
    // closes the passes (all tracks, or one) and adds their punched parts as clips - ONE undo step
    void endPasses(int onlyTrack = -1);
    void closePunches();
    void commitClips(int track, std::vector<Clip> next); // undo point + render
    void scheduleRender();
    juce::String renderComposite(int track, const std::vector<Clip>& list, juce::File& result, juce::int64& start);
    void adoptComposite(int track, const juce::File& f, juce::int64 start);
    juce::int64 compensationSamples(int track) const;
    void loadTakeForPlayback(int track);
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

    juce::File backingFile;
    juce::CriticalSection trackLock;
    std::unique_ptr<Track> backing;
    std::atomic<bool> backingReady { false };
    std::atomic<juce::int64> backingLength { 0 }; // session samples

    juce::CriticalSection writerLock; // guards every lane's writer

    // one recording track (GUITAR / INPUT 2)
    struct Lane
    {
        // the composite take (what plays)
        juce::File takeFile;
        std::unique_ptr<Track> take;
        std::atomic<bool> takeReady { false };
        std::atomic<juce::int64> takeStart { 0 }, takeLength { 0 };
        std::vector<Clip> clips;
        std::vector<Clip> renderedClips;     // the clips the current composite was made of (unchanged = no new render)
        // the pass being recorded
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer; // writerLock
        std::atomic<bool> passActive { false }, passStartPending { false };
        std::atomic<bool> rec { false };     // punched in now (REC)
        std::atomic<bool> armed { false };
        std::atomic<juce::int64> passStart { 0 };       // timeline position of the pass file's first sample
        std::atomic<int> pendingCompensation { 0 };
        juce::int64 recSkip = 0;
        juce::File passFile;
        juce::int64 punchIn = -1;                        // timeline; -1 = from the pass start (first take)
        std::vector<std::pair<juce::int64, juce::int64>> passPunches;
        std::atomic<juce::int64> muteTakeFrom { std::numeric_limits<juce::int64>::max() }; // song position
        // levels
        std::atomic<float> gainDb { 0.0f }, pan { 0.0f }, peak { 0.0f };
        double g = 1.0;
        std::unique_ptr<juce::AudioThumbnail> takeThumb, passThumb;
    };
    std::array<Lane, kTracks> lanes;
    std::atomic<bool> input2Available { false };
    std::atomic<float> input2Peak { 0.0f };
    std::atomic<bool> input2Listen { true };
    double mon2L = 0.0, mon2R = 0.0;     // smoothed monitor gains of the live INPUT 2

    std::atomic<int> state { (int) State::stopped };
    std::atomic<juce::int64> songPos { 0 };
    juce::int64 playStartPos = 0;       // STOP returns here

    std::atomic<juce::int64> passWallMs { 0 }, passWallPos { 0 }; // wall clock (epoch ms) and song position when the pass started
    juce::File takeVideo;
    ampsurd::screen::Overlay takeScreen;
    double videoFrame0Timeline = 0.0;
    std::atomic<float> videoOffsetMs { 0.0f };

    // clips + composite
    struct Snapshot
    {
        juce::File backing;                    // none = no backing track
        float backingGainDb = 0.0f;
        std::array<std::vector<Clip>, kTracks> clips;
        juce::File video;
        double videoFrame0 = 0.0;
        ampsurd::screen::Overlay screen;
    };
    std::vector<Snapshot> undoSnaps, redoSnaps;
    Snapshot snapshot() const;
    void restore(const Snapshot&);
    void pushStep();                           // before a change: remember the state, new branch (no redo)
    std::atomic<int> renderGeneration { 0 }, adoptedGeneration { 0 };
    juce::String renderError;
    juce::ThreadPool renderJobs { juce::ThreadPoolOptions {}.withThreadName("AMPSURD composite").withNumberOfThreads(1) };

    juce::AudioThumbnailCache thumbCache { 8 };
    juce::AudioThumbnail backingThumb { 256, formats, thumbCache };

    std::function<int()> deviceLatency;
    std::atomic<float> offsetMs { 0.0f }, backingGainDb { 0.0f };
    std::atomic<float> backingPeak { 0.0f };
    double gB = 1.0;
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
    // the metronome is mixed AFTER the limiter (owner 2026-10-29: a loud click made the limiter duck the guitar):
    // clicks of a block are made here, delayed by the limiter's look-ahead (timing unchanged) and added with the
    // headroom that is left - the guitar is never turned down for a click
    std::vector<double> clickBlock, clickDelay;
    int clickDelayPos = 0;
    void mixClicks(double* L, double* R, int n) noexcept;

    juce::ThreadPool jobs { juce::ThreadPoolOptions {}.withThreadName("AMPSURD export").withNumberOfThreads(1) };
    std::atomic<bool> busy { false };
    std::atomic<float> progress { 0.0f };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};

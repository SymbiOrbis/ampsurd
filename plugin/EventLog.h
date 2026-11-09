#pragma once

// EventLog (owner 2026-11-01): what went wrong and why - dropouts with what was going on at that moment, and
// problems (files not found, captures / IRs that could not load, audio device changes, update check errors).
// Shown in the plugin (click DROPOUTS in the footer, or settings menu -> Show log...) and written to
// Documents / AMPSURD / Logs / AMPSURD_log.txt (kept below ~1 MB: the previous one becomes AMPSURD_log_old.txt).
//
// Threads: pushDrop() is lock-free and allocation-free (audio thread). Everything else: any non-audio thread.
// drain() (message thread, the processor's housekeeping timer) turns the audio thread's records into lines.

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include <juce_core/juce_core.h>

namespace ampsurd
{
class EventLog
{
public:
    EventLog();

    // --- audio thread ---
    enum class DropKind : unsigned char { slowBlock, lateCall };
    struct Drop
    {
        double atMs = 0.0;          // juce::Time::getMillisecondCounterHiRes()
        float workMs = 0.0f;        // how long AMPSURD worked on the block
        float budgetMs = 0.0f;      // how long the block lasts (the time it had)
        float gapMs = 0.0f;         // time since the previous block started (lateCall)
        float ranMs = -1.0f;        // how long the audio thread really ran in that time (-1 = not known)
        float helperWaitMs = 0.0f;  // of the block: waiting for a helper core to finish its amp
        // where the time went (ms; owner 2026-11-09): per amp {pedals, amp, tone + cabinet, EQ}, -1 = did not run
        float slotMs[5][4] = { { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f } }; // [s][0] < 0: amp s did not run
        float pathsMs = -1.0f;      // all amps together (they run side by side on several cores); -1 = not measured
        float mixMs = 0.0f;         // mixing the amps, blend tone, global EQ
        float inputMs = 0.0f;       // input, tuner, PITCH
        float effectsMs = 0.0f;     // gate, ROOT, DOUBLER, flanger, delays, reverb
        float outputMs = 0.0f;      // limiter, bypass, player
        int samples = 0;
        DropKind kind = DropKind::slowBlock;
        bool grace = false;         // right after starting audio / loading: not counted in DROPOUTS
    };
    void pushDrop(const Drop& d) noexcept;

    // --- any other thread ---
    void add(const juce::String& category, const juce::String& text);
    // something that runs in the background (loading, measuring, calculating an IR): dropouts while it runs
    // name it. end() may come from another thread.
    int beginActivity(const juce::String& what);
    void endActivity(int id);
    struct Scope
    {
        Scope(EventLog& l, const juce::String& what) : log(l), id(l.beginActivity(what)) {}
        ~Scope() { log.endActivity(id); }
        EventLog& log;
        const int id;
    };

    // --- message thread ---
    // `context(atMs)`: the processor's view at that moment (buffer, CPU, amps between GAIN steps ...)
    void drain(const std::function<juce::String(const Drop&)>& describe);
    juce::String getText() const;
    int getNumEntries() const;
    int getVersion() const { return version.load(); }
    void clear();
    juce::File getFile() const { return file; }
    // the background work that overlapped [fromMs, toMs] (names, "; "-separated); empty = none
    juce::String activitiesAround(double fromMs, double toMs);

    static juce::String clockAt(double hiResMs); // "17:42:05"

private:
    void write(const juce::String& line);

    static constexpr int kRing = 128;
    std::array<Drop, kRing> ring {};
    std::atomic<juce::uint32> writeIdx { 0 };
    juce::uint32 readIdx = 0;
    double lastListedAt = -1.0e9; // dropouts within 1 s of a listed one are counted, then summed up in one line
    int notListed = 0;
    double notListedWorst = 0.0;

    mutable std::mutex mutex;
    juce::StringArray lines;
    struct Activity { int id; juce::String what; double start, end; };
    std::vector<Activity> activities;
    int nextId = 1;
    std::atomic<int> version { 0 };
    juce::File file;
};
} // namespace ampsurd

#include "EventLog.h"

namespace ampsurd
{
EventLog::EventLog()
{
    file = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("AMPSURD").getChildFile("Logs")
               .getChildFile("AMPSURD_log.txt");
    file.getParentDirectory().createDirectory();
    if (file.getSize() > 1024 * 1024)
    {
        const auto old = file.getSiblingFile("AMPSURD_log_old.txt");
        old.deleteFile();
        file.moveFileTo(old);
    }
}

juce::String EventLog::clockAt(double hiResMs)
{
    const double ago = juce::Time::getMillisecondCounterHiRes() - hiResMs;
    const auto t = juce::Time::getCurrentTime() - juce::RelativeTime::milliseconds((juce::int64) juce::jmax(0.0, ago));
    return t.formatted("%H:%M:%S");
}

void EventLog::pushDrop(const Drop& d) noexcept
{
    const auto w = writeIdx.load(std::memory_order_relaxed);
    ring[(size_t) (w % kRing)] = d;
    writeIdx.store(w + 1, std::memory_order_release);
}

void EventLog::write(const juce::String& line)
{
    // (mutex held)
    lines.add(line);
    if (lines.size() > 2000) lines.removeRange(0, lines.size() - 2000);
    file.appendText(line + juce::newLine, false, false, nullptr);
    ++version;
}

void EventLog::add(const juce::String& category, const juce::String& text)
{
    const auto line = juce::Time::getCurrentTime().formatted("%H:%M:%S") + "  " + category.paddedRight(' ', 9) + "  "
                    + text.replace("\n", " - ");
    std::lock_guard<std::mutex> l(mutex);
    write(line);
}

int EventLog::beginActivity(const juce::String& what)
{
    std::lock_guard<std::mutex> l(mutex);
    const double now = juce::Time::getMillisecondCounterHiRes();
    // forget work that ended long ago
    activities.erase(std::remove_if(activities.begin(), activities.end(),
                                    [now](const Activity& a) { return a.end > 0.0 && now - a.end > 30000.0; }),
                     activities.end());
    const int id = nextId++;
    activities.push_back({ id, what, now, 0.0 });
    return id;
}

void EventLog::endActivity(int id)
{
    std::lock_guard<std::mutex> l(mutex);
    for (auto& a : activities)
        if (a.id == id) a.end = juce::Time::getMillisecondCounterHiRes();
}

juce::String EventLog::activitiesAround(double fromMs, double toMs)
{
    std::lock_guard<std::mutex> l(mutex);
    juce::StringArray names;
    for (const auto& a : activities)
        if (a.start <= toMs + 20.0 && (a.end <= 0.0 || a.end >= fromMs - 20.0)) names.addIfNotAlreadyThere(a.what);
    return names.joinIntoString("; ");
}

void EventLog::drain(const std::function<juce::String(const Drop&)>& describe)
{
    const auto w = writeIdx.load(std::memory_order_acquire);
    if (w - readIdx > (juce::uint32) kRing)
    {
        add("DROPOUT", juce::String((int) (w - readIdx - kRing)) + " more dropouts came too fast to be listed");
        readIdx = w - kRing;
    }
    auto flushNotListed = [this] {
        if (notListed == 0) return;
        add("DROPOUT", juce::String(notListed) + " more dropout(s) in the second after that (longest block "
                           + juce::String(notListedWorst, 1) + " ms)");
        notListed = 0;
        notListedWorst = 0.0;
    };
    for (; readIdx != w; ++readIdx)
    {
        const auto d = ring[(size_t) (readIdx % kRing)];
        if (d.atMs - lastListedAt < 1000.0)
        {
            ++notListed;
            notListedWorst = juce::jmax(notListedWorst, (double) d.workMs);
            continue;
        }
        flushNotListed();
        lastListedAt = d.atMs;
        const auto text = describe(d);
        std::lock_guard<std::mutex> l(mutex);
        write(clockAt(d.atMs) + "  " + juce::String("DROPOUT").paddedRight(' ', 9) + "  " + text);
    }
    if (juce::Time::getMillisecondCounterHiRes() - lastListedAt > 1000.0) flushNotListed();
}

juce::String EventLog::getText() const
{
    std::lock_guard<std::mutex> l(mutex);
    return lines.joinIntoString("\n");
}

int EventLog::getNumEntries() const
{
    std::lock_guard<std::mutex> l(mutex);
    return lines.size();
}

void EventLog::clear()
{
    std::lock_guard<std::mutex> l(mutex);
    lines.clear();
    notListed = 0; // (message thread, like drain)
    notListedWorst = 0.0;
    ++version;
}
} // namespace ampsurd

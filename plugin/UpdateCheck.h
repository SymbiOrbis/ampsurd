#pragma once

// "Check for updates": asks GitHub for the latest AMPSURD release - only when the user clicks it
// (nothing is sent automatically; see PRIVACY.md). Message thread in, message thread out.

#include <functional>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace ampsurd
{
struct UpdateInfo
{
    bool ok = false;          // GitHub answered
    juce::String latest;      // e.g. "0.9.1"
    juce::String pageUrl;     // the release page (download)
    juce::String error;
};

// true if `latest` (e.g. "v0.10.0" or "0.10.0") is a higher version than `current` ("0.9.0")
inline bool isNewerVersion(juce::String latest, juce::String current)
{
    latest = latest.trimCharactersAtStart("vV").trim();
    current = current.trimCharactersAtStart("vV").trim();
    juce::StringArray a, b;
    a.addTokens(latest.upToFirstOccurrenceOf("-", false, false), ".", "");
    b.addTokens(current.upToFirstOccurrenceOf("-", false, false), ".", "");
    for (int i = 0; i < juce::jmax(a.size(), b.size()); ++i)
    {
        const int x = i < a.size() ? a[i].getIntValue() : 0, y = i < b.size() ? b[i].getIntValue() : 0;
        if (x != y) return x > y;
    }
    return false;
}

// runs the request on a background thread, calls `done` on the message thread
inline void checkForUpdates(std::function<void(const UpdateInfo&)> done)
{
    juce::Thread::launch([done] {
        UpdateInfo info;
        const juce::URL url("https://api.github.com/repos/SymbiOrbis/ampsurd/releases?per_page=10");
        int status = 0;
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withExtraHeaders("User-Agent: AMPSURD\r\nAccept: application/vnd.github+json")
                        .withConnectionTimeoutMs(8000)
                        .withStatusCode(&status);
        if (auto in = url.createInputStream(opts); in != nullptr && status == 200)
        {
            const auto json = juce::JSON::parse(in->readEntireStreamAsString());
            if (auto* list = json.getArray())
            {
                // the highest version among the published releases (pre-releases included: AMPSURD is in beta)
                for (const auto& r : *list)
                {
                    if ((bool) r.getProperty("draft", false)) continue;
                    const auto tag = r.getProperty("tag_name", {}).toString();
                    if (info.latest.isEmpty() || isNewerVersion(tag, info.latest))
                    {
                        info.latest = tag.trimCharactersAtStart("vV");
                        info.pageUrl = r.getProperty("html_url", {}).toString();
                    }
                }
                info.ok = true;
            }
            else
                info.error = "Unexpected answer from GitHub.";
        }
        else
            info.error = status != 0 ? "GitHub answered with status " + juce::String(status) + "." : "Could not reach GitHub (no internet?).";
        juce::MessageManager::callAsync([done, info] { done(info); });
    });
}
} // namespace ampsurd

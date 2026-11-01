#pragma once

// The log (owner 2026-11-01): dropouts with what was going on at that moment, and problems. Opens from DROPOUTS in
// the footer and from the settings menu (Show log...). An overlay inside the plugin window, like the IR map.

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

namespace ampsurd::ui
{
class LogPanel final : public juce::Component, private juce::Timer
{
public:
    explicit LogPanel(AmpsurdProcessor&);
    void open();
    void close();
    std::function<void()> onClose;

    void paint(juce::Graphics&) override;
    void resized() override;
    juce::String getShownText() const { return view.getText(); } // tests

private:
    void timerCallback() override;
    void refresh();

    AmpsurdProcessor& proc;
    juce::TextEditor view;
    juce::TextButton closeButton { "CLOSE" }, copyButton { "COPY" }, fileButton { "SHOW FILE" }, clearButton { "CLEAR" };
    int shownVersion = -1;
};
} // namespace ampsurd::ui

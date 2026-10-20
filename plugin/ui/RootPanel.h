#pragma once

// ROOT panel (EXPERIMENTAL module, docs/ROOT_MODULE.md): replaces the gate + tuner view while open.
// Left: ON, mode (PARALLEL / POST / SUB), polarity, what the mode does. Middle: ROOT, FREQ, GROWL,
// TIGHT, COMP. Right: the layer's curve (filters + BASS EQ), layer meter, BASS EQ (50 / 100 / 250 Hz).
// Compiled only with AMPSURD_ROOT_MODULE (AMPSURD_ROOT=1).

#include <array>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

#if AMPSURD_ROOT
namespace ampsurd::ui
{
class RootPanel final : public juce::Component
{
public:
    explicit RootPanel(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onClose;

private:
    struct Row
    {
        juce::String label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    Row makeRow(const juce::String& label, const juce::String& paramId, const juce::String& tip);
    juce::Rectangle<int> graphArea() const;

    AmpsurdProcessor& proc;
    juce::TextButton onButton { "ON" }, invertButton { "POLARITY" }, closeButton { "CLOSE" };
    std::array<juce::TextButton, 3> modeButtons { juce::TextButton { "PARALLEL" }, juce::TextButton { "POST" },
                                                  juce::TextButton { "SUB" } };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt, invertAtt;
    std::array<Row, 5> rows;   // ROOT, FREQ, GROWL, TIGHT, COMP
    std::array<Row, 3> eqRows; // SUB 50, LOW 100, LOW-MID 250
    float meter = 0.0f, grDb = 0.0f;
    int shownMode = -1;
    ampsurd::RootSettings shownSettings;
};
} // namespace ampsurd::ui
#endif

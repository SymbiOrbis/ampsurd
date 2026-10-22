#pragma once

// DOUBLER panel (EXPERIMENTAL module, docs/DOUBLER.md), shown in the centre area while open.
// Left: ON, number of doubles (1 / 2 / 3), what it does. Middle: TIGHT, LEVEL, WIDTH, LOW MONO, CHARACTER.
// Right: the stereo picture - where the guitar and every double sit and how late each double plays now.
// Compiled only with AMPSURD_DOUBLER_MODULE (AMPSURD_DOUBLER=1).

#include <array>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

#if AMPSURD_DOUBLER
namespace ampsurd::ui
{
class DoublerPanel final : public juce::Component
{
public:
    explicit DoublerPanel(AmpsurdProcessor&);
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
    juce::Rectangle<int> pictureArea() const;

    AmpsurdProcessor& proc;
    juce::TextButton onButton { "ON" }, closeButton { "CLOSE" };
    std::array<juce::TextButton, 3> voiceButtons { juce::TextButton { "1" }, juce::TextButton { "2" }, juce::TextButton { "3" } };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    std::array<Row, 5> rows; // TIGHT, LEVEL, WIDTH, LOW MONO, CHARACTER
    std::array<float, 3> shownDelay {};
    int shownVoices = -1;
    bool shownOn = false;
};
} // namespace ampsurd::ui
#endif

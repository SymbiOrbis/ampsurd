#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

// Milestone 1 development UI: intentionally plain. The real adaptive amp-deck UI is Milestone 5.
class MonstrosityEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MonstrosityEditor(MonstrosityProcessor&);
    ~MonstrosityEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFile();

    MonstrosityProcessor& processor;

    juce::TextButton loadButton { "LOAD .nam" }, unloadButton { "REMOVE" };
    juce::Label infoLabel, cpuLabel;
    juce::Slider inputSlider, outputSlider;
    juce::Label inputLabel { {}, "INPUT" }, outputLabel { {}, "OUTPUT" };
    juce::ToggleButton normaliseButton { "Normalise loudness" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> normaliseAttachment;

    std::unique_ptr<juce::FileChooser> chooser;
    float inMeter = 0.0f, outMeter = 0.0f;
    int clipHold = 0;
    bool statusIsError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonstrosityEditor)
};

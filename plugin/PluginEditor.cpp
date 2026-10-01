#include "PluginEditor.h"

namespace
{
const juce::Colour kBg { 0xff141414 }, kPanel { 0xff222222 }, kAccent { 0xffc8102e }, kText { 0xffe8e8e8 };
}

MonstrosityEditor::MonstrosityEditor(MonstrosityProcessor& p) : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(loadButton);
    addAndMakeVisible(unloadButton);
    loadButton.onClick = [this] { chooseFile(); };
    unloadButton.onClick = [this] { processor.unloadCapture(); };

    infoLabel.setJustificationType(juce::Justification::topLeft);
    infoLabel.setFont(juce::FontOptions(14.0f));
    infoLabel.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(infoLabel);

    cpuLabel.setJustificationType(juce::Justification::centredRight);
    cpuLabel.setColour(juce::Label::textColourId, kText.withAlpha(0.6f));
    addAndMakeVisible(cpuLabel);

    for (auto* s : { &inputSlider, &outputSlider })
    {
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        s->setColour(juce::Slider::rotarySliderFillColourId, kAccent);
        addAndMakeVisible(*s);
    }
    for (auto* l : { &inputLabel, &outputLabel })
    {
        l->setJustificationType(juce::Justification::centred);
        l->setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(*l);
    }
    addAndMakeVisible(normaliseButton);

    inputAttachment = std::make_unique<SliderAttachment>(processor.params, "input", inputSlider);
    outputAttachment = std::make_unique<SliderAttachment>(processor.params, "output", outputSlider);
    normaliseAttachment = std::make_unique<ButtonAttachment>(processor.params, "normalise", normaliseButton);

    setSize(620, 340);
    startTimerHz(30);
    timerCallback();
}

MonstrosityEditor::~MonstrosityEditor() { stopTimer(); }

void MonstrosityEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser>("Choose a NAM capture", juce::File(), "*.nam");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             const auto f = fc.getResult();
                             if (f.existsAsFile())
                                 processor.loadCapture(f);
                         });
}

void MonstrosityEditor::timerCallback()
{
    const auto st = processor.getStatus();
    if (infoLabel.getText() != st.text)
        infoLabel.setText(st.text, juce::dontSendNotification);
    statusIsError = st.error;
    infoLabel.setColour(juce::Label::textColourId, st.error ? juce::Colours::orange : kText);

    cpuLabel.setText("CPU " + juce::String(processor.getCpuLoadPercent(), 1) + "%", juce::dontSendNotification);

    inMeter = juce::jmax(processor.getAndResetInputPeak(), inMeter * 0.85f);
    outMeter = juce::jmax(processor.getAndResetOutputPeak(), outMeter * 0.85f);
    if (processor.getAndResetClip())
        clipHold = 45; // ~1.5 s
    else if (clipHold > 0)
        --clipHold;
    repaint();
}

void MonstrosityEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    auto top = getLocalBounds().removeFromTop(40);
    g.setColour(kAccent);
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.drawText("MONSTROSITY", top.reduced(14, 0), juce::Justification::centredLeft);
    g.setColour(kText.withAlpha(0.5f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("milestone 1 - dev build", top.reduced(14, 0).withTrimmedLeft(200), juce::Justification::centredLeft);

    g.setColour(kPanel);
    g.fillRoundedRectangle(juce::Rectangle<float>(14.0f, 48.0f, (float) getWidth() - 28.0f, 150.0f), 6.0f);

    // Meters (dBFS, -60..+6)
    auto drawMeter = [&](juce::Rectangle<int> r, float peak, const juce::String& label) {
        g.setColour(kPanel);
        g.fillRect(r);
        const float db = juce::Decibels::gainToDecibels(peak, -60.0f);
        const float prop = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
        g.setColour(db > 0.0f ? juce::Colours::red : (db > -6.0f ? juce::Colours::orange : juce::Colours::limegreen));
        g.fillRect(r.withTop(r.getBottom() - (int) (prop * (float) r.getHeight())));
        g.setColour(kText.withAlpha(0.6f));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(label, r.withY(r.getBottom() + 2).withHeight(12), juce::Justification::centred);
    };
    drawMeter({ getWidth() - 70, 214, 14, 90 }, inMeter, "IN");
    drawMeter({ getWidth() - 40, 214, 14, 90 }, outMeter, "OUT");

    g.setColour(clipHold > 0 ? juce::Colours::red : juce::Colours::darkred.withAlpha(0.4f));
    g.fillEllipse((float) getWidth() - 66.0f, 200.0f, 36.0f, 8.0f);
}

void MonstrosityEditor::resized()
{
    auto header = getLocalBounds().removeFromTop(40).reduced(14, 6);
    cpuLabel.setBounds(header.removeFromRight(110));

    infoLabel.setBounds(24, 56, getWidth() - 48, 134);

    auto row = juce::Rectangle<int>(14, 210, getWidth() - 110, 120);
    auto buttons = row.removeFromLeft(140);
    loadButton.setBounds(buttons.removeFromTop(36).reduced(0, 2));
    buttons.removeFromTop(6);
    unloadButton.setBounds(buttons.removeFromTop(30).reduced(0, 2));
    buttons.removeFromTop(6);
    normaliseButton.setBounds(buttons.removeFromTop(28));

    row.removeFromLeft(20);
    auto knobIn = row.removeFromLeft(110);
    inputLabel.setBounds(knobIn.removeFromTop(18));
    inputSlider.setBounds(knobIn);
    row.removeFromLeft(10);
    auto knobOut = row.removeFromLeft(110);
    outputLabel.setBounds(knobOut.removeFromTop(18));
    outputSlider.setBounds(knobOut);
}

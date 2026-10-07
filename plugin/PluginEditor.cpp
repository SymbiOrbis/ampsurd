#include "PluginEditor.h"

using namespace ampsurd::ui;

AmpsurdEditor::Content::Content(AmpsurdProcessor& p) : header(p), edit(p), centre(p), frankenstein(p), master(p)
{
    addAndMakeVisible(header);
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
    {
        slots[(size_t) s] = std::make_unique<SlotComponent>(p, s);
        addAndMakeVisible(*slots[(size_t) s]);
    }
    addChildComponent(edit);   // EDIT replaces the gate + tuner in the centre area
    addAndMakeVisible(centre);
    addChildComponent(frankenstein); // replaces the gate + tuner while Create Frankenstein is on
    addAndMakeVisible(master);
    addAndMakeVisible(footer);
}

void AmpsurdEditor::Content::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
}

void AmpsurdEditor::Content::resized()
{
    auto r = getLocalBounds();
    header.setBounds(r.removeFromTop(56));
    footer.setBounds(r.removeFromBottom(40));
    r = r.reduced(24, 0);
    r.removeFromTop(8);
    auto slotRow = r.removeFromTop(340);
    const int gap = 12;
    const int w = (slotRow.getWidth() - gap * (AmpsurdProcessor::kNumSlots - 1)) / AmpsurdProcessor::kNumSlots;
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
    {
        slots[(size_t) s]->setBounds(slotRow.removeFromLeft(w));
        slotRow.removeFromLeft(gap);
    }
    r.removeFromTop(16);
    r.removeFromBottom(12);
    master.setBounds(r.removeFromBottom(52));
    r.removeFromBottom(14);
    edit.setBounds(r);
    centre.setBounds(r);
    frankenstein.setBounds(r);
}

AmpsurdEditor::AmpsurdEditor(AmpsurdProcessor& p) : AudioProcessorEditor(&p), proc(p), content(p)
{
    setLookAndFeel(&lnf);
    content.setLookAndFeel(&lnf);
    addAndMakeVisible(content);
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
        content.slots[(size_t) s]->onEditClicked = [this](int slot) { selectSlot(selected == slot ? -1 : slot); };
    content.edit.onClose = [this] { selectSlot(-1); };

    setResizable(true, true);
    setResizeLimits(kWidth * 2 / 3, kHeight * 2 / 3, kWidth * 2, kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double) kWidth / (double) kHeight);
    setSize(kWidth, kHeight);

    timerCallback();
    startTimerHz(30);
}

AmpsurdEditor::~AmpsurdEditor()
{
    stopTimer();
    proc.setTunerVisible(false);
    content.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void AmpsurdEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
}

void AmpsurdEditor::resized()
{
    content.setBounds(0, 0, kWidth, kHeight);
    const float scale = (float) getWidth() / (float) kWidth;
    content.setTransform(juce::AffineTransform::scale(scale));
}

void AmpsurdEditor::selectSlot(int slot)
{
    selected = slot;
    content.edit.setSlot(slot);
    timerCallback();
}

void AmpsurdEditor::timerCallback()
{
    if (selected >= 0 && content.edit.getSlot() < 0)
        selected = -1; // the edited capture was removed
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
        content.slots[(size_t) s]->refresh(s == selected);
    content.edit.refresh();
    const bool editing = content.edit.getSlot() >= 0;
    const bool frank = !editing && proc.isFrankensteinOn();
    content.edit.setVisible(editing);
    content.frankenstein.setVisible(frank);
    content.centre.setVisible(!editing && !frank);
    proc.setTunerVisible(!editing && !frank);
    if (frank)
        content.frankenstein.refresh();
    else if (!editing)
        content.centre.refresh();
    content.footer.setCpuText("CPU " + juce::String(juce::roundToInt(proc.getCpuLoadPercent())) + "%");
    content.master.refresh();
    content.header.refresh();
}

#include "PluginEditor.h"

using namespace ampsurd::ui;

AmpsurdEditor::Content::Content(AmpsurdProcessor& p) : header(p), edit(p), ir(p), centre(p), frankenstein(p), globalEq(p), globalEqButton(p), master(p)
{
    addAndMakeVisible(header);
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
    {
        slots[(size_t) s] = std::make_unique<SlotComponent>(p, s);
        addAndMakeVisible(*slots[(size_t) s]);
    }
    addChildComponent(edit);   // EDIT replaces the gate + tuner in the centre area
    addChildComponent(ir);     // so does the IR editor
    addAndMakeVisible(centre);
    addChildComponent(frankenstein); // replaces the gate + tuner while Create Frankenstein is on
    addChildComponent(globalEq);     // replaces them while the Global EQ is open
    addAndMakeVisible(globalEqButton);
    addAndMakeVisible(master);
    addAndMakeVisible(footer);
    if (auto* pl = p.getPlayer())
    {
        playerPanel = std::make_unique<PlayerPanel>(p, *pl);
        addChildComponent(*playerPanel);
        addAndMakeVisible(playerButton);
        playerButton.setTooltip("Backing track and recorder");
    }
}

void AmpsurdEditor::Content::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
}

void AmpsurdEditor::Content::resized()
{
    auto r = getLocalBounds();
    header.setBounds(r.removeFromTop(56));
    playerButton.setBounds(210, 13, 150, 30);
    footer.setBounds(r.removeFromBottom(40));
    r = r.reduced(24, 0);
    r.removeFromTop(8);
    auto slotRow = r.removeFromTop(300);
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
    ir.setBounds(r);
    if (playerPanel) playerPanel->setBounds(r);
    globalEq.setBounds(r);
    auto withButton = r;
    globalEqButton.setBounds(withButton.removeFromRight(40));
    withButton.removeFromRight(10);
    centre.setBounds(withButton);
    frankenstein.setBounds(withButton);
}

AmpsurdEditor::AmpsurdEditor(AmpsurdProcessor& p) : AudioProcessorEditor(&p), proc(p), content(p)
{
    setLookAndFeel(&lnf);
    content.setLookAndFeel(&lnf);
    addAndMakeVisible(content);
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
    {
        content.slots[(size_t) s]->onEditClicked = [this](int slot) {
            selectSlot(selected == slot && content.ir.getSlot() < 0 ? -1 : slot);
        };
        content.slots[(size_t) s]->onIrClicked = [this](int slot) { showIr(content.ir.getSlot() == slot ? -1 : slot); };
    }
    content.edit.onClose = [this] { selectSlot(-1); };
    content.ir.onClose = [this] { showIr(-1); };
    if (content.playerPanel)
    {
        content.playerPanel->onClose = [this] { showPlayer(false); };
        content.playerButton.onClick = [this] { showPlayer(!playerOpen); };
    }
    content.globalEq.onClose = [this] { showGlobalEq(false); };
    content.globalEqButton.onClick = [this] { showGlobalEq(true); };

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

// The lower panel shows one view at a time: amp EDIT, IR editor, Global EQ, or (default) the gate +
// tuner / Frankenstein. Opening one closes the others.
void AmpsurdEditor::showGlobalEq(bool show)
{
    globalEqOpen = show;
    if (show)
    {
        playerOpen = false;
        selected = -1;
        content.edit.setSlot(-1);
        content.ir.setSlot(-1);
    }
    timerCallback();
}

void AmpsurdEditor::selectSlot(int slot)
{
    selected = slot;
    if (slot >= 0)
    {
        globalEqOpen = false;
        playerOpen = false;
        content.ir.setSlot(-1);
    }
    content.edit.setSlot(slot);
    timerCallback();
}

void AmpsurdEditor::showPlayer(bool show)
{
    if (!content.playerPanel) return;
    playerOpen = show;
    if (show)
    {
        selected = -1;
        globalEqOpen = false;
        content.edit.setSlot(-1);
        content.ir.setSlot(-1);
    }
    timerCallback();
}

void AmpsurdEditor::showIr(int slot)
{
    content.edit.setSlot(-1);
    globalEqOpen = false;
    if (slot >= 0) playerOpen = false;
    selected = slot;
    content.ir.setSlot(slot);
    timerCallback();
}

void AmpsurdEditor::timerCallback()
{
    if (content.ir.getSlot() >= 0 && proc.getSlotStatus(content.ir.getSlot()).state == AmpsurdProcessor::SlotState::empty)
        content.ir.setSlot(-1); // the slot was emptied
    if (selected >= 0 && content.edit.getSlot() < 0 && content.ir.getSlot() < 0)
        selected = -1; // the edited capture was removed
    const bool irOpen = content.ir.getSlot() >= 0;
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
        content.slots[(size_t) s]->refresh(s == selected, irOpen);
    content.edit.refresh();
    content.ir.setVisible(irOpen);
    if (irOpen) content.ir.refresh();
    const bool editing = content.edit.getSlot() >= 0 || irOpen;
    const bool playerView = !editing && playerOpen && content.playerPanel != nullptr;
    const bool global = !editing && !playerView && globalEqOpen;
    const bool frank = !editing && !global && !playerView && proc.isFrankensteinOn();
    const bool centre = !editing && !global && !playerView && !frank;
    if (content.playerPanel)
    {
        content.playerPanel->setVisible(playerView);
        if (playerView) content.playerPanel->refresh();
        else if (auto* pl = proc.getPlayer()) pl->poll();
        // the button shows a recording even while the panel is closed (blinks)
        auto* pl = proc.getPlayer();
        const bool rec = pl->getState() == PlayerRecorder::State::recording;
        const bool blink = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
        const juce::String t = rec ? juce::String::charToString((juce::juce_wchar) 0x25CF) + " REC  " + juce::String((int) pl->getPositionSeconds() / 60) + ":"
                                         + juce::String((int) pl->getPositionSeconds() % 60).paddedLeft('0', 2)
                                   : juce::String("PLAYER / REC");
        if (content.playerButton.getButtonText() != t) content.playerButton.setButtonText(t);
        const bool lit = (rec && blink) || pl->getState() == PlayerRecorder::State::playing;
        if ((bool) content.playerButton.getProperties().getWithDefault("lit", false) != lit)
        {
            content.playerButton.getProperties().set("lit", lit);
            content.playerButton.repaint();
        }
        content.playerButton.setToggleState(playerView, juce::dontSendNotification);
    }
    content.edit.setVisible(content.edit.getSlot() >= 0);
    content.globalEq.setVisible(global);
    content.frankenstein.setVisible(frank);
    content.centre.setVisible(centre);
    content.globalEqButton.setVisible(frank || centre); // with the gate + tuner (and Frankenstein) view
    proc.setTunerVisible(centre);
    if (global)
        content.globalEq.refresh();
    else if (frank)
        content.frankenstein.refresh();
    else if (centre)
        content.centre.refresh();
    content.globalEqButton.refresh();
    const double cpu = proc.getCpuLoadPercent();
    cpuAverage = cpuAverage < 0.0 ? cpu : cpuAverage + (cpu - cpuAverage) * 0.05; // ~0.7 s average at 30 Hz
    const auto now = juce::Time::getMillisecondCounter();
    if (now - cpuShownAt >= 1000)
    {
        cpuShownAt = now;
        const int drops = proc.getDropoutCount();
        content.footer.setCpuText((drops > 0 ? "DROPOUTS " + juce::String(drops) + "   " : juce::String())
                                  + "CPU " + juce::String(juce::roundToInt(cpuAverage)) + "%");
    }
    content.master.refresh();
    content.header.refresh();
}

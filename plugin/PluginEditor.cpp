#include "PluginEditor.h"

using namespace ampsurd::ui;

AmpsurdEditor::Content::Content(AmpsurdProcessor& p) : header(p), edit(p), ir(p), frankenstein(p), globalEq(p), globalEqButton(p),
#if AMPSURD_ROOT
      root(p),
#endif
#if AMPSURD_DOUBLER
      doubler(p),
#endif
      master(p)
{
    addAndMakeVisible(header);
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
    {
        slots[(size_t) s] = std::make_unique<SlotComponent>(p, s);
        addAndMakeVisible(*slots[(size_t) s]);
    }
    addChildComponent(edit);   // EDIT replaces GLOBAL (the default view) in the centre area
    addChildComponent(ir);     // so does the IR editor
    addChildComponent(frankenstein); // replaces GLOBAL while Frankenstein is on
   #if AMPSURD_ROOT
    addChildComponent(root);         // ROOT panel (experimental), opened from the master row
   #endif
   #if AMPSURD_DOUBLER
    addChildComponent(doubler);      // DOUBLER panel (experimental), opened from the master row
   #endif
    addAndMakeVisible(globalEq);     // GLOBAL: tuner, Global EQ, effects - the default view
    addChildComponent(globalEqButton); // in the Frankenstein view: opens GLOBAL
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
    // a selected slot's fill (here, so that the dragon is drawn on top of it)
    for (auto& sl : slots)
        if (sl->isSelected())
        {
            g.setColour(colours::raised);
            g.fillRoundedRectangle(sl->getBounds().toFloat().reduced(1.25f), 3.0f);
        }
    if (!showDragon) return;
    dragon.draw(g, getLocalBounds().toFloat());
    dragon.drawEyes(g, getLocalBounds().toFloat()); // under the slot texts
    // the lower panel, the master row and the footer cover the body almost completely (EQ curves and
    // sliders stay clean); the heads in the slots stay visible
    g.setColour(colours::background.withAlpha(0.8f));
    g.fillRect(master.getBounds().getUnion(edit.getBounds()).withTop(edit.getY() - 2).withBottom(master.getBottom() + 2)
                   .withLeft(edit.getX() - 2).withRight(edit.getRight() + 2));
    g.fillRect(footer.getBounds());
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
    master.setBounds(r.removeFromBottom(52 + MasterPanel::kGateRowH));
    r.removeFromBottom(14);
    edit.setBounds(r);
    ir.setBounds(r);
    if (playerPanel) playerPanel->setBounds(r);
    globalEq.setBounds(r);
   #if AMPSURD_ROOT
    root.setBounds(r);
   #endif
   #if AMPSURD_DOUBLER
    doubler.setBounds(r);
   #endif
    auto withButton = r;
    globalEqButton.setBounds(withButton.removeFromRight(40));
    withButton.removeFromRight(10);
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
    content.master.onFrankClick = [this] { frankensteinPressed(); };
    content.globalEq.onClose = [this] { showGlobalEq(false); };
    content.globalEqButton.onClick = [this] { showGlobalEq(true); };
   #if AMPSURD_ROOT
    content.root.onClose = [this] { showRoot(false); };
    content.master.onRootClick = [this] { showRoot(!rootOpen); };
   #endif
   #if AMPSURD_DOUBLER
    content.doubler.onClose = [this] { showDoubler(false); };
    content.master.onDoublerClick = [this] { showDoubler(!doublerOpen); };
   #endif

    setResizable(true, true);
    setResizeLimits(kWidth * 2 / 3, kHeight * 2 / 3, kWidth * 2, kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double) kWidth / (double) kHeight);
    setSize(kWidth, kHeight);

    // Buttons, boxes and sliders never keep the keyboard focus: Space / R / W reach the player
    // shortcuts (standalone), and in a DAW Space keeps working as the DAW's transport.
    std::function<void(juce::Component&)> noFocus = [&](juce::Component& c) {
        for (auto* ch : c.getChildren())
        {
            if (dynamic_cast<juce::Button*>(ch) || dynamic_cast<juce::ComboBox*>(ch) || dynamic_cast<juce::Slider*>(ch))
            {
                ch->setWantsKeyboardFocus(false);
                ch->setMouseClickGrabsKeyboardFocus(false);
            }
            noFocus(*ch);
        }
    };
    noFocus(content);
    setWantsKeyboardFocus(proc.getPlayer() != nullptr);

    timerCallback();
    startTimerHz(30);
}

bool AmpsurdEditor::keyPressed(const juce::KeyPress& key)
{
    // undo / redo of the settings (also in a DAW, when AMPSURD's window has the keyboard)
    const auto mods = key.getModifiers();
    const auto kc = juce::CharacterFunctions::toLowerCase((juce::juce_wchar) key.getKeyCode());
    if (mods.isCommandDown() && kc == 'z') { if (mods.isShiftDown()) proc.redo(); else proc.undo(); return true; }
    if (mods.isCommandDown() && kc == 'y') { proc.redo(); return true; }
    if (key == juce::KeyPress::escapeKey && proc.getEqClipboardSource() >= 0) { proc.clearEqClipboard(); return true; } // cancel a COPY

    auto* pl = proc.getPlayer();
    if (pl == nullptr) return false;
    using S = PlayerRecorder::State;
    const auto st = pl->getState();
    if (key == juce::KeyPress::spaceKey)
    {
        if (st == S::playing || st == S::recording) pl->stop();
        else pl->play(); // also CONTINUE after a pause
        return true;
    }
    const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (c == 'r' && !key.getModifiers().isAnyModifierKeyDown())
    {
        if (content.playerPanel) content.playerPanel->recordPressed(); // take / punch in / out, or a VIDEO take
        else pl->record();
        return true;
    }
    if ((c == 'w' && !key.getModifiers().isAnyModifierKeyDown()) || key == juce::KeyPress::homeKey)
    {
        pl->toStart();
        return true;
    }
    return false;
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

// The lower panel shows one view at a time: amp EDIT, IR editor, player, ROOT, Frankenstein (while it
// is on) or GLOBAL (tuner, Global EQ, effects - the default). Opening one closes the others.
void AmpsurdEditor::showDoubler(bool show)
{
    doublerOpen = show && AmpsurdProcessor::hasDoublerModule();
    if (doublerOpen)
    {
        rootOpen = false;
        globalEqOpen = false;
        playerOpen = false;
        selected = -1;
        content.edit.setSlot(-1);
        content.ir.setSlot(-1);
    }
    timerCallback();
}

void AmpsurdEditor::showRoot(bool show)
{
    rootOpen = show && AmpsurdProcessor::hasRootModule();
    if (rootOpen)
    {
        doublerOpen = false;
        globalEqOpen = false;
        playerOpen = false;
        selected = -1;
        content.edit.setSlot(-1);
        content.ir.setSlot(-1);
    }
    timerCallback();
}

void AmpsurdEditor::frankensteinPressed()
{
    // on and in view -> off; otherwise -> on AND in view (it used to switch on / off behind EDIT, IR, the player,
    // ROOT, DOUBLER or GLOBAL, so nothing seemed to happen - tester 2026-10-28)
    if (proc.isFrankensteinOn() && content.frankenstein.isVisible())
    {
        proc.setFrankenstein(false);
        timerCallback();
        return;
    }
    proc.setFrankenstein(true);
    rootOpen = false;
    doublerOpen = false;
    globalEqOpen = false;
    playerOpen = false;
    selected = -1;
    content.edit.setSlot(-1);
    content.ir.setSlot(-1);
    lastFrankOn = true;
    timerCallback();
}

void AmpsurdEditor::showGlobalEq(bool show)
{
    globalEqOpen = show;
    if (show)
    {
        rootOpen = false;
        doublerOpen = false;
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
        rootOpen = false;
        doublerOpen = false;
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
        rootOpen = false;
        doublerOpen = false;
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
    proc.expireEqClipboard(); // an unused EQ copy ends after 20 s
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
    {
        // switching Frankenstein on shows it (closes ROOT and GLOBAL)
        const bool frankNow = proc.isFrankensteinOn();
        if (frankNow && !lastFrankOn) { rootOpen = false; doublerOpen = false; globalEqOpen = false; }
        lastFrankOn = frankNow;
    }
    const bool frankOn = proc.isFrankensteinOn();
    const bool rootView = !editing && !playerView && rootOpen;
    const bool doublerView = !editing && !playerView && !rootView && doublerOpen;
    const bool frank = !editing && !playerView && !rootView && !doublerView && frankOn && !globalEqOpen;
    const bool global = !editing && !playerView && !rootView && !doublerView && !frank; // the default view
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
   #if AMPSURD_ROOT
    content.root.setVisible(rootView);
    if (rootView) content.root.refresh();
   #else
    juce::ignoreUnused(rootView);
   #endif
   #if AMPSURD_DOUBLER
    content.doubler.setVisible(doublerView);
    if (doublerView) content.doubler.refresh();
   #else
    juce::ignoreUnused(doublerView);
   #endif
    content.frankenstein.setVisible(frank);
    content.globalEqButton.setVisible(frank); // Frankenstein view: the way to GLOBAL
    content.globalEq.setCloseVisible(frankOn); // GLOBAL while Frankenstein is on: CLOSE returns to it
    proc.setTunerVisible(global);
    if (global)
        content.globalEq.refresh();
    else if (frank)
        content.frankenstein.refresh();
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
        content.footer.setTooltip(drops > 0 ? "DROPOUTS: how many times the audio was late since start - each one is a short click "
                                              "(the computer or the audio driver did not keep up). If it rises while you play: a larger "
                                              "buffer size (Options), ECO on the amps, or fewer amps."
                                            : juce::String());
    }
    content.master.refresh();
    content.header.refresh();

    // dragon: one head per slot - asleep (empty), awake (capture loaded), red eye (SOLO or EDIT)
    {
        std::array<int, 5> st {};
        for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
        {
            const bool loaded = proc.getSlotStatus(s).state == AmpsurdProcessor::SlotState::loaded;
            const bool solo = proc.params.getRawParameterValue(AmpsurdProcessor::slotParamId(s, "solo"))->load() > 0.5f;
            const bool edit = content.edit.getSlot() == s || content.ir.getSlot() == s;
            st[(size_t) s] = !loaded ? ampsurd::ui::DragonLayer::asleep : (solo || edit) ? ampsurd::ui::DragonLayer::redEye : ampsurd::ui::DragonLayer::awake;
        }
        const bool show = proc.isDragonShown();
        if (content.dragon.setStates(st) || show != content.showDragon)
        {
            content.showDragon = show;
            content.repaint();
        }
    }
    // standalone: take the keyboard focus whenever nothing else has it (typing a value keeps it)
    if (proc.getPlayer() != nullptr && isShowing() && juce::Component::getCurrentlyFocusedComponent() == nullptr)
        grabKeyboardFocus();
}

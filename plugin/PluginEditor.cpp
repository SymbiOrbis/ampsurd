#include "PluginEditor.h"

using namespace ampsurd::ui;

AmpsurdEditor::Content::Content(AmpsurdProcessor& p) : header(p), edit(p), ir(p), frankenstein(p), globalEq(p), globalEqButton(p),
#if AMPSURD_ROOT
      root(p),
#endif
#if AMPSURD_DOUBLER
      doubler(p),
#endif
      master(p),
      cabIrs(p),
      log(p)
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
    addChildComponent(cabIrs); // last: on top of everything while open
    addChildComponent(log);
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
    paintPedalStacks(g);
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

// Pedals before an amp (owner 2026-10-30): one more frame behind the slot per pedal, offset up and left like a
// stack of paper - only its top and left edges show. A bypassed pedal's frame is dimmed but stays.
void AmpsurdEditor::Content::paintPedalStacks(juce::Graphics& g)
{
    constexpr float step = 4.0f, radius = 3.0f; // 2 pedals = 8 px: fits the gaps above / between the slots
    for (auto& sl : slots)
    {
        const auto layers = sl->getPedalLayers(); // [0] = the pedal right before the amp ... (0 none, 1 on, 2 bypassed)
        const auto front = sl->getBounds().toFloat().reduced(colours::sectionBorder * 0.5f + 0.25f);
        int depth = 0;
        for (int st : layers) if (st != 0) ++depth;
        // back to front, each clipped by everything in front of it (so only the top / left edges are drawn)
        for (int k = depth; k >= 1; --k)
        {
            int seen = 0, state = 0;
            for (int st : layers) if (st != 0 && ++seen == k) { state = st; break; }
            juce::Graphics::ScopedSaveState save(g);
            g.excludeClipRegion(front.translated(-step * (float) (k - 1), -step * (float) (k - 1)).expanded(1.0f).toNearestInt());
            const auto r = front.translated(-step * (float) k, -step * (float) k);
            if (state == 2)
            {
                // bypassed: dimmed and dashed (not by colour alone)
                juce::Path p, dashed;
                p.addRoundedRectangle(r, radius);
                const float dashes[] { 4.0f, 3.0f };
                juce::PathStrokeType(1.5f).createDashedStroke(dashed, p, dashes, 2);
                g.setColour(colours::lineStrong.withAlpha(0.45f));
                g.fillPath(dashed);
            }
            else
            {
                g.setColour(colours::lineStrong.withAlpha(0.8f));
                g.drawRoundedRectangle(r, radius, 1.5f);
            }
        }
    }
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
    // AMPSURD IRs map: over the slots and the centre area (between the header and the master row)
    cabIrs.setBounds(24, 56 + 8, getWidth() - 48, r.getBottom() - (56 + 8));
    log.setBounds(cabIrs.getBounds());
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
    content.ir.onAmpsurdIrs = [this](int slot) { showAmpsurdIrs(slot); };
    content.cabIrs.onClose = [this] { timerCallback(); };
    content.log.onClose = [this] { timerCallback(); };
    content.footer.onCpuClicked = [this] { showLog(!content.log.isVisible()); };
    content.header.onShowLog = [this] { showLog(true); };
    // TWO PLAYERS (owner 2026-11-12): settings menu; the cards then show P1 / P2
    content.header.onToggleTwoPlayers = [this] { setTwoPlayers(!proc.isTwoPlayersOn()); };
    if (content.playerPanel) content.playerPanel->onSetTwoPlayers = [this](bool on) { setTwoPlayers(on); };
    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        content.header.onToggleFullScreen = [this] { setFullScreen(!fullScreen); };
        content.header.isFullScreen = [this] { return fullScreen; };
    }
    if (content.playerPanel)
    {
        content.playerPanel->onClose = [this] { showPlayer(false); };
        content.playerPanel->onKey = [this](const juce::KeyPress& k) { return keyPressed(k); };
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

    // full screen has no title bar: this button (in the free space beside AMPSURD) brings the window back
    exitFullScreen.setTooltip("Back to the normal window (Esc)");
    exitFullScreen.setWantsKeyboardFocus(false);
    exitFullScreen.setMouseClickGrabsKeyboardFocus(false);
    exitFullScreen.onClick = [this] { setFullScreen(false); };
    addChildComponent(exitFullScreen); // after the content: drawn on top

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
    if (key == juce::KeyPress::escapeKey && content.log.isVisible()) { content.log.close(); return true; }
    if (key == juce::KeyPress::escapeKey && content.cabIrs.isVisible()) { content.cabIrs.close(); return true; }
    if (key == juce::KeyPress::escapeKey && proc.getEqClipboardSource() >= 0) { proc.clearEqClipboard(); return true; } // cancel a COPY
    // FULL SCREEN (standalone): F11, on a Mac also Ctrl+Cmd+F; Esc leaves it
    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
       #if JUCE_MAC
        const bool macKeys = mods.isCtrlDown() && mods.isCommandDown() && kc == 'f';
       #else
        const bool macKeys = false;
       #endif
        if (key.getKeyCode() == juce::KeyPress::F11Key || macKeys)
        {
            setFullScreen(!fullScreen);
            return true;
        }
        if (key == juce::KeyPress::escapeKey && fullScreen) { setFullScreen(false); return true; }
    }

    auto* pl = proc.getPlayer();
    if (pl == nullptr) return false;
    using S = PlayerRecorder::State;
    const auto st = pl->getState();
    if (key == juce::KeyPress::spaceKey)
    {
        // Space: STOP while playing or recording (also a VIDEO take: the panel finishes it), otherwise PLAY
        if (st == S::playing || st == S::recording) pl->stop();
        else pl->play(); // also CONTINUE after a pause
        return true;
    }
    const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    // R or Ctrl+R (Cmd+R on a Mac): REC - audio take / punch in / out, or a VIDEO take (owner 2026-11-12: both keys).
    // With Ctrl the key code is used (Ctrl+R gives no text character on Windows).
    const bool ctrl = mods.isCtrlDown() || mods.isCommandDown();
    if ((c == 'r' && !mods.isAnyModifierKeyDown()) || (ctrl && !mods.isAltDown() && kc == 'r'))
    {
        if (content.playerPanel)
        {
            if (!playerOpen) showPlayer(true); // you see what is recorded
            content.playerPanel->recordPressed();
        }
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
    // scaled as a whole; in full screen the screen's shape can differ: fitted and centred (the rest is background)
    const float scale = juce::jmin((float) getWidth() / (float) kWidth, (float) getHeight() / (float) kHeight);
    const float ox = std::floor(((float) getWidth() - kWidth * scale) * 0.5f), oy = std::floor(((float) getHeight() - kHeight * scale) * 0.5f);
    content.setTransform(juce::AffineTransform::scale(scale).translated(ox, oy));
    // EXIT FULL SCREEN: top right, in the free strip beside or above AMPSURD; the bottom right corner if there is none
    const int bw = 132, bh = 24, pad = 6;
    const bool room = ox >= (float) (bw + pad * 2) || oy >= (float) (bh + pad * 2);
    exitFullScreen.setBounds(getWidth() - bw - pad, room ? pad : getHeight() - bh - pad, bw, bh);
    exitFullScreen.setVisible(fullScreen);
}

void AmpsurdEditor::setTwoPlayers(bool on)
{
    auto set = [this](const juce::String& id, float v) {
        if (auto* prm = proc.params.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v));
    };
    if (on)
    {
        // nobody's amps chosen for player 2 yet: amps 4 and 5 (P1 / P2 on the cards changes it)
        bool any = false;
        for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
            if (auto* v = proc.params.getRawParameterValue(AmpsurdProcessor::slotParamId(s, "player2"))) any = any || v->load() > 0.5f;
        if (!any)
            for (int s = 3; s < AmpsurdProcessor::kNumSlots; ++s) set(AmpsurdProcessor::slotParamId(s, "player2"), 1.0f);
        if (proc.isFrankensteinOn()) proc.setFrankenstein(false); // no Frankenstein with two players
    }
    set("twoPlayers", on ? 1.0f : 0.0f);
    if (content.playerPanel) content.playerPanel->twoPlayersChanged(on);
    timerCallback();
}

void AmpsurdEditor::setFullScreen(bool on)
{
    if (on == fullScreen || !juce::JUCEApplicationBase::isStandaloneApp()) return;
    auto* win = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent());
    if (win == nullptr) return;
    fullScreen = on;
    if (on)
    {
        windowBoundsBeforeFullScreen = win->getBounds();
        titleBarBeforeFullScreen = win->getTitleBarHeight();
    }
    // any shape while in full screen (the content is fitted), the usual limits and proportions otherwise
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(on ? 0.0 : (double) kWidth / (double) kHeight);
    if (on) setResizeLimits(kWidth / 4, kHeight / 4, 16384, 16384);
    else setResizeLimits(kWidth * 2 / 3, kHeight * 2 / 3, kWidth * 2, kHeight * 2);
    // no title bar (with JUCE's Options button) in full screen: Audio / MIDI settings are in the settings menu too
    for (auto* c : win->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText() == "Options") b->setVisible(!on);
    win->setTitleBarHeight(on ? 0 : titleBarBeforeFullScreen);
    juce::Desktop::getInstance().setKioskModeComponent(on ? win : nullptr, false);
    if (!on && !windowBoundsBeforeFullScreen.isEmpty()) win->setBounds(windowBoundsBeforeFullScreen);
    resized(); // shows / hides EXIT FULL SCREEN
    grabKeyboardFocus();
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

void AmpsurdEditor::showAmpsurdIrs(int slot)
{
    if (slot < 0) { if (content.cabIrs.isVisible()) content.cabIrs.close(); return; }
    if (content.ir.getSlot() != slot) showIr(slot);
    content.cabIrs.toFront(false);
    content.cabIrs.open(slot);
}

void AmpsurdEditor::showLog(bool show)
{
    if (!show) { if (content.log.isVisible()) content.log.close(); return; }
    content.log.toFront(false);
    content.log.open();
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
    if (content.cabIrs.isVisible() && proc.getSlotStatus(content.cabIrs.getSlot()).state == AmpsurdProcessor::SlotState::empty)
        content.cabIrs.close(); // its amp was removed
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
                                              "(the computer or the audio driver did not keep up). Click: the log - what was going on at each "
                                              "one and the likely cause."
                                            : juce::String("Click: the log (dropouts and problems)"));
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
    // standalone: take the keyboard focus whenever nothing else has it (typing a value keeps it) - but ONLY while
    // AMPSURD's own window is the active one. Grabbing it while a file dialog (Save as, Import pack, LOAD NAM), a name
    // prompt or the camera window was in front made macOS give the keys back to AMPSURD every 30 ms: the dialog could
    // not be typed into or browsed (Mac tester 2026-10-30, standalone only - the plugin has no player, so no grab).
    if (proc.getPlayer() != nullptr && isShowing() && juce::Component::getCurrentlyFocusedComponent() == nullptr
        && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0 && juce::Process::isForegroundProcess())
        if (auto* peer = getPeer(); peer != nullptr && peer->isFocused())
            grabKeyboardFocus();
}

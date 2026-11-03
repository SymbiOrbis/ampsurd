#include "LogPanel.h"

#include "Theme.h"

namespace ampsurd::ui
{
using namespace colours;

LogPanel::LogPanel(AmpsurdProcessor& p) : proc(p)
{
    setOpaque(true);
    view.setMultiLine(true, true);
    view.setReadOnly(true);
    view.setScrollbarsShown(true);
    view.setCaretVisible(false);
    view.setFont(Fonts::get().regular(13.0f));
    view.setColour(juce::TextEditor::backgroundColourId, background);
    view.setColour(juce::TextEditor::textColourId, text);
    view.setColour(juce::TextEditor::outlineColourId, line);
    view.setColour(juce::TextEditor::focusedOutlineColourId, line);
    addAndMakeVisible(view);
    for (auto* b : { &closeButton, &copyButton, &fileButton, &clearButton })
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
    }
    closeButton.setTooltip("Back (Esc)");
    copyButton.setTooltip("Copies the whole log - to paste into an e-mail or a message");
    fileButton.setTooltip("Shows the log file (Documents / AMPSURD / Logs / AMPSURD_log.txt) - to send it to someone");
    clearButton.setTooltip("Empties this list (the file keeps everything)");
    closeButton.onClick = [this] { close(); };
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(proc.getEventLog().getText()); };
    fileButton.onClick = [this] {
        const auto f = proc.getEventLog().getFile();
        if (f.existsAsFile()) f.revealToUser();
        else f.getParentDirectory().revealToUser();
    };
    clearButton.onClick = [this] { proc.getEventLog().clear(); refresh(); };
}

void LogPanel::open()
{
    setVisible(true);
    shownVersion = -1;
    refresh();
    startTimerHz(4);
}

void LogPanel::close()
{
    stopTimer();
    setVisible(false);
    if (onClose) onClose();
}

void LogPanel::timerCallback() { refresh(); }

void LogPanel::refresh()
{
    auto& log = proc.getEventLog();
    if (log.getVersion() == shownVersion) return;
    shownVersion = log.getVersion();
    const auto t = log.getText();
    view.setText(t.isEmpty() ? juce::String("Nothing yet - dropouts and problems will be listed here, with what was going on at that moment.")
                             : t, false);
    // a short log from the top; a long one scrolled to the newest entry at the bottom
    view.setCaretPosition(0);
    if (view.getTextHeight() > view.getHeight()) view.moveCaretToEnd();
    repaint();
}

void LogPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(28);
    closeButton.setBounds(header.removeFromRight(80));
    r.removeFromTop(8);
    auto bottom = r.removeFromBottom(30);
    clearButton.setBounds(bottom.removeFromRight(90));
    bottom.removeFromRight(8);
    fileButton.setBounds(bottom.removeFromRight(110));
    bottom.removeFromRight(8);
    copyButton.setBounds(bottom.removeFromRight(90));
    r.removeFromBottom(10);
    view.setBounds(r);
    shownVersion = -1; // laid out again: show it from the right place
    if (isVisible()) refresh();
}

void LogPanel::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(sectionBorder * 0.5f), 3.0f, sectionBorder);
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(28);
    g.setColour(accent);
    g.setFont(Fonts::get().label(11.0f));
    g.drawText("LOG", header.removeFromLeft(60), juce::Justification::centredLeft, false);
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText("Dropouts (with what was going on and the likely cause) and problems since AMPSURD started. Newest at the bottom.",
               header.withTrimmedRight(90), juce::Justification::centredLeft, true);
    auto bottom = getLocalBounds().reduced(16, 12).removeFromBottom(30);
    g.setFont(Fonts::get().regular(12.0f));
    g.setColour(textFaint);
    g.drawText(juce::String(proc.getEventLog().getNumEntries()) + " entries.  File: Documents / AMPSURD / Logs / AMPSURD_log.txt",
               bottom.withTrimmedRight(320), juce::Justification::centredLeft, true);
}
} // namespace ampsurd::ui

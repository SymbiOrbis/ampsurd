#pragma once

// AMPSURD IRs (2026-10-28): the owner's synthetic 4x12 cabinet IRs as one map to play with.
//
//   rows    = speaker x microphone (V30 / DV77 / SWAMP x SM57 / MD421 / R121)
//   columns = mic position, 25 mm (cap edge) .. 100 mm (on the cone), the set's files every 5 mm (dots)
//
// Click or drag anywhere: the IR follows the mouse - between the dots AMPSURD calculates the in-between IR
// (core/CabIrModel), so moving across the map is like moving the mic / swapping the mic or speaker. HOVER TO
// LISTEN: just moving the mouse over the map lets you hear the points; leaving the map returns to the chosen
// one (and hovering never becomes an undo step). SNAP TO GRID: only the 144 set points. SAVE IR writes the
// chosen IR as a WAV (named like the set's files), EXPORT ALL 144 the whole set at the session's rate.
// Shown over the slots and the centre area (an overlay inside the plugin window, no extra OS window).

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

namespace ampsurd::ui
{
class CabIrMatrix final : public juce::Component, private juce::Timer
{
public:
    explicit CabIrMatrix(AmpsurdProcessor&);
    ~CabIrMatrix() override;
    void open(int slot);  // shows it for this amp
    void close();         // back to what was chosen (a hover preview ends)
    int getSlot() const { return slot; }
    std::function<void()> onClose;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // tests: the point under a position of the map, and choosing / previewing it as the mouse would
    juce::Rectangle<int> getMapArea() const { return map; }
    CabPoint pointAt(juce::Point<int> p) const;
    juce::Point<float> positionOf(CabPoint p) const;
    void choose(CabPoint p);
    void preview(CabPoint p);
    void endPreview();
    bool hasChosen() const { return chosenValid; }
    CabPoint getChosen() const { return chosen; }
    void setHoverToListen(bool on) { hoverButton.setToggleState(on, juce::dontSendNotification); }
    void setSnap(bool on) { snapButton.setToggleState(on, juce::dontSendNotification); }
    void flush() { timerCallback(); } // tests: send the pending IR now
    // the WAV of a point, levelled like the original set (all 144 equally loud, peak of the loudest -1 dBFS)
    static bool writeIrFile(CabPoint p, double rate, const juce::File& f, juce::String& error);

private:
    void timerCallback() override;
    void request(CabPoint p);
    void saveChosen();
    void exportAll();
    void setStatus(const juce::String& s) { status = s; repaint(); }
    CabPoint snapped(CabPoint p) const;

    AmpsurdProcessor& proc;
    int slot = -1;
    juce::Rectangle<int> map, labels, axis, bottom;
    juce::TextButton closeButton { "CLOSE" }, hoverButton { "HOVER TO LISTEN" }, snapButton { "SNAP TO GRID" },
                     saveButton { "SAVE IR" }, exportButton { "EXPORT ALL 144" };
    CabPoint chosen, hover, pending, sent;
    bool chosenValid = false, hovering = false, dragging = false, hasPending = false;
    AmpsurdProcessor::IrStatus original; // the IR before the map was opened (restored after a hover preview)
    juce::String status;
};
} // namespace ampsurd::ui

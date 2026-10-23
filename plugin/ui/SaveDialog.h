#pragma once

// A save dialog with SEPARATE file types to choose from (owner 2026-10-23: "Save as type" must offer
// .ampsurd and .ampsurdpack one or the other; JUCE's chooser puts all patterns into one entry).
// Windows: the system's IFileSaveDialog with one entry per type. macOS: NSSavePanel with a "Format" pop-up.
// Other systems (Linux test builds): not available - the caller uses JUCE's chooser.
// Message thread, modal (the audio keeps running).

#include <juce_gui_basics/juce_gui_basics.h>

bool hasTypedSaveDialog();

// descriptions[i] / extensions[i] (without the dot) = one file type. Returns the file with the chosen type's
// extension (chosenType = its index), or File() when cancelled.
juce::File showTypedSaveDialog(juce::Component* parent, const juce::String& title, const juce::File& suggestion,
                               const juce::StringArray& descriptions, const juce::StringArray& extensions, int& chosenType);

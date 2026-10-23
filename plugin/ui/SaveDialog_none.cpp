#include "SaveDialog.h"

bool hasTypedSaveDialog() { return false; }

juce::File showTypedSaveDialog(juce::Component*, const juce::String&, const juce::File&, const juce::StringArray&, const juce::StringArray&, int& chosenType)
{
    chosenType = -1;
    return {};
}

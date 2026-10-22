// Platforms without video export (Linux test builds).
#include "VideoMux.h"

bool isVideoExportSupported() { return false; }

juce::String muxVideoToMp4(const juce::File&, const juce::AudioBuffer<float>&, double, double, const juce::File&,
                           const std::function<void(float)>&)
{
    return "Saving videos is not available on this system.";
}

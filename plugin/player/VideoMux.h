#pragma once

// Saves a VIDEO take as MP4 (H.264 video + AAC audio): the camera file's picture with AMPSURD's mix as
// the sound. audioLeadSeconds: where the audio's first sample falls in the video (+ = after frame 0,
// - = before it; the part before frame 0 is cut). The result is as long as the video.
//   Windows: Media Foundation (the camera file is re-encoded to H.264).
//   macOS:   AVFoundation (AVMutableComposition + AVAssetExportSession).
// Runs on a background thread (blocking). Returns an empty string on success, otherwise the error.

#include <functional>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

juce::String muxVideoToMp4(const juce::File& video, const juce::AudioBuffer<float>& audio, double sampleRate, double audioLeadSeconds,
                           const juce::File& out, const std::function<void(float)>& progress);
bool isVideoExportSupported();

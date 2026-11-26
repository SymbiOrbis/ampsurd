#pragma once

// Camera for VIDEO takes in the standalone player. Records the picture only; the sound of the saved video is
// AMPSURD's own mix (VideoMux).
//   Windows (VideoCapture_win.cpp): Media Foundation - the camera is opened by its device path (stable id), its
//     advertised formats are read and one is chosen automatically (NV12 / YUY2 / MJPEG ... converted for the
//     preview), the first live picture is waited for, the take is written as WMV. Cameras that only exist for
//     DirectShow (virtual cameras such as OBS Virtual Camera) and cameras Media Foundation cannot start go through
//     JUCE's DirectShow camera instead, with the size range widened until one of the camera's sizes fits.
//   macOS (VideoCapture.cpp): AVFoundation via JUCE.
// The public calls are message-thread only. onError / onInfo may be called from any thread.
// On other platforms (Linux test builds) isSupported() is false.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>
#if JUCE_WINDOWS || JUCE_MAC
  #include <juce_video/juce_video.h> // (the camera window's video playback uses it too)
#endif

class VideoCapture
{
public:
    VideoCapture();
    ~VideoCapture();

    static bool isSupported();
    // the cameras, as shown in the picker; open(index) refers to the list of the last call
    static juce::StringArray cameras();

    // empty = ok (a live picture arrived, or the camera is still starting), otherwise the message to show.
    // A failed open leaves nothing open, so the same camera can be tried again.
    juce::String open(int index);
    void close();
    bool isOpen() const;
    int getOpenIndex() const { return openIndex; }
    std::unique_ptr<juce::Component> createPreview(); // nullptr when no camera is open

    juce::String startRecording(const juce::File& folder); // empty = ok
    // stops; returns the first frame's wall clock (epoch ms) and the file (empty when nothing was recorded)
    juce::int64 stopRecording(juce::File& recordedFile);
    bool isRecording() const { return recording; }

    std::function<void(const juce::String&)> onError; // shown to the user (any thread)
    std::function<void(const juce::String&)> onInfo;  // for the log: devices, formats, what was chosen, codes (any thread)

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
    int openIndex = -1;
    bool recording = false;
};

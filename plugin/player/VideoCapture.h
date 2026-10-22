#pragma once

// Camera for VIDEO takes in the standalone player (Windows: DirectShow via JUCE, macOS: AVFoundation via
// JUCE). Records the picture only; the sound of the saved video is AMPSURD's own mix (VideoMux).
// Message thread only. On other platforms (Linux test builds) isSupported() is false.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>
#if JUCE_WINDOWS || JUCE_MAC
  #include <juce_video/juce_video.h>
#endif

class VideoCapture
{
public:
    VideoCapture();
    ~VideoCapture();

    static bool isSupported();
    static juce::StringArray cameras();

    juce::String open(int index); // empty = ok, otherwise the error to show
    void close();
    bool isOpen() const;
    int getOpenIndex() const { return openIndex; }
    std::unique_ptr<juce::Component> createPreview(); // nullptr when no camera is open

    juce::String startRecording(const juce::File& folder); // empty = ok
    // stops; returns the first frame's wall clock (epoch ms) and the file (empty when nothing was recorded)
    juce::int64 stopRecording(juce::File& recordedFile);
    bool isRecording() const { return recording; }

    std::function<void(const juce::String&)> onError;

private:
   #if JUCE_USE_CAMERA
    std::unique_ptr<juce::CameraDevice> device;
   #endif
    int openIndex = -1;
    bool recording = false;
    juce::File file;
};

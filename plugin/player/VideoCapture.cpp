// VideoCapture on macOS (AVFoundation via JUCE) and Linux test builds (no camera). Windows: VideoCapture_win.cpp.
#include "VideoCapture.h"

#if ! JUCE_WINDOWS

#if JUCE_MAC
  #include <juce_video/juce_video.h>
#endif

struct VideoCapture::Impl
{
   #if JUCE_USE_CAMERA
    std::unique_ptr<juce::CameraDevice> device;
    juce::File file;
   #endif
};

VideoCapture::VideoCapture() : impl(std::make_unique<Impl>()) {}
VideoCapture::~VideoCapture() { close(); }

bool VideoCapture::isSupported()
{
   #if JUCE_USE_CAMERA
    return true;
   #else
    return false;
   #endif
}

juce::StringArray VideoCapture::cameras()
{
   #if JUCE_USE_CAMERA
    return juce::CameraDevice::getAvailableDevices();
   #else
    return {};
   #endif
}

juce::String VideoCapture::open(int index)
{
    close();
   #if JUCE_USE_CAMERA
    const auto names = juce::CameraDevice::getAvailableDevices();
    const auto name = juce::isPositiveAndBelow(index, names.size()) ? names[index] : juce::String("camera " + juce::String(index + 1));
    if (onInfo) onInfo("Camera: opening \"" + name + "\" (AVFoundation)");
    // up to 1280 x 720 first (the size used so far); a camera without such a size is opened at its own size
    const int maxSizes[][2] = { { 1280, 720 }, { 1920, 1080 }, { 4096, 4096 } };
    for (const auto& m : maxSizes)
    {
        impl->device.reset(juce::CameraDevice::openDevice(index, 1, 1, m[0], m[1], true));
        if (impl->device != nullptr) break;
        if (onInfo) onInfo("Camera: JUCE could not open it with sizes up to " + juce::String(m[0]) + " x " + juce::String(m[1]));
    }
    // JUCE does not say why a camera could not be opened, so no cause is guessed here
    if (impl->device == nullptr) return "The camera could not be started.";
    impl->device->onErrorOccurred = [this](const juce::String& e) {
        if (onInfo) onInfo("Camera error: " + e);
        if (onError) onError(e);
    };
    openIndex = index;
    if (onInfo) onInfo("Camera: \"" + name + "\" open");
    return {};
   #else
    juce::ignoreUnused(index);
    return "Video recording is not available on this system.";
   #endif
}

void VideoCapture::close()
{
    juce::File f;
    if (recording) stopRecording(f);
   #if JUCE_USE_CAMERA
    impl->device.reset();
   #endif
    openIndex = -1;
}

bool VideoCapture::isOpen() const
{
   #if JUCE_USE_CAMERA
    return impl->device != nullptr;
   #else
    return false;
   #endif
}

std::unique_ptr<juce::Component> VideoCapture::createPreview()
{
   #if JUCE_USE_CAMERA
    if (impl->device != nullptr) return std::unique_ptr<juce::Component>(impl->device->createViewerComponent());
   #endif
    return {};
}

juce::String VideoCapture::startRecording(const juce::File& folder)
{
   #if JUCE_USE_CAMERA
    if (impl->device == nullptr) return "No camera is open.";
    folder.createDirectory();
    impl->file = folder.getNonexistentChildFile("Video " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"),
                                                juce::CameraDevice::getFileExtension());
    impl->device->startRecordingToFile(impl->file, 2); // 2 = high quality
    recording = true;
    return {};
   #else
    juce::ignoreUnused(folder);
    return "Video recording is not available on this system.";
   #endif
}

juce::int64 VideoCapture::stopRecording(juce::File& recordedFile)
{
    recordedFile = juce::File();
    if (!recording) return 0;
    recording = false;
   #if JUCE_USE_CAMERA
    if (impl->device == nullptr) return 0;
    impl->device->stopRecording();
    recordedFile = impl->file;
    return impl->device->getTimeOfFirstRecordedFrame().toMilliseconds();
   #else
    return 0;
   #endif
}

#endif // ! JUCE_WINDOWS

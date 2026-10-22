#include "VideoCapture.h"

VideoCapture::VideoCapture() = default;
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
    // up to 1280 x 720: sharp enough for an idea, and the Windows camera path records at a fixed bit rate
    device.reset(juce::CameraDevice::openDevice(index, 320, 240, 1280, 720, true));
    if (device == nullptr) return "The camera could not be opened (in use by another app, or no permission).";
    device->onErrorOccurred = [this](const juce::String& e) { if (onError) onError(e); };
    openIndex = index;
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
    device.reset();
   #endif
    openIndex = -1;
}

bool VideoCapture::isOpen() const
{
   #if JUCE_USE_CAMERA
    return device != nullptr;
   #else
    return false;
   #endif
}

std::unique_ptr<juce::Component> VideoCapture::createPreview()
{
   #if JUCE_USE_CAMERA
    if (device != nullptr) return std::unique_ptr<juce::Component>(device->createViewerComponent());
   #endif
    return {};
}

juce::String VideoCapture::startRecording(const juce::File& folder)
{
   #if JUCE_USE_CAMERA
    if (device == nullptr) return "No camera is open.";
    folder.createDirectory();
    file = folder.getNonexistentChildFile("Video " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"),
                                          juce::CameraDevice::getFileExtension());
    device->startRecordingToFile(file, 2); // 2 = high quality (30 frames per second on Windows)
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
    if (device == nullptr) return 0;
    device->stopRecording();
    recordedFile = file;
    return device->getTimeOfFirstRecordedFrame().toMilliseconds();
   #else
    return 0;
   #endif
}

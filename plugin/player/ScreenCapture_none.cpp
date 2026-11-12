// SCREEN BOX on systems without screen recording (Linux test builds). AMPSURD_FAKE_SCREEN=1: one test screen with a
// fixed picture, so the camera window's SCREEN controls and the box can be tested; nothing is recorded.
#include "ScreenCapture.h"

#include <vector>

namespace
{
bool fake() { return juce::SystemStats::getEnvironmentVariable("AMPSURD_FAKE_SCREEN", "0") == "1"; }
}

struct ScreenCapture::Impl {};

ScreenCapture::ScreenCapture() = default;
ScreenCapture::~ScreenCapture() { alive->store(false); close(); }
bool ScreenCapture::isSupported() { return fake(); }
juce::StringArray ScreenCapture::screens() { return fake() ? juce::StringArray { "Screen 1 (1280 x 720)" } : juce::StringArray {}; }

juce::String ScreenCapture::open(int index)
{
    close();
    if (!fake() || index != 0) return "Screen recording is not available on this system.";
    // a test picture: a dark "desktop" with two lighter "windows"
    const int w = 1280, h = 720;
    std::vector<uint8_t> px((size_t) (w * h * 4));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t* p = &px[(size_t) ((y * w + x) * 4)];
            const bool win1 = x > 80 && x < 700 && y > 60 && y < 500, win2 = x > 760 && x < 1200 && y > 200 && y < 650;
            const uint8_t v = win1 ? 210 : win2 ? 150 : 40;
            p[0] = (uint8_t) (win1 ? 230 : v); p[1] = v; p[2] = (uint8_t) (win2 ? 200 : v); p[3] = 255;
        }
    publishPreview(makePreview(px.data(), w, h, w * 4));
    openIndex = index;
    return {};
}

void ScreenCapture::close() { openIndex = -1; publishPreview({}); }
bool ScreenCapture::isOpen() const { return openIndex >= 0; }
juce::String ScreenCapture::startRecording(const juce::File&) { return "Screen recording is not available on this system."; }
juce::int64 ScreenCapture::stopRecording(juce::File& f) { f = juce::File(); recording = false; return 0; }
void ScreenCapture::excludeWindow(juce::Component*) {}

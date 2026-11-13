// SCREEN BOX: the parts of ScreenCapture shared by all platforms (preview picture, errors).
#include "ScreenCapture.h"
#include "ScreenOverlay.h"

juce::Image ScreenCapture::makePreview(const uint8_t* bgra, int w, int h, int pitch)
{
    if (bgra == nullptr || w < 2 || h < 2) return {};
    const int pw = std::min(1280, w), ph = std::max(2, (int) std::lround((double) h * pw / w));
    juce::Image img(juce::Image::ARGB, pw, ph, false, juce::SoftwareImageType());
    {
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
        // JUCE's ARGB in memory = B, G, R, A (little-endian): the same order as the screen's BGRA
        ampsurd::screen::scalePlane(bgra, w, h, pitch, bd.data, pw, ph, bd.lineStride, 4);
        for (int y = 0; y < ph; ++y)
        {
            auto* row = bd.getLinePointer(y);
            for (int x = 0; x < pw; ++x) row[x * 4 + 3] = 255; // opaque
        }
    }
    return img;
}

void ScreenCapture::publishPreview(const juce::Image& img)
{
    {
        const juce::SpinLock::ScopedLockType sl(previewLock);
        preview = img;
    }
    ++frames;
}

juce::Image ScreenCapture::latestFrame() const
{
    const juce::SpinLock::ScopedLockType sl(previewLock);
    return preview;
}

void ScreenCapture::reportError(const juce::String& e)
{
    juce::MessageManager::callAsync([this, e, a = alive] {
        if (!a->load()) return;
        lastError = e;
        if (onError) onError(e);
    });
}

void ScreenCapture::reportInfo(const juce::String& e)
{
    juce::MessageManager::callAsync([this, e, a = alive] {
        if (a->load() && onInfo) onInfo(e);
    });
}

#pragma once

// SCREEN BOX for VIDEO takes (owner 2026-11-12): a recording of a computer screen shown as a box over the camera
// picture. The camera picture stays large; the box can be shown, hidden, moved and resized at any time, also while
// recording. The camera and the screen are recorded into their own files; they are combined when the MP4 is saved.
//
// This file is the platform-independent part (tested on Linux): when the box is shown and where (keys over time),
// where it lands in the camera picture, scaling and drawing it into a frame (NV12 on Windows, BGRA on macOS).

#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace ampsurd::screen
{
// The box at one moment. Times: seconds of the camera video (0 = its first frame). The rectangle is relative to the
// camera window's preview area (0..1), which shows the camera picture fitted and centred (letterbox).
struct BoxKey
{
    double t = 0.0;
    bool visible = false;
    juce::Rectangle<float> rect { 0.62f, 0.62f, 0.34f, 0.34f };
    bool operator==(const BoxKey& o) const { return juce::exactlyEqual(t, o.t) && visible == o.visible && rect == o.rect; }
};

// A take's screen recording and its box over time.
struct Overlay
{
    juce::File file;                 // the screen recording (H.264, video only)
    double offsetSeconds = 0.0;      // camera time of the screen file's first frame
    double previewAspect = 16.0 / 9.0; // width / height of the preview area the box rectangles refer to
    std::vector<BoxKey> keys;        // sorted by time
    bool isUsed() const;             // a file and the box shown at some moment
    bool operator==(const Overlay& o) const
    {
        return file == o.file && juce::exactlyEqual(offsetSeconds, o.offsetSeconds) && juce::exactlyEqual(previewAspect, o.previewAspect) && keys == o.keys;
    }
};

// the box at camera time t: the last key at or before t (before the first key: hidden)
BoxKey boxAt(const std::vector<BoxKey>& keys, double t);

// the box in camera picture pixels (camW x camH): the preview rectangle mapped onto the picture (fitted and centred in
// the preview), kept inside the picture, even position and size (NV12). Empty when it is too small.
juce::Rectangle<int> boxInPicture(juce::Rectangle<float> previewRect, double previewAspect, int camW, int camH);

// the screen picture inside the box, keeping its proportions (centred, the rest of the box filled black)
juce::Rectangle<int> fitInBox(juce::Rectangle<int> box, int srcW, int srcH);

// area-average scaling of an interleaved 8-bit plane (channels per pixel: 1 = Y, 2 = UV, 4 = BGRA)
void scalePlane(const uint8_t* src, int srcW, int srcH, int srcPitch, uint8_t* dst, int dstW, int dstH, int dstPitch, int channels);

// BGRA (8-bit, top row first) -> NV12 (BT.601 limited range, as webcams and the H.264 encoder expect). Even sizes.
void bgraToNv12(const uint8_t* bgra, int w, int h, int pitch, uint8_t* y, int yPitch, uint8_t* uv, int uvPitch);

// Draws the screen picture as the box into a camera frame: a thin light frame, the picture fitted inside.
void drawBoxNv12(uint8_t* dstY, int dstYPitch, uint8_t* dstUV, int dstUVPitch, int dstW, int dstH,
                 const uint8_t* srcY, int srcYPitch, const uint8_t* srcUV, int srcUVPitch, int srcW, int srcH, juce::Rectangle<int> box);
void drawBoxBgra(uint8_t* dst, int dstW, int dstH, int dstPitch, const uint8_t* src, int srcW, int srcH, int srcPitch, juce::Rectangle<int> box);

// the mouse pointer drawn into a BGRA frame at (x, y) (a white arrow with a dark outline; Windows' capture has none)
void drawPointer(uint8_t* bgra, int w, int h, int pitch, int x, int y, float scale);

// the screen's size for recording: at most 1280 x 720 (sharp enough in a box, light on the computer), even
juce::Point<int> recordSize(int screenW, int screenH);
constexpr int kFramesPerSecond = 15;
} // namespace ampsurd::screen

#include "ScreenOverlay.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ampsurd::screen
{
bool Overlay::isUsed() const
{
    if (file == juce::File()) return false;
    for (const auto& k : keys)
        if (k.visible || k.swapped) return true;
    return false;
}

bool Overlay::anySwapped() const
{
    if (file == juce::File()) return false;
    for (const auto& k : keys)
        if (k.swapped) return true;
    return false;
}

juce::Point<int> outputSize(int camW, int camH, const Overlay* o, int screenH)
{
    camW &= ~1;
    camH &= ~1;
    if (o == nullptr || !o->anySwapped() || camW < 16 || camH < 16) return { camW, camH };
    const int target = juce::jlimit(camH, juce::jmax(camH, 1080), screenH > 0 ? juce::jmax(screenH, 720) : 1080);
    double s = (double) target / camH;
    if (camW * s > 2560.0) s = 2560.0 / camW;
    s = juce::jmax(1.0, s);
    return { (int) std::lround(camW * s) & ~1, (int) std::lround(camH * s) & ~1 };
}

void scaleBilinear(const uint8_t* src, int srcW, int srcH, int srcPitch, uint8_t* dst, int dstW, int dstH, int dstPitch, int channels)
{
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    if (dstW < srcW || dstH < srcH) { scalePlane(src, srcW, srcH, srcPitch, dst, dstW, dstH, dstPitch, channels); return; } // smaller: averaging
    std::vector<int> x0((size_t) dstW), x1((size_t) dstW), fx((size_t) dstW);
    for (int dx = 0; dx < dstW; ++dx)
    {
        const double sx = juce::jlimit(0.0, (double) (srcW - 1), (dx + 0.5) * srcW / dstW - 0.5);
        x0[(size_t) dx] = (int) sx;
        x1[(size_t) dx] = juce::jmin(srcW - 1, x0[(size_t) dx] + 1);
        fx[(size_t) dx] = (int) ((sx - x0[(size_t) dx]) * 256.0);
    }
    for (int dy = 0; dy < dstH; ++dy)
    {
        const double sy = juce::jlimit(0.0, (double) (srcH - 1), (dy + 0.5) * srcH / dstH - 0.5);
        const int y0 = (int) sy, y1 = juce::jmin(srcH - 1, y0 + 1), fy = (int) ((sy - y0) * 256.0);
        const uint8_t* r0 = src + (size_t) y0 * (size_t) srcPitch;
        const uint8_t* r1 = src + (size_t) y1 * (size_t) srcPitch;
        uint8_t* out = dst + (size_t) dy * (size_t) dstPitch;
        for (int dx = 0; dx < dstW; ++dx)
        {
            const int a = x0[(size_t) dx] * channels, b = x1[(size_t) dx] * channels, f = fx[(size_t) dx];
            for (int c = 0; c < channels; ++c)
            {
                const int top = r0[a + c] * (256 - f) + r0[b + c] * f;
                const int bot = r1[a + c] * (256 - f) + r1[b + c] * f;
                out[dx * channels + c] = (uint8_t) ((top * (256 - fy) + bot * fy + 32768) >> 16);
            }
        }
    }
}

BoxKey boxAt(const std::vector<BoxKey>& keys, double t)
{
    BoxKey b;
    b.visible = false;
    for (const auto& k : keys)
    {
        if (k.t > t) break;
        b = k;
    }
    return b;
}

juce::Rectangle<int> boxInPicture(juce::Rectangle<float> r, double previewAspect, int camW, int camH)
{
    if (camW < 16 || camH < 16 || previewAspect <= 0.0) return {};
    // where the picture is in the preview (fitted, centred), in preview units 0..1
    const double camAspect = (double) camW / (double) camH;
    double px = 0.0, py = 0.0, pw = 1.0, ph = 1.0;
    if (camAspect > previewAspect) { ph = previewAspect / camAspect; py = (1.0 - ph) * 0.5; }
    else { pw = camAspect / previewAspect; px = (1.0 - pw) * 0.5; }
    double bw = juce::jlimit(0.0, 1.0, (double) r.getWidth() / pw), bh = juce::jlimit(0.0, 1.0, (double) r.getHeight() / ph);
    double bx = ((double) r.getX() - px) / pw, by = ((double) r.getY() - py) / ph;
    bx = juce::jlimit(0.0, 1.0 - bw, bx); // inside the picture (moved, not cut)
    by = juce::jlimit(0.0, 1.0 - bh, by);
    auto even = [](double v) { return (int) std::floor(v * 0.5) * 2; };
    int x = even(bx * camW), y = even(by * camH), w = even(bw * camW), h = even(bh * camH);
    w = std::min(w, (camW & ~1) - x);
    h = std::min(h, (camH & ~1) - y);
    if (w < 16 || h < 16) return {};
    return { x, y, w, h };
}

juce::Rectangle<int> fitInBox(juce::Rectangle<int> box, int srcW, int srcH)
{
    return fitInArea(box.reduced(2), srcW, srcH);
}

juce::Rectangle<int> fitInArea(juce::Rectangle<int> inner, int srcW, int srcH)
{
    if (srcW <= 0 || srcH <= 0 || inner.getWidth() < 4 || inner.getHeight() < 4) return {};
    const double s = std::min((double) inner.getWidth() / srcW, (double) inner.getHeight() / srcH);
    int w = ((int) std::floor(srcW * s)) & ~1, h = ((int) std::floor(srcH * s)) & ~1;
    int x = (inner.getX() + (inner.getWidth() - w) / 2) & ~1, y = (inner.getY() + (inner.getHeight() - h) / 2) & ~1;
    w = std::min(w, inner.getRight() - x) & ~1;
    h = std::min(h, inner.getBottom() - y) & ~1;
    if (w < 2 || h < 2) return {};
    return { x, y, w, h };
}

void scalePlane(const uint8_t* src, int srcW, int srcH, int srcPitch, uint8_t* dst, int dstW, int dstH, int dstPitch, int channels)
{
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    std::vector<int> x0((size_t) dstW), x1((size_t) dstW);
    for (int dx = 0; dx < dstW; ++dx)
    {
        x0[(size_t) dx] = (int) ((juce::int64) dx * srcW / dstW);
        x1[(size_t) dx] = std::max(x0[(size_t) dx] + 1, (int) ((juce::int64) (dx + 1) * srcW / dstW));
    }
    for (int dy = 0; dy < dstH; ++dy)
    {
        const int y0 = (int) ((juce::int64) dy * srcH / dstH);
        const int y1 = std::max(y0 + 1, (int) ((juce::int64) (dy + 1) * srcH / dstH));
        uint8_t* out = dst + (size_t) dy * (size_t) dstPitch;
        for (int dx = 0; dx < dstW; ++dx)
        {
            unsigned sum[4] = { 0, 0, 0, 0 };
            const int a = x0[(size_t) dx], b = x1[(size_t) dx];
            for (int sy = y0; sy < y1; ++sy)
            {
                const uint8_t* row = src + (size_t) sy * (size_t) srcPitch;
                for (int sx = a; sx < b; ++sx)
                    for (int c = 0; c < channels; ++c) sum[c] += row[sx * channels + c];
            }
            const unsigned n = (unsigned) ((y1 - y0) * (b - a));
            for (int c = 0; c < channels; ++c) out[dx * channels + c] = (uint8_t) ((sum[c] + n / 2) / n);
        }
    }
}

void bgraToNv12(const uint8_t* bgra, int w, int h, int pitch, uint8_t* y, int yPitch, uint8_t* uv, int uvPitch)
{
    for (int r = 0; r < h; ++r)
    {
        const uint8_t* p = bgra + (size_t) r * (size_t) pitch;
        uint8_t* yo = y + (size_t) r * (size_t) yPitch;
        for (int c = 0; c < w; ++c)
        {
            const int B = p[c * 4], G = p[c * 4 + 1], R = p[c * 4 + 2];
            yo[c] = (uint8_t) (((66 * R + 129 * G + 25 * B + 128) >> 8) + 16);
        }
    }
    for (int r = 0; r + 1 < h; r += 2)
    {
        const uint8_t* p0 = bgra + (size_t) r * (size_t) pitch;
        const uint8_t* p1 = p0 + pitch;
        uint8_t* o = uv + (size_t) (r / 2) * (size_t) uvPitch;
        for (int c = 0; c + 1 < w; c += 2)
        {
            int B = 0, G = 0, R = 0;
            for (const uint8_t* q : { p0 + c * 4, p0 + c * 4 + 4, p1 + c * 4, p1 + c * 4 + 4 })
            {
                B += q[0];
                G += q[1];
                R += q[2];
            }
            B = (B + 2) / 4;
            G = (G + 2) / 4;
            R = (R + 2) / 4;
            o[c] = (uint8_t) juce::jlimit(0, 255, ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128);
            o[c + 1] = (uint8_t) juce::jlimit(0, 255, ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128);
        }
    }
}

namespace
{
void fillNv12(uint8_t* y, int yPitch, uint8_t* uv, int uvPitch, juce::Rectangle<int> r, uint8_t Y, uint8_t U, uint8_t V)
{
    for (int row = r.getY(); row < r.getBottom(); ++row) std::fill_n(y + (size_t) row * (size_t) yPitch + r.getX(), r.getWidth(), Y);
    for (int row = r.getY() / 2; row < r.getBottom() / 2; ++row)
    {
        uint8_t* o = uv + (size_t) row * (size_t) uvPitch;
        for (int c = r.getX() / 2; c < r.getRight() / 2; ++c) { o[c * 2] = U; o[c * 2 + 1] = V; }
    }
}
} // namespace

void drawBoxNv12(uint8_t* dstY, int dstYPitch, uint8_t* dstUV, int dstUVPitch, int dstW, int dstH,
                 const uint8_t* srcY, int srcYPitch, const uint8_t* srcUV, int srcUVPitch, int srcW, int srcH, juce::Rectangle<int> box)
{
    box = box.getIntersection({ 0, 0, dstW & ~1, dstH & ~1 });
    if (box.getWidth() < 8 || box.getHeight() < 8) return;
    fillNv12(dstY, dstYPitch, dstUV, dstUVPitch, box, 200, 128, 128);          // the frame: light grey
    fillNv12(dstY, dstYPitch, dstUV, dstUVPitch, box.reduced(2), 16, 128, 128); // black around the picture
    const auto pic = fitInBox(box, srcW, srcH);
    if (pic.isEmpty() || srcY == nullptr) return;
    scalePlane(srcY, srcW, srcH, srcYPitch, dstY + (size_t) pic.getY() * (size_t) dstYPitch + pic.getX(), pic.getWidth(), pic.getHeight(), dstYPitch, 1);
    scalePlane(srcUV, srcW / 2, srcH / 2, srcUVPitch, dstUV + (size_t) (pic.getY() / 2) * (size_t) dstUVPitch + pic.getX(),
               pic.getWidth() / 2, pic.getHeight() / 2, dstUVPitch, 2);
}

void drawBoxBgra(uint8_t* dst, int dstW, int dstH, int dstPitch, const uint8_t* src, int srcW, int srcH, int srcPitch, juce::Rectangle<int> box)
{
    box = box.getIntersection({ 0, 0, dstW, dstH });
    if (box.getWidth() < 8 || box.getHeight() < 8) return;
    auto fill = [&](juce::Rectangle<int> r, uint8_t v) {
        for (int row = r.getY(); row < r.getBottom(); ++row)
        {
            uint8_t* o = dst + (size_t) row * (size_t) dstPitch + (size_t) r.getX() * 4;
            for (int c = 0; c < r.getWidth(); ++c) { o[c * 4] = v; o[c * 4 + 1] = v; o[c * 4 + 2] = v; o[c * 4 + 3] = 255; }
        }
    };
    fill(box, 214);
    fill(box.reduced(2), 0);
    const auto pic = fitInBox(box, srcW, srcH);
    if (pic.isEmpty() || src == nullptr) return;
    scalePlane(src, srcW, srcH, srcPitch, dst + (size_t) pic.getY() * (size_t) dstPitch + (size_t) pic.getX() * 4, pic.getWidth(), pic.getHeight(),
               dstPitch, 4);
}

void drawFullNv12(uint8_t* dstY, int dstYPitch, uint8_t* dstUV, int dstUVPitch, int dstW, int dstH,
                  const uint8_t* srcY, int srcYPitch, const uint8_t* srcUV, int srcUVPitch, int srcW, int srcH)
{
    const juce::Rectangle<int> all { 0, 0, dstW & ~1, dstH & ~1 };
    fillNv12(dstY, dstYPitch, dstUV, dstUVPitch, all, 16, 128, 128);
    const auto pic = fitInArea(all, srcW, srcH);
    if (pic.isEmpty() || srcY == nullptr) return;
    scalePlane(srcY, srcW, srcH, srcYPitch, dstY + (size_t) pic.getY() * (size_t) dstYPitch + pic.getX(), pic.getWidth(), pic.getHeight(), dstYPitch, 1);
    scalePlane(srcUV, srcW / 2, srcH / 2, srcUVPitch, dstUV + (size_t) (pic.getY() / 2) * (size_t) dstUVPitch + pic.getX(),
               pic.getWidth() / 2, pic.getHeight() / 2, dstUVPitch, 2);
}

void drawFullBgra(uint8_t* dst, int dstW, int dstH, int dstPitch, const uint8_t* src, int srcW, int srcH, int srcPitch)
{
    for (int row = 0; row < dstH; ++row)
    {
        uint8_t* o = dst + (size_t) row * (size_t) dstPitch;
        for (int c = 0; c < dstW; ++c) { o[c * 4] = 0; o[c * 4 + 1] = 0; o[c * 4 + 2] = 0; o[c * 4 + 3] = 255; }
    }
    const auto pic = fitInArea({ 0, 0, dstW, dstH }, srcW, srcH);
    if (pic.isEmpty() || src == nullptr) return;
    scalePlane(src, srcW, srcH, srcPitch, dst + (size_t) pic.getY() * (size_t) dstPitch + (size_t) pic.getX() * 4, pic.getWidth(), pic.getHeight(),
               dstPitch, 4);
}

bool composeNv12(const BoxKey& key, double previewAspect, uint8_t* frame, int w, int h,
                 const uint8_t* screenNv12, int screenW, int screenH, std::vector<uint8_t>& scratch)
{
    if (screenNv12 == nullptr || w < 16 || h < 16) return false;
    const auto box = key.visible ? boxInPicture(key.rect, previewAspect, w, h) : juce::Rectangle<int>();
    const size_t ySize = (size_t) w * (size_t) h, sY = (size_t) screenW * (size_t) screenH;
    if (!key.swapped)
    {
        if (box.isEmpty()) return false;
        drawBoxNv12(frame, w, frame + ySize, w, w, h, screenNv12, screenW, screenNv12 + sY, screenW, screenW, screenH, box);
        return true;
    }
    scratch.assign(frame, frame + ySize * 3 / 2); // the camera picture, for the box
    drawFullNv12(frame, w, frame + ySize, w, w, h, screenNv12, screenW, screenNv12 + sY, screenW, screenW, screenH);
    if (!box.isEmpty()) drawBoxNv12(frame, w, frame + ySize, w, w, h, scratch.data(), w, scratch.data() + ySize, w, w, h, box);
    return true;
}

bool composeBgra(const BoxKey& key, double previewAspect, uint8_t* frame, int w, int h, int pitch,
                 const uint8_t* screenBgra, int screenW, int screenH, int screenPitch, std::vector<uint8_t>& scratch)
{
    if (screenBgra == nullptr || w < 16 || h < 16) return false;
    const auto box = key.visible ? boxInPicture(key.rect, previewAspect, w, h) : juce::Rectangle<int>();
    if (!key.swapped)
    {
        if (box.isEmpty()) return false;
        drawBoxBgra(frame, w, h, pitch, screenBgra, screenW, screenH, screenPitch, box);
        return true;
    }
    scratch.resize((size_t) w * (size_t) h * 4);
    for (int y = 0; y < h; ++y) memcpy(scratch.data() + (size_t) y * (size_t) w * 4, frame + (size_t) y * (size_t) pitch, (size_t) w * 4);
    drawFullBgra(frame, w, h, pitch, screenBgra, screenW, screenH, screenPitch);
    if (!box.isEmpty()) drawBoxBgra(frame, w, h, pitch, scratch.data(), w, h, w * 4, box);
    return true;
}

void drawPointer(uint8_t* bgra, int w, int h, int pitch, int x, int y, float scale)
{
    // a classic arrow, 12 x 19 at scale 1: '#' outline, '.' fill
    static const char* shape[] = {
        "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ", "#....#      ", "#.....#     ",
        "#......#    ", "#.......#   ", "#........#  ", "#.........# ", "#......#####", "#...#..#    ", "#..# #..#   ",
        "#.#  #..#   ", "##    #..#  ", "#     #..#  ", "       ##   ", "            " };
    const float s = juce::jlimit(0.5f, 3.0f, scale);
    const int rows = 19, cols = 12;
    for (int py = 0; py < (int) std::ceil(rows * s); ++py)
        for (int px = 0; px < (int) std::ceil(cols * s); ++px)
        {
            const char c = shape[std::min(rows - 1, (int) (py / s))][std::min(cols - 1, (int) (px / s))];
            if (c == ' ') continue;
            const int X = x + px, Y = y + py;
            if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
            uint8_t* o = bgra + (size_t) Y * (size_t) pitch + (size_t) X * 4;
            const uint8_t v = c == '#' ? 20 : 250;
            o[0] = v; o[1] = v; o[2] = v; o[3] = 255;
        }
}

juce::Point<int> recordSize(int w, int h)
{
    if (w <= 0 || h <= 0) return {};
    const double s = std::min({ 1.0, 1920.0 / w, 1080.0 / h }); // up to 1080p (owner 2026-11-12: SWAP needs the detail)
    // multiples of 16 (H.264's blocks): a decoder then has no padded rows to get wrong (0.9.20: 1080 -> 1088 padding
    // shifted the colours of the screen in the saved video); the shape changes by < 1 %
    return { std::max(16, (int) std::lround(w * s) & ~15), std::max(16, (int) std::lround(h * s) & ~15) };
}
} // namespace ampsurd::screen

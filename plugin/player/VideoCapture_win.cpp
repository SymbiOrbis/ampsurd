// VideoCapture on Windows (owner 2026-11-26: a Canon EOS R50 in its USB webcam mode was listed but "could not be
// opened"; OBS Virtual Camera the same).
//
// Why the old way failed: JUCE's DirectShow camera (juce_CameraDevice_windows.h) only accepts a camera that offers
// a size inside the range it is given (AMPSURD asked for 320 x 240 .. 1280 x 720). A camera that offers 1920 x 1080
// only (the R50's USB webcam mode; OBS Virtual Camera at a 1080p canvas) has no such size, JUCE gives up before
// the camera is touched and returns nothing - no error code - and AMPSURD guessed "in use / no permission".
//
// Now:
//   * The list comes from Media Foundation (the camera API that the Windows Camera app uses): each camera is opened
//     by its device path (symbolic link), not by its position in a list. Cameras that only DirectShow knows
//     (virtual cameras such as OBS Virtual Camera) are added from JUCE's DirectShow list.
//   * Media Foundation: every format the camera advertises is read (and written to the log); one is chosen
//     automatically - smooth (>= 24 fps) first, then the largest size up to 1280 x 720 (what AMPSURD used so far),
//     else the smallest larger one (e.g. 1920 x 1080). MJPEG / YUY2 / NV12 ... are converted by Media Foundation
//     for the preview. The camera counts as open only when its first live picture arrived; a format that fails is
//     followed by the next one. Permission / in-use errors are recognised by their codes and named; anything else
//     says that the camera could not be started (the code is in the log).
//   * Video takes: written by this code (Media Foundation) as WMV at up to 1280 x 720 like before (so the camera
//     window's playback and the MP4 mux read it as before); WMV unavailable -> H.264 MP4.
//   * DirectShow (JUCE) is the fallback: for DirectShow-only cameras and when Media Foundation cannot start a camera
//     for a reason other than permission / in use. The size range is widened step by step (1280 x 720, 1920 x 1080,
//     any), the camera's DirectShow formats are written to the log, and it counts as open only with a picture.
#include "VideoCapture.h"
#include "ScreenOverlay.h"

#include <juce_video/juce_video.h>

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
constexpr DWORD kVideo = (DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM;
constexpr int kRecMaxW = 1280, kRecMaxH = 720; // video takes: the size used so far (CPU next to the amps)

juce::String hex(HRESULT hr) { return "0x" + juce::String::toHexString((juce::uint32) hr).paddedLeft('0', 8).toUpperCase(); }

// "MJPG", "YUY2", "NV12", "RGB32" ...
juce::String formatName(const GUID& g)
{
    if (g == MFVideoFormat_RGB32) return "RGB32";
    if (g == MFVideoFormat_ARGB32) return "ARGB32";
    if (g == MFVideoFormat_RGB24) return "RGB24";
    if (g == MEDIASUBTYPE_RGB24) return "RGB24";
    if (g == MEDIASUBTYPE_RGB32) return "RGB32";
    const auto d = (juce::uint32) g.Data1;
    if (d > 0xff)
    {
        juce::String s;
        for (int i = 0; i < 4; ++i)
        {
            const char c = (char) ((d >> (8 * i)) & 0xff);
            s << juce::String::charToString(c >= 32 && c < 127 ? (juce::juce_wchar) c : (juce::juce_wchar) '?');
        }
        return s.trim();
    }
    return "format " + juce::String((int) d);
}

enum class Cause { none, permission, inUse, gone, other };

// Only codes whose meaning is documented get a cause - everything else is "could not be started".
Cause classify(HRESULT hr)
{
    if (SUCCEEDED(hr)) return Cause::none;
    if (hr == E_ACCESSDENIED) return Cause::permission; // Settings > Privacy & security > Camera
    if (hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) || hr == HRESULT_FROM_WIN32(ERROR_BUSY)
        || hr == HRESULT_FROM_WIN32(ERROR_DEVICE_IN_USE) || hr == MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED
        || hr == MF_E_HW_MFT_FAILED_START_STREAMING)
        return Cause::inUse;
    if (hr == MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED || hr == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED)
        || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        return Cause::gone;
    return Cause::other;
}

juce::String messageFor(Cause c)
{
    switch (c)
    {
        case Cause::permission:
            return "Windows does not let AMPSURD use the camera. Turn on Settings > Privacy & security > Camera > "
                   "\"Let desktop apps access your camera\", then choose the camera again.";
        case Cause::inUse:
            return "The camera is in use by another app. Close the other app (or switch its camera off), then choose "
                   "the camera again.";
        case Cause::gone:
            return "The camera is no longer connected. Connect it and open the camera window again.";
        case Cause::none:
        case Cause::other:
        default:
            return "The camera could not be started (the log has the details).";
    }
}

// one camera of the picker
struct Device
{
    juce::String shown;          // the name in the picker (unique)
    juce::String name;           // the camera's own name
    juce::String link;           // Media Foundation device path; empty = DirectShow only
    juce::String dsName;         // DirectShow name (empty = not known to DirectShow)
    int dsOccurrence = 0;        // the n-th DirectShow camera with that name
};

std::vector<Device>& deviceList()
{
    static std::vector<Device> list;
    return list;
}

juce::String& deviceListLog()
{
    static juce::String s;
    return s;
}

struct MfScope
{
    HRESULT hr;
    MfScope() : hr(MFStartup(MF_VERSION)) {}
    ~MfScope() { if (SUCCEEDED(hr)) MFShutdown(); }
};

juce::String getString(IMFAttributes* a, const GUID& key)
{
    WCHAR* s = nullptr;
    UINT32 len = 0;
    if (FAILED(a->GetAllocatedString(key, &s, &len)) || s == nullptr) return {};
    juce::String r(s);
    CoTaskMemFree(s);
    return r;
}

std::vector<Device> listCameras(juce::String& log)
{
    std::vector<Device> list;
    {
        MfScope mf;
        ComPtr<IMFAttributes> a;
        IMFActivate** acts = nullptr;
        UINT32 n = 0;
        HRESULT hr = MFCreateAttributes(&a, 1);
        if (SUCCEEDED(hr)) hr = a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(a.Get(), &acts, &n);
        if (FAILED(hr)) log << "Media Foundation camera list failed (" << hex(hr) << "). ";
        for (UINT32 i = 0; i < n; ++i)
        {
            Device d;
            d.name = getString(acts[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            d.link = getString(acts[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            if (d.name.isEmpty()) d.name = "Camera " + juce::String((int) i + 1);
            if (d.link.isNotEmpty()) list.push_back(d);
            acts[i]->Release();
        }
        if (acts != nullptr) CoTaskMemFree(acts);
    }
    // DirectShow: the same cameras (matched by name) + DirectShow-only ones (virtual cameras)
    const auto ds = juce::CameraDevice::getAvailableDevices();
    std::vector<bool> claimed(list.size(), false);
    for (int i = 0; i < ds.size(); ++i)
    {
        int occurrence = 0;
        for (int j = 0; j < i; ++j)
            if (ds[j] == ds[i]) ++occurrence;
        bool matched = false;
        for (size_t k = 0; k < list.size() && !matched; ++k)
            if (!claimed[k] && list[k].name == ds[i])
            {
                claimed[k] = true;
                list[k].dsName = ds[i];
                list[k].dsOccurrence = occurrence;
                matched = true;
            }
        if (!matched)
        {
            Device d;
            d.name = ds[i];
            d.dsName = ds[i];
            d.dsOccurrence = occurrence;
            list.push_back(d);
        }
    }
    // unique names in the picker (two cameras of the same model)
    for (size_t i = 0; i < list.size(); ++i)
    {
        int same = 0;
        for (size_t j = 0; j < i; ++j)
            if (list[j].name == list[i].name) ++same;
        list[i].shown = same == 0 ? list[i].name : list[i].name + " (" + juce::String(same + 1) + ")";
    }
    log << "Cameras: ";
    for (size_t i = 0; i < list.size(); ++i)
        log << (i > 0 ? "; " : "") << "\"" << list[i].shown << "\" "
            << (list[i].link.isNotEmpty() ? juce::String(list[i].dsName.isNotEmpty() ? "[Media Foundation + DirectShow]" : "[Media Foundation]")
                                          : juce::String("[DirectShow only]"));
    if (list.empty()) log << "none";
    return list;
}

//==============================================================================
// the newest picture, shared with the preview (it may outlive the camera)
struct FrameStore
{
    std::mutex lock;
    juce::Image latest;
    int serial = 0;
};

class FramePreview final : public juce::Component, private juce::Timer
{
public:
    explicit FramePreview(std::shared_ptr<FrameStore> s) : store(std::move(s))
    {
        setOpaque(true);
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
        juce::Image im;
        {
            std::lock_guard<std::mutex> l(store->lock);
            im = store->latest;
        }
        if (im.isValid())
        {
            g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
            g.drawImage(im, getLocalBounds().toFloat(), juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour(juce::Colours::grey);
            g.drawText("Starting the camera...", getLocalBounds(), juce::Justification::centred, false);
        }
    }

private:
    void timerCallback() override
    {
        int s;
        {
            std::lock_guard<std::mutex> l(store->lock);
            s = store->serial;
        }
        if (s != shown) { shown = s; repaint(); }
    }

    std::shared_ptr<FrameStore> store;
    int shown = -1;
};

//==============================================================================
struct Format
{
    ComPtr<IMFMediaType> type;
    GUID subtype {};
    UINT32 w = 0, h = 0, fpsN = 0, fpsD = 1;
    double fps() const { return fpsD > 0 ? (double) fpsN / fpsD : 0.0; }
    juce::String describe() const
    {
        return formatName(subtype) + " " + juce::String((int) w) + "x" + juce::String((int) h) + " "
             + juce::String(fps(), fps() == std::floor(fps()) ? 0 : 2) + " fps";
    }
};

int subtypeRank(const GUID& g)
{
    if (g == MFVideoFormat_NV12) return 0; // no decoding, cheap conversion
    if (g == MFVideoFormat_YUY2) return 1;
    if (g == MFVideoFormat_MJPG) return 2; // decoding costs some CPU
    if (g == MFVideoFormat_RGB32 || g == MFVideoFormat_RGB24) return 3;
    return 4;
}

// The order in which the formats are tried (nothing camera specific - only what the camera advertises)
void rankFormats(std::vector<Format>& f)
{
    auto sizeClass = [](const Format& x) {
        if (x.w >= 320 && x.h >= 240 && x.w <= (UINT32) kRecMaxW && x.h <= (UINT32) kRecMaxH) return 0; // as before
        if (x.w > (UINT32) kRecMaxW || x.h > (UINT32) kRecMaxH) return 1;                                // e.g. 1080p only
        return 2;                                                                                      // tiny
    };
    std::stable_sort(f.begin(), f.end(), [&](const Format& a, const Format& b) {
        const bool sa = a.fps() >= 23.5, sb = b.fps() >= 23.5;
        if (sa != sb) return sa;
        const int ca = sizeClass(a), cb = sizeClass(b);
        if (ca != cb) return ca < cb;
        const double aa = (double) a.w * a.h, ab = (double) b.w * b.h;
        if (aa != ab) return ca == 1 ? aa < ab : aa > ab; // larger up to 720p, else the smallest larger one
        const double fa = std::abs(a.fps() - 30.0), fb = std::abs(b.fps() - 30.0);
        if (std::abs(fa - fb) > 0.5) return fa < fb;
        return subtypeRank(a.subtype) < subtypeRank(b.subtype);
    });
}

//==============================================================================
// Media Foundation camera: its own thread opens the camera, reads pictures, writes video takes.
class MfCamera final : public juce::Thread
{
public:
    struct Result { bool ok = false; Cause cause = Cause::other; juce::String message; };

    MfCamera(VideoCapture& o, juce::String deviceLink, juce::String deviceName)
        : juce::Thread("AMPSURD camera"), owner(o), link(std::move(deviceLink)), name(std::move(deviceName)) {}

    ~MfCamera() override { stop(); }

    std::shared_ptr<FrameStore> frames = std::make_shared<FrameStore>();

    // waits for the first live picture (or the reason there is none)
    Result start(int timeoutMs)
    {
        startThread();
        if (!started.wait(timeoutMs))
        {
            info("no picture after " + juce::String(timeoutMs / 1000) + " s - given up");
            Result r;
            r.message = "The camera could not be started: it sent no picture.";
            return r;
        }
        std::lock_guard<std::mutex> l(resultLock);
        return result;
    }

    void stop()
    {
        signalThreadShouldExit();
        if (isThreadRunning() && !waitForThreadToExit(1500))
        {
            // a camera that sends nothing blocks ReadSample: shutting the source down ends it
            {
                std::lock_guard<std::mutex> l(sourceLock);
                if (source != nullptr) source->Shutdown();
            }
            if (!waitForThreadToExit(4000))
            {
                info("the camera thread did not end - stopped by force");
                stopThread(1000);
            }
        }
    }

    juce::String beginRecording(const juce::File& folder, const juce::String& baseName)
    {
        if (!isThreadRunning()) return "The camera is not running.";
        recStarted.reset();
        {
            std::lock_guard<std::mutex> l(reqLock);
            recFolder = folder;
            recBase = baseName;
            recStartPending = true;
            recStopPending = false;
            recError.clear();
        }
        if (!recStarted.wait(3000))
        {
            std::lock_guard<std::mutex> l(reqLock);
            recStartPending = false;
            return "The camera sends no picture - the video take could not start.";
        }
        std::lock_guard<std::mutex> l(reqLock);
        return recError;
    }

    juce::int64 endRecording(juce::File& out)
    {
        recStopped.reset();
        {
            std::lock_guard<std::mutex> l(reqLock);
            recStopPending = true;
        }
        if (isThreadRunning()) recStopped.wait(8000);
        std::lock_guard<std::mutex> l(reqLock);
        out = writtenFile;
        return firstFrameMs;
    }

private:
    void info(const juce::String& s) { if (owner.onInfo) owner.onInfo("Camera \"" + name + "\": " + s); }

    void finishStart(const Result& r)
    {
        {
            std::lock_guard<std::mutex> l(resultLock);
            result = r;
        }
        started.signal();
    }

    Result fail(HRESULT hr, const juce::String& what)
    {
        Result r;
        r.cause = classify(hr);
        r.message = messageFor(r.cause);
        info(what + " failed: " + hex(hr));
        return r;
    }

    HRESULT createReader()
    {
        reader.Reset();
        {
            std::lock_guard<std::mutex> l(sourceLock);
            if (source != nullptr) { source->Shutdown(); source.Reset(); }
        }
        ComPtr<IMFAttributes> a;
        HRESULT hr = MFCreateAttributes(&a, 2);
        if (SUCCEEDED(hr)) hr = a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(hr)) hr = a->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link.toWideCharPointer());
        ComPtr<IMFMediaSource> src;
        if (SUCCEEDED(hr)) hr = MFCreateDeviceSource(a.Get(), &src);
        if (FAILED(hr)) { lastStep = "opening the camera (MFCreateDeviceSource)"; return hr; }
        {
            std::lock_guard<std::mutex> l(sourceLock);
            source = src;
        }
        ComPtr<IMFAttributes> ra;
        hr = MFCreateAttributes(&ra, 2);
        // the reader converts whatever the camera sends (MJPEG, YUY2, NV12 ...) into RGB32 for the preview
        if (SUCCEEDED(hr)) hr = ra->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(src.Get(), ra.Get(), &reader);
        if (FAILED(hr)) { lastStep = "creating the source reader"; return hr; }
        reader->SetStreamSelection((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
        hr = reader->SetStreamSelection(kVideo, TRUE);
        if (FAILED(hr)) lastStep = "selecting the video stream";
        return hr;
    }

    std::vector<Format> nativeFormats()
    {
        std::vector<Format> list;
        for (DWORD i = 0; i < 512; ++i)
        {
            Format f;
            if (FAILED(reader->GetNativeMediaType(kVideo, i, &f.type))) break;
            f.type->GetGUID(MF_MT_SUBTYPE, &f.subtype);
            MFGetAttributeSize(f.type.Get(), MF_MT_FRAME_SIZE, &f.w, &f.h);
            if (FAILED(MFGetAttributeRatio(f.type.Get(), MF_MT_FRAME_RATE, &f.fpsN, &f.fpsD)) || f.fpsD == 0) { f.fpsN = 0; f.fpsD = 1; }
            list.push_back(f);
        }
        return list;
    }

    // the chosen native format + RGB32 out; reads the size and row layout of what arrives
    HRESULT select(const Format* f)
    {
        HRESULT hr = S_OK;
        if (f != nullptr)
        {
            hr = reader->SetCurrentMediaType(kVideo, nullptr, f->type.Get());
            if (FAILED(hr)) { lastStep = "choosing " + f->describe(); return hr; }
        }
        ComPtr<IMFMediaType> out;
        hr = MFCreateMediaType(&out);
        if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(kVideo, nullptr, out.Get());
        if (FAILED(hr)) { lastStep = "converting " + (f != nullptr ? f->describe() : juce::String("the camera's format")) + " to RGB32"; return hr; }
        return readCurrentType();
    }

    HRESULT readCurrentType()
    {
        ComPtr<IMFMediaType> cur;
        HRESULT hr = reader->GetCurrentMediaType(kVideo, &cur);
        if (FAILED(hr)) { lastStep = "reading the output format"; return hr; }
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &w, &h);
        width = (int) w;
        height = (int) h;
        UINT32 s = 0;
        stride = SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &s)) ? (LONG) (INT32) s : (LONG) (width * 4);
        UINT32 n = 0, d = 1;
        if (SUCCEEDED(MFGetAttributeRatio(cur.Get(), MF_MT_FRAME_RATE, &n, &d)) && n > 0 && d > 0) { fpsN = n; fpsD = d; }
        if (width <= 0 || height <= 0) { lastStep = "reading the picture size"; return E_FAIL; }
        return S_OK;
    }

    // one picture: hr = the reader's result; true = a picture arrived
    bool readOne(HRESULT& hr, LONGLONG& ts)
    {
        for (int tries = 0; tries < 200 && !threadShouldExit(); ++tries)
        {
            DWORD stream = 0, flags = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(kVideo, 0, &stream, &flags, &ts, &sample);
            if (FAILED(hr)) return false;
            if ((flags & MF_SOURCE_READERF_ERROR) != 0) { hr = E_FAIL; return false; }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) { hr = MF_E_END_OF_STREAM; return false; }
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0)
            {
                hr = readCurrentType();
                if (FAILED(hr)) return false;
                info("the format changed: " + juce::String(width) + "x" + juce::String(height));
            }
            if (sample != nullptr && toImage(sample.Get())) return true;
        }
        if (SUCCEEDED(hr)) hr = E_FAIL;
        return false;
    }

    bool toImage(IMFSample* sample)
    {
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->GetBufferByIndex(0, &buf)) && FAILED(sample->ConvertToContiguousBuffer(&buf))) return false;
        // a picture nobody is drawing (the preview holds the newest one)
        juce::Image* dst = nullptr;
        for (auto& p : pool)
            if (!p.isValid() || p.getWidth() != width || p.getHeight() != height || p.getReferenceCount() <= 1) { dst = &p; break; }
        if (dst == nullptr) return false;
        if (!dst->isValid() || dst->getWidth() != width || dst->getHeight() != height)
            *dst = juce::Image(juce::Image::ARGB, width, height, false, juce::SoftwareImageType());
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        bool locked2d = false, locked = false;
        ComPtr<IMF2DBuffer> b2;
        if (SUCCEEDED(buf.As(&b2)) && SUCCEEDED(b2->Lock2D(&scan0, &pitch))) locked2d = true;
        else
        {
            BYTE* data = nullptr;
            DWORD maxLen = 0, len = 0;
            if (FAILED(buf->Lock(&data, &maxLen, &len))) return false;
            locked = true;
            const LONG absStride = std::abs(stride);
            if ((DWORD) absStride * (DWORD) height > len) { buf->Unlock(); return false; }
            pitch = stride;
            scan0 = stride < 0 ? data + (size_t) absStride * (size_t) (height - 1) : data;
        }
        {
            juce::Image::BitmapData bd(*dst, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < height; ++y)
            {
                auto* row = reinterpret_cast<juce::uint32*>(bd.getLinePointer(y));
                memcpy(row, scan0 + (ptrdiff_t) y * pitch, (size_t) width * 4);
                for (int x = 0; x < width; ++x) row[x] |= 0xff000000u; // RGB32 has no alpha: opaque
            }
        }
        if (locked2d) b2->Unlock2D();
        if (locked) buf->Unlock();
        current = *dst;
        {
            std::lock_guard<std::mutex> l(frames->lock);
            frames->latest = *dst;
            ++frames->serial;
        }
        return true;
    }

    //--------------------------------------------------------------------------- video takes
    HRESULT openWriter(const juce::File& f, bool wmv)
    {
        writer.Reset();
        ComPtr<IMFAttributes> wa;
        MFCreateAttributes(&wa, 1);
        wa->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        HRESULT hr = MFCreateSinkWriterFromURL(f.getFullPathName().toWideCharPointer(), nullptr, wa.Get(), &writer);
        if (FAILED(hr)) return hr;
        const UINT32 n = fpsN > 0 ? fpsN : 30, d = fpsN > 0 ? fpsD : 1;
        ComPtr<IMFMediaType> outT, inT;
        MFCreateMediaType(&outT);
        outT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outT->SetGUID(MF_MT_SUBTYPE, wmv ? MFVideoFormat_WMV3 : MFVideoFormat_H264);
        outT->SetUINT32(MF_MT_AVG_BITRATE, (UINT32) juce::jlimit(1500000, 6000000, recW * recH * 4)); // ~3.7 Mbit/s at 720p
        outT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(outT.Get(), MF_MT_FRAME_SIZE, (UINT32) recW, (UINT32) recH);
        MFSetAttributeRatio(outT.Get(), MF_MT_FRAME_RATE, n, d);
        MFSetAttributeRatio(outT.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->AddStream(outT.Get(), &writerStream);
        if (FAILED(hr)) return hr;
        MFCreateMediaType(&inT);
        inT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        inT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(inT.Get(), MF_MT_FRAME_SIZE, (UINT32) recW, (UINT32) recH);
        MFSetAttributeRatio(inT.Get(), MF_MT_FRAME_RATE, n, d);
        MFSetAttributeRatio(inT.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->SetInputMediaType(writerStream, inT.Get(), nullptr);
        if (FAILED(hr)) return hr;
        return writer->BeginWriting();
    }

    void startTake()
    {
        // up to 1280 x 720, the camera's proportions, even sizes
        const double s = juce::jmin(1.0, (double) kRecMaxW / width, (double) kRecMaxH / height);
        recW = juce::jmax(2, (int) std::lround(width * s) & ~1);
        recH = juce::jmax(2, (int) std::lround(height * s) & ~1);
        juce::File f = recFolder.getNonexistentChildFile(recBase, ".wmv");
        HRESULT hr = openWriter(f, true);
        if (FAILED(hr))
        {
            writer.Reset();
            f.deleteFile();
            const HRESULT wmvHr = hr;
            f = recFolder.getNonexistentChildFile(recBase, ".mp4");
            hr = openWriter(f, false);
            if (SUCCEEDED(hr)) info("WMV writer failed (" + hex(wmvHr) + ") - the take is written as H.264 MP4");
        }
        std::lock_guard<std::mutex> l(reqLock);
        if (SUCCEEDED(hr))
        {
            writing = true;
            writtenFile = f;
            firstTs = -1;
            lastSampleTime = -1;
            framesWritten = 0;
            firstFrameMs = 0;
            recError.clear();
            info("video take: " + f.getFileName() + " at " + juce::String(recW) + "x" + juce::String(recH));
        }
        else
        {
            writer.Reset();
            f.deleteFile();
            writtenFile = juce::File();
            recError = "The video take could not start: the video encoder failed (" + hex(hr) + ").";
            info(recError);
        }
    }

    void writeFrame(LONGLONG ts)
    {
        if (writer == nullptr || !current.isValid()) return;
        if (firstTs < 0)
        {
            firstTs = ts;
            // the picture's capture time: the camera's time stamps run on the system clock (MFGetSystemTime)
            const double age = (double) (MFGetSystemTime() - ts) / 10000.0;
            const double ageMs = age >= 0.0 && age < 2000.0 ? age : 0.0;
            std::lock_guard<std::mutex> l(reqLock);
            firstFrameMs = juce::Time::currentTimeMillis() - (juce::int64) std::lround(ageMs);
        }
        const LONGLONG t = ts - firstTs;
        if (t <= lastSampleTime) return;
        const juce::Image::BitmapData bd(current, juce::Image::BitmapData::readOnly);
        const uint8_t* bgra = bd.getLinePointer(0);
        int pitch = bd.lineStride;
        if (recW != width || recH != height)
        {
            scaled.resize((size_t) recW * (size_t) recH * 4);
            ampsurd::screen::scalePlane(bgra, width, height, pitch, scaled.data(), recW, recH, recW * 4, 4);
            bgra = scaled.data();
            pitch = recW * 4;
        }
        const size_t ySize = (size_t) recW * (size_t) recH;
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(MFCreateMemoryBuffer((DWORD) (ySize * 3 / 2), &buf))) return;
        BYTE* data = nullptr;
        if (FAILED(buf->Lock(&data, nullptr, nullptr))) return;
        ampsurd::screen::bgraToNv12(bgra, recW, recH, pitch, data, recW, data + ySize, recW);
        buf->Unlock();
        buf->SetCurrentLength((DWORD) (ySize * 3 / 2));
        ComPtr<IMFSample> s;
        MFCreateSample(&s);
        s->AddBuffer(buf.Get());
        s->SetSampleTime(t);
        s->SetSampleDuration(10000000LL * (fpsN > 0 ? fpsD : 1) / (fpsN > 0 ? fpsN : 30));
        if (SUCCEEDED(writer->WriteSample(writerStream, s.Get()))) { lastSampleTime = t; ++framesWritten; }
    }

    void endTake()
    {
        if (writer != nullptr)
        {
            const HRESULT fin = writer->Finalize();
            writer.Reset();
            info("video take ended: " + juce::String(framesWritten) + " pictures"
                 + (FAILED(fin) ? ", finishing the file failed (" + hex(fin) + ")" : juce::String()));
        }
        std::lock_guard<std::mutex> l(reqLock);
        writing = false;
    }

    void serviceRecording(LONGLONG ts)
    {
        bool startNow = false, stopNow = false;
        {
            std::lock_guard<std::mutex> l(reqLock);
            startNow = recStartPending;
            recStartPending = false;
            stopNow = recStopPending;
            recStopPending = false;
        }
        if (startNow)
        {
            if (writer != nullptr) endTake();
            startTake();
            recStarted.signal();
        }
        if (writer != nullptr) writeFrame(ts);
        if (stopNow)
        {
            endTake();
            recStopped.signal();
        }
    }

    //---------------------------------------------------------------------------
    void run() override
    {
        // below the audio (like the screen recording): a late camera picture is invisible, a late audio block a pop
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const HRESULT mfs = MFStartup(MF_VERSION);
        bool running = false;
        LONGLONG ts = 0;
        if (FAILED(mfs)) finishStart(fail(mfs, "starting Media Foundation"));
        else
        {
            HRESULT hr = createReader();
            if (FAILED(hr)) finishStart(fail(hr, lastStep));
            else
            {
                auto formats = nativeFormats();
                {
                    juce::String s;
                    for (size_t i = 0; i < formats.size(); ++i) s << (i > 0 ? ", " : "") << formats[i].describe();
                    info("advertised formats (" + juce::String((int) formats.size()) + "): " + (s.isEmpty() ? juce::String("none") : s));
                }
                rankFormats(formats);
                const size_t attempts = formats.empty() ? 1 : juce::jmin<size_t>(formats.size(), 6);
                Result r;
                r.message = messageFor(Cause::other);
                for (size_t k = 0; k < attempts && !threadShouldExit(); ++k)
                {
                    const Format* f = formats.empty() ? nullptr : &formats[k];
                    if (k > 0)
                    {
                        hr = createReader(); // a fresh start for the next format
                        if (FAILED(hr)) { r = fail(hr, lastStep); break; }
                    }
                    hr = select(f);
                    if (FAILED(hr)) { r = fail(hr, lastStep); continue; }
                    info("trying " + (f != nullptr ? f->describe() : juce::String("the camera's current format")) + " -> RGB32 "
                         + juce::String(width) + "x" + juce::String(height));
                    if (readOne(hr, ts))
                    {
                        info("live: first picture " + juce::String(width) + "x" + juce::String(height)
                             + (f != nullptr ? " from " + f->describe() : juce::String()));
                        r = Result { true, Cause::none, {} };
                        running = true;
                        break;
                    }
                    r = fail(hr, "the first picture");
                    if (r.cause == Cause::permission || r.cause == Cause::inUse || r.cause == Cause::gone) break; // another format would not help
                }
                finishStart(r);
            }
        }
        if (running)
        {
            serviceRecording(ts);
            while (!threadShouldExit())
            {
                HRESULT hr = S_OK;
                if (readOne(hr, ts))
                {
                    serviceRecording(ts);
                    continue;
                }
                if (threadShouldExit()) break;
                const auto c = classify(hr);
                info("reading failed: " + hex(hr));
                if (owner.onError)
                    owner.onError(c == Cause::gone    ? juce::String("The camera was disconnected.")
                                  : c == Cause::inUse ? juce::String("Another app took the camera.")
                                                      : "The camera stopped sending pictures (" + hex(hr) + ").");
                break; // isOpen() is false from now on: the camera can be chosen again
            }
        }
        if (writer != nullptr) endTake();
        recStopped.signal();
        recStarted.signal();
        current = {};
        for (auto& p : pool) p = {};
        reader.Reset();
        {
            std::lock_guard<std::mutex> l(sourceLock);
            if (source != nullptr) { source->Shutdown(); source.Reset(); }
        }
        if (SUCCEEDED(mfs)) MFShutdown();
        if (SUCCEEDED(com)) CoUninitialize();
        if (!running) started.signal();
    }

    VideoCapture& owner;
    const juce::String link, name;

    juce::WaitableEvent started;
    std::mutex resultLock;
    Result result;

    std::mutex sourceLock;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    juce::String lastStep;
    int width = 0, height = 0;
    LONG stride = 0;
    UINT32 fpsN = 0, fpsD = 1;
    juce::Image pool[3], current;

    // video takes (requests from the message thread under reqLock; the writer belongs to this thread)
    std::mutex reqLock;
    juce::File recFolder, writtenFile;
    juce::String recBase, recError;
    bool recStartPending = false, recStopPending = false, writing = false;
    juce::WaitableEvent recStarted, recStopped;
    juce::int64 firstFrameMs = 0;
    ComPtr<IMFSinkWriter> writer;
    DWORD writerStream = 0;
    int recW = 0, recH = 0, framesWritten = 0;
    LONGLONG firstTs = -1, lastSampleTime = -1;
    std::vector<uint8_t> scaled;
};

//==============================================================================
// DirectShow: what the camera offers (for the log - JUCE does not say why it could not open a camera)
juce::String probeDirectShow(const juce::String& name, int occurrence)
{
    ComPtr<ICreateDevEnum> de;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&de));
    if (FAILED(hr)) return "DirectShow device list failed (" + hex(hr) + ")";
    ComPtr<IEnumMoniker> em;
    hr = de->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &em, 0);
    if (hr != S_OK || em == nullptr) return "no DirectShow cameras";
    ComPtr<IMoniker> m;
    int seen = 0;
    while (em->Next(1, m.ReleaseAndGetAddressOf(), nullptr) == S_OK)
    {
        ComPtr<IPropertyBag> bag;
        if (FAILED(m->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)))) continue;
        VARIANT v;
        VariantInit(&v);
        if (FAILED(bag->Read(L"FriendlyName", &v, nullptr))) continue;
        const juce::String n(v.vt == VT_BSTR ? v.bstrVal : L"");
        VariantClear(&v);
        if (n != name || seen++ != occurrence) continue;
        ComPtr<IBaseFilter> f;
        hr = m->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&f));
        if (FAILED(hr)) return "its DirectShow filter could not be created (" + hex(hr) + ")";
        ComPtr<ICaptureGraphBuilder2> cgb;
        hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cgb));
        if (FAILED(hr)) return "no capture graph builder (" + hex(hr) + ")";
        ComPtr<IAMStreamConfig> sc;
        hr = cgb->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, f.Get(), IID_PPV_ARGS(&sc));
        if (FAILED(hr)) hr = cgb->FindInterface(&PIN_CATEGORY_CAPTURE, nullptr, f.Get(), IID_PPV_ARGS(&sc));
        if (FAILED(hr) || sc == nullptr) return "no stream configuration (" + hex(hr) + ")";
        int count = 0, size = 0;
        sc->GetNumberOfCapabilities(&count, &size);
        juce::String s;
        for (int i = 0; i < count && size == (int) sizeof(VIDEO_STREAM_CONFIG_CAPS); ++i)
        {
            AM_MEDIA_TYPE* mt = nullptr;
            VIDEO_STREAM_CONFIG_CAPS caps {};
            if (FAILED(sc->GetStreamCaps(i, &mt, (BYTE*) &caps)) || mt == nullptr) continue;
            long w = 0, h = 0;
            REFERENCE_TIME frame = 0;
            if (mt->formattype == FORMAT_VideoInfo && mt->cbFormat >= sizeof(VIDEOINFOHEADER) && mt->pbFormat != nullptr)
            {
                const auto* vih = reinterpret_cast<const VIDEOINFOHEADER*>(mt->pbFormat);
                w = vih->bmiHeader.biWidth; h = std::abs(vih->bmiHeader.biHeight); frame = vih->AvgTimePerFrame;
            }
            else if (mt->formattype == FORMAT_VideoInfo2 && mt->cbFormat >= sizeof(VIDEOINFOHEADER2) && mt->pbFormat != nullptr)
            {
                const auto* vih = reinterpret_cast<const VIDEOINFOHEADER2*>(mt->pbFormat);
                w = vih->bmiHeader.biWidth; h = std::abs(vih->bmiHeader.biHeight); frame = vih->AvgTimePerFrame;
            }
            s << (s.isEmpty() ? "" : ", ") << formatName(mt->subtype) << " " << (int) w << "x" << (int) h;
            if (frame > 0) s << " " << juce::String(1.0e7 / (double) frame, 0) << " fps";
            if (mt->cbFormat != 0) CoTaskMemFree(mt->pbFormat);
            if (mt->pUnk != nullptr) mt->pUnk->Release();
            CoTaskMemFree(mt);
        }
        return "DirectShow formats (" + juce::String(count) + "): " + (s.isEmpty() ? juce::String("none") : s);
    }
    return "not found in the DirectShow list";
}

// counts JUCE's pictures (JUCE does not tell whether its graph delivers anything)
struct FrameWatch final : juce::CameraDevice::Listener
{
    void imageReceived(const juce::Image&) override
    {
        if (frames.fetch_add(1) == 0) first.signal();
    }
    std::atomic<int> frames { 0 };
    juce::WaitableEvent first;
};
} // namespace

//==============================================================================
struct VideoCapture::Impl
{
    std::unique_ptr<MfCamera> mf;
    std::unique_ptr<juce::CameraDevice> ds;
    std::unique_ptr<FrameWatch> dsWatch;
    juce::File dsFile;
};

VideoCapture::VideoCapture() : impl(std::make_unique<Impl>()) {}
VideoCapture::~VideoCapture() { close(); }

bool VideoCapture::isSupported() { return true; }

juce::StringArray VideoCapture::cameras()
{
    juce::String log;
    deviceList() = listCameras(log);
    deviceListLog() = log;
    juce::StringArray names;
    for (const auto& d : deviceList()) names.add(d.shown);
    return names;
}

juce::String VideoCapture::open(int index)
{
    close();
    auto info = [this](const juce::String& s) { if (onInfo) onInfo(s); };
    if (deviceList().empty()) cameras();
    if (!juce::isPositiveAndBelow(index, (int) deviceList().size()))
        return "This camera is no longer in the list. Close the camera window and open it again.";
    const Device d = deviceList()[(size_t) index];
    info(deviceListLog());

    Cause mfCause = Cause::other;
    juce::String mfMessage;
    if (d.link.isNotEmpty())
    {
        info("Camera \"" + d.shown + "\": opening with Media Foundation, device path " + d.link);
        impl->mf = std::make_unique<MfCamera>(*this, d.link, d.shown);
        const auto r = impl->mf->start(8000);
        if (r.ok) { openIndex = index; return {}; }
        impl->mf.reset(); // releases the camera: it can be chosen again
        mfCause = r.cause;
        mfMessage = r.message;
        // permission / in use / unplugged: DirectShow would meet the same
        if (r.cause == Cause::permission || r.cause == Cause::inUse || r.cause == Cause::gone || d.dsName.isEmpty())
            return mfMessage;
        info("Camera \"" + d.shown + "\": Media Foundation could not start it - trying DirectShow");
    }

    // DirectShow (JUCE)
    info("Camera \"" + d.shown + "\": " + probeDirectShow(d.dsName, d.dsOccurrence));
    const auto dsNames = juce::CameraDevice::getAvailableDevices();
    int dsIndex = -1;
    for (int i = 0, seen = 0; i < dsNames.size(); ++i)
        if (dsNames[i] == d.dsName && seen++ == d.dsOccurrence) { dsIndex = i; break; }
    if (dsIndex < 0) return mfMessage.isNotEmpty() ? mfMessage : messageFor(Cause::gone);
    // up to 1280 x 720 first (as before); a camera without such a size at its own size
    const int maxSizes[][2] = { { 1280, 720 }, { 1920, 1080 }, { 8192, 8192 } };
    for (const auto& m : maxSizes)
    {
        impl->ds.reset(juce::CameraDevice::openDevice(dsIndex, 1, 1, m[0], m[1], true));
        if (impl->ds != nullptr)
        {
            info("Camera \"" + d.shown + "\": DirectShow graph built with sizes up to " + juce::String(m[0]) + "x" + juce::String(m[1]));
            break;
        }
        info("Camera \"" + d.shown + "\": DirectShow could not build its graph with sizes up to " + juce::String(m[0]) + "x" + juce::String(m[1]));
    }
    if (impl->ds == nullptr) return mfMessage.isNotEmpty() ? mfMessage : messageFor(Cause::other);
    impl->dsWatch = std::make_unique<FrameWatch>();
    impl->ds->addListener(impl->dsWatch.get()); // also starts the graph
    if (!impl->dsWatch->first.wait(6000))
    {
        info("Camera \"" + d.shown + "\": DirectShow sent no picture within 6 s");
        impl->ds->removeListener(impl->dsWatch.get());
        impl->ds.reset();
        impl->dsWatch.reset();
        return "The camera could not be started: it sent no picture.";
    }
    impl->ds->onErrorOccurred = [this](const juce::String& e) {
        if (onInfo) onInfo("Camera error: " + e);
        if (onError) onError(e);
    };
    info("Camera \"" + d.shown + "\": live through DirectShow");
    juce::ignoreUnused(mfCause);
    openIndex = index;
    return {};
}

void VideoCapture::close()
{
    juce::File f;
    if (recording) stopRecording(f);
    impl->mf.reset();
    if (impl->ds != nullptr && impl->dsWatch != nullptr) impl->ds->removeListener(impl->dsWatch.get());
    impl->ds.reset();
    impl->dsWatch.reset();
    openIndex = -1;
}

// (a Media Foundation camera that stopped sending pictures is not open any more)
bool VideoCapture::isOpen() const { return (impl->mf != nullptr && impl->mf->isThreadRunning()) || impl->ds != nullptr; }

std::unique_ptr<juce::Component> VideoCapture::createPreview()
{
    if (impl->mf != nullptr) return std::make_unique<FramePreview>(impl->mf->frames);
    if (impl->ds != nullptr) return std::unique_ptr<juce::Component>(impl->ds->createViewerComponent());
    return {};
}

juce::String VideoCapture::startRecording(const juce::File& folder)
{
    if (!isOpen()) return "No camera is open.";
    folder.createDirectory();
    const auto base = "Video " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S");
    if (impl->mf != nullptr)
    {
        const auto err = impl->mf->beginRecording(folder, base);
        if (err.isNotEmpty()) return err;
        recording = true;
        return {};
    }
    impl->dsFile = folder.getNonexistentChildFile(base, juce::CameraDevice::getFileExtension());
    impl->ds->startRecordingToFile(impl->dsFile, 2); // 2 = high quality (30 frames per second)
    recording = true;
    return {};
}

juce::int64 VideoCapture::stopRecording(juce::File& recordedFile)
{
    recordedFile = juce::File();
    if (!recording) return 0;
    recording = false;
    if (impl->mf != nullptr) return impl->mf->endRecording(recordedFile);
    if (impl->ds == nullptr) return 0;
    impl->ds->stopRecording();
    recordedFile = impl->dsFile;
    return impl->ds->getTimeOfFirstRecordedFrame().toMilliseconds();
}

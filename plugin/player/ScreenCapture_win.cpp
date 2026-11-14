// SCREEN BOX on Windows: DXGI desktop duplication (one monitor) -> scaled to at most 1920 x 1080 -> the box's preview,
// and while recording H.264 into an MP4 with Media Foundation. The mouse pointer is drawn in (desktop duplication
// leaves it out). Windows marked with excludeWindow() are left out of the capture (Windows 10 2004 or newer).
#include "ScreenCapture.h"
#include "ScreenOverlay.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strmif.h>
#include <wrl/client.h>

#include <mutex>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "oleaut32.lib")

// ICodecAPI settings (codecapi.h's values, here so no GUID library is needed)
static const GUID kAVEncNumWorkerThreads = { 0xb0c8bf60, 0x16f7, 0x4951, { 0xa3, 0x0b, 0x1d, 0xb1, 0x60, 0x92, 0x93, 0xd6 } };
static const GUID kAVEncCommonQualityVsSpeed = { 0x98332df8, 0x03cd, 0x476b, { 0x89, 0xfa, 0x3f, 0x9e, 0x44, 0x2d, 0xec, 0x9f } };

using Microsoft::WRL::ComPtr;

namespace
{
struct Output
{
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc {};
};

std::vector<Output> listOutputs()
{
    std::vector<Output> list;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**) factory.GetAddressOf()))) return list;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++a)
    {
        ComPtr<IDXGIOutput> out;
        for (UINT o = 0; adapter->EnumOutputs(o, out.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++o)
        {
            Output e;
            e.adapter = adapter;
            e.output = out;
            out->GetDesc(&e.desc);
            if (e.desc.AttachedToDesktop) list.push_back(e);
        }
    }
    return list;
}

constexpr DWORD kWdaExcludeFromCapture = 0x00000011; // WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+)
} // namespace

struct ScreenCapture::Impl final : public juce::Thread
{
    Impl(ScreenCapture& o, int idx) : juce::Thread("AMPSURD screen"), owner(o), index(idx) {}
    ~Impl() override { stopThread(4000); }

    ScreenCapture& owner;
    const int index;

    // recording requests (message thread) <-> the capture thread
    std::mutex lock;
    juce::File requestFile;      // start: the file to write
    bool startRequested = false, stopRequested = false, stopDone = false;
    juce::int64 firstFrameMs = 0;
    juce::File writtenFile;
    juce::WaitableEvent stopEvent;

    // the capture thread's state
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> dupl;
    ComPtr<ID3D11Texture2D> staging;
    DXGI_OUTPUT_DESC desc {};
    int srcW = 0, srcH = 0, recW = 0, recH = 0;
    std::vector<uint8_t> frame;  // the scaled picture (BGRA, recW x recH)
    bool haveFrame = false;
    std::vector<uint8_t> nv12;
    ComPtr<IMFSinkWriter> writer;
    DWORD stream = 0;
    double recStartMs = 0.0;
    LONGLONG lastTime = -1;
    int framesWritten = 0;
    double startAskedMs = 0.0;   // when REC asked for the recording (it waits up to 3 s for the first picture)

    // the fallback when desktop duplication gives no picture (some graphics drivers, remote desktop, a second
    // graphics card): GDI copies of the screen - slower, but they work everywhere (owner 2026-11-12: the screen
    // was not recorded on his PC, only the camera)
    bool gdi = false;
    HDC gdiScreen = nullptr, gdiMem = nullptr;
    HBITMAP gdiBmp = nullptr;
    HGDIOBJ gdiOld = nullptr;
    void* gdiBits = nullptr;
    double gdiLast = 0.0;

    void freeGdi()
    {
        if (gdiMem != nullptr) { SelectObject(gdiMem, gdiOld); DeleteDC(gdiMem); gdiMem = nullptr; }
        if (gdiBmp != nullptr) { DeleteObject(gdiBmp); gdiBmp = nullptr; }
        if (gdiScreen != nullptr) { ReleaseDC(nullptr, gdiScreen); gdiScreen = nullptr; }
        gdiBits = nullptr;
    }

    bool useGdi()
    {
        freeGdi();
        dupl.Reset();
        auto outs = listOutputs();
        if (index < 0 || index >= (int) outs.size()) return false;
        desc = outs[(size_t) index].desc;
        const auto& r = desc.DesktopCoordinates;
        srcW = (int) (r.right - r.left);
        srcH = (int) (r.bottom - r.top);
        if (srcW < 16 || srcH < 16) return false;
        // lighter; but a recording already running keeps its picture size (the file cannot change it)
        const auto rs = writer != nullptr && recW > 0 ? juce::Point<int>(recW, recH) : ampsurd::screen::recordSize(srcW, srcH, true);
        if (rs.x != recW || rs.y != recH) haveFrame = false;
        recW = rs.x;
        recH = rs.y;
        frame.resize((size_t) recW * (size_t) recH * 4);
        gdiScreen = GetDC(nullptr);
        gdiMem = CreateCompatibleDC(gdiScreen);
        BITMAPINFO bi {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = srcW;
        bi.bmiHeader.biHeight = -srcH; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        gdiBmp = CreateDIBSection(gdiScreen, &bi, DIB_RGB_COLORS, &gdiBits, nullptr, 0);
        if (gdiScreen == nullptr || gdiMem == nullptr || gdiBmp == nullptr || gdiBits == nullptr) { freeGdi(); return false; }
        gdiOld = SelectObject(gdiMem, gdiBmp);
        gdi = true;
        return true;
    }

    bool grabGdi()
    {
        // 10 pictures a second (GDI has no "new picture" signal; each one is copied by the processor)
        const double interval = 1000.0 / ampsurd::screen::kLightFramesPerSecond;
        const double wait = gdiLast + interval - juce::Time::getMillisecondCounterHiRes();
        if (wait > 0.0) juce::Thread::sleep((int) std::ceil(wait));
        gdiLast = juce::Time::getMillisecondCounterHiRes();
        const auto& r = desc.DesktopCoordinates;
        if (!BitBlt(gdiMem, 0, 0, srcW, srcH, gdiScreen, r.left, r.top, SRCCOPY | CAPTUREBLT)) return false;
        GdiFlush();
        ampsurd::screen::scalePlane((const uint8_t*) gdiBits, srcW, srcH, srcW * 4, frame.data(), recW, recH, recW * 4, 4);
        drawCursor();
        haveFrame = true;
        return true;
    }

    void drawCursor()
    {
        CURSORINFO ci { sizeof(CURSORINFO) };
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) != 0)
        {
            const auto& r = desc.DesktopCoordinates;
            const double sx = (double) recW / std::max<LONG>(1, r.right - r.left), sy = (double) recH / std::max<LONG>(1, r.bottom - r.top);
            if (ci.ptScreenPos.x >= r.left && ci.ptScreenPos.x < r.right && ci.ptScreenPos.y >= r.top && ci.ptScreenPos.y < r.bottom)
                ampsurd::screen::drawPointer(frame.data(), recW, recH, recW * 4, (int) ((ci.ptScreenPos.x - r.left) * sx),
                                             (int) ((ci.ptScreenPos.y - r.top) * sy), (float) std::max(0.6, sx * 1.4));
        }
    }

    juce::String setUp()
    {
        dupl.Reset();
        staging.Reset();
        context.Reset();
        device.Reset();
        auto outs = listOutputs();
        if (index < 0 || index >= (int) outs.size()) return "This screen is not there any more.";
        auto& o = outs[(size_t) index];
        desc = o.desc;
        D3D_FEATURE_LEVEL fl;
        HRESULT hr = D3D11CreateDevice(o.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, &device, &fl, &context);
        if (FAILED(hr)) return "The graphics card cannot be used for screen recording.";
        ComPtr<IDXGIOutput1> out1;
        if (FAILED(o.output.As(&out1))) return "Screen recording needs Windows 8 or newer.";
        hr = out1->DuplicateOutput(device.Get(), &dupl);
        if (FAILED(hr)) return "This screen cannot be recorded right now (error 0x" + juce::String::toHexString((juce::uint32) hr) + ").";
        DXGI_OUTDUPL_DESC dd;
        dupl->GetDesc(&dd);
        srcW = (int) dd.ModeDesc.Width;
        srcH = (int) dd.ModeDesc.Height;
        const auto rs = ampsurd::screen::recordSize(srcW, srcH);
        if (rs.x != recW || rs.y != recH) haveFrame = false;
        recW = rs.x;
        recH = rs.y;
        frame.resize((size_t) recW * (size_t) recH * 4);
        D3D11_TEXTURE2D_DESC td {};
        td.Width = (UINT) srcW;
        td.Height = (UINT) srcH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) return "Screen recording: out of graphics memory.";
        return {};
    }

    // the desktop picture -> `frame` (scaled) + the mouse pointer
    bool grab(UINT timeoutMs)
    {
        if (gdi) return grabGdi();
        if (dupl == nullptr) return false;
        DXGI_OUTDUPL_FRAME_INFO info;
        ComPtr<IDXGIResource> res;
        HRESULT hr = dupl->AcquireNextFrame(timeoutMs, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
        if (FAILED(hr))
        {
            // the desktop changed (resolution, a UAC prompt, fullscreen game...): start again
            juce::Thread::sleep(200);
            const auto e = setUp();
            if (e.isNotEmpty()) { juce::Thread::sleep(500); }
            return false;
        }
        bool ok = false;
        ComPtr<ID3D11Texture2D> tex;
        if (info.LastPresentTime.QuadPart != 0 && SUCCEEDED(res.As(&tex)))
        {
            context->CopyResource(staging.Get(), tex.Get());
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m)))
            {
                ampsurd::screen::scalePlane((const uint8_t*) m.pData, srcW, srcH, (int) m.RowPitch, frame.data(), recW, recH, recW * 4, 4);
                context->Unmap(staging.Get(), 0);
                ok = true;
            }
        }
        dupl->ReleaseFrame();
        if (ok)
        {
            drawCursor();
            haveFrame = true;
        }
        return ok;
    }

    int fps() const { return gdi ? ampsurd::screen::kLightFramesPerSecond : ampsurd::screen::kFramesPerSecond; }

    // H.264 into an MP4; the graphics card's encoder first, Windows' own (software) encoder if that fails
    HRESULT openWriter(const juce::File& f)
    {
        HRESULT hr = openWriterWith(f, true);
        if (FAILED(hr))
        {
            writer.Reset();
            f.deleteFile();
            const HRESULT hw = hr;
            hr = openWriterWith(f, false);
            if (SUCCEEDED(hr)) owner.reportInfo("Screen recording: the graphics card's video encoder failed (0x" + juce::String::toHexString((juce::uint32) hw) + "), Windows' own encoder is used.");
        }
        return hr;
    }

    HRESULT openWriterWith(const juce::File& f, bool hardware)
    {
        writer.Reset();
        ComPtr<IMFAttributes> wa;
        MFCreateAttributes(&wa, 1);
        wa->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, hardware ? TRUE : FALSE);
        HRESULT hr = MFCreateSinkWriterFromURL(f.getFullPathName().toWideCharPointer(), nullptr, wa.Get(), &writer);
        if (FAILED(hr)) return hr;
        ComPtr<IMFMediaType> outT, inT;
        MFCreateMediaType(&outT);
        outT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        outT->SetUINT32(MF_MT_AVG_BITRATE, (UINT32) juce::jlimit(3000000, 8000000, recW * recH * 4)); // ~8 Mbit/s at 1080p: text stays sharp
        outT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(outT.Get(), MF_MT_FRAME_SIZE, (UINT32) recW, (UINT32) recH);
        MFSetAttributeRatio(outT.Get(), MF_MT_FRAME_RATE, (UINT32) fps(), 1);
        MFSetAttributeRatio(outT.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->AddStream(outT.Get(), &stream);
        if (FAILED(hr)) return hr;
        MFCreateMediaType(&inT);
        inT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        inT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(inT.Get(), MF_MT_FRAME_SIZE, (UINT32) recW, (UINT32) recH);
        MFSetAttributeRatio(inT.Get(), MF_MT_FRAME_RATE, (UINT32) fps(), 1);
        MFSetAttributeRatio(inT.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->SetInputMediaType(stream, inT.Get(), nullptr);
        if (FAILED(hr)) return hr;
        if (!hardware)
        {
            // Windows' own encoder: few threads and its quicker settings - it shares the processor with the amps
            ComPtr<ICodecAPI> codec;
            if (SUCCEEDED(writer->GetServiceForStream(stream, GUID_NULL, IID_PPV_ARGS(&codec))))
            {
                VARIANT v;
                VariantInit(&v);
                v.vt = VT_UI4;
                v.ulVal = 2;
                codec->SetValue(&kAVEncNumWorkerThreads, &v);
                v.ulVal = 0; // 0 = fastest .. 100 = best quality
                codec->SetValue(&kAVEncCommonQualityVsSpeed, &v);
            }
        }
        return writer->BeginWriting();
    }

    void writeFrame()
    {
        if (writer == nullptr || !haveFrame) return;
        const LONGLONG t = (LONGLONG) ((juce::Time::getMillisecondCounterHiRes() - recStartMs) * 10000.0);
        if (t <= lastTime) return;
        const size_t ySize = (size_t) recW * (size_t) recH;
        nv12.resize(ySize * 3 / 2);
        ampsurd::screen::bgraToNv12(frame.data(), recW, recH, recW * 4, nv12.data(), recW, nv12.data() + ySize, recW);
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(MFCreateMemoryBuffer((DWORD) nv12.size(), &buf))) return;
        BYTE* data = nullptr;
        buf->Lock(&data, nullptr, nullptr);
        memcpy(data, nv12.data(), nv12.size());
        buf->Unlock();
        buf->SetCurrentLength((DWORD) nv12.size());
        ComPtr<IMFSample> s;
        MFCreateSample(&s);
        s->AddBuffer(buf.Get());
        s->SetSampleTime(t);
        s->SetSampleDuration(10000000LL / fps());
        if (SUCCEEDED(writer->WriteSample(stream, s.Get()))) { lastTime = t; ++framesWritten; }
    }

    void run() override
    {
        // below the audio (owner 2026-11-13: pops in video takes): the amps' helper threads come first, the
        // screen waits - a late screen picture is invisible, a late audio block is a pop
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const HRESULT mf = MFStartup(MF_VERSION);
        const auto err = setUp();
        if (err.isNotEmpty())
        {
            if (useGdi()) owner.reportInfo("Screen recording: " + err + " - the slower way (GDI) is used instead.");
            else owner.reportError(err);
        }
        const double openedMs = juce::Time::getMillisecondCounterHiRes();
        double lastShown = 0.0, lastWritten = 0.0;
        while (!threadShouldExit())
        {
            const bool fresh = grab(30);
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double interval = 1000.0 / fps();
            // desktop duplication gave no picture in 2 s: the GDI way
            if (!gdi && !haveFrame && now - openedMs > 2000.0)
            {
                if (useGdi()) owner.reportInfo("Screen recording: the fast way (desktop duplication) gave no picture - the slower way (GDI) is used.");
                else { owner.reportError("This screen gives no picture (neither desktop duplication nor GDI)."); juce::Thread::sleep(500); }
            }
            if (fresh && now - lastShown >= interval)
            {
                lastShown = now;
                owner.publishPreview(ScreenCapture::makePreview(frame.data(), recW, recH, recW * 4));
            }
            {
                std::unique_lock<std::mutex> sl(lock);
                // (waits up to 3 s for the first picture - right after choosing the screen there may be none yet)
                if (startRequested && (haveFrame || now - startAskedMs > 3000.0))
                {
                    startRequested = false;
                    recStartMs = now;
                    lastTime = -1;
                    firstFrameMs = 0;
                    framesWritten = 0;
                    writtenFile = juce::File();
                    const HRESULT hr = haveFrame ? openWriter(requestFile) : E_FAIL;
                    if (SUCCEEDED(hr))
                    {
                        writtenFile = requestFile;
                        firstFrameMs = juce::Time::currentTimeMillis(); // the current picture is written right away
                        writeFrame();
                        lastWritten = now;
                    }
                    else
                    {
                        writer.Reset();
                        owner.reportError(haveFrame ? "The screen could not be recorded: the video encoder failed (0x" + juce::String::toHexString((juce::uint32) hr) + ")."
                                                    : juce::String("The screen could not be recorded: no picture from the screen."));
                    }
                }
                if (stopRequested)
                {
                    stopRequested = false;
                    startRequested = false; // a start still waiting for its first picture: not any more
                    if (writer != nullptr)
                    {
                        writeFrame(); // the screen as it is now: the recording lasts until the end of the take
                        const HRESULT fin = writer->Finalize();
                        writer.Reset();
                        owner.reportInfo("Screen recorded: " + juce::String(framesWritten) + " pictures" + (gdi ? " (GDI)" : "")
                                         + (FAILED(fin) ? ", finishing the file failed (0x" + juce::String::toHexString((juce::uint32) fin) + ")" : juce::String()));
                    }
                    stopDone = true;
                    stopEvent.signal();
                }
            }
            // a new picture at 15 per second, or the same one again at least every second (a still screen)
            if (writer != nullptr && ((fresh && now - lastWritten >= interval * 0.9) || now - lastWritten >= 1000.0))
            {
                writeFrame();
                lastWritten = now;
            }
            if (!fresh) juce::Thread::sleep(5);
        }
        if (writer != nullptr) { writer->Finalize(); writer.Reset(); }
        freeGdi();
        dupl.Reset();
        staging.Reset();
        context.Reset();
        device.Reset();
        if (SUCCEEDED(mf)) MFShutdown();
        if (SUCCEEDED(com)) CoUninitialize();
        stopEvent.signal();
    }
};

ScreenCapture::ScreenCapture() = default;

ScreenCapture::~ScreenCapture()
{
    alive->store(false);
    close();
}

bool ScreenCapture::isSupported()
{
    return !listOutputs().empty();
}

juce::StringArray ScreenCapture::screens()
{
    juce::StringArray names;
    const auto outs = listOutputs();
    for (size_t i = 0; i < outs.size(); ++i)
    {
        const auto& r = outs[i].desc.DesktopCoordinates;
        const bool main = r.left == 0 && r.top == 0;
        names.add("Screen " + juce::String((int) i + 1) + " (" + juce::String(r.right - r.left) + " x " + juce::String(r.bottom - r.top) + ")"
                  + (main ? " - main" : ""));
    }
    return names;
}

juce::String ScreenCapture::open(int index)
{
    close();
    if (index < 0 || index >= (int) listOutputs().size()) return "This screen is not there any more.";
    lastError.clear();
    impl = std::make_unique<Impl>(*this, index);
    impl->startThread();
    openIndex = index;
    return {};
}

void ScreenCapture::close()
{
    if (recording) { juce::File f; stopRecording(f); }
    impl.reset();
    openIndex = -1;
    publishPreview({});
}

bool ScreenCapture::isOpen() const { return impl != nullptr; }

juce::String ScreenCapture::startRecording(const juce::File& folder)
{
    if (impl == nullptr) return "No screen is chosen.";
    folder.createDirectory();
    const auto f = folder.getNonexistentChildFile("Screen " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".mp4");
    {
        std::lock_guard<std::mutex> sl(impl->lock);
        impl->requestFile = f;
        impl->startAskedMs = juce::Time::getMillisecondCounterHiRes();
        impl->startRequested = true;
        impl->stopRequested = false;
        impl->stopDone = false;
    }
    recording = true;
    return {};
}

juce::int64 ScreenCapture::stopRecording(juce::File& recordedFile)
{
    recordedFile = juce::File();
    if (!recording || impl == nullptr) { recording = false; return 0; }
    recording = false;
    impl->stopEvent.reset();
    {
        std::lock_guard<std::mutex> sl(impl->lock);
        impl->stopRequested = true;
    }
    impl->stopEvent.wait(5000); // the file is finished (a moment)
    std::lock_guard<std::mutex> sl(impl->lock);
    if (!impl->stopDone) return 0;
    recordedFile = impl->writtenFile;
    return recordedFile != juce::File() ? impl->firstFrameMs : 0;
}

void ScreenCapture::excludeWindow(juce::Component* w)
{
    if (w == nullptr) return;
    if (auto* peer = w->getPeer())
        SetWindowDisplayAffinity((HWND) peer->getNativeHandle(), kWdaExcludeFromCapture); // older Windows: ignored (fails)
}

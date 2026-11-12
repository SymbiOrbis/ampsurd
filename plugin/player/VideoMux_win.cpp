// Windows: MP4 from the camera file (picture, re-encoded to H.264) and AMPSURD's mix (sound, AAC) with
// Media Foundation (part of Windows: no extra libraries, no codec licences of our own).
#include "VideoMux.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <climits>
#include <memory>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

bool isVideoExportSupported() { return true; }

namespace
{
struct ComScope
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
struct MfScope
{
    HRESULT hr = MFStartup(MF_VERSION);
    ~MfScope() { if (SUCCEEDED(hr)) MFShutdown(); }
};
juce::String failed(const char* what, HRESULT hr)
{
    return juce::String(what) + " failed (error 0x" + juce::String::toHexString((juce::uint32) hr) + ").";
}
constexpr DWORD kVideo = (DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM;
constexpr UINT32 kAudioRate = 48000;
} // namespace

// an NV12 sample -> `out` (stride w, the UV plane right after the Y plane). planeH = the height the decoder's buffer
// really has (it can be padded, e.g. 1088 for 1080).
static bool copyNv12(IMFSample* sample, int w, int h, int planeH, std::vector<uint8_t>& out)
{
    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf))) return false;
    out.resize((size_t) w * (size_t) h * 3 / 2);
    BYTE* base = nullptr;
    LONG pitch = 0;
    ComPtr<IMF2DBuffer> b2;
    ComPtr<IMF2DBuffer2> b22;
    bool locked2d = false;
    DWORD len = 0;
    BYTE* start = nullptr;
    // the buffer's real size tells its real plane height: decoders pad 1080 to 1088 without saying so in the
    // media type (0.9.20: the UV plane read 8 rows too early - a green band on top, the colours shifted down)
    if (SUCCEEDED(buf.As(&b22)) && SUCCEEDED(b22->Lock2DSize(MF2DBuffer_LockFlags_Read, &base, &pitch, &start, &len)) && pitch > 0)
    {
        locked2d = true;
        b2 = b22;
        if (len >= (DWORD) pitch * (DWORD) h * 3 / 2)
        {
            const int ph = (int) ((juce::uint64) len * 2 / 3 / (juce::uint64) pitch) & ~1;
            if (ph >= h && ph <= h + 32) planeH = std::max(planeH, ph);
        }
    }
    else if (SUCCEEDED(buf.As(&b2)) && SUCCEEDED(b2->Lock2D(&base, &pitch)) && pitch > 0)
        locked2d = true;
    else
    {
        if (FAILED(buf->Lock(&base, nullptr, &len))) return false;
        pitch = w;
        if (len >= (DWORD) (w * h * 3 / 2)) planeH = std::max(h, (int) (len * 2 / 3 / (DWORD) w));
    }
    planeH = std::max(planeH, h);
    for (int y = 0; y < h; ++y) memcpy(out.data() + (size_t) y * (size_t) w, base + (size_t) y * (size_t) pitch, (size_t) w);
    const BYTE* uv = base + (size_t) pitch * (size_t) planeH;
    for (int y = 0; y < h / 2; ++y)
        memcpy(out.data() + (size_t) w * (size_t) h + (size_t) y * (size_t) w, uv + (size_t) y * (size_t) pitch, (size_t) w);
    if (locked2d) b2->Unlock2D(); else buf->Unlock();
    return true;
}

// the screen recording, read along with the camera picture: the newest screen frame at a given camera time
namespace
{
struct ScreenReader
{
    ComPtr<IMFSourceReader> reader;
    int w = 0, h = 0, planeH = 0;
    std::vector<uint8_t> frame;   // the current screen picture (NV12, stride w)
    bool have = false, ended = false;
    LONGLONG first = -1;
    ComPtr<IMFSample> next;       // read ahead
    LONGLONG nextTime = 0;

    bool open(const juce::File& f)
    {
        ComPtr<IMFAttributes> ra;
        MFCreateAttributes(&ra, 1);
        ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (FAILED(MFCreateSourceReaderFromURL(f.getFullPathName().toWideCharPointer(), ra.Get(), &reader))) return false;
        reader->SetStreamSelection((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection(kVideo, TRUE);
        ComPtr<IMFMediaType> native;
        if (FAILED(reader->GetNativeMediaType(kVideo, 0, &native))) return false;
        UINT32 nw = 0, nh = 0;
        MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &nw, &nh);
        w = (int) (nw & ~1u);
        h = (int) (nh & ~1u);
        if (w < 16 || h < 16) return false;
        ComPtr<IMFMediaType> t;
        MFCreateMediaType(&t);
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, (UINT32) w, (UINT32) h);
        if (FAILED(reader->SetCurrentMediaType(kVideo, nullptr, t.Get()))) return false;
        planeH = h;
        ComPtr<IMFMediaType> cur;
        if (SUCCEEDED(reader->GetCurrentMediaType(kVideo, &cur)))
        {
            UINT32 cw = 0, ch = 0;
            MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &cw, &ch);
            if ((int) ch >= h) planeH = (int) ch;
        }
        readNext();
        return true;
    }
    void readNext()
    {
        next.Reset();
        for (;;)
        {
            DWORD flags = 0;
            LONGLONG ts = 0;
            ComPtr<IMFSample> s;
            if (FAILED(reader->ReadSample(kVideo, 0, nullptr, &flags, &ts, &s)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) { ended = true; return; }
            if (s == nullptr) continue;
            if (first < 0) first = ts;
            next = s;
            nextTime = ts - first;
            return;
        }
    }
    // the screen picture at screen time t (100 ns from its first frame); false = none yet
    bool at(LONGLONG t)
    {
        while (!ended && next != nullptr && nextTime <= t)
        {
            have = copyNv12(next.Get(), w, h, planeH, frame) || have;
            readNext();
        }
        return have;
    }
};
} // namespace

juce::String muxVideoToMp4(const juce::File& video, const juce::AudioBuffer<float>& audio, double sampleRate, double audioLeadSeconds,
                           const juce::File& out, const std::function<void(float)>& progress, const ampsurd::screen::Overlay* overlay)
{
    ComScope com;
    MfScope mf;
    if (FAILED(mf.hr)) return failed("Starting Media Foundation", mf.hr);

    // 1. the camera file, decoded to NV12 (what the H.264 encoder takes)
    ComPtr<IMFAttributes> ra;
    MFCreateAttributes(&ra, 1);
    ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(video.getFullPathName().toWideCharPointer(), ra.Get(), &reader);
    if (FAILED(hr)) return failed("Reading the camera file", hr);
    reader->SetStreamSelection((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(kVideo, TRUE);
    ComPtr<IMFMediaType> native;
    hr = reader->GetNativeMediaType(kVideo, 0, &native);
    if (FAILED(hr)) return failed("Reading the picture format", hr);
    UINT32 w = 0, h = 0, fn = 30, fd = 1;
    MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &w, &h);
    if (FAILED(MFGetAttributeRatio(native.Get(), MF_MT_FRAME_RATE, &fn, &fd)) || fn == 0 || fd == 0) { fn = 30; fd = 1; }
    // camera WMV files often carry no real frame rate (the ASF time base, 1000 fps, was seen on the owner's
    // camera; the frames come at ~30 fps). The H.264 encoder rejects such rates: use 30 fps for anything
    // outside 5..120 (the frames keep their own time stamps, so the timing stays right).
    if ((double) fn / (double) fd < 5.0 || (double) fn / (double) fd > 120.0) { fn = 30; fd = 1; }
    w &= ~1u; // H.264: even sizes
    h &= ~1u;
    if (w == 0 || h == 0) return "The camera file has no picture.";
    ComPtr<IMFMediaType> nv12;
    MFCreateMediaType(&nv12);
    nv12->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    nv12->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    nv12->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(nv12.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(nv12.Get(), MF_MT_FRAME_RATE, fn, fd);
    MFSetAttributeRatio(nv12.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = reader->SetCurrentMediaType(kVideo, nullptr, nv12.Get());
    if (FAILED(hr)) return failed("Decoding the camera file", hr);
    int camPlaneH = (int) h;
    {
        ComPtr<IMFMediaType> cur;
        if (SUCCEEDED(reader->GetCurrentMediaType(kVideo, &cur)))
        {
            UINT32 cw = 0, ch = 0;
            MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &cw, &ch);
            if (ch >= h) camPlaneH = (int) ch;
        }
    }
    // SCREEN BOX: the screen recording drawn into the camera picture (only when the box is shown at some moment)
    std::unique_ptr<ScreenReader> screen;
    if (overlay != nullptr && overlay->isUsed() && overlay->file.existsAsFile())
    {
        screen = std::make_unique<ScreenReader>();
        if (!screen->open(overlay->file)) screen.reset(); // without the box rather than no video
    }
    std::vector<uint8_t> composed, scratch, big;
    // SWAP somewhere: the saved picture bigger than the camera's (the screen keeps its detail), the camera scaled up
    const auto os = ampsurd::screen::outputSize((int) w, (int) h, screen != nullptr ? overlay : nullptr, screen != nullptr ? screen->h : 0);
    const UINT32 ow = (UINT32) os.x, oh = (UINT32) os.y;
    const bool upscale = screen != nullptr && (ow != w || oh != h);
    if (upscale) big.assign((size_t) ow * oh * 3 / 2, 0);
    LONGLONG duration = 0; // 100 ns units
    {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(reader->GetPresentationAttribute((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)))
            duration = (LONGLONG) v.uhVal.QuadPart;
        PropVariantClear(&v);
    }

    // 2. the MP4 writer: H.264 + AAC
    out.deleteFile();
    ComPtr<IMFAttributes> wa;
    MFCreateAttributes(&wa, 1);
    wa->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    ComPtr<IMFSinkWriter> writer;
    hr = MFCreateSinkWriterFromURL(out.getFullPathName().toWideCharPointer(), nullptr, wa.Get(), &writer);
    if (FAILED(hr)) return failed("Creating the MP4 file", hr);

    ComPtr<IMFMediaType> vOut;
    MFCreateMediaType(&vOut);
    vOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    vOut->SetUINT32(MF_MT_AVG_BITRATE, std::max<UINT32>(2000000, ow * oh * 6)); // ~5.5 Mbit/s at 720p, ~12 at 1080p
    vOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vOut.Get(), MF_MT_FRAME_SIZE, ow, oh);
    MFSetAttributeRatio(vOut.Get(), MF_MT_FRAME_RATE, fn, fd);
    MFSetAttributeRatio(vOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD vStream = 0;
    hr = writer->AddStream(vOut.Get(), &vStream);
    if (FAILED(hr)) return failed("Setting up the H.264 picture", hr);
    ComPtr<IMFMediaType> nv12In; // what the writer gets: the camera's size, or the bigger one
    MFCreateMediaType(&nv12In);
    nv12->CopyAllItems(nv12In.Get());
    MFSetAttributeSize(nv12In.Get(), MF_MT_FRAME_SIZE, ow, oh);
    hr = writer->SetInputMediaType(vStream, nv12In.Get(), nullptr);
    if (FAILED(hr)) return failed("Connecting the picture to H.264", hr);

    ComPtr<IMFMediaType> aOut, aIn;
    MFCreateMediaType(&aOut);
    aOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
    aOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    aOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); // 192 kbit/s
    MFCreateMediaType(&aIn);
    aIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    aIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
    aIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    aIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    aIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioRate * 4);
    DWORD aStream = 0;
    hr = writer->AddStream(aOut.Get(), &aStream);
    if (FAILED(hr)) return failed("Setting up the AAC sound", hr);
    hr = writer->SetInputMediaType(aStream, aIn.Get(), nullptr);
    if (FAILED(hr)) return failed("Connecting the sound to AAC", hr);
    hr = writer->BeginWriting();
    if (FAILED(hr)) return failed("Starting the MP4 file", hr);

    // 3. the sound at 48 kHz on the video's timeline (audioLeadSeconds after frame 0)
    const double ratio = sampleRate / (double) kAudioRate;
    const int resampledLen = (int) std::floor((double) audio.getNumSamples() / ratio);
    juce::AudioBuffer<float> rs(2, std::max(1, resampledLen));
    rs.clear();
    for (int c = 0; c < 2; ++c)
    {
        juce::LagrangeInterpolator li;
        li.process(ratio, audio.getReadPointer(juce::jmin(c, audio.getNumChannels() - 1)), rs.getWritePointer(c), resampledLen);
    }
    const LONGLONG totalFrames = duration > 0 ? duration * (LONGLONG) kAudioRate / 10000000LL
                                              : (LONGLONG) resampledLen + (LONGLONG) (audioLeadSeconds * kAudioRate);
    const LONGLONG leadFrames = (LONGLONG) std::llround(audioLeadSeconds * kAudioRate);
    LONGLONG audioDone = 0;
    auto writeAudioUntil = [&](LONGLONG until100ns) -> HRESULT {
        constexpr LONGLONG chunk = 1024;
        while (audioDone < totalFrames && audioDone * 10000000LL / kAudioRate < until100ns)
        {
            const LONGLONG n = std::min(chunk, totalFrames - audioDone);
            ComPtr<IMFMediaBuffer> buf;
            HRESULT r = MFCreateMemoryBuffer((DWORD) (n * 4), &buf);
            if (FAILED(r)) return r;
            BYTE* data = nullptr;
            buf->Lock(&data, nullptr, nullptr);
            auto* pcm = reinterpret_cast<int16_t*>(data);
            for (LONGLONG i = 0; i < n; ++i)
            {
                const LONGLONG src = audioDone + i - leadFrames;
                for (int c = 0; c < 2; ++c)
                {
                    const float v = (src >= 0 && src < resampledLen) ? rs.getSample(c, (int) src) : 0.0f;
                    pcm[i * 2 + c] = (int16_t) juce::jlimit(-32768, 32767, (int) std::lround(v * 32767.0f));
                }
            }
            buf->Unlock();
            buf->SetCurrentLength((DWORD) (n * 4));
            ComPtr<IMFSample> s;
            MFCreateSample(&s);
            s->AddBuffer(buf.Get());
            s->SetSampleTime(audioDone * 10000000LL / kAudioRate);
            s->SetSampleDuration(n * 10000000LL / kAudioRate);
            r = writer->WriteSample(aStream, s.Get());
            if (FAILED(r)) return r;
            audioDone += n;
        }
        return S_OK;
    };

    // 4. picture frames (re-timed from 0) with the sound written alongside
    LONGLONG first = -1;
    for (;;)
    {
        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> s;
        hr = reader->ReadSample(kVideo, 0, nullptr, &flags, &ts, &s);
        if (FAILED(hr)) return failed("Reading a picture frame", hr);
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
        if (s == nullptr) continue;
        if (first < 0) first = ts;
        const LONGLONG t = ts - first;
        if (screen != nullptr)
        {
            const double tc = (double) t / 1.0e7;
            const auto key = ampsurd::screen::boxAt(overlay->keys, tc);
            const LONGLONG st = (LONGLONG) ((tc - overlay->offsetSeconds) * 1.0e7);
            // (screen->at() reads the screen file up to this moment; always, so it keeps up)
            const bool haveScreen = st >= 0 && screen->at(st);
            auto replaceWith = [&](const std::vector<uint8_t>& pic) {
                ComPtr<IMFMediaBuffer> nb;
                if (FAILED(MFCreateMemoryBuffer((DWORD) pic.size(), &nb))) return;
                BYTE* d = nullptr;
                nb->Lock(&d, nullptr, nullptr);
                memcpy(d, pic.data(), pic.size());
                nb->Unlock();
                nb->SetCurrentLength((DWORD) pic.size());
                ComPtr<IMFSample> ns;
                MFCreateSample(&ns);
                ns->AddBuffer(nb.Get());
                LONGLONG dur = 0;
                if (SUCCEEDED(s->GetSampleDuration(&dur))) ns->SetSampleDuration(dur);
                s = ns;
            };
            if (upscale)
            {
                // every frame at the bigger size: the camera scaled up smoothly, then the screen / box at full detail
                if (copyNv12(s.Get(), (int) w, (int) h, camPlaneH, composed))
                {
                    const size_t cy = (size_t) w * h, by = (size_t) ow * oh;
                    ampsurd::screen::scaleBilinear(composed.data(), (int) w, (int) h, (int) w, big.data(), (int) ow, (int) oh, (int) ow, 1);
                    ampsurd::screen::scaleBilinear(composed.data() + cy, (int) w / 2, (int) h / 2, (int) w, big.data() + by, (int) ow / 2, (int) oh / 2,
                                                   (int) ow, 2);
                    if ((key.visible || key.swapped) && haveScreen)
                        ampsurd::screen::composeNv12(key, overlay->previewAspect, big.data(), (int) ow, (int) oh, screen->frame.data(), screen->w,
                                                     screen->h, scratch);
                }
                replaceWith(big); // (a frame that could not be read: the previous picture again)
            }
            else if ((key.visible || key.swapped) && haveScreen && copyNv12(s.Get(), (int) w, (int) h, camPlaneH, composed)
                     && ampsurd::screen::composeNv12(key, overlay->previewAspect, composed.data(), (int) w, (int) h, screen->frame.data(), screen->w,
                                                     screen->h, scratch))
                replaceWith(composed);
        }
        s->SetSampleTime(t);
        hr = writeAudioUntil(t + 5000000LL);
        if (FAILED(hr)) return failed("Writing the sound", hr);
        hr = writer->WriteSample(vStream, s.Get());
        if (FAILED(hr)) return failed("Writing a picture frame", hr);
        if (progress && duration > 0) progress((float) juce::jlimit(0.0, 1.0, (double) t / (double) duration));
    }
    hr = writeAudioUntil(LLONG_MAX);
    if (FAILED(hr)) return failed("Writing the sound", hr);
    hr = writer->Finalize();
    if (FAILED(hr)) return failed("Finishing the MP4 file", hr);
    if (progress) progress(1.0f);
    return {};
}

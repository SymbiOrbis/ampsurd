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

juce::String muxVideoToMp4(const juce::File& video, const juce::AudioBuffer<float>& audio, double sampleRate, double audioLeadSeconds,
                           const juce::File& out, const std::function<void(float)>& progress)
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
    vOut->SetUINT32(MF_MT_AVG_BITRATE, std::max<UINT32>(2000000, w * h * 6)); // ~5.5 Mbit/s at 720p
    vOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vOut.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(vOut.Get(), MF_MT_FRAME_RATE, fn, fd);
    MFSetAttributeRatio(vOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD vStream = 0;
    hr = writer->AddStream(vOut.Get(), &vStream);
    if (FAILED(hr)) return failed("Setting up the H.264 picture", hr);
    hr = writer->SetInputMediaType(vStream, nv12.Get(), nullptr);
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

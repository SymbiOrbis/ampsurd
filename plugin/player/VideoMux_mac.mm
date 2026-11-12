// macOS: MP4 from the camera file (picture) and AMPSURD's mix (sound) with AVFoundation.
#include "VideoMux.h"

#include <algorithm>

#include <juce_audio_formats/juce_audio_formats.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

bool isVideoExportSupported() { return true; }

namespace
{
NSURL* urlFor(const juce::File& f) { return [NSURL fileURLWithPath: [NSString stringWithUTF8String: f.getFullPathName().toRawUTF8()]]; }
}

static juce::String muxPlain(const juce::File& video, const juce::AudioBuffer<float>& audio, double sampleRate, double audioLeadSeconds,
                             const juce::File& out, const std::function<void(float)>& progress)
{
    // the mix as a temporary 24-bit WAV (AVFoundation reads it, the export encodes AAC)
    const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("AMPSURD video sound", ".wav");
    {
        std::unique_ptr<juce::OutputStream> os = tmp.createOutputStream();
        if (os == nullptr) return "Cannot write a temporary file.";
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(24));
        if (w == nullptr) return "Cannot write a temporary file.";
        w->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    }

    juce::String error;
    @autoreleasepool
    {
        AVURLAsset* vAsset = [AVURLAsset URLAssetWithURL: urlFor(video) options: nil];
        AVURLAsset* aAsset = [AVURLAsset URLAssetWithURL: urlFor(tmp) options: nil];
        JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
        NSArray<AVAssetTrack*>* vTracks = [vAsset tracksWithMediaType: AVMediaTypeVideo];
        NSArray<AVAssetTrack*>* aTracks = [aAsset tracksWithMediaType: AVMediaTypeAudio];
        JUCE_END_IGNORE_DEPRECATION_WARNINGS
        if (vTracks.count == 0) error = "The camera file has no picture.";
        else
        {
            AVMutableComposition* comp = [AVMutableComposition composition];
            AVMutableCompositionTrack* cv = [comp addMutableTrackWithMediaType: AVMediaTypeVideo preferredTrackID: kCMPersistentTrackID_Invalid];
            NSError* e = nil;
            const CMTime vDur = vAsset.duration;
            [cv insertTimeRange: CMTimeRangeMake(kCMTimeZero, vDur) ofTrack: vTracks[0] atTime: kCMTimeZero error: &e];
            cv.preferredTransform = vTracks[0].preferredTransform;
            if (e != nil) error = "The picture cannot be used: " + juce::String([[e localizedDescription] UTF8String]);

            if (error.isEmpty() && aTracks.count > 0)
            {
                // place the sound: audioLeadSeconds after frame 0 (negative: cut its beginning)
                const double aStart = audioLeadSeconds < 0.0 ? -audioLeadSeconds : 0.0;
                const double atTime = audioLeadSeconds > 0.0 ? audioLeadSeconds : 0.0;
                const double len = std::min(CMTimeGetSeconds(aAsset.duration) - aStart, CMTimeGetSeconds(vDur) - atTime);
                if (len > 0.0)
                {
                    AVMutableCompositionTrack* ca = [comp addMutableTrackWithMediaType: AVMediaTypeAudio preferredTrackID: kCMPersistentTrackID_Invalid];
                    const int32_t scale = 48000;
                    [ca insertTimeRange: CMTimeRangeMake(CMTimeMakeWithSeconds(aStart, scale), CMTimeMakeWithSeconds(len, scale))
                                ofTrack: aTracks[0]
                                 atTime: CMTimeMakeWithSeconds(atTime, scale)
                                  error: &e];
                    if (e != nil) error = "The sound cannot be used: " + juce::String([[e localizedDescription] UTF8String]);
                }
            }

            if (error.isEmpty())
            {
                out.deleteFile();
                AVAssetExportSession* ex = [[AVAssetExportSession alloc] initWithAsset: comp presetName: AVAssetExportPresetHighestQuality];
                ex.outputURL = urlFor(out);
                ex.outputFileType = AVFileTypeMPEG4;
                ex.shouldOptimizeForNetworkUse = YES;
                dispatch_semaphore_t done = dispatch_semaphore_create(0);
                [ex exportAsynchronouslyWithCompletionHandler: ^{ dispatch_semaphore_signal(done); }];
                while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 100 * (int64_t) NSEC_PER_MSEC)) != 0)
                    if (progress) progress(ex.progress);
                if (ex.status != AVAssetExportSessionStatusCompleted)
                    error = "Saving the video failed" + (ex.error != nil ? ": " + juce::String([[ex.error localizedDescription] UTF8String]) : juce::String("."));
                dispatch_release(done);
                [ex release];
            }
        }
    }
    tmp.deleteFile();
    if (error.isEmpty() && progress) progress(1.0f);
    return error;
}

namespace
{
// SCREEN BOX: the camera picture with the screen recording drawn in as a box -> a video-only file (H.264). The camera
// file is decoded to BGRA, the newest screen frame (at camera time - offset) is drawn where the box was, re-encoded.
juce::String composeScreenBox(const juce::File& video, const ampsurd::screen::Overlay& o, const juce::File& result,
                              const std::function<void(float)>& progress)
{
    juce::String error;
    @autoreleasepool
    {
        AVURLAsset* cam = [AVURLAsset URLAssetWithURL: urlFor(video) options: nil];
        AVURLAsset* scr = [AVURLAsset URLAssetWithURL: urlFor(o.file) options: nil];
        JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
        NSArray<AVAssetTrack*>* ct = [cam tracksWithMediaType: AVMediaTypeVideo];
        NSArray<AVAssetTrack*>* st = [scr tracksWithMediaType: AVMediaTypeVideo];
        JUCE_END_IGNORE_DEPRECATION_WARNINGS
        if (ct.count == 0) return "The camera file has no picture.";
        if (st.count == 0) return "The screen recording has no picture.";
        NSError* e = nil;
        NSDictionary* bgra = @{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) };
        AVAssetReader* cr = [AVAssetReader assetReaderWithAsset: cam error: &e];
        AVAssetReader* sr = [AVAssetReader assetReaderWithAsset: scr error: &e];
        if (cr == nil || sr == nil) return "The recordings cannot be read.";
        AVAssetReaderTrackOutput* co = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack: ct[0] outputSettings: bgra];
        AVAssetReaderTrackOutput* so = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack: st[0] outputSettings: bgra];
        [cr addOutput: co];
        [sr addOutput: so];
        if (![cr startReading] || ![sr startReading]) return "The recordings cannot be read.";
        const CGSize size = ct[0].naturalSize;
        const int w = ((int) size.width) & ~1, h = ((int) size.height) & ~1;
        if (w < 16 || h < 16) return "The camera file has no picture.";
        result.deleteFile();
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL: urlFor(result) fileType: AVFileTypeMPEG4 error: &e];
        if (writer == nil) return "Cannot write a temporary file.";
        NSDictionary* settings = @{ AVVideoCodecKey: AVVideoCodecTypeH264, AVVideoWidthKey: @(w), AVVideoHeightKey: @(h),
                                    AVVideoCompressionPropertiesKey: @{ AVVideoAverageBitRateKey: @(std::max(2000000, w * h * 6)) } };
        AVAssetWriterInput* in = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeVideo outputSettings: settings];
        in.expectsMediaDataInRealTime = NO;
        in.transform = ct[0].preferredTransform;
        AVAssetWriterInputPixelBufferAdaptor* ad = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput: in
                                       sourcePixelBufferAttributes: @{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                                                                       (id) kCVPixelBufferWidthKey: @(w), (id) kCVPixelBufferHeightKey: @(h) }];
        [writer addInput: in];
        if (![writer startWriting]) return "Cannot write a temporary file.";
        [writer startSessionAtSourceTime: kCMTimeZero];

        const double dur = std::max(0.1, CMTimeGetSeconds(cam.duration));
        CMSampleBufferRef scrNext = [so copyNextSampleBuffer], scrCur = nullptr;
        double scrFirst = scrNext != nullptr ? CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(scrNext)) : 0.0;
        double camFirst = -1.0;
        std::vector<uint8_t> scratch;
        for (;;)
        {
            CMSampleBufferRef cs = [co copyNextSampleBuffer];
            if (cs == nullptr) break;
            CVImageBufferRef cpb = CMSampleBufferGetImageBuffer(cs);
            const double pts = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(cs));
            if (camFirst < 0.0) camFirst = pts;
            const double tc = pts - camFirst;
            // the newest screen frame at this moment
            const double ts = tc - o.offsetSeconds;
            while (scrNext != nullptr && CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(scrNext)) - scrFirst <= ts)
            {
                if (scrCur != nullptr) CFRelease(scrCur);
                scrCur = scrNext;
                scrNext = [so copyNextSampleBuffer];
            }
            CVPixelBufferRef outPb = nullptr;
            if (cpb != nullptr && CVPixelBufferPoolCreatePixelBuffer(nullptr, ad.pixelBufferPool, &outPb) == kCVReturnSuccess && outPb != nullptr)
            {
                CVPixelBufferLockBaseAddress(cpb, kCVPixelBufferLock_ReadOnly);
                CVPixelBufferLockBaseAddress(outPb, 0);
                auto* dst = (uint8_t*) CVPixelBufferGetBaseAddress(outPb);
                const size_t dp = CVPixelBufferGetBytesPerRow(outPb), sp = CVPixelBufferGetBytesPerRow(cpb);
                const auto* src = (const uint8_t*) CVPixelBufferGetBaseAddress(cpb);
                const int ch = std::min(h, (int) CVPixelBufferGetHeight(cpb)), cw = std::min(w, (int) CVPixelBufferGetWidth(cpb));
                for (int y = 0; y < ch; ++y) memcpy(dst + (size_t) y * dp, src + (size_t) y * sp, (size_t) cw * 4);
                CVPixelBufferUnlockBaseAddress(cpb, kCVPixelBufferLock_ReadOnly);
                const auto key = ampsurd::screen::boxAt(o.keys, tc);
                CVImageBufferRef spb = scrCur != nullptr ? CMSampleBufferGetImageBuffer(scrCur) : nullptr;
                if ((key.visible || key.swapped) && spb != nullptr && ts >= 0.0)
                {
                    // the screen box, or SWAP: the screen large and the camera in the box
                    CVPixelBufferLockBaseAddress(spb, kCVPixelBufferLock_ReadOnly);
                    ampsurd::screen::composeBgra(key, o.previewAspect, dst, w, h, (int) dp, (const uint8_t*) CVPixelBufferGetBaseAddress(spb),
                                                 (int) CVPixelBufferGetWidth(spb), (int) CVPixelBufferGetHeight(spb),
                                                 (int) CVPixelBufferGetBytesPerRow(spb), scratch);
                    CVPixelBufferUnlockBaseAddress(spb, kCVPixelBufferLock_ReadOnly);
                }
                CVPixelBufferUnlockBaseAddress(outPb, 0);
                while (![in isReadyForMoreMediaData]) juce::Thread::sleep(2);
                [ad appendPixelBuffer: outPb withPresentationTime: CMTimeMakeWithSeconds(tc, 600)];
                CVPixelBufferRelease(outPb);
            }
            CFRelease(cs);
            if (progress) progress((float) juce::jlimit(0.0, 1.0, tc / dur));
        }
        if (scrCur != nullptr) CFRelease(scrCur);
        if (scrNext != nullptr) CFRelease(scrNext);
        [in markAsFinished];
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler: ^{ dispatch_semaphore_signal(done); }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        dispatch_release(done);
        if (writer.status != AVAssetWriterStatusCompleted)
            error = "Combining the camera and the screen failed" + (writer.error != nil ? ": " + juce::String([[writer.error localizedDescription] UTF8String]) : juce::String("."));
    }
    return error;
}
} // namespace

juce::String muxVideoToMp4(const juce::File& video, const juce::AudioBuffer<float>& audio, double sampleRate, double audioLeadSeconds,
                           const juce::File& out, const std::function<void(float)>& progress, const ampsurd::screen::Overlay* overlay)
{
    if (overlay == nullptr || !overlay->isUsed() || !overlay->file.existsAsFile())
        return muxPlain(video, audio, sampleRate, audioLeadSeconds, out, progress);
    // 1. camera + screen box -> a temporary picture file, 2. that picture + AMPSURD's sound -> the MP4
    const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("AMPSURD video picture", ".mp4");
    auto err = composeScreenBox(video, *overlay, tmp, [&](float p) { if (progress) progress(p * 0.6f); });
    if (err.isEmpty())
        err = muxPlain(tmp, audio, sampleRate, audioLeadSeconds, out, [&](float p) { if (progress) progress(0.6f + p * 0.4f); });
    else
        err = muxPlain(video, audio, sampleRate, audioLeadSeconds, out, progress).isEmpty()
                  ? juce::String() // saved without the box rather than not at all
                  : err;
    tmp.deleteFile();
    return err;
}

// macOS: MP4 from the camera file (picture) and AMPSURD's mix (sound) with AVFoundation.
#include "VideoMux.h"

#include <algorithm>

#include <juce_audio_formats/juce_audio_formats.h>

#import <AVFoundation/AVFoundation.h>

bool isVideoExportSupported() { return true; }

namespace
{
NSURL* urlFor(const juce::File& f) { return [NSURL fileURLWithPath: [NSString stringWithUTF8String: f.getFullPathName().toRawUTF8()]]; }
}

juce::String muxVideoToMp4(const juce::File& video, const juce::AudioBuffer<float>& audio, double sampleRate, double audioLeadSeconds,
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

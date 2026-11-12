// SCREEN BOX on macOS 12.3 or newer: ScreenCaptureKit (one display, scaled by the system to at most 1280 x 720, with
// the mouse pointer) -> the box's preview, and while recording H.264 into an MP4 with AVAssetWriter. The first time,
// macOS asks for the permission to record the screen (System Settings > Privacy & Security > Screen Recording).
// Windows marked with excludeWindow() (the camera window, the box) are left out. Manual retain / release (no ARC).
#include "ScreenCapture.h"
#include "ScreenOverlay.h"

#include <mutex>
#include <vector>

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

namespace
{
NSURL* urlFor(const juce::File& f) { return [NSURL fileURLWithPath: [NSString stringWithUTF8String: f.getFullPathName().toRawUTF8()]]; }

struct DisplayInfo
{
    CGDirectDisplayID id = 0;
    juce::String name;
    int w = 0, h = 0; // pixels
};

std::vector<DisplayInfo> listDisplays()
{
    std::vector<DisplayInfo> list;
    @autoreleasepool
    {
        int n = 0;
        for (NSScreen* s in [NSScreen screens])
        {
            DisplayInfo d;
            d.id = (CGDirectDisplayID) [[[s deviceDescription] objectForKey: @"NSScreenNumber"] unsignedIntValue];
            juce::String label;
            if (@available(macOS 10.15, *)) label = juce::String([[s localizedName] UTF8String]);
            if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(d.id))
            {
                d.w = (int) CGDisplayModeGetPixelWidth(mode);
                d.h = (int) CGDisplayModeGetPixelHeight(mode);
                CGDisplayModeRelease(mode);
            }
            ++n;
            d.name = "Screen " + juce::String(n) + (label.isNotEmpty() ? " - " + label : juce::String())
                     + " (" + juce::String((int) s.frame.size.width) + " x " + juce::String((int) s.frame.size.height) + ")";
            list.push_back(d);
        }
    }
    return list;
}
} // namespace

struct ScreenCapture::Impl
{
    Impl(ScreenCapture& o, DisplayInfo d) : owner(o), display(d)
    {
        queue = dispatch_queue_create("AMPSURD screen", DISPATCH_QUEUE_SERIAL);
        const auto rs = ampsurd::screen::recordSize(d.w > 0 ? d.w : 1920, d.h > 0 ? d.h : 1080);
        recW = rs.x;
        recH = rs.y;
        frame.resize((size_t) recW * (size_t) recH * 4);
    }
    ~Impl() { dispatch_release(queue); }

    ScreenCapture& owner;
    DisplayInfo display;
    dispatch_queue_t queue;
    id stream = nil, output = nil; // SCStream, AmpsurdScreenOutput (12.3+)
    std::vector<uint32_t> excluded; // CGWindowIDs

    // on `queue` only
    std::vector<uint8_t> frame;
    int recW = 0, recH = 0;
    bool haveFrame = false;
    double lastShown = 0.0;
    AVAssetWriter* writer = nil;
    AVAssetWriterInput* input = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
    double recStartMs = 0.0;
    juce::int64 firstFrameMs = 0, lastPtsMs = -1;
    juce::File file;

    void onFrame(CVPixelBufferRef pb)
    {
        if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
        const int w = (int) CVPixelBufferGetWidth(pb), h = (int) CVPixelBufferGetHeight(pb);
        const auto* src = (const uint8_t*) CVPixelBufferGetBaseAddress(pb);
        const int pitch = (int) CVPixelBufferGetBytesPerRow(pb);
        if (src != nullptr && w > 0 && h > 0)
        {
            ampsurd::screen::scalePlane(src, w, h, pitch, frame.data(), recW, recH, recW * 4, 4); // same size: a copy
            haveFrame = true;
        }
        CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastShown >= 1000.0 / ampsurd::screen::kFramesPerSecond * 0.9)
        {
            lastShown = now;
            owner.publishPreview(ScreenCapture::makePreview(frame.data(), recW, recH, recW * 4));
        }
        writeFrame();
    }

    void writeFrame()
    {
        if (writer == nil || input == nil || !haveFrame || ![input isReadyForMoreMediaData]) return;
        const auto ms = (juce::int64) (juce::Time::getMillisecondCounterHiRes() - recStartMs);
        if (ms <= lastPtsMs) return;
        CVPixelBufferPoolRef pool = [adaptor pixelBufferPool];
        if (pool == nullptr) return;
        CVPixelBufferRef out = nullptr;
        if (CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &out) != kCVReturnSuccess || out == nullptr) return;
        CVPixelBufferLockBaseAddress(out, 0);
        auto* dst = (uint8_t*) CVPixelBufferGetBaseAddress(out);
        const size_t dp = CVPixelBufferGetBytesPerRow(out);
        for (int y = 0; y < recH; ++y) memcpy(dst + (size_t) y * dp, frame.data() + (size_t) y * (size_t) recW * 4, (size_t) recW * 4);
        CVPixelBufferUnlockBaseAddress(out, 0);
        if ([adaptor appendPixelBuffer: out withPresentationTime: CMTimeMake(ms, 1000)]) lastPtsMs = ms;
        CVPixelBufferRelease(out);
    }

    juce::String startWriter(const juce::File& f) // on `queue`
    {
        NSError* e = nil;
        writer = [[AVAssetWriter alloc] initWithURL: urlFor(f) fileType: AVFileTypeMPEG4 error: &e];
        if (writer == nil) return "The screen recording file cannot be written.";
        NSDictionary* settings = @{ AVVideoCodecKey: AVVideoCodecTypeH264,
                                    AVVideoWidthKey: @(recW),
                                    AVVideoHeightKey: @(recH),
                                    AVVideoCompressionPropertiesKey: @{ AVVideoAverageBitRateKey: @(3000000) } };
        input = [[AVAssetWriterInput alloc] initWithMediaType: AVMediaTypeVideo outputSettings: settings];
        input.expectsMediaDataInRealTime = YES;
        NSDictionary* attrs = @{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                                 (id) kCVPixelBufferWidthKey: @(recW),
                                 (id) kCVPixelBufferHeightKey: @(recH) };
        adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc] initWithAssetWriterInput: input sourcePixelBufferAttributes: attrs];
        [writer addInput: input];
        if (![writer startWriting]) { releaseWriter(); return "The screen recording could not start."; }
        [writer startSessionAtSourceTime: kCMTimeZero];
        recStartMs = juce::Time::getMillisecondCounterHiRes();
        lastPtsMs = -1;
        file = f;
        firstFrameMs = juce::Time::currentTimeMillis(); // the current picture is written right away
        writeFrame();
        return {};
    }

    void releaseWriter()
    {
        [adaptor release];
        [input release];
        [writer release];
        adaptor = nil;
        input = nil;
        writer = nil;
    }
};

API_AVAILABLE(macos(12.3))
@interface AmpsurdScreenOutput_v1 : NSObject <SCStreamOutput, SCStreamDelegate>
{
@public
    ScreenCapture::Impl* impl;
}
@end

@implementation AmpsurdScreenOutput_v1
- (void) stream: (SCStream*) stream didOutputSampleBuffer: (CMSampleBufferRef) sb ofType: (SCStreamOutputType) type
{
    juce::ignoreUnused(stream);
    if (type != SCStreamOutputTypeScreen || impl == nullptr) return;
    CFArrayRef att = CMSampleBufferGetSampleAttachmentsArray(sb, false);
    if (att != nullptr && CFArrayGetCount(att) > 0)
    {
        NSDictionary* d = (NSDictionary*) CFArrayGetValueAtIndex(att, 0);
        NSNumber* st = [d objectForKey: SCStreamFrameInfoStatus];
        if (st != nil && [st integerValue] != SCFrameStatusComplete) return; // idle / blank: nothing new
    }
    CVImageBufferRef pb = CMSampleBufferGetImageBuffer(sb);
    if (pb != nullptr) impl->onFrame(pb);
}

- (void) stream: (SCStream*) stream didStopWithError: (NSError*) error
{
    juce::ignoreUnused(stream);
    if (impl != nullptr)
        impl->owner.reportError("The screen recording stopped: " + juce::String([[error localizedDescription] UTF8String]));
}
@end

namespace
{
// the content filter: the display without the excluded windows (12.3+); nil + error when not possible
API_AVAILABLE(macos(12.3))
SCContentFilter* makeFilter(CGDirectDisplayID displayID, const std::vector<uint32_t>& excluded, juce::String& error)
{
    __block SCShareableContent* content = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows: NO
                                               onScreenWindowsOnly: YES
                                                 completionHandler: ^(SCShareableContent* c, NSError* e) {
        juce::ignoreUnused(e);
        content = [c retain];
        dispatch_semaphore_signal(done);
    }];
    const bool ok = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 4 * (int64_t) NSEC_PER_SEC)) == 0;
    dispatch_release(done);
    if (!ok || content == nil)
    {
        [content release];
        error = "macOS did not give access to the screen. Allow AMPSURD in System Settings > Privacy & Security > Screen Recording.";
        return nil;
    }
    SCDisplay* display = nil;
    for (SCDisplay* d in content.displays)
        if (d.displayID == displayID) display = d;
    NSMutableArray<SCWindow*>* out = [NSMutableArray array];
    for (SCWindow* w in content.windows)
        for (auto wid : excluded)
            if (w.windowID == wid) [out addObject: w];
    SCContentFilter* filter = display != nil ? [[[SCContentFilter alloc] initWithDisplay: display excludingWindows: out] autorelease] : nil;
    [content release];
    if (filter == nil) error = "This screen is not there any more.";
    return filter;
}
} // namespace

ScreenCapture::ScreenCapture() = default;

ScreenCapture::~ScreenCapture()
{
    alive->store(false);
    close();
}

bool ScreenCapture::isSupported()
{
    if (@available(macOS 12.3, *)) return true;
    return false;
}

juce::StringArray ScreenCapture::screens()
{
    juce::StringArray names;
    if (!isSupported()) return names;
    for (auto& d : listDisplays()) names.add(d.name);
    return names;
}

juce::String ScreenCapture::open(int index)
{
    close();
    if (@available(macOS 12.3, *))
    {
        const auto displays = listDisplays();
        if (index < 0 || index >= (int) displays.size()) return "This screen is not there any more.";
        if (!CGPreflightScreenCaptureAccess())
        {
            CGRequestScreenCaptureAccess(); // the system's question (once)
            return "macOS needs your permission: System Settings > Privacy & Security > Screen Recording > AMPSURD on. "
                   "Then choose the screen again (macOS may ask to restart AMPSURD).";
        }
        auto im = std::make_unique<Impl>(*this, displays[(size_t) index]);
        juce::String err;
        juce::String result;
        @autoreleasepool
        {
            SCContentFilter* filter = makeFilter(im->display.id, im->excluded, err);
            if (filter == nil) return err;
            SCStreamConfiguration* cfg = [[[SCStreamConfiguration alloc] init] autorelease];
            cfg.width = (size_t) im->recW;
            cfg.height = (size_t) im->recH;
            cfg.minimumFrameInterval = CMTimeMake(1, ampsurd::screen::kFramesPerSecond);
            cfg.pixelFormat = kCVPixelFormatType_32BGRA;
            cfg.showsCursor = YES;
            cfg.queueDepth = 5;
            AmpsurdScreenOutput_v1* out = [[AmpsurdScreenOutput_v1 alloc] init];
            out->impl = im.get();
            SCStream* stream = [[SCStream alloc] initWithFilter: filter configuration: cfg delegate: out];
            NSError* e = nil;
            if (![stream addStreamOutput: out type: SCStreamOutputTypeScreen sampleHandlerQueue: im->queue error: &e])
            {
                [stream release];
                out->impl = nullptr;
                [out release];
                return "The screen cannot be recorded: " + juce::String([[e localizedDescription] UTF8String]);
            }
            auto a = alive;
            [stream startCaptureWithCompletionHandler: ^(NSError* startError) {
                if (startError != nil && a->load())
                    reportError("The screen cannot be recorded: " + juce::String([[startError localizedDescription] UTF8String]));
            }];
            im->stream = stream;
            im->output = out;
        }
        impl = std::move(im);
        openIndex = index;
        return result;
    }
    juce::ignoreUnused(index);
    return "Screen recording needs macOS 12.3 or newer.";
}

void ScreenCapture::close()
{
    if (recording) { juce::File f; stopRecording(f); }
    if (impl != nullptr)
    {
        if (@available(macOS 12.3, *))
        {
            SCStream* stream = (SCStream*) impl->stream;
            AmpsurdScreenOutput_v1* out = (AmpsurdScreenOutput_v1*) impl->output;
            if (stream != nil)
            {
                dispatch_semaphore_t done = dispatch_semaphore_create(0);
                [stream stopCaptureWithCompletionHandler: ^(NSError*) { dispatch_semaphore_signal(done); }];
                dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 3 * (int64_t) NSEC_PER_SEC));
                dispatch_release(done);
            }
            dispatch_sync(impl->queue, ^{ if (out != nil) out->impl = nullptr; }); // no frame reaches the Impl any more
            [stream release];
            [out release];
        }
        impl.reset();
    }
    openIndex = -1;
    publishPreview({});
}

bool ScreenCapture::isOpen() const { return impl != nullptr; }

juce::String ScreenCapture::startRecording(const juce::File& folder)
{
    if (impl == nullptr) return "No screen is chosen.";
    folder.createDirectory();
    const auto f = folder.getNonexistentChildFile("Screen " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".mp4");
    __block juce::String err;
    Impl* im = impl.get();
    dispatch_sync(im->queue, ^{
        if (!im->haveFrame) { err = "The screen has not sent a picture yet - try again in a moment."; return; }
        err = im->startWriter(f);
    });
    if (err.isNotEmpty()) return err;
    recording = true;
    return {};
}

juce::int64 ScreenCapture::stopRecording(juce::File& recordedFile)
{
    recordedFile = juce::File();
    if (!recording || impl == nullptr) { recording = false; return 0; }
    recording = false;
    Impl* im = impl.get();
    __block AVAssetWriter* writer = nil;
    __block juce::int64 first = 0;
    __block juce::File file;
    dispatch_sync(im->queue, ^{
        im->writeFrame(); // the screen as it is now: the recording lasts until the end of the take
        [im->input markAsFinished];
        writer = [im->writer retain];
        first = im->firstFrameMs;
        file = im->file;
        im->releaseWriter();
    });
    if (writer == nil) return 0;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler: ^{ dispatch_semaphore_signal(done); }];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * (int64_t) NSEC_PER_SEC));
    dispatch_release(done);
    const bool ok = [writer status] == AVAssetWriterStatusCompleted;
    [writer release];
    if (!ok) return 0;
    recordedFile = file;
    return first;
}

void ScreenCapture::excludeWindow(juce::Component* w)
{
    if (w == nullptr || impl == nullptr) return;
    auto* peer = w->getPeer();
    if (peer == nullptr) return;
    NSView* view = (NSView*) peer->getNativeHandle();
    NSWindow* win = [view window];
    if (win == nil) return;
    const auto wid = (uint32_t) [win windowNumber];
    for (auto e : impl->excluded)
        if (e == wid) return;
    impl->excluded.push_back(wid);
    if (@available(macOS 12.3, *))
    {
        juce::String err;
        SCContentFilter* filter = makeFilter(impl->display.id, impl->excluded, err);
        if (filter != nil && impl->stream != nil) [(SCStream*) impl->stream updateContentFilter: filter completionHandler: nil];
    }
}

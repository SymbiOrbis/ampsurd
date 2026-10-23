// macOS: NSSavePanel with a "Format" pop-up (one item per file type); the name's extension follows it.
#include "SaveDialog.h"

#import <AppKit/AppKit.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" // allowedFileTypes: still the way on 10.15

@interface AmpsurdSaveFormatTarget : NSObject
@property (nonatomic, assign) NSSavePanel* panel;
@property (nonatomic, retain) NSArray<NSString*>* extensions;
- (void) formatChanged: (NSPopUpButton*) sender;
@end

@implementation AmpsurdSaveFormatTarget
- (void) formatChanged: (NSPopUpButton*) sender
{
    const NSInteger i = [sender indexOfSelectedItem];
    if (i >= 0 && i < (NSInteger) [self.extensions count])
        [self.panel setAllowedFileTypes: @[ [self.extensions objectAtIndex: (NSUInteger) i] ]];
}
@end

namespace
{
NSString* ns(const juce::String& s) { return [NSString stringWithUTF8String: s.toRawUTF8()]; }
}

bool hasTypedSaveDialog() { return true; }

juce::File showTypedSaveDialog(juce::Component*, const juce::String& title, const juce::File& suggestion,
                               const juce::StringArray& descriptions, const juce::StringArray& extensions, int& chosenType)
{
    chosenType = -1;
    if (extensions.isEmpty()) return {};
    @autoreleasepool
    {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setTitle: ns(title)];
        [panel setMessage: ns(title)];
        [panel setDirectoryURL: [NSURL fileURLWithPath: ns(suggestion.getParentDirectory().getFullPathName())]];
        [panel setNameFieldStringValue: ns(suggestion.getFileNameWithoutExtension())];
        [panel setExtensionHidden: NO];

        NSMutableArray<NSString*>* exts = [NSMutableArray array];
        for (auto& e : extensions) [exts addObject: ns(e)];
        [panel setAllowedFileTypes: @[ [exts objectAtIndex: 0] ]];

        NSView* accessory = [[[NSView alloc] initWithFrame: NSMakeRect(0, 0, 460, 44)] autorelease];
        NSTextField* label = [NSTextField labelWithString: @"Format:"];
        [label setFrame: NSMakeRect(10, 12, 60, 20)];
        NSPopUpButton* popup = [[[NSPopUpButton alloc] initWithFrame: NSMakeRect(72, 8, 380, 28) pullsDown: NO] autorelease];
        for (int i = 0; i < extensions.size(); ++i)
            [popup addItemWithTitle: ns(descriptions[i] + " (." + extensions[i] + ")")];
        AmpsurdSaveFormatTarget* target = [[[AmpsurdSaveFormatTarget alloc] init] autorelease];
        target.panel = panel;
        target.extensions = exts;
        [popup setTarget: target];
        [popup setAction: @selector(formatChanged:)];
        [accessory addSubview: label];
        [accessory addSubview: popup];
        [panel setAccessoryView: accessory];

        if ([panel runModal] != NSModalResponseOK) return {};
        chosenType = juce::jlimit(0, extensions.size() - 1, (int) [popup indexOfSelectedItem]);
        const juce::File f(juce::String::fromUTF8([[[panel URL] path] UTF8String]));
        return f.withFileExtension("." + extensions[chosenType]);
    }
    return {};
}

#pragma clang diagnostic pop

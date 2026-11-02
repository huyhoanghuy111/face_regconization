// Tao phim ngay trong ung dung de kiem thu duong nhap lieu cua macOS.
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {

NSMutableDictionary<NSString*, NSTextField*>* textFields = nil;
NSMutableSet<NSString*>* usedThisFrame = nil;
NSTextField* previousField = nil;

NSWindow* mainWindow() { return [[NSApp windows] firstObject]; }

NSTextField* createTextField(NSView* parent) {
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
    field.bordered = NO;
    field.bezeled = NO;
    field.drawsBackground = NO;
    field.focusRingType = NSFocusRingTypeNone;
    field.textColor = [NSColor whiteColor];
    field.font = [NSFont systemFontOfSize:16];
    field.usesSingleLineMode = YES;
    field.cell.scrollable = YES;
    field.hidden = YES;
    [parent addSubview:field];
    return field;
}

// Cat chuoi UTF-8 cho vua buffer ma khong lam vo mot ky tu tieng Viet.
void copyUtf8(const char* source, char* buffer, size_t size) {
    size_t length = std::min(std::strlen(source), size - 1);
    while (length > 0 && (static_cast<unsigned char>(source[length]) & 0xC0) == 0x80) {
        --length;
    }
    std::memcpy(buffer, source, length);
    buffer[length] = '\0';
}

}  // namespace

void macTextField(const char* id, float x, float y, float width, float height, char* buffer,
                  size_t bufferSize, bool focus) {
    if (textFields == nil) {
        textFields = [NSMutableDictionary dictionary];
        usedThisFrame = [NSMutableSet set];
    }
    NSWindow* window = mainWindow();
    NSView* content = [window contentView];
    NSString* key = @(id);
    NSTextField* field = textFields[key];
    if (field == nil) {
        field = createTextField(content);
        textFields[key] = field;
    }

    const CGFloat top = content.isFlipped ? y : content.bounds.size.height - y - height;
    field.frame = NSMakeRect(x, top, width, height);
    if (field.hidden) {
        field.stringValue = @(buffer);
        field.hidden = NO;
    }
    if (focus) {
        [window makeFirstResponder:field];
    }

    // Tab chuyen sang o tiep theo theo thu tu ve
    previousField.nextKeyView = field;
    previousField = field;
    [usedThisFrame addObject:key];

    NSText* editor = [field currentEditor];
    NSString* value = editor != nil ? [editor string] : [field stringValue];
    copyUtf8([value UTF8String], buffer, bufferSize);
}

void macTextFieldsEndFrame() {
    NSWindow* window = mainWindow();
    for (NSString* key in textFields) {
        NSTextField* field = textFields[key];
        if (![usedThisFrame containsObject:key] && !field.hidden) {
            if ([field currentEditor] != nil) {
                [window makeFirstResponder:[window contentView]];  // tra ban phim cho cua so
            }
            field.hidden = YES;
        }
    }
    [usedThisFrame removeAllObjects];
    previousField = nil;
}

void macSetTextField(const char* id, const char* text) {
    NSTextField* field = textFields[@(id)];
    if (field != nil) {
        field.stringValue = @(text);
    }
}

void macPrintInputState() {
    NSWindow* window = [NSApp keyWindow];
    TISInputSourceRef source = TISCopyCurrentKeyboardInputSource();
    NSString* sourceId = source != nullptr
                             ? (__bridge NSString*)TISGetInputSourceProperty(source, kTISPropertyInputSourceID)
                             : @"(none)";
    std::fprintf(stderr, "input: active=%d keyWindow=%d windowNumber=%ld responder=%s context=%d source=%s\n",
                 [NSApp isActive] ? 1 : 0, window != nil ? 1 : 0,
                 static_cast<long>([[[NSApp windows] firstObject] windowNumber]),
                 [[[[[NSApp windows] firstObject] firstResponder] className] UTF8String],
                 [NSTextInputContext currentInputContext] != nil ? 1 : 0, [sourceId UTF8String]);
    if (source != nullptr) {
        CFRelease(source);
    }
}

void macActivateApp() {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activate];
    [[[NSApp windows] firstObject] makeKeyAndOrderFront:nil];
}

void macSelectInputSource(const char* sourceId) {
    NSDictionary* filter = @{(__bridge NSString*)kTISPropertyInputSourceID : @(sourceId)};
    CFArrayRef sources = TISCreateInputSourceList((__bridge CFDictionaryRef)filter, false);
    if (sources != nullptr && CFArrayGetCount(sources) > 0) {
        TISSelectInputSource((TISInputSourceRef)CFArrayGetValueAtIndex(sources, 0));
    }
    if (sources != nullptr) {
        CFRelease(sources);
    }
}

void macSendKey(char character) {
    NSWindow* window = [NSApp keyWindow] != nil ? [NSApp keyWindow] : [[NSApp windows] firstObject];
    NSString* text = [NSString stringWithFormat:@"%c", character];
    const unsigned short keyCode = character == 'a'   ? 0
                                   : character == 's' ? 1
                                   : character == 'b' ? 11
                                   : character == ' ' ? 49
                                                      : 8;
    for (NSEventType type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
        NSEvent* event = [NSEvent keyEventWithType:type
                                          location:NSZeroPoint
                                     modifierFlags:0
                                         timestamp:[[NSProcessInfo processInfo] systemUptime]
                                      windowNumber:[window windowNumber]
                                           context:nil
                                        characters:text
                       charactersIgnoringModifiers:text
                                         isARepeat:NO
                                           keyCode:keyCode];
        [NSApp sendEvent:event];
    }
}

// Tien trinh nen rat nho: nhan Control + Option + K o bat ky dau de mo may cham cong.
// RegisterEventHotKey khong can quyen Accessibility.
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include <cstdio>

static OSStatus onHotKey(EventHandlerCallRef, EventRef, void*) {
    NSURL* app = [NSURL fileURLWithPath:@KIOSK_APP_PATH];
    NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;  // dang mo thi dua len phia truoc
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:app
                                          configuration:configuration
                                      completionHandler:nil];
    return noErr;
}

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];  // khong hien tren Dock

        EventTypeSpec pressed = {kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&onHotKey, 1, &pressed, nullptr, nullptr);

        EventHotKeyRef hotKey = nullptr;
        const EventHotKeyID hotKeyId = {'face', 1};
        const OSStatus status = RegisterEventHotKey(kVK_ANSI_K, controlKey | optionKey, hotKeyId,
                                                    GetApplicationEventTarget(), 0, &hotKey);
        if (status != noErr) {
            std::fprintf(stderr, "Khong dang ky duoc phim tat (ma loi %d)\n", static_cast<int>(status));
            return 1;
        }
        std::fprintf(stderr, "Da dang ky Control + Option + K\n");
        [NSApp run];
    }
    return 0;
}

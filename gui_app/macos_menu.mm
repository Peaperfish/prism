#include "macos_menu.hpp"

#import <Cocoa/Cocoa.h>

// The menu items' target: turns a click (or its shortcut) into an SDL event for the main loop.
// The event type is the item's tag.
@interface PrismMenuTarget : NSObject
- (void)postEvent:(id)sender;
@end

@implementation PrismMenuTarget
- (void)postEvent:(id)sender {
    SDL_Event event;
    SDL_zero(event);
    event.type = static_cast<Uint32>([sender tag]);
    SDL_PushEvent(&event);
}
@end

namespace prism::gui_app {

namespace {

// Points the first item of the app menu that `matches` at an SDL event of `event_type`.
template <typename Matches>
bool connect_app_menu_item(Uint32 event_type, Matches matches) {
    // A menu item doesn't own its target, so this one lives as long as the app.
    static PrismMenuTarget* target = nil;
    NSMenu* main_menu = [NSApp mainMenu];
    if (main_menu == nil || [main_menu numberOfItems] == 0) {
        return false;
    }
    NSMenu* app_menu = [[main_menu itemAtIndex:0] submenu];
    for (NSMenuItem* item in [app_menu itemArray]) {
        if (matches(item)) {
            if (target == nil) {
                target = [[PrismMenuTarget alloc] init];
            }
            [item setTag:static_cast<NSInteger>(event_type)];
            [item setTarget:target];
            [item setAction:@selector(postEvent:)];
            return true;
        }
    }
    return false;
}

}  // namespace

bool connect_macos_preferences_item(Uint32 event_type) {
    return connect_app_menu_item(event_type, [](NSMenuItem* item) {
        return [[item keyEquivalent] isEqualToString:@","];
    });
}

bool connect_macos_about_item(Uint32 event_type) {
    return connect_app_menu_item(event_type, [](NSMenuItem* item) {
        return [item action] == @selector(orderFrontStandardAboutPanel:);
    });
}

}  // namespace prism::gui_app

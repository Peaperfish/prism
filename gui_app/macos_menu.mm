#include "macos_menu.hpp"

#import <Cocoa/Cocoa.h>

// The menu item's target: turns the click (or Cmd+,) into an SDL event for the main loop.
@interface PrismPreferencesTarget : NSObject
@property(nonatomic) Uint32 eventType;
- (void)openPreferences:(id)sender;
@end

@implementation PrismPreferencesTarget
- (void)openPreferences:(id)sender {
    (void)sender;
    SDL_Event event;
    SDL_zero(event);
    event.type = self.eventType;
    SDL_PushEvent(&event);
}
@end

namespace prism::gui_app {

bool connect_macos_preferences_item(Uint32 event_type) {
    // A menu item doesn't own its target, so this one lives as long as the app.
    static PrismPreferencesTarget* target = nil;
    NSMenu* main_menu = [NSApp mainMenu];
    if (main_menu == nil || [main_menu numberOfItems] == 0) {
        return false;
    }
    NSMenu* app_menu = [[main_menu itemAtIndex:0] submenu];
    for (NSMenuItem* item in [app_menu itemArray]) {
        if ([[item keyEquivalent] isEqualToString:@","]) {
            if (target == nil) {
                target = [[PrismPreferencesTarget alloc] init];
            }
            target.eventType = event_type;
            [item setTarget:target];
            [item setAction:@selector(openPreferences:)];
            return true;
        }
    }
    return false;
}

}  // namespace prism::gui_app

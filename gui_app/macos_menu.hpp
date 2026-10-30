#pragma once

#include <SDL3/SDL.h>

namespace prism::gui_app {

// macOS only. SDL builds the app menu (Prism -> About, Preferences..., Hide, Quit) but leaves its
// "Preferences..." item (Cmd+,) without an action, so it is always greyed out and SDL offers no
// way to attach one. This finds that item and makes it push an SDL event of `event_type`, so
// Preferences opens from where Mac users look for it. False if the item wasn't found (the app
// then offers Preferences in its own menu bar instead). Call after SDL_Init(SDL_INIT_VIDEO).
bool connect_macos_preferences_item(Uint32 event_type);

// The same for the app menu's "About Prism" item, which otherwise opens the standard macOS About
// panel: it then pushes `event_type`, so the app shows its own About window (version, licence and
// third-party notices). False if the item wasn't found.
bool connect_macos_about_item(Uint32 event_type);

}  // namespace prism::gui_app

#include "shortcut_logic.h"

ShortcutKind getShortcutKind(int shortcutId) {
  if (shortcutId <= 0)
    return ShortcutKind::None;
  if (shortcutId >= 2 && shortcutId <= 5)
    return ShortcutKind::Hold;
  if (shortcutId >= 1000) {
    SDL_Scancode sc = (SDL_Scancode)(shortcutId - 1000);
    switch (sc) {
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RGUI:
      return ShortcutKind::Hold;
    default:
      return ShortcutKind::Discrete;
    }
  }
  return ShortcutKind::Discrete;
}

#ifndef SHORTCUT_LOGIC_H
#define SHORTCUT_LOGIC_H

#include <SDL3/SDL.h>

// Moved here from controller_window.h so this and the two functions
// below can be compiled and unit tested without the rest of that
// header's own dependency weight (GLFW, ImGui, the whole controller
// window struct) - controller_window.h now gets this enum by
// including this header instead of defining it itself.
//
// A shortcut ID's kind: None = shortcut disabled; Hold = the
// effective value differs from the stored setting for exactly as
// long as the key/button is held (never touches the stored setting
// itself); Discrete = a fresh press flips the stored setting once,
// same as clicking the checkbox.
enum class ShortcutKind { None, Hold, Discrete };

// Shortcut ID encoding (see drawShortcutDropdown() in
// settings_window.cpp for the UI side of this):
//   0                 = Off
//   1                 = Middle Mouse Button (used only by Input History's
//                       own built-in Middle-Mouse Click-Through toggle)
//   2..5              = Meta / Ctrl / Shift / Alt (legacy, Hold kind)
//   6..9              = F9..F12 (legacy, Discrete kind)
//   1000 + scancode   = arbitrary SDL_Scancode key. Modifier keys are
//                       Hold kind (temporary flip while held);
//                       everything else is Discrete kind (fresh press
//                       flips the stored setting once).
//
// Pure - int in, enum out, no external state - so it's declared as an
// ordinary function (not a template like the one below) and defined
// once in shortcut_logic.cpp, called directly by both production code
// and its own tests alike.
ShortcutKind getShortcutKind(int shortcutId);

// Numbering matches the dropdown built in settings_window.cpp's
// drawShortcutDropdown() - keep the two in sync if either changes.
// 0=Off, 1=Middle Mouse Button, 2=Meta (hold), 3=Ctrl (hold),
// 4=Shift (hold), 5=Alt (hold), 6=F9, 7=F10, 8=F11, 9=F12.
//
// Generic version of isShortcutPhysicallyActive() (controller_window.cpp)
// - templated on how to check key/mouse state, rather than calling
// GlobalKeyboard::isPressed()/isMouseButtonPressed() directly, since
// those read genuinely live, physical hardware state that a unit test
// has no meaningful way to control. Production code passes those real
// functions in (a thin, near-empty wrapper); a test passes a lambda
// closed over a fixed, fake "pressed" set instead - the actual value
// worth testing is the shortcutId -> which key(s)/button, and how
// they combine (an OR of two, for the modifier-key cases) - mapping,
// not whether a real key happens to be down at the moment a test
// runs.
template <typename IsKeyPressedFn, typename IsMouseButtonPressedFn>
bool isShortcutPhysicallyActiveGeneric(
    int shortcutId, IsKeyPressedFn isKeyPressed,
    IsMouseButtonPressedFn isMouseButtonPressed) {
  if (shortcutId <= 0)
    return false;
  // Arbitrary-key shortcuts (added via "Press any key..." in the
  // dropdown) all share one range - see getShortcutKind() above.
  if (shortcutId >= 1000) {
    SDL_Scancode sc = (SDL_Scancode)(shortcutId - 1000);
    return isKeyPressed(sc);
  }
  switch (shortcutId) {
  case 1:
    return isMouseButtonPressed(2);
  case 2:
    return isKeyPressed(SDL_SCANCODE_LGUI) || isKeyPressed(SDL_SCANCODE_RGUI);
  case 3:
    return isKeyPressed(SDL_SCANCODE_LCTRL) ||
          isKeyPressed(SDL_SCANCODE_RCTRL);
  case 4:
    return isKeyPressed(SDL_SCANCODE_LSHIFT) ||
          isKeyPressed(SDL_SCANCODE_RSHIFT);
  case 5:
    return isKeyPressed(SDL_SCANCODE_LALT) || isKeyPressed(SDL_SCANCODE_RALT);
  case 6:
    return isKeyPressed(SDL_SCANCODE_F9);
  case 7:
    return isKeyPressed(SDL_SCANCODE_F10);
  case 8:
    return isKeyPressed(SDL_SCANCODE_F11);
  case 9:
    return isKeyPressed(SDL_SCANCODE_F12);
  default:
    return false;
  }
}

#endif // SHORTCUT_LOGIC_H

#ifndef KEYBOARD_INPUT_H
#define KEYBOARD_INPUT_H

#include <SDL3/SDL.h>

namespace GlobalKeyboard {

// Starts the native, system-wide keyboard monitor for the current platform.
// On Linux this reads evdev keyboard devices; on Windows it uses a low-level
// keyboard hook; on macOS it uses a listen-only CGEventTap.
//
// Failure is non-fatal: the application can still run with controller input.
bool initialize();
void shutdown();

// Thread-safe snapshot of whether a physical key represented by SDL_Scancode
// is currently held down globally, even when another application has focus.
bool isPressed(SDL_Scancode scancode);

// Returns a short explanation of the current backend/status for diagnostics.
const char *backendName();

// ---- Global mouse state (system-wide) ----
// Returns the current mouse position in screen coordinates (pixels).
void getMousePosition(int &x, int &y);

// Returns the relative movement since the last call (or since last frame).
void getMouseDelta(float &dx, float &dy);

// Returns true if the given mouse button is currently pressed.
// Button codes: 0=left,1=right,2=middle,3=side1,4=side2,5=side3,6=side4,7=extra
bool isMouseButtonPressed(int button);

// ---- Global scroll wheel (system-wide) ----
// Returns the accumulated scroll deltas since the last call (or since last
// frame). Positive dx = scroll right, positive dy = scroll up.
void getScrollDelta(float &dx, float &dy);

} // namespace GlobalKeyboard

#endif // KEYBOARD_INPUT_H
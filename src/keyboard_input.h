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

// ---- Responsiveness / idle CPU trade-off (Windows only) ----
// On Windows, the low-level keyboard/mouse hooks are dispatched from a
// dedicated thread that wakes up on a timer even when there's no input
// at all, to check for pending hook messages promptly. How often it
// wakes is a direct trade-off: a shorter interval means less delay
// between a real key/mouse event happening and this app noticing it,
// but the wake-and-check itself costs a small amount of CPU every time
// it happens - at 1ms, that's up to 1000 wake-ups per second, all the
// time, even sitting completely idle. Longer intervals cut that idle
// cost at the expense of adding up to that same amount of extra input
// latency in the worst case (an event landing just after a check has to
// wait for the next one).
//
// setPollIntervalMs() takes effect immediately, without needing to
// restart the backend - callers can change it any time after
// initialize() has been called. Values are clamped to [1, 16]ms;
// anything looser than that would add input lag well beyond what feels
// responsive for a live input overlay. No-op on Linux/macOS: both of
// those backends are already blocking/event-driven with no equivalent
// poll loop to tune, so there's nothing for this to do there.
void setPollIntervalMs(int ms);
int getPollIntervalMs();

} // namespace GlobalKeyboard

#endif // KEYBOARD_INPUT_H
#ifndef INPUT_HISTORY_TYPES_H
#define INPUT_HISTORY_TYPES_H

// ------------------------------------------------------------------
// Plain data types for a captured input history. Deliberately split
// out from input_history.h (which declares the actual capture/window/
// notation functions) because controller_window.h needs
// InputHistoryEntry for its own struct fields, and input_history.h's
// functions take a controller_window& - putting everything in one
// header would make the two headers include each other.
//
// Capture always records the same structured InputHistoryInput data
// regardless of display style - Raw text, Fighting-game notation, and
// every glyph style are all just different ways of *rendering* the
// same captured entries (see input_history.cpp's draw function), not
// different capture logic. That's what lets someone switch styles on
// the fly without losing history.
// ------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <string>
#include <vector>

enum class InputHistorySource {
  Gamepad,
  Keyboard,
  Mouse,
};

// One physical input that changed state in a given frame. A single
// InputHistoryEntry holds more than one of these only when "merge
// simultaneous presses" is on and multiple inputs landed in the same
// frame (e.g. a two-button throw input in a fighting game).
struct InputHistoryInput {
  InputHistorySource source = InputHistorySource::Gamepad;

  // Valid when source == Gamepad and this is a regular button (face
  // buttons, shoulders, Start/Back/Guide, paddles, touchpad click,
  // stick clicks) - an SDL_GamepadButton index. -1 if this input is a
  // direction (see dpadDigit below) rather than a discrete button.
  int gamepadButton = -1;

  // Valid when source == Gamepad and this input represents the D-pad
  // or a stick's direction rather than a discrete button - numpad
  // notation digit (1-9, 5 = neutral/centered). This is the direction
  // *at the moment this entry was captured*, already resolved by
  // whichever source (D-Pad/Left Stick/Right Stick/Auto) the window's
  // input_history_direction_source setting picked - see
  // computeNumpadDirection() in input_history.cpp. -1 if this input
  // isn't a direction change.
  int dpadDigit = -1;

  // Set when this entry represents a completed compound motion (e.g.
  // "236") rather than a single direction step - see
  // detectMotionCompletion() in input_history.cpp. Holds the motion's
  // display string, matching one of the keys in
  // kKnownMotions/kMotionPatterns, e.g. "236", "623", "360". Pushed as
  // its own additional entry right after the individual direction
  // digits that completed it - those still appear too, this is an
  // extra confirmation entry a glyph style can show as the combined
  // motion icon (see the FGC Motion glyph pack), not a replacement
  // for the individual steps.
  std::string motionMatch;

  // Valid when source == Gamepad and this input is an analog trigger
  // (0 = left, 1 = right, -1 = not a trigger). Triggers are SDL axes,
  // not buttons - SDL_GAMEPAD_BUTTON_COUNT doesn't include them at
  // all, so they need their own rising-edge detection (crossing a
  // press threshold) separate from the regular button loop. Paired
  // with triggerPercent (0-100, how far pressed at the moment this
  // entry was captured - not continuously updated afterward, same
  // "one entry per press event" model as every other input here)
  // for glyph styles to show alongside the trigger icon, since an
  // analog input's depth is part of what actually happened, not just
  // whether it crossed the threshold.
  int triggerAxis = -1;
  float triggerPercent = 0.0f;

  // True when this input is a gyro "flick" (see
  // controller_window::input_history_capture_gyro's doc comment for
  // the detection mechanism). rawLabel already holds the direction
  // ("Left Flick" etc.) - this flag is what tells the glyph renderer
  // to also try the generic "gamepad:gyro" icon (e.g. Steam Deck's
  // gyro glyph) alongside that text, same "icon + extra detail"
  // pattern triggers use for their percentage.
  bool isGyroFlick = false;

  // Valid when source == Keyboard.
  SDL_Scancode key = SDL_SCANCODE_UNKNOWN;

  // Valid when source == Mouse - same 0=left/1=right/2=middle/3+=side
  // numbering used elsewhere in this app (see getMouseButtonName()).
  int mouseButton = -1;

  // Human-readable label for Raw-text style and the persistent log
  // file - e.g. "A", "LB", "W", "Left Click", "6" (a direction digit).
  std::string rawLabel;
};

struct InputHistoryEntry {
  Uint64 timestampMs = 0;
  // Real wall-clock time this entry was captured (milliseconds since
  // the Unix epoch, via std::chrono::system_clock) - separate from
  // timestampMs above, which is SDL_GetTicks() (ms since the app
  // started, not a real date/time) and is what msSincePrevious is
  // computed from. This is only for the optional Date/Time column
  // (see controller_window::input_history_show_timestamp) - formatted
  // to an actual calendar date/time at render time.
  Uint64 wallClockMs = 0;
  // Milliseconds since the previous entry - the actual "frame data"
  // fighting-game notation style is built around. 0 for the very
  // first captured entry (nothing to diff against yet), and also 0
  // (with timingReset set) when the gap exceeded the window's
  // input_history_timing_reset_ms setting - a multi-second pause
  // between inputs isn't meaningful "combo timing", it just means the
  // player stopped, so it's flagged rather than shown as a real
  // number that would misleadingly suggest it was measured.
  Uint64 msSincePrevious = 0;
  bool timingReset = false;
  // >= 0 marks this entry as a hold that just released, holding how
  // long it was held for - drawn as a "(H) X.Xs" suffix after the
  // normal glyph/text rendering (see drawEntryInputCell() in
  // input_history.cpp). -1 (the default) means this is an ordinary
  // entry, not a hold release.
  float holdDurationSeconds = -1.0f;
  std::vector<InputHistoryInput> inputs;
};

// A currently-held input, tracked separately from the scrolling
// history deque above - see captureInputHistory()'s hold-tracking
// pass and drawOneInputHistoryWindow()'s rendering of these (pinned
// at the newest-entry end, live-updating every frame, removed on
// release) in input_history.cpp.
struct ActiveHold {
  std::string identityKey; // e.g. "gp_btn_0", "key_87", "mouse_0" -
                           // unique per physical input, used to
                           // find/update the right entry across frames
  InputHistoryInput input; // reused for glyph/label resolution -
                           // exactly what a normal press of this
                           // input would look like
  Uint64 startMs = 0;
  bool confirmedHold = false; // crossed input_history_hold_threshold_ms -
                              // a hold below that duration never shows
                              // at all (indistinguishable from a tap)
};

#endif

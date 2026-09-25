#include "app_fonts.h"
#include "controller_window.h"
#include "input_history_glyphs.h"
#include "keyboard_input.h"
#include "settings.h"
#include "settings_window.h"

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
// clang-format on

#include "icon_data.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "stb_image.h"

#include <spdlog/spdlog.h>

#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <algorithm>

// See setWindowClickThrough()'s definition in controller_window.cpp - a
// window not registered in `windows` (this module's own GLFW windows
// aren't) falls straight through to the generic GLFW_MOUSE_PASSTHROUGH
// path on every platform, which is exactly what this module wants.
void setWindowClickThrough(GLFWwindow *window, bool enable);

namespace {

// ------------------------------------------------------------------
// Numpad-notation direction (1-9, 5 = neutral) - the actual "frame
// data" grammar fighting-game players read, built from whichever
// input the window's input_history_direction_source setting picks.
// ------------------------------------------------------------------

int numpadFromDpad(bool up, bool down, bool left, bool right) {
  if (up && left)
    return 7;
  if (up && right)
    return 9;
  if (down && left)
    return 1;
  if (down && right)
    return 3;
  if (up)
    return 8;
  if (down)
    return 2;
  if (left)
    return 4;
  if (right)
    return 6;
  return 5;
}

// dx/dy already normalized to roughly [-1, 1]. SDL's stick Y axis is
// negative = up, positive = down (SDL's documented convention, not
// math convention) - so "up" below checks dy < -deadzone, not > 0.
int numpadFromStick(float dx, float dy, float deadzone = 0.5f) {
  return numpadFromDpad(dy < -deadzone, dy > deadzone, dx < -deadzone,
                        dx > deadzone);
}

// Whether a gamepad button index has been marked Invert on any mesh
// it's bound to in the Model table - e.g. a grip sensor that reads
// "pressed" while released and "not pressed" while actually gripped,
// which some controllers (reportedly including a Steam Controller's
// grip sensors) do, and which the Model table already has its own
// per-mesh Invert checkbox to correct for the 3D display. Input
// History's own capture reads the raw SDL button state directly,
// completely independent of which meshes are bound to what - so
// without this, a button set up this way would show as permanently
// "held" in Input History (and pile up in Show Holds) for exactly as
// long as the controller is actually being held normally, which is
// backwards from what actually happened. Mirroring the same Invert
// flag here, rather than adding a separate Input-History-only
// setting, means it's one thing to set per button, not two things
// that could drift out of sync with each other.
//
// This scans the model's meshes fresh each call rather than caching
// a lookup table - mesh count is small (tens, not thousands) and
// this only runs once per button per frame, so the cost is trivial,
// and it stays correct automatically if bindings/inverts change
// while Input History is running, with no separate invalidation to
// remember.
bool isGamepadButtonInverted(controller_window &w, int buttonIdx) {
  std::string target = "gamepad:b" + std::to_string(buttonIdx);
  for (auto &mesh : w.model.meshes) {
    if (mesh.invert && mesh.inputBinding == target)
      return true;
  }
  return false;
}

int computeDpadDigit(controller_window &w) {
  if (!w.is_gamecontroller || !w.sdl_controller)
    return 5;
  bool up = SDL_GetGamepadButton(w.sdl_controller, SDL_GAMEPAD_BUTTON_DPAD_UP);
  bool down =
      SDL_GetGamepadButton(w.sdl_controller, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  bool left =
      SDL_GetGamepadButton(w.sdl_controller, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  bool right =
      SDL_GetGamepadButton(w.sdl_controller, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  return numpadFromDpad(up, down, left, right);
}

int computeStickDigit(controller_window &w, SDL_GamepadAxis axisX,
                      SDL_GamepadAxis axisY) {
  if (!w.is_gamecontroller || !w.sdl_controller)
    return 5;
  float dx = SDL_GetGamepadAxis(w.sdl_controller, axisX) / 32767.0f;
  float dy = SDL_GetGamepadAxis(w.sdl_controller, axisY) / 32767.0f;
  return numpadFromStick(dx, dy);
}

// Resolves the window's input_history_direction_source setting (Auto/
// D-Pad/Left Stick/Right Stick) into an actual numpad digit for this
// frame. Auto prefers the D-Pad whenever it's off-center, falling back
// to the Left Stick - see the setting's own doc comment in
// controller_window.h for why those two specifically.
int computeNumpadDirection(controller_window &w) {
  switch (w.input_history_direction_source) {
  case 1: // D-Pad only
    return computeDpadDigit(w);
  case 2: // Left Stick only
    return computeStickDigit(w, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY);
  case 3: // Right Stick only
    return computeStickDigit(w, SDL_GAMEPAD_AXIS_RIGHTX,
                             SDL_GAMEPAD_AXIS_RIGHTY);
  default: { // Auto
    int dpad = computeDpadDigit(w);
    if (dpad != 5)
      return dpad;
    return computeStickDigit(w, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY);
  }
  }
}

// Which physical input actually produced computeNumpadDirection()'s
// current result - for Raw display's label (see
// directionRawLabel() below), which needs to say specifically
// "D-Pad" or a specific stick, not just a numpad digit (that's
// Fighting-Game Notation's job, not Raw's). In Auto mode this mirrors
// computeNumpadDirection()'s own D-Pad-first preference exactly, so
// the label always matches whichever source the digit actually came
// from.
const char *directionSourceLabel(controller_window &w) {
  switch (w.input_history_direction_source) {
  case 1:
    return "D-Pad";
  case 2:
    return "Left Stick";
  case 3:
    return "Right Stick";
  default:
    return (computeDpadDigit(w) != 5) ? "D-Pad" : "Left Stick";
  }
}

// Plain compass description for a numpad digit, independent of
// source - "Up", "Down-Left", etc. Combined with
// directionSourceLabel() above for Raw display's actual label (e.g.
// "D-Pad Up"), distinct from Fighting-Game Notation's bare digit.
const char *compassDirectionLabel(int digit) {
  switch (digit) {
  case 1:
    return "Down-Left";
  case 2:
    return "Down";
  case 3:
    return "Down-Right";
  case 4:
    return "Left";
  case 6:
    return "Right";
  case 7:
    return "Up-Left";
  case 8:
    return "Up";
  case 9:
    return "Up-Right";
  default:
    return "Neutral";
  }
}

// Full Raw-display label for a direction digit, e.g. "D-Pad Up" -
// captured once at press time (see the capture site below) since the
// source (D-Pad vs a specific stick) needs to be read from the
// controller state at that moment, not reconstructed later from just
// the digit.
std::string directionRawLabel(controller_window &w, int digit) {
  return std::string(directionSourceLabel(w)) + " " +
        compassDirectionLabel(digit);
}

// ------------------------------------------------------------------
// Compound motion detection (236, 623, 360, etc.) - matches the
// window's rolling buffer of recent direction changes
// (input_history_motion_buffer) against a fixed set of known motion
// patterns, in the order a real fighting game's input reader would:
// walk backward from "right now", greedily consuming each pattern
// digit as it's found, in order - so extra/incidental directions in
// between don't break the match (the same kind of leniency real
// games give, e.g. rolling slightly past a quarter-circle's exact
// diagonal still registers). See detectMotionCompletion() below for
// how this gets applied to the live buffer.
//
// "360" isn't literally the digits 3-6-0 (0 isn't a direction at
// all) - it's fighting-game shorthand for a full rotation, so its
// pattern here is the actual 8-point compass sweep instead, in
// either rotational direction.
// ------------------------------------------------------------------

} // namespace

// Not in the anonymous namespace above - exposed with external
// linkage so it's independently testable/callable, matching
// captureInputHistory()'s own visibility.
struct MotionPattern {
  std::string name;   // matches a kKnownMotions entry in settings_window.cpp
  std::string digits; // the actual sequence to pattern-match against
};

// Generates the sliding-window 360 candidates: any 5 consecutive
// points from the 8-point compass cycle, in either rotational
// direction, starting from any of the 8 possible positions. Requiring
// the full, exact 8-point loop (starting and ending at one specific
// digit) turned out to be unrealistically strict - real 360 execution
// on an analog stick rarely traces a perfect closed loop back to the
// exact start, and rarely hits every single one of the 8 compass
// points cleanly either. 5-of-8 leaves enough room for a couple of
// points to get skipped by analog imprecision while still requiring
// a genuine sweep, not just a quarter-circle.
std::vector<MotionPattern> generate360Patterns() {
  const char *cw = "23698741";  // clockwise cycle starting from down
  const char *ccw = "21478963"; // counter-clockwise cycle starting from down
  std::vector<MotionPattern> result;
  for (const char *cycle : {cw, ccw}) {
    std::string doubled = std::string(cycle) + cycle; // handles wraparound
    for (int start = 0; start < 8; ++start) {
      result.push_back({"360", doubled.substr(start, 5)});
    }
  }
  return result;
}

const std::vector<MotionPattern> &motionPatterns() {
  static const std::vector<MotionPattern> patterns = [] {
    std::vector<MotionPattern> p;
    // Named, specific motions are checked before the 360 sweep
    // candidates below, not after - even though the 360 windows are
    // 6 digits long (longer than these 5-digit ones), length isn't
    // actually a good specificity signal here: a clean 41236 (half-
    // circle forward) execution can incidentally satisfy one of the
    // 360 sweep windows too (both trace overlapping arcs), and 360 is
    // fundamentally a generic "swept most of the circle" catch-all,
    // so it should only win when nothing more specific also matched.
    p.push_back({"41236", "41236"}); // half-circle forward
    p.push_back({"63214", "63214"}); // half-circle back
    p.push_back({"21478", "21478"});
    p.push_back({"23698", "23698"});
    p.push_back({"47896", "47896"});
    p.push_back({"69874", "69874"});
    p.push_back({"87412", "87412"});
    p.push_back({"89632", "89632"});
    p.push_back({"236", "236"}); // quarter-circle forward
    p.push_back({"214", "214"}); // quarter-circle back
    p.push_back({"623", "623"}); // dragon-punch motion
    std::vector<MotionPattern> rotations = generate360Patterns();
    p.insert(p.end(), rotations.begin(), rotations.end());
    return p;
  }();
  return patterns;
}

// True if `digits` appears as a subsequence of the buffer (in order,
// not necessarily contiguous) ending at the buffer's most recent
// entry, with the total elapsed time from the first matched digit to
// the last within timeoutMs.
bool matchesMotionDigits(const std::deque<std::pair<int, Uint64>> &buffer,
                         const std::string &digits, int timeoutMs) {
  if (buffer.empty() || digits.empty())
    return false;
  if (buffer.back().first != (digits.back() - '0'))
    return false; // the motion has to complete *now*, on the latest input

  int patternIdx = (int)digits.size() - 1;
  Uint64 lastTs = buffer.back().second;
  Uint64 firstMatchedTs = lastTs;
  for (auto it = buffer.rbegin(); it != buffer.rend() && patternIdx >= 0; ++it) {
    if (it->first == digits[patternIdx] - '0') {
      firstMatchedTs = it->second;
      --patternIdx;
    }
  }
  if (patternIdx >= 0)
    return false; // ran out of buffer before matching every digit

  return (lastTs - firstMatchedTs) <= (Uint64)timeoutMs;
}

// Checks the window's motion buffer against every known pattern
// (longest/most specific first) and returns the matched motion's
// display name (e.g. "236"), or an empty string if nothing matched.
// Longer motions get proportionally more time than shorter ones -
// input_history_motion_timeout_ms is calibrated to a 2-transition
// quarter-circle (236/214), and scales up from there, since a flat
// window for every motion regardless of length would make longer
// ones (particularly 360's 7-transition full rotation) nearly
// impossible to complete in time - real games show the same kind of
// scaling (SF6: ~183ms for quarter-circles vs. ~533ms for 360s,
// roughly 80-90ms per transition either way).
std::string detectMotionCompletion(controller_window &w) {
  for (const auto &pattern : motionPatterns()) {
    int transitions = (int)pattern.digits.size() - 1;
    int scaledTimeout =
        w.input_history_motion_timeout_ms * std::max(transitions, 1) / 2;
    if (matchesMotionDigits(w.input_history_motion_buffer, pattern.digits,
                            scaledTimeout)) {
      return pattern.name;
    }
  }
  return "";
}

namespace {

const char *mouseButtonLabel(int button) {
  switch (button) {
  case 0:
    return "Left Click";
  case 1:
    return "Right Click";
  case 2:
    return "Middle Click";
  case 3:
    return "Mouse 4";
  case 4:
    return "Mouse 5";
  default:
    return "Mouse Button";
  }
}

// Title-cases and space-separates an SDL scancode name for the Raw
// display style - SDL_GetScancodeName() already returns something
// reasonable ("W", "Left Ctrl", "Space"), so this just guards against
// a null/unknown name rather than reformatting.
std::string keyboardLabel(SDL_Scancode sc) {
  const char *name = SDL_GetScancodeName(sc);
  if (!name || !*name)
    return "Key " + std::to_string((int)sc);
  return name;
}

// ------------------------------------------------------------------
// Persistent per-window log file - see controller_window::
// input_history_log_to_file's doc comment in controller_window.h for
// why this exists alongside the live in-window ring buffer (a
// speedrunner scrolling back through what they actually did, not just
// what's visible in the live overlay right now).
// ------------------------------------------------------------------

void openInputHistoryLog(controller_window &w) {
  if (w.input_history_log_file || !w.input_history_log_to_file)
    return;

  std::string dir = config_base_path + "/input_history_logs";
  std::filesystem::create_directories(dir);

  std::time_t t = std::time(nullptr);
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);

  // Sanitize the window title for use in a filename - same idea as
  // check_filename_valid() elsewhere in this app, kept local here to
  // avoid depending on settings_window.cpp for one string helper.
  std::string safeTitle;
  for (char c : w.window_title)
    safeTitle += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';

  w.input_history_log_path =
      dir + "/" + safeTitle + "_" + stamp + ".log";
  w.input_history_log_file = fopen(w.input_history_log_path.c_str(), "w");
  if (w.input_history_log_file) {
    spdlog::info("Input History: logging '{}' to {}", w.window_title,
                 w.input_history_log_path);
  } else {
    spdlog::error("Input History: failed to open log file {}",
                  w.input_history_log_path);
  }
}

void closeInputHistoryLog(controller_window &w) {
  if (w.input_history_log_file) {
    fclose(w.input_history_log_file);
    w.input_history_log_file = nullptr;
  }
}

void writeEntryToLog(controller_window &w, const InputHistoryEntry &entry) {
  if (!w.input_history_log_file)
    return;
  std::string combined;
  for (size_t i = 0; i < entry.inputs.size(); ++i) {
    if (i)
      combined += "+";
    combined += entry.inputs[i].rawLabel;
  }
  fprintf(w.input_history_log_file, "[%llu] (+%llums) %s\n",
         (unsigned long long)entry.timestampMs,
         (unsigned long long)entry.msSincePrevious, combined.c_str());
  fflush(w.input_history_log_file); // a crash/force-quit shouldn't lose the tail
}

// ------------------------------------------------------------------
// Capture: appends a new entry, or - if input_history_merge_simultaneous
// is on and something was already captured this same frame - folds
// this input into that entry instead of starting a new row.
// ------------------------------------------------------------------

void pushInputEvent(controller_window &w, InputHistoryInput input,
                    float holdDurationSeconds = -1.0f) {
  Uint64 now = SDL_GetTicks();

  // "Simultaneous" is now a real, configurable time window rather than
  // "landed in the same capture() call" (which was as tight as ~16ms
  // at 60fps - stricter than what a human press of two buttons "at
  // once" usually looks like, routinely 30-80ms apart). A hold-release
  // entry always gets its own new entry rather than merging into a
  // recent one - it represents a distinct, already-timed event of its
  // own, not a simultaneous press alongside something else.
  if (w.input_history_merge_simultaneous && holdDurationSeconds < 0.0f &&
      !w.input_history_entries.empty() &&
      (now - w.input_history_entries.back().timestampMs) <=
          (Uint64)w.input_history_simultaneous_window_ms) {
    w.input_history_entries.back().inputs.push_back(std::move(input));
    writeEntryToLog(w, w.input_history_entries.back());
    return;
  }

  InputHistoryEntry entry;
  entry.timestampMs = now;
  entry.holdDurationSeconds = holdDurationSeconds;
  entry.wallClockMs =
      (Uint64)std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  entry.msSincePrevious =
      w.input_history_last_event_ms ? (now - w.input_history_last_event_ms) : 0;
  // A gap longer than the configured reset window isn't meaningful
  // "combo timing" - it just means the player paused - so it's
  // flagged rather than shown as a real measured number (see
  // InputHistoryEntry::timingReset's doc comment).
  if (w.input_history_last_event_ms &&
      entry.msSincePrevious > (Uint64)w.input_history_timing_reset_ms) {
    entry.msSincePrevious = 0;
    entry.timingReset = true;
  }
  entry.inputs.push_back(std::move(input));

  w.input_history_entries.push_back(entry);
  while ((int)w.input_history_entries.size() > w.input_history_length)
    w.input_history_entries.pop_front();

  w.input_history_last_event_ms = now;

  writeEntryToLog(w, entry);
}

} // namespace

void setInputHistoryEnabled(controller_window &w, bool enabled) {
  w.input_history_enabled = enabled;
  if (enabled) {
    openInputHistoryLog(w);
    // Re-baseline edge detection so turning this on mid-session
    // doesn't immediately fire "pressed" for every button/key already
    // held down at that moment.
    w.input_history_gamepad_state_initialized = false;
    w.input_history_key_state_initialized = false;
    w.input_history_mouse_state_initialized = false;
  } else {
    closeInputHistoryLog(w);
  }
}

// ------------------------------------------------------------------
// Per-frame capture - called from controller_window.cpp's existing
// per-window input-processing pass (see the g_log_controller/keyboard/
// mouse block it sits alongside). Reads the physical device directly
// (SDL_GetGamepadButton/Axis, GlobalKeyboard::isPressed/
// isMouseButtonPressed) rather than going through mesh bindings, since
// Input History is meant to reflect the raw device, not just whichever
// inputs happen to have a mesh assigned.
// ------------------------------------------------------------------
void captureInputHistory(controller_window &w) {
  if (!w.input_history_enabled)
    return;


  // ---- Gamepad/Joystick ----
  if (w.input_history_capture_gamepad && w.is_gamecontroller &&
      w.sdl_controller) {
    int prevDigit = w.input_history_last_dpad_dir;
    int digit = computeNumpadDirection(w);
    bool digitChanged = digit != prevDigit;
    // A "return to neutral" (digit 5) is its own opt-in setting - most
    // players don't want a fresh entry every time they let go of the
    // stick, only for the digit that actually mattered on the way
    // there. Still updates input_history_last_dpad_dir either way, so
    // the *next* real direction change is correctly detected as a
    // transition regardless of whether the neutral step itself got
    // logged.
    bool shouldLog =
        digitChanged && (digit != 5 || w.input_history_show_neutral_direction);
    if (w.input_history_gamepad_state_initialized && shouldLog) {
      InputHistoryInput in;
      in.source = InputHistorySource::Gamepad;
      in.dpadDigit = digit;
      in.rawLabel = directionRawLabel(w, digit);
      pushInputEvent(w, in);
    }
    // Motion-buffer tracking is independent of shouldLog/Show Return
    // to Neutral above (that only controls what's *displayed*) - real
    // motions never pass through digit 5 anyway (see kMotionPatterns),
    // so only non-neutral transitions are worth buffering at all.
    if (w.input_history_gamepad_state_initialized && digitChanged && digit != 5) {
      Uint64 now = SDL_GetTicks();
      w.input_history_motion_buffer.push_back({digit, now});
      while (w.input_history_motion_buffer.size() > 20)
        w.input_history_motion_buffer.pop_front();

      std::string matched = detectMotionCompletion(w);
      if (!matched.empty()) {
        InputHistoryInput motionIn;
        motionIn.source = InputHistorySource::Gamepad;
        motionIn.motionMatch = matched;
        motionIn.rawLabel = matched;
        pushInputEvent(w, motionIn);
        // A completed motion consumes its buffer - without this, the
        // same trailing digits could immediately re-match (e.g. the
        // "236" tail of a "41236" would otherwise also fire its own
        // separate 236 entry the very next frame).
        w.input_history_motion_buffer.clear();
      }
    }
    w.input_history_last_dpad_dir = digit;

    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 64; ++b) {
      // D-Pad buttons are skipped here when the direction source
      // setting actually reads D-Pad state (Auto or D-Pad) - in that
      // case they're already captured via the numpad-direction system
      // above, and capturing them again here produced a duplicate
      // entry for every D-Pad press (both "2" and "D-Pad Down", or
      // both the down glyph and "2" in a glyph style). If direction
      // source is set to a stick instead, D-Pad presses wouldn't be
      // represented anywhere otherwise, so they still go through the
      // normal button path in that case.
      bool dpadHandledByDirection =
          w.input_history_direction_source == 0 || w.input_history_direction_source == 1;
      if (dpadHandledByDirection &&
          (b == SDL_GAMEPAD_BUTTON_DPAD_UP || b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
           b == SDL_GAMEPAD_BUTTON_DPAD_LEFT || b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
        continue;
      // Marked Ignore for this display style (see
      // isRawButtonIgnored()'s own doc comment) - skip entirely, not
      // just "no glyph for it": no discrete press entry, and (since
      // input_history_last_gamepad_button[b] below never gets touched
      // for this index) no hold tracking either, which reads that
      // same array. "- 2" converts input_history_display_style (0=Raw,
      // 1=Notation, 2+=actual glyph styles) into listGlyphStyles()'s
      // own indexing (0-based, glyph styles only) - the same
      // conversion drawEntryInputCell() already does for glyph
      // lookups. Missing this is exactly what made Ignore Button
      // silently check the wrong style (or an out-of-range index that
      // safely no-ops) instead of the one actually selected.
      if (w.input_history_display_style >= 2 &&
          isRawButtonIgnored(w.input_history_display_style - 2, b)) {
        // Force-clear rather than just skip: if this button was
        // already held (true) the moment it became ignored - a real
        // scenario for an inverted grip sensor, which reads "pressed"
        // for as long as the controller is actually being held
        // normally - leaving its stored state untouched would leave
        // it stuck at true forever (nothing else ever writes it once
        // skipped), which is exactly what kept its hold timer running
        // indefinitely until the app was restarted. Also drops any
        // hold already being tracked for it, so an in-progress one
        // doesn't linger either.
        w.input_history_last_gamepad_button[b] = false;
        std::string ignoredKey = "gp_btn_" + std::to_string(b);
        for (auto it = w.input_history_active_holds.begin();
            it != w.input_history_active_holds.end();) {
          if (it->identityKey == ignoredKey)
            it = w.input_history_active_holds.erase(it);
          else
            ++it;
        }
        continue;
      }
      bool pressed =
          SDL_GetGamepadButton(w.sdl_controller, (SDL_GamepadButton)b);
      // See isGamepadButtonInverted()'s own doc comment - this is what
      // keeps an inverted button (e.g. a grip sensor wired backwards)
      // from showing as permanently held in Input History for as long
      // as the controller is actually being held normally.
      if (isGamepadButtonInverted(w, b))
        pressed = !pressed;
      if (w.input_history_gamepad_state_initialized && pressed &&
          !w.input_history_last_gamepad_button[b]) {
        InputHistoryInput in;
        in.source = InputHistorySource::Gamepad;
        in.gamepadButton = b;
        in.rawLabel =
            (b >= 0 && b < 21) ? button_names[b] : ("Button " + std::to_string(b));
        pushInputEvent(w, in);
      }
      w.input_history_last_gamepad_button[b] = pressed;
    }

    // ---- Triggers ----
    // Triggers are SDL axes (SDL_GAMEPAD_AXIS_LEFT_TRIGGER/RIGHT_TRIGGER),
    // not part of SDL_GAMEPAD_BUTTON_COUNT at all - the button loop
    // above never sees them, which is exactly why they weren't being
    // registered before. "Pressed" for an analog input is a threshold
    // crossing (input_history_trigger_threshold) rather than a literal
    // on/off state; the percent pulled at the moment of that crossing
    // is captured alongside it (triggerPercent) so a glyph style can
    // show "how far", not just "that it happened".
    for (int ti = 0; ti < 2; ++ti) {
      SDL_GamepadAxis axis =
          ti == 0 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
      float value = SDL_GetGamepadAxis(w.sdl_controller, axis) / 32767.0f;
      bool pressed = value >= w.input_history_trigger_threshold;
      if (w.input_history_gamepad_state_initialized && pressed &&
          !w.input_history_last_trigger_state[ti]) {
        InputHistoryInput in;
        in.source = InputHistorySource::Gamepad;
        in.triggerAxis = ti;
        in.triggerPercent = value * 100.0f;
        in.rawLabel = (ti == 0 ? "LT " : "RT ") +
                     std::to_string((int)(in.triggerPercent + 0.5f)) + "%";
        pushInputEvent(w, in);
      }
      w.input_history_last_trigger_state[ti] = pressed;
    }

    w.input_history_gamepad_state_initialized = true;
  }

  // ---- Gyro (flicks) ----
  // Independent of input_history_capture_gamepad above - gated on its
  // own toggle plus the window's gyro actually being enabled at all
  // (w.gyro_enabled; gyro_data is meaningless/stale otherwise). See
  // controller_window::input_history_capture_gyro's doc comment for
  // the full detection mechanism (threshold + rolling-window
  // accumulation + cooldown) and why it doesn't just log continuous
  // motion.
  if (w.input_history_capture_gyro && w.gyro_enabled) {
    Uint64 now = SDL_GetTicks();
    // SDL gyro sensors report angular velocity in radians/sec (SDL's
    // documented convention) - gyro_data[0]/[1] are already being read
    // that way elsewhere in this file's existing gyro-to-rotation
    // code, matched here for consistency. Sign/axis convention is a
    // best-effort default (like the app's other axis Invert
    // checkboxes elsewhere) - "yaw" drives left/right, "pitch" drives
    // up/down.
    float yawRateDeg = w.gyro_data[1] * (180.0f / 3.14159265f);
    float pitchRateDeg = w.gyro_data[0] * (180.0f / 3.14159265f);

    bool active = fabs(yawRateDeg) > w.input_history_gyro_threshold ||
                 fabs(pitchRateDeg) > w.input_history_gyro_threshold;

    if (active) {
      if (now - w.input_history_gyro_window_start_ms >
          (Uint64)w.input_history_gyro_flick_window_ms) {
        // Rolling window expired since the last time this crossed the
        // sensitivity threshold - start accumulating fresh rather than
        // keeping stale motion from a much earlier, unrelated moment.
        w.input_history_gyro_accum_yaw = 0.0f;
        w.input_history_gyro_accum_pitch = 0.0f;
        w.input_history_gyro_window_start_ms = now;
      }
      w.input_history_gyro_accum_yaw += yawRateDeg * (float)w.deltaTime;
      w.input_history_gyro_accum_pitch += pitchRateDeg * (float)w.deltaTime;

      bool cooldownOk = (now - w.input_history_gyro_last_flick_ms) >
                        (Uint64)w.input_history_gyro_flick_cooldown_ms;
      float absYaw = fabs(w.input_history_gyro_accum_yaw);
      float absPitch = fabs(w.input_history_gyro_accum_pitch);

      if (cooldownOk &&
          (absYaw > w.input_history_gyro_flick_threshold ||
          absPitch > w.input_history_gyro_flick_threshold)) {
        InputHistoryInput in;
        in.source = InputHistorySource::Gamepad;
        in.isGyroFlick = true;
        if (absYaw > absPitch) {
          in.rawLabel = w.input_history_gyro_accum_yaw > 0 ? "Right Flick"
                                                            : "Left Flick";
        } else {
          in.rawLabel = w.input_history_gyro_accum_pitch > 0 ? "Up Flick"
                                                              : "Down Flick";
        }
        pushInputEvent(w, in);

        w.input_history_gyro_last_flick_ms = now;
        w.input_history_gyro_accum_yaw = 0.0f;
        w.input_history_gyro_accum_pitch = 0.0f;
      }
    }
  }

  // ---- Keyboard ----
  if (w.input_history_capture_keyboard) {
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
      SDL_Scancode sc = static_cast<SDL_Scancode>(i);
      bool pressed = GlobalKeyboard::isPressed(sc);
      // Only bother building the "keyboard:key_x" binding string and
      // checking it when the key is actually down - checking all
      // ~512 scancodes' Ignore status every single frame regardless
      // of state would be wasted work for the near-total majority
      // that are never pressed at any given moment.
      if (pressed && w.input_history_display_style >= 2) {
        const char *name = SDL_GetScancodeName(sc);
        if (name && name[0]) {
          std::string key = "key_";
          for (const char *p = name; *p; ++p)
            key.push_back((char)tolower((unsigned char)*p));
          if (isInputIgnored(w.input_history_display_style - 2,
                             "keyboard:" + key)) {
            // Same force-clear reasoning as the gamepad case above -
            // if this key was already held the moment it became
            // ignored, leaving its stored state untouched would leave
            // it stuck forever.
            pressed = false;
            std::string ignoredKey = "key_" + std::to_string(i);
            for (auto it = w.input_history_active_holds.begin();
                it != w.input_history_active_holds.end();) {
              if (it->identityKey == ignoredKey)
                it = w.input_history_active_holds.erase(it);
              else
                ++it;
            }
          }
        }
      }
      if (w.input_history_key_state_initialized && pressed &&
          !w.input_history_last_key_state[i]) {
        InputHistoryInput in;
        in.source = InputHistorySource::Keyboard;
        in.key = sc;
        in.rawLabel = keyboardLabel(sc);
        pushInputEvent(w, in);
      }
      w.input_history_last_key_state[i] = pressed;
    }
    w.input_history_key_state_initialized = true;
  }

  // ---- Mouse ----
  if (w.input_history_capture_mouse) {
    // Matches the Model table's own mouse_left/mouse_right/mouse_middle/
    // mouse_4../mouse_8 binding-string naming exactly, so an Ignore
    // Button row picked from that same list matches the right button
    // here.
    static const char *kMouseBindingNames[8] = {
        "mouse_left", "mouse_right", "mouse_middle", "mouse_4",
        "mouse_5",    "mouse_6",     "mouse_7",      "mouse_8"};
    for (int b = 0; b < 8; ++b) {
      bool pressed = GlobalKeyboard::isMouseButtonPressed(b);
      if (pressed && w.input_history_display_style >= 2 &&
          isInputIgnored(w.input_history_display_style - 2,
                         std::string("mouse:") + kMouseBindingNames[b])) {
        pressed = false;
        std::string ignoredKey = "mouse_" + std::to_string(b);
        for (auto it = w.input_history_active_holds.begin();
            it != w.input_history_active_holds.end();) {
          if (it->identityKey == ignoredKey)
            it = w.input_history_active_holds.erase(it);
          else
            ++it;
        }
      }
      if (w.input_history_mouse_state_initialized && pressed &&
          !w.input_history_last_mouse_state[b]) {
        InputHistoryInput in;
        in.source = InputHistorySource::Mouse;
        in.mouseButton = b;
        in.rawLabel = mouseButtonLabel(b);
        pushInputEvent(w, in);
      }
      w.input_history_last_mouse_state[b] = pressed;
    }
    w.input_history_mouse_state_initialized = true;
  }

  // ---- Hold detection ----
  // A unified pass across every source, reusing the "last state"
  // arrays already updated above (all reflect *this* frame's actual
  // state by this point) rather than re-polling hardware - see
  // ActiveHold's doc comment in input_history_types.h. Skips D-Pad
  // buttons the same way the regular button-capture loop above does,
  // for the same reason (already represented via direction).
  if (w.input_history_show_holds) {
    Uint64 now = SDL_GetTicks();
    std::vector<std::string> stillPressed;

    auto trackHold = [&](const std::string &key, InputHistorySource source,
                         int gamepadButton, SDL_Scancode scancode,
                         int mouseButton, int dpadDigit,
                         const std::string &label) {
      stillPressed.push_back(key);
      for (auto &h : w.input_history_active_holds) {
        if (h.identityKey == key) {
          if (!h.confirmedHold &&
              now - h.startMs >= (Uint64)w.input_history_hold_threshold_ms)
            h.confirmedHold = true;
          return;
        }
      }
      ActiveHold nh;
      nh.identityKey = key;
      nh.startMs = now;
      nh.input.source = source;
      nh.input.gamepadButton = gamepadButton;
      nh.input.key = scancode;
      nh.input.mouseButton = mouseButton;
      nh.input.dpadDigit = dpadDigit;
      nh.input.rawLabel = label;
      w.input_history_active_holds.push_back(nh);
    };

    if (w.input_history_capture_gamepad) {
      bool dpadHandledByDirection = w.input_history_direction_source == 0 ||
                                   w.input_history_direction_source == 1;
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 64; ++b) {
        if (dpadHandledByDirection &&
            (b == SDL_GAMEPAD_BUTTON_DPAD_UP || b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
             b == SDL_GAMEPAD_BUTTON_DPAD_LEFT || b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
          continue;
        if (!w.input_history_last_gamepad_button[b])
          continue;
        std::string label = (b >= 0 && b < 21) ? button_names[b]
                                               : ("Button " + std::to_string(b));
        trackHold("gp_btn_" + std::to_string(b), InputHistorySource::Gamepad, b,
                  SDL_SCANCODE_UNKNOWN, -1, -1, label);
      }
    }
    if (w.input_history_capture_keyboard) {
      for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
        if (!w.input_history_last_key_state[i])
          continue;
        SDL_Scancode sc = static_cast<SDL_Scancode>(i);
        trackHold("key_" + std::to_string(i), InputHistorySource::Keyboard, -1,
                  sc, -1, -1, keyboardLabel(sc));
      }
    }
    if (w.input_history_capture_mouse) {
      for (int b = 0; b < 8; ++b) {
        if (!w.input_history_last_mouse_state[b])
          continue;
        trackHold("mouse_" + std::to_string(b), InputHistorySource::Mouse, -1,
                  SDL_SCANCODE_UNKNOWN, b, -1, mouseButtonLabel(b));
      }
    }
    // Direction holds - D-Pad and stick both funnel into the same
    // numpad digit (see computeNumpadDirection()/Direction Source),
    // so holding either is tracked the same way a button hold is,
    // through the same trackHold() path. Neutral (5) never counts as
    // a hold, same as it never gets its own discrete entry either
    // unless Show Return to Neutral is on - digit 5 isn't "holding a
    // direction", it's the absence of one. A genuine direction change
    // (say 6 to 3) is treated as a new, separate hold rather than a
    // continuation, matching how a player would actually describe it.
    // input_history_gamepad_state_initialized guards against
    // input_history_last_dpad_dir's default value (0 - not a real
    // direction; valid values are 1-9) being read as a genuine held
    // direction before any real controller reading has ever happened -
    // without this, a "0" hold would appear to be held from the
    // instant the app starts and only clear once a real controller
    // connects and overwrites it with an actual reading, which is
    // exactly the bug this guard exists to prevent (the discrete
    // capture path above already has this same guard).
    if (w.input_history_capture_gamepad &&
        w.input_history_gamepad_state_initialized &&
        w.input_history_last_dpad_dir != 5) {
      int digit = w.input_history_last_dpad_dir;
      trackHold("gp_dir_" + std::to_string(digit), InputHistorySource::Gamepad,
                -1, SDL_SCANCODE_UNKNOWN, -1, digit, directionRawLabel(w, digit));
    }

    // Anything not in stillPressed this frame has been released - log
    // a permanent history entry for any hold that was confirmed (a
    // hold that never crossed the threshold never showed live either,
    // so it shouldn't suddenly appear in history just because it
    // ended - consistent with how it was treated the whole time it
    // was held).
    for (const auto &h : w.input_history_active_holds) {
      bool stillHeld = std::find(stillPressed.begin(), stillPressed.end(),
                                 h.identityKey) != stillPressed.end();
      if (!stillHeld && h.confirmedHold) {
        float heldSeconds = (now - h.startMs) / 1000.0f;
        pushInputEvent(w, h.input, heldSeconds);
      }
    }
    w.input_history_active_holds.erase(
        std::remove_if(w.input_history_active_holds.begin(),
                       w.input_history_active_holds.end(),
                       [&](const ActiveHold &h) {
                         return std::find(stillPressed.begin(),
                                         stillPressed.end(),
                                         h.identityKey) == stillPressed.end();
                       }),
        w.input_history_active_holds.end());
  } else if (!w.input_history_active_holds.empty()) {
    w.input_history_active_holds.clear();
  }
}

// ------------------------------------------------------------------
// Display styles
//
// Index 0 = Raw, 1 = Fighting-Game Notation, 2..N+1 = one of the
// glyph styles listGlyphStyles() discovers by scanning glyphs/ - see
// input_history_glyphs.h for why this is fully dynamic now rather
// than a fixed list: a user-created custom mapping shows up here
// exactly the same way a bundled one does.
// ------------------------------------------------------------------

int inputHistoryGlyphStyleCount() { return (int)listGlyphStyles().size(); }

std::string inputHistoryDisplayStyleName(int displayStyleIndex) {
  if (displayStyleIndex == 0)
    return "Raw";
  if (displayStyleIndex == 1)
    return "Fighting-Game Notation";
  int glyphIdx = displayStyleIndex - 2;
  const auto &styles = listGlyphStyles();
  if (glyphIdx >= 0 && glyphIdx < (int)styles.size())
    return styles[glyphIdx].displayName;
  return "Unknown";
}

#if defined(_WIN32)
// Real per-pixel transparency for the Input History window, on
// Windows - now uses controller_window.cpp's shared CompanionWindow
// type/functions (createCompanionWindow/destroyCompanionWindow/
// updateCompanionWindow) directly at each call site below, instead of
// a second, independent implementation. That duplication was a
// deliberate choice early on (avoiding risk to the already-proven
// controller-window path, since none of this was testable on real
// Windows hardware from here) - but the trade-off didn't hold up: the
// duplicate silently fell behind the original and was missing several
// things it should have had from day one (WM_NCHITTEST entirely,
// keyboard forwarding, SetCapture during drags). One shared
// implementation now, verified against real MinGW/Windows headers via
// a cross-compiler syntax check even without hardware to run it on.
#endif


namespace {

// Maps a captured InputHistoryInput to a controller-glyph logical name,
// for glyph display styles. Returns false (no icon) for inputs a
// controller glyph pack has no concept of (keyboard keys, mouse
// buttons) - those still render as text even in a "glyph style",
// falling back gracefully rather than leaving a blank space.
// Converts a captured InputHistoryInput into the string key format
// glyph mapping info.json files use (see input_history_glyphs.h's big
// comment) - gamepad buttons/dpad/triggers/directions, keyboard
// scancodes, and mouse buttons all resolve to one. Returns empty for
// anything that never gets a key (there isn't one for every possible
// value, e.g. an unrecognized gamepad button index).
std::string inputKeyFor(const InputHistoryInput &in) {
  if (in.source == InputHistorySource::Keyboard) {
    const char *name = SDL_GetScancodeName(in.key);
    if (!name || !*name)
      return "";
    std::string lowered;
    for (const char *p = name; *p; ++p)
      lowered.push_back((char)tolower((unsigned char)*p));
    return "keyboard:" + lowered;
  }
  if (in.source == InputHistorySource::Mouse) {
    if (in.mouseButton >= 0 && in.mouseButton <= 2)
      return "mouse:" + std::to_string(in.mouseButton);
    return "mouse:default";
  }
  if (in.source != InputHistorySource::Gamepad)
    return "";

  if (in.triggerAxis == 0)
    return "gamepad:trigger:left";
  if (in.triggerAxis == 1)
    return "gamepad:trigger:right";
  if (in.isGyroFlick)
    return "gamepad:gyro";
  if (!in.motionMatch.empty())
    return "gamepad:direction:" + in.motionMatch;
  if (in.dpadDigit >= 0)
    return "gamepad:direction:" + std::to_string(in.dpadDigit);
  if (in.gamepadButton < 0)
    return "";

  switch (in.gamepadButton) {
  case SDL_GAMEPAD_BUTTON_SOUTH:
    return "gamepad:button:south";
  case SDL_GAMEPAD_BUTTON_EAST:
    return "gamepad:button:east";
  case SDL_GAMEPAD_BUTTON_WEST:
    return "gamepad:button:west";
  case SDL_GAMEPAD_BUTTON_NORTH:
    return "gamepad:button:north";
  case SDL_GAMEPAD_BUTTON_BACK:
    return "gamepad:button:back";
  case SDL_GAMEPAD_BUTTON_GUIDE:
    return "gamepad:button:guide";
  case SDL_GAMEPAD_BUTTON_START:
    return "gamepad:button:start";
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
    return "gamepad:button:stick_left_click";
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    return "gamepad:button:stick_right_click";
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    return "gamepad:button:shoulder_left";
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    return "gamepad:button:shoulder_right";
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return "gamepad:dpad:up";
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return "gamepad:dpad:down";
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return "gamepad:dpad:left";
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return "gamepad:dpad:right";
  case SDL_GAMEPAD_BUTTON_MISC1:
    return "gamepad:button:misc1";
  case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
    return "gamepad:button:paddle_left1";
  case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
    return "gamepad:button:paddle_right1";
  case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
    return "gamepad:button:paddle_left2";
  case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
    return "gamepad:button:paddle_right2";
  case SDL_GAMEPAD_BUTTON_TOUCHPAD:
    return "gamepad:button:touchpad";
  default:
    return "";
  }
}

// Renders just the "input" cell content for one entry - Raw and
// Notation both render as plain text (their only real difference right
// now is that Notation already gets numpad-digit-style direction
// labels from capture, same as Raw - the distinction matters more once
// FGC Motion glyphs are wired in for direction sequences), or a row of
// glyph icons for the icon-pack styles. Timing (if enabled) is no
// longer rendered here - it's now an independent setting shown in its
// own table column by the caller, for any style, not just Notation -
// see controller_window::input_history_show_timing's doc comment.
void drawEntryInputCell(const InputHistoryEntry &entry, int displayStyle,
                        int glyphSize, int fontSize) {
  if (displayStyle == 0 || displayStyle == 1) {
    // ---- Raw / Fighting-Game Notation ----
    std::string line;
    for (size_t i = 0; i < entry.inputs.size(); ++i) {
      if (i)
        line += displayStyle == 1 ? "+" : " + ";
      const InputHistoryInput &in = entry.inputs[i];
      // Notation shows a direction as its numpad digit (or motion
      // string) - Raw shows the plain description (e.g. "D-Pad Up",
      // held in rawLabel) instead. These read very differently on
      // purpose: Raw describes the physical input in plain terms,
      // Notation is specifically fighting-game numpad notation - a
      // direction showing up as a bare digit in Raw mode (or a plain
      // description in Notation mode) would blur that distinction
      // away entirely.
      if (displayStyle == 1 && !in.motionMatch.empty())
        line += in.motionMatch;
      else if (displayStyle == 1 && in.dpadDigit >= 0)
        line += std::to_string(in.dpadDigit);
      else
        line += in.rawLabel;
    }
    if (entry.holdDurationSeconds >= 0.0f) {
      char suffix[32];
      snprintf(suffix, sizeof(suffix), " (H) %.1fs", entry.holdDurationSeconds);
      line += suffix;
    }
    // Raw/Notation text size is independent of the glyph icon size -
    // ImGui has no per-widget font size, so this scales the window's
    // current font for just this one draw call and resets it right
    // after, rather than leaking the scale into whatever's drawn next
    // (the timing/timestamp columns, or the next row).
    float scale = fontSize / 16.0f; // 16px is ImGui's typical default
    ImGui::SetWindowFontScale(scale);
    ImGui::TextUnformatted(line.c_str());
    ImGui::SetWindowFontScale(1.0f);
    return;
  }

  // ---- Glyph style ----
  int glyphIdx = displayStyle - 2;
  const auto &styles = listGlyphStyles();
  if (glyphIdx < 0 || glyphIdx >= (int)styles.size()) {
    ImGui::TextUnformatted("?");
    return;
  }

  float rowStartY = ImGui::GetCursorPosY();
  bool firstOnLine = true;
  for (const auto &in : entry.inputs) {
    if (!firstOnLine)
      ImGui::SameLine();
    firstOnLine = false;

    // Now tries a glyph for directions too (styles like FGC Motion
    // define direction art - InputIcon_1.png..9.png, plus compound
    // motions - where the earlier hardcoded-enum system never had
    // anywhere for those to live), falling back to the plain digit as
    // text when this style doesn't have one, same as any other input.
    std::string key = inputKeyFor(in);
    GLuint tex = key.empty() ? 0 : getGlyphTexture(glyphIdx, key);

    // Most ordinary controller packs (Xbox Series, PS5, etc.) don't
    // define "gamepad:direction:N" at all - they represent the D-Pad
    // as a physical button, under "gamepad:dpad:up/down/left/right".
    // For the four cardinal digits specifically (2/4/6/8, which map
    // directly onto an actual D-Pad button), fall back to that key if
    // the direction one isn't defined for this style, so those packs'
    // existing D-Pad icons are found - a style with dedicated
    // direction art (FGC Motion) already matched on the first try
    // above and never reaches this. Diagonals (1/3/7/9) have no
    // single-button equivalent to fall back to, so they still
    // correctly fall through to plain text on a style with no
    // direction art of their own.
    if (!tex && in.dpadDigit >= 0) {
      const char *dpadKey = nullptr;
      switch (in.dpadDigit) {
      case 2:
        dpadKey = "gamepad:dpad:down";
        break;
      case 4:
        dpadKey = "gamepad:dpad:left";
        break;
      case 6:
        dpadKey = "gamepad:dpad:right";
        break;
      case 8:
        dpadKey = "gamepad:dpad:up";
        break;
      default:
        break;
      }
      if (dpadKey)
        tex = getGlyphTexture(glyphIdx, dpadKey);
    }

    if (tex) {
      float size = (float)glyphSize;
      ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(size, size));
      if (in.triggerAxis >= 0) {
        // Analog inputs get their depth shown alongside the icon - "that
        // it was pressed" alone loses real information a glyph style
        // otherwise can't convey (a light tap vs. a full pull).
        ImGui::SameLine();
        ImGui::Text("%d%%", (int)(in.triggerPercent + 0.5f));
      }
      if (in.isGyroFlick) {
        // The gyro icon alone doesn't say which way - "that a flick
        // happened" is much less useful than "that a LEFT flick
        // happened", same reasoning as showing trigger depth above.
        ImGui::SameLine();
        ImGui::TextUnformatted(in.rawLabel.c_str());
      }
    } else {
      // No art for this input on this style - fall back to text
      // rather than leaving a gap, same graceful-degradation the
      // glyph catalog itself already does at the file level.
      ImGui::TextUnformatted(in.rawLabel.c_str());
    }
  }

  if (entry.holdDurationSeconds >= 0.0f) {
    // Vertically center against whatever was just drawn (a glyph
    // image or a line of fallback text - these have different
    // heights) - same technique drawActiveHoldsSection() uses for the
    // live version of this same text, which this one previously
    // didn't have at all, hence the misalignment once a hold actually
    // released into a permanent history entry.
    float contentHeight = ImGui::GetItemRectSize().y;
    float scale = fontSize / 16.0f;
    float textLineHeight = ImGui::GetTextLineHeight() * scale;
    float verticalOffset = (contentHeight - textLineHeight) * 0.5f;
    ImGui::SameLine();
    if (verticalOffset > 0.0f)
      ImGui::SetCursorPosY(rowStartY + verticalOffset);
    ImGui::SetWindowFontScale(scale);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(H) %.1fs",
                       entry.holdDurationSeconds);
    ImGui::SetWindowFontScale(1.0f);
  }
}

// Renders the timestamp column's content - real wall-clock date/time
// the entry was captured, for whoever wants to know exactly when a
// button was pressed, not just how long since the last one.
void drawEntryTimestampCell(const InputHistoryEntry &entry) {
  std::time_t t = (std::time_t)(entry.wallClockMs / 1000);
  unsigned ms = (unsigned)(entry.wallClockMs % 1000);
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
  ImGui::Text("%s.%03u", buf, ms);
}

// Renders the timing column's content for one entry - in milliseconds
// or frames (against a fixed 60fps reference, the standard fighting-
// game frame-data convention regardless of the game's actual render
// rate), or a plain "--" placeholder when this entry followed a pause
// long enough to reset (see InputHistoryEntry::timingReset - showing a
// real-looking number there would misleadingly suggest it was
// measured combo timing rather than just "the player paused").
void drawEntryTimingCell(const InputHistoryEntry &entry, bool inFrames) {
  if (entry.timingReset || entry.msSincePrevious == 0) {
    ImGui::TextDisabled("--");
    return;
  }
  if (inFrames) {
    float frames = entry.msSincePrevious / (1000.0f / 60.0f);
    ImGui::Text("%.0ff", frames);
  } else {
    ImGui::Text("%llums", (unsigned long long)entry.msSincePrevious);
  }
}

// Renders the window's currently-active holds (confirmed ones only -
// a hold below input_history_hold_threshold_ms never shows at all,
// indistinguishable from a tap) as their own small table: glyph/label
// exactly like a normal entry would render, plus a live "Hold X.Xs"
// suffix ticking up every frame. Pinned outside the scrollable
// history table entirely (see its caller), so a long history can
// never scroll an active hold out of view.
void drawActiveHoldsSection(controller_window &w) {
  bool anyConfirmed = false;
  for (auto &h : w.input_history_active_holds)
    if (h.confirmedHold)
      anyConfirmed = true;
  if (!anyConfirmed) {
    w.input_history_holds_section_height = 0.0f;
    return;
  }

  float startY = ImGui::GetCursorPosY();
  Uint64 now = SDL_GetTicks();
  if (ImGui::BeginTable("InputHistoryHolds", 1,
                        ImGuiTableFlags_Borders |
                            (w.input_history_alternating_rows
                                 ? ImGuiTableFlags_RowBg
                                 : 0))) {
    ImGui::TableSetupColumn("Holds", ImGuiTableColumnFlags_WidthStretch);
    float fontScale = w.input_history_font_size / 16.0f;
    for (auto &h : w.input_history_active_holds) {
      if (!h.confirmedHold)
        continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();

      float rowStartY = ImGui::GetCursorPosY();
      InputHistoryEntry synthetic;
      synthetic.inputs.push_back(h.input);
      drawEntryInputCell(synthetic, w.input_history_display_style,
                        w.input_history_glyph_size, w.input_history_font_size);
      // Vertically center the "Hold X.Xs" text against whatever was
      // just drawn beside it - a glyph image (input_history_glyph_size
      // tall) and a line of Raw/Notation text have different heights,
      // so without this the text sits flush with the top of the row
      // instead of centered against the glyph.
      float contentHeight = ImGui::GetItemRectSize().y;
      float textLineHeight = ImGui::GetTextLineHeight() * fontScale;
      float verticalOffset = (contentHeight - textLineHeight) * 0.5f;

      float heldSeconds = (now - h.startMs) / 1000.0f;
      ImGui::SameLine();
      if (verticalOffset > 0.0f)
        ImGui::SetCursorPosY(rowStartY + verticalOffset);
      ImGui::SetWindowFontScale(fontScale);
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Hold %.1fs",
                         heldSeconds);
      ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::EndTable();
  }
  // Actual rendered height, measured directly rather than estimated -
  // see input_history_holds_section_height's own doc comment. Read by
  // the main table's own height-reservation calculation next frame.
  w.input_history_holds_section_height = ImGui::GetCursorPosY() - startY;
}

void ensureInputHistoryWindowCreated(controller_window &w) {
  if (w.input_history_glfw_window)
    return;

  glfwWindowHint(GLFW_FLOATING, w.input_history_always_on_top);
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

#if defined(IMGUI_IMPL_OPENGL_ES2)
  const char *glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#elif defined(__APPLE__)
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

  std::string title = w.window_title + " - Input History";
  // Created at its last known position/size (see input_history_last_x's
  // own doc comment) rather than a fixed default, so it reopens
  // wherever it was left, across both a re-enable within the same
  // session and a fresh app launch (that field's own default covers
  // the very first time this window is ever created).
  w.input_history_glfw_window = glfwCreateWindow(
      w.input_history_last_width, w.input_history_last_height, title.c_str(),
      NULL, NULL);
  if (!w.input_history_glfw_window) {
    spdlog::error("Failed to create Input History window for '{}'.",
                  w.window_title);
    return;
  }
  glfwSetWindowPos(w.input_history_glfw_window, w.input_history_last_x,
                   w.input_history_last_y);

  GLFWimage images[1];
  images[0].pixels = stbi_load_from_memory(
      Embedded::icon_data, static_cast<int>(Embedded::icon_size),
      &images[0].width, &images[0].height, nullptr, 4);
  if (images[0].pixels) {
    glfwSetWindowIcon(w.input_history_glfw_window, 1, images);
    stbi_image_free(images[0].pixels);
  }

  setWindowClickThrough(w.input_history_glfw_window,
                        w.input_history_click_through);
  w.input_history_click_through_last_applied = w.input_history_click_through;

  GLFWwindow *previousContext = glfwGetCurrentContext();
  makeContextCurrentSafe(w.input_history_glfw_window);
  glfwSwapInterval(0);

  ImGuiContext *previousImgui = ImGui::GetCurrentContext();
  w.input_history_imgui_ctx = ImGui::CreateContext();
  ImGui::SetCurrentContext(w.input_history_imgui_ctx);
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  applyCustomImGuiTheme(); // match the main Settings window's purple theme
  setupAppFonts(ImGui::GetIO());

  ImGui_ImplGlfw_InitForOpenGL(w.input_history_glfw_window, true);
  w.input_history_backend_ready = ImGui_ImplOpenGL3_Init(glsl_version);
  if (!w.input_history_backend_ready) {
    spdlog::error("Failed to initialize ImGui OpenGL3 backend for the Input "
                  "History window.");
  }

  if (previousImgui)
    ImGui::SetCurrentContext(previousImgui);
  if (previousContext)
    makeContextCurrentSafe(previousContext);

#if defined(_WIN32)
  // Needed unconditionally on Windows, not just as a fallback -
  // GLFW_TRANSPARENT_FRAMEBUFFER above (via DwmEnableBlurBehindWindow)
  // isn't reliably honored on AMD, so relying on it alone would
  // silently show a solid black window instead of transparent on some
  // systems.
  if (w.input_history_backend_ready) {
    createCompanionWindow(w.input_history_overlay, w.input_history_glfw_window,
                          &w.input_history_click_through,
                          w.window_title + " - Input History");
    // Re-applied now that the companion exists - the earlier call
    // above only had the (about-to-be-hidden) GLFW window to act on,
    // which does nothing for the companion's actual clickability once
    // it takes over as the visible window (see setWindowClickThrough()
    // in controller_window.cpp).
    setWindowClickThrough(w.input_history_glfw_window,
                          w.input_history_click_through);
  }
#endif
}

void destroyInputHistoryWindow(controller_window &w) {
  if (!w.input_history_glfw_window)
    return;

#if defined(_WIN32)
  destroyCompanionWindow(w.input_history_overlay); // before the GLFW window
                                                    // goes away below - it
                                                    // calls glfwShowWindow()
#endif

  GLFWwindow *previousContext = glfwGetCurrentContext();
  ImGuiContext *previousImgui = ImGui::GetCurrentContext();

  makeContextCurrentSafe(w.input_history_glfw_window);
  ImGui::SetCurrentContext(w.input_history_imgui_ctx);
  if (w.input_history_backend_ready) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
  }
  ImGui::DestroyContext(w.input_history_imgui_ctx);
  glfwDestroyWindow(w.input_history_glfw_window);

  // The GL context this window's glyph textures were loaded into is
  // now gone (destroyed with the window above, and never shared with
  // anything else - see its glfwCreateWindow() call). Drop the cache's
  // record of those now-invalid texture IDs so the next time this
  // window is created, glyphs get reloaded fresh into whatever
  // (different) context that new window gets, instead of reusing IDs
  // that are either dead or - worse - silently alias some unrelated
  // texture in the new context. See invalidateGlyphTextureCache()'s
  // own doc comment for the full explanation; this is what was
  // causing corrupted-looking glyphs after disabling and re-enabling
  // Input History.
  invalidateGlyphTextureCache();

  w.input_history_glfw_window = nullptr;
  w.input_history_imgui_ctx = nullptr;
  w.input_history_backend_ready = false;

  if (previousImgui)
    ImGui::SetCurrentContext(previousImgui);
  if (previousContext)
    makeContextCurrentSafe(previousContext);
}

void drawOneInputHistoryWindow(controller_window &w) {
  if (!w.input_history_enabled) {
    if (w.input_history_glfw_window)
      destroyInputHistoryWindow(w);
    return;
  }

  bool alreadyExisted = (w.input_history_glfw_window != nullptr);
  ensureInputHistoryWindowCreated(w);
  if (!w.input_history_glfw_window || !w.input_history_backend_ready)
    return;
#if defined(_WIN32)
  if (!alreadyExisted) {
    // On the very frame this window (and its companion - see
    // CompanionWindow's own doc comment) get created, the companion
    // setup just hid this GLFW window via glfwHideWindow(), and
    // immediately trying to bind a fresh GL context to a window
    // that's mid-visibility-transition can fail on some Windows
    // graphics drivers with "WGL: Failed to make context current: The
    // requested transformation operation is not supported" - a real,
    // reproducible error seen specifically the first time this window
    // opens. Skipping the render for this one frame gives Windows a
    // full frame to settle that transition before any GL context
    // operation touches this window again; every subsequent frame
    // (alreadyExisted == true) proceeds completely normally.
    return;
  }
#endif

  // Keep the "last known" position/size fresh every frame this window
  // actually exists - see input_history_last_x's own doc comment for
  // why this is tracked separately rather than only read at save time.
  // Position specifically is skipped on a platform that can't report
  // it at all (see g_window_pos_unavailable's own doc comment) - size
  // alone is still queryable there, so that half keeps working; the
  // position half just keeps whatever it was last successfully read
  // as (or its default, if never), rather than repeatedly trying a
  // query already known to fail and logging a fresh error every frame.
  if (!g_window_pos_unavailable)
    glfwGetWindowPos(w.input_history_glfw_window, &w.input_history_last_x,
                     &w.input_history_last_y);
  glfwGetWindowSize(w.input_history_glfw_window, &w.input_history_last_width,
                    &w.input_history_last_height);

  if (glfwWindowShouldClose(w.input_history_glfw_window)) {
    // Closing the window (native close button/Alt-F4) turns the
    // feature off, same as unchecking it in Settings - not just hidden,
    // since a stray always-on-top window with no visible way back to
    // Settings would be an easy way to get "stuck".
    glfwSetWindowShouldClose(w.input_history_glfw_window, GLFW_FALSE);
    setInputHistoryEnabled(w, false);
    return;
  }

  GLFWwindow *previousContext = glfwGetCurrentContext();
  ImGuiContext *previousImgui = ImGui::GetCurrentContext();

  makeContextCurrentSafe(w.input_history_glfw_window);
  ImGui::SetCurrentContext(w.input_history_imgui_ctx);
  applyCustomImGuiTheme(); // re-applied every frame - see its doc comment

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Same "Drag to Move"/"Scroll to Resize" mechanism controller
  // windows already have - only meaningful while Click-Through is
  // off, since with it on this window never receives mouse events at
  // all (they pass straight through to whatever's behind it).
  //
  // On-the-fly shortcuts - see drag_to_move_shortcut's doc comment on
  // controller windows in controller_window.h for the full picture.
  // This window's own hover check (via its own glfw_window) is
  // completely independent of any controller window's, so a
  // shortcut fired while hovering here never affects one there.
  bool dragToMoveShortcutCurrentlyHeld = false;
  bool dragToMoveEffective = updateShortcutToggle(
      w.input_history_drag_to_move, w.input_history_drag_to_move_shortcut_was_active,
      w.input_history_drag_to_move_shortcut, w.input_history_glfw_window,
      &dragToMoveShortcutCurrentlyHeld);
  // Middle Mouse Button built-in - see its doc comment on controller
  // windows' own click_through_middle_mouse_was_active for the full
  // explanation. Applied first, same layering as there.
  updateShortcutToggle(w.input_history_click_through,
                       w.input_history_click_through_middle_mouse_was_active,
                       1 /* Middle Mouse Button */, w.input_history_glfw_window);
  bool clickThroughEffective = updateShortcutToggle(
      w.input_history_click_through,
      w.input_history_click_through_shortcut_was_active,
      w.input_history_click_through_shortcut, w.input_history_glfw_window);
  // Same reasoning as the controller-window version of this line
  // (controller_window.cpp, right after its own clickThroughEffective
  // is computed) - while the Drag-to-Move shortcut is PHYSICALLY,
  // ACTIVELY held right now, Click-Through is also forced on for that
  // same duration, so an OS-level "hold this key to drag any window"
  // gesture bound to the same key as this shortcut isn't blocked by
  // this window's own Click-Through being off. Gated on
  // dragToMoveShortcutCurrentlyHeld, not dragToMoveEffective - see the
  // controller-window version's own comment for why: the latter is
  // also true whenever Drag to Move is simply, persistently enabled
  // with no shortcut held at all, which would otherwise force Click-
  // Through on for anyone who just wanted this feature always on.
  clickThroughEffective =
      clickThroughEffective || dragToMoveShortcutCurrentlyHeld;
  if (clickThroughEffective != w.input_history_click_through_last_applied) {
    setWindowClickThrough(w.input_history_glfw_window, clickThroughEffective);
    w.input_history_click_through_last_applied = clickThroughEffective;
  }

  if (dragToMoveEffective && !g_window_pos_unavailable) {
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      int win_x = 0, win_y = 0;
      glfwGetWindowPos(w.input_history_glfw_window, &win_x, &win_y);
      double screen_x, screen_y;
#if defined(_WIN32)
      // While the companion window is active (real per-pixel
      // transparency - see CompanionWindow's doc comment), io.MousePos
      // is only as fresh as the last WM_MOUSEMOVE forwarded from the
      // companion window to this (hidden) GLFW window. Moving this
      // window below feeds back into updateCompanionWindow() moving
      // the companion window to match, which can itself generate a
      // fresh WM_MOUSEMOVE/WM_NCHITTEST as Windows recalculates what's
      // under the cursor after the move - which then gets forwarded
      // back here, updating io.MousePos again, on a coordinate basis
      // that's now stale relative to this drag's own anchor point.
      // That's a real feedback loop: each frame's move triggers a
      // position update that distorts the next frame's move, and it
      // can run away in a couple of frames on real hardware. Reading
      // the cursor's true, absolute desktop position directly
      // sidesteps the whole loop - it's never relayed through
      // anything, so it can't be thrown off by this window (or its
      // companion) having just moved.
      if (w.input_history_overlay.hwnd) {
        POINT pt;
        GetCursorPos(&pt);
        screen_x = pt.x;
        screen_y = pt.y;
      } else
#endif
      {
        screen_x = win_x + io.MousePos.x;
        screen_y = win_y + io.MousePos.y;
      }
      if (!w.input_history_drag_moving) {
        w.input_history_drag_moving = true;
        w.input_history_drag_move_anchor_x = screen_x;
        w.input_history_drag_move_anchor_y = screen_y;
        w.input_history_drag_move_start_win_x = win_x;
        w.input_history_drag_move_start_win_y = win_y;
      } else {
        // Applying the full accumulated delta from the drag's start,
        // not frame-to-frame deltas, so small rounding errors can't
        // compound over a long drag - see the matching comment on
        // controller windows' own drag-to-move for the full reasoning.
        int new_x = w.input_history_drag_move_start_win_x +
                    (int)std::lround(screen_x - w.input_history_drag_move_anchor_x);
        int new_y = w.input_history_drag_move_start_win_y +
                    (int)std::lround(screen_y - w.input_history_drag_move_anchor_y);
        glfwSetWindowPos(w.input_history_glfw_window, new_x, new_y);
      }
    } else {
      w.input_history_drag_moving = false;
    }
  }
  if (w.input_history_scroll_to_resize) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f) {
      int ww = 0, hh = 0;
      glfwGetWindowSize(w.input_history_glfw_window, &ww, &hh);
      if (io.MouseWheel > 0) {
        ww = (int)(ww * 1.05f);
        hh = (int)(hh * 1.05f);
      } else {
        ww = (int)(ww * 0.95f);
        hh = (int)(hh * 0.95f);
        if (ww < 50)
          ww = 50;
        if (hh < 50)
          hh = 50;
      }
      glfwSetWindowSize(w.input_history_glfw_window, ww, hh);
    }
  }

  int winW, winH;
  glfwGetWindowSize(w.input_history_glfw_window, &winW, &winH);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));

  // Background and content transparency are independent settings (see
  // their doc comments in controller_window.h) - the window's
  // background color gets its own alpha here, while the blanket
  // ImGuiStyleVar_Alpha below is scoped to just the table content
  // (text/glyphs), not the window chrome/background, so one doesn't
  // affect the other.
  ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
  bg.w = w.input_history_opacity;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
  ImGui::Begin("InputHistoryRoot", nullptr,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                  ImGuiWindowFlags_NoScrollbar);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, w.input_history_content_opacity);


  // A real table (input | timing) with visible row separators, rather
  // than free-floating text - reads far more clearly than a number
  // sitting loose next to another number with nothing to tell them
  // apart. The timing column only exists at all when
  // input_history_show_timing is on, for any display style, not just
  // Fighting-Game Notation.
  int columns = 1 + (w.input_history_show_timing ? 1 : 0) +
               (w.input_history_show_timestamp ? 1 : 0);
  ImGuiTableFlags tableFlags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit;
  if (w.input_history_alternating_rows)
    tableFlags |= ImGuiTableFlags_RowBg;

  // How much vertical space the active holds section needs, so the
  // main table can be constrained to leave exactly that much room
  // instead of using all available space and pushing the holds
  // section below the window's visible bounds. Previously the table
  // had no height limit and relied on the *outer* window's own
  // scroll (via SetScrollY/SetScrollHereY below) to bring the newest
  // entry into view - which meant that whenever the table's content
  // needed that scroll (any time it doesn't fully fit, which gets
  // more likely the smaller the window is resized), the holds
  // section - drawn right after, in the same scrollable flow - got
  // scrolled out of view right along with it, even though it was
  // still technically being drawn. Giving the table its own separate,
  // internally-scrollable region means the outer window never needs
  // to scroll at all, so the holds section - outside the table
  // entirely - always stays exactly where it's pinned (top or
  // reserved bottom space), independent of how much history exists
  // or how small the window gets.
  //
  // The reserved amount itself comes from input_history_holds_section_
  // height - what the section actually measured as, last time it was
  // drawn - rather than a hand-written formula estimating row count *
  // row height. An estimate like that has no way to account for
  // exactly how much space ImGui's own table borders/padding/spacing
  // add on top of the content itself, so it tended to run slightly
  // short - which is exactly what let the section overflow past its
  // reserved space and visibly cut into (or get cut off by) the main
  // table. A direct measurement can't have that class of error.

  // Active holds are pinned at whichever end is "newest" - outside
  // the scrollable table entirely, so a long history can never scroll
  // one out of view while it's still being held.
  if (w.input_history_newest_on_top)
    drawActiveHoldsSection(w);

  // Only the bottom case needs an explicit reservation here - the top
  // case already reserves its own space naturally, just by having
  // been drawn first (the table below simply starts after it).
  float reserveForBottomHolds = (!w.input_history_newest_on_top)
                                    ? w.input_history_holds_section_height
                                    : 0.0f;
  float tableHeight =
      std::max(0.0f, ImGui::GetContentRegionAvail().y - reserveForBottomHolds);

  // Removes the child window's own default padding (top+bottom, from
  // the style's WindowPadding) so its content area exactly matches
  // tableHeight above with nothing hidden - otherwise the last row
  // could end up partially cut off at the child window's own edge,
  // since the reserved-space calculation has no way to know about
  // padding it never accounted for.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (ImGui::BeginChild("InputHistoryTableRegion", ImVec2(0, tableHeight),
                        false, ImGuiWindowFlags_NoScrollbar)) {
    if (ImGui::BeginTable("InputHistoryTable", columns, tableFlags)) {
    if (w.input_history_show_timestamp)
      ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
    if (w.input_history_show_timing)
      ImGui::TableSetupColumn("Timing", ImGuiTableColumnFlags_WidthFixed);

    // Font size now applies to every column (Time/Input/Timing), not
    // just the Input column's Raw/Notation text - previously left the
    // other two columns visually inconsistent with it.
    float fontScale = w.input_history_font_size / 16.0f;

    auto drawRow = [&](const InputHistoryEntry &entry) {
      ImGui::TableNextRow();
      if (w.input_history_show_timestamp) {
        ImGui::TableNextColumn();
        ImGui::SetWindowFontScale(fontScale);
        drawEntryTimestampCell(entry);
        ImGui::SetWindowFontScale(1.0f);
      }
      ImGui::TableNextColumn();
      drawEntryInputCell(entry, w.input_history_display_style,
                        w.input_history_glyph_size, w.input_history_font_size);
      if (w.input_history_show_timing) {
        ImGui::TableNextColumn();
        ImGui::SetWindowFontScale(fontScale);
        drawEntryTimingCell(entry, w.input_history_timing_in_frames);
        ImGui::SetWindowFontScale(1.0f);
      }
    };

    // Newest at the bottom by default, like a terminal/log scrolling
    // upward as new inputs come in - or newest at the top instead, per
    // input_history_newest_on_top, so the latest input is always the
    // first thing visible without needing to scroll.
    if (w.input_history_newest_on_top) {
      // Pin the view to the top every frame, rather than trying to
      // scroll there relative to the last-drawn row's cursor position
      // (which would only be correct after the OLDEST row, not the
      // newest one at the top) - this is what actually keeps the
      // newest entry visible without the user needing to scroll.
      ImGui::SetScrollY(0.0f);
      for (auto it = w.input_history_entries.rbegin();
          it != w.input_history_entries.rend(); ++it) {
        drawRow(*it);
      }
    } else {
      for (auto &entry : w.input_history_entries) {
        drawRow(entry);
      }
      // SetScrollY(GetScrollMaxY()) directly, rather than
      // SetScrollHereY(1.0f) (scroll to make the last-drawn item
      // visible) - SetScrollHereY computes its target from the last
      // item's own rect, which inside a table cell could end up
      // slightly imprecise and leave the very last row partially cut
      // off at the bottom edge instead of fully flush with it.
      // Scrolling directly to the actual maximum scroll position is a
      // more direct guarantee of the same intent ("show the bottom of
      // the content"), with no per-item rect calculation involved.
      ImGui::SetScrollY(ImGui::GetScrollMaxY());
    }
    ImGui::EndTable();
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar(); // WindowPadding pushed before BeginChild above

  if (!w.input_history_newest_on_top)
    drawActiveHoldsSection(w);

  ImGui::PopStyleVar();
  ImGui::End();
  ImGui::PopStyleColor();

  ImGui::Render();
  glViewport(0, 0, winW, winH);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // transparent clear - see the
                                        // GLFW_TRANSPARENT_FRAMEBUFFER
                                        // hint above
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#if defined(_WIN32)
  if (w.input_history_overlay.hwnd) {
    // Companion window active - read this frame back and blit it there
    // instead of presenting the (hidden) GLFW window directly. Now has
    // its own Always on Top toggle (input_history_always_on_top),
    // mirroring the controller window's own setting - previously
    // hardcoded true unconditionally.
    updateCompanionWindow(w.input_history_overlay,
                          w.input_history_always_on_top, 1.0 / 60.0);
  } else {
    glfwSwapBuffers(w.input_history_glfw_window);
  }
#else
  glfwSwapBuffers(w.input_history_glfw_window);
#endif

  if (previousImgui)
    ImGui::SetCurrentContext(previousImgui);
  if (previousContext)
    makeContextCurrentSafe(previousContext);
}

} // namespace

void drawInputHistoryWindows() {
  for (auto &w : windows) {
    drawOneInputHistoryWindow(w);
  }
}

void cleanupInputHistory(controller_window &w) {
  closeInputHistoryLog(w);
  destroyInputHistoryWindow(w);
}

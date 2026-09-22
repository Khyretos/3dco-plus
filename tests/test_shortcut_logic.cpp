// Tests for getShortcutKind() and isShortcutPhysicallyActiveGeneric()
// (shortcut_logic.h) - the shortcutId -> kind/keys mapping extracted
// from controller_window.cpp. isShortcutPhysicallyActiveGeneric() is
// templated specifically so these tests can supply a fake, fixed
// "pressed" set instead of GlobalKeyboard's real, live hardware
// state - what's actually worth testing here is the mapping (which
// key(s) a given shortcutId checks, and how multiple keys combine),
// not whether a real key happens to be down when the test runs.

#include "doctest.h"
#include "shortcut_logic.h"

#include <set>

namespace {

// A fake "is this pressed" pair closed over a fixed set of scancodes/
// buttons, standing in for GlobalKeyboard::isPressed()/
// isMouseButtonPressed() in production.
struct FakeInput {
  std::set<SDL_Scancode> pressedKeys;
  std::set<int> pressedButtons;

  bool key(SDL_Scancode sc) const { return pressedKeys.count(sc) > 0; }
  bool button(int b) const { return pressedButtons.count(b) > 0; }
};

} // namespace

// ---- getShortcutKind ----

TEST_CASE("getShortcutKind: 0 and negative are None") {
  CHECK(getShortcutKind(0) == ShortcutKind::None);
  CHECK(getShortcutKind(-1) == ShortcutKind::None);
}

TEST_CASE("getShortcutKind: 1 (Middle Mouse Button) is Discrete") {
  CHECK(getShortcutKind(1) == ShortcutKind::Discrete);
}

TEST_CASE("getShortcutKind: legacy modifier IDs 2-5 are Hold") {
  CHECK(getShortcutKind(2) == ShortcutKind::Hold);
  CHECK(getShortcutKind(3) == ShortcutKind::Hold);
  CHECK(getShortcutKind(4) == ShortcutKind::Hold);
  CHECK(getShortcutKind(5) == ShortcutKind::Hold);
}

TEST_CASE("getShortcutKind: legacy F9-F12 IDs 6-9 are Discrete") {
  CHECK(getShortcutKind(6) == ShortcutKind::Discrete);
  CHECK(getShortcutKind(7) == ShortcutKind::Discrete);
  CHECK(getShortcutKind(8) == ShortcutKind::Discrete);
  CHECK(getShortcutKind(9) == ShortcutKind::Discrete);
}

TEST_CASE("getShortcutKind: arbitrary-key modifier scancodes are Hold, "
         "every other arbitrary key is Discrete") {
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_LSHIFT) == ShortcutKind::Hold);
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_RSHIFT) == ShortcutKind::Hold);
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_LCTRL) == ShortcutKind::Hold);
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_RCTRL) == ShortcutKind::Hold);
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_LALT) == ShortcutKind::Hold);
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_RALT) == ShortcutKind::Hold);
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_LGUI) == ShortcutKind::Hold);
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_RGUI) == ShortcutKind::Hold);

  // A perfectly ordinary key, like A - should be Discrete, not Hold.
  CHECK(getShortcutKind(1000 + SDL_SCANCODE_A) == ShortcutKind::Discrete);
}

// ---- isShortcutPhysicallyActiveGeneric ----

TEST_CASE("isShortcutPhysicallyActiveGeneric: 0 and negative are never "
         "active, regardless of what's pressed") {
  FakeInput fake;
  fake.pressedKeys.insert(SDL_SCANCODE_A);
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      0, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      -1, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
}

TEST_CASE("isShortcutPhysicallyActiveGeneric: arbitrary-key shortcut "
         "checks exactly its own scancode") {
  FakeInput fake;
  fake.pressedKeys.insert(SDL_SCANCODE_A);
  int shortcutId = 1000 + SDL_SCANCODE_A;
  CHECK(isShortcutPhysicallyActiveGeneric(
      shortcutId, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));

  int otherShortcutId = 1000 + SDL_SCANCODE_B;
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      otherShortcutId, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
}

TEST_CASE("isShortcutPhysicallyActiveGeneric: legacy modifier IDs check "
         "either the left or right key of that modifier (an OR)") {
  // Ctrl (id 3) should be active whether LEFT or RIGHT ctrl is the one
  // actually held - this is the exact behavior worth locking in, since
  // a naive rewrite could easily check only one side.
  FakeInput onlyLeft;
  onlyLeft.pressedKeys.insert(SDL_SCANCODE_LCTRL);
  CHECK(isShortcutPhysicallyActiveGeneric(
      3, [&](SDL_Scancode sc) { return onlyLeft.key(sc); },
      [&](int b) { return onlyLeft.button(b); }));

  FakeInput onlyRight;
  onlyRight.pressedKeys.insert(SDL_SCANCODE_RCTRL);
  CHECK(isShortcutPhysicallyActiveGeneric(
      3, [&](SDL_Scancode sc) { return onlyRight.key(sc); },
      [&](int b) { return onlyRight.button(b); }));

  FakeInput neither;
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      3, [&](SDL_Scancode sc) { return neither.key(sc); },
      [&](int b) { return neither.button(b); }));
}

TEST_CASE("isShortcutPhysicallyActiveGeneric: each legacy modifier ID "
         "checks its own pair, not some other modifier's") {
  FakeInput fake;
  fake.pressedKeys.insert(SDL_SCANCODE_LSHIFT);
  // Shift is held, but Ctrl (id 3) and Alt (id 5) should NOT report
  // active just because some other modifier happens to be down.
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      3, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      5, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
  // Shift itself (id 4) should.
  CHECK(isShortcutPhysicallyActiveGeneric(
      4, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
}

TEST_CASE("isShortcutPhysicallyActiveGeneric: legacy F9-F12 IDs check their "
         "own function key only") {
  FakeInput fake;
  fake.pressedKeys.insert(SDL_SCANCODE_F11);
  CHECK(isShortcutPhysicallyActiveGeneric(
      8, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      6, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      7, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      9, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));
}

TEST_CASE("isShortcutPhysicallyActiveGeneric: id 1 (Middle Mouse Button) "
         "checks the mouse button callback, not a key at all") {
  FakeInput fake;
  fake.pressedButtons.insert(2); // middle button index, per the doc comment
  CHECK(isShortcutPhysicallyActiveGeneric(
      1, [&](SDL_Scancode sc) { return fake.key(sc); },
      [&](int b) { return fake.button(b); }));

  FakeInput noButtons;
  CHECK_FALSE(isShortcutPhysicallyActiveGeneric(
      1, [&](SDL_Scancode sc) { return noButtons.key(sc); },
      [&](int b) { return noButtons.button(b); }));
}

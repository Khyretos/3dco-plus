// Tests for walkCombineWithChain() (combine_with_chain.h) - the
// generic algorithm extracted from isInputIgnored()
// (input_history_glyphs.cpp) after a real bug was found there: that
// function used to only ever check the exact style passed in, never
// following the combine_with chain that getGlyphTexture() (the same
// file) already did - so an ignore rule set on a style's combine_with
// target silently never applied while viewing the combining style.
//
// These use a plain in-memory std::map fixture standing in for the
// real disk-backed style cache (getStyleData()) - no StyleData
// struct, no filesystem, no image decoding needed to exercise the
// chain-walking algorithm itself, which is the whole point of having
// pulled it out into its own header.

#include "doctest.h"
#include "combine_with_chain.h"

#include <map>
#include <string>
#include <vector>

namespace {

// A minimal stand-in for what getStyleData() would return - just the
// two things the algorithm actually needs from each hop.
struct FakeStyle {
  std::vector<std::string> ignoredInputs;
  std::string combineWith;
};

// Returns a lookup lambda closed over `styles`, matching the same
// "does this folder ignore `binding`, where does it combine to next"
// shape isInputIgnored()'s own real lambda has.
auto makeLookup(const std::map<std::string, FakeStyle> &styles,
               const std::string &binding) {
  return [&styles, &binding](const std::string &folder) -> ChainHop {
    auto it = styles.find(folder);
    if (it == styles.end())
      return {false, false, ""};
    bool matched = false;
    for (const auto &b : it->second.ignoredInputs) {
      if (b == binding) {
        matched = true;
        break;
      }
    }
    return {true, matched, it->second.combineWith};
  };
}

} // namespace

TEST_CASE("walkCombineWithChain: matches on the starting style itself, no "
         "chain needed") {
  std::map<std::string, FakeStyle> styles = {
      {"SteamDeck", {{"gamepad:b5"}, ""}},
  };
  CHECK(walkCombineWithChain("SteamDeck", makeLookup(styles, "gamepad:b5")));
  CHECK_FALSE(
      walkCombineWithChain("SteamDeck", makeLookup(styles, "gamepad:b6")));
}

TEST_CASE("walkCombineWithChain: matches on the combine_with target - the "
         "exact bug this was built to catch") {
  std::map<std::string, FakeStyle> styles = {
      {"SteamDeck", {{}, "ThumbstickTouch"}},
      {"ThumbstickTouch", {{"gamepad:b24"}, ""}},
  };
  // The ignore rule lives on ThumbstickTouch, not SteamDeck itself -
  // starting the walk at SteamDeck must still find it.
  CHECK(walkCombineWithChain("SteamDeck", makeLookup(styles, "gamepad:b24")));
}

TEST_CASE("walkCombineWithChain: no match anywhere in the chain returns "
         "false, not a crash") {
  std::map<std::string, FakeStyle> styles = {
      {"SteamDeck", {{}, "ThumbstickTouch"}},
      {"ThumbstickTouch", {{"gamepad:b24"}, ""}},
  };
  CHECK_FALSE(
      walkCombineWithChain("SteamDeck", makeLookup(styles, "gamepad:b99")));
}

TEST_CASE("walkCombineWithChain: a folder that doesn't exist at all breaks "
         "the chain rather than matching") {
  std::map<std::string, FakeStyle> styles = {
      {"SteamDeck", {{}, "DoesNotExist"}},
  };
  CHECK_FALSE(
      walkCombineWithChain("SteamDeck", makeLookup(styles, "gamepad:b5")));
}

TEST_CASE("walkCombineWithChain: a starting style that doesn't exist "
         "returns false immediately") {
  std::map<std::string, FakeStyle> styles = {
      {"SteamDeck", {{"gamepad:b5"}, ""}},
  };
  CHECK_FALSE(walkCombineWithChain("NoSuchStyle",
                                   makeLookup(styles, "gamepad:b5")));
}

TEST_CASE("walkCombineWithChain: a three-hop chain still resolves - not "
         "just a single combine_with level") {
  std::map<std::string, FakeStyle> styles = {
      {"A", {{}, "B"}},
      {"B", {{}, "C"}},
      {"C", {{"keyboard:key_a"}, ""}},
  };
  CHECK(walkCombineWithChain("A", makeLookup(styles, "keyboard:key_a")));
}

TEST_CASE("walkCombineWithChain: an accidental cycle terminates instead of "
         "looping forever, thanks to the hop cap") {
  std::map<std::string, FakeStyle> styles = {
      {"A", {{}, "B"}},
      {"B", {{}, "A"}}, // hand-edited data forming a cycle
  };
  // Should return false (no match ever found) rather than hang.
  CHECK_FALSE(walkCombineWithChain("A", makeLookup(styles, "gamepad:b5")));
}

TEST_CASE("walkCombineWithChain: respects a custom maxHops rather than "
         "always using the default 8") {
  std::map<std::string, FakeStyle> styles = {
      {"A", {{}, "B"}},
      {"B", {{}, "C"}},
      {"C", {{"gamepad:b5"}, ""}},
  };
  // The match is 2 hops away (A -> B -> C) - capping at 1 hop should
  // never reach it.
  CHECK_FALSE(
      walkCombineWithChain("A", makeLookup(styles, "gamepad:b5"), 1));
  // But the default (8) or a generous explicit cap should.
  CHECK(walkCombineWithChain("A", makeLookup(styles, "gamepad:b5"), 5));
}

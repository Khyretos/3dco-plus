#ifndef COMBINE_WITH_CHAIN_H
#define COMBINE_WITH_CHAIN_H

#include <string>

// Generic result of looking up one hop's info - deliberately not tied
// to StyleData or anything glyph-specific, so this same walker can
// serve any "one thing can combine with another, forming a chain, and
// a target found partway through wins" pattern. Right now that's
// isInputIgnored() (input_history_glyphs.cpp) and, in spirit,
// getGlyphTexture()'s own folderName resolution in the same file -
// but the pattern itself is domain agnostic, hence living here rather
// than as a private detail of the glyph system.
struct ChainHop {
  bool found = false;      // false = this folder doesn't exist at all - stop, report failure
  bool matched = false;    // true = what we were checking for was found at this hop - stop, report success
  std::string combineWith; // next folder to try if not matched (empty = end of chain)
};

// Walks the chain starting at startFolder, calling lookupHop(folderName)
// once per hop until one reports matched=true (returns true), one
// reports found=false (returns false - the chain is broken), or the
// chain runs out of combineWith targets (returns false).
//
// maxHops guards against an accidental cycle from hand-edited data
// (A combines with B combines with A) looping forever - the same cap
// getGlyphTexture() already used before this was extracted.
//
// Templated on the lookup callable rather than taking a
// std::function, so production code can pass a lambda that reads
// from the real, disk-backed style cache with zero indirection
// overhead, while a test passes a lambda closed over a plain
// in-memory std::map fixture - no StyleData, no disk I/O, no image
// decoding needed to exercise the algorithm itself.
template <typename LookupHopFn>
bool walkCombineWithChain(const std::string &startFolder, LookupHopFn lookupHop,
                          int maxHops = 8) {
  std::string folderName = startFolder;
  for (int hop = 0; hop < maxHops && !folderName.empty(); ++hop) {
    ChainHop result = lookupHop(folderName);
    if (!result.found)
      return false;
    if (result.matched)
      return true;
    folderName = result.combineWith;
  }
  return false;
}

#endif // COMBINE_WITH_CHAIN_H

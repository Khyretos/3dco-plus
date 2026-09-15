#ifndef INPUT_HISTORY_GLYPHS_H
#define INPUT_HISTORY_GLYPHS_H

// ------------------------------------------------------------------
// Glyph catalog for the Input History window's icon-based display
// styles (see input_history.cpp for the window itself).
//
// Fully data-driven: every "style" (Xbox Series, PS5, a user's own
// custom mapping, anything) is just a folder under glyphs/ containing
// an info.json - a flat map of input key -> glyph filename, e.g.
// {"gamepad:button:south": "A.png", "keyboard:w": "W.png", ...}. A
// folder with no info.json (the bundled pack's "Others/" subfolders,
// e.g.) is never surfaced as a selectable style - it's still present
// on disk (for a user building a custom mapping to pick art from) but
// isn't itself one. This is what makes user-created custom mappings
// (built via the same creator UI as the built-in ones - see
// settings_window.cpp) show up identically to the bundled styles: as
// far as the loader's concerned, there's no difference between them.
//
// A style's info.json can also set "combine_with": "<other style>",
// so a style that only defines some inputs (e.g. FGC Motion, which
// only has direction glyphs) falls back to another style for anything
// it doesn't define itself - looked up live at query time, not baked
// in when generated, so editing the style being combined with is
// reflected immediately.
//
// It can also set "exclude_from_combine": ["<prefix>", ...] - any key
// starting with one of those prefixes is never inherited through
// combine_with, even if this style doesn't define it itself (it just
// gets no glyph at all, same as any other undefined key with no
// combine_with). FGC Motion uses this for "gamepad:dpad:" - it
// already represents directions its own way (the numpad-digit/motion
// glyphs), so pulling in Xbox's or PS5's separate D-pad button icons
// through the combine fallback would be redundant/confusing, not
// missing functionality.
//
// Input keys reuse this app's existing binding-string convention
// (gamepad:bN, keyboard:key_X, mouse:mouse_N - see
// controller_window.cpp's input binding parsing) where it already
// exists, extended with a few concepts that didn't have one:
//   gamepad:button:<name>     - south/east/west/north/back/start/
//                                guide/misc1/touchpad/shoulder_left/
//                                shoulder_right/stick_left/
//                                stick_left_click/stick_right/
//                                stick_right_click/paddle_left1/
//                                paddle_right1/paddle_left2/paddle_right2
//   gamepad:dpad:<direction>  - up/down/left/right
//   gamepad:trigger:<side>    - left/right
//   gamepad:direction:<n>     - numpad digit 1-9, or a compound FGC
//                                motion string like "236"/"623"/"360"
//   keyboard:<scancode-name>  - lowercased SDL_GetScancodeName() output
//   mouse:<button-index>      - 0/1/2/... or "default" as a fallback
//
// Everything is parsed once and cached in memory (both the info.json
// data and the loaded GL textures) - folder scanning and JSON parsing
// only ever happen on first use per session, never per-frame, so this
// has no effect on input capture timing regardless of how many custom
// mappings exist.
// ------------------------------------------------------------------

#include <glad/glad.h>
#include <string>
#include <vector>

// One discovered glyph mapping style - either bundled (Xbox Series,
// PS5, ...) or user-created. Returned by listing functions below;
// callers refer to a style by its index into that list for the
// rest of this session (matching how input_history_display_style
// already works: 0=Raw, 1=Notation, 2+=index into this list).
struct GlyphStyleInfo {
  std::string folderName; // e.g. "Xbox Series", or a custom name
  std::string displayName;
};

// Rescans glyphs/ for style folders (those containing an info.json)
// and returns them, sorted with the standard styles first (in a
// fixed, sensible order) followed by any custom ones alphabetically.
// Cached after the first call within a session - call
// refreshGlyphStyleList() first if a style was just created/renamed
// and the list needs to reflect that immediately.
const std::vector<GlyphStyleInfo> &listGlyphStyles();
void refreshGlyphStyleList();

// Checks that every standard bundled style (Keyboard & Mouse Dark/
// Light, Xbox Series, PS5, FGC Motion, etc. - the fixed list this app
// ships with) still has its own folder + info.json under glyphs/, and
// re-extracts the embedded pack (harmless/idempotent - never touches
// custom folders, which aren't part of the embedded zip) if any are
// missing - e.g. a style someone deleted or a fresh install that only
// partially extracted. Called once at startup; safe to call again any
// time (a no-op when everything's already present).
void ensureStandardGlyphStylesPresent();

// Returns the glyph texture for a style + input key (see the key
// format in the big comment above), or 0 if this style (and whatever
// it combine_with's, transitively) has no glyph for that key -
// callers should fall back to a text label in that case.
GLuint getGlyphTexture(int styleIndex, const std::string &inputKey);

// Drops every cached glyph texture ID (not the parsed style/mapping
// data - that's plain data, not tied to any GL context, and is safe
// to keep) without touching the actual GL objects. Call this once a
// GL context that may have had glyph textures loaded into it is about
// to be destroyed - Input History's own window/context is created
// fresh (not shared with anything) every time it's created, so a
// texture ID cached from a previous instance of that context is
// invalid in the next one. The driver already frees the GL objects
// themselves automatically when their context is destroyed; this
// call only clears the cache's own (now-stale) record of their IDs,
// so the next lookup reloads fresh into whatever context is current
// at that point, rather than returning an ID that happens to be
// invalid, or - worse - happens to alias some unrelated texture the
// new context allocated with the same number, which is what
// corrupted-looking glyphs after re-enabling Input History turned out
// to be: a stale ID silently repurposed as something else entirely.
void invalidateGlyphTextureCache();

// Absolute path to a style's own directory (where its info.json and
// glyph image files live), creating it if needed. Used by the custom
// mapping creator UI to know where to copy a user-picked image, and by
// its "new mapping" flow to know where to write a fresh info.json.
std::string getGlyphStyleDirectory(const std::string &folderName);

// Writes (or overwrites) a style's info.json from an in-memory mapping
// - used by the custom mapping creator UI's Save. Invalidates any
// cached texture/lookup data for this style so the change is visible
// immediately, and refreshes the style list if this is a brand new
// folder.
bool saveGlyphStyleMapping(
    const std::string &folderName, const std::string &displayName,
    const std::vector<std::pair<std::string, std::string>> &mappings,
    const std::string &combineWith = "",
    const std::vector<std::string> &excludeFromCombine = {});

// Raw accessors for the mapping creator UI's "edit an existing style"
// flow - loads a style's info.json and returns its data as-is (not
// resolved through combine_with, unlike getGlyphTexture()), so the
// editor shows exactly what this style itself defines.
std::vector<std::pair<std::string, std::string>>
getGlyphStyleMappings(const std::string &folderName);
std::string getGlyphStyleCombineWith(const std::string &folderName);
std::string getGlyphStyleDisplayNameFor(const std::string &folderName);

// Ensures the bundled glyph pack has been extracted to the user's data
// directory (config_base_path + "/glyphs") - safe to call every frame,
// only does real work once per process.
std::string ensureGlyphsExtracted();

// Loads an arbitrary image (PNG/JPG/BMP - whatever stb_image reads),
// resizes it to targetSize x targetSize via bilinear resampling, and
// writes it as a PNG to destPath. Used by the custom glyph mapping
// creator UI so a user-picked image doesn't need to already match the
// bundled pack's conventions (100x100 PNG) - see saveGlyphStyleMapping()
// for where the resulting file ends up referenced from.
bool convertAndSaveGlyphImage(const std::string &sourcePath,
                              const std::string &destPath, int targetSize = 100);

#endif

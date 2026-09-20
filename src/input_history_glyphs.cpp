#include "input_history_glyphs.h"

#include "settings.h"
#include "glyphs_zip_data.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace {

std::string glyphExtractedRoot;

// One style's parsed info.json, cached after first load.
struct StyleData {
  std::string displayName;
  std::unordered_map<std::string, std::string> mappings; // key -> filename
  std::string combineWith; // folder name, or empty
  // Key prefixes that never fall through to combineWith, even when
  // this style doesn't define them itself - see the big header
  // comment on "exclude_from_combine" for why (FGC Motion + D-pad).
  std::vector<std::string> excludeFromCombine;
  // Full "type:value" bindings (the same convention used everywhere
  // else in this app - e.g. "gamepad:b24", "keyboard:key_a",
  // "mouse:mouse_left") that Input History should never capture at
  // all while this style is selected - not just "no glyph for it",
  // genuinely no discrete press entry and no hold tracking either.
  // Originally just raw gamepad button indices, motivated by a grip
  // sensor inverted in the Model table so it reads correctly on the
  // 3D model, but which would otherwise show as permanently held in
  // Input History for as long as the controller is actually being
  // held normally - generalized to every input type Input History
  // actually captures on its own (gamepad, keyboard, mouse), matching
  // the Model table's own Type + Specific Input picker. "joystick:"
  // bindings are accepted for UI consistency with that same picker
  // but never actually match anything - Input History only ever
  // captures gamepad buttons via SDL's mapped Gamepad API, never a
  // raw, unmapped joystick's own button state.
  std::vector<std::string> ignoredInputs;
};

std::vector<GlyphStyleInfo> g_styleList;
bool g_styleListLoaded = false;
std::unordered_map<std::string, StyleData> g_styleDataCache; // key: folder name
std::unordered_map<std::string, GLuint> g_textureCache;      // key: absolute path

// Fixed display order for the standard bundled styles, so the
// dropdown doesn't jump around between sessions depending on
// filesystem enumeration order - anything not in this list (custom
// mappings) sorts alphabetically after these. Keyboard & Mouse is two
// separate, fully self-contained folders (not one folder with a
// variant mechanism) - simpler to read/edit, and lets someone pick
// "Keyboard & Mouse (Light)" as an ordinary style choice like any
// other rather than a special in-style toggle.
const std::vector<std::string> &standardOrder() {
  static const std::vector<std::string> order = {
      "Xbox Series",         "Xbox One",  "Xbox 360",  "PS5",
      "PS4",                 "PS3",       "Switch",    "Steam Deck",
      "Keyboard & Mouse (Dark)", "Keyboard & Mouse (Light)",
      "FGC Motion",          "FGC Motion (Xbox)",
  };
  return order;
}

bool loadStyleData(const std::string &folderName, StyleData &out) {
  std::string root = ensureGlyphsExtracted();
  std::string infoPath = root + "/" + folderName + "/info.json";
  std::ifstream f(infoPath);
  if (!f)
    return false;

  json j;
  try {
    f >> j;
  } catch (const std::exception &e) {
    spdlog::warn("Glyph mapping '{}': failed to parse info.json - {}",
                 folderName, e.what());
    return false;
  }

  out.displayName = j.value("display_name", folderName);
  out.combineWith = j.value("combine_with", "");
  if (j.contains("exclude_from_combine") &&
      j["exclude_from_combine"].is_array()) {
    for (auto &v : j["exclude_from_combine"])
      if (v.is_string())
        out.excludeFromCombine.push_back(v.get<std::string>());
  }
  if (j.contains("ignored_inputs") && j["ignored_inputs"].is_array()) {
    for (auto &v : j["ignored_inputs"])
      if (v.is_string())
        out.ignoredInputs.push_back(v.get<std::string>());
  }
  // Migrates the older, gamepad-only "ignored_raw_buttons" int-array
  // format (a bare button index, e.g. 24) into the current
  // "type:value" one ("gamepad:b24") - so a style saved before Ignore
  // Button supported anything but gamepad buttons keeps working
  // exactly as it did, without needing to be re-set-up by hand.
  if (j.contains("ignored_raw_buttons") &&
      j["ignored_raw_buttons"].is_array()) {
    for (auto &v : j["ignored_raw_buttons"])
      if (v.is_number_integer())
        out.ignoredInputs.push_back("gamepad:b" + std::to_string(v.get<int>()));
  }
  if (j.contains("mappings") && j["mappings"].is_object()) {
    for (auto &[key, val] : j["mappings"].items()) {
      if (val.is_string())
        out.mappings[key] = val.get<std::string>();
    }
  }
  return true;
}

const StyleData *getStyleData(const std::string &folderName) {
  auto it = g_styleDataCache.find(folderName);
  if (it != g_styleDataCache.end())
    return &it->second;

  StyleData data;
  if (!loadStyleData(folderName, data))
    return nullptr;
  auto result = g_styleDataCache.emplace(folderName, std::move(data));
  return &result.first->second;
}

GLuint loadTextureFromDisk(const std::string &absolutePath) {
  auto it = g_textureCache.find(absolutePath);
  if (it != g_textureCache.end())
    return it->second;

  if (!std::filesystem::exists(absolutePath)) {
    g_textureCache.emplace(absolutePath, 0);
    return 0;
  }

  int width = 0, height = 0, channels = 0;
  unsigned char *data =
      stbi_load(absolutePath.c_str(), &width, &height, &channels, 4);
  if (!data) {
    spdlog::warn("Input History: failed to decode glyph '{}'", absolutePath);
    g_textureCache.emplace(absolutePath, 0);
    return 0;
  }

  GLuint id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
              GL_UNSIGNED_BYTE, data);
  stbi_image_free(data);

  g_textureCache.emplace(absolutePath, id);
  return id;
}

} // namespace

std::string ensureGlyphsExtracted() {
  if (!glyphExtractedRoot.empty())
    return glyphExtractedRoot;

  std::string dest = config_base_path + "/glyphs";
  std::error_code ec;
  bool alreadyPresent = std::filesystem::exists(dest, ec) &&
                        std::filesystem::is_directory(dest, ec) &&
                        !std::filesystem::is_empty(dest, ec);
  if (!alreadyPresent) {
    std::filesystem::create_directories(dest);
    if (Embedded::glyphs_zip_size > 0) {
      if (extract_zip_from_memory(Embedded::glyphs_zip_data,
                                  Embedded::glyphs_zip_size, dest)) {
        spdlog::info("Extracted Input History glyph pack to {}", dest);
      } else {
        spdlog::error("Failed to extract embedded glyph pack to {}", dest);
      }
    } else {
      spdlog::warn(
          "Embedded glyph pack is empty - Input History glyph styles will "
          "fall back to text labels. See tools/generate_glyphs_zip.py.");
    }
  }

  glyphExtractedRoot = dest;
  return glyphExtractedRoot;
}

void refreshGlyphStyleList() {
  g_styleListLoaded = false;
  g_styleDataCache.clear();
  listGlyphStyles();
}

const std::vector<GlyphStyleInfo> &listGlyphStyles() {
  if (g_styleListLoaded)
    return g_styleList;

  g_styleList.clear();
  std::string root = ensureGlyphsExtracted();

  std::vector<std::string> found;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
    if (!entry.is_directory())
      continue;
    std::string infoPath = entry.path().string() + "/info.json";
    if (std::filesystem::exists(infoPath))
      found.push_back(entry.path().filename().string());
  }

  // Standard styles first, in a fixed order, then anything else
  // (custom mappings) alphabetically.
  std::vector<std::string> ordered;
  for (const auto &name : standardOrder()) {
    if (std::find(found.begin(), found.end(), name) != found.end())
      ordered.push_back(name);
  }
  std::vector<std::string> customs;
  for (const auto &name : found) {
    if (std::find(standardOrder().begin(), standardOrder().end(), name) ==
        standardOrder().end())
      customs.push_back(name);
  }
  std::sort(customs.begin(), customs.end());
  ordered.insert(ordered.end(), customs.begin(), customs.end());

  for (const auto &folderName : ordered) {
    const StyleData *data = getStyleData(folderName);
    if (!data)
      continue;
    GlyphStyleInfo info;
    info.folderName = folderName;
    info.displayName = data->displayName;
    g_styleList.push_back(info);
  }

  g_styleListLoaded = true;
  return g_styleList;
}

void ensureStandardGlyphStylesPresent() {
  std::string root = ensureGlyphsExtracted();
  bool anyMissing = false;
  for (const auto &name : standardOrder()) {
    if (!std::filesystem::exists(root + "/" + name + "/info.json")) {
      anyMissing = true;
      spdlog::warn("Standard glyph style '{}' is missing its info.json - "
                   "will restore it from the bundled pack.",
                   name);
    }
  }
  if (!anyMissing)
    return;

  // Re-extracting is harmless and idempotent: it only ever writes
  // files that are actually part of the embedded zip, so any custom
  // mapping folder (never part of that zip) is completely unaffected,
  // and any standard folder that's still intact just gets overwritten
  // with the same bytes it already had.
  if (Embedded::glyphs_zip_size > 0 &&
      extract_zip_from_memory(Embedded::glyphs_zip_data,
                              Embedded::glyphs_zip_size, root)) {
    spdlog::info("Restored missing standard glyph style(s) from the "
                 "bundled pack.");
  } else {
    spdlog::error("Failed to restore missing standard glyph style(s).");
  }
  refreshGlyphStyleList();
}

GLuint getGlyphTexture(int styleIndex, const std::string &inputKey) {
  const auto &styles = listGlyphStyles();
  if (styleIndex < 0 || styleIndex >= (int)styles.size())
    return 0;

  // Follow combine_with chains (capped to avoid an accidental cycle
  // from hand-edited JSON looping forever).
  std::string folderName = styles[styleIndex].folderName;
  bool first = true;
  for (int hop = 0; hop < 8 && !folderName.empty(); ++hop) {
    const StyleData *data = getStyleData(folderName);
    if (!data)
      return 0;

    // A key this style deliberately excludes from combine_with never
    // falls through, even on the first hop (where "deliberately
    // excludes" is moot since this IS the style being asked for) -
    // the exclusion only actually matters once we're about to move to
    // a *different* folderName below, so check before that hop, not
    // before looking the key up here.
    auto it = data->mappings.find(inputKey);
    if (it != data->mappings.end()) {
      std::string root = ensureGlyphsExtracted();
      std::string path = root + "/" + folderName + "/" + it->second;
      return loadTextureFromDisk(path);
    }

    // Only the originating style's own excludeFromCombine applies -
    // check it before falling through to combine_with (never on a
    // combine_with target itself, so a chain of combines can't
    // surprise-suppress something the middle style didn't ask to).
    if (first) {
      bool excluded = false;
      for (auto &prefix : data->excludeFromCombine) {
        if (inputKey.rfind(prefix, 0) == 0) {
          excluded = true;
          break;
        }
      }
      if (excluded)
        return 0;
    }
    first = false;
    folderName = data->combineWith;
  }
  return 0;
}

void invalidateGlyphTextureCache() { g_textureCache.clear(); }

bool isRawButtonIgnored(int styleIndex, int buttonIdx) {
  return isInputIgnored(styleIndex, "gamepad:b" + std::to_string(buttonIdx));
}

bool isInputIgnored(int styleIndex, const std::string &binding) {
  const auto &styles = listGlyphStyles();
  if (styleIndex < 0 || styleIndex >= (int)styles.size())
    return false;
  const StyleData *data = getStyleData(styles[styleIndex].folderName);
  if (!data)
    return false;
  for (const std::string &b : data->ignoredInputs)
    if (b == binding)
      return true;
  return false;
}

std::string getGlyphStyleDirectory(const std::string &folderName) {
  std::string dir = ensureGlyphsExtracted() + "/" + folderName;
  std::filesystem::create_directories(dir);
  return dir;
}

bool saveGlyphStyleMapping(
    const std::string &folderName, const std::string &displayName,
    const std::vector<std::pair<std::string, std::string>> &mappings,
    const std::string &combineWith,
    const std::vector<std::string> &excludeFromCombine,
    const std::vector<std::string> &ignoredInputs) {
  std::string dir = getGlyphStyleDirectory(folderName);

  json j;
  j["display_name"] = displayName;
  if (!combineWith.empty())
    j["combine_with"] = combineWith;
  if (!excludeFromCombine.empty())
    j["exclude_from_combine"] = excludeFromCombine;
  if (!ignoredInputs.empty())
    j["ignored_inputs"] = ignoredInputs;
  json m = json::object();
  for (auto &[key, filename] : mappings)
    m[key] = filename;
  j["mappings"] = m;

  std::ofstream f(dir + "/info.json");
  if (!f) {
    spdlog::error("Failed to write glyph mapping info.json for '{}'",
                  folderName);
    return false;
  }
  f << j.dump(2);
  f.close();

  // Invalidate any cached data for this style so the change is
  // visible immediately, and pick up a brand new folder in the style
  // list without needing a restart.
  g_styleDataCache.erase(folderName);
  refreshGlyphStyleList();
  spdlog::info("Saved glyph mapping '{}' ({} entries) to {}", displayName,
              mappings.size(), dir + "/info.json");
  return true;
}

std::vector<std::pair<std::string, std::string>>
getGlyphStyleMappings(const std::string &folderName) {
  std::vector<std::pair<std::string, std::string>> result;
  const StyleData *data = getStyleData(folderName);
  if (!data)
    return result;
  for (auto &[key, filename] : data->mappings)
    result.emplace_back(key, filename);
  return result;
}

std::string getGlyphStyleCombineWith(const std::string &folderName) {
  const StyleData *data = getStyleData(folderName);
  return data ? data->combineWith : "";
}

std::vector<std::string> getGlyphStyleIgnoredInputs(const std::string &folderName) {
  const StyleData *data = getStyleData(folderName);
  return data ? data->ignoredInputs : std::vector<std::string>{};
}

std::string getGlyphStyleDisplayNameFor(const std::string &folderName) {
  const StyleData *data = getStyleData(folderName);
  return data ? data->displayName : folderName;
}

namespace {

// Simple bilinear resize - RGBA, 4 bytes/pixel, no external resize
// library needed for what's just icon-sized images. Not aiming for
// stb_image_resize-grade filtering quality (Mitchell/Catmull-Rom
// etc.) - bilinear is entirely sufficient at this size and this is
// far more reliable to vendor correctly than pulling in a second
// ~10,000-line stb header just for this one call site.
std::vector<unsigned char> bilinearResize(const unsigned char *src, int srcW,
                                          int srcH, int dstW, int dstH) {
  std::vector<unsigned char> out((size_t)dstW * dstH * 4);
  for (int y = 0; y < dstH; ++y) {
    float srcY = (y + 0.5f) * srcH / dstH - 0.5f;
    int y0 = (int)std::floor(srcY);
    float fy = srcY - y0;
    int y0c = std::clamp(y0, 0, srcH - 1);
    int y1c = std::clamp(y0 + 1, 0, srcH - 1);

    for (int x = 0; x < dstW; ++x) {
      float srcX = (x + 0.5f) * srcW / dstW - 0.5f;
      int x0 = (int)std::floor(srcX);
      float fx = srcX - x0;
      int x0c = std::clamp(x0, 0, srcW - 1);
      int x1c = std::clamp(x0 + 1, 0, srcW - 1);

      for (int c = 0; c < 4; ++c) {
        float p00 = src[(y0c * srcW + x0c) * 4 + c];
        float p10 = src[(y0c * srcW + x1c) * 4 + c];
        float p01 = src[(y1c * srcW + x0c) * 4 + c];
        float p11 = src[(y1c * srcW + x1c) * 4 + c];
        float top = p00 + (p10 - p00) * fx;
        float bottom = p01 + (p11 - p01) * fx;
        float value = top + (bottom - top) * fy;
        out[(y * dstW + x) * 4 + c] =
            (unsigned char)std::clamp(value + 0.5f, 0.0f, 255.0f);
      }
    }
  }
  return out;
}

} // namespace

bool convertAndSaveGlyphImage(const std::string &sourcePath,
                              const std::string &destPath, int targetSize) {
  int w = 0, h = 0, channels = 0;
  unsigned char *data = stbi_load(sourcePath.c_str(), &w, &h, &channels, 4);
  if (!data) {
    spdlog::error("convertAndSaveGlyphImage: failed to load '{}'", sourcePath);
    return false;
  }

  std::vector<unsigned char> resized;
  const unsigned char *toWrite = data;
  if (w != targetSize || h != targetSize) {
    resized = bilinearResize(data, w, h, targetSize, targetSize);
    toWrite = resized.data();
    w = h = targetSize;
  }

  std::filesystem::create_directories(
      std::filesystem::path(destPath).parent_path());
  int ok = stbi_write_png(destPath.c_str(), w, h, 4, toWrite, w * 4);
  stbi_image_free(data);

  if (!ok) {
    spdlog::error("convertAndSaveGlyphImage: failed to write '{}'", destPath);
    return false;
  }
  spdlog::info("Converted glyph image '{}' -> '{}' ({}x{})", sourcePath,
              destPath, targetSize, targetSize);
  return true;
}

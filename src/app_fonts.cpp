#include "app_fonts.h"

#include "fonts_data.h"
#include "imgui.h"
#include "misc/freetype/imgui_freetype.h"

// Reference size for every font below. ImGui 1.92's atlas is dynamic:
// glyphs are rasterized on first use, at whatever size is actually
// drawn (Input History's Font Size, for example), so this only sets the
// default and adding the fallback fonts costs nothing until a character
// from one of them is actually shown.
static constexpr float kUiFontSize = 15.0f;

static void addFont(ImFontAtlas *atlas, const unsigned char *data, size_t size,
                    ImFontConfig cfg) {
  // Embedded, static data - the atlas must not try to free it.
  cfg.FontDataOwnedByAtlas = false;
  atlas->AddFontFromMemoryTTF(const_cast<unsigned char *>(data), (int)size,
                              kUiFontSize, &cfg);
}

void setupAppFonts(ImGuiIO &io) {
  ImFontAtlas *atlas = io.Fonts;

  ImFontConfig cfg;
  addFont(atlas, Embedded::noto_sans_ttf, Embedded::noto_sans_ttf_size, cfg);
  ImFont *uiFont = atlas->Fonts.back();

  // Everything below is merged into the UI font as a fallback: a
  // character is taken from the first source (in this order) that has
  // it, so these never change how text Noto Sans can already draw looks.
  cfg.MergeMode = true;
  addFont(atlas, Embedded::noto_sans_math_ttf,
          Embedded::noto_sans_math_ttf_size, cfg); // arrows, math

  ImFontConfig emojiCfg = cfg;
  emojiCfg.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LoadColor;
  addFont(atlas, Embedded::twemoji_ttf, Embedded::twemoji_ttf_size,
          emojiCfg); // color emoji

  addFont(atlas, Embedded::noto_sans_symbols2_ttf,
          Embedded::noto_sans_symbols2_ttf_size,
          cfg); // check marks and symbols without an emoji form

  io.FontDefault = uiFont;
  ImGui::GetStyle().FontSizeBase = kUiFontSize;
}

#pragma once

struct ImGuiIO;

// Loads the app's UI font (Noto Sans) plus its fallbacks - arrows/math,
// color emoji (Twemoji) and other symbols - into the current ImGui
// context's font atlas, and makes it the default. Call right after
// ImGui::CreateContext(), once per context. See assets/fonts/README.txt
// for the fonts and their licenses.
void setupAppFonts(ImGuiIO &io);

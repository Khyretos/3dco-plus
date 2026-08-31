#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#elif __FreeBSD__
#elif __ANDROID__
#elif __APPLE__
#include <sys/wait.h>
#include <unistd.h>
#elif _WIN32
#include <windows.h>
#define SDL_MAIN_HANDLED
#else // some other operating system
#endif

#include "controller_window.h"
#include "icon_data.h"
#include "imfilebrowser.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "keyboard_input.h"
#include "log_window.h"
#include "model.h"
#include "settings.h"
#include "settings_window.h"
#include "shader.h"
#include "stb_image.h"
#include "strings.h"
#include "tray_icon.h"
#include <SDL3/SDL_joystick.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>
#include <stdio.h>
#include <sys/stat.h>

static void DraggableTooltip(const char *text) {
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    // Colored first line (teal/green) – indicates draggable
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.8f, 1.0f));
    ImGui::Text("Drag to change or click to type.");
    ImGui::PopStyleColor();
    // Second line – normal white (default color)
    ImGui::TextUnformatted(text);
    ImGui::EndTooltip();
  }
}

// Unified drag-float helper for every angle-related slider in this
// file. Two different storage conventions exist across the codebase:
// some angle fields store radians (the shader/math code consumes them
// directly as radians - e.g. texture rotation, mesh pivot rotation),
// others already store degrees natively (camera yaw/pitch/roll,
// spotlight yaw/pitch/cutoff/outer_cutoff). is_radians selects which,
// so every angle control goes through one consistent helper instead of
// each call site hand-rolling its own DragFloat with its own
// speed/format/range - and, for radian fields specifically, instead of
// risking exactly the bug this replaces: the old texture Rotation
// slider labeled itself "deg" with a -180..180 range but never
// actually converted, so what the shader consumed as radians was
// edited directly as if it were already degrees.
static bool draggableFloatAngle(const char *label, float *value,
                                bool is_radians, float speed = 0.5f,
                                float min_deg = -360.0f, float max_deg = 360.0f,
                                const char *format = "%.1f\xC2\xB0") {
  if (is_radians) {
    float degrees = glm::degrees(*value);
    if (ImGui::DragFloat(label, &degrees, speed, min_deg, max_deg, format)) {
      *value = glm::radians(degrees);
      return true;
    }
    return false;
  }
  return ImGui::DragFloat(label, value, speed, min_deg, max_deg, format);
}

static std::string g_last_glfw_error;

void glfw_error_callback(int error, const char *description) {
  g_last_glfw_error = std::string(description);
  spdlog::error("GLFW error {}: {}", error, description);
}

static GLuint getHelpIcon() {
  static GLuint iconTex = 0;
  if (iconTex)
    return iconTex;

  int w, h, comp;
  unsigned char *data = stbi_load_from_memory(
      Embedded::icon_data, static_cast<int>(Embedded::icon_size), &w, &h, &comp,
      4);
  if (!data) {
    spdlog::warn("Failed to load embedded icon for help section");
    return 0;
  }
  glGenTextures(1, &iconTex);
  glBindTexture(GL_TEXTURE_2D, iconTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(data);
  return iconTex;
}

// ---- Capture state for auto‑binding (global) ----
static struct CaptureState {
  bool active = false;
  controller_window *window = nullptr;
  int mesh = -1;
  int type = -1; // 0=gamepad,1=joystick,2=keyboard,3=mouse
  bool keyboard_snapshot[SDL_SCANCODE_COUNT];
  bool gamepad_snapshot[32];
  bool joystick_snapshot[128];
  bool mouse_snapshot[8];
  float axis_snapshot[128];
} capture;

static void startCapture(controller_window *w, int meshIdx, int type) {
  spdlog::debug("startCapture called: mesh={}, type={}", meshIdx, type);

  capture.active = true;
  capture.window = w;
  capture.mesh = meshIdx;
  capture.type = type;

  // Zero out snapshots to avoid garbage
  memset(capture.joystick_snapshot, 0, sizeof(capture.joystick_snapshot));
  memset(capture.axis_snapshot, 0, sizeof(capture.axis_snapshot));

  if (type == 2) { // Keyboard
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
      capture.keyboard_snapshot[i] = GlobalKeyboard::isPressed((SDL_Scancode)i);
    }
  } else if (type == 0) { // Gamepad
    if (w->sdl_controller) {
      for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT && i < 32; ++i) {
        capture.gamepad_snapshot[i] =
            SDL_GetGamepadButton(w->sdl_controller, (SDL_GamepadButton)i);
      }
      for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT && i < 32; ++i) {
        capture.axis_snapshot[i] =
            SDL_GetGamepadAxis(w->sdl_controller, (SDL_GamepadAxis)i) /
            32767.0f;
      }
    }
  } else if (type == 1) { // Joystick – fallback to raw joystick from
                          // gamecontroller if needed
    SDL_Joystick *joy = nullptr;
    if (w->sdl_joystick)
      joy = w->sdl_joystick;
    else if (w->sdl_controller)
      joy = SDL_GetGamepadJoystick(w->sdl_controller);
    if (joy) {
      int numButtons = SDL_GetNumJoystickButtons(joy);
      int maxButtons = std::min(numButtons, 128);
      for (int i = 0; i < maxButtons; ++i) {
        capture.joystick_snapshot[i] = SDL_GetJoystickButton(joy, i);
      }
      int numAxes = SDL_GetNumJoystickAxes(joy);
      int maxAxes = std::min(numAxes, 128);
      for (int i = 0; i < maxAxes; ++i) {
        capture.axis_snapshot[i] = SDL_GetJoystickAxis(joy, i) / 32767.0f;
      }
      spdlog::debug("Joystick snapshot: {} buttons, {} axes", maxButtons,
                    maxAxes);
    } else {
      spdlog::warn("No joystick available for capture");
    }
  } else if (type == 3) { // Mouse
    for (int i = 0; i < 8; ++i) {
      capture.mouse_snapshot[i] = GlobalKeyboard::isMouseButtonPressed(i);
    }
  }
}

static void clearCapture() {
  spdlog::debug("clearCapture called");
  capture.active = false;
  capture.window = nullptr;
  capture.mesh = -1;
  capture.type = -1;
}

static bool pollAndCapture(controller_window *w, int meshIdx, int type,
                           std::string &outBinding) {
  if (!capture.active || capture.window != w || capture.mesh != meshIdx ||
      capture.type != type) {
    return false;
  }

  // ---- Keyboard ----
  if (type == 2) {
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
      bool current = GlobalKeyboard::isPressed((SDL_Scancode)i);
      if (current && !capture.keyboard_snapshot[i]) {
        const char *name = SDL_GetScancodeName((SDL_Scancode)i);
        if (name) {
          outBinding = "key_" + std::string(name);
          for (char &c : outBinding)
            c = tolower(c);
          spdlog::debug("Keyboard capture: {}", outBinding);
          return true;
        }
      }
    }
  }

  // ---- Gamepad ----
  if (type == 0 && w->sdl_controller) {
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT && i < 32; ++i) {
      bool current =
          SDL_GetGamepadButton(w->sdl_controller, (SDL_GamepadButton)i);
      if (current && !capture.gamepad_snapshot[i]) {
        outBinding = "b" + std::to_string(i);
        spdlog::debug("Gamepad button capture: {}", outBinding);
        return true;
      }
    }
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT && i < 32; ++i) {
      float current =
          SDL_GetGamepadAxis(w->sdl_controller, (SDL_GamepadAxis)i) / 32767.0f;
      float snap = capture.axis_snapshot[i];
      if (fabs(current) > 0.8f && fabs(current - snap) > 0.2f) {
        std::string dir = (current > 0) ? "+" : "-";
        outBinding = "a" + std::to_string(i) + dir;
        spdlog::debug("Gamepad axis capture: {}", outBinding);
        return true;
      }
    }
  }

  // ---- Joystick (raw) ----
  if (type == 1) {
    SDL_Joystick *joy = nullptr;
    if (w->sdl_joystick)
      joy = w->sdl_joystick;
    else if (w->sdl_controller)
      joy = SDL_GetGamepadJoystick(w->sdl_controller);
    if (joy) {
      int numButtons = SDL_GetNumJoystickButtons(joy);
      int maxButtons = std::min(numButtons, 128);
      for (int i = 0; i < maxButtons; ++i) {
        bool current = SDL_GetJoystickButton(joy, i);
        if (current && !capture.joystick_snapshot[i]) {
          outBinding = "b" + std::to_string(i);
          spdlog::debug("Joystick button capture: {}", outBinding);
          return true;
        }
      }
      int numAxes = SDL_GetNumJoystickAxes(joy);
      int maxAxes = std::min(numAxes, 128);
      for (int i = 0; i < maxAxes; ++i) {
        float current = SDL_GetJoystickAxis(joy, i) / 32767.0f;
        float snap = capture.axis_snapshot[i];
        if (fabs(current) > 0.8f && fabs(current - snap) > 0.2f) {
          std::string dir = (current > 0) ? "+" : "-";
          outBinding = "a" + std::to_string(i) + dir;
          spdlog::debug("Joystick axis capture: {}", outBinding);
          return true;
        }
      }
    }
  }

  // ---- Mouse ----
  if (type == 3) {
    for (int i = 0; i < 8; ++i) {
      bool current = GlobalKeyboard::isMouseButtonPressed(i);
      if (current && !capture.mouse_snapshot[i]) {
        const char *names[] = {"left", "right", "middle", "4",
                               "5",    "6",     "7",      "8"};
        outBinding = "mouse_" + std::string(names[i]);
        spdlog::debug("Mouse button capture: {}", outBinding);
        return true;
      }
    }
  }

  return false;
}

using json = nlohmann::json;

extern std::vector<controller_window> windows;
extern std::string button_names[21];
extern std::string config_base_path;

static bool HasTouchpadFinger(controller_window *w, int touchpadIdx,
                              int fingerIdx) {
  if (!w || !w->is_gamecontroller || !w->sdl_controller)
    return false;
  int numTouchpads = SDL_GetNumGamepadTouchpads(w->sdl_controller);
  if (touchpadIdx >= numTouchpads)
    return false;
  int numFingers =
      SDL_GetNumGamepadTouchpadFingers(w->sdl_controller, touchpadIdx);
  return fingerIdx < numFingers;
}

bool g_log_controller = false;
bool g_log_keyboard = false;
bool g_log_mouse = false;
bool g_tray_enabled = false;
// Gates verbose per-item diagnostic logging (e.g. "Loading mesh: X" for
// every mesh in a model) that is only useful when actively debugging,
// but was previously always emitted at spdlog::info - see
// setDebugModeEnabled() below for what this actually does at runtime.
bool g_debug_mode_enabled = false;

void setDebugModeEnabled(bool enabled) {
  g_debug_mode_enabled = enabled;
  // spdlog checks the active level before formatting a message at all,
  // so raising/lowering it here is what actually removes the per-mesh /
  // per-item logging overhead when debug mode is off, rather than just
  // hiding already-formatted lines after the fact. Anything logged at
  // spdlog::debug (see model.cpp's mesh loader, for instance) is a
  // no-op below this line's cost when disabled.
  spdlog::set_level(enabled ? spdlog::level::debug : spdlog::level::info);
}

// Export Mapping button's transient result banner (see the button's
// handler further down) - not per-window, just reused for whichever
// window's button was last clicked, matching how the network status
// toast is scoped per-window instead (this one doesn't need to be,
// since only one export can happen at a time from user interaction).
std::string export_mapping_result;
bool export_mapping_ok = false;
double export_mapping_popup_until = 0.0;

static int last_logged_device_index = -1;

// Helper to center a piece of text or a widget horizontally within the current
// content region.
static void CenterItem(float itemWidth) {
  float availWidth = ImGui::GetContentRegionAvail().x;
  float offset = (availWidth - itemWidth) * 0.5f;
  if (offset > 0.0f) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
  }
}

static void CenterText(const char *text) {
  float textWidth = ImGui::CalcTextSize(text).x;
  CenterItem(textWidth);
}

// ------------------------------------------------------------------
// Shaded submenu helper
// ------------------------------------------------------------------
static void BeginShadedGroup() {
  ImGui::GetWindowDrawList()->ChannelsSplit(2);
  ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
  ImGui::Indent(6.0f);
  ImGui::Dummy(ImVec2(0.0f, 4.0f)); // top padding
  ImGui::BeginGroup();
}

static void EndShadedGroup(ImU32 bgColor, ImU32 borderColor) {
  ImGui::EndGroup();

  // Get the group's bounding rectangle
  ImVec2 rectMin = ImGui::GetItemRectMin();
  ImVec2 rectMax = ImGui::GetItemRectMax();

  // Extend the background to the full window width (minus a small margin)
  float fullWidth =
      ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2;
  rectMin.x = ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x;
  rectMax.x = rectMin.x + fullWidth;

  // Add some padding inside the background
  ImVec2 pad(8.0f, 4.0f);
  rectMin.x -= pad.x;
  rectMax.x += pad.x;
  rectMin.y -= pad.y;
  rectMax.y += pad.y;

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->ChannelsSetCurrent(0);
  dl->AddRectFilled(rectMin, rectMax, bgColor, 6.0f);
  dl->AddRect(rectMin, rectMax, borderColor, 6.0f);
  dl->ChannelsMerge();

  ImGui::Dummy(ImVec2(0.0f, 4.0f)); // bottom padding
  ImGui::Unindent(6.0f);
}

// Fitting purple/black themed accent shades - each submenu gets its own
// tint so the UI reads as colorful while staying on-theme.
static ImU32 ShadeColor(float r, float g, float b, float a = 0.30f) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}
static ImU32 ShadeBorder(float r, float g, float b, float a = 0.65f) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

// Actual filenames of OBJ meshes (used for file I/O)
std::string mesh_filenames[35] = {
    "top_shell.obj",    "bottom_shell.obj",  "extra.obj",
    "left_trigger.obj", "right_trigger.obj", "left_stick.obj",
    "right_stick.obj",  "left_ring.obj",     "right_ring.obj",
    "a_button.obj",     "b_button.obj",      "x_button.obj",
    "y_button.obj",     "back_button.obj",   "guide_button.obj",
    "start_button.obj", "left_cap.obj",      "right_cap.obj",
    "left_bumper.obj",  "right_bumper.obj",  "dpad_up.obj",
    "dpad_down.obj",    "dpad_left.obj",     "dpad_right.obj",
    "misc.obj",         "paddle1.obj",       "paddle2.obj",
    "paddle3.obj",      "paddle4.obj",       "touchpad.obj",
    "touch_point1.obj", "touch_point2.obj",  "jltouchpad2.obj",
    "touch_point3.obj", "touch_point4.obj"};

std::string invalid_characters = "\\/:*?\"<>|";

std::string input_names[35] = {"a button",
                               "b button",
                               "x button",
                               "y button",
                               "back button",
                               "guide button",
                               "start button",
                               "left stick click",
                               "right stick click",
                               "left bumper",
                               "right bumper",
                               "dpad up",
                               "dpad down",
                               "dpad left",
                               "dpad right",
                               "touchpad click",
                               "misc button",
                               "paddle 1",
                               "paddle 2",
                               "paddle 3",
                               "paddle 4",
                               "left stick x-axis",
                               "left stick y-axis",
                               "right stick x-axis",
                               "right stick y-axis",
                               "left trigger",
                               "right trigger",
                               "Touchpad 0 Finger 0 X",
                               "Touchpad 0 Finger 0 Y",
                               "Touchpad 0 Finger 1 X",
                               "Touchpad 0 Finger 1 Y",
                               "Touchpad 1 Finger 0 X",
                               "Touchpad 1 Finger 0 Y",
                               "Touchpad 1 Finger 1 X",
                               "Touchpad 1 Finger 1 Y"};

std::string mesh_names[35] = {
    "top shell",     "bottom shell",  "extra",         "left trigger",
    "right trigger", "left stick",    "right stick",   "left ring",
    "right ring",    "a button",      "b button",      "x button",
    "y button",      "back button",   "guide button",  "start button",
    "left cap",      "right cap",     "left bumper",   "right bumper",
    "d-pad up",      "d-pad down",    "d-pad left",    "d-pad right",
    "misc",          "paddle 1",      "paddle 2",      "paddle 3",
    "paddle 4",      "touchpad",      "touch point 1", "touch point 2",
    "touchpad 2",    "touch point 3", "touch point 4"};

bool remap = false;
std::string rebind_string = "";

unsigned int tabs_made = 0;
unsigned selected_tab = 0;
int selected_mesh = -1; // -1 == no row selected
unsigned material_mesh = 0;
unsigned texture_mesh = 0;

GLFWwindow *glfw_settings_window;
GLFWmonitor *primary_monitor;
const GLFWvidmode *vid_mode;
// The log window (see log_window.cpp) now owns its own GLFW window and
// ImGui context so it can stay always-on-top independently of this
// window. Since ImGui's "current context" is global, drawSettingsWindow()
// explicitly restores this one on entry rather than assuming it's still
// current - the log window borrows and restores the previous context
// around its own rendering, but being explicit here too means the two
// can never silently step on each other regardless of call order.
ImGuiContext *g_settings_imgui_ctx = nullptr;

ImGui::FileBrowser texture_dialog;
ImGui::FileBrowser model_dialog;
ImGui::FileBrowser import_model_dialog;
ImGui::FileBrowser shader_resource_dialog;
// Which shader the next shader_resource_dialog selection should be
// copied into as a channelN.* file - set when the "Add Resource" button
// opens the dialog, consumed once a file is picked.
std::string g_shader_resource_target;

std::vector<window_tab> tabs;
std::vector<Texture> textures;

ImVec4 clear_color = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
ImGuiIO *io;

void createSettingsWindow() {
  // Set error callback first
  glfwSetErrorCallback(glfw_error_callback);

  // Try to initialize GLFW without forcing a specific platform.
  // On systems with both Wayland and X11, GLFW will choose Wayland if
  // available, but if that fails (e.g. no Wayland compositor), we fall back to
  // X11.
  if (!glfwInit()) {
    spdlog::warn("GLFW initialization failed. Retrying with X11 platform...");
    glfwTerminate(); // clean up any partial state
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (!glfwInit()) {
      spdlog::critical("GLFW initialization failed even with X11 platform.");
      exit(1);
    }
  }

  // Log which platform GLFW is using
#if defined(GLFW_PLATFORM_WAYLAND) && defined(GLFW_PLATFORM_X11)
  int platform = glfwGetPlatform();
  const char *platform_name = platform == GLFW_PLATFORM_WAYLAND ? "Wayland"
                              : platform == GLFW_PLATFORM_X11   ? "X11"
                                                                : "unknown";
  spdlog::info("GLFW windowing backend: {}", platform_name);
#endif

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

  // Try to create the window with transparent framebuffer first
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
  glfw_settings_window =
      glfwCreateWindow(640, 480, "3D Controller Overlay", NULL, NULL);

  if (!glfw_settings_window) {
    spdlog::warn("Failed to create window with transparent framebuffer: {}",
                 g_last_glfw_error);
    spdlog::info("Retrying without transparent framebuffer...");
    // Clear the hint and try again
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
    glfw_settings_window =
        glfwCreateWindow(640, 480, "3D Controller Overlay", NULL, NULL);
  }

  if (!glfw_settings_window) {
    spdlog::critical(
        "Failed to create settings window even without transparency: {}",
        g_last_glfw_error);
    glfwTerminate();
    exit(1);
  }

  glfwMakeContextCurrent(glfw_settings_window);
  glfwSwapInterval(1);
  glfwSetFramebufferSizeCallback(glfw_settings_window,
                                 settings_framebuffer_size_callback);

  GLFWimage images[1];
  images[0].pixels = stbi_load_from_memory(
      Embedded::icon_data, static_cast<int>(Embedded::icon_size),
      &images[0].width, &images[0].height, nullptr, 4);
  if (images[0].pixels == NULL) {
    spdlog::warn("Could not load embedded icon for settings window.");
  } else {
    glfwSetWindowIcon(glfw_settings_window, 1, images);
    stbi_image_free(images[0].pixels);
  }

  primary_monitor = glfwGetPrimaryMonitor();
  vid_mode = glfwGetVideoMode(primary_monitor);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    spdlog::critical("Failed to initialize GLAD - no valid OpenGL context. "
                     "Cannot continue.");
    exit(1);
  }

  IMGUI_CHECKVERSION();
  g_settings_imgui_ctx = ImGui::CreateContext();
  io = &ImGui::GetIO();
  (void)io;

  // ---- Redirect ImGui INI file to config_base_path ----
  static std::string ini_path = config_base_path + "/imgui.ini";
  io->IniFilename = ini_path.c_str();

  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 purple = ImVec4(0.45f, 0.18f, 0.59f, 1.0f); // royal purple
  ImVec4 purple_light = ImVec4(0.6f, 0.3f, 0.75f, 1.0f);
  ImVec4 purple_dark = ImVec4(0.3f, 0.1f, 0.4f, 1.0f);
  style.Colors[ImGuiCol_Button] = purple;
  style.Colors[ImGuiCol_ButtonHovered] = purple_light;
  style.Colors[ImGuiCol_ButtonActive] = purple_dark;
  style.Colors[ImGuiCol_Header] = purple;
  style.Colors[ImGuiCol_HeaderHovered] = purple_light;
  style.Colors[ImGuiCol_HeaderActive] = purple_dark;
  style.Colors[ImGuiCol_CheckMark] = purple_light;
  style.Colors[ImGuiCol_SliderGrab] = purple;
  style.Colors[ImGuiCol_SliderGrabActive] = purple_light;
  style.Colors[ImGuiCol_FrameBgHovered] = purple_dark;
  style.Colors[ImGuiCol_Tab] = purple_dark;
  style.Colors[ImGuiCol_TabHovered] = purple_light;
  style.Colors[ImGuiCol_TabActive] = purple;
  style.Colors[ImGuiCol_ResizeGrip] = purple;
  style.Colors[ImGuiCol_ResizeGripHovered] = purple_light;
  style.Colors[ImGuiCol_ResizeGripActive] = purple_dark;

  // ---- Additional styling for tree nodes and combo boxes ----
  style.Colors[ImGuiCol_FrameBg] = purple_dark;
  style.Colors[ImGuiCol_FrameBgHovered] = purple_light;
  style.Colors[ImGuiCol_FrameBgActive] = purple;

  ImGui_ImplGlfw_InitForOpenGL(glfw_settings_window, true);
  if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
    spdlog::critical("Failed to initialize ImGui's OpenGL3 backend. "
                     "Cannot continue.");
    exit(1);
  }

  texture_dialog.SetWindowSize(400, 300);
  texture_dialog.SetTitle("Select Texture File");
  texture_dialog.SetTypeFilters({".png", ".jpg"});

  model_dialog.SetWindowSize(400, 300);
  model_dialog.SetTitle("Select Model File");
  model_dialog.SetTypeFilters(
      {".obj", ".fbx", ".gltf", ".glb", ".blend", ".dae", ".stl"});

  import_model_dialog.SetWindowSize(400, 300);
  import_model_dialog.SetTitle("Import 3D Model");
  import_model_dialog.SetTypeFilters(
      {".obj", ".fbx", ".gltf", ".glb", ".blend", ".dae", ".stl"});

  shader_resource_dialog.SetWindowSize(400, 300);
  shader_resource_dialog.SetTitle("Add Shader Resource (Channel Texture)");
  shader_resource_dialog.SetTypeFilters({".png", ".jpg", ".jpeg"});
}

GLFWwindow *getSettingsWindow() { return glfw_settings_window; }

void settings_framebuffer_size_callback(GLFWwindow *window, int width,
                                        int height) {
  glViewport(0, 0, width, height);
}

void close_window(unsigned ID) {
  removeControllerWindow(ID);
  removeTab(ID);
}

const GLFWvidmode *get_vid_mode() { return vid_mode; }

void removeTab(unsigned ID) {
  for (unsigned i = 0; i < tabs.size(); ++i) {
    if (tabs[i].ID == ID) {
      tabs.erase(tabs.begin() + i);
      selected_tab = 0;
      break;
    }
  }
}

void removeSettingsWindow() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(glfw_settings_window);
}

void settings_window_input(bool &quit) {
  if (glfwGetKey(glfw_settings_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(glfw_settings_window, true);
  }
  if (glfwWindowShouldClose(glfw_settings_window)) {
    quit = true;
  }
}

void settings_sdl_events(SDL_Event *event) {
  // Maybe used later
}

bool check_tab_title_exists(std::string title) {
  bool exists = false;
  for (my_tab t : tabs) {
    if (title == t.title) {
      exists = true;
      break;
    }
  }
  return exists;
}

// Forward declarations of helper functions (defined later)
void DrawImportPreviewControls(controller_window &w);
void SaveImportedModel(controller_window &w);
void writeOBJ(const std::string &path, const ImportedMesh &mesh);

void drawSettingsWindow() {
  glfwMakeContextCurrent(glfw_settings_window);
  ImGui::SetCurrentContext(g_settings_imgui_ctx);
  glfwSwapInterval(1);

  // Refresh the tray icon's menu data every frame, unconditionally
  // (not nested inside the per-tab detail view below, which only draws
  // when at least one controller window is open and selected) - so
  // closing every controller window correctly clears the tray's
  // "Controllers" submenu too, instead of leaving it showing windows
  // that no longer exist.
  if (g_tray_enabled) {
    std::vector<TrayIcon::ControllerEntry> tray_controllers;
    tray_controllers.reserve(tabs.size());

    TrayIcon::NetworkStatus net_status = TrayIcon::NetworkStatus::Disabled;
    std::vector<TrayIcon::ConnectionEntry> net_connections;

    for (const auto &t : tabs) {
      controller_window *cw = getControllerWindow(t.ID);
      if (!cw)
        continue;
      TrayIcon::ControllerEntry entry;
      entry.id = t.ID;
      entry.title = t.title;
      entry.minimized = isControllerWindowMinimized(*cw);
      tray_controllers.push_back(entry);

      if (!cw->network_enabled)
        continue;
      const char *proto = cw->network_protocol == 1 ? "TCP" : "UDP";
      bool connecting =
          cw->network_protocol == 1
              ? cw->network_tcp_connecting
              : (!cw->network_handshake_ack && cw->network_mode == 0);
      if (cw->network_connected) {
        if (net_status != TrayIcon::NetworkStatus::Connected)
          net_status = TrayIcon::NetworkStatus::Connected;
        TrayIcon::ConnectionEntry ce;
        ce.label =
            cw->network_mode == 0
                ? (t.title + ": sending " + proto + " to " + cw->network_ip +
                   ":" + std::to_string(cw->network_port))
                : (t.title + ": receiving " + proto + " on port " +
                   std::to_string(cw->network_port));
        net_connections.push_back(ce);
      } else if (connecting) {
        if (net_status == TrayIcon::NetworkStatus::Disabled)
          net_status = TrayIcon::NetworkStatus::Connecting;
      }
    }
    TrayIcon::setControllerList(tray_controllers);
    TrayIcon::setNetworkStatus(net_status, net_connections);
  }

  // ---- Set viewport to framebuffer size (fixes Retina scaling) ----
  int fb_width, fb_height;
  glfwGetFramebufferSize(glfw_settings_window, &fb_width, &fb_height);
  glViewport(0, 0, fb_width, fb_height);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  static bool show_delete_popup = false;

  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove;

#ifdef IMGUI_HAS_VIEWPORT
  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);
#else
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
#endif

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("Settings Window", nullptr, window_flags);

  // ---- Draw Import Preview controls if any preview window exists ----
  for (auto &w : windows) {
    if (w.is_import_preview && w.import_preview.is_open) {
      if (ImGui::CollapsingHeader("Import Preview",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawImportPreviewControls(w);
      }
      break;
    }
  }

  bool new_controller_window = false;
  int new_tab_number = 1;
  std::string new_tab_title = "Controller ";
  while (check_tab_title_exists(
      std::string("Controller ").append(std::to_string(new_tab_number)))) {
    new_tab_number++;
  }
  new_tab_title.append(std::to_string(new_tab_number));

  static ImGuiTabBarFlags tab_bar_flags =
      ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable |
      ImGuiTabBarFlags_FittingPolicyResizeDown;

  if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags)) {
    if (ImGui::TabItemButton("New", ImGuiTabItemFlags_Trailing |
                                        ImGuiTabItemFlags_NoTooltip)) {
      window_tab new_tab;
      tabs_made++;
      new_tab.title = new_tab_title;
      tabs.push_back(new_tab);
      new_controller_window = true;
    }
    for (unsigned i = 0; i < tabs.size(); ++i) {
      bool open = true;
      if (ImGui::BeginTabItem(tabs[i].title.c_str(), &open,
                              ImGuiTabItemFlags_None)) {
        selected_tab = i;
        ImGui::EndTabItem();
      }
      if (!open) {
        unsigned id = tabs[i].ID;
        removeControllerWindow(id);
        removeTab(id);
        // The tab is gone; selected_tab will be reset inside removeTab
      }
    }
    ImGui::EndTabBar();
  }

  if (tabs.size() > 0 && new_controller_window == false) {
    controller_window *current_window =
        getControllerWindow(tabs[selected_tab].ID);

    if (current_window->is_import_preview) {
      // Preview windows only show import controls (drawn outside this block)
      // Skip all normal sections.
      ImGui::End();
      ImGui::PopStyleVar();
      return;
    }
    if (!current_window) {
      ImGui::End();
      ImGui::PopStyleVar();
      return;
    }

    bool receiverMode =
        (current_window->network_enabled && current_window->network_mode == 1);

    // ============================================================
    // WINDOW
    // ============================================================
    if (ImGui::CollapsingHeader("Window")) {
      char title[20] = {};
      if (ImGui::InputTextWithHint("Title", tabs[selected_tab].title.c_str(),
                                   title, IM_ARRAYSIZE(title),
                                   ImGuiInputTextFlags_EnterReturnsTrue)) {
        glfwSetWindowTitle(current_window->glfw_window, title);
        tabs[selected_tab].title = std::string(title);
        current_window->window_title = std::string(title);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Set the window title.");
      ImGui::NewLine();

      // ---- System tray ----
      // Global (app-wide) setting, not specific to this window, but
      // grouped here under Window per request. Hidden entirely on
      // platforms without a real implementation (see the comment at
      // the top of tray_icon.h) rather than shown as a control that
      // can't do anything. Note: since this now lives inside a
      // per-tab section, it's only reachable while at least one
      // controller window is open and this section is selected.
      if (TrayIcon::isSupported()) {
        if (ImGui::Checkbox("Enable Taskbar Icon", &g_tray_enabled)) {
          if (g_tray_enabled) {
            if (!TrayIcon::enable())
              g_tray_enabled = false; // creation failed - don't claim it's on
          } else {
            TrayIcon::disable();
          }
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Adds a system tray icon. Click it to minimize/restore the "
              "main window; right-click for a menu with per-controller "
              "minimize/restore, network status, and Quit.");
        ImGui::SameLine();
      }
      // Debug Mode is intentionally not nested inside the
      // TrayIcon::isSupported() check above - it's a logging setting
      // with nothing to do with the tray, and needs to be reachable on
      // every platform (including ones like macOS where the tray icon
      // isn't implemented yet and that whole block is hidden).
      if (ImGui::Checkbox("Enable Debug Mode", &g_debug_mode_enabled)) {
        setDebugModeEnabled(g_debug_mode_enabled);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Enables verbose diagnostic logging (e.g. every mesh loaded "
            "per model). Off by default since it adds a small but "
            "noticeable delay when loading models with many meshes - "
            "turn this on before opening the log window if you need to "
            "report a bug.");
      ImGui::NewLine();

#if defined(_WIN32)
      // Minimize/Maximize/Restore buttons - only needed on Windows,
      // where the window is always shown through the undecorated
      // companion window (see createTransparentOverlay() in
      // controller_window.cpp) regardless of the Borderless setting,
      // so there's no title bar to double-click or grab a native
      // minimize/maximize button from. On Linux/macOS the window has
      // real OS decorations unless the user separately checks
      // Borderless, so the native controls already exist there -
      // hidden here entirely rather than shown as a redundant control.
      // These call the wrappers in controller_window.cpp/h rather than
      // GLFW's iconify/maximize/restore directly - on Windows those
      // wrappers also re-hide the real GLFW window afterward, since
      // GLFW's own Win32 implementation of these transitions appears to
      // un-hide it as a side effect (see minimizeControllerWindow()'s
      // comment in controller_window.h for the full explanation).
      if (ImGui::Button("Minimize")) {
        minimizeControllerWindow(*current_window);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Minimize this window.");
      ImGui::SameLine();
      if (ImGui::Button("Maximize")) {
        maximizeControllerWindow(*current_window);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximize this window.");
      ImGui::SameLine();
      if (ImGui::Button("Restore")) {
        restoreControllerWindow(*current_window);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore from minimized/maximized back to its "
                          "normal size and position.");
      ImGui::NewLine();
#endif

      if (ImGui::BeginTable("WindowOptionsColumns", 2,
                            ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (ImGui::Checkbox("Always on Top", &current_window->always_on_top)) {
          glfwSetWindowAttrib(current_window->glfw_window, GLFW_FLOATING,
                              current_window->always_on_top);
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Keep window above all others.");

        ImGui::TableNextColumn();
#if !defined(_WIN32)
        // Borderless only ever touches GLFW_DECORATED on the GLFW
        // window. On Windows, every controller window is always shown
        // through the layered companion window instead (see
        // createTransparentOverlay() in controller_window.cpp) - an
        // undecorated WS_POPUP by construction (that's what makes
        // WS_EX_LAYERED legal on it at all) - so this setting would
        // have no visible effect there. Rather than show a permanently
        // greyed-out control, it's hidden entirely on that platform;
        // still fully functional here on Linux/macOS, where the GLFW
        // window itself is what's actually visible.
        {
          if (ImGui::Checkbox("Borderless", &current_window->borderless)) {
            glfwSetWindowAttrib(current_window->glfw_window, GLFW_DECORATED,
                                !current_window->borderless);
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide title bar and borders.");
        }
#endif

        ImGui::TableNextColumn();
        ImGui::Checkbox("Drag to Move", &current_window->drag_to_move);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Left‑click drag to move window.");

        ImGui::TableNextColumn();
        ImGui::Checkbox("Scroll to Resize", &current_window->scroll_to_resize);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Scroll mouse wheel to resize window.");

        ImGui::TableNextColumn();
        ImGui::Checkbox("Show Grid", &current_window->grid);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Show/hide reference grid.");

        ImGui::TableNextColumn();
        ImGui::Checkbox("Wireframe Mode", &current_window->wireframe);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Toggle wireframe rendering.");

        ImGui::TableNextColumn();
        // Transparent Background controls bg_color's alpha (the
        // clear-color alpha channel, so the desktop/game shows through).
        // On Linux/macOS that's the whole story - every window is
        // already created with GLFW_TRANSPARENT_FRAMEBUFFER = true (see
        // createControllerWindow()), so nothing else needs to change.
        //
        // On Windows, GLFW's own transparent-framebuffer support can't
        // be used at all: GLFW's window class has CS_OWNDC, and
        // Microsoft's documentation says WS_EX_LAYERED - the style real
        // per-pixel window transparency requires - "cannot be used if
        // the window has a class style of either CS_OWNDC or
        // CS_CLASSDC." That's a hard rule, not a driver bug, and it's
        // almost certainly why the one GLFW PR that tried to work around
        // it (glfw/glfw#2681) never got merged, and why the resulting
        // AMD-specific black-background report (glfw/glfw#2731) remains
        // open with no accepted fix. So on Windows every controller
        // window is always shown through a second, plain window with no
        // CS_OWNDC (see createTransparentOverlay() in
        // controller_window.cpp, created once at window-creation time,
        // not toggled here) - the GLFW window keeps rendering hidden
        // behind it. This checkbox only ever changes bg_color[3] (the
        // alpha that window's per-pixel transparency is built from) -
        // the companion window itself always exists on Windows
        // regardless of this setting, since it's also what made input
        // reliably keep working during Windows Fullscreen Optimizations
        // (see the comment in createControllerWindow()).
        if (ImGui::Checkbox("Transparent Background",
                            &current_window->transparent_bg)) {
          current_window->bg_color[3] =
              current_window->transparent_bg ? 0.0f : 1.0f;
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Make the window background fully transparent. The 3D model "
              "will appear on your desktop."
#if defined(_WIN32)
              "\nOn Windows the window is always shown through a "
              "borderless companion window regardless of this setting "
              "(Borderless isn't shown on this platform since it "
              "wouldn't do anything) - this only changes whether its "
              "background is see-through or solid."
#endif
          );
#if !defined(_WIN32)
        if (!current_window->transparency_supported) {
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
          ImGui::TextUnformatted("(!)");
          ImGui::PopStyleColor();
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Your graphics driver / display server did not grant a "
                "transparent framebuffer for this window, so the "
                "background will stay solid regardless of this setting. "
                "This is a driver/display-server limitation, not something "
                "this app's settings can override. Check the log for "
                "details.");
        }
#endif

        ImGui::TableNextColumn();
        {
          bool ct = current_window->click_through;
          if (ct) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.25f, 0.65f, 0.35f, 1.0f));
          }
          if (ImGui::Button(ct ? "Click-Through: ON" : "Click-Through: OFF")) {
            current_window->click_through = !current_window->click_through;
            // setWindowClickThrough() is the single source of truth for
            // click-through (see its definition in controller_window.cpp):
            // GLFW_MOUSE_PASSTHROUGH on the GLFW window on Linux/macOS,
            // or the layered companion window's WS_EX_TRANSPARENT on
            // Windows (that window is now always active there - see
            // createControllerWindow()). It does not force decoration or
            // floating - each setting only ever does the one thing its
            // name says.
            setWindowClickThrough(current_window->glfw_window,
                                  current_window->click_through);
          }
          if (ct) {
            ImGui::PopStyleColor(2);
          }
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Let mouse clicks pass through this window to whatever is "
              "behind it, turning it into a pure on-screen overlay.");

        ImGui::EndTable();
      }
      ImGui::NewLine();
      int w = 0, h = 0;
      glfwGetWindowSize(current_window->glfw_window, &w, &h);
      if (ImGui::DragInt("Width", &w, 1.0f, 10, vid_mode->width, "%d px")) {
        if (w < 10)
          w = 10;
        if (w > vid_mode->width)
          w = vid_mode->width;
        glfwSetWindowSize(current_window->glfw_window, w, h);
      }
      if (ImGui::IsItemHovered())
        DraggableTooltip("Window width in pixels.");

      if (ImGui::DragInt("Height", &h, 1.0f, 10, vid_mode->height, "%d px")) {
        if (h < 10)
          h = 10;
        if (h > vid_mode->height)
          h = vid_mode->height;
        glfwSetWindowSize(current_window->glfw_window, w, h);
      }
      if (ImGui::IsItemHovered())
        DraggableTooltip("Window height in pixels.");

      ImGui::NewLine();
      // With vsync permanently off for controller windows on every
      // platform (glfwSwapInterval(0) is unconditional now - a vsync
      // wait was found to be able to stall this window indefinitely on
      // some GPU/driver combinations, confirmed on NVIDIA, while a
      // fullscreen or borderless game runs behind it), Frame Cap is the
      // actual lever for CPU/GPU usage. Previously there was no UI for
      // it at all, so it silently stayed at its 60 default no matter
      // what. Lower this if the model doesn't need to visually update
      // quickly (e.g. a slowly-changing gyro readout) to directly cut
      // render cost.
      int fc = current_window->frame_cap;
      if (ImGui::SliderInt("Frame Cap", &fc, 5, 144, "%d FPS")) {
        current_window->frame_cap = (Uint8)std::clamp(fc, 5, 144);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Caps how often this window redraws, via a plain sleep - "
            "vsync is never used for controller windows on any platform "
            "(a vsync wait was found to be able to stall this window "
            "indefinitely on some GPU/driver combinations while a "
            "fullscreen or borderless game runs behind it). This is the "
            "main lever for this window's CPU/GPU usage. Lower values "
            "reduce resource use at the cost of less smooth movement in "
            "the model.");

#if defined(_WIN32)
      // Separate from Frame Cap above: on Windows, every controller
      // window is actually presented through the layered companion
      // window (see createTransparentOverlay() in controller_window.cpp)
      // via a PBO readback + GDI blit, which has its own cost on top of
      // the 3D render itself. Decoupling these two rates lets the model
      // render smoothly while pushing it to the visible window less
      // often, trading visual smoothness for a further cut in CPU/GDI
      // usage without touching the render rate at all.
      int overlay_hz =
          current_window->overlay_update_interval > 0.0
              ? (int)std::lround(1.0 / current_window->overlay_update_interval)
              : 60;
      if (ImGui::SliderInt("Overlay Update Rate", &overlay_hz, 5, 60,
                           "%d Hz")) {
        overlay_hz = std::clamp(overlay_hz, 5, 60);
        current_window->overlay_update_interval = 1.0 / (double)overlay_hz;
      }
      if (ImGui::IsItemHovered())
        DraggableTooltip(
            "Windows only: how often the rendered model is actually "
            "pushed to the visible overlay window, independent of Frame "
            "Cap above. Lower values reduce CPU/GDI usage further, at "
            "the cost of the visible overlay updating less smoothly "
            "than the model is actually rendering internally.");
#endif

      ImGui::NewLine();
      if (ImGui::Button("Open Data Directory")) {
        OsOpenInShell(config_base_path.c_str());
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Open the folder where settings, models, and logs are stored.");

      ImGui::SameLine();
      if (ImGui::Button(isLogWindowOpen() ? "Hide Log Window"
                                          : "Open Log Window")) {
        toggleLogWindow();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Show a live view of the application log in its own window.\n"
            "Useful on macOS/Linux, where no console is attached unless "
            "you launch the app from a terminal.");

      ImGui::NewLine();
      ImGui::ColorEdit4("Background Color", current_window->bg_color);
      if (ImGui::IsItemHovered())
        DraggableTooltip("Background colour and opacity.");
    }

    // ============================================================
    // CAMERA
    // ============================================================
    if (ImGui::CollapsingHeader("Camera")) {
      ImGui::DragFloat("Distance", &current_window->camera_distance, 0.1f, 1,
                       10);
      if (ImGui::IsItemHovered())
        DraggableTooltip("Camera distance from the model.");
      draggableFloatAngle("Yaw", &current_window->camera_yaw, false, 0.5f, -360,
                          360);
      DraggableTooltip("Horizontal camera orbit.");
      draggableFloatAngle("Pitch", &current_window->camera_pitch, false, 0.5f,
                          -180, 180);
      DraggableTooltip("Vertical camera orbit.");
      draggableFloatAngle("Roll", &current_window->camera_roll, false, 0.5f,
                          -180, 180);
      DraggableTooltip("Camera roll (tilt).");
      ImGui::DragFloat("Pan X", &current_window->camera_offset_x, 0.01f, -5.0f,
                       5.0f, "%.2f");
      if (ImGui::IsItemHovered())
        DraggableTooltip("Translate camera horizontally.");
      ImGui::DragFloat("Pan Y", &current_window->camera_offset_y, 0.01f, -5.0f,
                       5.0f, "%.2f");
      if (ImGui::IsItemHovered())
        DraggableTooltip("Translate camera vertically.");
      if (ImGui::Button("Reset")) {
        current_window->camera_distance = 3.3f;
        current_window->camera_yaw = 0.0f;
        current_window->camera_pitch = 89.999f;
        current_window->camera_roll = 0.0f;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset camera to default view.");
    }

    // ============================================================
    // CONTROLLER
    // ============================================================
    if (ImGui::CollapsingHeader("Controller")) {
      // Get list of joysticks
      int num_joy = 0;
      SDL_JoystickID *joy_ids = SDL_GetJoysticks(&num_joy);
      std::vector<SDL_JoystickID> device_ids;
      if (joy_ids) {
        device_ids.assign(joy_ids, joy_ids + num_joy);
        SDL_free(joy_ids);
      }

      std::string device_name = "None";
      if (current_window->is_gamecontroller && current_window->sdl_controller) {
        device_name = SDL_GetGamepadName(current_window->sdl_controller);
      } else if (current_window->sdl_joystick) {
        device_name = SDL_GetJoystickName(current_window->sdl_joystick);
      }

      if (receiverMode)
        ImGui::BeginDisabled();
      if (ImGui::BeginCombo("Controllers", device_name.c_str(), 0)) {
        // ---- "None" option ----
        if (ImGui::Selectable("None", device_name == "None")) {
          // Close any currently open device
          if (current_window->sdl_controller) {
            SDL_CloseGamepad(current_window->sdl_controller);
            current_window->sdl_controller = nullptr;
          }
          if (current_window->sdl_joystick) {
            SDL_CloseJoystick(current_window->sdl_joystick);
            current_window->sdl_joystick = nullptr;
          }
          current_window->is_gamecontroller = false;
          current_window->joystick_index = -1;
          // Reset gyro
          current_window->gyro_enabled = false;
          current_window->gyro_matrix = glm::mat4(1.0f);
          // Reset preferred GUID/name to empty (user manually chose None)
          current_window->preferred_guid = "";
          current_window->preferred_name = "";
          current_window->preferred_index = -1;
          current_window->preferred_serial = "";
          current_window->preferred_path = "";
        }

        // ---- Existing device loop ----
        for (int idx = 0; idx < (int)device_ids.size(); ++idx) {
          SDL_JoystickID id = device_ids[idx];
          const char *name = SDL_GetJoystickNameForID(id);
          bool is_game = SDL_IsGamepad(id);
          std::string label = std::string(name ? name : "Unknown") +
                              (is_game ? " (gamepad)" : " (joystick)") + " [" +
                              std::to_string(idx) + "]";
          if (ImGui::Selectable(label.c_str())) {
            // ---- Close any currently open device ----
            if (current_window->sdl_controller) {
              SDL_CloseGamepad(current_window->sdl_controller);
              current_window->sdl_controller = nullptr;
            }
            if (current_window->sdl_joystick) {
              SDL_CloseJoystick(current_window->sdl_joystick);
              current_window->sdl_joystick = nullptr;
            }
            current_window->is_gamecontroller = false;

            // ---- Open the new device ----
            if (is_game) {
              current_window->sdl_controller = SDL_OpenGamepad(id);
              if (current_window->sdl_controller) {
                current_window->is_gamecontroller = true;
                spdlog::info(
                    "Switched to gamecontroller: {}",
                    SDL_GetGamepadName(current_window->sdl_controller));

                // ---- Re‑enable gyro if it was previously enabled ----
                if (current_window->gyro_enabled) {
                  if (SDL_GamepadHasSensor(current_window->sdl_controller,
                                           SDL_SENSOR_GYRO)) {
                    SDL_SetGamepadSensorEnabled(current_window->sdl_controller,
                                                SDL_SENSOR_GYRO, true);
                    spdlog::info("Gyro re‑enabled on new controller.");
                  } else {
                    spdlog::warn(
                        "New controller does not support gyro, disabling.");
                    current_window->gyro_enabled = false;
                  }
                }
              } else {
                spdlog::error("Failed to open gamecontroller ID {}: {}", id,
                              SDL_GetError());
              }
            } else {
              current_window->sdl_joystick = SDL_OpenJoystick(id);
              if (current_window->sdl_joystick) {
                current_window->is_gamecontroller = false;
                spdlog::info("Switched to generic joystick: {}",
                             SDL_GetJoystickName(current_window->sdl_joystick));
              } else {
                spdlog::error("Failed to open generic joystick ID {}: {}", id,
                              SDL_GetError() ? SDL_GetError()
                                             : "(no error details)");
              }
            }
            current_window->joystick_index = idx;

            // Store preferred GUID and name for this window
            SDL_GUID dev_guid = SDL_GetJoystickGUIDForID(id);
            char guid_str[64];
            SDL_GUIDToString(dev_guid, guid_str, sizeof(guid_str));
            current_window->preferred_guid = guid_str;
            current_window->preferred_name = name ? name : "";

            // Compute ordinal among devices with same GUID
            current_window->preferred_guid_index = 0;
            for (int k = 0; k < idx; ++k) {
              SDL_JoystickID other_id = device_ids[k];
              SDL_GUID other_guid = SDL_GetJoystickGUIDForID(other_id);
              char other_guid_str[64];
              SDL_GUIDToString(other_guid, other_guid_str,
                               sizeof(other_guid_str));
              if (strcmp(guid_str, other_guid_str) == 0)
                current_window->preferred_guid_index++;
            }

            // Store index, serial and path for reliable re‑detection
            current_window->preferred_index = idx;
            if (current_window->sdl_controller) {
              SDL_Joystick *joy =
                  SDL_GetGamepadJoystick(current_window->sdl_controller);
              const char *serial = SDL_GetJoystickSerial(joy);
              const char *path = SDL_GetJoystickPath(joy);
              current_window->preferred_serial = serial ? serial : "";
              current_window->preferred_path = path ? path : "";
            } else if (current_window->sdl_joystick) {
              const char *serial =
                  SDL_GetJoystickSerial(current_window->sdl_joystick);
              const char *path =
                  SDL_GetJoystickPath(current_window->sdl_joystick);
              current_window->preferred_serial = serial ? serial : "";
              current_window->preferred_path = path ? path : "";
            }

            // ---- Reset all input state ----
            // Reset touchpad states
            for (int t = 0; t < 4; ++t) {
              for (int f = 0; f < 2; ++f) {
                current_window->touchpad_data[t][f].down = false;
                current_window->touchpad_data[t][f].x = 0.0f;
                current_window->touchpad_data[t][f].y = 0.0f;
              }
            }
            // Reset axis and hat history
            for (int i = 0; i < 32; ++i)
              current_window->last_axis_values[i] = 0.0f;
            for (int i = 0; i < 16; ++i)
              current_window->last_hat_values[i] = SDL_HAT_CENTERED;
            // Reset button states
            for (int i = 0; i < 128; ++i)
              current_window->last_joy_button_values[i] = false;
            for (int i = 0; i < 64; ++i)
              current_window->last_button_values[i] = false;

            // ---- Reset gyro completely ----
            current_window->gyro_matrix = glm::mat4(1.0f);
            current_window->gyro_data[0] = 0.0f;
            current_window->gyro_data[1] = 0.0f;
            current_window->gyro_data[2] = 0.0f;
            current_window->gyro_time = 0;
            current_window->gyro_toggled =
                true; // force first‑frame timestamp read
            current_window->lastTime = glfwGetTime();
            // Force gyro to re‑initialise timestamp on next frame
          }
        }
        ImGui::EndCombo();
      }
      if (receiverMode)
        ImGui::EndDisabled();

      if (ImGui::TreeNode("Settings")) {
        // Apply a dark purple background for the tree node
        BeginShadedGroup();
        ImGui::Checkbox("Popup Bumpers", &current_window->model.popup_bumpers);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Animate bumpers when pressed.");
        ImGui::SameLine();
        ImGui::Checkbox("Popup Triggers",
                        &current_window->model.popup_triggers);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Animate triggers when pressed.");
        ImGui::SameLine();
        ImGui::Checkbox("Popup Paddles", &current_window->model.popup_paddles);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Animate paddles when pressed.");
        ImGui::NewLine();
        if (current_window->model.meshes.size() > 7) {
          ImGui::SliderInt(
              "L-Stick Highlight Deadzone",
              &current_window->model.meshes[7].ring_highlight_deadzone, 0, 100);
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Deadzone for left stick highlight ring.");
        if (current_window->model.meshes.size() > 8) {
          ImGui::SliderInt(
              "R-Stick Highlight Deadzone",
              &current_window->model.meshes[8].ring_highlight_deadzone, 0, 100);
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Deadzone for right stick highlight ring.");
        ImGui::ColorEdit4("Highlight Color (Global)",
                          current_window->highlight_color);
        if (ImGui::IsItemHovered())
          DraggableTooltip("Default highlight color for all meshes. Can be "
                           "overridden per mesh.");
        EndShadedGroup(ShadeColor(0.42f, 0.28f, 0.62f),
                       ShadeBorder(0.42f, 0.28f, 0.62f));

        // Inside the Controller CollapsingHeader, after highlight color:
        ImGui::NewLine();
        ImGui::Text("Global Shader Effect");
        std::vector<std::string> shaderNames = GetShaderNames();
        int currentGlobalShaderIdx = 0;
        if (!current_window->global_shader_name.empty()) {
          for (int i = 1; i < (int)shaderNames.size(); ++i) {
            if (shaderNames[i] == current_window->global_shader_name) {
              currentGlobalShaderIdx = i;
              break;
            }
          }
        }
        std::vector<const char *> shaderNamesCStr;
        for (auto &s : shaderNames)
          shaderNamesCStr.push_back(s.c_str());
        if (ImGui::Combo("Global Shader", &currentGlobalShaderIdx,
                         shaderNamesCStr.data(), (int)shaderNamesCStr.size())) {
          if (currentGlobalShaderIdx == 0)
            current_window->global_shader_name = "";
          else
            current_window->global_shader_name =
                shaderNames[currentGlobalShaderIdx];
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Applies to all meshes unless a mesh has its own "
                            "shader override.");

        // ---- Logging toggle ----
        ImGui::NewLine();
        ImGui::Checkbox("Log Controller/Joystick", &g_log_controller);
        ImGui::SameLine();
        ImGui::Checkbox("Log Keyboard", &g_log_keyboard);
        ImGui::SameLine();
        ImGui::Checkbox("Log Mouse", &g_log_mouse);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Toggle logging for each device type.");
        ImGui::NewLine();

        // ---- Input responsiveness / idle CPU trade-off ----
        // Global (app-wide), not per-window, same as the logging toggles
        // above - see setPollIntervalMs()'s declaration in
        // keyboard_input.h for the full explanation. Windows only: on
        // Linux/macOS the global keyboard/mouse backends are already
        // blocking/event-driven with no equivalent poll loop, so this
        // would have nothing to affect there - hidden entirely rather
        // than shown as a control that silently does nothing.
#if defined(_WIN32)
        static int poll_ms = GlobalKeyboard::getPollIntervalMs();
        if (ImGui::SliderInt("Input Responsiveness", &poll_ms, 1, 16,
                             "%d ms")) {
          GlobalKeyboard::setPollIntervalMs(poll_ms);
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "How often the background keyboard/mouse listener wakes up "
              "to check for input, even when nothing is being pressed. "
              "Lower values (toward 1ms) notice a key/click sooner after "
              "it happens, but cost a small amount of CPU every single "
              "wake-up - at 1ms that's up to 1000 wake-ups per second, "
              "all the time, even sitting completely idle. Higher values "
              "cut that idle CPU cost, at the expense of adding up to "
              "that same amount of extra delay before a real input is "
              "noticed. Takes effect immediately.");
#endif

        ImGui::TreePop();

      } // end Controller
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Select a gamepad or joystick.");

    // ============================================================
    // MODEL
    // ============================================================

    static int mesh_to_delete = -1; // for per‑row delete confirmation

    if (ImGui::CollapsingHeader("Model")) {
      ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Model Selection");
      ImGui::Separator();

      // ---- Clamp all mesh indices to valid range ----
      size_t meshCount = current_window->model.meshes.size();
      if (meshCount == 0) {
        selected_mesh = -1;
        material_mesh = 0;
        texture_mesh = 0;
      } else {
        if (selected_mesh >= 0 && selected_mesh >= (int)meshCount)
          selected_mesh = (int)meshCount - 1;
        if (material_mesh >= meshCount)
          material_mesh = meshCount - 1;
        if (texture_mesh >= meshCount)
          texture_mesh = meshCount - 1;
      }

      // ---- Model selection combo ----
      if (ImGui::BeginCombo("Models", current_window->model_name.c_str(), 0)) {
        std::string models_root = get_models_root();
        std::string dir_path = models_root;
        dir_path.append("/");

        if (std::filesystem::exists(dir_path) &&
            std::filesystem::is_directory(dir_path)) {
          struct stat sb;
          for (const auto &entry :
               std::filesystem::directory_iterator(dir_path)) {
            std::string model_dir = entry.path().string();
            std::string delimiter = "/";
            if (stat(model_dir.c_str(), &sb) == 0 && (sb.st_mode & S_IFDIR)) {
              size_t pos = 0;
              while ((pos = model_dir.find(delimiter)) != std::string::npos) {
                model_dir.erase(0, pos + delimiter.length());
              }
              if (ImGui::Selectable(model_dir.c_str())) {
                current_window->model_name = model_dir;
                std::string model_path = models_root;
                model_path.append("/");
                model_path.append(model_dir);
                glfwMakeContextCurrent(current_window->glfw_window);
                loadModel(current_window->model, model_path);
                // Update mesh_count
                glfwMakeContextCurrent(glfw_settings_window);
              }
            }
          }
        } else {
          ImGui::TextDisabled("No models directory found at:\n%s",
                              dir_path.c_str());
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select a controller model.");

      // ---- Source URL ----
      char source[256];
      strncpy(source, current_window->model.source.c_str(), 255);
      if (ImGui::InputText("Source URL", source, 256)) {
        current_window->model.source = source;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Optional URL where the model was obtained.");

      if (ImGui::Button("Duplicate Model")) {
        ImGui::OpenPopup("duplicate");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create a copy of the current model with all its "
                          "meshes and settings.");

      if (ImGui::BeginPopup("duplicate")) {
        char name[32] = {};
        static bool name_valid = true;
        if (ImGui::InputText("New Model Name", name, IM_ARRAYSIZE(name),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
          bool valid = check_filename_valid(name);
          name_valid = valid;
          if (valid) {
            std::string src_path = current_window->model.path;
            std::string dest_path = get_models_root() + "/" + name;
            if (std::filesystem::exists(dest_path)) {
              spdlog::warn("Model folder '{}' already exists.", name);
            } else {
              try {
                // Copy entire model folder
                std::filesystem::copy(
                    src_path, dest_path,
                    std::filesystem::copy_options::recursive |
                        std::filesystem::copy_options::overwrite_existing);
                // Load the new model
                glfwMakeContextCurrent(current_window->glfw_window);
                loadModel(current_window->model, dest_path);
                glfwMakeContextCurrent(glfw_settings_window);
                current_window->model_name = name;
                current_window->model.path = dest_path;
                spdlog::info("Duplicated model to '{}'", dest_path);
              } catch (const std::exception &e) {
                spdlog::error("Failed to duplicate model: {}", e.what());
              }
            }
            ImGui::CloseCurrentPopup();
          } else {
            spdlog::warn("Model folder name '{}' rejected: contains an invalid "
                         "character.",
                         name);
          }
        }
        if (!name_valid) {
          ImGui::Text("Name cannot include characters \\/:*?\"<>|");
        }
        ImGui::EndPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Delete Model")) {
        ImGui::OpenPopup("delete");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Delete the current model folder.");

      if (ImGui::BeginPopup("delete")) {
        ImGui::Text("Delete this model?");
        if (ImGui::Button("Confirm")) {
          std::filesystem::remove_all(current_window->model.path);
          std::string dir_path = get_models_root();
          dir_path.append("/");
          std::vector<std::string> model_folders;
          struct stat sb;
          for (const auto &entry :
               std::filesystem::directory_iterator(dir_path)) {
            if (stat(entry.path().string().c_str(), &sb) == 0 &&
                (sb.st_mode & S_IFDIR)) {
              model_folders.push_back(entry.path().string());
            }
          }
          if (model_folders.size() > 0) {
            glfwMakeContextCurrent(current_window->glfw_window);
            loadModel(current_window->model, model_folders.front().c_str());
            glfwMakeContextCurrent(glfw_settings_window);
            current_window->model_name = get_top_folder(model_folders.front());
          } else {
            current_window->model_name = "";
          }
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Import New Model")) {
        import_model_dialog.Open();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a 3D model file and map its meshes.");

      ImGui::TextWrapped(
          "Import a 3D model (FBX, glTF, OBJ, etc.) and map its meshes "
          "to controller parts. After importing, a preview window will "
          "open where you can assign each mesh to a controller part.");

      // ============================================================
      //  MATERIALS & TEXTURES
      // ============================================================
      if (!current_window->model.meshes.empty()) {
        // ---- Materials ----
        if (ImGui::TreeNode("Materials")) {
          BeginShadedGroup();
          // Ensure material_mesh is valid
          if (material_mesh >= (int)current_window->model.meshes.size())
            material_mesh = (int)current_window->model.meshes.size() - 1;
          // Get the actual mesh name for preview
          std::string previewName =
              (material_mesh >= 0 &&
               material_mesh < (int)current_window->model.meshes.size())
                  ? current_window->model.meshes[material_mesh].name
                  : "";
          if (ImGui::BeginCombo("Meshes", previewName.c_str(), 0)) {
            for (int i = 0; i < (int)current_window->model.meshes.size(); ++i) {
              const std::string &displayName =
                  current_window->model.meshes[i].name.empty()
                      ? ("Mesh " + std::to_string(i))
                      : current_window->model.meshes[i].name;
              if (ImGui::Selectable(displayName.c_str(), material_mesh == i)) {
                material_mesh = i;
              }
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select a mesh to edit its material.");

          Mesh &matMesh = current_window->model.meshes[material_mesh];
          ImGui::NewLine();
          ImGui::DragFloat("Ambient", &matMesh.material.ambient, 0.01f, 0, 1);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Ambient light reflection.");

          ImGui::DragFloat("Diffuse", &matMesh.material.diffuse, 0.01f, 0, 1);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Diffuse light reflection.");
          ImGui::DragFloat("Specular", &matMesh.material.specular, 0.01f, 0, 1);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Specular (shininess) intensity.");
          ImGui::DragFloat("Shininess", &matMesh.material.shininess, 0.5f, 1,
                           256);
          if (ImGui::IsItemHovered())
            DraggableTooltip(
                "Specular exponent (higher = sharper highlights).");
          ImGui::ColorEdit3("Color", matMesh.material.color);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Base colour of the mesh.");
          matMesh.original_color[0] = matMesh.material.color[0];
          matMesh.original_color[1] = matMesh.material.color[1];
          matMesh.original_color[2] = matMesh.material.color[2];
          matMesh.original_alpha = matMesh.material.alpha;
          EndShadedGroup(ShadeColor(0.55f, 0.18f, 0.45f),
                         ShadeBorder(0.55f, 0.18f, 0.45f));
          ImGui::TreePop();
        }

        // ---- Textures ----
        if (ImGui::TreeNode("Textures")) {
          BeginShadedGroup();
          if (texture_mesh >= (int)current_window->model.meshes.size())
            texture_mesh = (int)current_window->model.meshes.size() - 1;
          std::string previewName =
              (texture_mesh >= 0 &&
               texture_mesh < (int)current_window->model.meshes.size())
                  ? current_window->model.meshes[texture_mesh].name
                  : "";
          if (ImGui::BeginCombo("Meshes", previewName.c_str(), 0)) {
            for (int i = 0; i < (int)current_window->model.meshes.size(); ++i) {
              const std::string &displayName =
                  current_window->model.meshes[i].name.empty()
                      ? ("Mesh " + std::to_string(i))
                      : current_window->model.meshes[i].name;
              if (ImGui::Selectable(displayName.c_str(), texture_mesh == i)) {
                texture_mesh = i;
              }
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select a mesh to manage its textures.");

          Mesh &texMesh = current_window->model.meshes[texture_mesh];
          ImGui::NewLine();
          static size_t current_texture = 0;
          if (ImGui::BeginListBox("Textures")) {
            for (size_t n = 0; n < texMesh.textures.size(); n++) {
              const bool is_selected = (current_texture == n);
              if (ImGui::Selectable(texMesh.textures[n].name.c_str(),
                                    is_selected)) {
                current_texture = n;
              }
            }
            ImGui::EndListBox();
          }
          if (texMesh.textures.size() < 16) {
            if (ImGui::Button("New Texture")) {
              texture_dialog.Open();
              current_texture = texMesh.textures.size();
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Add a new texture to the selected mesh.");
          }
          if (!texMesh.textures.empty()) {
            if (texMesh.textures.size() < 16) {
              ImGui::SameLine();
            }
            if (ImGui::Button("Delete Texture")) {
              glfwMakeContextCurrent(current_window->glfw_window);
              deleteTexture(texMesh.textures[current_texture].id);
              texMesh.textures.erase(texMesh.textures.begin() +
                                     current_texture);
              glfwMakeContextCurrent(glfw_settings_window);
              current_texture = 0;
              for (size_t i = 0; i < texMesh.textures.size(); i++) {
                texMesh.textures[i].name =
                    std::to_string(i + 1) + ": " + texMesh.textures[i].path;
              }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Remove the selected texture.");
            ImGui::SameLine();
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
              if (current_texture > 0) {
                Texture temp = texMesh.textures[current_texture - 1];
                texMesh.textures[current_texture - 1] =
                    texMesh.textures[current_texture];
                texMesh.textures[current_texture] = temp;
                current_texture--;
              }
              for (size_t i = 0; i < texMesh.textures.size(); i++) {
                texMesh.textures[i].name =
                    std::to_string(i + 1) + ": " + texMesh.textures[i].path;
              }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Move selected texture up.");
            ImGui::SameLine();
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
              if (current_texture < texMesh.textures.size() - 1) {
                Texture temp = texMesh.textures[current_texture + 1];
                texMesh.textures[current_texture + 1] =
                    texMesh.textures[current_texture];
                texMesh.textures[current_texture] = temp;
                current_texture++;
              }
              for (size_t i = 0; i < texMesh.textures.size(); i++) {
                texMesh.textures[i].name =
                    std::to_string(i + 1) + ": " + texMesh.textures[i].path;
              }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Move selected texture down.");

            Texture *t = &texMesh.textures[current_texture];
            ImGui::NewLine();
            enum Type { diffuse, specular, emission, type_count };
            const char *type_names[type_count] = {"Diffuse", "Specular",
                                                  "Emissive"};
            const char *type_name = (t->type >= 0 && t->type < type_count)
                                        ? type_names[t->type]
                                        : "Unknown";
            ImGui::DragInt("Type", &t->type, 1, 0, type_count - 1, type_name);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Texture type: diffuse, specular, or emissive.");
            enum Wrap {
              repeat,
              mirror_repeat,
              clamp_edge,
              clamp_border,
              wrap_count
            };
            const char *wrap_names[wrap_count] = {"Repeat", "Mirrored Repeat",
                                                  "Clamp to Edge",
                                                  "Clamp to Border"};
            const char *wrap_name_x = (t->wrapX >= 0 && t->wrapX < wrap_count)
                                          ? wrap_names[t->wrapX]
                                          : "Unknown";
            if (ImGui::DragInt("X Wrap", &t->wrapX, 1, 0, wrap_count - 1,
                               wrap_name_x)) {
              glfwMakeContextCurrent(current_window->glfw_window);
              glBindTexture(GL_TEXTURE_2D, t->id);
              switch (t->wrapX) {
              case repeat:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                break;
              case mirror_repeat:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                GL_MIRRORED_REPEAT);
                break;
              case clamp_edge:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                GL_CLAMP_TO_EDGE);
                break;
              case clamp_border:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                GL_CLAMP_TO_BORDER);
                glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                                 t->border);
                break;
              }
              glfwMakeContextCurrent(glfw_settings_window);
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Horizontal texture wrapping mode.");
            const char *wrap_name_y = (t->wrapY >= 0 && t->wrapY < wrap_count)
                                          ? wrap_names[t->wrapY]
                                          : "Unknown";
            if (ImGui::DragInt("Y Wrap", &t->wrapY, 1, 0, wrap_count - 1,
                               wrap_name_y)) {
              glfwMakeContextCurrent(current_window->glfw_window);
              glBindTexture(GL_TEXTURE_2D, t->id);
              switch (t->wrapY) {
              case repeat:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                break;
              case mirror_repeat:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                GL_MIRRORED_REPEAT);
                break;
              case clamp_edge:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                GL_CLAMP_TO_EDGE);
                break;
              case clamp_border:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                GL_CLAMP_TO_BORDER);
                glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                                 t->border);
                break;
              }
              glfwMakeContextCurrent(glfw_settings_window);
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Vertical texture wrapping mode.");
            if (ImGui::ColorEdit3("Border Color", t->border)) {
              glfwMakeContextCurrent(current_window->glfw_window);
              glBindTexture(GL_TEXTURE_2D, t->id);
              glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                               t->border);
              glfwMakeContextCurrent(glfw_settings_window);
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Border color used when clamp‑to‑border is selected.");
            ImGui::InputFloat("Offset X", &t->offsetX, 0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Horizontal texture offset.");
            ImGui::InputFloat("Offset Y", &t->offsetY, 0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Vertical texture offset.");
            ImGui::InputFloat("Scale X", &t->scaleX, 0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Horizontal texture scale.");
            ImGui::InputFloat("Scale Y", &t->scaleY, 0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Vertical texture scale.");
            draggableFloatAngle("Rotation", &t->rotation, /*is_radians=*/true,
                                0.1f, -180.0f, 180.0f);
            DraggableTooltip("Texture rotation angle.");
          }
          EndShadedGroup(ShadeColor(0.22f, 0.38f, 0.58f),
                         ShadeBorder(0.22f, 0.38f, 0.58f));
          ImGui::TreePop();
        }
      } else {
        ImGui::TextDisabled("No meshes in this model – Materials and Textures "
                            "are not available.");
      }
      // ============================================================
      //  MESH LIST TABLE
      // ============================================================
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.0f, 1.0f), "Mesh List & Editing");
      ImGui::Separator();
      ImGui::NewLine();
      ImGui::Separator();

      ImGui::Text("Mesh List (%zu meshes)",
                  current_window->model.meshes.size());
      ImGui::SameLine();
      if (ImGui::Button("Save Model")) {
        writeJson(current_window->model,
                  current_window->model.path + "/info.json");
        spdlog::info("Model saved to {}", current_window->model.path);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save current model settings to info.json.");

      ImGui::SameLine();
      if (ImGui::Button("Export Mapping")) {
        std::string message;
        bool ok = exportGamepadMapping(*current_window, message);
        export_mapping_result = message;
        export_mapping_ok = ok;
        export_mapping_popup_until = glfwGetTime() + 6.0;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Appends this controller's manually-configured bindings to "
            "gamecontrollerdb.txt as a standard SDL mapping line, so you "
            "can share that file (or just this one line) with others - "
            "or so this same device gets recognized as a proper Gamepad "
            "next time.\nOnly works for bindings using Input Type = "
            "Joystick (a raw, unrecognized device) - if this device is "
            "already a recognized Gamepad, there's nothing to export.");
      if (glfwGetTime() < export_mapping_popup_until) {
        ImGui::PushStyleColor(
            ImGuiCol_Text, export_mapping_ok ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                             : ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", export_mapping_result.c_str());
        ImGui::PopStyleColor();
      }

      // --- Check-all buttons ---
      if (ImGui::Button("Toggle All Visible")) {
        for (auto &mesh : current_window->model.meshes) {
          if (mesh.elements > 0)
            mesh.visible = !mesh.visible;
        }
      }
      ImGui::SameLine();
      static int set_all_type = 0;
      const char *type_names[] = {"Gamepad", "Joystick", "Keyboard", "Mouse"};
      const char *binding_prefixes[] = {"gamepad", "joystick", "keyboard",
                                        "mouse"};
      if (ImGui::Combo("Set All Type", &set_all_type, type_names,
                       IM_ARRAYSIZE(type_names))) {
        for (auto &mesh : current_window->model.meshes) {
          if (mesh.elements > 0) {
            mesh.inputType = set_all_type;
            // Update the binding prefix so the table shows the new type.
            // The value part is cleared so the user can pick a specific input.
            mesh.inputBinding =
                std::string(binding_prefixes[set_all_type]) + ":";
          }
        }
      }
      ImGui::NewLine();

      ImGui::NewLine();
      ImGui::DragFloat("Mouse Sensitivity", &current_window->mouse_sensitivity,
                       0.01f, 0.01f, 2.0f, "%.2f");

      if (ImGui::IsItemHovered())
        DraggableTooltip(
            "Scale factor for mouse movement -> touchpoint displacement. "
            "Lower = slower, higher = faster. Default (0.5) gives a moderate "
            "speed.");
      ImGui::NewLine();
      if (ImGui::BeginTable("MeshTable", 7,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Parent", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Invert", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)current_window->model.meshes.size(); ++i) {
          Mesh &mesh = current_window->model.meshes[i];
          bool hasMesh = (mesh.elements > 0);
          ImGui::TableNextRow();
          // Highlight selected row if any
          if (selected_mesh == i) {
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(ImVec4(0.35f, 0.15f, 0.45f, 1.0f)));
          }

          // Column 0: #
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%d", i);

          // Column 1: Name
          ImGui::TableSetColumnIndex(1);
          if (hasMesh) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.3f, 0.1f, 0.4f, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(0.2f, 0.05f, 0.3f, 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,
                                ImVec2(0.0f, 0.5f));
            // Toggle selection: if clicked and already selected, deselect; else
            // select
            bool isSelected = (selected_mesh == i);
            if (ImGui::Button(mesh.name.c_str(), ImVec2(-1, 0))) {
              if (isSelected) {
                selected_mesh = -1; // deselect
              } else {
                selected_mesh = i;
              }
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
          } else {
            ImGui::TextDisabled("%s (empty)", mesh.name.c_str());
          }

          // Column 2: Input (type + binding)
          ImGui::TableSetColumnIndex(2);
          if (hasMesh) {
            ImGui::PushID(i + 5000);

            // ---- Helper: friendly label for input binding ----
            auto getFriendlyInputLabel =
                [](const std::string &type,
                   const std::string &raw) -> std::string {
              if ((type == "gamepad" || type == "joystick") &&
                  raw == "leftstick")
                return "Left Stick (X/Y)";
              if ((type == "gamepad" || type == "joystick") &&
                  raw == "rightstick")
                return "Right Stick (X/Y)";
              if (type == "gamepad") {
                if (raw[0] == 'b') {
                  int num = 0;
                  try {
                    num = std::stoi(raw.substr(1));
                  } catch (...) {
                    return raw;
                  }
                  static const char *btnNames[] = {"A",
                                                   "B",
                                                   "X",
                                                   "Y",
                                                   "Back",
                                                   "Guide",
                                                   "Start",
                                                   "Left Stick",
                                                   "Right Stick",
                                                   "Left Bumper",
                                                   "Right Bumper",
                                                   "D-Pad Up",
                                                   "D-Pad Down",
                                                   "D-Pad Left",
                                                   "D-Pad Right",
                                                   "Misc",
                                                   "Paddle1",
                                                   "Paddle2",
                                                   "Paddle3",
                                                   "Paddle4",
                                                   "Touchpad"};
                  if (num >= 0 && num < 21)
                    return raw + " (" + btnNames[num] + ")";
                } else if (raw[0] == 'a') {
                  int num = 0;
                  bool isDir = false;
                  int dir = 0;
                  if (raw.back() == '+' || raw.back() == '-') {
                    isDir = true;
                    dir = (raw.back() == '+') ? 1 : -1;
                    try {
                      num = std::stoi(raw.substr(1, raw.size() - 2));
                    } catch (...) {
                      return raw;
                    }
                  } else {
                    try {
                      num = std::stoi(raw.substr(1));
                    } catch (...) {
                      return raw;
                    }
                  }
                  static const char *axisNames[] = {
                      "Left X",  "Left Y",       "Right X",
                      "Right Y", "Left Trigger", "Right Trigger"};
                  std::string label;
                  if (num >= 0 && num < 6) {
                    label = axisNames[num];
                  } else {
                    label = "Axis " + std::to_string(num);
                  }
                  if (isDir) {
                    std::string dirStr = (dir == 1) ? " +" : " -";
                    return raw + " (" + label + dirStr + ")";
                  } else {
                    // Plain analog axis – show as "Axis Name (analog)"
                    return raw + " (" + label + " analog)";
                  }
                }
                if (type == "gamepad") {
                  // ---- Touchpad handling ----
                  if (raw.rfind("touch", 0) == 0) {
                    std::string rest = raw.substr(5);
                    size_t underscore1 = rest.find('_');
                    if (underscore1 != std::string::npos) {
                      std::string touchStr = rest.substr(0, underscore1);
                      std::string rest2 = rest.substr(underscore1 + 1);
                      size_t underscore2 = rest2.find('_');
                      if (underscore2 == std::string::npos) {
                        // Combined: touchX_fY
                        std::string fingerStr = rest2;
                        return "Touchpad " + touchStr + ", Finger " +
                               fingerStr.substr(1) + " (X/Y)";
                      } else {
                        // Per-axis: touchX_fY_z
                        std::string fingerStr = rest2.substr(0, underscore2);
                        char axis = rest2.back();
                        return "Touchpad " + touchStr + ", Finger " +
                               fingerStr.substr(1) + " " +
                               (axis == 'x' ? "X" : "Y");
                      }
                    }
                  }

                  // ... existing button/axis handling (button, axis, etc.) ...
                }
              } else if (type == "joystick") {
                if (raw[0] == 'h') {
                  size_t dot = raw.find('.');
                  if (dot != std::string::npos) {
                    int hatIdx = 0, dir = 0;
                    try {
                      hatIdx = std::stoi(raw.substr(1, dot - 1));
                      dir = std::stoi(raw.substr(dot + 1));
                    } catch (...) {
                      return raw;
                    }
                    static const char *dirNames[] = {
                        "Up",   "Right-Up",  "Right", "Right-Down",
                        "Down", "Left-Down", "Left",  "Left-Up"};
                    if (dir >= 0 && dir < 8)
                      return raw + " (" + dirNames[dir] + ")";
                  }
                }
              } else if (type == "mouse") {
                if (raw == "mouse_xy")
                  return "Mouse XY (relative)";
                if (raw == "mouse_x")
                  return "Mouse X (relative)";
                if (raw == "mouse_y")
                  return "Mouse Y (relative)";
                if (raw == "mouse_scroll_xy")
                  return "Scroll XY (combined)";
                if (raw == "mouse_scroll_x")
                  return "Scroll X (horizontal)";
                if (raw == "mouse_scroll_y")
                  return "Scroll Y (vertical)";
                if (raw == "mouse_left")
                  return "Left Button";
                if (raw == "mouse_right")
                  return "Right Button";
                if (raw == "mouse_middle")
                  return "Middle Button";
                if (raw == "mouse_4")
                  return "Button 4";
                if (raw == "mouse_5")
                  return "Button 5";
                if (raw == "mouse_6")
                  return "Button 6";
                if (raw == "mouse_7")
                  return "Button 7";
                if (raw == "mouse_8")
                  return "Button 8";
                return raw;
              }
              return raw; // fallback
            };

            // Parse current binding
            std::string binding = mesh.inputBinding;
            std::string currentType = "gamepad";
            std::string currentValue = "";
            size_t colon = binding.find(':');
            if (colon != std::string::npos) {
              currentType = binding.substr(0, colon);
              currentValue = binding.substr(colon + 1);
            }

            // ---- Type drop-down ----
            const char *type_names[] = {"gamepad", "joystick", "keyboard",
                                        "mouse"};
            int type_idx = 0;
            for (int t = 0; t < 4; ++t) {
              if (currentType == type_names[t]) {
                type_idx = t;
                break;
              }
            }
            if (ImGui::BeginCombo("##type", type_names[type_idx])) {
              for (int t = 0; t < 4; ++t) {
                if (ImGui::Selectable(type_names[t], type_idx == t)) {
                  type_idx = t;
                  currentType = type_names[t];
                  currentValue = "";
                  mesh.inputBinding = currentType + ":";
                }
              }
              ImGui::EndCombo();
            }

            ImGui::SameLine();

            // ---- Specific input drop-down with friendly names ----
            std::vector<std::string> inputOptions;
            if (currentType == "gamepad" || currentType == "joystick") {
              // Special stick entries (full X/Y)
              inputOptions.push_back("leftstick");
              inputOptions.push_back("rightstick");
              for (int b = 0; b < 32; ++b)
                inputOptions.push_back("b" + std::to_string(b));
              for (int a = 0; a < 8; ++a) {
                inputOptions.push_back("a" + std::to_string(a)); // plain axis
                inputOptions.push_back("a" + std::to_string(a) + "+");
                inputOptions.push_back("a" + std::to_string(a) + "-");
              }
              for (int t = 0; t < 2; ++t) {
                for (int f = 0; f < 2; ++f) {
                  // Per-axis
                  inputOptions.push_back("touch" + std::to_string(t) + "_f" +
                                         std::to_string(f) + "_x");
                  inputOptions.push_back("touch" + std::to_string(t) + "_f" +
                                         std::to_string(f) + "_y");
                  // Combined (both axes)
                  inputOptions.push_back("touch" + std::to_string(t) + "_f" +
                                         std::to_string(f));
                }
              }
              if (currentType == "joystick") {
                for (int h = 0; h < 4; ++h) {
                  for (int d = 0; d < 8; ++d)
                    inputOptions.push_back("h" + std::to_string(h) + "." +
                                           std::to_string(d));
                }
              }
            } else if (currentType == "keyboard") {
              // Add all keyboard scancodes with a name. A handful of SDL2
              // scancodes share the same display name (e.g. RETURN and
              // RETURN2 are both "Return") - skip repeats so the dropdown
              // never shows two identical-looking entries where only one
              // actually does anything (see the emplace() note next to
              // keyMap in controller_window.cpp for the full story).
              std::set<std::string> seenKeyNames;
              for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
                SDL_Scancode sc = static_cast<SDL_Scancode>(i);
                const char *name = SDL_GetScancodeName(sc);
                if (name && strcmp(name, "UNKNOWN") != 0) {
                  std::string key = "key_";
                  for (const char *p = name; *p; ++p)
                    key.push_back(tolower(*p));
                  if (seenKeyNames.insert(key).second)
                    inputOptions.push_back(key);
                }
              }
            } else if (currentType == "mouse") {
              // Combined axes (like a stick)
              inputOptions.push_back("mouse_xy");
              // Individual axes (absolute position or delta? we'll treat as
              // delta)
              inputOptions.push_back("mouse_x");
              inputOptions.push_back("mouse_y");
              // Scroll
              inputOptions.push_back("mouse_scroll_xy");
              inputOptions.push_back("mouse_scroll_x");
              inputOptions.push_back("mouse_scroll_y");
              // Buttons
              inputOptions.push_back("mouse_left");
              inputOptions.push_back("mouse_right");
              inputOptions.push_back("mouse_middle");
              // Extra buttons (GLFW supports up to 8)
              inputOptions.push_back("mouse_4");
              inputOptions.push_back("mouse_5");
              inputOptions.push_back("mouse_6");
              inputOptions.push_back("mouse_7");
              inputOptions.push_back("mouse_8");
            }

            // Build friendly label for the selected binding
            std::string selectedDisplay =
                currentValue.empty()
                    ? "unbound"
                    : getFriendlyInputLabel(currentType, currentValue);

            bool comboActive =
                ImGui::BeginCombo("##input", selectedDisplay.c_str());
            if (comboActive) {
              // "Unbound" option
              if (ImGui::Selectable("Unbound", currentValue.empty())) {
                currentValue = "";
                mesh.inputBinding = "";
              }
              ImGui::Separator();

              for (const auto &opt : inputOptions) {
                bool isSelected = (opt == currentValue);
                std::string displayLabel =
                    getFriendlyInputLabel(currentType, opt);
                if (ImGui::Selectable(displayLabel.c_str(), isSelected)) {
                  currentValue = opt;
                  mesh.inputBinding = currentType + ":" + currentValue;
                }
              }
              ImGui::EndCombo();

              // ---- Start capture if this combo is now active ----
              if (!capture.active) {
                startCapture(current_window, i, type_idx);
              }
            } else {
              // ---- Combo is not active ----
              // If we were capturing for this mesh and the combo closed, clear
              // capture
              if (capture.active && capture.window == current_window &&
                  capture.mesh == i) {
                clearCapture();
              }
            }

            // ---- Handle capture polling while combo is active ----
            if (capture.active && capture.window == current_window &&
                capture.mesh == i) {
              std::string newBinding;
              if (pollAndCapture(current_window, i, type_idx, newBinding)) {
                // Set the binding
                currentValue = newBinding;
                mesh.inputBinding = currentType + ":" + currentValue;
                // Clear capture so we don't keep capturing
                clearCapture();
                // Optionally, log the auto-binding
                spdlog::info("Auto-bound mesh '{}' to input '{}'", mesh.name,
                             newBinding);
              }
            }

            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Choose the input that triggers this mesh.");

            ImGui::PopID();
          } else {
            ImGui::TextDisabled(" ");
          }

          // Column 3: Parent
          ImGui::TableSetColumnIndex(3);
          if (hasMesh) {
            std::vector<int> candidateIndices; // real mesh-vector indices
            std::vector<std::string> candidateLabels;
            candidateIndices.push_back(-1);
            candidateLabels.push_back("None");
            for (int k = 0; k < (int)current_window->model.meshes.size(); ++k) {
              if (k == i)
                continue;
              const Mesh &candidate = current_window->model.meshes[k];
              if (candidate.elements == 0)
                continue;
              std::string label = !candidate.name.empty()
                                      ? candidate.name
                                      : ("Mesh " + std::to_string(k));
              if (candidate.assignedPart >= 0 && candidate.assignedPart < 35)
                label += " (" + mesh_names[candidate.assignedPart] + ")";
              candidateIndices.push_back(k);
              candidateLabels.push_back(label);
            }

            int current_pos = 0;
            for (int pos = 0; pos < (int)candidateIndices.size(); ++pos) {
              if (candidateIndices[pos] == mesh.parentIndex) {
                current_pos = pos;
                break;
              }
            }

            std::vector<const char *> parent_names;
            for (auto &label : candidateLabels)
              parent_names.push_back(label.c_str());

            ImGui::PushID(i + 2000);
            if (ImGui::Combo("##parent", &current_pos, parent_names.data(),
                             (int)parent_names.size())) {
              int newParent = candidateIndices[current_pos];

              if (newParent != -1 &&
                  wouldCreateCycle(current_window->model, i, newParent)) {
                spdlog::warn("Cannot set parent: would create a cycle.");
              } else {
                glm::vec3 worldPos =
                    getModelWorldPositionWithoutGyro(current_window->model, i);

                if (newParent == -1) {
                  mesh.position[0] = worldPos.x;
                  mesh.position[1] = worldPos.y;
                  mesh.position[2] = worldPos.z;
                } else {
                  glm::mat4 parentMat = getModelMatrixWithoutGyro(
                      current_window->model, newParent);
                  glm::mat4 invParentMat = glm::inverse(parentMat);
                  glm::vec4 localPos = invParentMat * glm::vec4(worldPos, 1.0f);
                  mesh.position[0] = localPos.x;
                  mesh.position[1] = localPos.y;
                  mesh.position[2] = localPos.z;
                }

                mesh.parentIndex = newParent;
                writeJson(current_window->model,
                          current_window->model.path + "/info.json");
              }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Attach this mesh to a parent part (e.g., stick).");
            ImGui::PopID();
          } else {
            ImGui::TextDisabled("N/A");
          }

          // Column 4: Visible
          ImGui::TableSetColumnIndex(4);
          if (hasMesh) {
            ImGui::PushID(i + 3000);
            ImGui::Checkbox("##visible", &mesh.visible);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Show/hide this mesh in the 3D view.");
            ImGui::PopID();
          } else {
            ImGui::TextDisabled(" ");
          }

          // Column 5: Invert
          ImGui::TableSetColumnIndex(5);
          if (hasMesh) {
            ImGui::PushID(i + 4000);
            ImGui::Checkbox("##invert", &mesh.invert);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Invert the input (button press or axis direction).");
            ImGui::PopID();
          } else {
            ImGui::TextDisabled(" ");
          }

          // Column 6: Actions
          ImGui::TableSetColumnIndex(6);
          if (hasMesh) {
            if (ImGui::Button(("Import##" + std::to_string(i)).c_str())) {
              model_dialog.Open();
              selected_mesh = i;
            }
            ImGui::SameLine();
            if (ImGui::Button(("Del##" + std::to_string(i)).c_str())) {
              mesh_to_delete = i;
              show_delete_popup = true;
            }
          } else {
            ImGui::TextDisabled(" ");
          }
        }
        ImGui::EndTable();
      }

      // ---- Per‑row delete modal (triggered by flag) ----
      if (show_delete_popup) {
        ImGui::OpenPopup("Delete Mesh##confirm");
        // Keep the flag true so the popup stays open; we'll close it inside the
        // modal.
      }

      if (ImGui::BeginPopupModal("Delete Mesh##confirm", NULL,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete this mesh permanently?");
        ImGui::Text("This action cannot be undone.");
        ImGui::Separator();

        if (ImGui::Button("Yes, Delete", ImVec2(120, 0))) {
          if (mesh_to_delete >= 0 &&
              mesh_to_delete < (int)current_window->model.meshes.size()) {
            // Log which mesh is being deleted
            std::string meshName =
                current_window->model.meshes[mesh_to_delete].name;
            spdlog::info("Deleting mesh '{}' at index {}", meshName,
                         mesh_to_delete);

            // Remove the mesh from the vector
            current_window->model.meshes.erase(
                current_window->model.meshes.begin() + mesh_to_delete);

            // Update info.json immediately
            writeJson(current_window->model,
                      current_window->model.path + "/info.json");

            // Adjust selection if needed
            if (selected_mesh == mesh_to_delete) {
              selected_mesh = -1;
            } else if (selected_mesh > mesh_to_delete) {
              selected_mesh--;
            }

            spdlog::info("Mesh '{}' deleted and model saved.", meshName);
          } else {
            spdlog::warn("Invalid mesh index for deletion: {}", mesh_to_delete);
          }

          mesh_to_delete = -1;
          show_delete_popup = false; // close popup
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
          mesh_to_delete = -1;
          show_delete_popup = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      ImGui::Separator();

      // ---- Detailed controls for the selected mesh ----
      if (selected_mesh >= 0 &&
          selected_mesh < (int)current_window->model.meshes.size()) {
        Mesh &selectedMesh = current_window->model.meshes[selected_mesh];
        if (selectedMesh.elements == 0) {
          ImGui::TextDisabled("No mesh loaded at index %d.", selected_mesh);
        } else {

          // ---- Header with mesh name and Save button ----
          ImGui::Text("Editing: %s", selectedMesh.name.c_str());
          ImGui::SameLine();
          if (ImGui::Button("Save Model##Editing")) { // <-- unique ID
            if (current_window->model.path.empty()) {
              spdlog::error("Cannot save: model path is empty.");
            } else {
              writeJson(current_window->model,
                        current_window->model.path + "/info.json");
              spdlog::info("Model saved to {}", current_window->model.path);
            }
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Write current settings to info.json.");

          // ---- Position Section (collapsible) ----
          if (ImGui::TreeNode("Position")) {
            BeginShadedGroup();
            ImGui::InputFloat("X Position", &selectedMesh.position[0], 0.01f,
                              1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Move the mesh along the X axis.");
            ImGui::InputFloat("Y Position", &selectedMesh.position[1], 0.01f,
                              1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Move the mesh along the Y axis.");
            ImGui::InputFloat("Z Position", &selectedMesh.position[2], 0.01f,
                              1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Move the mesh along the Z axis.");
            EndShadedGroup(ShadeColor(0.20f, 0.40f, 0.62f),
                           ShadeBorder(0.20f, 0.40f, 0.62f));
            ImGui::TreePop();
          }

          // ---- Pivot Section (collapsible) ----
          if (ImGui::TreeNode("Pivot Point")) {
            BeginShadedGroup();
            ImGui::TextWrapped(
                "The pivot is the point around which the mesh rotates "
                "(for sticks, triggers, buttons). The orange circle shows its "
                "current position.");
            ImGui::InputFloat("Pivot X", &selectedMesh.pivot_offset[0], 0.01f,
                              1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Offset of the pivot from the mesh origin (X).");
            ImGui::InputFloat("Pivot Y", &selectedMesh.pivot_offset[1], 0.01f,
                              1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Offset of the pivot from the mesh origin (Y).");
            ImGui::InputFloat("Pivot Z", &selectedMesh.pivot_offset[2], 0.01f,
                              1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Offset of the pivot from the mesh origin (Z).");

            // ---- Auto-center buttons ----
            if (selectedMesh.hasBBox) {
              ImGui::Text("Auto‑set pivot:");
              if (ImGui::Button("Center of Mass")) {
                glm::vec3 center = computeMeshCenter(selectedMesh);
                spdlog::info("Center of Mass: ({:.3f}, {:.3f}, {:.3f})",
                             center.x, center.y, center.z);
                selectedMesh.pivot_offset[0] = center.x;
                selectedMesh.pivot_offset[1] = center.y;
                selectedMesh.pivot_offset[2] = center.z;
              }
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Sets the pivot to the geometric centre of the mesh.");
              ImGui::SameLine();
              if (selectedMesh.assignedPart == 5 ||
                  selectedMesh.assignedPart == 6) {
                if (ImGui::Button("Set Pivot to Stick Base")) {
                  float px =
                      (selectedMesh.bboxMin.x + selectedMesh.bboxMax.x) * 0.5f;
                  float py = selectedMesh.bboxMin.y;
                  float pz =
                      (selectedMesh.bboxMin.z + selectedMesh.bboxMax.z) * 0.5f;
                  selectedMesh.pivot_offset[0] = px;
                  selectedMesh.pivot_offset[1] = py;
                  selectedMesh.pivot_offset[2] = pz;
                }
                if (ImGui::IsItemHovered())
                  ImGui::SetTooltip(
                      "Sets the pivot to the bottom‑centre of the stick mesh. "
                      "This makes the stick rotate like a real joystick.");
              }
            } else {
              ImGui::TextDisabled(
                  "Bounding box not available – auto‑center disabled.");
            }
            EndShadedGroup(ShadeColor(0.58f, 0.38f, 0.10f),
                           ShadeBorder(0.58f, 0.38f, 0.10f));
            ImGui::TreePop();
          }

          // ---- Rotation Section (collapsible) ----
          if (ImGui::TreeNode("Rotation")) {
            BeginShadedGroup();
            ImGui::InputFloat("Rot X (deg)", &selectedMesh.rotation[0], 0.1f,
                              1.0f, "%.1f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Euler rotation around the X axis (degrees).");
            ImGui::InputFloat("Rot Y (deg)", &selectedMesh.rotation[1], 0.1f,
                              1.0f, "%.1f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Euler rotation around the Y axis (degrees).");
            ImGui::InputFloat("Rot Z (deg)", &selectedMesh.rotation[2], 0.1f,
                              1.0f, "%.1f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Euler rotation around the Z axis (degrees).");
            EndShadedGroup(ShadeColor(0.22f, 0.52f, 0.30f),
                           ShadeBorder(0.22f, 0.52f, 0.30f));
            ImGui::TreePop();
          }

          // ---- Movement & Animation (collapsible) ----
          if (ImGui::TreeNode("Movement & Animation")) {
            BeginShadedGroup();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f),
                               "Travel (button press offset)");
            ImGui::InputFloat("Travel X", &selectedMesh.travel[0], 0.01f, 1.0f,
                              "%.3f");
            ImGui::InputFloat("Travel Y", &selectedMesh.travel[1], 0.01f, 1.0f,
                              "%.3f");
            ImGui::InputFloat("Travel Z", &selectedMesh.travel[2], 0.01f, 1.0f,
                              "%.3f");
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f),
                               "Travel Rotation (deg)");
            ImGui::InputFloat("Rot X", &selectedMesh.travel_rotation[0], 0.1f,
                              1.0f, "%.1f");
            ImGui::InputFloat("Rot Y", &selectedMesh.travel_rotation[1], 0.1f,
                              1.0f, "%.1f");
            ImGui::InputFloat("Rot Z", &selectedMesh.travel_rotation[2], 0.1f,
                              1.0f, "%.1f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Movement when the button is pressed.");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f),
                               "Popup (bumper/paddle)");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Make this mesh pop out when its input is triggered.");
            ImGui::Checkbox("Is Bumper", &selectedMesh.isBumper);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("This mesh acts as a bumper – it will pop when "
                                "'Popup Bumpers' is enabled.");
            ImGui::SameLine();
            ImGui::Checkbox("Is Trigger", &selectedMesh.isTrigger);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("This mesh acts as a trigger – it will pop "
                                "when 'Popup Triggers' is enabled.");
            ImGui::SameLine();
            ImGui::Checkbox("Is Paddle", &selectedMesh.isPaddle);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("This mesh acts as a paddle – it will pop when "
                                "'Popup Paddles' is enabled.");
            ImGui::InputFloat("Popup Offset X", &selectedMesh.popup_offset[0],
                              0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Vertical offset when the part 'pops up'.");
            ImGui::InputFloat("Popup Offset Y", &selectedMesh.popup_offset[1],
                              0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Depth offset when the part 'pops up'.");
            ImGui::InputFloat("Popup Offset Z", &selectedMesh.popup_offset[2],
                              0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Horizontal offset when the part 'pops up'.");
            draggableFloatAngle("Popup Yaw", &selectedMesh.popup_rotation[1],
                                /*is_radians=*/true, 0.1f, -180.0f, 180.0f);
            DraggableTooltip("Yaw rotation when the part 'pops up' (e.g. a "
                             "bumper tilting outward).");
            draggableFloatAngle("Popup Pitch", &selectedMesh.popup_rotation[0],
                                /*is_radians=*/true, 0.1f, -180.0f, 180.0f);
            DraggableTooltip("Pitch rotation when the part 'pops up'.");
            draggableFloatAngle("Popup Roll", &selectedMesh.popup_rotation[2],
                                /*is_radians=*/true, 0.1f, -180.0f, 180.0f);
            DraggableTooltip("Roll rotation when the part 'pops up'.");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f),
                               "Stick / Trigger Limits");
            draggableFloatAngle("Stick Max Angle", &selectedMesh.stick_max,
                                /*is_radians=*/true, 0.1f, 0.0f, 45.0f);
            DraggableTooltip("Maximum tilt angle for analog sticks.");
            draggableFloatAngle("Trigger Max Angle", &selectedMesh.trigger_max,
                                /*is_radians=*/true, 0.1f, 0.0f, 90.0f);
            DraggableTooltip("Maximum pull angle for triggers.");
            EndShadedGroup(ShadeColor(0.16f, 0.48f, 0.52f),
                           ShadeBorder(0.16f, 0.48f, 0.52f));
            ImGui::TreePop();
          }

          // ---- Highlight Override (per-mesh) ----
          if (ImGui::TreeNode("Highlight Override")) {
            BeginShadedGroup();
            bool useCustom = selectedMesh.use_custom_highlight;
            if (ImGui::Checkbox("Override global highlight color",
                                &useCustom)) {
              selectedMesh.use_custom_highlight = useCustom;
              if (useCustom) {
                // Copy current global color as default
                selectedMesh.custom_highlight_color[0] =
                    current_window->highlight_color[0];
                selectedMesh.custom_highlight_color[1] =
                    current_window->highlight_color[1];
                selectedMesh.custom_highlight_color[2] =
                    current_window->highlight_color[2];
              }
            }
            if (selectedMesh.use_custom_highlight) {
              ImGui::ColorEdit4("Custom Highlight Color",
                                selectedMesh.custom_highlight_color);
              if (ImGui::IsItemHovered())
                DraggableTooltip("Custom Highlight Color");
            }

            // ---- Dual highlight for axes ----
            bool dual = selectedMesh.use_dual_highlight;
            if (ImGui::Checkbox("Dual Highlight (for axes)", &dual)) {
              selectedMesh.use_dual_highlight = dual;
            }
            if (selectedMesh.use_dual_highlight) {
              ImGui::ColorEdit4("Positive Color",
                                selectedMesh.highlight_color_positive);
              if (ImGui::IsItemHovered())
                DraggableTooltip("Positive Color");
              ImGui::ColorEdit4("Negative Color",
                                selectedMesh.highlight_color_negative);
              if (ImGui::IsItemHovered())
                DraggableTooltip("Negative Color");
              ImGui::DragFloat("Axis Deadzone", &selectedMesh.axis_deadzone,
                               0.001f, 0.0f, 0.5f, "%.3f");
              if (ImGui::IsItemHovered())
                DraggableTooltip(
                    "Deadzone to start showing the Custom Highlight Color");
              ImGui::TextWrapped("The highlight will be off when the axis "
                                 "value is within this deadzone. It ramps from "
                                 "0 to full between the deadzone and 1.");
              ImGui::TextWrapped(
                  "For joystick axes, the mesh will highlight in the positive "
                  "color when the axis is positive, and the negative color "
                  "when negative.");
            }

            EndShadedGroup(ShadeColor(0.58f, 0.16f, 0.16f),
                           ShadeBorder(0.58f, 0.16f, 0.16f));
            ImGui::TreePop();
          }

          // ---- Shader Selection ----
          if (ImGui::TreeNode("Shader Effect")) {
            BeginShadedGroup();
            ImGui::TextWrapped("Select a custom shader effect for this mesh.");

            // Build list of shader names
            std::vector<std::string> shaderNames;
            shaderNames.push_back("None");

            // Get the list from the shader manager
            std::vector<std::string> customShaders =
                GetShaderNames(); // declared in shader.h
            for (const auto &s : customShaders) {
              if (s != "None") // avoid duplicate
                shaderNames.push_back(s);
            }

            // Determine index of selected shader
            int current_shader_idx = 0;
            if (!selectedMesh.shader_name.empty()) {
              for (size_t i = 0; i < shaderNames.size(); ++i) {
                if (shaderNames[i] == selectedMesh.shader_name) {
                  current_shader_idx = (int)i;
                  break;
                }
              }
            }

            // Build const char* array for ImGui::Combo
            std::vector<const char *> shaderNamesCStr;
            for (const auto &s : shaderNames)
              shaderNamesCStr.push_back(s.c_str());

            // Combo
            if (ImGui::Combo("Shader", &current_shader_idx,
                             shaderNamesCStr.data(),
                             (int)shaderNamesCStr.size())) {
              if (current_shader_idx == 0)
                selectedMesh.shader_name = "";
              else
                selectedMesh.shader_name = shaderNames[current_shader_idx];
            }

            // ---- Add Resource (channel texture) ----
            // Only relevant once an actual shader is selected. This
            // covers the "shadertoy shaders with a noise channel don't
            // load correctly" case: rather than requiring the user to
            // manually find the shader's folder on disk and drop a
            // correctly-named channel0.png in by hand, they can pick an
            // image here and it's copied into the right place (and the
            // shader's cached channel texture is invalidated so it
            // takes effect immediately - see shader.cpp).
            if (current_shader_idx != 0) {
              if (ImGui::Button("Add Resource...")) {
                g_shader_resource_target = shaderNames[current_shader_idx];
                shader_resource_dialog.Open();
              }
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Add an image as a channel texture (iChannel0-3) for "
                    "this shader - useful for ShaderToy-style shaders "
                    "that expect a noise or gradient texture. Filled "
                    "into the next free channel slot automatically; if "
                    "no image is provided, unused channels fall back to "
                    "generated noise.");
            }

            EndShadedGroup(ShadeColor(0.2f, 0.5f, 0.3f),
                           ShadeBorder(0.2f, 0.5f, 0.3f));
            ImGui::TreePop();
          }

          // ---- Reset Transform ----
          ImGui::Separator();
          if (ImGui::Button("Reset Transform")) {
            selectedMesh.position[0] = selectedMesh.position[1] =
                selectedMesh.position[2] = 0.0f;
            selectedMesh.pivot_offset[0] = selectedMesh.pivot_offset[1] =
                selectedMesh.pivot_offset[2] = 0.0f;
            selectedMesh.rotation[0] = selectedMesh.rotation[1] =
                selectedMesh.rotation[2] = 0.0f;
            selectedMesh.travel[0] = selectedMesh.travel[1] =
                selectedMesh.travel[2] = 0.0f;
            selectedMesh.popup_offset[0] = selectedMesh.popup_offset[1] =
                selectedMesh.popup_offset[2] = 0.0f;
            selectedMesh.popup_rotation[0] = selectedMesh.popup_rotation[1] =
                selectedMesh.popup_rotation[2] = 0.0f;
            selectedMesh.trigger_max = 0.0f;
            selectedMesh.stick_max = 0.0f;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reset all transform values (position, pivot, "
                              "rotation, travel, popup) to zero.");

          // ---- Legacy Highlight checkbox (now replaced by override) ----
          // We keep it as a temporary visual highlight in the 3D view (doesn't
          // save)
          ImGui::Checkbox("Temporary Highlight (view only)",
                          &current_window->highlight_enabled);
          ImGui::SameLine();
          ImGui::ColorEdit3("Temp Color", current_window->highlight_color);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Temporary Highlight Color");

          if (current_window->highlight_enabled) {
            if (current_window->original_colors.find(selected_mesh) ==
                current_window->original_colors.end()) {
              current_window->original_colors[selected_mesh] = {
                  selectedMesh.material.color[0],
                  selectedMesh.material.color[1],
                  selectedMesh.material.color[2]};
            }
            selectedMesh.material.color[0] = current_window->highlight_color[0];
            selectedMesh.material.color[1] = current_window->highlight_color[1];
            selectedMesh.material.color[2] = current_window->highlight_color[2];
            selectedMesh.highlight_value = 0.0f;
          } else {
            auto it = current_window->original_colors.find(selected_mesh);
            if (it != current_window->original_colors.end()) {
              selectedMesh.material.color[0] = it->second[0];
              selectedMesh.material.color[1] = it->second[1];
              selectedMesh.material.color[2] = it->second[2];
              selectedMesh.highlight_value = 0.0f;
              current_window->original_colors.erase(it);
            }
          }

          // ---- Touchpoint anchoring (if this mesh is a touch point part) ----
          int part = selectedMesh.assignedPart;
          bool isTouchPoint =
              (part == 30 || part == 31 || part == 33 || part == 34);
          if (isTouchPoint) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f),
                               "Touch Point Settings");
            int touchpadIdx =
                getTouchpadAncestor(current_window->model, selected_mesh);
            if (touchpadIdx != -1) {
              ImGui::Text(
                  "Anchored to touchpad: %s",
                  current_window->model.meshes[touchpadIdx].name.c_str());
              ImGui::Text(
                  "Touch area size: %.2f x %.2f",
                  current_window->model.meshes[touchpadIdx].touch_width,
                  current_window->model.meshes[touchpadIdx].touch_height);
              if (ImGui::Button("Reset position to touchpad origin")) {
                selectedMesh.position[0] = 0.0f;
                selectedMesh.position[1] = 0.0f;
                selectedMesh.position[2] = 0.0f;
                if (selectedMesh.parentIndex != touchpadIdx) {
                  selectedMesh.parentIndex = touchpadIdx;
                }
                spdlog::info("Reset touchpoint to origin of touchpad.");
              }
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Set this touch point's position to (0,0,0) "
                                  "relative to its parent touchpad.");
            } else {
              ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                 "Not anchored to a touchpad.");
              if (ImGui::Button("Find and anchor to touchpad")) {
                int found = -1;
                for (int i = 0; i < (int)current_window->model.meshes.size();
                     ++i) {
                  if (current_window->model.meshes[i].isTouchpad) {
                    found = i;
                    break;
                  }
                }
                if (found != -1) {
                  selectedMesh.parentIndex = found;
                  selectedMesh.position[0] = 0.0f;
                  selectedMesh.position[1] = 0.0f;
                  selectedMesh.position[2] = 0.0f;
                  spdlog::info("Anchored touch point to touchpad mesh '{}'",
                               current_window->model.meshes[found].name);
                } else {
                  spdlog::warn("No touchpad mesh found. Please mark a mesh as "
                               "a touchpad first.");
                }
              }
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Find the first mesh marked as a touchpad and set this as "
                    "its child with position (0,0,0).");
            }
            // Override custom scale: force it off for touchpoints
            selectedMesh.useCustomScale = false;
          }

          // ---- Touchpad configuration (independent of assignedPart) ----
          ImGui::Separator();
          bool isTouchpad = selectedMesh.isTouchpad;
          if (ImGui::Checkbox("Is Touchpad", &isTouchpad)) {
            selectedMesh.isTouchpad = isTouchpad;
            if (isTouchpad) {
              if (selectedMesh.touch_width <= 0.01f)
                selectedMesh.touch_width = 1.0f;
              if (selectedMesh.touch_height <= 0.01f)
                selectedMesh.touch_height = 1.0f;
            }
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Mark this mesh as a touchpad surface. This "
                              "enables touch area controls.");
          ImGui::SameLine();
          ImGui::Checkbox("Is Touchpoint", &selectedMesh.isTouchpoint);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Mark this mesh as a touchpoint (moves with "
                              "mouse/touch input).");
          if (selectedMesh.isTouchpad) {
            ImGui::DragFloat("Touch Area Width", &selectedMesh.touch_width,
                             0.01f, 0.01f, 5.0f, "%.2f");
            if (ImGui::IsItemHovered())
              DraggableTooltip("Width of the touch-sensitive area "
                               "in world units.");
            ImGui::DragFloat("Touch Area Height", &selectedMesh.touch_height,
                             0.01f, 0.01f, 5.0f, "%.2f");
            if (ImGui::IsItemHovered())
              DraggableTooltip("Height of the touch-sensitive area "
                               "in world units.");

            ImGui::Checkbox("Show Touch Area",
                            &current_window->show_touch_area);

            if (ImGui::BeginTable("TouchTransform", 2,
                                  ImGuiTableFlags_BordersInnerV)) {
              ImGui::TableSetupColumn("Offset",
                                      ImGuiTableColumnFlags_WidthStretch);
              ImGui::TableSetupColumn("Rotation",
                                      ImGuiTableColumnFlags_WidthStretch);
              ImGui::TableHeadersRow();
              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);
              ImGui::Text("Offset (world units)");
              ImGui::DragFloat("X", &selectedMesh.touch_offset[0], 0.01f, -2.0f,
                               2.0f, "%.2f");
              if (ImGui::IsItemHovered())
                DraggableTooltip("Touchpoint horizontal offset.");
              ImGui::DragFloat("Y", &selectedMesh.touch_offset[1], 0.01f, -2.0f,
                               2.0f, "%.2f");
              if (ImGui::IsItemHovered())
                DraggableTooltip("Touchpoint vertical offset.");
              ImGui::DragFloat("Z", &selectedMesh.touch_offset[2], 0.01f, -2.0f,
                               2.0f, "%.2f");
              if (ImGui::IsItemHovered())
                DraggableTooltip("Touchpoint depth offset.");
              ImGui::TableSetColumnIndex(1);
              ImGui::Text("Rotation (degrees)");
              draggableFloatAngle("Yaw", &selectedMesh.touch_rotation[1],
                                  /*is_radians=*/true, 0.5f, -360.0f, 360.0f);
              if (ImGui::IsItemHovered())
                DraggableTooltip("Touchpoint yaw rotation.");
              draggableFloatAngle("Pitch", &selectedMesh.touch_rotation[0],
                                  /*is_radians=*/true, 0.5f, -360.0f, 360.0f);
              if (ImGui::IsItemHovered())
                DraggableTooltip("Touchpoint pitch rotation.");
              draggableFloatAngle("Roll", &selectedMesh.touch_rotation[2],
                                  /*is_radians=*/true, 0.5f, -360.0f, 360.0f);
              if (ImGui::IsItemHovered())
                DraggableTooltip("Touchpoint roll rotation.");
              ImGui::EndTable();
            }

            ImGui::TextColored(
                ImVec4(0.7f, 0.7f, 0.2f, 1.0f),
                "Note: To use touch points, assign a mesh to a touch point "
                "part,\n"
                "set its parent to the touchpad, and position it at (0,0,0) "
                "relative\n"
                "to the touchpad. It will then move within the touch area.");
          }

          // ---- Custom scale ----
          if (selectedMesh.useCustomScale) {
            ImGui::Separator();
            ImGui::Text("Custom scale");
            ImGui::InputFloat("Scale X", &selectedMesh.scale[0], 0.01f, 1.0f,
                              "%.2f");
            ImGui::InputFloat("Scale Y", &selectedMesh.scale[1], 0.01f, 1.0f,
                              "%.2f");
            ImGui::InputFloat("Scale Z", &selectedMesh.scale[2], 0.01f, 1.0f,
                              "%.2f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Custom scale for this mesh.");
          }

          ImGui::Separator();
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                             "Changes are saved when you click 'Save Model' or "
                             "switch models.");
        }
      } else {
        ImGui::TextDisabled(
            "Select a mesh from the table above to edit its properties.");
      }
    } // end Model

    // ============================================================
    // GYRO
    // ============================================================
    if (ImGui::CollapsingHeader("Gyro")) {
      bool has_gyro = false;
      if (current_window->is_gamecontroller && current_window->sdl_controller) {
        has_gyro = SDL_GamepadHasSensor(current_window->sdl_controller,
                                        SDL_SENSOR_GYRO) == true;
      }
      if (!has_gyro && current_window->gyro_sensor) {
        has_gyro = true;
      }

      bool enabled = current_window->gyro_enabled;
      ImGui::BeginDisabled(!has_gyro);
      if (ImGui::Checkbox("Enable Gyro", &enabled)) {
        current_window->gyro_enabled = enabled;
        if (current_window->is_gamecontroller &&
            current_window->sdl_controller) {
          SDL_SetGamepadSensorEnabled(current_window->sdl_controller,
                                      SDL_SENSOR_GYRO, enabled);
        }
        if (enabled) {
          current_window->gyro_toggled = true;
          Uint64 timestamp;
          if (SDL_GetGamepadSensorData(current_window->sdl_controller,
                                       SDL_SENSOR_GYRO,
                                       current_window->gyro_data, 3) == 0) {
            current_window->gyro_time = timestamp;
          }
        } else {
          current_window->gyro_matrix = glm::mat4(1.0f);
        }
      }
      ImGui::EndDisabled();

      if (has_gyro && current_window->gyro_enabled) {
        ImGui::DragFloat("Gyro Sensitivity", &current_window->gyro_sensitivity,
                         0.1f, 0.1f, 10.0f, "%.1f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Sensitivity multiplier (internally scaled by 0.1). "
              "Range 0-10 gives effective sensitivity 0-1.0.");
        ImGui::DragInt("Gyro Correction", &current_window->gyro_correction, 1,
                       0, 10);
        if (ImGui::Button("Reset Gyro")) {
          current_window->gyro_matrix = glm::mat4(1.0f);
        }
        ImGui::NewLine();
        ImGui::Text("Reset Gyro button combo");

        std::string button1_name = "";
        if (current_window->reset_gyro_button1 > -1) {
          button1_name =
              input_names[current_window->reset_gyro_button1].c_str();
        } else {
          button1_name = "none";
        }
        if (ImGui::BeginCombo("Button 1", button1_name.c_str(), 0)) {
          for (unsigned i = 0; i < 22; i++) {
            if (i > 0) {
              if (ImGui::Selectable(input_names[i - 1].c_str())) {
                current_window->reset_gyro_button1 = i - 1;
              }
            } else {
              if (ImGui::Selectable("none")) {
                current_window->reset_gyro_button1 = -1;
              }
            }
          }
          ImGui::EndCombo();
        }

        std::string button2_name = "";
        if (current_window->reset_gyro_button2 > -1) {
          button2_name =
              input_names[current_window->reset_gyro_button2].c_str();
        } else {
          button2_name = "none";
        }
        if (ImGui::BeginCombo("Button 2", button2_name.c_str(), 0)) {
          for (unsigned i = 0; i < 22; i++) {
            if (i > 0) {
              if (ImGui::Selectable(input_names[i - 1].c_str())) {
                current_window->reset_gyro_button2 = i - 1;
              }
            } else {
              if (ImGui::Selectable("none")) {
                current_window->reset_gyro_button2 = -1;
              }
            }
          }
          ImGui::EndCombo();
        }

        ImGui::Checkbox("Gyro Debug Logging",
                        &current_window->gyro_debug_logging);
      } else if (!has_gyro) {
        ImGui::TextDisabled("No gyroscope detected for this controller.");
      }
    }

    // ============================================================
    // NETWORK
    // ============================================================
    if (ImGui::CollapsingHeader("Network")) {
      ImGui::TextWrapped(
          "Send or receive controller input over the network as JSON.");
      ImGui::Separator();

      // ---- Status indicator ----
      // Uses network_connected rather than network_status: the latter
      // is only ever updated on the UDP code paths (see
      // sendNetworkState()/receiveNetworkState() in
      // controller_window.cpp) and is never touched anywhere in the
      // TCP paths at all, so it could never turn green for a TCP
      // connection regardless of whether one was actually established.
      // network_connected is properly maintained for both protocols.
      {
        bool connected = current_window->network_connected;
        ImU32 color;
        const char *label;

        if (!current_window->network_enabled) {
          color = ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // gray
          label = "Off";
        } else if (connected) {
          color = ImGui::GetColorU32(ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // green
          label = "Connected";
        } else {
          color = ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.0f, 1.0f)); // yellow
          label = "Trying to connect...";
        }

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(pos.x + 6, pos.y + 6), 6.0f, color);
        ImGui::Dummy(ImVec2(20, 12));
        ImGui::SameLine();
        ImGui::Text("%s", label);
        ImGui::Spacing();

        // ---- "Just connected" toast ----
        // A small circle is easy to miss - this gives an unmissable,
        // temporary confirmation the moment a connection actually
        // completes, rather than making the user hunt for and stare at
        // the indicator to notice it changed color.
        if (connected && !current_window->network_was_connected) {
          current_window->network_connected_toast_until = glfwGetTime() + 3.0;
        }
        current_window->network_was_connected = connected;
        if (glfwGetTime() < current_window->network_connected_toast_until) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
          if (current_window->network_mode == 0) {
            ImGui::TextWrapped("\xE2\x9C\x93 Connected - now sending to %s:%d",
                               current_window->network_ip.c_str(),
                               current_window->network_port);
          } else {
            ImGui::TextWrapped("\xE2\x9C\x93 Connected - receiving on port %d",
                               current_window->network_port);
          }
          ImGui::PopStyleColor();
        }
      }

      // Mode
      int mode = current_window->network_mode;
      const char *mode_names[] = {"Sender", "Receiver"};
      if (ImGui::Combo("Mode", &mode, mode_names, 2)) {
        current_window->network_mode = mode;
        if (current_window->network_enabled) {
          initNetwork(*current_window);
        }
      }

      // Protocol
      int proto = current_window->network_protocol;
      const char *proto_names[] = {"UDP", "TCP"};
      if (ImGui::Combo("Protocol", &proto, proto_names, 2)) {
        current_window->network_protocol = proto;
        if (current_window->network_enabled) {
          initNetwork(*current_window);
        }
      }

      // IP / Port
      char ip[256];
      strncpy(ip, current_window->network_ip.c_str(), 255);
      if (ImGui::InputText("IP Address", ip, 256)) {
        current_window->network_ip = ip;
      }
      if (ImGui::InputInt("Port", &current_window->network_port, 1, 100)) {
        current_window->network_port =
            std::max(1, std::min(65535, current_window->network_port));
      }

      // Send rate
      int rate = current_window->network_send_rate;
      const char *rate_names[] = {"Max", "60 Hz", "30 Hz", "15 Hz", "10 Hz"};
      int rate_vals[] = {0, 60, 30, 15, 10};
      int rate_idx = 0;
      for (int i = 0; i < 5; i++)
        if (rate_vals[i] == rate)
          rate_idx = i;
      if (ImGui::Combo("Send Rate", &rate_idx, rate_names, 5)) {
        current_window->network_send_rate = rate_vals[rate_idx];
      }

      // Enable toggle
      bool enable = current_window->network_enabled;
      if (ImGui::Checkbox("Enable Network", &enable)) {
        current_window->network_enabled = enable;
        if (enable) {
          initNetwork(*current_window);
        } else {
          shutdownNetwork(*current_window);
        }
      }

      // Explanation text
      ImGui::TextWrapped(
          "UDP: fast, connectionless, supports broadcast (e.g., "
          "255.255.255.255). "
          "TCP: reliable, one-to-one (receiver accepts one sender).");
      ImGui::TextWrapped(
          "Sender sends the current mesh state at the chosen rate. "
          "Receiver listens on the port and applies received state.");
      ImGui::TextWrapped(
          "Note: In receiver mode, local controller input is ignored.");
    }

    // ============================================================
    // LIGHTING
    // ============================================================
    if (ImGui::CollapsingHeader("Lighting")) {
      // ---- Directional Lights ----
      if (ImGui::TreeNode("Directional Lights")) {
        BeginShadedGroup();
        static unsigned current_dir_light = 0;
        std::string preview_name = "";
        if (current_window->direct_lights.size() > 0)
          preview_name =
              current_window->direct_lights[current_dir_light].name.c_str();
        if (ImGui::BeginCombo("Lights", preview_name.c_str(), 0)) {
          for (unsigned i = 0; i < current_window->direct_lights.size(); i++) {
            if (ImGui::Selectable(
                    current_window->direct_lights[i].name.c_str())) {
              current_dir_light = i;
            }
          }
          ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Select a directional light to edit.");

        // New light button
        if (current_window->direct_lights.size() < 16) {
          if (ImGui::Button("New Light")) {
            direct_light new_dir_light;
            std::string new_light_name = "Directional Light ";
            static unsigned count = current_window->direct_lights.size() + 1;
            bool name_exists = false;
            while (true) {
              name_exists = false;
              std::string test_name = new_light_name;
              test_name.append(std::to_string(count));
              for (direct_light d : current_window->direct_lights) {
                if (d.name == test_name.c_str()) {
                  name_exists = true;
                  count++;
                  break;
                }
              }
              if (!name_exists)
                break;
            }
            new_dir_light.name =
                new_light_name.append(std::to_string(count)).c_str();
            count++;
            current_window->direct_lights.push_back(new_dir_light);
            current_dir_light = current_window->direct_lights.size() - 1;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create a new directional light.");
        }

        // Delete and edit – only if we have lights
        if (current_window->direct_lights.size() > 0) {
          if (current_window->direct_lights.size() < 16)
            ImGui::SameLine();
          if (ImGui::Button("Delete Light")) {
            current_window->direct_lights.erase(
                current_window->direct_lights.begin() + current_dir_light);
            // Reset index to 0 (or keep it valid)
            if (current_window->direct_lights.size() > 0) {
              if (current_dir_light >= current_window->direct_lights.size())
                current_dir_light = 0;
            }
            // If we deleted the only light, skip the rest of the editing UI
            // by using a goto or by re-checking the size.
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete the selected directional light.");
        }

        // ---- Editing controls (only if we still have lights) ----
        if (current_window->direct_lights.size() > 0) {
          ImGui::NewLine();
          direct_light *d = &current_window->direct_lights[current_dir_light];
          char name[64] = {};
          if (ImGui::InputTextWithHint("Name", d->name.c_str(), name,
                                       IM_ARRAYSIZE(name),
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
            bool exists = false;
            for (direct_light dl : current_window->direct_lights) {
              if (dl.name == name) {
                exists = true;
                break;
              }
            }
            if (!exists)
              d->name = std::string(name);
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rename the light.");
          ImGui::DragFloat("X Direction", &d->direction.x, 0.01f, -1, 1);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light direction X.");
          ImGui::DragFloat("Y Direction", &d->direction.y, 0.01f, -1, 1);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light direction Y.");
          ImGui::DragFloat("Z Direction", &d->direction.z, 0.01f, -1, 1);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light direction Z.");
          ImGui::ColorEdit3("Color", d->color);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light color.");
        }
        EndShadedGroup(ShadeColor(0.58f, 0.46f, 0.10f),
                       ShadeBorder(0.58f, 0.46f, 0.10f));
        ImGui::TreePop();
      }

      // ---- Point Lights ----
      if (ImGui::TreeNode("Point Lights")) {
        BeginShadedGroup();
        static unsigned current_point_light = 0;
        std::string preview_name = "";
        if (current_window->point_lights.size() > 0)
          preview_name =
              current_window->point_lights[current_point_light].name.c_str();
        if (ImGui::BeginCombo("Lights", preview_name.c_str(), 0)) {
          for (unsigned i = 0; i < current_window->point_lights.size(); i++) {
            if (ImGui::Selectable(
                    current_window->point_lights[i].name.c_str())) {
              current_point_light = i;
            }
          }
          ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Select a point light to edit.");

        // New light button
        if (current_window->point_lights.size() < 16) {
          if (ImGui::Button("New Light")) {
            point_light new_point_light;
            std::string new_light_name = "Point Light ";
            static unsigned count = current_window->point_lights.size() + 1;
            bool name_exists = false;
            while (true) {
              name_exists = false;
              std::string test_name = new_light_name;
              test_name.append(std::to_string(count));
              for (point_light p : current_window->point_lights) {
                if (p.name == test_name.c_str()) {
                  name_exists = true;
                  count++;
                  break;
                }
              }
              if (!name_exists)
                break;
            }
            new_point_light.name =
                new_light_name.append(std::to_string(count)).c_str();
            count++;
            new_point_light.position.x = 2.0f;
            new_point_light.position.y = 2.0f;
            new_point_light.position.z = 2.0f;
            current_window->point_lights.push_back(new_point_light);
            current_point_light = current_window->point_lights.size() - 1;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create a new point light.");
        }

        // Delete button
        if (current_window->point_lights.size() > 0) {
          if (current_window->point_lights.size() < 16)
            ImGui::SameLine();
          if (ImGui::Button("Delete Light")) {
            current_window->point_lights.erase(
                current_window->point_lights.begin() + current_point_light);
            if (current_window->point_lights.size() > 0) {
              if (current_point_light >= current_window->point_lights.size())
                current_point_light = 0;
            }
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete the selected point light.");
        }

        // ---- Editing controls (only if we still have lights) ----
        if (current_window->point_lights.size() > 0) {
          ImGui::NewLine();
          point_light *p = &current_window->point_lights[current_point_light];
          char name[64] = {};
          if (ImGui::InputTextWithHint("Name", p->name.c_str(), name,
                                       IM_ARRAYSIZE(name),
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
            bool exists = false;
            for (point_light pl : current_window->point_lights) {
              if (pl.name == name) {
                exists = true;
                break;
              }
            }
            if (!exists)
              p->name = std::string(name);
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rename the light.");
          ImGui::Checkbox("Hide Source", &p->hide);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide the light bulb visual.");
          ImGui::DragFloat("X Position", &p->position.x, 0.1f, -10, 10);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light position X.");
          ImGui::DragFloat("Y Position", &p->position.y, 0.1f, -10, 10);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light position Y.");
          ImGui::DragFloat("Z Position", &p->position.z, 0.1f, -10, 10);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light position Z.");
          ImGui::DragFloat("Brightness", &p->intensity, 0.01f, 0, 1);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light intensity.");
          if (ImGui::ColorEdit3("Color", p->color)) {
            p->ambient.r = p->color[0] * 0.05f;
            p->ambient.g = p->color[1] * 0.05f;
            p->ambient.b = p->color[2] * 0.05f;
            p->diffuse.r = p->color[0] * 0.8f;
            p->diffuse.g = p->color[1] * 0.8f;
            p->diffuse.b = p->color[2] * 0.8f;
            p->specular.r = p->color[0];
            p->specular.g = p->color[1];
            p->specular.b = p->color[2];
          }
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light color.");
        }
        EndShadedGroup(ShadeColor(0.58f, 0.20f, 0.36f),
                       ShadeBorder(0.58f, 0.20f, 0.36f));
        ImGui::TreePop();
      }

      // ---- Spot Lights ----
      if (ImGui::TreeNode("Spot Lights")) {
        BeginShadedGroup();
        static unsigned current_spot_light = 0;
        std::string preview_name = "";
        if (current_window->spot_lights.size() > 0)
          preview_name =
              current_window->spot_lights[current_spot_light].name.c_str();
        if (ImGui::BeginCombo("Lights", preview_name.c_str(), 0)) {
          for (unsigned i = 0; i < current_window->spot_lights.size(); i++) {
            if (ImGui::Selectable(
                    current_window->spot_lights[i].name.c_str())) {
              current_spot_light = i;
            }
          }
          ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Select a spot light to edit.");

        // New light button
        if (current_window->spot_lights.size() < 16) {
          if (ImGui::Button("New Light")) {
            spot_light new_spot_light;
            std::string new_light_name = "Spot Light ";
            static unsigned count = current_window->spot_lights.size() + 1;
            bool name_exists = false;
            while (true) {
              name_exists = false;
              std::string test_name = new_light_name;
              test_name.append(std::to_string(count));
              for (spot_light s : current_window->spot_lights) {
                if (s.name == test_name.c_str()) {
                  name_exists = true;
                  count++;
                  break;
                }
              }
              if (!name_exists)
                break;
            }
            new_spot_light.name =
                new_light_name.append(std::to_string(count)).c_str();
            count++;
            new_spot_light.position.x = 0.0f;
            new_spot_light.position.y = 0.0f;
            new_spot_light.position.z = 2.0f;
            current_window->spot_lights.push_back(new_spot_light);
            current_spot_light = current_window->spot_lights.size() - 1;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create a new spot light.");
        }

        // Delete button
        if (current_window->spot_lights.size() > 0) {
          if (current_window->spot_lights.size() < 16)
            ImGui::SameLine();
          if (ImGui::Button("Delete Light")) {
            current_window->spot_lights.erase(
                current_window->spot_lights.begin() + current_spot_light);
            if (current_window->spot_lights.size() > 0) {
              if (current_spot_light >= current_window->spot_lights.size())
                current_spot_light = 0;
            }
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete the selected spot light.");
        }

        // ---- Editing controls (only if we still have lights) ----
        if (current_window->spot_lights.size() > 0) {
          ImGui::NewLine();
          spot_light *s = &current_window->spot_lights[current_spot_light];
          char name[64] = {};
          if (ImGui::InputTextWithHint("Name", s->name.c_str(), name,
                                       IM_ARRAYSIZE(name),
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
            bool exists = false;
            for (spot_light sl : current_window->spot_lights) {
              if (sl.name == name) {
                exists = true;
                break;
              }
            }
            if (!exists)
              s->name = std::string(name);
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rename the light.");
          ImGui::Checkbox("Hide Source", &s->hide);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide the light bulb visual.");
          ImGui::DragFloat("X Position", &s->position.x, 0.1f, -10, 10);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light position X.");
          ImGui::DragFloat("Y Position", &s->position.y, 0.1f, -10, 10);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light position Y.");
          ImGui::DragFloat("Z Position", &s->position.z, 0.1f, -10, 10);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light position Z.");
          ImGui::DragFloat("Brightness", &s->intensity, 0.01f, 0, 1);
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light intensity.");
          if (ImGui::ColorEdit3("Color", s->color)) {
            s->ambient.r = s->color[0] * 0.05f;
            s->ambient.g = s->color[1] * 0.05f;
            s->ambient.b = s->color[2] * 0.05f;
            s->diffuse.r = s->color[0] * 0.8f;
            s->diffuse.g = s->color[1] * 0.8f;
            s->diffuse.b = s->color[2] * 0.8f;
            s->specular.r = s->color[0];
            s->specular.g = s->color[1];
            s->specular.b = s->color[2];
          }
          if (ImGui::IsItemHovered())
            DraggableTooltip("Light color.");
          if (draggableFloatAngle("Yaw", &s->yaw, false, 0.5f, -180, 180)) {
            s->direction.x =
                cos(glm::radians(s->pitch)) * sin(glm::radians(s->yaw + 180));
            s->direction.y = sin(glm::radians(s->pitch));
            s->direction.z =
                cos(glm::radians(s->pitch)) * cos(glm::radians(s->yaw + 180));
          }
          DraggableTooltip("Horizontal direction.");
          if (draggableFloatAngle("Pitch", &s->pitch, false, 0.5f, -90, 90)) {
            s->direction.x =
                cos(glm::radians(s->pitch)) * sin(glm::radians(s->yaw + 180));
            s->direction.y = sin(glm::radians(s->pitch));
            s->direction.z =
                cos(glm::radians(s->pitch)) * cos(glm::radians(s->yaw + 180));
          }
          DraggableTooltip("Vertical direction.");
          draggableFloatAngle("Beam Angle", &s->cutoff, false, 0.5f, 0, 90);
          DraggableTooltip("Inner cone angle.");
          draggableFloatAngle("Edge Blur", &s->outer_cutoff, false, 0.5f, 0,
                              100);
          DraggableTooltip("Softness of the cone edge.");
        }
        EndShadedGroup(ShadeColor(0.16f, 0.42f, 0.56f),
                       ShadeBorder(0.16f, 0.42f, 0.56f));
        ImGui::TreePop();
      }
    }

    // ============================================================
    // HELP
    // ============================================================
    if (ImGui::CollapsingHeader("Help")) {
      // Main two columns: icon (left) and content (right)
      ImGui::Columns(2, "HelpColumns", false);
      ImGui::SetColumnWidth(0, 80.0f);

      // Left: icon
      GLuint iconTex = getHelpIcon();
      if (iconTex) {
        ImGui::Image((void *)(intptr_t)iconTex, ImVec2(64, 64));
      } else {
        ImGui::Dummy(ImVec2(64, 64));
      }
      ImGui::NextColumn();

      // Right: your project info
      ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f),
                         "3D Controller Overlay +");
      ImGui::SameLine();
      ImGui::TextDisabled("v1.0.0");

      ImGui::NewLine();
      ImGui::Text("A feature-rich fork of the original 3D Controller Overlay.");
      ImGui::Text("Developed by Khyretos with AI assistence.");
      ImGui::NewLine();

      // ---- Two‑column table for repository & Discord ----
      if (ImGui::BeginTable("HelpLinks", 2,
                            ImGuiTableFlags_SizingStretchProp |
                                ImGuiTableFlags_NoBordersInBody)) {
        // Label column: 35% of available width, Widgets column: 65%
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch,
                                0.35f);
        ImGui::TableSetupColumn("Widgets", ImGuiTableColumnFlags_WidthStretch,
                                0.65f);

        // Row 1: Repository
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Repository (this fork):");

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Open Khyretos/3dco-plus")) {
          OsOpenInShell("https://github.com/Khyretos/3dco-plus");
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy URL")) {
          ImGui::SetClipboardText("https://github.com/Khyretos/3dco-plus");
        }

        // Row 2: Discord
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("My Discord (support / feedback):");

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Join my Discord")) {
          // Replace with your own invite link
          OsOpenInShell("https://discord.com/invite/pRUfZhNaYQ");
        }

        ImGui::EndTable();
      }

      ImGui::Columns(1);

      // ------------------------------------------------------------------
      // Original project section – with shaded background, centered
      // ------------------------------------------------------------------
      ImGui::Separator();

      BeginShadedGroup();

      CenterText("Original Project");
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.2f, 1.0f), "Original Project");

      CenterText("3D Controller Overlay by Larf (larfingshnew)");
      ImGui::Text("3D Controller Overlay by Larf (larfingshnew)");

      CenterText("The original project this fork is based on.");
      ImGui::Text("The original project this fork is based on.");

      // Center the buttons row
      const char *btn1 = "Open Original GitHub";
      const char *btn2 = "Open Original Discord";
      float btn1w = ImGui::CalcTextSize(btn1).x +
                    ImGui::GetStyle().FramePadding.x * 2 + 2;
      float btn2w = ImGui::CalcTextSize(btn2).x +
                    ImGui::GetStyle().FramePadding.x * 2 + 2;
      float spacing = ImGui::GetStyle().ItemSpacing.x;
      float totalWidth = btn1w + spacing + btn2w;
      CenterItem(totalWidth);

      if (ImGui::Button(btn1)) {
        OsOpenInShell("https://github.com/larfingshnew/3d-controller-overlay");
      }
      ImGui::SameLine();
      if (ImGui::Button(btn2)) {
        OsOpenInShell("https://discord.gg/aKwHHvCMnS");
      }

      EndShadedGroup(ShadeColor(0.30f, 0.20f, 0.30f, 0.40f),
                     ShadeBorder(0.30f, 0.20f, 0.30f, 0.70f));
    }
  } // end big if (tabs.size() > 0 && new_controller_window == false)

  ImGui::End();
  ImGui::PopStyleVar();

  texture_dialog.Display();
  model_dialog.Display();
  shader_resource_dialog.Display();

  if (shader_resource_dialog.HasSelected()) {
    std::string destDir = getShaderResourceDirectory(g_shader_resource_target);

    // Find the first channel slot (0-3) not already occupied by an
    // image file, so multiple "Add Resource" clicks fill iChannel0,
    // then iChannel1, etc. rather than all overwriting the same slot.
    int slot = -1;
    for (int i = 0; i < 4 && slot == -1; ++i) {
      bool exists = false;
      for (const char *ext : {".png", ".jpg", ".jpeg"}) {
        if (std::filesystem::exists(destDir + "/channel" +
                                    std::to_string(i) + ext)) {
          exists = true;
          break;
        }
      }
      if (!exists)
        slot = i;
    }
    if (slot == -1) {
      slot = 0; // all four channel slots already used - overwrite the first
      spdlog::warn("Shader '{}' already has 4 channel resources; "
                   "overwriting channel0.",
                   g_shader_resource_target);
    }

    std::string srcPath = shader_resource_dialog.GetSelected().string();
    std::string ext = std::filesystem::path(srcPath).extension().string();
    std::string destPath = destDir + "/channel" + std::to_string(slot) + ext;
    try {
      // Clear out any other-extension file for this slot first, so
      // re-adding a resource as a different format doesn't leave two
      // files that both match "channelN.*" for the same slot.
      for (const char *oldExt : {".png", ".jpg", ".jpeg"}) {
        std::string oldPath =
            destDir + "/channel" + std::to_string(slot) + oldExt;
        if (oldPath != destPath && std::filesystem::exists(oldPath))
          std::filesystem::remove(oldPath);
      }
      std::filesystem::copy_file(
          srcPath, destPath, std::filesystem::copy_options::overwrite_existing);
      spdlog::info("Added shader resource '{}' as channel{} for shader '{}'",
                   srcPath, slot, g_shader_resource_target);
      invalidateShaderChannelCache(g_shader_resource_target, slot);
    } catch (const std::exception &e) {
      spdlog::error("Failed to copy shader resource '{}': {}", srcPath,
                    e.what());
    }
    shader_resource_dialog.ClearSelected();
  }

  if (texture_dialog.HasSelected()) {
    controller_window *ctrl = getControllerWindow(tabs[selected_tab].ID);
    if (!ctrl) {
      spdlog::error("No controller window for texture import.");
      texture_dialog.ClearSelected();
    } else if (ctrl->model.meshes.empty()) {
      spdlog::error("Cannot add texture: model has no meshes.");
      texture_dialog.ClearSelected();
    } else if (texture_mesh >= ctrl->model.meshes.size()) {
      spdlog::error("Texture mesh index out of range.");
      texture_dialog.ClearSelected();
    } else {
      spdlog::debug("Selected texture file: {}",
                    texture_dialog.GetSelected().string());
      glfwMakeContextCurrent(ctrl->glfw_window);
      Texture t;
      loadTexture(t.id, texture_dialog.GetSelected().string());
      t.path = texture_dialog.GetSelected().string();
      t.name =
          std::to_string(ctrl->model.meshes[texture_mesh].textures.size() + 1) +
          ": " + t.path;
      ctrl->model.meshes[texture_mesh].textures.push_back(t);
      glfwMakeContextCurrent(glfw_settings_window);
      texture_dialog.ClearSelected();
    }
  }

  if (model_dialog.HasSelected()) {
    controller_window *ctrl_win = getControllerWindow(tabs[selected_tab].ID);
    if (!ctrl_win) {
      spdlog::error("No valid controller window for model import.");
      model_dialog.ClearSelected();
    } else if (selected_mesh < 0 ||
               selected_mesh >= (int)ctrl_win->model.meshes.size()) {
      spdlog::error("Invalid mesh index: {}", selected_mesh);
      model_dialog.ClearSelected();
    } else {
      spdlog::debug("Selected model file: {}",
                    model_dialog.GetSelected().string());
      const auto copy_options =
          std::filesystem::copy_options::overwrite_existing;
      std::filesystem::path from_path = model_dialog.GetSelected();
      std::filesystem::path to_path = get_models_root();
      to_path.append(ctrl_win->model.path);
      to_path.append(mesh_filenames[selected_mesh]);
      std::filesystem::copy(from_path, to_path, copy_options);

      glfwMakeContextCurrent(ctrl_win->glfw_window);
      loadMesh(ctrl_win->model.meshes[selected_mesh], to_path.string());
      glfwMakeContextCurrent(glfw_settings_window);

      writeJson(ctrl_win->model, ctrl_win->model.path + "/info.json");
    }
    model_dialog.ClearSelected();
  }
  // --- Import Model Dialog ---
  import_model_dialog.Display();
  if (import_model_dialog.HasSelected()) {
    std::string filepath = import_model_dialog.GetSelected().string();
    spdlog::info("Importing model: {}", filepath);

    Model temp_model;
    importModelFile(temp_model, filepath);
    if (temp_model.imported_meshes.empty()) {
      spdlog::error("Failed to import model: no meshes found.");
      import_model_dialog.ClearSelected();
    } else {
      std::string preview_title =
          "Import Preview - " +
          std::filesystem::path(filepath).filename().string();
      createControllerWindow(preview_title, "dummy");
      controller_window *preview_window = getLastWindow();
      preview_window->model = temp_model;
      convertImportedToMeshes(preview_window->model);
      // Ensure all meshes are visible
      for (auto &mesh : preview_window->model.meshes) {
        mesh.visible = true;
        mesh.material.alpha = 1.0f;
      }
      preview_window->is_import_preview = true;
      preview_window->import_preview.is_open = true;
      preview_window->import_preview.imported_model = temp_model;

      preview_window->import_preview.assignments.clear();
      for (auto &mesh : temp_model.imported_meshes) {
        ImportAssignment assign;
        assign.mesh_name = mesh.name;
        assign.assigned_part = -1;
        assign.touch_width = 1.0f;
        assign.touch_height = 1.0f;
        preview_window->import_preview.assignments.push_back(assign);
      }
      preview_window->import_preview.selected_mesh_index = -1;
      preview_window->import_preview.save_name = "NewModel";

      import_model_dialog.ClearSelected();
    }
  }

  ImGui::Render();
  glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(glfw_settings_window);

  if (new_controller_window) {
    std::string first_model = get_first_model();
    if (!first_model.empty()) {
      createControllerWindow(new_tab_title, first_model);
      tabs.back().ID = tabs_made;
      getLastWindow()->ID = tabs_made;
    } else {
      spdlog::warn("No model folders found in '{}'. Please create a "
                   "model folder.",
                   get_models_root());
      tabs.pop_back();
      tabs_made--;
    }
  }
} // end drawSettingsWindow()

// =========================================================================
//  HELPER FUNCTION DEFINITIONS
// =========================================================================

void DrawImportPreviewControls(controller_window &w) {
  ImGui::Text("Imported Model: %zu meshes",
              w.import_preview.imported_model.imported_meshes.size());

  // Sort assignments alphabetically by mesh name
  std::sort(w.import_preview.assignments.begin(),
            w.import_preview.assignments.end(),
            [](const ImportAssignment &a, const ImportAssignment &b) {
              return a.mesh_name < b.mesh_name;
            });

  // Update mesh colors and touch dimensions in real‑time
  for (auto &assign : w.import_preview.assignments) {
    if (assign.assigned_part >= 0 && assign.assigned_part < 35) {
      Mesh &mesh = w.model.meshes[assign.assigned_part];
      mesh.visible = true;
      mesh.material.alpha = 1.0f;
      bool isTouchPart =
          (assign.assigned_part == 29 || assign.assigned_part == 30 ||
           assign.assigned_part == 31 || assign.assigned_part == 32 ||
           assign.assigned_part == 33 || assign.assigned_part == 34);
      if (isTouchPart) {
        mesh.material.color[0] = 1.0f;
        mesh.material.color[1] = 0.2f;
        mesh.material.color[2] = 0.2f;
        mesh.highlight_value = 0.3f;
        mesh.touch_width = assign.touch_width;
        mesh.touch_height = assign.touch_height;
      } else {
        mesh.material.color[0] = 0.8f;
        mesh.material.color[1] = 0.8f;
        mesh.material.color[2] = 0.8f;
        mesh.highlight_value = 0.0f;
      }
    }
  }

  if (ImGui::BeginTable("ImportAssignTable", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("Mesh Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Controller Part",
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Max Angle", ImGuiTableColumnFlags_WidthFixed,
                            100.0f);
    ImGui::TableSetupColumn("Parent Part", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Touch W", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Touch H", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < w.import_preview.assignments.size(); ++i) {
      auto &assign = w.import_preview.assignments[i];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%s", assign.mesh_name.c_str());
      ImGui::TableSetColumnIndex(1);

      int current_part = assign.assigned_part + 1;
      const char *part_names[36];
      part_names[0] = "Unassigned";
      for (int j = 0; j < 35; ++j)
        part_names[j + 1] = mesh_names[j].c_str();

      ImGui::PushID(i);
      if (ImGui::Combo("##part", &current_part, part_names, 36)) {
        assign.assigned_part = current_part - 1;
        assign.max_angle = 0.0f;
        assign.parent_part = -1;
        assign.touch_width = 1.0f;
        assign.touch_height = 1.0f;
        w.import_preview.selected_mesh_index = -1;
        if (assign.assigned_part >= 0 &&
            assign.assigned_part < (int)w.model.meshes.size()) {
          w.model.meshes[assign.assigned_part].stick_max = 0.0f;
          w.model.meshes[assign.assigned_part].trigger_max = 0.0f;
        }
        spdlog::debug("Assigned mesh '{}' to part {} ({})", assign.mesh_name,
                     assign.assigned_part,
                     assign.assigned_part >= 0
                         ? mesh_names[assign.assigned_part]
                         : "none");
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(2);
      bool isStick = (assign.assigned_part == 5 || assign.assigned_part == 6);
      bool isTrigger = (assign.assigned_part == 3 || assign.assigned_part == 4);
      if (isStick || isTrigger) {
        float max_angle_deg = glm::degrees(assign.max_angle);
        float min_deg = 0.0f, max_deg = 45.0f;
        if (isTrigger)
          max_deg = 90.0f;
        ImGui::PushID(i + 1000);
        if (ImGui::SliderFloat("##maxangle", &max_angle_deg, min_deg, max_deg,
                               "%.1f°")) {
          assign.max_angle = glm::radians(max_angle_deg);
          if (assign.assigned_part >= 0 &&
              assign.assigned_part < (int)w.model.meshes.size()) {
            if (isStick)
              w.model.meshes[assign.assigned_part].stick_max = assign.max_angle;
            else if (isTrigger)
              w.model.meshes[assign.assigned_part].trigger_max =
                  assign.max_angle;
          }
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Maximum rotation/pull angle in degrees.");
        }
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("N/A");
      }

      ImGui::TableSetColumnIndex(3);
      if (assign.assigned_part >= 0 && assign.assigned_part < 35) {
        int current_parent = assign.parent_part + 1;
        const char *parent_names[36];
        parent_names[0] = "None";
        for (int j = 0; j < 35; ++j)
          parent_names[j + 1] = mesh_names[j].c_str();
        ImGui::PushID(i + 2000);
        if (ImGui::Combo("##parent", &current_parent, parent_names, 36)) {
          assign.parent_part = current_parent - 1;
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Attach this mesh to a parent part (e.g., stick).");
        }
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("N/A");
      }

      ImGui::TableSetColumnIndex(4);
      bool isTouchPart =
          (assign.assigned_part == 29 || assign.assigned_part == 30 ||
           assign.assigned_part == 31 || assign.assigned_part == 32 ||
           assign.assigned_part == 33 || assign.assigned_part == 34);
      if (isTouchPart) {
        ImGui::PushID(i + 3000);
        if (ImGui::SliderFloat("##tw", &assign.touch_width, 0.01f, 5.0f,
                               "%.2f")) {
          if (assign.touch_width < 0.01f)
            assign.touch_width = 0.01f;
          if (assign.touch_width > 5.0f)
            assign.touch_width = 5.0f;
          if (assign.assigned_part >= 0 &&
              assign.assigned_part < (int)w.model.meshes.size()) {
            w.model.meshes[assign.assigned_part].touch_width =
                assign.touch_width;
          }
        }
        if (ImGui::IsItemHovered())
          DraggableTooltip("Touch area width (world units). Adjust to match "
                           "your physical controller.");
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("N/A");
      }

      ImGui::TableSetColumnIndex(5);
      if (isTouchPart) {
        ImGui::PushID(i + 4000);
        if (ImGui::SliderFloat("##th", &assign.touch_height, 0.01f, 5.0f,
                               "%.2f")) {
          if (assign.touch_height < 0.01f)
            assign.touch_height = 0.01f;
          if (assign.touch_height > 5.0f)
            assign.touch_height = 5.0f;
          if (assign.assigned_part >= 0 &&
              assign.assigned_part < (int)w.model.meshes.size()) {
            w.model.meshes[assign.assigned_part].touch_height =
                assign.touch_height;
          }
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Touch area height (world units). Adjust to match "
                            "your physical controller.");
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("N/A");
      }

      ImGui::TableSetColumnIndex(6);
      ImGui::PushID(i);
      if (ImGui::Button("Highlight")) {
        w.import_preview.selected_mesh_index = i;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Highlight this mesh in the 3D view.");
      if (assign.assigned_part == -1) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Unassigned meshes are saved as separate OBJ files "
                            "in the model folder.");
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  char save_name[64] = {};
  strncpy(save_name, w.import_preview.save_name.c_str(), 63);
  if (ImGui::InputText("Model Name", save_name, 64)) {
    w.import_preview.save_name = save_name;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Name for the new model folder.");

  if (ImGui::Button("Save Model")) {
    SaveImportedModel(w);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Save the imported model with current assignments.");
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    w.import_preview.is_open = false;
    glfwSetWindowShouldClose(w.glfw_window, true);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Close the preview without saving.");
}

// Forward declaration of writeJson (defined in model.cpp)
void writeJson(Model &m, const std::string &path);

// Helper to build a Mesh from an ImportedMesh (no file I/O)
void buildMeshFromImported(Mesh &mesh, const ImportedMesh &imported) {
  // Clear any old GL resources
  if (mesh.vao)
    glDeleteVertexArrays(1, &mesh.vao);
  if (mesh.vbo)
    glDeleteBuffers(1, &mesh.vbo);
  if (mesh.ebo)
    glDeleteBuffers(1, &mesh.ebo);

  // Build vertex data array (8 floats per vertex: pos, normal, texcoord)
  std::vector<float> vertex_data;
  vertex_data.reserve(imported.positions.size() * 8);
  for (size_t i = 0; i < imported.positions.size(); ++i) {
    vertex_data.push_back(imported.positions[i].x);
    vertex_data.push_back(imported.positions[i].y);
    vertex_data.push_back(imported.positions[i].z);
    vertex_data.push_back(imported.normals[i].x);
    vertex_data.push_back(imported.normals[i].y);
    vertex_data.push_back(imported.normals[i].z);
    vertex_data.push_back(imported.texcoords[i].x);
    vertex_data.push_back(imported.texcoords[i].y);
  }

  glGenVertexArrays(1, &mesh.vao);
  glGenBuffers(1, &mesh.vbo);
  glGenBuffers(1, &mesh.ebo);

  glBindVertexArray(mesh.vao);

  glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float),
               vertex_data.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               imported.indices.size() * sizeof(unsigned int),
               imported.indices.data(), GL_STATIC_DRAW);

  mesh.elements = imported.indices.size();

  // Compute bounding box
  if (!imported.positions.empty()) {
    mesh.hasBBox = true;
    mesh.bboxMin = imported.positions[0];
    mesh.bboxMax = imported.positions[0];
    for (const auto &v : imported.positions) {
      mesh.bboxMin.x = std::min(mesh.bboxMin.x, v.x);
      mesh.bboxMin.y = std::min(mesh.bboxMin.y, v.y);
      mesh.bboxMin.z = std::min(mesh.bboxMin.z, v.z);
      mesh.bboxMax.x = std::max(mesh.bboxMax.x, v.x);
      mesh.bboxMax.y = std::max(mesh.bboxMax.y, v.y);
      mesh.bboxMax.z = std::max(mesh.bboxMax.z, v.z);
    }
  } else {
    mesh.hasBBox = false;
  }

  glBindVertexArray(0);
}

void SaveImportedModel(controller_window &w) {
  std::string model_name = w.import_preview.save_name;
  if (model_name.empty()) {
    spdlog::error("Model name cannot be empty.");
    return;
  }

  std::string new_model_path = get_models_root() + "/" + model_name;
  try {
    std::filesystem::create_directories(new_model_path);
  } catch (const std::exception &e) {
    spdlog::error("Failed to create directory {}: {}", new_model_path,
                  e.what());
    return;
  }
  spdlog::info("Saving imported model to: {}", new_model_path);

  // Clear any existing meshes
  w.model.meshes.clear();

  // For each assignment, create a Mesh and upload geometry
  for (auto &assign : w.import_preview.assignments) {
    auto it =
        std::find_if(w.import_preview.imported_model.imported_meshes.begin(),
                     w.import_preview.imported_model.imported_meshes.end(),
                     [&](const ImportedMesh &mesh) {
                       return mesh.name == assign.mesh_name;
                     });
    if (it == w.import_preview.imported_model.imported_meshes.end()) {
      spdlog::warn("Imported mesh '{}' not found, skipping.", assign.mesh_name);
      continue;
    }

    std::string safe_name = assign.mesh_name;
    for (char &c : safe_name) {
      if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' ||
          c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        c = '_';
    }
    std::string obj_filename = safe_name + ".obj";
    std::string full_path = new_model_path + "/" + obj_filename;

    // Write the OBJ file
    spdlog::debug("Writing OBJ: {}", full_path);
    writeOBJ(full_path, *it);

    // Check if the file was written
    Mesh mesh;
    if (std::filesystem::exists(full_path)) {
      loadMesh(mesh, full_path);
      if (mesh.elements == 0) {
        spdlog::warn("Failed to load OBJ: {}, falling back to direct import.",
                     full_path);
        buildMeshFromImported(mesh, *it);
      }
    } else {
      spdlog::warn("OBJ file not written: {}, falling back to direct import.",
                   full_path);
      buildMeshFromImported(mesh, *it);
    }

    // Set properties from assignment
    mesh.filename = obj_filename;
    mesh.name = assign.mesh_name;
    mesh.assignedPart = assign.assigned_part;
    mesh.parentIndex = assign.parent_part;
    mesh.stick_max = assign.max_angle;
    mesh.trigger_max = assign.max_angle;
    mesh.touch_width = assign.touch_width;
    mesh.touch_height = assign.touch_height;
    mesh.visible = true;
    mesh.material.alpha = 1.0f;

    w.model.meshes.push_back(std::move(mesh));
    spdlog::debug("Added mesh '{}' with {} vertices.", assign.mesh_name,
                 mesh.elements);
  }

  // Resolve parent part indices
  for (auto &mesh : w.model.meshes) {
    int wantedPart = mesh.parentIndex;
    if (wantedPart < 0) {
      mesh.parentIndex = -1;
      continue;
    }
    int resolved = -1;
    for (int k = 0; k < (int)w.model.meshes.size(); ++k) {
      if (&w.model.meshes[k] == &mesh)
        continue;
      if (w.model.meshes[k].assignedPart == wantedPart) {
        resolved = k;
        break;
      }
    }
    if (resolved == -1) {
      spdlog::warn("Mesh '{}' requested parent part {} but no mesh in this "
                   "model has that part assigned; leaving unparented.",
                   mesh.name, wantedPart);
    }
    mesh.parentIndex = resolved;
  }

  // Write info.json
  w.model.path = new_model_path;
  try {
    writeJson(w.model, new_model_path + "/info.json");
  } catch (const std::exception &e) {
    spdlog::error("Failed to write JSON: {}", e.what());
    return;
  }

  spdlog::info("New model saved successfully: {} meshes.",
               w.model.meshes.size());

  // Close the preview window
  w.is_import_preview = false;
  w.import_preview.is_open = false;
  // Destroy the window properly
  glfwDestroyWindow(w.glfw_window);
  // Remove from windows vector
  for (unsigned i = 0; i < windows.size(); ++i) {
    if (windows[i].ID == w.ID) {
      windows.erase(windows.begin() + i);
      break;
    }
  }
  // Remove the tab
  removeTab(w.ID);
}

void writeOBJ(const std::string &path, const ImportedMesh &mesh) {
  std::ofstream f(path);
  if (!f.is_open()) {
    spdlog::error("Failed to write OBJ: {}", path);
    return;
  }

  spdlog::debug("Writing OBJ with {} vertices, {} indices",
                mesh.positions.size(), mesh.indices.size());

  // Write vertices
  for (auto &p : mesh.positions) {
    f << "v " << p.x << " " << p.y << " " << p.z << "\n";
  }

  // Write normals
  for (auto &n : mesh.normals) {
    f << "vn " << n.x << " " << n.y << " " << n.z << "\n";
  }

  // Write texture coordinates
  for (auto &t : mesh.texcoords) {
    f << "vt " << t.x << " " << t.y << "\n";
  }

  f << "s off\n";

  // Check if we have valid indices
  if (mesh.indices.empty()) {
    spdlog::warn("No indices found in imported mesh '{}'", mesh.name);
    return;
  }

  // Ensure we have a multiple of 3 (triangles)
  size_t numIndices = mesh.indices.size();
  size_t numTriangles = numIndices / 3;

  if (numTriangles * 3 != numIndices) {
    spdlog::warn("Index count {} is not a multiple of 3 for mesh '{}'",
                 numIndices, mesh.name);
    // Just use whatever we have
  }

  // Write faces
  for (size_t i = 0; i < numIndices && i + 2 < numIndices; i += 3) {
    // Get indices safely
    unsigned int idx0 = mesh.indices[i];
    unsigned int idx1 = mesh.indices[i + 1];
    unsigned int idx2 = mesh.indices[i + 2];

    // Check if indices are valid (within vertex range)
    if (idx0 >= mesh.positions.size() || idx1 >= mesh.positions.size() ||
        idx2 >= mesh.positions.size()) {
      spdlog::warn("Invalid face indices in mesh '{}' ({} {} {}), skipping",
                   mesh.name, idx0, idx1, idx2);
      continue;
    }

    // OBJ indices are 1-based
    int i0 = idx0 + 1;
    int i1 = idx1 + 1;
    int i2 = idx2 + 1;

    f << "f " << i0 << "/" << i0 << "/" << i0 << " " << i1 << "/" << i1 << "/"
      << i1 << " " << i2 << "/" << i2 << "/" << i2 << "\n";
  }
}

static std::string getSettingsFilePath() {
  return config_base_path + "/settings.json";
}

// Reserved top-level key in settings.json for app-wide (non-per-window)
// settings - e.g. the logging toggles and input poll interval. Chosen
// to be extremely unlikely to collide with a real tab title; skipped by
// name wherever the per-tab loop iterates root's keys.
static const char *kAppSettingsKey = "__app_settings__";

// ------------------------------------------------------------------
// saveGlobalSettings()
// ------------------------------------------------------------------
static void saveGlobalSettings() {
  json root = json::object();

  for (const auto &t : tabs) {
    controller_window *w = getControllerWindow(t.ID);
    if (!w)
      continue;

    json tab = json::object();
    tab["title"] = t.title;
    tab["model_path"] = w->model.path;

    // Window state
    tab["always_on_top"] = w->always_on_top;
    tab["borderless"] = w->borderless;
    tab["drag_to_move"] = w->drag_to_move;
    tab["scroll_to_resize"] = w->scroll_to_resize;
    tab["grid"] = w->grid;
    tab["wireframe"] = w->wireframe;
    tab["transparent_bg"] = w->transparent_bg;
    tab["click_through"] = w->click_through;

    int ww, hh;
    glfwGetWindowSize(w->glfw_window, &ww, &hh);
    tab["width"] = ww;
    tab["height"] = hh;

    int x, y;
    glfwGetWindowPos(w->glfw_window, &x, &y);
    tab["x_pos"] = x;
    tab["y_pos"] = y;

    tab["swap_interval"] = w->swap_interval;
    tab["frame_cap"] = w->frame_cap;
#if defined(_WIN32)
    tab["overlay_update_interval"] = w->overlay_update_interval;
#endif
    tab["bg_color"] = {w->bg_color[0], w->bg_color[1], w->bg_color[2],
                       w->bg_color[3]};
    tab["freelook"] = w->freelook;

    // Camera
    tab["camera_distance"] = w->camera_distance;
    tab["camera_yaw"] = w->camera_yaw;
    tab["camera_pitch"] = w->camera_pitch;
    tab["camera_roll"] = w->camera_roll;
    tab["move_speed"] = w->move_speed;
    tab["turn_speed"] = w->turn_speed;
    tab["freelook_yaw"] = w->freelook_yaw;
    tab["freelook_pitch"] = w->freelook_pitch;
    tab["freelook_position"] = {w->freelook_position.x, w->freelook_position.y,
                                w->freelook_position.z};

    // Model‑level options (per‑window overrides)
    tab["popup_bumpers"] = w->model.popup_bumpers;
    tab["popup_triggers"] = w->model.popup_triggers;
    tab["popup_paddles"] = w->model.popup_paddles;
    tab["left_stick_deadzone"] =
        (w->model.meshes.size() > 7)
            ? w->model.meshes[7].ring_highlight_deadzone
            : 0;
    tab["right_stick_deadzone"] =
        (w->model.meshes.size() > 8)
            ? w->model.meshes[8].ring_highlight_deadzone
            : 0;

    // Colors
    tab["highlight_color"] = {w->highlight_color[0], w->highlight_color[1],
                              w->highlight_color[2]};

    // Gyro
    tab["gyro_debug_logging"] = w->gyro_debug_logging;
    tab["gyro_enabled"] = w->gyro_enabled;
    tab["reset_gyro_button1"] = w->reset_gyro_button1;
    tab["reset_gyro_button2"] = w->reset_gyro_button2;
    tab["gyro_correction"] = w->gyro_correction;
    tab["gyro_sensitivity"] = w->gyro_sensitivity;

    tab["preferred_guid"] = w->preferred_guid;
    tab["preferred_name"] = w->preferred_name;
    tab["preferred_index"] = w->preferred_index;
    tab["preferred_guid_index"] = w->preferred_guid_index;
    tab["preferred_serial"] = w->preferred_serial;
    tab["preferred_path"] = w->preferred_path;

    tab["network_enabled"] = w->network_enabled;
    tab["network_mode"] = w->network_mode;
    tab["network_ip"] = w->network_ip;
    tab["network_port"] = w->network_port;
    tab["network_protocol"] = w->network_protocol;
    tab["network_send_rate"] = w->network_send_rate;

    // ---- Per-mesh data (highlight override etc.) ----
    json meshes = json::array();
    for (auto &mesh : w->model.meshes) {
      json m;
      m["use_custom_highlight"] = mesh.use_custom_highlight;
      m["custom_highlight_color"] = {mesh.custom_highlight_color[0],
                                     mesh.custom_highlight_color[1],
                                     mesh.custom_highlight_color[2]};
      // Also store other transform data (already in info.json, but we save here
      // as well) We'll rely on info.json for transforms, but we need highlight
      // override saved.
      meshes.push_back(m);
    }
    tab["meshes"] = meshes;

    // ---- Lights ----
    json direct = json::array();
    for (auto &dl : w->direct_lights) {
      json obj;
      obj["name"] = dl.name;
      obj["direction"] = {dl.direction.x, dl.direction.y, dl.direction.z};
      obj["color"] = {dl.color[0], dl.color[1], dl.color[2]};
      direct.push_back(obj);
    }
    tab["direct_lights"] = direct;

    json point = json::array();
    for (auto &pl : w->point_lights) {
      json obj;
      obj["name"] = pl.name;
      obj["hide"] = pl.hide;
      obj["position"] = {pl.position.x, pl.position.y, pl.position.z};
      obj["intensity"] = pl.intensity;
      obj["color"] = {pl.color[0], pl.color[1], pl.color[2]};
      point.push_back(obj);
    }
    tab["point_lights"] = point;

    json spot = json::array();
    for (auto &sl : w->spot_lights) {
      json obj;
      obj["name"] = sl.name;
      obj["hide"] = sl.hide;
      obj["position"] = {sl.position.x, sl.position.y, sl.position.z};
      obj["intensity"] = sl.intensity;
      obj["color"] = {sl.color[0], sl.color[1], sl.color[2]};
      obj["yaw"] = sl.yaw;
      obj["pitch"] = sl.pitch;
      obj["cutoff"] = sl.cutoff;
      obj["outer_cutoff"] = sl.outer_cutoff;
      spot.push_back(obj);
    }
    tab["spot_lights"] = spot;

    // Store under the tab's ID (or title) – we'll use title as key.
    root[t.title] = tab;
  }

  // App-wide settings that aren't tied to any one controller window -
  // stored under a reserved key rather than a new file, and explicitly
  // skipped (by name) in the per-tab loop below when loading, so it's
  // never mistaken for a tab.
  json app_settings = json::object();
  app_settings["log_controller"] = g_log_controller;
  app_settings["log_keyboard"] = g_log_keyboard;
  app_settings["log_mouse"] = g_log_mouse;
  app_settings["input_poll_interval_ms"] = GlobalKeyboard::getPollIntervalMs();
  app_settings["tray_enabled"] = g_tray_enabled;
  app_settings["debug_mode_enabled"] = g_debug_mode_enabled;
  root[kAppSettingsKey] = app_settings;

  std::ofstream f(getSettingsFilePath());
  if (!f) {
    spdlog::error("Failed to open settings.json for writing");
    return;
  }
  f << root.dump(4);
  spdlog::info("Saved global settings to {}", getSettingsFilePath());
}

// ------------------------------------------------------------------
// loadGlobalSettings()
// ------------------------------------------------------------------
static void loadGlobalSettings() {
  // Debug Mode defaults to off, which also means the log level defaults
  // to info rather than whatever main.cpp set it to at startup - applied
  // unconditionally up front so a first run with no settings.json yet
  // still gets the quieter default rather than staying at startup's more
  // verbose level until the user opens Settings and saves once.
  setDebugModeEnabled(false);

  std::ifstream f(getSettingsFilePath());
  if (!f) {
    spdlog::info("No settings.json found – will create on first save.");
    return;
  }

  json root;
  try {
    f >> root;
  } catch (...) {
    spdlog::warn("Failed to parse settings.json – ignoring.");
    return;
  }

  // Delete old settings folder if it still exists
  std::filesystem::remove_all(config_base_path + "/settings");

  // Restore app-wide settings before creating any windows, so e.g. the
  // input poll interval is correct from the very first frame rather
  // than only after the user opens Settings once.
  if (root.contains(kAppSettingsKey)) {
    const json &app_settings = root[kAppSettingsKey];
    g_log_controller = app_settings.value("log_controller", false);
    g_log_keyboard = app_settings.value("log_keyboard", false);
    g_log_mouse = app_settings.value("log_mouse", false);
    GlobalKeyboard::setPollIntervalMs(
        app_settings.value("input_poll_interval_ms", 1));
    g_tray_enabled = app_settings.value("tray_enabled", false);
    if (g_tray_enabled && TrayIcon::isSupported()) {
      if (!TrayIcon::enable())
        g_tray_enabled = false; // creation failed - don't claim it's on
    } else if (!TrayIcon::isSupported()) {
      g_tray_enabled = false; // never true on a platform that can't show it
    }
    setDebugModeEnabled(app_settings.value("debug_mode_enabled", false));
  }

  // We'll create tabs in the order they appear in the JSON
  for (auto &[title, tab] : root.items()) {
    if (title == kAppSettingsKey)
      continue; // not a tab - see saveGlobalSettings() above
    std::string modelPath = tab.value("model_path", "");
    if (modelPath.empty()) {
      spdlog::warn("Tab '{}' has no model_path, skipping.", title);
      continue;
    }

    // Create the window
    createControllerWindow(title, modelPath);
    controller_window *w = getLastWindow();
    if (!w)
      continue;

    // ---- Push a tab for this window ----
    window_tab new_tab;
    new_tab.title = title;
    new_tab.ID = ++tabs_made;
    tabs.push_back(new_tab);
    w->ID = new_tab.ID;

    // ---- Apply all settings ----

    // ---- Try to select preferred device ----
    std::string guid = tab.value("preferred_guid", "");
    std::string name = tab.value("preferred_name", "");
    w->preferred_index = tab.value("preferred_index", -1);
    w->preferred_serial = tab.value("preferred_serial", std::string());
    w->preferred_path = tab.value("preferred_path", std::string());
    w->preferred_guid_index = tab.value("preferred_guid_index", -1);
    w->network_enabled = tab.value("network_enabled", false);
    w->network_mode = tab.value("network_mode", 0);
    w->network_ip = tab.value("network_ip", std::string("127.0.0.1"));
    w->network_port = tab.value("network_port", 5000);
    w->network_protocol = tab.value("network_protocol", 0);
    w->network_send_rate = tab.value("network_send_rate", 60);

    if (w->network_enabled) {
      initNetwork(*w);
    }

    int num_joy = 0;
    SDL_JoystickID *joy_ids = SDL_GetJoysticks(&num_joy);

    auto openDevice = [&](SDL_JoystickID id, int idx) -> bool {
      bool is_game = SDL_IsGamepad(id);
      if (is_game) {
        w->sdl_controller = SDL_OpenGamepad(id);
        if (w->sdl_controller) {
          w->is_gamecontroller = true;
          spdlog::info("Auto‑selected gamecontroller: {}",
                       SDL_GetGamepadName(w->sdl_controller));
          if (w->gyro_enabled) {
            if (SDL_GamepadHasSensor(w->sdl_controller, SDL_SENSOR_GYRO)) {
              SDL_SetGamepadSensorEnabled(w->sdl_controller, SDL_SENSOR_GYRO,
                                          true);
            } else {
              w->gyro_enabled = false;
            }
          }
        } else {
          spdlog::warn("Failed to auto‑open gamecontroller ID {}: {}", id,
                       SDL_GetError());
        }
      } else {
        w->sdl_joystick = SDL_OpenJoystick(id);
        if (w->sdl_joystick) {
          w->is_gamecontroller = false;
          spdlog::info("Auto‑selected generic joystick: {}",
                       SDL_GetJoystickName(w->sdl_joystick));
        } else {
          spdlog::warn("Failed to auto‑open generic joystick ID {}: {}", id,
                       SDL_GetError());
        }
      }
      if (w->sdl_controller || w->sdl_joystick) {
        w->joystick_index = idx;
        // Reset input state (same as manual selection)
        for (int t = 0; t < 4; ++t)
          for (int f = 0; f < 2; ++f) {
            w->touchpad_data[t][f].down = false;
            w->touchpad_data[t][f].x = 0.0f;
            w->touchpad_data[t][f].y = 0.0f;
          }
        for (int j = 0; j < 32; ++j)
          w->last_axis_values[j] = 0.0f;
        for (int j = 0; j < 16; ++j)
          w->last_hat_values[j] = SDL_HAT_CENTERED;
        for (int j = 0; j < 128; ++j)
          w->last_joy_button_values[j] = false;
        for (int j = 0; j < 64; ++j)
          w->last_button_values[j] = false;
        w->gyro_matrix = glm::mat4(1.0f);
        w->gyro_data[0] = w->gyro_data[1] = w->gyro_data[2] = 0.0f;
        w->gyro_time = 0;
        w->gyro_toggled = true;
        w->lastTime = glfwGetTime();

        // Immediately update the saved preferences to reflect the actual open
        // device
        SDL_Joystick *joy = w->sdl_controller
                                ? SDL_GetGamepadJoystick(w->sdl_controller)
                                : w->sdl_joystick;
        if (joy) {
          SDL_GUID dev_guid = SDL_GetJoystickGUID(joy);
          char guid_str[64];
          SDL_GUIDToString(dev_guid, guid_str, sizeof(guid_str));
          w->preferred_guid = guid_str;
          w->preferred_name = SDL_GetJoystickName(joy);
          const char *serial = SDL_GetJoystickSerial(joy);
          const char *path = SDL_GetJoystickPath(joy);
          w->preferred_serial = serial ? serial : "";
          w->preferred_path = path ? path : "";
          // Compute ordinal
          w->preferred_guid_index = 0;
          for (int k = 0; k < num_joy; ++k) {
            if (k == idx)
              continue;
            SDL_JoystickID other_id = joy_ids[k];
            SDL_GUID other_guid = SDL_GetJoystickGUIDForID(other_id);
            char other_guid_str[64];
            SDL_GUIDToString(other_guid, other_guid_str,
                             sizeof(other_guid_str));
            if (strcmp(guid_str, other_guid_str) == 0)
              w->preferred_guid_index++;
          }
          w->preferred_index = idx;
        }
        return true;
      }
      return false;
    };

    // 1. Try exact match by device path first (most reliable)
    bool deviceOpened = false;
    if (!w->preferred_path.empty() && num_joy > 0) {
      for (int i = 0; i < num_joy; ++i) {
        SDL_JoystickID id = joy_ids[i];
        SDL_Joystick *tmpJoy = nullptr;
        SDL_Gamepad *tmpPad = nullptr;
        bool isGame = SDL_IsGamepad(id);
        if (isGame) {
          tmpPad = SDL_OpenGamepad(id);
          if (tmpPad)
            tmpJoy = SDL_GetGamepadJoystick(tmpPad);
          else
            continue;
        } else {
          tmpJoy = SDL_OpenJoystick(id);
        }
        if (tmpJoy) {
          const char *path = SDL_GetJoystickPath(tmpJoy);
          if (path && w->preferred_path == path) {
            spdlog::debug("Device match: path '{}'", path);
            if (openDevice(id, i)) {
              deviceOpened = true;
              break;
            }
          }
          if (!isGame)
            SDL_CloseJoystick(tmpJoy);
          else if (tmpPad)
            SDL_CloseGamepad(tmpPad);
        }
      }
    }

    // 2. Fallback to serial
    if (!deviceOpened && !w->preferred_serial.empty()) {
      for (int i = 0; i < num_joy; ++i) {
        SDL_JoystickID id = joy_ids[i];
        SDL_Joystick *tmpJoy = nullptr;
        SDL_Gamepad *tmpPad = nullptr;
        bool isGame = SDL_IsGamepad(id);
        if (isGame) {
          tmpPad = SDL_OpenGamepad(id);
          if (tmpPad)
            tmpJoy = SDL_GetGamepadJoystick(tmpPad);
          else
            continue;
        } else {
          tmpJoy = SDL_OpenJoystick(id);
        }
        if (tmpJoy) {
          const char *serial = SDL_GetJoystickSerial(tmpJoy);
          if (serial && w->preferred_serial == serial) {
            spdlog::debug("Device match: serial '{}'", serial);
            if (openDevice(id, i)) {
              deviceOpened = true;
              break;
            }
          }
          if (!isGame)
            SDL_CloseJoystick(tmpJoy);
          else if (tmpPad)
            SDL_CloseGamepad(tmpPad);
        }
      }
    }

    // 3. Fallback to GUID (ignore ordinal if only one match exists)
    if (!deviceOpened && !guid.empty()) {
      int guid_matches = 0;
      int first_match_idx = -1;
      for (int i = 0; i < num_joy; ++i) {
        SDL_JoystickID id = joy_ids[i];
        SDL_GUID dev_guid = SDL_GetJoystickGUIDForID(id);
        char guid_str[64];
        SDL_GUIDToString(dev_guid, guid_str, sizeof(guid_str));
        if (guid == guid_str) {
          guid_matches++;
          if (first_match_idx == -1)
            first_match_idx = i;
        }
      }
      if (guid_matches == 1 && first_match_idx != -1) {
        spdlog::debug("Device match: GUID '{}' (unique)", guid);
        if (openDevice(joy_ids[first_match_idx], first_match_idx))
          deviceOpened = true;
      } else if (guid_matches > 1 && w->preferred_guid_index >= 0) {
        // Multiple matches – use ordinal
        int guid_match_count = 0;
        for (int i = 0; i < num_joy; ++i) {
          SDL_JoystickID id = joy_ids[i];
          SDL_GUID dev_guid = SDL_GetJoystickGUIDForID(id);
          char guid_str[64];
          SDL_GUIDToString(dev_guid, guid_str, sizeof(guid_str));
          if (guid == guid_str) {
            if (guid_match_count == w->preferred_guid_index) {
              spdlog::debug("Device match: GUID '{}' (ordinal {})", guid,
                            w->preferred_guid_index);
              if (openDevice(id, i)) {
                deviceOpened = true;
                break;
              }
            }
            guid_match_count++;
          }
        }
      }
    }

    // 4. Fallback to name (unique or ordinal)
    if (!deviceOpened && !name.empty()) {
      int name_matches = 0;
      int first_match_idx = -1;
      for (int i = 0; i < num_joy; ++i) {
        const char *candidate = SDL_GetJoystickNameForID(joy_ids[i]);
        if (candidate && name == candidate) {
          name_matches++;
          if (first_match_idx == -1)
            first_match_idx = i;
        }
      }
      if (name_matches == 1 && first_match_idx != -1) {
        spdlog::debug("Device match: name '{}' (unique)", name);
        if (openDevice(joy_ids[first_match_idx], first_match_idx))
          deviceOpened = true;
      } else if (name_matches > 1 && w->preferred_guid_index >= 0) {
        int name_match_count = 0;
        for (int i = 0; i < num_joy; ++i) {
          const char *candidate = SDL_GetJoystickNameForID(joy_ids[i]);
          if (candidate && name == candidate) {
            if (name_match_count == w->preferred_guid_index) {
              spdlog::debug("Device match: name '{}' (ordinal {})", name,
                            w->preferred_guid_index);
              if (openDevice(joy_ids[i], i)) {
                deviceOpened = true;
                break;
              }
            }
            name_match_count++;
          }
        }
      }
    }

    // 5. Fallback to stored index
    if (!deviceOpened && w->preferred_index >= 0 &&
        w->preferred_index < num_joy) {
      spdlog::debug("Device match: index '{}'", w->preferred_index);
      if (openDevice(joy_ids[w->preferred_index], w->preferred_index))
        deviceOpened = true;
    }

    // 6. Last resort: first device
    if (!deviceOpened && num_joy > 0) {
      spdlog::warn("No saved device matched. Falling back to first device.");
      openDevice(joy_ids[0], 0);
    }

    SDL_free(joy_ids);

    w->always_on_top = tab.value("always_on_top", false);
    glfwSetWindowAttrib(w->glfw_window, GLFW_FLOATING, w->always_on_top);

    w->borderless = tab.value("borderless", false);
    w->transparent_bg = tab.value("transparent_bg", false);
    w->click_through = tab.value("click_through", w->transparent_bg);

    w->drag_to_move = tab.value("drag_to_move", false);
    w->scroll_to_resize = tab.value("scroll_to_resize", false);
    w->grid = tab.value("grid", false);
    w->wireframe = tab.value("wireframe", false);

    // Borderless controls GLFW_DECORATED on the GLFW window - on Windows
    // that's now always hidden behind the layered companion window (see
    // createControllerWindow()/createTransparentOverlay() in
    // controller_window.cpp), so it has no visible effect there (see the
    // Borderless checkbox's tooltip above), only on Linux/macOS. Always
    // on Top controls GLFW_FLOATING. Transparent Background only affects
    // bg_color's alpha (applied via the "bg_color" key below) - the
    // companion window itself was already created unconditionally when
    // createControllerWindow() ran earlier in this loop. Click-Through
    // only affects click passthrough via setWindowClickThrough().
    glfwSetWindowAttrib(w->glfw_window, GLFW_DECORATED, !w->borderless);
    setWindowClickThrough(w->glfw_window, w->click_through);
    int ww = tab.value("width", 640);
    int hh = tab.value("height", 480);
    glfwSetWindowSize(w->glfw_window, ww, hh);

    int x = tab.value("x_pos", 100);
    int y = tab.value("y_pos", 100);
    glfwSetWindowPos(w->glfw_window, x, y);

    w->swap_interval = tab.value("swap_interval", 1);
    w->frame_cap = tab.value("frame_cap", 60);
#if defined(_WIN32)
    w->overlay_update_interval =
        tab.value("overlay_update_interval", 1.0 / 60.0);
#endif

    auto bg =
        tab.value("bg_color", std::array<float, 4>{0.2f, 0.3f, 0.3f, 1.0f});
    w->bg_color[0] = bg[0];
    w->bg_color[1] = bg[1];
    w->bg_color[2] = bg[2];
    w->bg_color[3] = bg[3];

    w->freelook = tab.value("freelook", false);

    w->camera_distance = tab.value("camera_distance", 3.5f);
    w->camera_yaw = tab.value("camera_yaw", 0.0f);
    w->camera_pitch = tab.value("camera_pitch", 89.999f);
    w->camera_roll = tab.value("camera_roll", 0.0f);
    w->move_speed = tab.value("move_speed", 5);
    w->turn_speed = tab.value("turn_speed", 5);
    w->freelook_yaw = tab.value("freelook_yaw", 180.0f);
    w->freelook_pitch = tab.value("freelook_pitch", 0.0f);
    auto fp =
        tab.value("freelook_position", std::array<float, 3>{0.0f, 0.5f, 3.0f});
    w->freelook_position = glm::vec3(fp[0], fp[1], fp[2]);

    w->model.popup_bumpers = tab.value("popup_bumpers", false);
    w->model.popup_triggers = tab.value("popup_triggers", false);
    w->model.popup_paddles = tab.value("popup_paddles", false);

    int ldz = tab.value("left_stick_deadzone", 15);
    if (w->model.meshes.size() > 7)
      w->model.meshes[7].ring_highlight_deadzone = ldz;
    int rdz = tab.value("right_stick_deadzone", 15);
    if (w->model.meshes.size() > 8)
      w->model.meshes[8].ring_highlight_deadzone = rdz;

    auto hc =
        tab.value("highlight_color", std::array<float, 3>{1.0f, 0.0f, 0.0f});
    w->highlight_color[0] = hc[0];
    w->highlight_color[1] = hc[1];
    w->highlight_color[2] = hc[2];

    auto tao =
        tab.value("touch_area_offset", std::array<float, 3>{0.0f, 0.01f, 0.0f});

    w->gyro_debug_logging = tab.value("gyro_debug_logging", false);
    w->gyro_enabled = tab.value("gyro_enabled", false);
    w->reset_gyro_button1 = tab.value("reset_gyro_button1", -1);
    w->reset_gyro_button2 = tab.value("reset_gyro_button2", -1);
    w->gyro_correction = tab.value("gyro_correction", 5);
    w->gyro_sensitivity = tab.value("gyro_sensitivity", 5.0f);

    // ---- Per-mesh highlight override ----
    if (tab.contains("meshes") && tab["meshes"].is_array()) {
      auto &meshArr = tab["meshes"];
      for (size_t i = 0; i < meshArr.size() && i < w->model.meshes.size();
           ++i) {
        auto &m = meshArr[i];
        w->model.meshes[i].use_custom_highlight =
            m.value("use_custom_highlight", false);
        auto col = m.value("custom_highlight_color",
                           std::array<float, 3>{1.0f, 0.0f, 0.0f});
        w->model.meshes[i].custom_highlight_color[0] = col[0];
        w->model.meshes[i].custom_highlight_color[1] = col[1];
        w->model.meshes[i].custom_highlight_color[2] = col[2];
      }
    }

    // ---- Lights ----
    w->direct_lights.clear();
    for (auto &dl : tab["direct_lights"]) {
      direct_light d;
      d.name = dl.value("name", "Directional Light");
      auto dir =
          dl.value("direction", std::array<float, 3>{0.25f, -1.0f, 0.0f});
      d.direction = glm::vec3(dir[0], dir[1], dir[2]);
      auto col = dl.value("color", std::array<float, 3>{1.0f, 1.0f, 1.0f});
      d.color[0] = col[0];
      d.color[1] = col[1];
      d.color[2] = col[2];
      w->direct_lights.push_back(d);
    }

    w->point_lights.clear();
    for (auto &pl : tab["point_lights"]) {
      point_light p;
      p.name = pl.value("name", "Point Light");
      p.hide = pl.value("hide", false);
      auto pos = pl.value("position", std::array<float, 3>{0.0f, 0.0f, 0.0f});
      p.position = glm::vec3(pos[0], pos[1], pos[2]);
      p.intensity = pl.value("intensity", 0.5f);
      auto col = pl.value("color", std::array<float, 3>{1.0f, 1.0f, 1.0f});
      p.color[0] = col[0];
      p.color[1] = col[1];
      p.color[2] = col[2];
      p.ambient =
          glm::vec3(p.color[0] * 0.05f, p.color[1] * 0.05f, p.color[2] * 0.05f);
      p.diffuse =
          glm::vec3(p.color[0] * 0.8f, p.color[1] * 0.8f, p.color[2] * 0.8f);
      p.specular = glm::vec3(p.color[0], p.color[1], p.color[2]);
      w->point_lights.push_back(p);
    }

    w->spot_lights.clear();
    for (auto &sl : tab["spot_lights"]) {
      spot_light s;
      s.name = sl.value("name", "Spot Light");
      s.hide = sl.value("hide", false);
      auto pos = sl.value("position", std::array<float, 3>{0.0f, 0.0f, 2.0f});
      s.position = glm::vec3(pos[0], pos[1], pos[2]);
      s.intensity = sl.value("intensity", 0.5f);
      auto col = sl.value("color", std::array<float, 3>{1.0f, 1.0f, 1.0f});
      s.color[0] = col[0];
      s.color[1] = col[1];
      s.color[2] = col[2];
      s.ambient =
          glm::vec3(s.color[0] * 0.05f, s.color[1] * 0.05f, s.color[2] * 0.05f);
      s.diffuse =
          glm::vec3(s.color[0] * 0.8f, s.color[1] * 0.8f, s.color[2] * 0.8f);
      s.specular = glm::vec3(s.color[0], s.color[1], s.color[2]);
      s.yaw = sl.value("yaw", 0.0f);
      s.pitch = sl.value("pitch", 0.0f);
      s.direction.x =
          cos(glm::radians(s.pitch)) * sin(glm::radians(s.yaw + 180));
      s.direction.y = sin(glm::radians(s.pitch));
      s.direction.z =
          cos(glm::radians(s.pitch)) * cos(glm::radians(s.yaw + 180));
      s.cutoff = sl.value("cutoff", 20.0f);
      s.outer_cutoff = sl.value("outer_cutoff", 50.0f);
      w->spot_lights.push_back(s);
    }

    // Update the tab title (already set, but ensure it's correct)
    tabs.back().title = title;
    glfwSetWindowTitle(w->glfw_window, title.c_str());
  }
}

void saveTabs() { saveGlobalSettings(); }

void loadTabs() {
  // Delete any leftover old‑format settings directory
  std::filesystem::remove_all(config_base_path + "/settings");
  loadGlobalSettings();
}

bool check_filename_valid(const char *name) {
  bool valid = true;
  for (int i = 0; i < 32; i++) {
    for (char c : invalid_characters) {
      if (name[i] == c) {
        valid = false;
        break;
      }
    }
    if (!valid)
      break;
  }
  return valid;
}

std::string get_top_folder(std::string path) {
  std::string delimiter = "/";
  std::string dir = path;
  struct stat sb;
  if (stat(dir.c_str(), &sb) == 0 && (sb.st_mode & S_IFDIR)) {
    size_t pos = 0;
    while ((pos = dir.find(delimiter)) != std::string::npos) {
      dir.erase(0, pos + delimiter.length());
    }
  }
  return dir;
}

std::string get_first_model() {
  std::string dir_path = get_models_root();
  dir_path.append("/");
  std::vector<std::string> model_folders;
  struct stat sb;
  for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
    if (stat(entry.path().string().c_str(), &sb) == 0 &&
        (sb.st_mode & S_IFDIR)) {
      model_folders.push_back(entry.path().string());
    }
  }
  if (model_folders.empty())
    return "";
  return model_folders.front();
}

void OsOpenInShell(const char *path) {
#if defined(__linux__) || defined(__APPLE__)
  // Launch the opener directly via fork/exec instead of system(). This
  // passes `path` as a single argv entry rather than interpolating it into
  // a shell command string, so it can't be affected by shell metacharacters
  // (quotes, `;`, `$()`, etc.) even if `path` ever comes from something
  // less trusted than a hardcoded URL (e.g. a user-controlled file path).
  const char *open_executable =
#if defined(__linux__)
      "xdg-open";
#else
      "open";
#endif
  pid_t pid = fork();
  if (pid == 0) {
    // Child: replace ourselves with the opener; never returns on success.
    execlp(open_executable, open_executable, path, (char *)nullptr);
    _exit(127); // execlp failed
  } else if (pid > 0) {
    // Parent: don't block the UI thread waiting on the opener.
    int status;
    waitpid(pid, &status, WNOHANG);
  } else {
    spdlog::warn("OsOpenInShell: fork() failed for '{}'", path);
  }
#elif defined(_WIN32)
  ::ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWDEFAULT);
#else
  // unsupported platform
  (void)path;
#endif
}
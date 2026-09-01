#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cstring>
#include <nlohmann/json.hpp> // already included, but ensure it's here

#include "controller_window.h"
#include "cube_info.h"
#include "icon_data.h"
#include "keyboard_input.h"
#include "settings.h"
#include "settings_window.h"
#include "shader.h"
#include "shaders.h"
#include <SDL3/SDL_joystick.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <spdlog/spdlog.h>
#include <unordered_map>

#ifdef __linux__ // or just #ifndef _WIN32
#include <fcntl.h>
#endif

extern unsigned selected_tab;
extern int selected_mesh;
extern std::vector<window_tab> tabs;
std::string get_top_folder(std::string path);

extern bool g_log_controller;
extern bool g_log_keyboard;
extern bool g_log_mouse;

extern std::string config_base_path;
extern bool gQuit;
extern GLFWwindow *glfw_settings_window;
extern std::vector<controller_window> windows;

#if defined(_WIN32)
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <windowsx.h>
#endif

// Forward declarations for network functions
void shutdownNetwork(controller_window &w);
void initNetwork(controller_window &w);
void logNetworkMessage(controller_window &w, const std::string &direction,
                       const std::string &address, int port,
                       const std::string &jsonStr);

// ------------------------------------------------------------------
// Set window click-through (mouse passthrough).
//
// This used to try to avoid GLFW_MOUSE_PASSTHROUGH on Windows (which sets
// WS_EX_TRANSPARENT on the GLFW window) by overriding WM_NCHITTEST to
// return HTTRANSPARENT instead. That doesn't work at all: per Microsoft's
// own WM_NCHITTEST documentation, HTTRANSPARENT only forwards the
// hit-test to another window "in the same thread" - it can never route a
// click to a different process, which is exactly what this feature needs
// (clicking through to a game).
//
// It turned out GLFW_MOUSE_PASSTHROUGH/WS_EX_TRANSPARENT on the GLFW
// window directly has a worse problem than the DWM recompositing
// overhead we originally built the WM_NCHITTEST detour to dodge:
// WS_EX_TRANSPARENT makes a window's presentation depend on DWM actively
// resolving a live recompositing dependency against whatever's behind
// it. Windows' Fullscreen Optimizations feature hands control of the
// display to a foreground borderless-fullscreen game and lets DWM step
// back from its normal compositing duties - and with that dependency
// unresolved, input froze completely (confirmed on NVIDIA specifically
// in that mode), which a much simpler earlier version of this app
// (before it had a click-through feature at all, so no WS_EX_TRANSPARENT
// usage anywhere) never did - it just ran slowly there instead.
//
// So on Windows, click-through never touches the GLFW window's own
// WS_EX_TRANSPARENT at all anymore: the layered companion window (see
// createTransparentOverlay(), now created unconditionally for every
// controller window - see createControllerWindow()) is what's actually
// visible and receiving input, and this applies WS_EX_TRANSPARENT to
// THAT window instead, via plain SetWindowLongPtr (it isn't a GLFW
// window, so glfwSetWindowAttrib can't touch it anyway).
// UpdateLayeredWindow-based presentation has no equivalent live DWM
// dependency - we hand Windows a finished bitmap directly - so it isn't
// subject to the same stall. The GLFW_MOUSE_PASSTHROUGH branch below
// only ever runs on Linux/macOS (no companion window there - GLFW's
// native transparent framebuffer works fine, and this class of Windows
// Fullscreen-Optimizations problem doesn't apply), or as a fallback if
// the companion window failed to be created for some reason.
// ------------------------------------------------------------------
void setWindowClickThrough(GLFWwindow *window, bool enable) {
#if defined(_WIN32)
  for (auto &w : windows) {
    if (w.glfw_window == window && w.transparent_overlay_hwnd) {
      HWND hwnd = (HWND)w.transparent_overlay_hwnd;
      LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
      if (enable)
        ex_style |= WS_EX_TRANSPARENT;
      else
        ex_style &= ~WS_EX_TRANSPARENT;
      SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style);
      spdlog::debug("Overlay window click-through set to {}", enable);
      return;
    }
  }
#endif
#if defined(GLFW_MOUSE_PASSTHROUGH) // GLFW 3.4+
  glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH,
                      enable ? GLFW_TRUE : GLFW_FALSE);
  spdlog::debug("Mouse passthrough set to {}", enable);
#else
  (void)window;
  (void)enable;
  spdlog::warn("Click‑through not supported: GLFW < 3.4");
#endif
}

// ------------------------------------------------------------------
// Network helper functions (UDP/TCP sender & receiver)
// ------------------------------------------------------------------
void applyNetworkInputToMeshes(controller_window &w) {
  for (int meshIdx = 0; meshIdx < (int)w.model.meshes.size(); ++meshIdx) {
    Mesh &mesh = w.model.meshes[meshIdx];
    if (mesh.inputBinding.empty())
      continue;

    size_t colon = mesh.inputBinding.find(':');
    if (colon == std::string::npos)
      continue;
    std::string type = mesh.inputBinding.substr(0, colon);
    std::string value = mesh.inputBinding.substr(colon + 1);

    mesh.press = 0.0f;
    mesh.stick_X = 0.0f;
    mesh.stick_Y = 0.0f;
    mesh.pull = 0.0f;
    mesh.highlight_value = 0.0f;
    mesh.touch_state = 0;
    mesh.touch_X = 0.5f;
    mesh.touch_Y = 0.5f;

    if (type == "gamepad" || type == "joystick") {
      bool useRaw = (type == "joystick");
      if (value == "leftstick") {
        float lx = useRaw ? w.net_joystick_axes[0] : w.net_gamepad_axes[0];
        float ly = useRaw ? w.net_joystick_axes[1] : w.net_gamepad_axes[1];
        if (mesh.invert) {
          lx = -lx;
          ly = -ly;
        }
        mesh.stick_X = lx * 32767.0f;
        mesh.stick_Y = ly * 32767.0f;
        mesh.highlight_value = (fabs(lx) > 0.1f || fabs(ly) > 0.1f)
                                   ? std::max(fabs(lx), fabs(ly)) * 1.2f
                                   : 0.0f;
        continue;
      }
      if (value == "rightstick") {
        float rx = useRaw ? w.net_joystick_axes[2] : w.net_gamepad_axes[2];
        float ry = useRaw ? w.net_joystick_axes[3] : w.net_gamepad_axes[3];
        if (mesh.invert) {
          rx = -rx;
          ry = -ry;
        }
        mesh.stick_X = rx * 32767.0f;
        mesh.stick_Y = ry * 32767.0f;
        mesh.highlight_value = (fabs(rx) > 0.1f || fabs(ry) > 0.1f)
                                   ? std::max(fabs(rx), fabs(ry)) * 1.2f
                                   : 0.0f;
        continue;
      }
      // Touchpad bindings
      if (type == "gamepad" && value.rfind("touch", 0) == 0) {
        std::string rest = value.substr(5);
        size_t underscore1 = rest.find('_');
        if (underscore1 != std::string::npos) {
          int touchpadIdx = std::stoi(rest.substr(0, underscore1));
          std::string rest2 = rest.substr(underscore1 + 1);
          size_t underscore2 = rest2.find('_');
          if (underscore2 == std::string::npos) {
            // Combined: touchX_fY
            int fingerIdx = std::stoi(rest2.substr(1));
            if (touchpadIdx >= 0 && touchpadIdx < 4 && fingerIdx >= 0 &&
                fingerIdx < 2) {
              auto &ts = w.touchpad_data[touchpadIdx][fingerIdx];
              if (ts.down) {
                float x = ts.x;
                float y = ts.y;
                if (mesh.invert) {
                  x = 1.0f - x;
                  y = 1.0f - y;
                }
                mesh.touch_X = x;
                mesh.touch_Y = y;
                mesh.touch_state = 1;
                mesh.glow_intensity = 1.0f;
              } else {
                mesh.touch_state = 0;
                mesh.glow_intensity = 0.0f;
              }
            }
          } else {
            int fingerIdx = std::stoi(rest2.substr(0, underscore2));
            char axis = rest2.back();
            if (touchpadIdx >= 0 && touchpadIdx < 4 && fingerIdx >= 0 &&
                fingerIdx < 2) {
              auto &ts = w.touchpad_data[touchpadIdx][fingerIdx];
              if (ts.down) {
                float val = (axis == 'x') ? ts.x : ts.y;
                if (mesh.invert)
                  val = 1.0f - val;
                if (axis == 'x')
                  mesh.touch_X = val;
                else
                  mesh.touch_Y = val;
                mesh.touch_state = 1;
                mesh.glow_intensity = 1.0f;
              } else {
                mesh.touch_state = 0;
                mesh.glow_intensity = 0.0f;
              }
            }
          }
        }
        continue; // skip rest of loop
      }

      if (value.empty())
        continue;
      char prefix = value[0];
      if (prefix == 'b') {
        int num = std::stoi(value.substr(1));
        bool pressed =
            useRaw ? w.net_joystick_buttons[num] : w.net_gamepad_buttons[num];
        if (mesh.invert)
          pressed = !pressed;
        mesh.press = pressed ? 1.0f : 0.0f;
        mesh.highlight_value = mesh.press;
      } else if (prefix == 'a') {
        int num = std::stoi(value.substr(1));
        float axisVal =
            useRaw ? w.net_joystick_axes[num] : w.net_gamepad_axes[num];
        if (mesh.invert)
          axisVal = -axisVal;
        mesh.travel_value = fabs(axisVal);
        mesh.axis_highlight_value = axisVal;
        mesh.highlight_value = fabs(axisVal);
        if (num == 4 || num == 5) { // triggers
          float val = std::max(0.0f, std::min(1.0f, axisVal));
          mesh.pull = val * 32767.0f;
          mesh.press = val;
        }
      }
      // For hat etc., we can skip for now or implement similar.
    } else if (type == "keyboard") {
      // parse key name from value (e.g., "key_w")
      std::string keyName = value.substr(4);
      SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
      for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
        if (strcmp(SDL_GetScancodeName((SDL_Scancode)i), keyName.c_str()) ==
            0) {
          sc = (SDL_Scancode)i;
          break;
        }
      }
      bool pressed = w.net_keyboard_keys.count(sc) > 0;
      if (mesh.invert)
        pressed = !pressed;
      mesh.press = pressed ? 1.0f : 0.0f;
      mesh.highlight_value = mesh.press;
    } else if (type == "mouse") {
      if (value == "mouse_xy" || value == "mouse_x" || value == "mouse_y") {
        float dx = w.net_mouse_dx * w.mouse_sensitivity * 0.005f;
        float dy = w.net_mouse_dy * w.mouse_sensitivity * 0.005f;
        if (mesh.isTouchpoint) {
          mesh.touch_X += dx;
          mesh.touch_Y += dy;
          mesh.touch_X = std::max(0.0f, std::min(1.0f, mesh.touch_X));
          mesh.touch_Y = std::max(0.0f, std::min(1.0f, mesh.touch_Y));
          mesh.touch_state = 1;
          mesh.glow_intensity = 1.0f;
        } else {
          if (value == "mouse_x") {
            mesh.stick_X = std::max(-1.0f, std::min(1.0f, dx)) * 32767.0f;
          } else if (value == "mouse_y") {
            mesh.stick_Y = std::max(-1.0f, std::min(1.0f, dy)) * 32767.0f;
          } else {
            mesh.stick_X = std::max(-1.0f, std::min(1.0f, dx)) * 32767.0f;
            mesh.stick_Y = std::max(-1.0f, std::min(1.0f, dy)) * 32767.0f;
          }
          mesh.highlight_value =
              (fabs(dx) > 0.1f || fabs(dy) > 0.1f) ? 1.0f : 0.0f;
        }
      }
      // mouse buttons
      else if (value.find("mouse_") == 0) {
        int button = -1;
        if (value == "mouse_left")
          button = 0;
        else if (value == "mouse_right")
          button = 1;
        else if (value == "mouse_middle")
          button = 2;
        else if (value == "mouse_4")
          button = 3;
        else if (value == "mouse_5")
          button = 4;
        if (button >= 0 && button < 8) {
          bool pressed = w.net_mouse_buttons[button];
          if (mesh.invert)
            pressed = !pressed;
          mesh.press = pressed ? 1.0f : 0.0f;
          mesh.highlight_value = mesh.press;
        }
      }
    }
  }
}

std::string buildNetworkStateJson(controller_window &w) {
  nlohmann::json j;
  j["type"] = "input";

  // ---- Gamepad (change detection) ----
  if (w.is_gamecontroller) {
    nlohmann::json gp = nlohmann::json::object();
    nlohmann::json changed_buttons = nlohmann::json::array();
    nlohmann::json changed_axes = nlohmann::json::array();

    for (int i = 0; i < 32; ++i) {
      if (w.net_gamepad_buttons[i] != w.last_sent_gamepad_buttons[i]) {
        changed_buttons.push_back(
            {{"index", i}, {"value", w.net_gamepad_buttons[i]}});
        w.last_sent_gamepad_buttons[i] = w.net_gamepad_buttons[i];
      }
    }
    for (int i = 0; i < 8; ++i) {
      if (fabs(w.net_gamepad_axes[i] - w.last_sent_gamepad_axes[i]) > 0.001f) {
        changed_axes.push_back(
            {{"index", i}, {"value", w.net_gamepad_axes[i]}});
        w.last_sent_gamepad_axes[i] = w.net_gamepad_axes[i];
      }
    }

    if (!changed_buttons.empty() || !changed_axes.empty()) {
      gp["buttons"] = changed_buttons;
      gp["axes"] = changed_axes;
      j["gamepad"] = gp;
    }
  }

  // ---- Raw Joystick ----
  if (w.sdl_joystick) {
    nlohmann::json joystick = nlohmann::json::object();
    nlohmann::json changed_buttons = nlohmann::json::array();
    nlohmann::json changed_axes = nlohmann::json::array();

    for (int i = 0; i < 128; ++i) {
      if (w.net_joystick_buttons[i] != w.last_sent_joystick_buttons[i]) {
        changed_buttons.push_back(
            {{"index", i}, {"value", w.net_joystick_buttons[i]}});
        w.last_sent_joystick_buttons[i] = w.net_joystick_buttons[i];
      }
    }
    for (int i = 0; i < 128; ++i) {
      if (fabs(w.net_joystick_axes[i] - w.last_sent_joystick_axes[i]) >
          0.001f) {
        changed_axes.push_back(
            {{"index", i}, {"value", w.net_joystick_axes[i]}});
        w.last_sent_joystick_axes[i] = w.net_joystick_axes[i];
      }
    }

    if (!changed_buttons.empty() || !changed_axes.empty()) {
      joystick["buttons"] = changed_buttons;
      joystick["axes"] = changed_axes;
      j["joystick"] = joystick;
    }
  }

  // ---- Keyboard (diff between current and last sent) ----
  {
    std::set<SDL_Scancode> current_keys = w.net_keyboard_keys;
    std::set<SDL_Scancode> all_keys;
    for (auto k : current_keys)
      all_keys.insert(k);
    for (auto k : w.last_sent_keyboard_keys)
      all_keys.insert(k);

    nlohmann::json changed_keys = nlohmann::json::array();
    for (auto k : all_keys) {
      bool now_pressed = current_keys.count(k) > 0;
      bool prev_pressed = w.last_sent_keyboard_keys.count(k) > 0;
      if (now_pressed != prev_pressed) {
        const char *name = SDL_GetScancodeName(k);
        changed_keys.push_back(
            {{"key", std::string("key_") + name}, {"pressed", now_pressed}});
      }
    }
    if (!changed_keys.empty())
      j["keyboard"] = changed_keys;

    w.last_sent_keyboard_keys = current_keys;
  }

  // ---- Mouse (delta + button changes) ----
  {
    nlohmann::json mouse = nlohmann::json::object();
    bool has_change = false;

    if (fabs(w.net_mouse_dx - w.last_sent_mouse_dx) > 0.001f ||
        fabs(w.net_mouse_dy - w.last_sent_mouse_dy) > 0.001f) {
      mouse["dx"] = w.net_mouse_dx;
      mouse["dy"] = w.net_mouse_dy;
      has_change = true;
    }
    for (int i = 0; i < 8; ++i) {
      if (w.net_mouse_buttons[i] != w.last_sent_mouse_buttons[i]) {
        mouse["buttons"].push_back(
            {{"index", i}, {"value", w.net_mouse_buttons[i]}});
        has_change = true;
      }
    }

    if (has_change) {
      j["mouse"] = mouse;
      w.last_sent_mouse_dx = w.net_mouse_dx;
      w.last_sent_mouse_dy = w.net_mouse_dy;
      memcpy(w.last_sent_mouse_buttons, w.net_mouse_buttons,
             sizeof(w.last_sent_mouse_buttons));
    }
  }

  // ---- Gyro matrix (if enabled and changed) ----
  if (w.gyro_enabled) {
    bool changed = false;
    const float *mat = glm::value_ptr(w.gyro_matrix);
    for (int i = 0; i < 16; ++i) {
      if (fabs(mat[i] - w.last_sent_gyro_matrix[i]) > 0.0001f) {
        changed = true;
        break;
      }
    }
    if (changed) {
      j["gyro_matrix"] = std::vector<float>(mat, mat + 16);
      memcpy(w.last_sent_gyro_matrix, mat, sizeof(w.last_sent_gyro_matrix));
    }
  }

  // ---- Gyro reset (if reset button combo just pressed) ----
  if (w.network_gyro_reset) {
    j["gyro_reset"] = true;
    w.network_gyro_reset = false;
  }

  // ---- Touchpad (finger data changes) ----
  if (w.is_gamecontroller && w.sdl_controller) {
    nlohmann::json touchpads = nlohmann::json::array();
    for (int t = 0; t < 4; ++t) {
      for (int f = 0; f < 2; ++f) {
        auto &ts = w.touchpad_data[t][f];
        bool changed = false;
        if (ts.down != w.last_sent_touchpad_finger[t][f])
          changed = true;
        else if (ts.down &&
                 (fabs(ts.x - w.last_sent_touchpad_x[t][f]) > 0.001f ||
                  fabs(ts.y - w.last_sent_touchpad_y[t][f]) > 0.001f))
          changed = true;

        if (changed) {
          touchpads.push_back({{"touchpad", t},
                               {"finger", f},
                               {"down", ts.down},
                               {"x", ts.x},
                               {"y", ts.y}});
          w.last_sent_touchpad_finger[t][f] = ts.down;
          w.last_sent_touchpad_x[t][f] = ts.x;
          w.last_sent_touchpad_y[t][f] = ts.y;
        }
      }
    }
    if (!touchpads.empty())
      j["touchpads"] = touchpads;
  }

  return j.dump();
}

void applyNetworkStateJson(controller_window &w, const std::string &data) {
  try {
    auto j = nlohmann::json::parse(data);
    if (j["type"] != "input")
      return;

    // ---- Gamepad ----
    if (j.contains("gamepad")) {
      auto gp = j["gamepad"];
      if (gp.contains("buttons") && gp["buttons"].is_array()) {
        for (auto &btn : gp["buttons"]) {
          int idx = btn.value("index", -1);
          if (idx >= 0 && idx < 32)
            w.net_gamepad_buttons[idx] = btn.value("value", false);
        }
      }
      if (gp.contains("axes") && gp["axes"].is_array()) {
        for (auto &ax : gp["axes"]) {
          int idx = ax.value("index", -1);
          if (idx >= 0 && idx < 8)
            w.net_gamepad_axes[idx] = ax.value("value", 0.0f);
        }
      }
    }

    // ---- Joystick ----
    if (j.contains("joystick")) {
      auto js = j["joystick"];
      if (js.contains("buttons") && js["buttons"].is_array()) {
        for (auto &btn : js["buttons"]) {
          int idx = btn.value("index", -1);
          if (idx >= 0 && idx < 128)
            w.net_joystick_buttons[idx] = btn.value("value", false);
        }
      }
      if (js.contains("axes") && js["axes"].is_array()) {
        for (auto &ax : js["axes"]) {
          int idx = ax.value("index", -1);
          if (idx >= 0 && idx < 128)
            w.net_joystick_axes[idx] = ax.value("value", 0.0f);
        }
      }
    }

    // ---- Keyboard (changes) ----
    if (j.contains("keyboard") && j["keyboard"].is_array()) {
      for (auto &key : j["keyboard"]) {
        std::string k = key.value("key", "");
        bool pressed = key.value("pressed", false);
        // Convert "key_w" to scancode
        std::string sc_name = k.substr(4);
        SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
        for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
          if (strcmp(SDL_GetScancodeName((SDL_Scancode)i), sc_name.c_str()) ==
              0) {
            sc = (SDL_Scancode)i;
            break;
          }
        }
        if (sc != SDL_SCANCODE_UNKNOWN) {
          if (pressed)
            w.net_keyboard_keys.insert(sc);
          else
            w.net_keyboard_keys.erase(sc);
        }
      }
    }

    // ---- Mouse ----
    if (j.contains("mouse")) {
      auto mouse = j["mouse"];
      if (mouse.contains("dx"))
        w.net_mouse_dx = mouse["dx"].get<float>();
      if (mouse.contains("dy"))
        w.net_mouse_dy = mouse["dy"].get<float>();
      if (mouse.contains("buttons") && mouse["buttons"].is_array()) {
        for (auto &btn : mouse["buttons"]) {
          int idx = btn.value("index", -1);
          if (idx >= 0 && idx < 8)
            w.net_mouse_buttons[idx] = btn.value("value", false);
        }
      }
    }

    // ---- Gyro matrix ----
    if (j.contains("gyro_matrix") && j["gyro_matrix"].is_array() &&
        j["gyro_matrix"].size() == 16) {
      float mat[16];
      for (int i = 0; i < 16; ++i)
        mat[i] = j["gyro_matrix"][i].get<float>();
      w.gyro_matrix = glm::make_mat4(mat);
    }

    // ---- Gyro reset ----
    if (j.value("gyro_reset", false)) {
      w.gyro_matrix = glm::mat4(1.0f);
    }

    // ---- Touchpad ----
    if (j.contains("touchpads") && j["touchpads"].is_array()) {
      for (auto &tp : j["touchpads"]) {
        int t = tp.value("touchpad", -1);
        int f = tp.value("finger", -1);
        if (t >= 0 && t < 4 && f >= 0 && f < 2) {
          w.touchpad_data[t][f].down = tp.value("down", false);
          w.touchpad_data[t][f].x = tp.value("x", 0.0f);
          w.touchpad_data[t][f].y = tp.value("y", 0.0f);
        }
      }
    }

    // Apply inputs to meshes
    applyNetworkInputToMeshes(w);

    // Reset mouse deltas (they were consumed)
    w.net_mouse_dx = 0.0f;
    w.net_mouse_dy = 0.0f;

  } catch (const std::exception &e) {
    spdlog::warn("Failed to parse network JSON: {}", e.what());
  }
}

void initNetwork(controller_window &w) {
  shutdownNetwork(w); // clean any old sockets

  if (!w.network_enabled)
    return;

  w.network_connected = false;

  if (w.network_protocol == 0) { // UDP
    if (w.network_mode == 0) {   // sender
      w.network_socket = socket(AF_INET, SOCK_DGRAM, 0);
      if (w.network_socket < 0) {
        spdlog::error("UDP socket creation failed");
        return;
      }
      // Enable broadcast if IP is 255.255.255.255
      int broadcast = 1;
      setsockopt(w.network_socket, SOL_SOCKET, SO_BROADCAST,
                 (const char *)&broadcast, sizeof(broadcast));
    } else { // receiver
      w.network_socket = socket(AF_INET, SOCK_DGRAM, 0);
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_port = htons(w.network_port);
      if (bind(w.network_socket, (sockaddr *)&addr, sizeof(addr)) < 0) {
        spdlog::error("UDP bind failed on port {}", w.network_port);
        shutdownNetwork(w);
        return;
      }
// Non-blocking
#ifdef _WIN32
      u_long mode = 1;
      ioctlsocket(w.network_socket, FIONBIO, &mode);
#else
      int flags = fcntl(w.network_socket, F_GETFL, 0);
      fcntl(w.network_socket, F_SETFL, flags | O_NONBLOCK);
#endif
    }
  } else {                     // TCP
    if (w.network_mode == 0) { // sender (connect to receiver)
      w.network_socket = socket(AF_INET, SOCK_STREAM, 0);
      if (w.network_socket < 0) {
        spdlog::error("TCP socket creation failed");
        return;
      }

      // Non-blocking BEFORE connect(), not after: a blocking connect()
      // to an unreachable host (receiver not running yet, wrong IP, a
      // firewall silently dropping the SYN - all routine in exactly
      // the dual-PC setup this feature targets) can block for the OS's
      // full TCP connect timeout, commonly 20+ seconds and longer on
      // some Windows configurations. sendNetworkState() below retries
      // this every 2 seconds while disconnected, and this whole app is
      // single-threaded (see Input()/Draw() in main.cpp) - a blocking
      // connect() here freezes rendering and input polling for that
      // entire duration, every single retry. Switching to non-blocking
      // first makes connect() return immediately with
      // EINPROGRESS/WSAEWOULDBLOCK instead of waiting; actual
      // completion is confirmed later via select() in
      // sendNetworkState(), which never blocks either.
#ifdef _WIN32
      u_long mode = 1;
      ioctlsocket(w.network_socket, FIONBIO, &mode);
#else
      int flags = fcntl(w.network_socket, F_GETFL, 0);
      fcntl(w.network_socket, F_SETFL, flags | O_NONBLOCK);
#endif

      sockaddr_in server_addr{};
      server_addr.sin_family = AF_INET;
      inet_pton(AF_INET, w.network_ip.c_str(), &server_addr.sin_addr);
      server_addr.sin_port = htons(w.network_port);

      int result = connect(w.network_socket, (sockaddr *)&server_addr,
                           sizeof(server_addr));
      bool in_progress;
#ifdef _WIN32
      in_progress = (result < 0 && WSAGetLastError() == WSAEWOULDBLOCK);
#else
      in_progress = (result < 0 && errno == EINPROGRESS);
#endif
      if (result < 0 && !in_progress) {
        spdlog::warn("TCP connect failed immediately, retrying later");
#ifdef _WIN32
        closesocket(w.network_socket);
#else
        close(w.network_socket);
#endif
        w.network_socket = -1;
        return;
      }
      // Connection is now in progress (or, rarely, completed
      // immediately) - sendNetworkState() confirms the actual outcome
      // via select() before ever attempting to send anything.
      w.network_tcp_connected = false;
      w.network_tcp_connecting = true;
    } else { // receiver (listen)
      w.network_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_port = htons(w.network_port);
      if (bind(w.network_listen_socket, (sockaddr *)&addr, sizeof(addr)) < 0) {
        spdlog::error("TCP bind failed on port {}", w.network_port);
        shutdownNetwork(w);
        return;
      }
      if (listen(w.network_listen_socket, 1) < 0) {
        spdlog::error("TCP listen failed");
        shutdownNetwork(w);
        return;
      }
// Non-blocking accept
#ifdef _WIN32
      u_long mode = 1;
      ioctlsocket(w.network_listen_socket, FIONBIO, &mode);
#else
      int flags = fcntl(w.network_listen_socket, F_GETFL, 0);
      fcntl(w.network_listen_socket, F_SETFL, flags | O_NONBLOCK);
#endif
    }
  }
  spdlog::info("Network {} initialized ({}://{}:{})",
               w.network_mode == 0 ? "sender" : "receiver",
               (w.network_protocol == 0 ? "UDP" : "TCP"), w.network_ip.c_str(),
               w.network_port);

  w.network_status =
      (w.network_mode == 0) ? 1 : 1; // both start as 'trying' (1)
}

void shutdownNetwork(controller_window &w) {
  if (w.network_socket >= 0) {
#ifdef _WIN32
    closesocket(w.network_socket);
#else
    close(w.network_socket);
#endif
    w.network_socket = -1;
  }
  if (w.network_listen_socket >= 0) {
#ifdef _WIN32
    closesocket(w.network_listen_socket);
#else
    close(w.network_listen_socket);
#endif
    w.network_listen_socket = -1;
  }
  w.network_tcp_connected = false;
  w.network_tcp_connecting = false;
  w.network_connected = false;
}

void sendNetworkState(controller_window &w) {
  if (!w.network_enabled || w.network_mode != 0)
    return;

  // ---- TCP: poll a non-blocking connect() for completion ----
  // select() with a zero timeout never blocks regardless of outcome -
  // this is what lets initNetwork() kick off connect() without
  // waiting for it, and still find out here whether it succeeded,
  // failed, or is still in progress, one frame at a time.
  if (w.network_protocol == 1 && w.network_tcp_connecting) {
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(w.network_socket, &writefds);
    timeval tv{0, 0};
    int sel =
        select((int)w.network_socket + 1, nullptr, &writefds, nullptr, &tv);
    if (sel > 0 && FD_ISSET(w.network_socket, &writefds)) {
      int err = 0;
      socklen_t len = sizeof(err);
      getsockopt(w.network_socket, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
      w.network_tcp_connecting = false;
      if (err == 0) {
        w.network_tcp_connected = true;
        spdlog::info("TCP connected to {}:{}", w.network_ip, w.network_port);
      } else {
        spdlog::warn("TCP connect failed (error {}), retrying later", err);
#ifdef _WIN32
        closesocket(w.network_socket);
#else
        close(w.network_socket);
#endif
        w.network_socket = -1;
      }
    }
    // Still in progress (sel == 0): nothing to do yet, try again next
    // frame - still never blocking either way.
    return;
  }

  // ---- TCP: retry connection if disconnected ----
  if (w.network_protocol == 1 && w.network_socket < 0) {
    double now = glfwGetTime();
    if (now - w.network_last_reconnect_time > 2.0) {
      w.network_last_reconnect_time = now;
      initNetwork(w);
    }
    return;
  }

  // ---- Poll for incoming packets from the peer ----
  // This is what actually lets the sender detect the receiver being
  // alive - handshake acks, ongoing heartbeat acks (see
  // receiveNetworkState()), or for TCP, just any bytes at all. Must
  // run before the UDP handshake block below, which returns early
  // while waiting for exactly this.
  if (w.network_protocol == 0 && w.network_socket >= 0) {
    char in_buffer[4096];
    for (int i = 0; i < 8; ++i) {
      sockaddr_in from{};
      socklen_t from_len = sizeof(from);
      int len = recvfrom(w.network_socket, in_buffer, sizeof(in_buffer) - 1, 0,
                         (sockaddr *)&from, &from_len);
      if (len <= 0)
        break;
      in_buffer[len] = '\0';
      std::string data(in_buffer);
      if (data.find("\"type\":\"handshake_ack\"") != std::string::npos ||
          data.find("\"type\":\"heartbeat_ack\"") != std::string::npos) {
        w.network_handshake_ack = true;
        w.last_network_receive_time = glfwGetTime();
      }
    }
  } else if (w.network_socket >= 0) {
    char in_buffer[512];
    for (int i = 0; i < 8; ++i) {
      int len = recv(w.network_socket, in_buffer, sizeof(in_buffer) - 1, 0);
      if (len > 0) {
        w.last_network_receive_time = glfwGetTime();
      } else if (len == 0) {
        // Receiver closed the connection gracefully.
        spdlog::info("TCP receiver disconnected");
#ifdef _WIN32
        closesocket(w.network_socket);
#else
        close(w.network_socket);
#endif
        w.network_socket = -1;
        w.network_tcp_connected = false;
        w.network_connected = false;
        return;
      } else {
        break; // no more data available right now
      }
    }
  }

  // ---- UDP handshake ----
  if (w.network_protocol == 0 && !w.network_handshake_ack) {
    // Send handshake every 0.5 seconds until ack received
    double now = glfwGetTime();
    if (now - w.network_last_handshake_sent > 0.5) {
      w.network_last_handshake_sent = now;
      std::string hello = "{\"type\":\"handshake\"}";
      if (w.network_logging)
        logNetworkMessage(w, "HANDSHAKE", w.network_ip, w.network_port, hello);
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      inet_pton(AF_INET, w.network_ip.c_str(), &addr.sin_addr);
      addr.sin_port = htons(w.network_port);
      sendto(w.network_socket, hello.c_str(), hello.size(), 0,
             (sockaddr *)&addr, sizeof(addr));
    }
    return; // no actual data until ack
  }

  // ---- Send actual state ----
  std::string json = buildNetworkStateJson(w);

  if (w.network_logging)
    logNetworkMessage(w, "SEND", w.network_ip, w.network_port, json);

  if (w.network_protocol == 0) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, w.network_ip.c_str(), &addr.sin_addr);
    addr.sin_port = htons(w.network_port);
    sendto(w.network_socket, json.c_str(), json.size(), 0, (sockaddr *)&addr,
           sizeof(addr));
    w.last_network_activity_time = glfwGetTime();
  } else {
    json += "\n";
    send(w.network_socket, json.c_str(), json.size(), 0);
  }

  // ---- Update connection status, with a liveness timeout ----
  // last_network_receive_time only advances when we actually hear back
  // FROM the peer (via the poll above) - if that's gone stale, the
  // peer is presumably dead even though our own socket state doesn't
  // know it yet (this catches an abrupt receiver crash/disconnect that
  // a graceful TCP close or a one-time UDP handshake wouldn't).
  double now = glfwGetTime();
  bool timed_out = w.last_network_receive_time > 0.0 &&
                   now - w.last_network_receive_time >
                       controller_window::kNetworkTimeoutSeconds;
  if (w.network_protocol == 1) { // TCP
    w.network_connected = w.network_tcp_connected && !timed_out;
  } else { // UDP
    w.network_connected = w.network_handshake_ack && !timed_out;
  }
  if (timed_out) {
    spdlog::warn("Receiver timed out (no response for {}s) - marking "
                 "disconnected",
                 controller_window::kNetworkTimeoutSeconds);
    if (w.network_protocol == 0) {
      w.network_handshake_ack = false; // force a fresh handshake
    } else {
#ifdef _WIN32
      closesocket(w.network_socket);
#else
      close(w.network_socket);
#endif
      w.network_socket = -1;
      w.network_tcp_connected = false;
    }
  }
}

void receiveNetworkState(controller_window &w) {
  if (!w.network_enabled || w.network_mode != 1)
    return;
  char buffer[65536];

  if (w.network_protocol == 0) { // UDP
    int processed = 0;
    while (processed < 16) {
      sockaddr_in sender_addr{};
      socklen_t sender_len = sizeof(sender_addr);
      int len = recvfrom(w.network_socket, buffer, sizeof(buffer) - 1, 0,
                         (sockaddr *)&sender_addr, &sender_len);
      if (len > 0) {
        buffer[len] = '\0';
        // Declare once for later use
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(sender_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        int port = ntohs(sender_addr.sin_port);
        if (w.network_logging) {
          logNetworkMessage(w, "RECV", ip_str, port, buffer);
        }

        // Handshake: reply once so the sender's handshake retry loop
        // (see sendNetworkState()) stops and starts sending state.
        // (This function only ever runs for network_mode == 1/receiver
        // - the sender's own handshake_ack detection happens via its
        // own incoming-packet poll in sendNetworkState(), not here.)
        std::string data(buffer);
        if (data.find("\"type\":\"handshake\"") != std::string::npos) {
          std::string ack = "{\"type\":\"handshake_ack\"}";
          sendto(w.network_socket, ack.c_str(), ack.size(), 0,
                 (sockaddr *)&sender_addr, sizeof(sender_addr));
          if (w.network_logging)
            logNetworkMessage(w, "ACK", ip_str, port, ack);
          w.last_network_receive_time = glfwGetTime();
          continue;
        }

        applyNetworkStateJson(w, buffer);
        processed++;

        w.network_connected = true;
        w.last_network_receive_time = glfwGetTime();

        // Echo a lightweight heartbeat ack back on every packet, not
        // just the initial handshake - this is what lets the sender
        // detect an ongoing live connection (see sendNetworkState()'s
        // incoming-packet poll) rather than only ever knowing about
        // the very first handshake reply.
        std::string hb_ack = "{\"type\":\"heartbeat_ack\"}";
        sendto(w.network_socket, hb_ack.c_str(), hb_ack.size(), 0,
               (sockaddr *)&sender_addr, sizeof(sender_addr));

      } else
        break;
    }

    // ---- Liveness timeout: sender went silent (crashed, network
    // dropped, closed the app, etc.) ----
    if (w.network_connected && w.last_network_receive_time > 0.0 &&
        glfwGetTime() - w.last_network_receive_time >
            controller_window::kNetworkTimeoutSeconds) {
      spdlog::warn("UDP sender timed out (no data for {}s) - marking "
                   "disconnected",
                   controller_window::kNetworkTimeoutSeconds);
      w.network_connected = false;
    }
  } else { // TCP
    if (w.network_listen_socket >= 0 && !w.network_tcp_connected) {
      sockaddr_in cli_addr{};
      socklen_t cli_len = sizeof(cli_addr);
      int cli_sock =
          accept(w.network_listen_socket, (sockaddr *)&cli_addr, &cli_len);
      if (cli_sock >= 0) {
        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(cli_addr.sin_addr), peer_ip, INET_ADDRSTRLEN);
        w.network_peer_ip = peer_ip;
        w.network_peer_port = ntohs(cli_addr.sin_port);
        w.network_socket = cli_sock;
        w.network_tcp_connected = true;
        w.network_connected = true;
        w.last_network_receive_time = glfwGetTime();
        w.network_tcp_buffer.clear();
        spdlog::info("TCP client connected");
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(w.network_socket, FIONBIO, &mode);
#else
        int flags = fcntl(w.network_socket, F_GETFL, 0);
        fcntl(w.network_socket, F_SETFL, flags | O_NONBLOCK);
#endif
      }
    }
    if (w.network_socket >= 0 && w.network_tcp_connected) {
      int processed = 0;
      bool peer_disconnected = false;
      while (processed < 16) {
        int len = recv(w.network_socket, buffer, sizeof(buffer) - 1, 0);
        if (len > 0) {
          buffer[len] = '\0';
          if (w.network_logging) {
            logNetworkMessage(w, "RECV", w.network_peer_ip, w.network_peer_port,
                              buffer);
          }
          w.network_tcp_buffer += std::string(buffer, len);
          size_t newline_pos;
          while ((newline_pos = w.network_tcp_buffer.find('\n')) !=
                 std::string::npos) {
            std::string line = w.network_tcp_buffer.substr(0, newline_pos);
            w.network_tcp_buffer.erase(0, newline_pos + 1);
            if (!line.empty()) {
              applyNetworkStateJson(w, line);
              w.last_network_receive_time = glfwGetTime();
              // Heartbeat ack back to the sender on every line
              // processed - lets the sender's own incoming-packet poll
              // (see sendNetworkState()) detect this connection is
              // still alive on an ongoing basis, not just at connect
              // time.
              const char *hb_ack = "{\"type\":\"heartbeat_ack\"}\n";
              send(w.network_socket, hb_ack, strlen(hb_ack), 0);
            }
          }
          processed++;
        } else if (len == 0) {
          // Peer closed the connection gracefully (e.g. the sender's
          // app quit normally) - recv() returning exactly 0 is TCP's
          // way of signaling this, distinct from "no data available
          // right now" (a negative return with EWOULDBLOCK/EAGAIN).
          spdlog::info("TCP sender disconnected");
          peer_disconnected = true;
          break;
        } else {
          break; // no more data available right now
        }
      }
      if (peer_disconnected) {
#ifdef _WIN32
        closesocket(w.network_socket);
#else
        close(w.network_socket);
#endif
        w.network_socket = -1;
        w.network_tcp_connected = false;
        w.network_connected = false;
      } else if (w.network_connected && w.last_network_receive_time > 0.0 &&
                 glfwGetTime() - w.last_network_receive_time >
                     controller_window::kNetworkTimeoutSeconds) {
        // An abrupt disconnect (crash, cable pulled, etc.) won't
        // necessarily show up as recv() == 0 the way a graceful close
        // does - this timeout catches that case too.
        spdlog::warn("TCP sender timed out (no data for {}s) - marking "
                     "disconnected",
                     controller_window::kNetworkTimeoutSeconds);
#ifdef _WIN32
        closesocket(w.network_socket);
#else
        close(w.network_socket);
#endif
        w.network_socket = -1;
        w.network_tcp_connected = false;
        w.network_connected = false;
      }
    }
  }
}
// Helper to log a network message in pretty-printed JSON (if logging enabled)
void logNetworkMessage(controller_window &w, const std::string &direction,
                       const std::string &address, int port,
                       const std::string &jsonStr) {
  if (!w.network_logging)
    return;
  static int count = 0;
  if (++count % 10 != 0) // log every 10th message
    return;
  try {
    auto j = nlohmann::json::parse(jsonStr);
    spdlog::info("Network {} to/from {}:{}:\n{}", direction, address, port,
                 j.dump(2));
  } catch (...) {
    spdlog::info("Network {} to/from {}:{}:\n{}", direction, address, port,
                 jsonStr);
  }
}

#if defined(_WIN32)
namespace {

const wchar_t *kOverlayClassName = L"3dcoPlusTransparentOverlay";
bool g_overlay_class_registered = false;

// Forwards mouse/keyboard input to the real (hidden) GLFW window so all
// existing input handling (drag-to-move, scroll-to-resize, part
// highlighting, pivot dragging, freelook's shift-key check, etc.) keeps
// working completely unmodified - it just reaches the GLFW window
// through this relay instead of directly. Client-area coordinates line
// up because this window is kept the same size and position as the GLFW
// window every frame (see updateTransparentOverlay()). See
// createTransparentOverlay()'s comment below for why this window exists.
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam) {
  switch (msg) {
  case WM_NCHITTEST: {
    controller_window *cw = nullptr;
    for (auto &w : windows) {
      if (w.transparent_overlay_hwnd == (void *)hwnd) {
        cw = &w;
        break;
      }
    }
    // Click-through takes priority over resize - see the comment below
    // on why this returns HTTRANSPARENT uniformly.
    if (cw && cw->click_through)
      return HTTRANSPARENT;

    // Custom resize borders. This window deliberately has no
    // WS_THICKFRAME: adding it draws a real native frame/border, which
    // then visibly conflicts with anything drawn ourselves (this is
    // exactly what went wrong in an earlier attempt - "decorations and
    // the self-made decorations" both showing up at once). Instead,
    // hit-testing a margin near each edge and returning the matching
    // HT* code lets Windows' own native resize-drag take over from
    // there - correct cursor icons, screen-edge snapping, etc., with no
    // visible frame added at all. GetWindowRect and lParam are both in
    // screen coordinates here, so they compare directly.
    if (cw) {
      RECT rect;
      GetWindowRect(hwnd, &rect);
      const int kMargin = 8;
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      bool left = x < rect.left + kMargin;
      bool right = x >= rect.right - kMargin;
      bool top = y < rect.top + kMargin;
      bool bottom = y >= rect.bottom - kMargin;
      if (top && left)
        return HTTOPLEFT;
      if (top && right)
        return HTTOPRIGHT;
      if (bottom && left)
        return HTBOTTOMLEFT;
      if (bottom && right)
        return HTBOTTOMRIGHT;
      if (left)
        return HTLEFT;
      if (right)
        return HTRIGHT;
      if (top)
        return HTTOP;
      if (bottom)
        return HTBOTTOM;
    }
    break; // falls through to DefWindowProcW -> normal HTCLIENT
  }
  case WM_GETMINMAXINFO: {
    // Keep native edge-resize from shrinking the window into something
    // degenerate (a 0- or near-0-sized framebuffer would break
    // rendering and the PBO readback in updateTransparentOverlay()).
    MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO *>(lParam);
    mmi->ptMinTrackSize.x = 100;
    mmi->ptMinTrackSize.y = 100;
    return 0;
  }
  case WM_SIZE: {
    // A native resize-drag (triggered by the WM_NCHITTEST handling
    // above) only changes this window's own size - the actual 3D
    // content is rendered by the separate, hidden GLFW window (see
    // createTransparentOverlay()'s comment), which has no idea this
    // happened. Push the new size onto it here so the next frame
    // actually renders at the new size instead of stretching/clipping
    // whatever was already there. Skip SIZE_MINIMIZED - that reports a
    // degenerate 0x0 size, which isn't a real content size to apply;
    // minimizing is instead handled explicitly via glfwIconifyWindow()
    // (see the Minimize button in settings_window.cpp) and
    // updateTransparentOverlay() hiding this window / skipping the
    // readback entirely while iconified.
    if (wParam != SIZE_MINIMIZED) {
      int new_w = LOWORD(lParam);
      int new_h = HIWORD(lParam);
      for (auto &w : windows) {
        if (w.transparent_overlay_hwnd == (void *)hwnd) {
          glfwSetWindowSize(w.glfw_window, new_w, new_h);
          break;
        }
      }
    }
    break;
  }
  case WM_MOVE: {
    // Resizing from the left or top edge moves this window's top-left
    // corner natively, same reasoning as WM_SIZE above - push it onto
    // the GLFW window so position tracking (drag-to-move, saved
    // tab layout, etc.) stays correct instead of silently drifting out
    // of sync with what's actually on screen.
    int new_x = GET_X_LPARAM(lParam);
    int new_y = GET_Y_LPARAM(lParam);
    for (auto &w : windows) {
      if (w.transparent_overlay_hwnd == (void *)hwnd) {
        glfwSetWindowPos(w.glfw_window, new_x, new_y);
        break;
      }
    }
    break;
  }
  case WM_LBUTTONDOWN:
    // Capture the mouse for the duration of the drag: without this, a
    // fast drag can move the cursor outside this window's current
    // bounds before the window catches up (especially relevant now that
    // Drag to Move actually repositions the window - see
    // controller_window_input()), and WM_MOUSEMOVE would then go to
    // whatever's now under the cursor instead of here, making the drag
    // feel like it "lets go" partway through. SetCapture keeps mouse
    // messages coming to this window regardless of where the cursor
    // physically is until ReleaseCapture (on WM_LBUTTONUP below).
    SetCapture(hwnd);
    goto forward_input_message;
  case WM_LBUTTONUP:
    ReleaseCapture();
    goto forward_input_message;
  case WM_MOUSEMOVE:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_MOUSEWHEEL:
  case WM_MOUSEHWHEEL:
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_CHAR: {
  forward_input_message:
    for (auto &w : windows) {
      if (w.transparent_overlay_hwnd == (void *)hwnd) {
        HWND glfw_hwnd = glfwGetWin32Window(w.glfw_window);
        if (glfw_hwnd)
          SendMessageW(glfw_hwnd, msg, wParam, lParam);
        break;
      }
    }
    break;
  }
  case WM_ERASEBKGND:
    // UpdateLayeredWindow supplies the entire visible surface every
    // frame - letting the default background brush paint here would
    // just fight it.
    return 1;
  case WM_CLOSE:
  case WM_DESTROY:
    // Lifecycle is owned entirely by destroyTransparentOverlay(); this
    // window has no close button/system menu for the user to trigger
    // these from directly, but ignore them defensively either way.
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ensureOverlayClassRegistered() {
  if (g_overlay_class_registered)
    return;
  WNDCLASSW wc = {};
  wc.lpfnWndProc = OverlayWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kOverlayClassName;
  // IDC_ARROW expands to the ANSI (LPSTR) macro form here since this
  // project doesn't define UNICODE/_UNICODE, but LoadCursorW needs a
  // wide string - MAKEINTRESOURCEW(32512) is IDC_ARROW's actual resource
  // ID, used directly to sidestep the ANSI/wide macro ambiguity.
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  // Deliberately NOT CS_OWNDC (unlike GLFW's own window class) - that
  // restriction is the whole reason this window exists. See
  // createTransparentOverlay()'s comment below.
  wc.style = CS_HREDRAW | CS_VREDRAW;
  RegisterClassW(&wc);
  g_overlay_class_registered = true;
}

} // namespace

// ------------------------------------------------------------------
// Real per-pixel window transparency on Windows, via a companion window.
//
// GLFW's own Win32 window class is registered with CS_OWNDC (so each
// window can own a persistent device context for WGL). Per Microsoft's
// documentation, WS_EX_LAYERED - the style that makes per-pixel window
// transparency possible at all - "cannot be used if the window has a
// class style of either CS_OWNDC or CS_CLASSDC." That's a hard Win32
// rule, not a driver bug: WS_EX_LAYERED can never be applied to a
// GLFW-created window, on any GPU vendor. (This is almost certainly why
// glfw/glfw#2681 - the PR that would have added WS_EX_LAYERED support -
// was never merged upstream, and why glfw/glfw#2731, the resulting
// AMD-specific black-background bug report, remains open with no
// accepted fix: DWM's alpha-compositing path GLFW actually ships,
// DwmEnableBlurBehindWindow without WS_EX_LAYERED, apparently isn't
// reliably honored by AMD's driver.)
//
// The way around this: create a second, plain HWND with no CS_OWNDC.
// WS_EX_LAYERED is legal there, so classic UpdateLayeredWindow works
// normally - and because it's pure software compositing (we hand
// Windows a finished bitmap; there's no GPU-vendor-specific DWM alpha
// path involved at all), it's reliable on every GPU vendor rather than
// depending on driver behavior we don't control.
//
// The GLFW window keeps existing and rendering exactly as before - it's
// just hidden. Every frame, updateTransparentOverlay() reads back its
// rendered pixels and blits them into this companion window instead of
// calling glfwSwapBuffers(). OverlayWndProc (above) forwards input
// received here back to the hidden GLFW window, so every existing
// mouse/keyboard-driven feature in this file keeps working unmodified.
// ------------------------------------------------------------------
void createTransparentOverlay(controller_window &w) {
  if (w.transparent_overlay_hwnd)
    return; // already created

  ensureOverlayClassRegistered();

  int x = 0, y = 0, width = 0, height = 0;
  glfwGetWindowPos(w.glfw_window, &x, &y);
  glfwGetWindowSize(w.glfw_window, &width, &height);
  if (width <= 0)
    width = 1;
  if (height <= 0)
    height = 1;

  // WS_EX_NOACTIVATE: clicking this window shouldn't steal foreground
  // focus from whatever game is running underneath - input still
  // reaches it (and gets forwarded), it just doesn't become the
  // active/foreground window on click, matching how an overlay should
  // behave.
  //
  // Title: read back from the (still-existing, just hidden) GLFW
  // window rather than left empty. This companion window is the one
  // actually visible on screen once transparency is active - the GLFW
  // window itself gets hidden a few lines down - so capture tools like
  // OBS's Window Capture source enumerate *this* HWND, and previously
  // saw an empty title (shown by OBS as a bare "null") instead of the
  // controller's actual name (e.g. "Mouse", "FlightStick R").
  wchar_t title_buf[256] = L"";
  GetWindowTextW(glfwGetWin32Window(w.glfw_window), title_buf,
                 static_cast<int>(sizeof(title_buf) / sizeof(title_buf[0])));

  HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_NOACTIVATE, kOverlayClassName, title_buf,
      WS_POPUP, x, y, width, height, nullptr, nullptr,
      GetModuleHandleW(nullptr), nullptr);

  if (!hwnd) {
    spdlog::error(
        "createTransparentOverlay: CreateWindowExW failed (GetLastError={})",
        GetLastError());
    return;
  }

  w.transparent_overlay_hwnd = (void *)hwnd;

  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  SetWindowPos(hwnd, w.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST, x, y,
               width, height, SWP_NOACTIVATE);
  glfwHideWindow(w.glfw_window); // still renders, just not shown directly

  spdlog::info("Created transparent overlay window ({}x{} at {},{})", width,
               height, x, y);
}

void destroyTransparentOverlay(controller_window &w) {
  if (!w.transparent_overlay_hwnd)
    return;
  glfwMakeContextCurrent(w.glfw_window);
  for (int i = 0; i < 2; ++i) {
    if (w.overlay_fence[i]) {
      glDeleteSync(w.overlay_fence[i]);
      w.overlay_fence[i] = nullptr;
    }
    w.overlay_pbo_pending[i] = false;
  }
  if (w.overlay_pbo[0] != 0) {
    glDeleteBuffers(2, w.overlay_pbo);
    w.overlay_pbo[0] = w.overlay_pbo[1] = 0;
  }
  w.overlay_pbo_write_index = 0;

  DestroyWindow((HWND)w.transparent_overlay_hwnd);
  w.transparent_overlay_hwnd = nullptr;
  glfwShowWindow(w.glfw_window);
  spdlog::info("Destroyed transparent overlay window");
}

namespace {
// Converts one BGRA, bottom-up, straight-alpha frame (what OpenGL
// produces) into what UpdateLayeredWindow needs (top-down, premultiplied
// alpha) and blits it. Called from updateTransparentOverlay() once a
// pending PBO read's fence confirms the data is actually ready - never
// from a context where the caller is still waiting on the GPU.
void blitOverlayFrame(HWND hwnd, int width, int height,
                      const unsigned char *src) {
  static std::vector<unsigned char> dst_pixels;
  dst_pixels.resize((size_t)width * height * 4);

  for (int row = 0; row < height; ++row) {
    const unsigned char *s = &src[(size_t)(height - 1 - row) * width * 4];
    unsigned char *dst = &dst_pixels[(size_t)row * width * 4];
    for (int col = 0; col < width; ++col) {
      unsigned char b = s[col * 4 + 0];
      unsigned char g = s[col * 4 + 1];
      unsigned char r = s[col * 4 + 2];
      unsigned char a = s[col * 4 + 3];
      dst[col * 4 + 0] = (unsigned char)((b * a) / 255);
      dst[col * 4 + 1] = (unsigned char)((g * a) / 255);
      dst[col * 4 + 2] = (unsigned char)((r * a) / 255);
      dst[col * 4 + 3] = a;
    }
  }

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height; // negative = top-down DIB
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  HDC screen_dc = GetDC(nullptr);
  HDC mem_dc = CreateCompatibleDC(screen_dc);
  void *bits = nullptr;
  HBITMAP dib =
      CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (dib && bits) {
    memcpy(bits, dst_pixels.data(), dst_pixels.size());
    HBITMAP old_bitmap = (HBITMAP)SelectObject(mem_dc, dib);

    POINT src_pos = {0, 0};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    if (!UpdateLayeredWindow(hwnd, screen_dc, nullptr, &size, mem_dc, &src_pos,
                             0, &blend, ULW_ALPHA)) {
      spdlog::warn("UpdateLayeredWindow failed (GetLastError={})",
                   GetLastError());
    }

    SelectObject(mem_dc, old_bitmap);
    DeleteObject(dib);
  } else {
    spdlog::warn("blitOverlayFrame: CreateDIBSection failed "
                 "(GetLastError={})",
                 GetLastError());
  }
  DeleteDC(mem_dc);
  ReleaseDC(nullptr, screen_dc);
}
} // namespace

void updateTransparentOverlay(controller_window &w) {
  if (!w.transparent_overlay_hwnd)
    return;
  HWND hwnd = (HWND)w.transparent_overlay_hwnd;

  // Defensive safety net: the real GLFW window must never be visible
  // while its companion is active - it's undecorated-transparency-free,
  // so if it ever becomes visible (GLFW's Win32 maximize/restore
  // implementations appear to call ShowWindow() as a side effect of the
  // state transition, undoing glfwHideWindow() - see
  // maximizeControllerWindow()'s comment above), it shows up as a solid
  // black, decorated window on top of/instead of the companion.
  // Checking every frame catches this regardless of which code path
  // caused it, not just the wrapper functions that call
  // glfwHideWindow() explicitly right after the transition.
  if (glfwGetWindowAttrib(w.glfw_window, GLFW_VISIBLE)) {
    glfwHideWindow(w.glfw_window);
  }

  if (w.overlay_minimized) {
    // Uses our own flag rather than GLFW_ICONIFIED - see
    // controller_window::overlay_minimized's declaration in
    // controller_window.h for why GLFW's own iconified tracking
    // couldn't be trusted here. Actually hide the companion window
    // while minimized, rather than just skipping the readback below and
    // leaving it on screen frozen on its last rendered frame.
    // IsWindowVisible check avoids a redundant ShowWindow call every
    // single frame while minimized.
    if (IsWindowVisible(hwnd))
      ShowWindow(hwnd, SW_HIDE);
    return;
  }
  if (!IsWindowVisible(hwnd))
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

  // Throttle overlay updates to reduce DWM load
  double now = glfwGetTime();
  if (now - w.last_overlay_update_time < w.overlay_update_interval)
    return;
  w.last_overlay_update_time = now;

  int width = 0, height = 0;
  glfwGetFramebufferSize(w.glfw_window, &width, &height);
  if (width <= 0 || height <= 0)
    return;

  // The GLFW window remains the single source of truth for position,
  // size, and always-on-top state - every existing drag/resize/settings
  // path already writes to it exactly as before. This window is purely
  // a visible, clickable mirror, kept in sync every frame.
  int wx = 0, wy = 0, ww = 0, wh = 0;
  glfwGetWindowPos(w.glfw_window, &wx, &wy);
  glfwGetWindowSize(w.glfw_window, &ww, &wh);
  SetWindowPos(hwnd, w.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST, wx, wy,
               ww, wh, SWP_NOACTIVATE);

  glfwMakeContextCurrent(w.glfw_window);

  if (w.overlay_pbo[0] == 0)
    glGenBuffers(2, w.overlay_pbo);

  // ---- Consume a previously issued read, if it's ready ----
  // See controller_window::overlay_pbo's declaration in controller_window.h
  // for why this exists at all (short version: NVIDIA can stall a plain
  // glReadPixels into client memory badly enough to freeze the whole app
  // while a fullscreen game has GPU priority - this never blocks).
  int read_index = 1 - w.overlay_pbo_write_index;
  if (w.overlay_pbo_pending[read_index]) {
    GLenum wait_result =
        glClientWaitSync(w.overlay_fence[read_index], 0, 0 /* no wait */);
    if (wait_result == GL_ALREADY_SIGNALED ||
        wait_result == GL_CONDITION_SATISFIED) {
      // Use the width/height THIS SLOT was actually allocated for (set
      // below, in the issue phase, at the time its glBufferData ran) -
      // not the current frame's width/height. The window can be resized
      // between a slot's read being issued and it being consumed here;
      // mapping a range larger than what was actually allocated for
      // that specific buffer fails glMapBufferRange's range validation,
      // which is exactly the "glMapBufferRange failed" warning seen
      // while resizing.
      int slot_w = w.overlay_pbo_width[read_index];
      int slot_h = w.overlay_pbo_height[read_index];
      glBindBuffer(GL_PIXEL_PACK_BUFFER, w.overlay_pbo[read_index]);
      void *mapped =
          glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (size_t)slot_w * slot_h * 4,
                           GL_MAP_READ_BIT);
      if (mapped) {
        blitOverlayFrame(hwnd, slot_w, slot_h,
                         static_cast<const unsigned char *>(mapped));
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
      } else {
        spdlog::warn("updateTransparentOverlay: glMapBufferRange failed "
                     "(slot {}x{})",
                     slot_w, slot_h);
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      glDeleteSync(w.overlay_fence[read_index]);
      w.overlay_fence[read_index] = nullptr;
      w.overlay_pbo_pending[read_index] = false;
    }
    // Not ready yet: leave it pending and check again next frame. The
    // overlay window simply keeps showing whatever it last displayed -
    // Windows doesn't repaint a layered window until UpdateLayeredWindow
    // is called again, so skipping the call is enough; nothing here
    // ever waits for the GPU to catch up.
  }

  // ---- Issue this frame's read, if that slot is free ----
  if (!w.overlay_pbo_pending[w.overlay_pbo_write_index]) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER,
                 w.overlay_pbo[w.overlay_pbo_write_index]);
    // Re-specifying storage every time (rather than reusing a fixed
    // allocation) lets the driver orphan the previous buffer instead of
    // waiting for its contents to be fully consumed first - the normal
    // pattern for streaming PBOs - and transparently handles the window
    // being resized between frames.
    glBufferData(GL_PIXEL_PACK_BUFFER, (size_t)width * height * 4, nullptr,
                 GL_STREAM_READ);
    // Record exactly what this slot was allocated for, so the consume
    // phase above uses the right size even if width/height have since
    // changed (see the comment there).
    w.overlay_pbo_width[w.overlay_pbo_write_index] = width;
    w.overlay_pbo_height[w.overlay_pbo_write_index] = height;
    // GL_BGRA matches what UpdateLayeredWindow/GDI expects, avoiding a
    // channel swap later. The final nullptr means "read into whatever
    // buffer is bound to GL_PIXEL_PACK_BUFFER", not client memory - this
    // is what makes the call non-blocking.
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    w.overlay_fence[w.overlay_pbo_write_index] =
        glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    w.overlay_pbo_pending[w.overlay_pbo_write_index] = true;
    w.overlay_pbo_write_index = 1 - w.overlay_pbo_write_index;
  }
  // Else: both slots are still backlogged (the GPU is very far behind) -
  // skip issuing a new read too rather than piling up more in-flight
  // work; try again next frame.
}
#endif // _WIN32

static GLuint g_glowTexture = 0;
const char *getMouseButtonName(int button) {
  switch (button) {
  case GLFW_MOUSE_BUTTON_LEFT:
    return "Left";
  case GLFW_MOUSE_BUTTON_RIGHT:
    return "Right";
  case GLFW_MOUSE_BUTTON_MIDDLE:
    return "Middle";
  case 4:
    return "Button 4";
  case 5:
    return "Button 5";
  case 6:
    return "Button 6";
  case 7:
    return "Button 7";
  case 8:
    return "Button 8";
  default:
    return nullptr;
  }
}

void createGlowTexture() {
  if (g_glowTexture)
    return;
  const int size = 128;
  std::vector<unsigned char> data(size * size * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      float dx = (x - size / 2.0f) / (size / 2.0f);
      float dy = (y - size / 2.0f) / (size / 2.0f);
      float dist = std::sqrt(dx * dx + dy * dy);
      float alpha = 1.0f - dist;
      if (alpha < 0)
        alpha = 0;
      alpha = alpha * alpha * (3 - 2 * alpha);
      int idx = (y * size + x) * 4;
      data[idx + 0] = 100;
      data[idx + 1] = 180;
      data[idx + 2] = 255;
      data[idx + 3] = (unsigned char)(alpha * 255);
    }
  }
  glGenTextures(1, &g_glowTexture);
  glBindTexture(GL_TEXTURE_2D, g_glowTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// ------------------------------------------------------------------
// Export the current window's manual raw-joystick bindings as a
// standard gamecontrollerdb.txt line, and append it to the on-disk
// file (see ensure_gamecontrollerdb() in settings.cpp - this only
// works because that file is now read from disk every launch, not
// re-extracted from the embedded copy each time).
//
// Only meaningful for raw joystick bindings (type == "joystick"):
// if the device is already recognized as an SDL gamepad
// (is_gamecontroller == true, type == "gamepad"), SDL already has
// some mapping for it and there's nothing useful to contribute here -
// the whole point of this feature is turning a RAW, unrecognized
// joystick into a properly-mapped SDL gamepad other people (and this
// app's own "Gamepad" input type) can use, by sharing this file.
//
// Scope: translates the well-defined 1:1 cases (buttons, single-axis
// triggers, the leftstick/rightstick keyword bindings, cardinal-
// direction hat bindings, paddle/misc buttons). Anything that doesn't
// map cleanly onto a standard gamecontrollerdb key (shells, caps,
// touchpads/touch points, diagonal hat directions) is silently
// skipped rather than guessed at - the exported line is always valid
// syntax, just potentially incomplete for parts with no standard SDL
// equivalent.
// ------------------------------------------------------------------
namespace {
// Appends "<key>:<value>," to the accumulator, only if not already
// present for that key (a part could theoretically be bound more than
// once across meshes - first one wins, rest are skipped with a
// warning rather than producing a key twice, which SDL would reject).
void appendMappingKey(std::string &accum, std::set<std::string> &seen_keys,
                      const std::string &key, const std::string &value) {
  if (seen_keys.count(key)) {
    spdlog::warn("Export Mapping: '{}' already set, skipping duplicate "
                 "binding for it",
                 key);
    return;
  }
  seen_keys.insert(key);
  accum += key + ":" + value + ",";
}

// Parses this app's own inputBinding grammar (see the parsing loop
// this mirrors, further up in this file - "gamepad:"/"joystick:"
// followed by "b<N>", "a<N>"/"a<N>+"/"a<N>-", "h<N>.<0-7>", or the
// "leftstick"/"rightstick" keywords) into gamecontrollerdb key(s) for
// the given semantic role(s). axis_key/axis_key2 are used for the
// leftstick/rightstick case (two keys from one binding); button_key is
// used for everything else (one key).
void translateBindingToMapping(const std::string &inputBinding, bool invert,
                               const char *button_key, const char *axis_key,
                               std::string &accum,
                               std::set<std::string> &seen_keys) {
  size_t colon = inputBinding.find(':');
  if (colon == std::string::npos)
    return;
  std::string type = inputBinding.substr(0, colon);
  std::string value = inputBinding.substr(colon + 1);
  if (type != "joystick")
    return; // only raw joystick bindings are exportable - see comment above

  // "leftstick"/"rightstick" keyword bindings are handled separately by
  // tryStick() in exportGamepadMapping() below, since they resolve to
  // two fixed axis numbers rather than anything parsed from this
  // string - never reached via this function's caller (tryPart) for
  // those two parts.
  if (value.empty() || value == "leftstick" || value == "rightstick")
    return;

  char prefix = value[0];
  if (prefix == 'b' && button_key) {
    appendMappingKey(accum, seen_keys, button_key, "b" + value.substr(1));
  } else if (prefix == 'a' && (button_key || axis_key)) {
    const char *key = axis_key ? axis_key : button_key;
    if (value.back() == '+' || value.back() == '-') {
      // Half-axis-as-button: this app's "a<N>+"/"a<N>-" suffix
      // notation maps to gamecontrollerdb's "+a<N>"/"-a<N>" PREFIX
      // notation for the same concept (a button that's considered
      // pressed when the axis is in that half).
      std::string num = value.substr(1, value.size() - 2);
      std::string sign = (value.back() == '+') ? "+" : "-";
      appendMappingKey(accum, seen_keys, key, sign + "a" + num);
    } else {
      std::string num = value.substr(1);
      appendMappingKey(accum, seen_keys, key, "a" + num + (invert ? "~" : ""));
    }
  } else if (prefix == 'h' && button_key) {
    size_t dot = value.find('.');
    if (dot == std::string::npos)
      return;
    std::string hat_num = value.substr(1, dot - 1);
    int hat_dir = 0;
    try {
      hat_dir = std::stoi(value.substr(dot + 1));
    } catch (...) {
      return;
    }
    // Only the four cardinal directions map to a single, unambiguous
    // gamecontrollerdb hat bitmask - diagonals (1,3,5,7 in this app's
    // 8-direction compass indexing) are combinations SDL synthesizes
    // from the cardinals rather than having their own separate key, so
    // they're skipped here rather than guessed at.
    int bitmask = 0;
    switch (hat_dir) {
    case 0:
      bitmask = 1;
      break; // up
    case 2:
      bitmask = 2;
      break; // right
    case 4:
      bitmask = 4;
      break; // down
    case 6:
      bitmask = 8;
      break; // left
    default:
      spdlog::warn(
          "Export Mapping: diagonal hat direction for '{}' has no single "
          "gamecontrollerdb key - skipping",
          button_key);
      return;
    }
    appendMappingKey(accum, seen_keys, button_key,
                     "h" + hat_num + "." + std::to_string(bitmask));
  }
}
} // namespace

bool exportGamepadMapping(controller_window &w, std::string &out_message) {
  if (!w.sdl_joystick) {
    out_message = "No joystick is open on this window - open one first.";
    return false;
  }
  if (w.is_gamecontroller) {
    out_message =
        "This device is already recognized as a Gamepad by SDL - there's "
        "nothing to export. This feature is for devices bound manually "
        "as a raw Joystick.";
    return false;
  }
  if ((int)w.model.meshes.size() < 1) {
    out_message = "This model has no meshes to export bindings from.";
    return false;
  }

  SDL_GUID guid = SDL_GetJoystickGUID(w.sdl_joystick);
  char guid_str[64] = {};
  SDL_GUIDToString(guid, guid_str, sizeof(guid_str));
  std::string name = SDL_GetJoystickName(w.sdl_joystick)
                         ? SDL_GetJoystickName(w.sdl_joystick)
                         : "Unknown Controller";
  // Commas would break gamecontrollerdb's comma-delimited format.
  std::replace(name.begin(), name.end(), ',', ' ');

  std::string mapping;
  std::set<std::string> seen_keys;

  auto tryPart = [&](int part_index, const char *button_key,
                     const char *axis_key = nullptr) {
    if (part_index < 0 || part_index >= (int)w.model.meshes.size())
      return;
    const Mesh &mesh = w.model.meshes[part_index];
    if (mesh.inputBinding.empty())
      return;
    translateBindingToMapping(mesh.inputBinding, mesh.invert, button_key,
                              axis_key, mapping, seen_keys);
  };

  // ---- Buttons (1:1 with mesh_names indices - see settings_window.cpp) ----
  tryPart(9, "a");
  tryPart(10, "b");
  tryPart(11, "x");
  tryPart(12, "y");
  tryPart(13, "back");
  tryPart(14, "guide");
  tryPart(15, "start");
  tryPart(18, "leftshoulder");
  tryPart(19, "rightshoulder");
  tryPart(7, "leftstick");  // stick click
  tryPart(8, "rightstick"); // stick click
  tryPart(20, "dpup");
  tryPart(21, "dpdown");
  tryPart(22, "dpleft");
  tryPart(23, "dpright");
  tryPart(24, "misc1");
  tryPart(25, "paddle1");
  tryPart(26, "paddle2");
  tryPart(27, "paddle3");
  tryPart(28, "paddle4");

  // ---- Trigger axes ----
  tryPart(3, nullptr, "lefttrigger");
  tryPart(4, nullptr, "righttrigger");

  // ---- Sticks: "leftstick"/"rightstick" keyword bindings hardcode
  // raw axes 0,1 (left) and 2,3 (right) regardless of any number in
  // the binding string - see get_axis_value_choice()'s call sites for
  // "leftstick"/"rightstick" earlier in this file. ----
  auto tryStick = [&](int part_index, const char *x_key, const char *y_key,
                      int x_axis, int y_axis) {
    if (part_index < 0 || part_index >= (int)w.model.meshes.size())
      return;
    const Mesh &mesh = w.model.meshes[part_index];
    if (mesh.inputBinding != "joystick:leftstick" &&
        mesh.inputBinding != "joystick:rightstick")
      return;
    std::string suffix = mesh.invert ? "~" : "";
    appendMappingKey(mapping, seen_keys, x_key,
                     "a" + std::to_string(x_axis) + suffix);
    appendMappingKey(mapping, seen_keys, y_key,
                     "a" + std::to_string(y_axis) + suffix);
  };
  tryStick(5, "leftx", "lefty", 0, 1);
  tryStick(6, "rightx", "righty", 2, 3);

  if (mapping.empty()) {
    out_message = "No raw-joystick bindings found to export. This exports "
                  "bindings set with Input Type = Joystick, not Gamepad.";
    return false;
  }

#if defined(_WIN32)
  const char *platform = "Windows";
#elif defined(__APPLE__)
  const char *platform = "Mac OSX";
#else
  const char *platform = "Linux";
#endif

  std::string line = std::string(guid_str) + "," + name + "," + mapping +
                     "platform:" + platform + ",";

  std::string path = get_gamecontrollerdb_path();
  std::ofstream out(path, std::ios::app);
  if (!out) {
    out_message = "Failed to open " + path + " for appending.";
    return false;
  }
  out << line << "\n";
  out.close();

  // Take effect immediately, without needing a restart.
  int added = SDL_AddGamepadMapping(line.c_str());
  spdlog::info("Exported gamepad mapping for '{}' ({}) to {}: {}", name,
               guid_str, path, line);

  out_message = added >= 0
                    ? "Mapping exported and applied. You can now share "
                      "gamecontrollerdb.txt with others, or send them just "
                      "this line."
                    : "Mapping was appended to the file, but SDL rejected it "
                      "when applying immediately - check the log for "
                      "details. It will still be picked up on next launch.";
  return true;
}

void createTouchAreaRect(controller_window &w) {
  if (!w.touch_area_vao) {
    float vertices[] = {-0.5f, 0.0f, -0.5f, 0.5f,  0.0f, -0.5f,
                        0.5f,  0.0f, 0.5f,  -0.5f, 0.0f, 0.5f};
    unsigned int line_indices[] = {0, 1, 1, 2, 2, 3, 3, 0};
    glGenVertexArrays(1, &w.touch_area_vao);
    glGenBuffers(1, &w.touch_area_vbo);
    glGenBuffers(1, &w.touch_area_ebo);
    glBindVertexArray(w.touch_area_vao);
    glBindBuffer(GL_ARRAY_BUFFER, w.touch_area_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, w.touch_area_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(line_indices), line_indices,
                 GL_STATIC_DRAW);
    w.touch_area_elements = 8;
    glBindVertexArray(0);
  }
}

void logAxisChange(controller_window &w, int axisIdx, float value,
                   const std::string &label) {
  if (g_log_controller && axisIdx < 32) {
    float diff = value - w.last_axis_values[axisIdx];
    if (fabs(diff) > 0.01f) {
      spdlog::info("Axis {} ({}) changed to {:.3f}", axisIdx, label, value);
      w.last_axis_values[axisIdx] = value;
    }
  }
}

void logHatChange(controller_window &w, int hatIdx, Uint8 value) {
  if (g_log_controller && hatIdx < 16) {
    if (value != w.last_hat_values[hatIdx]) {
      const char *dirName = "Center";
      switch (value) {
      case SDL_HAT_UP:
        dirName = "Up";
        break;
      case SDL_HAT_RIGHT:
        dirName = "Right";
        break;
      case SDL_HAT_DOWN:
        dirName = "Down";
        break;
      case SDL_HAT_LEFT:
        dirName = "Left";
        break;
      case SDL_HAT_RIGHTUP:
        dirName = "Right-Up";
        break;
      case SDL_HAT_RIGHTDOWN:
        dirName = "Right-Down";
        break;
      case SDL_HAT_LEFTUP:
        dirName = "Left-Up";
        break;
      case SDL_HAT_LEFTDOWN:
        dirName = "Left-Down";
        break;
      default:
        dirName = "Unknown";
        break;
      }
      spdlog::info("Hat {} changed to {}", hatIdx, dirName);
      w.last_hat_values[hatIdx] = value;
    }
  }
}

std::string button_names[21] = {"south button", "east button",  "west button",
                                "north button", "back button",  "guide button",
                                "start button", "left cap",     "right cap",
                                "left bumper",  "right bumper", "d-pad up",
                                "d-pad down",   "d-pad left",   "d-pad right",
                                "misc",         "paddle 1",     "paddle 2",
                                "paddle 3",     "paddle 4",     "touchpad"};

float grid_vertices[] = {-1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                         0.0f,  0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                         0.0f,  0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};

unsigned int defaultWidth = 640;
unsigned int defaultHeight = 480;
std::vector<controller_window> windows;

float get_axis_value_choice(controller_window &w, int axis_idx, bool useRaw) {
  if (useRaw) {
    SDL_Joystick *joy = nullptr;
    if (w.is_gamecontroller && w.sdl_controller) {
      joy = SDL_GetGamepadJoystick(w.sdl_controller);
    } else if (w.sdl_joystick) {
      joy = w.sdl_joystick;
    }
    if (joy && axis_idx < SDL_GetNumJoystickAxes(joy)) {
      return SDL_GetJoystickAxis(joy, axis_idx) / 32767.0f;
    }
    return 0.0f;
  } else {
    if (w.is_gamecontroller && w.sdl_controller) {
      if (axis_idx >= 0 && axis_idx < 6) {
        return SDL_GetGamepadAxis(w.sdl_controller, (SDL_GamepadAxis)axis_idx) /
               32767.0f;
      }
    }
    return 0.0f;
  }
}

float get_axis_value(controller_window &w, int axis_idx) {
  if (axis_idx < 6) {
    return get_axis_value_choice(w, axis_idx, false);
  } else {
    return get_axis_value_choice(w, axis_idx, true);
  }
}

bool get_button_value_choice(controller_window &w, int btn_idx, bool useRaw) {
  if (useRaw) {
    SDL_Joystick *joy = nullptr;
    if (w.is_gamecontroller && w.sdl_controller) {
      joy = SDL_GetGamepadJoystick(w.sdl_controller);
    } else if (w.sdl_joystick) {
      joy = w.sdl_joystick;
    }
    if (joy && btn_idx < SDL_GetNumJoystickButtons(joy)) {
      return SDL_GetJoystickButton(joy, btn_idx);
    }
    return false;
  } else {
    if (w.is_gamecontroller && w.sdl_controller) {
      if (btn_idx < SDL_GAMEPAD_BUTTON_COUNT) {
        return SDL_GetGamepadButton(w.sdl_controller,
                                    (SDL_GamepadButton)btn_idx);
      }
    }
    return false;
  }
}

static Uint8 getHatValue(controller_window &w, int hatIdx) {
  if (w.sdl_joystick && hatIdx < SDL_GetNumJoystickHats(w.sdl_joystick)) {
    return SDL_GetJoystickHat(w.sdl_joystick, hatIdx);
  }
  return SDL_HAT_CENTERED;
}

// ------------------------------------------------------------------
// Create axis indicator (RGB cross at pivot)
// ------------------------------------------------------------------
void createAxisIndicator(controller_window &w) {
  if (w.axis_vao)
    return;
  float vertices[] = {
      // X axis (red)
      0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      // Y axis (green)
      0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 1.0f, 0.0f,
      // Z axis (blue)
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 1.0f};
  glGenVertexArrays(1, &w.axis_vao);
  glGenBuffers(1, &w.axis_vbo);
  glBindVertexArray(w.axis_vao);
  glBindBuffer(GL_ARRAY_BUFFER, w.axis_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  w.axis_elements = 6; // 3 lines * 2 vertices each
}

void createControllerWindow(std::string title, std::string model_path) {
  controller_window w;
  w.gyro_sensitivity = 5.0f;
  w.logger = spdlog::get("3dco+");

  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
  w.glfw_window =
      glfwCreateWindow(defaultWidth, defaultHeight, title.c_str(), NULL, NULL);
  if (!w.glfw_window) {
    spdlog::error("Failed to create controller window: {}", title);
    glfwTerminate();
    return;
  }
  glfwMakeContextCurrent(w.glfw_window);
  w.window_title = title;
  setWindowClickThrough(w.glfw_window, false); // default: no passthrough

  glEnable(GL_MULTISAMPLE);

  // ---- Diagnostics for transparency issues ----
  // GLFW_TRANSPARENT_FRAMEBUFFER can silently fail depending on the
  // GPU driver / platform combo. If the background renders solid instead
  // of see-through, check this log line: glfw_transparent_attrib=0 means
  // GLFW itself never actually got a transparency-capable framebuffer for
  // this window, no matter what hint we requested — that's the real,
  // authoritative signal (GL_ALPHA_BITS isn't usable here: it's a
  // compatibility-profile-only query that glad doesn't expose for a core
  // profile context).
  {
    int transparent_attrib =
        glfwGetWindowAttrib(w.glfw_window, GLFW_TRANSPARENT_FRAMEBUFFER);
    const char *gl_vendor = (const char *)glGetString(GL_VENDOR);
    const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
#if defined(__linux__)
    int platform = glfwGetPlatform();
    const char *platform_name = platform == GLFW_PLATFORM_WAYLAND ? "Wayland"
                                : platform == GLFW_PLATFORM_X11   ? "X11"
                                                                  : "unknown";
#else
    const char *platform_name = "n/a";
#endif
    spdlog::info("Controller window '{}': platform={} gl_vendor='{}' "
                 "gl_renderer='{}' glfw_transparent_attrib={}",
                 title, platform_name, gl_vendor ? gl_vendor : "?",
                 gl_renderer ? gl_renderer : "?", transparent_attrib);
    // Authoritative signal used to warn the user in the UI (see
    // settings_window.cpp): if GLFW itself reports the framebuffer isn't
    // transparent even right after we requested it, no amount of
    // clear-color/alpha juggling in our draw call will fix it — the
    // driver/display server never granted an alpha-capable surface.
    // (On Windows this no longer matters for Transparent Background
    // itself - see createTransparentOverlay() - but is still relevant on
    // Linux/macOS, which use GLFW's native transparent framebuffer.)
    w.transparency_supported = (transparent_attrib == GLFW_TRUE);
    if (!w.transparency_supported) {
      spdlog::warn("Controller window '{}': transparent framebuffer was NOT "
                   "granted by the driver/display server. \"Transparent "
                   "Background\" will not work as expected on this system.",
                   title);
    }
  }

  GLFWimage images[1];
  images[0].pixels = stbi_load_from_memory(
      Embedded::icon_data, static_cast<int>(Embedded::icon_size),
      &images[0].width, &images[0].height, nullptr, 4);
  if (images[0].pixels) {
    glfwSetWindowIcon(w.glfw_window, 1, images);
    stbi_image_free(images[0].pixels);
  } else {
    spdlog::warn("Could not load embedded icon for controller window");
  }

  glfwSetScrollCallback(w.glfw_window, controller_window_scroll_callback);
  w.lastFrame = glfwGetTime();

  make_grid(w);
  lightingSpecification(w);

  createShader(w.shader, vertex_shader_code.c_str(),
               fragment_shader_code.c_str());
  createShader(w.grid_shader, grid_vertex_shader_code.c_str(),
               grid_fragment_shader_code.c_str());
  createShader(w.light_source_shader, light_source_vertex_shader_code.c_str(),
               light_source_fragment_shader_code.c_str());
  createShader(w.touch_shader, touch_area_vertex_shader_code.c_str(),
               touch_area_fragment_shader_code.c_str());

  direct_light d;
  w.direct_lights.push_back(d);

  loadModel(w.model, model_path);

  if (w.model.meshes.empty()) {
    spdlog::error("Failed to load any meshes for model at '{}'.", model_path);
  } else {
    spdlog::info("Loaded {} meshes from '{}'.", w.model.meshes.size(),
                 model_path);
  }

  w.model_name = get_top_folder(model_path);

  if (model_path == "dummy") {
    spdlog::info("Creating import preview window (no controller)");
    w.sdl_controller = nullptr;
    w.sdl_joystick = nullptr;
    w.is_gamecontroller = false;
    w.gyro_enabled = false;
    w.gyro_sensor = nullptr;
    w.accel_sensor = nullptr;
    w.scroll_to_resize = false;
    w.drag_to_move = false;
    w.freelook = false;
    w.mouse_first_click = true;
  }
  if (model_path != "dummy") {
    int num_joysticks = 0;
    SDL_JoystickID *joy_ids = SDL_GetJoysticks(&num_joysticks);
    if (num_joysticks == 0) {
      spdlog::error("No joysticks found.");
    } else {
      spdlog::info("Found {} joystick(s).", num_joysticks);
      for (int i = 0; i < num_joysticks; ++i) {
        SDL_JoystickID id = joy_ids[i];
        const char *name = SDL_GetJoystickNameForID(id);
        bool is_game = SDL_IsGamepad(id);
        spdlog::debug("Device {}: {} (gamecontroller: {})", i,
                      name ? name : "Unknown", is_game);
      }

      int chosen = 0;
      SDL_JoystickID chosenID = joy_ids[chosen];

      if (SDL_IsGamepad(chosenID)) {
        w.sdl_controller = SDL_OpenGamepad(chosenID);
        w.is_gamecontroller = true;
        w.joystick_index = chosen;
        if (w.sdl_controller) {
          spdlog::info("Opened gamecontroller: {}",
                       SDL_GetGamepadName(w.sdl_controller));
          // Safely enable gyro
          if (SDL_SetGamepadSensorEnabled(w.sdl_controller, SDL_SENSOR_GYRO,
                                          true) == 0) {
            spdlog::info("Controller has gyro: true");
            w.gyro_enabled = true;
            // Optionally read a first sample to verify, but don't rely on it.
            float dummy[3];
            if (SDL_GetGamepadSensorData(w.sdl_controller, SDL_SENSOR_GYRO,
                                         dummy, 3) == 0) {
              w.gyro_toggled = true; // first read will set time properly
            } else {
              spdlog::warn("Initial gyro read failed: {}", SDL_GetError());
            }
          } else {
            spdlog::warn("Failed to enable gyro sensor: {}", SDL_GetError());
          }
          if (SDL_GamepadHasSensor(w.sdl_controller, SDL_SENSOR_GYRO)) {
            if (SDL_SetGamepadSensorEnabled(w.sdl_controller, SDL_SENSOR_GYRO,
                                            true) == 0) {
              spdlog::info("Gyro sensor enabled successfully.");
              w.gyro_enabled = true;
              // Verify it's actually enabled
              if (SDL_GamepadSensorEnabled(w.sdl_controller, SDL_SENSOR_GYRO)) {
                spdlog::info("Gyro sensor is now active.");
              } else {
                spdlog::warn(
                    "Sensor enabled flag returned false after enabling.");
              }
            } else {
              spdlog::warn("Failed to enable gyro: {}", SDL_GetError());
            }
          } else {
            spdlog::warn("Controller does not have a gyro sensor.");
          }
        } else {
          spdlog::error("Failed to open gamecontroller {}: {}", chosen,
                        SDL_GetError());
        }
      } else {
        w.sdl_joystick = SDL_OpenJoystick(chosenID);
        spdlog::warn("Generic joystick opened – you may need to manually map "
                     "buttons in the Mapping section.");
        w.is_gamecontroller = false;
        w.joystick_index = chosen;
        if (w.sdl_joystick) {
          spdlog::info("Opened generic joystick: {}",
                       SDL_GetJoystickName(w.sdl_joystick));
          SDL_JoystickID joyID = SDL_GetJoystickID(w.sdl_joystick);

          // ---- Sensors ----
          int num_sensors = 0;
          SDL_SensorID *sensor_ids = SDL_GetSensors(&num_sensors);
          if (sensor_ids) {
            for (int s = 0; s < num_sensors; ++s) {
              SDL_SensorID sensorID = sensor_ids[s];
              SDL_Sensor *sensor = SDL_OpenSensor(sensorID);
              if (!sensor)
                continue;
              SDL_SensorType type = SDL_GetSensorType(sensor);
              if (type == SDL_SENSOR_GYRO) {
                if (sensorID == joyID) {
                  w.gyro_sensor = sensor;
                  w.gyro_enabled = true;
                  spdlog::info("Gyro sensor opened (ID {})", sensorID);
                  break;
                } else {
                  SDL_CloseSensor(sensor);
                }
              } else if (type == SDL_SENSOR_ACCEL) {
                if (sensorID == joyID) {
                  w.accel_sensor = sensor;
                  spdlog::info("Accel sensor opened (ID {})", sensorID);
                  break;
                } else {
                  SDL_CloseSensor(sensor);
                }
              } else {
                SDL_CloseSensor(sensor);
              }
            }
            // Fallback: if we didn't find the matching sensor, take the first
            // gyro/accel
            if (!w.gyro_sensor) {
              for (int s = 0; s < num_sensors; ++s) {
                SDL_SensorID sensorID = sensor_ids[s];
                SDL_Sensor *sensor = SDL_OpenSensor(sensorID);
                if (!sensor)
                  continue;
                if (SDL_GetSensorType(sensor) == SDL_SENSOR_GYRO) {
                  w.gyro_sensor = sensor;
                  w.gyro_enabled = true;
                  spdlog::info("Gyro sensor opened by fallback (ID {})",
                               sensorID);
                  break;
                } else {
                  SDL_CloseSensor(sensor);
                }
              }
            }
            if (!w.accel_sensor) {
              for (int s = 0; s < num_sensors; ++s) {
                SDL_SensorID sensorID = sensor_ids[s];
                SDL_Sensor *sensor = SDL_OpenSensor(sensorID);
                if (!sensor)
                  continue;
                if (SDL_GetSensorType(sensor) == SDL_SENSOR_ACCEL) {
                  w.accel_sensor = sensor;
                  spdlog::info("Accel sensor opened by fallback (ID {})",
                               sensorID);
                  break;
                } else {
                  SDL_CloseSensor(sensor);
                }
              }
            }
            SDL_free(sensor_ids);
          }
        } else {
          spdlog::error("Failed to open generic joystick {}: {}", chosen,
                        SDL_GetError());
        }
      }
    }
    SDL_free(joy_ids);
  }

  w.gyro_matrix = glm::mat4(1.0f);

  for (int i = 0; i < 128; ++i) {
    w.last_joy_button_values[i] = false;
  }
  for (int i = 0; i < 64; ++i) {
    w.last_button_values[i] = false;
  }

#if defined(_WIN32)
  createTransparentOverlay(w);
#endif

  windows.push_back(w);
}

void recreateControllerWindow(controller_window *w) {
  if (!w)
    return;
  // Save window state
  std::string title = w->window_title;
  int x, y, width, height;
  glfwGetWindowPos(w->glfw_window, &x, &y);
  glfwGetWindowSize(w->glfw_window, &width, &height);
  bool was_always_on_top = w->always_on_top;
  bool was_borderless = w->borderless;
  int was_swap_interval = w->swap_interval;
  bool was_grid = w->grid;
  bool was_wireframe = w->wireframe;
  float bg_color[4];
  memcpy(bg_color, w->bg_color, 4 * sizeof(float));

#if defined(_WIN32)
  // The companion window is now the Windows default (see
  // createControllerWindow()), so had_transparent_overlay will normally
  // always be true here - this flag mainly guards the rare case where
  // creation originally failed and left transparent_overlay_hwnd null,
  // in which case there's nothing to tear down/recreate and the GLFW
  // window is shown directly as a fallback either way.
  bool had_transparent_overlay = (w->transparent_overlay_hwnd != nullptr);
  if (had_transparent_overlay)
    destroyTransparentOverlay(*w);
#endif

  // Destroy old window (free resources)
  glfwDestroyWindow(w->glfw_window);

  // Recreate with GLFW_TRANSPARENT_FRAMEBUFFER if needed
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,
                 w->transparent_bg ? GLFW_TRUE : GLFW_FALSE);
  w->glfw_window = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
  if (!w->glfw_window) {
    spdlog::error("Failed to recreate controller window with transparency.");
    return;
  }
  glfwMakeContextCurrent(w->glfw_window);
  w->window_title = title;

  glfwSetWindowPos(w->glfw_window, x, y);
  // Borderless controls GLFW_DECORATED, Always on Top controls
  // GLFW_FLOATING. Neither is affected by Transparent Background - on
  // Windows that's handled entirely by the layered companion window
  // (see createTransparentOverlay()), which is always borderless by
  // construction; on Linux/macOS it's GLFW's native transparent
  // framebuffer, which doesn't require undecorated either.
  glfwSetWindowAttrib(w->glfw_window, GLFW_FLOATING, was_always_on_top);
  glfwSetWindowAttrib(w->glfw_window, GLFW_DECORATED, !was_borderless);
  glfwSwapInterval(was_swap_interval);
  w->grid = was_grid;
  w->wireframe = was_wireframe;
  memcpy(w->bg_color, bg_color, 4 * sizeof(float));

#if defined(_WIN32)
  if (had_transparent_overlay)
    createTransparentOverlay(*w);
#endif

  // Apply click-through after all attributes. setWindowClickThrough() is
  // the ONLY place that should ever touch GLFW_MOUSE_PASSTHROUGH - see its
  // definition for why a direct glfwSetWindowAttrib(..., GLFW_MOUSE_
  // PASSTHROUGH, ...) call anywhere else is harmful on Windows.
  setWindowClickThrough(w->glfw_window, w->click_through);

  // Recreate OpenGL resources that depend on the context
  // (shaders, VAOs, etc.) – these were destroyed when the old window was
  // destroyed. The model and its meshes are still in memory; we just need to
  // re‑upload their GL resources.
  for (auto &mesh : w->model.meshes) {
    // Re‑load the mesh from its OBJ file (or re‑upload from stored data)
    // Since we have the mesh data (vertices, indices) still in memory, we can
    // re‑create the buffers. For simplicity, we call loadMesh again – but that
    // would read from disk. A better approach is to store the raw vertex/index
    // data in Mesh and re‑upload. For a quick fix, we can reload from the OBJ
    // file (which still exists).
    if (!mesh.filename.empty()) {
      std::string objPath = w->model.path + "/" + mesh.filename;
      loadMesh(mesh, objPath);
    }
  }
  // Recreate grid, lighting, touch area, etc.
  make_grid(*w);
  lightingSpecification(*w);
  createTouchAreaRect(*w);
  // Recreate shaders
  createShader(w->shader, vertex_shader_code.c_str(),
               fragment_shader_code.c_str());
  createShader(w->grid_shader, grid_vertex_shader_code.c_str(),
               grid_fragment_shader_code.c_str());
  createShader(w->light_source_shader, light_source_vertex_shader_code.c_str(),
               light_source_fragment_shader_code.c_str());
  createShader(w->touch_shader, touch_area_vertex_shader_code.c_str(),
               touch_area_fragment_shader_code.c_str());

  // Restore the window in the windows list (if needed, but we already have the
  // pointer)
  spdlog::info("Recreated window with transparent background = {}",
               w->transparent_bg);
}

void applyMappingToMeshes(controller_window &w, float globalMouseDx,
                          float globalMouseDy, float globalScrollDx,
                          float globalScrollDy) {
  static const std::unordered_map<std::string, SDL_Scancode> keyMap = []() {
    std::unordered_map<std::string, SDL_Scancode> map;
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
      SDL_Scancode sc = static_cast<SDL_Scancode>(i);
      const char *name = SDL_GetScancodeName(sc);
      if (name && strcmp(name, "UNKNOWN") != 0) {
        std::string key = "key_";
        for (const char *p = name; *p; ++p)
          key.push_back(tolower(*p));
        // NOTE: SDL2's scancode name table is not 1:1 - SDL_SCANCODE_RETURN
        // (40) and SDL_SCANCODE_RETURN2 (158) are BOTH named "Return". Since
        // we iterate scancodes in ascending order, use emplace() (first
        // write wins) instead of operator[] (last write wins), so the main
        // Return/Enter key keeps its canonical, lower scancode instead of
        // being silently remapped to RETURN2 - a scancode none of our
        // platform keyboard backends ever actually produce. Without this,
        // "keyboard:key_return" bindings are captured correctly (capture
        // reads the pressed scancode directly) but never highlight during
        // normal use (playback goes through this map).
        map.emplace(key, sc);
      }
    }
    return map;
  }();

  const float MOUSE_SCALE = 0.005f;

  // ---- Early exit if no controller is connected ----
  if (!w.sdl_controller && !w.sdl_joystick) {
    for (auto &mesh : w.model.meshes) {
      // Only reset if the mesh is NOT a keyboard or mouse binding
      if (mesh.inputType != INPUT_TYPE_KEYBOARD &&
          mesh.inputType != INPUT_TYPE_MOUSE) {
        mesh.press = 0.0f;
        mesh.highlight_value = 0.0f;
        mesh.pull = 0.0f;
        mesh.axis_highlight_value = 0.0f;
        mesh.travel_value = 0.0f;
        mesh.travel_signed = 0.0f;
      }
    }
    // Do NOT return; continue so keyboard/mouse meshes get processed.
  }

  for (int meshIdx = 0; meshIdx < (int)w.model.meshes.size(); ++meshIdx) {
    Mesh &mesh = w.model.meshes[meshIdx];
    // Reset axis highlight value – will be set only for axis bindings below
    mesh.axis_highlight_value = 0.0f;
    mesh.travel_value = 0.0f;
    mesh.travel_signed = 0.0f;

    if (mesh.inputBinding.empty())
      continue;

    size_t colon = mesh.inputBinding.find(':');
    if (colon == std::string::npos)
      continue;
    std::string type = mesh.inputBinding.substr(0, colon);
    std::string value = mesh.inputBinding.substr(colon + 1);

    mesh.press = 0.0f;
    mesh.highlight_value = 0.0f;
    mesh.pull = 0.0f;

    // ------------------------------------------------------------------
    // Guard against malformed/incompatible bindings
    // ------------------------------------------------------------------
    try {

      if (type == "gamepad" || type == "joystick") {
        bool useRaw = (type == "joystick");

        if (value == "leftstick") {
          float lx = get_axis_value_choice(w, 0, useRaw);
          float ly = get_axis_value_choice(w, 1, useRaw);
          if (mesh.invert) {
            lx = -lx;
            ly = -ly;
          }
          mesh.stick_X = lx * 32767.0f;
          mesh.stick_Y = ly * 32767.0f;
          mesh.highlight_value = (fabs(lx) > 0.1f || fabs(ly) > 0.1f)
                                     ? std::max(fabs(lx), fabs(ly)) * 1.2f
                                     : 0.0f;

          float magnitude = sqrt(lx * lx + ly * ly);
          if (magnitude > 1.0f)
            magnitude = 1.0f;
          mesh.travel_value = magnitude;
          continue;
        } else if (value == "rightstick") {
          float rx = get_axis_value_choice(w, 2, useRaw);
          float ry = get_axis_value_choice(w, 3, useRaw);
          if (mesh.invert) {
            rx = -rx;
            ry = -ry;
          }
          mesh.stick_X = rx * 32767.0f;
          mesh.stick_Y = ry * 32767.0f;
          mesh.highlight_value = (fabs(rx) > 0.1f || fabs(ry) > 0.1f)
                                     ? std::max(fabs(rx), fabs(ry)) * 1.2f
                                     : 0.0f;
          float magnitude = sqrt(rx * rx + ry * ry);
          if (magnitude > 1.0f)
            magnitude = 1.0f;
          mesh.travel_value = magnitude;
          continue;
        }

        // An empty value ("gamepad:" / "joystick:" with nothing after the
        // colon) has no prefix character to read - bail out instead of
        // indexing value[0] on an empty string.
        if (value.empty())
          continue;

        char prefix = value[0];
        int num = 0, hatDir = -1;
        bool isDirection = false;
        int dir = 0;

        if (prefix == 'b') {
          num = std::stoi(value.substr(1));
          bool pressed = get_button_value_choice(w, num, useRaw);
          if (mesh.invert)
            pressed = !pressed;
          mesh.press = pressed ? 1.0f : 0.0f;
          mesh.highlight_value = pressed ? 1.0f : 0.0f;
          mesh.travel_value = mesh.press;
        } else if (prefix == 'a') {
          if (value.back() == '+') {
            isDirection = true;
            dir = 1;
            num = std::stoi(value.substr(1, value.size() - 2));
          } else if (value.back() == '-') {
            isDirection = true;
            dir = -1;
            num = std::stoi(value.substr(1, value.size() - 2));
          } else {
            num = std::stoi(value.substr(1));
          }
          float axisVal = get_axis_value_choice(w, num, useRaw);
          mesh.travel_signed = axisVal;
          if (mesh.invert)
            axisVal = -axisVal;
          mesh.axis_highlight_value = axisVal;
          mesh.travel_value = fabs(axisVal);

          if (isDirection) {
            bool pressed = (dir > 0) ? (axisVal > 0.5f) : (axisVal < -0.5f);
            mesh.press = pressed ? 1.0f : 0.0f;
            mesh.highlight_value = pressed ? 1.0f : 0.0f;
            mesh.axis_highlight_value =
                pressed ? (dir > 0 ? 1.0f : -1.0f) : 0.0f;
            mesh.travel_signed = pressed ? (dir > 0 ? 1.0f : -1.0f) : 0.0f;
          } else {
            // For non‑trigger axes, we want `press` to be 0 so it does not
            // cause a highlight. Triggers (gamepad axes 4 and 5) still need
            // `press` for their pull animation.
            if (type == "gamepad" && (num == 4 || num == 5)) {
              float val = std::max(0.0f, std::min(1.0f, axisVal));
              mesh.pull = val * 32767.0f;
              mesh.press = val; // used for trigger pull animation
            } else {
              mesh.pull = 0.0f;  // not used for non‑triggers
              mesh.press = 0.0f; // do NOT blend highlight via press
            }
            mesh.axis_highlight_value = axisVal;
            mesh.highlight_value = fabs(axisVal);
          }
        } else if (prefix == 'h') {
          size_t dot = value.find('.');
          if (dot != std::string::npos) {
            num = std::stoi(value.substr(1, dot - 1));
            hatDir = std::stoi(value.substr(dot + 1));
            Uint8 hatVal = getHatValue(w, num);
            Uint8 sdlDir = 0;
            switch (hatDir) {
            case 0:
              sdlDir = SDL_HAT_UP;
              break;
            case 1:
              sdlDir = SDL_HAT_RIGHTUP;
              break;
            case 2:
              sdlDir = SDL_HAT_RIGHT;
              break;
            case 3:
              sdlDir = SDL_HAT_RIGHTDOWN;
              break;
            case 4:
              sdlDir = SDL_HAT_DOWN;
              break;
            case 5:
              sdlDir = SDL_HAT_LEFTDOWN;
              break;
            case 6:
              sdlDir = SDL_HAT_LEFT;
              break;
            case 7:
              sdlDir = SDL_HAT_LEFTUP;
              break;
            }
            bool pressed = (hatVal & sdlDir) != 0;
            mesh.press = pressed ? 1.0f : 0.0f;
            mesh.highlight_value = pressed ? 1.0f : 0.0f;
            mesh.travel_value = mesh.press;
          }
        }

        if (type == "gamepad" && value.rfind("touch", 0) == 0) {
          std::string rest = value.substr(5);
          size_t underscore1 = rest.find('_');
          if (underscore1 != std::string::npos) {
            std::string touchStr = rest.substr(0, underscore1);
            std::string rest2 = rest.substr(underscore1 + 1);
            size_t underscore2 = rest2.find('_');
            if (underscore2 == std::string::npos) {
              std::string fingerStr = rest2;
              int touchpadIdx = std::stoi(touchStr);
              int fingerIdx = std::stoi(fingerStr.substr(1));
              if (touchpadIdx >= 0 && touchpadIdx < 4 && fingerIdx >= 0 &&
                  fingerIdx < 2) {
                auto &ts = w.touchpad_data[touchpadIdx][fingerIdx];
                if (ts.down) {
                  float x = ts.x;
                  float y = ts.y;
                  if (mesh.invert) {
                    x = 1.0f - x;
                    y = 1.0f - y;
                  }
                  mesh.touch_X = x;
                  mesh.touch_Y = y;
                  mesh.touch_state = 1;
                  mesh.glow_intensity = 1.0f;

                  int part = mesh.assignedPart;
                  int touchpadIdxFound = getTouchpadAncestor(w.model, meshIdx);
                  if (touchpadIdxFound == -1) {
                    for (int i = 0; i < (int)w.model.meshes.size(); ++i) {
                      if (i != meshIdx && w.model.meshes[i].isTouchpad) {
                        touchpadIdxFound = i;
                        break;
                      }
                    }
                  }
                  if (touchpadIdxFound != -1 && touchpadIdxFound != meshIdx) {
                    if (mesh.parentIndex != touchpadIdxFound ||
                        mesh.position[0] != 0.0f || mesh.position[1] != 0.0f ||
                        mesh.position[2] != 0.0f) {
                      mesh.parentIndex = touchpadIdxFound;
                      mesh.position[0] = 0.0f;
                      mesh.position[1] = 0.0f;
                      mesh.position[2] = 0.0f;
                      mesh.useCustomScale = false;
                      spdlog::info("Anchored touchpoint '{}' to touchpad '{}'",
                                   mesh.name,
                                   w.model.meshes[touchpadIdxFound].name);
                    }
                  }
                } else {
                  mesh.touch_state = 0;
                  mesh.glow_intensity = 0.0f;
                }
              }
            } else {
              std::string fingerStr = rest2.substr(0, underscore2);
              char axis = rest2.back();
              int touchpadIdx = std::stoi(touchStr);
              int fingerIdx = std::stoi(fingerStr.substr(1));

              int numTouchpads = SDL_GetNumGamepadTouchpads(w.sdl_controller);
              if (touchpadIdx >= numTouchpads)
                continue; // skip this mesh
              int numFingers = SDL_GetNumGamepadTouchpadFingers(
                  w.sdl_controller, touchpadIdx);
              if (fingerIdx >= numFingers)
                continue;

              if (touchpadIdx >= 0 && touchpadIdx < 4 && fingerIdx >= 0 &&
                  fingerIdx < 2) {
                auto &ts = w.touchpad_data[touchpadIdx][fingerIdx];
                if (ts.down) {
                  float val = (axis == 'x') ? ts.x : ts.y;
                  if (mesh.invert)
                    val = 1.0f - val;
                  if (axis == 'x')
                    mesh.touch_X = val;
                  else
                    mesh.touch_Y = val;
                  mesh.touch_state = 1;
                  mesh.glow_intensity = 1.0f;

                  int part = mesh.assignedPart;
                  int touchpadIdxFound = getTouchpadAncestor(w.model, meshIdx);
                  if (touchpadIdxFound == -1) {
                    for (int i = 0; i < (int)w.model.meshes.size(); ++i) {
                      if (i != meshIdx && w.model.meshes[i].isTouchpad) {
                        touchpadIdxFound = i;
                        break;
                      }
                    }
                  }
                  if (touchpadIdxFound != -1 && touchpadIdxFound != meshIdx) {
                    if (mesh.parentIndex != touchpadIdxFound ||
                        mesh.position[0] != 0.0f || mesh.position[1] != 0.0f ||
                        mesh.position[2] != 0.0f) {
                      mesh.parentIndex = touchpadIdxFound;
                      mesh.position[0] = 0.0f;
                      mesh.position[1] = 0.0f;
                      mesh.position[2] = 0.0f;
                      mesh.useCustomScale = false;
                      spdlog::info("Anchored touchpoint '{}' to touchpad '{}'",
                                   mesh.name,
                                   w.model.meshes[touchpadIdxFound].name);
                    }
                  }
                } else {
                  mesh.touch_state = 0;
                  mesh.glow_intensity = 0.0f;
                }
              }
            }
          }
          continue;
        }
      } else if (type == "mouse") {
        // Determine which delta to use based on binding name
        float dx = 0.0f, dy = 0.0f;
        bool isScroll = false;
        if (value == "mouse_xy" || value == "mouse_x" || value == "mouse_y") {
          dx = globalMouseDx;
          dy = globalMouseDy;
          isScroll = false;
        } else if (value == "mouse_scroll_xy" || value == "mouse_scroll_x" ||
                   value == "mouse_scroll_y") {
          dx = globalScrollDx;
          dy = globalScrollDy;
          isScroll = true;
        }

        if (value == "mouse_xy" || value == "mouse_scroll_xy") {
          // ---- Scroll wheel: treat like a button ----
          if (value == "mouse_scroll_xy" || value == "mouse_scroll_x" ||
              value == "mouse_scroll_y") {
            // Scroll-wheel bindings previously tried to show
            // direction/magnitude as a stick-style X/Y position
            // (stick_X/stick_Y/pull) with a continuously-scaled
            // highlight. That didn't read well: these bindings are
            // normally used on a button-shaped mesh, not a joystick,
            // and a partial-brightness highlight looked more like a
            // bug than an intentional effect. This now just lights
            // the mesh fully the moment scroll motion is detected,
            // matching the plain on/off highlight every other button
            // binding uses.
            //
            // Scroll events are discrete per-tick deltas rather than
            // a held value, so w.scroll_accum_magnitude here is reused
            // purely as a short decaying "lit recently" flag (see the
            // decay step further down) rather than an actual
            // accumulated position - without it, a single scroll tick
            // would flash for one frame and be easy to miss.
            bool scrolledX =
                value != "mouse_scroll_y" && fabs(globalScrollDx) > 0.001f;
            bool scrolledY =
                value != "mouse_scroll_x" && fabs(globalScrollDy) > 0.001f;
            if (scrolledX || scrolledY)
              w.scroll_accum_magnitude = 1.0f;

            bool lit = w.scroll_accum_magnitude > 0.05f;
            mesh.press = lit ? 1.0f : 0.0f;
            mesh.highlight_value = lit ? 1.0f : 0.0f;
            continue;
          }

          dx *= w.mouse_sensitivity * MOUSE_SCALE;
          dy *= w.mouse_sensitivity * MOUSE_SCALE;
          bool isTouchPoint = mesh.isTouchpoint;
          if (isTouchPoint) {
            // REMOVED: clamping cap – raw input is passed directly
            mesh.touch_X += dx;
            mesh.touch_Y += dy;
            mesh.touch_X = std::max(0.0f, std::min(1.0f, mesh.touch_X));
            mesh.touch_Y = std::max(0.0f, std::min(1.0f, mesh.touch_Y));
            if (fabs(dx) > 0.0001f || fabs(dy) > 0.0001f)
              w.touchpoint_last_move_time[meshIdx] = glfwGetTime();
            mesh.touch_state = 1;
            mesh.glow_intensity = 1.0f;
            mesh.stick_X = 0.0f;
            mesh.stick_Y = 0.0f;
            mesh.highlight_value =
                (fabs(dx) > 0.01f || fabs(dy) > 0.01f) ? 1.0f : 0.0f;
          } else {
            dx = std::max(-1.0f, std::min(1.0f, dx));
            dy = std::max(-1.0f, std::min(1.0f, dy));
            if (mesh.invert) {
              dx = -dx;
              dy = -dy;
            }
            mesh.stick_X = dx * 32767.0f;
            mesh.stick_Y = dy * 32767.0f;
            mesh.highlight_value = (fabs(dx) > 0.1f || fabs(dy) > 0.1f)
                                       ? std::max(fabs(dx), fabs(dy)) * 1.2f
                                       : 0.0f;
          }
          continue;
        } else if (value == "mouse_x" || value == "mouse_scroll_x") {
          float val = dx * w.mouse_sensitivity * MOUSE_SCALE;
          bool isTouchPoint = mesh.isTouchpoint;
          if (isTouchPoint) {
            // REMOVED: clamping cap
            if (mesh.invert)
              val = -val;
            mesh.touch_X += val;
            mesh.touch_X = std::max(0.0f, std::min(1.0f, mesh.touch_X));
            if (fabs(val) > 0.0001f)
              w.touchpoint_last_move_time[meshIdx] = glfwGetTime();
            mesh.touch_state = 1;
            mesh.glow_intensity = 1.0f;
            mesh.highlight_value = fabs(val) > 0.01f ? 1.0f : 0.0f;
          } else {
            val = std::max(-1.0f, std::min(1.0f, val));
            if (mesh.invert)
              val = -val;
            mesh.stick_X = val * 32767.0f;
            mesh.highlight_value = fabs(val) > 0.1f ? fabs(val) * 1.2f : 0.0f;
          }
          continue;
        } else if (value == "mouse_y" || value == "mouse_scroll_y") {
          float val = dy * w.mouse_sensitivity * MOUSE_SCALE;
          bool isTouchPoint = mesh.isTouchpoint;
          if (isTouchPoint) {
            // REMOVED: clamping cap
            if (mesh.invert)
              val = -val;
            mesh.touch_Y += val;
            mesh.touch_Y = std::max(0.0f, std::min(1.0f, mesh.touch_Y));
            if (fabs(val) > 0.0001f)
              w.touchpoint_last_move_time[meshIdx] = glfwGetTime();
            mesh.touch_state = 1;
            mesh.glow_intensity = 1.0f;
            mesh.highlight_value = fabs(val) > 0.01f ? 1.0f : 0.0f;
          } else {
            val = std::max(-1.0f, std::min(1.0f, val));
            if (mesh.invert)
              val = -val;
            mesh.stick_Y = val * 32767.0f;
            mesh.highlight_value = fabs(val) > 0.1f ? fabs(val) * 1.2f : 0.0f;
          }
          continue;
        } else {
          // Mouse buttons
          int button = -1;
          if (value == "mouse_left")
            button = 0;
          else if (value == "mouse_right")
            button = 1;
          else if (value == "mouse_middle")
            button = 2;
          else if (value == "mouse_4")
            button = 3;
          else if (value == "mouse_5")
            button = 4;
          else if (value == "mouse_6")
            button = 5;
          else if (value == "mouse_7")
            button = 6;
          else if (value == "mouse_8")
            button = 7;
          if (button >= 0 && button < 8) {
            bool pressed = GlobalKeyboard::isMouseButtonPressed(button);
            if (mesh.invert)
              pressed = !pressed;
            mesh.press = pressed ? 1.0f : 0.0f;
            mesh.highlight_value = pressed ? 1.0f : 0.0f;
            mesh.travel_value = mesh.press;
          }
          continue;
        }
      } else if (type == "keyboard") {
        if (value.empty()) {
          mesh.press = 0.0f;
          mesh.highlight_value = 0.0f;
        } else {
          auto it = keyMap.find(value);
          if (it != keyMap.end()) {
            bool pressed = GlobalKeyboard::isPressed(it->second);
            if (mesh.invert)
              pressed = !pressed;
            mesh.press = pressed ? 1.0f : 0.0f;
            mesh.highlight_value = pressed ? 1.0f : 0.0f;
            mesh.travel_value = mesh.press;
          } else {
            static std::set<std::string> warnedKeys;
            if (warnedKeys.insert(value).second) {
              spdlog::warn("Unknown keyboard key: {}", value);
            }
          }
        }
      }

    } catch (const std::exception &e) {
      // Most likely std::stoi choking on a malformed/unexpected binding
      // string (e.g. "gamepad:b" with no number, or an index that made
      // sense on the platform the model was saved on but not here). Log it
      // once per unique offending binding and move on instead of crashing.
      static std::set<std::string> warnedBadBindings;
      std::string key = mesh.name + "|" + mesh.inputBinding;
      if (warnedBadBindings.insert(key).second) {
        spdlog::warn("Skipping unusable input binding '{}' on mesh '{}': {}",
                     mesh.inputBinding, mesh.name, e.what());
      }
      mesh.press = 0.0f;
      mesh.highlight_value = 0.0f;
      mesh.pull = 0.0f;
      mesh.axis_highlight_value = 0.0f;
    }
  }
}

void controller_window_input() {
  SDL_PumpEvents();

  float globalMouseDx, globalMouseDy;
  GlobalKeyboard::getMouseDelta(globalMouseDx, globalMouseDy);

  float globalScrollDx, globalScrollDy;
  GlobalKeyboard::getScrollDelta(globalScrollDx, globalScrollDy);

  int globalMouseX, globalMouseY;
  GlobalKeyboard::getMousePosition(globalMouseX, globalMouseY);

  for (auto &w : windows) {
    if (!w.is_import_preview) {
      if (w.network_enabled && w.network_mode == 1) {
        // Receiver: read from network instead of controller
        receiveNetworkState(w);
        continue; // skip the rest of the controller processing
      }
      if (w.model.meshes.empty()) {
        spdlog::warn("Controller window has empty model meshes; skipping "
                     "controller input.");
      } else {
        // Gyro processing (with safety checks)
        if (w.gyro_enabled) {
          bool has_gyro_source = false;
          if (w.is_gamecontroller && w.sdl_controller) {
            has_gyro_source = true;
            int ret = SDL_GetGamepadSensorData(w.sdl_controller,
                                               SDL_SENSOR_GYRO, w.gyro_data, 3);
            if (ret >= 0) {
              if (isnan(w.gyro_data[0]) || isnan(w.gyro_data[1]) ||
                  isnan(w.gyro_data[2])) {
                spdlog::debug("Gyro data contains NaN, skipping frame");
                goto skip_gyro_processing;
              }
              if (fabs(w.gyro_data[0]) < 1e-6f &&
                  fabs(w.gyro_data[1]) < 1e-6f &&
                  fabs(w.gyro_data[2]) < 1e-6f) {
                goto skip_gyro_processing;
              }
              if (w.gyro_debug_logging) {
                static int gyro_log_counter = 0;
                if (++gyro_log_counter % 60 == 0) { // Log every 60 frames
                  glm::vec3 euler =
                      glm::eulerAngles(glm::quat_cast(w.gyro_matrix));
                  spdlog::debug(
                      "Gyro Euler: yaw={:.3f} pitch={:.3f} roll={:.3f}",
                      glm::degrees(euler.y), glm::degrees(euler.x),
                      glm::degrees(euler.z));
                }
              }
              double current_time = glfwGetTime(); // seconds
              if (w.gyro_toggled) {
                w.gyro_time = current_time;
                w.gyro_toggled = false;
              } else {
                float dt = (float)(current_time - w.gyro_time);
                dt = glm::clamp(dt, 0.0001f, 0.1f);
                const float SCALE = 0.1f;
                float sens = w.gyro_sensitivity * SCALE;
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[0] * dt * sens,
                                glm::vec3(1, 0, 0));
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[1] * dt * sens,
                                glm::vec3(0, 1, 0));
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[2] * dt * sens,
                                glm::vec3(0, 0, 1));
                w.gyro_matrix[3][0] = 0.0f;
                w.gyro_matrix[3][1] = 0.0f;
                w.gyro_matrix[3][2] = 0.0f;
                w.gyro_matrix[3][3] = 1.0f;
                glm::mat3 rot = glm::mat3(w.gyro_matrix);
                glm::vec3 col0 = rot[0];
                glm::vec3 col1 = rot[1];
                glm::vec3 col2 = rot[2];
                col0 = glm::normalize(col0);
                col1 = glm::normalize(col1 - glm::dot(col0, col1) * col0);
                col2 = glm::cross(col0, col1);
                rot = glm::mat3(col0, col1, col2);
                w.gyro_matrix = glm::mat4(rot);
                if (w.reset_gyro_button1 >= 0 && w.reset_gyro_button2 >= 0) {
                  if (get_button_value_choice(w, w.reset_gyro_button1, true) &&
                      get_button_value_choice(w, w.reset_gyro_button2, true)) {
                    w.gyro_matrix = glm::mat4(1.0f);
                    w.network_gyro_reset = true;
                    if (w.gyro_debug_logging)
                      spdlog::debug("Gyro reset via button combo");
                  }
                }
                w.gyro_time = current_time;
                glm::vec3 up_error =
                    glm::cross(glm::vec3(0, 1, 0),
                               glm::vec3(0, 1, 0) * glm::mat3(w.gyro_matrix));
                if (glm::length(up_error) > 0.001f) {
                  w.gyro_matrix =
                      glm::rotate(w.gyro_matrix, w.gyro_correction * 0.0001f,
                                  glm::normalize(up_error));
                }
                glm::vec3 right_error =
                    glm::cross(glm::vec3(1, 0, 0),
                               glm::vec3(1, 0, 0) * glm::mat3(w.gyro_matrix));
                if (glm::length(right_error) > 0.001f) {
                  w.gyro_matrix =
                      glm::rotate(w.gyro_matrix, w.gyro_correction * 0.0001f,
                                  glm::normalize(right_error));
                }
                if (w.reset_gyro_button1 >= 0 && w.reset_gyro_button2 >= 0) {
                  if (get_button_value_choice(w, w.reset_gyro_button1, true) &&
                      get_button_value_choice(w, w.reset_gyro_button2, true)) {
                    w.gyro_matrix = glm::mat4(1.0f);
                    if (w.gyro_debug_logging)
                      spdlog::debug("Gyro reset via button combo");
                  }
                }
                if (w.gyro_debug_logging) {
                  static int log_counter = 0;
                  if (++log_counter % 120 == 0) {
                    glm::vec3 euler =
                        glm::eulerAngles(glm::quat_cast(w.gyro_matrix));
                    spdlog::debug(
                        "Gyro Euler: yaw={:.3f} pitch={:.3f} roll={:.3f}",
                        glm::degrees(euler.y), glm::degrees(euler.x),
                        glm::degrees(euler.z));
                  }
                }
                w.gyro_time = current_time;
              }
            }
          } else if (w.gyro_sensor) {
            has_gyro_source = true;
            float sensor_data[3];
            if (SDL_GetSensorData(w.gyro_sensor, sensor_data, 3) == 0) {
              w.gyro_data[0] = sensor_data[0];
              w.gyro_data[1] = sensor_data[1];
              w.gyro_data[2] = sensor_data[2];
            } else {
              spdlog::debug("Failed to read gyro sensor data: {}",
                            SDL_GetError());
            }
          } else {
            spdlog::warn("Gyro enabled but no valid source; disabling.");
            w.gyro_enabled = false;
            w.gyro_debug_logging = false;
          }
        skip_gyro_processing:;
        }

        // Touchpad data (with bounds checking)
        if (w.is_gamecontroller && w.sdl_controller) {
          int touch_pads = SDL_GetNumGamepadTouchpads(w.sdl_controller);
          // ---- SAFETY: clamp to our array size ----
          if (touch_pads > 4)
            touch_pads = 4;
          for (int t = 0; t < touch_pads; ++t) {
            int numFingers =
                SDL_GetNumGamepadTouchpadFingers(w.sdl_controller, t);
            if (numFingers > 2)
              numFingers = 2;
            for (int f = 0; f < numFingers; ++f) {
              SDL_GetGamepadTouchpadFinger(
                  w.sdl_controller, t, f, &w.touchpad_data[t][f].down,
                  &w.touchpad_data[t][f].x, &w.touchpad_data[t][f].y, nullptr);
            }
          }
        }

        // LOGGING (unchanged)
        if (g_log_controller) {
          if (w.is_gamecontroller && w.sdl_controller) {
            SDL_Joystick *joy = SDL_GetGamepadJoystick(w.sdl_controller);
            if (joy) {
              int numAxes = SDL_GetNumJoystickAxes(joy);
              for (int i = 0; i < numAxes; ++i) {
                float val = get_axis_value(w, i);
              }
              int numJoyButtons = SDL_GetNumJoystickButtons(joy);
              // ---- SAFETY: clamp to our array size ----
              if (numJoyButtons > 128)
                numJoyButtons = 128;
              for (int b = 0; b < numJoyButtons; ++b) {
                bool pressed = SDL_GetJoystickButton(joy, b);
                if (pressed && !w.last_joy_button_values[b]) {
                  spdlog::info("[b{}] Joystick Button {} pressed", b, b);
                }
                w.last_joy_button_values[b] = pressed;
              }
              for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
                bool pressed = SDL_GetGamepadButton(w.sdl_controller,
                                                    (SDL_GamepadButton)b);
                if (pressed && !w.last_button_values[b]) {
                  std::string name = (b >= 0 && b < 21)
                                         ? button_names[b]
                                         : "Button " + std::to_string(b);
                  spdlog::info("[b{}] {} pressed", b, name);
                }
                w.last_button_values[b] = pressed;
              }
              int numTouchpads = SDL_GetNumGamepadTouchpads(w.sdl_controller);
              if (numTouchpads > 4)
                numTouchpads = 4;
              for (int t = 0; t < numTouchpads; ++t) {
                int numFingers =
                    SDL_GetNumGamepadTouchpadFingers(w.sdl_controller, t);
                if (numFingers > 2)
                  numFingers = 2;
                for (int f = 0; f < numFingers; ++f) {
                  bool down;
                  float x, y;
                  if (SDL_GetGamepadTouchpadFinger(w.sdl_controller, t, f,
                                                   &down, &x, &y,
                                                   nullptr) == 0) {
                    if (down) {
                      spdlog::info(
                          "Touchpad {} finger {} down at ({:.3f}, {:.3f})", t,
                          f, x, y);
                    } else if (down) {
                      spdlog::info("Touchpad {} finger {} up", t, f);
                    }
                  }
                }
              }
            }
          } else if (!w.is_gamecontroller && w.sdl_joystick) {
            int numButtons = SDL_GetNumJoystickButtons(w.sdl_joystick);
            // ---- SAFETY: clamp to our array size ----
            if (numButtons > 128)
              numButtons = 128;
            for (int i = 0; i < numButtons; ++i) {
              bool pressed = SDL_GetJoystickButton(w.sdl_joystick, i);
              if (pressed && !w.last_joy_button_values[i]) {
                spdlog::info("[b{}] Generic Button {} pressed", i, i);
              }
              w.last_joy_button_values[i] = pressed;
            }
            int numAxes = SDL_GetNumJoystickAxes(w.sdl_joystick);
            for (int i = 0; i < numAxes; ++i) {
              float val = SDL_GetJoystickAxis(w.sdl_joystick, i) / 32767.0f;
              std::string label = "Generic Axis " + std::to_string(i);
              logAxisChange(w, i, val, label);
            }
            int numHats = SDL_GetNumJoystickHats(w.sdl_joystick);
            for (int i = 0; i < numHats; ++i) {
              Uint8 hatVal = SDL_GetJoystickHat(w.sdl_joystick, i);
              logHatChange(w, i, hatVal);
            }
          }
        }

        if (g_log_keyboard) {
          static std::array<bool, SDL_SCANCODE_COUNT> lastKeyState{};
          static bool keyboardLogInitialized = false;
          for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
            bool pressed =
                GlobalKeyboard::isPressed(static_cast<SDL_Scancode>(i));
            if (keyboardLogInitialized && pressed != lastKeyState[i]) {
              const char *name =
                  SDL_GetScancodeName(static_cast<SDL_Scancode>(i));
              spdlog::info("[keyboard] Key '{}' {}", name,
                           pressed ? "pressed" : "released");
            }
            lastKeyState[i] = pressed;
          }
          keyboardLogInitialized = true;
        }

        if (g_log_mouse) {
          static std::array<bool, 8> prevMouseButtons{};
          static bool mouseLogInitialized = false;
          if (!mouseLogInitialized) {
            for (int b = 0; b < 8; ++b)
              prevMouseButtons[b] = GlobalKeyboard::isMouseButtonPressed(b);
            mouseLogInitialized = true;
          }
          for (int b = 0; b < 8; ++b) {
            bool current = GlobalKeyboard::isMouseButtonPressed(b);
            if (current != prevMouseButtons[b]) {
              const char *name = getMouseButtonName(b);
              std::string buttonName;
              if (name) {
                buttonName = name;
              } else {
                buttonName = "Button " + std::to_string(b);
              }
              spdlog::info("[mouse] {} {}", buttonName,
                           current ? "pressed" : "released");
              prevMouseButtons[b] = current;
            }
          }

          // ---- Log mouse movement direction using ASCII arrows ----
          static float last_log_mouse_x = 0.0f, last_log_mouse_y = 0.0f;
          float dx = globalMouseX - last_log_mouse_x;
          float dy = -(globalMouseY -
                       last_log_mouse_y); // Invert Y for screen coordinates
          float distance_sq = dx * dx + dy * dy;
          if (distance_sq > 25.0f) { // 5.0f squared
            float distance = sqrt(distance_sq);
            const char *direction = "";
            float angle = atan2(dy, dx) * 180.0f / 3.14159265f;

            // Normalize angle to 0-360
            if (angle < 0)
              angle += 360.0f;

            // Determine direction (8 directions)
            if (angle >= 22.5f && angle < 67.5f)
              direction = "NE"; // Up-Right
            else if (angle >= 67.5f && angle < 112.5f)
              direction = "N"; // Up
            else if (angle >= 112.5f && angle < 157.5f)
              direction = "NW"; // Up-Left
            else if (angle >= 157.5f && angle < 202.5f)
              direction = "W"; // Left
            else if (angle >= 202.5f && angle < 247.5f)
              direction = "SW"; // Down-Left
            else if (angle >= 247.5f && angle < 292.5f)
              direction = "S"; // Down
            else if (angle >= 292.5f && angle < 337.5f)
              direction = "SE"; // Down-Right
            else
              direction = "E"; // Right

            spdlog::info("[mouse] moved {}", direction);
            last_log_mouse_x = static_cast<float>(globalMouseX);
            last_log_mouse_y = static_cast<float>(globalMouseY);
          }

          if (fabs(globalScrollDx) > 0.01f || fabs(globalScrollDy) > 0.01f) {
            spdlog::info("[mouse] scroll ({:.1f}, {:.1f})", globalScrollDx,
                         globalScrollDy);
          }
        }

        // Fill network input snapshot
        if (w.network_enabled && w.network_mode == 0) {
          // Gamepad
          if (w.is_gamecontroller && w.sdl_controller) {
            for (int i = 0; i < 32; ++i)
              w.net_gamepad_buttons[i] = get_button_value_choice(w, i, false);
            for (int i = 0; i < 8; ++i)
              w.net_gamepad_axes[i] = get_axis_value_choice(w, i, false);
          }
          // Joystick
          if (w.sdl_joystick) {
            for (int i = 0; i < 128; ++i)
              w.net_joystick_buttons[i] =
                  SDL_GetJoystickButton(w.sdl_joystick, i);
            for (int i = 0; i < 128; ++i)
              w.net_joystick_axes[i] =
                  SDL_GetJoystickAxis(w.sdl_joystick, i) / 32767.0f;
          }
          // Keyboard
          w.net_keyboard_keys.clear();
          for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
            if (GlobalKeyboard::isPressed((SDL_Scancode)i))
              w.net_keyboard_keys.insert((SDL_Scancode)i);
          }
          // Mouse
          GlobalKeyboard::getMouseDelta(w.net_mouse_dx, w.net_mouse_dy);
          for (int i = 0; i < 8; ++i)
            w.net_mouse_buttons[i] = GlobalKeyboard::isMouseButtonPressed(i);
        }

        applyMappingToMeshes(w, globalMouseDx, globalMouseDy, globalScrollDx,
                             globalScrollDy);

        // ---- Handle touchpoint idle timeout ----
        // Only check every 60 frames to reduce overhead
        static int frame_counter = 0;
        frame_counter++;
        if (frame_counter % 60 ==
            0) { // Check every 60 frames (~1 second at 60fps)
          double current_time = glfwGetTime();
          for (int meshIdx = 0; meshIdx < (int)w.model.meshes.size();
               ++meshIdx) {
            Mesh &mesh = w.model.meshes[meshIdx];
            if (mesh.inputBinding.find("mouse:") != 0)
              continue;
            bool isTouchPoint = mesh.isTouchpoint;
            if (!isTouchPoint)
              continue;
            auto it = w.touchpoint_last_move_time.find(meshIdx);
            if (it == w.touchpoint_last_move_time.end())
              continue;
            // Only reset after 5 seconds of inactivity
            if (current_time - it->second > 5.0) {
              // Reset to center
              mesh.touch_X = 0.5f;
              mesh.touch_Y = 0.5f;
              mesh.touch_state = 0;
              mesh.glow_intensity = 0.0f;
              mesh.highlight_value = 0.0f;
              mesh.visible = false;
            }
          }
        }

        // ---- Decay the scroll-wheel "lit recently" flag ----
        // w.scroll_accum_magnitude is now just a short decaying pulse
        // set to 1.0 the instant a scroll tick is detected above, not
        // an accumulated position - see the mouse_scroll_* handling
        // above for why. scroll_accum_x/y are unused now that scroll
        // bindings behave like a plain button instead of a stick.
        static const float SCROLL_HIGHLIGHT_DECAY = 0.85f;
        static const float SCROLL_EPSILON = 0.001f;
        if (w.scroll_accum_magnitude > SCROLL_EPSILON) {
          w.scroll_accum_magnitude *= SCROLL_HIGHLIGHT_DECAY;
          if (w.scroll_accum_magnitude < SCROLL_EPSILON)
            w.scroll_accum_magnitude = 0.0f;
        }

        // Propagate stick motion
        for (int stickPart : {5, 6}) {
          Mesh *stickMesh = nullptr;
          int stickIndex = -1;
          for (int i = 0; i < (int)w.model.meshes.size(); ++i) {
            if (w.model.meshes[i].assignedPart == stickPart) {
              stickMesh = &w.model.meshes[i];
              stickIndex = i;
              break;
            }
          }
          if (!stickMesh)
            continue;

          for (auto &child : w.model.meshes) {
            if (child.parentIndex == stickIndex && child.inputBinding.empty()) {
              child.stick_X = stickMesh->stick_X;
              child.stick_Y = stickMesh->stick_Y;
              if (child.assignedPart == 7 || child.assignedPart == 8) {
                float threshold = child.ring_highlight_deadzone * 0.01f;
                float dx = fabs(stickMesh->stick_X / 32767.0f);
                float dy = fabs(stickMesh->stick_Y / 32767.0f);
                child.highlight_value = (dx > threshold || dy > threshold)
                                            ? std::max(dx, dy) * 1.2f
                                            : 0.0f;
              }
            }
          }
        }
      }
    } // end if (!w.is_import_preview)

    // Window-relative mouse for orbit/pivot (now allowed for import preview
    // too)
    int win_width, win_height;
    glfwGetWindowSize(w.glfw_window, &win_width, &win_height);
    if (win_width == 0 || win_height == 0)
      continue;

    double mouse_x, mouse_y;
    glfwGetCursorPos(w.glfw_window, &mouse_x, &mouse_y);

    w.mouse_delta_x = (float)mouse_x - w.mouse_x;
    w.mouse_delta_y = (float)mouse_y - w.mouse_y;
    w.mouse_x = (float)mouse_x;
    w.mouse_y = (float)mouse_y;

    for (int b = 0; b <= GLFW_MOUSE_BUTTON_LAST; ++b)
      w.mouse_buttons_prev[b] = w.mouse_buttons[b];

    for (int b = 0; b <= GLFW_MOUSE_BUTTON_LAST; ++b)
      w.mouse_buttons[b] = (glfwGetMouseButton(w.glfw_window, b) == GLFW_PRESS);

    int left_button =
        w.mouse_buttons[GLFW_MOUSE_BUTTON_LEFT] ? GLFW_PRESS : GLFW_RELEASE;
    int middle_button =
        w.mouse_buttons[GLFW_MOUSE_BUTTON_MIDDLE] ? GLFW_PRESS : GLFW_RELEASE;

    bool pivotHit = false;
    glm::vec3 pivotPos(0.0f);
    if (selected_tab < tabs.size()) {
      unsigned activeID = tabs[selected_tab].ID;
      if (w.ID == activeID && selected_mesh >= 0 &&
          selected_mesh < (int)w.model.meshes.size()) {
        const Mesh &mesh = w.model.meshes[selected_mesh];
        if (mesh.elements > 0) {
          glm::mat4 pivotMat =
              getMeshFinalMatrix(w.model, selected_mesh, w.gyro_matrix);
          pivotPos = glm::vec3(pivotMat[3]);
          glm::vec4 clipPos =
              w.projection_matrix * w.view_matrix * glm::vec4(pivotPos, 1.0f);
          if (clipPos.w > 0.0f) {
            clipPos /= clipPos.w;
            float screenX = (clipPos.x * 0.5f + 0.5f) * win_width;
            float screenY = (1.0f - (clipPos.y * 0.5f + 0.5f)) * win_height;
            double dist = sqrt((mouse_x - screenX) * (mouse_x - screenX) +
                               (mouse_y - screenY) * (mouse_y - screenY));
            if (dist < 20.0)
              pivotHit = true;
          }
        }
      }
    }

    if (left_button == GLFW_PRESS && !w.drag_to_move) {
      if (pivotHit && !w.pivot_dragging) {
        w.pivot_dragging = true;
        w.pivot_drag_start_screen_x = mouse_x;
        w.pivot_drag_start_screen_y = mouse_y;
        w.pivot_drag_mesh_index = selected_mesh;
        w.pivot_drag_start_world = pivotPos;
      }

      if (w.pivot_dragging) {
        if (w.pivot_drag_mesh_index >= 0 &&
            w.pivot_drag_mesh_index < (int)w.model.meshes.size()) {
          Mesh &mesh = w.model.meshes[w.pivot_drag_mesh_index];
          glm::vec3 camRight = glm::normalize(glm::vec3(
              w.view_matrix[0][0], w.view_matrix[1][0], w.view_matrix[2][0]));
          glm::vec3 camUp = glm::normalize(glm::vec3(
              w.view_matrix[0][1], w.view_matrix[1][1], w.view_matrix[2][1]));
          float distance = glm::length(w.camera_position - pivotPos);
          float scale = distance * 0.001f;
          double dx = mouse_x - w.pivot_drag_start_screen_x;
          double dy = mouse_y - w.pivot_drag_start_screen_y;
          glm::vec3 deltaWorld =
              (float)dx * scale * camRight - (float)dy * scale * camUp;
          mesh.pivot_offset[0] += deltaWorld.x;
          mesh.pivot_offset[1] += deltaWorld.y;
          mesh.pivot_offset[2] += deltaWorld.z;
          w.pivot_drag_start_screen_x = mouse_x;
          w.pivot_drag_start_screen_y = mouse_y;
        }
      } else {
        if (w.mouse_first_click) {
          w.prev_mouse_x = mouse_x;
          w.prev_mouse_y = mouse_y;
          w.mouse_first_click = false;
        }
        double delta_x = mouse_x - w.prev_mouse_x;
        double delta_y = mouse_y - w.prev_mouse_y;
        float sensitivity = 0.5f;
        if (glfwGetKey(w.glfw_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(w.glfw_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
          w.camera_roll += delta_x * sensitivity;
        } else {
          w.camera_yaw -= delta_x * sensitivity;
          w.camera_pitch += delta_y * sensitivity;
        }
        w.prev_mouse_x = mouse_x;
        w.prev_mouse_y = mouse_y;
      }
    } else if (left_button == GLFW_PRESS && w.drag_to_move) {
      // Actually move the window - previously this checkbox only
      // disabled the camera-rotate-on-drag behavior above and had no
      // window-moving logic of its own at all (it likely worked by
      // coincidence before, on a decorated window, by getting out of
      // the way of the native title-bar drag - which never reaches this
      // code anyway, since title bar clicks are non-client-area events).
      //
      // mouse_x/mouse_y are window-relative client coordinates, which
      // change meaning the instant the window itself moves - e.g. if
      // the cursor stays physically still on screen but the window
      // slides out from under it, mouse_x/mouse_y change even though
      // nothing the user did caused that. So the drag is tracked in
      // screen space instead: capture the cursor's screen position
      // (window position + client-relative mouse position) and the
      // window's position, both at the moment the drag starts, then
      // every frame afterward move the window by exactly how far the
      // screen-space cursor position has moved since - applying the
      // full accumulated delta to the start position each time, not
      // frame-to-frame deltas, so small rounding errors can't compound
      // over a long drag.
      int win_x = 0, win_y = 0;
      glfwGetWindowPos(w.glfw_window, &win_x, &win_y);
      double screen_x = win_x + mouse_x;
      double screen_y = win_y + mouse_y;

      if (!w.drag_moving) {
        w.drag_moving = true;
        w.drag_move_anchor_x = screen_x;
        w.drag_move_anchor_y = screen_y;
        w.drag_move_start_win_x = win_x;
        w.drag_move_start_win_y = win_y;
      } else {
        int new_x = w.drag_move_start_win_x +
                    (int)std::lround(screen_x - w.drag_move_anchor_x);
        int new_y = w.drag_move_start_win_y +
                    (int)std::lround(screen_y - w.drag_move_anchor_y);
        glfwSetWindowPos(w.glfw_window, new_x, new_y);
      }
      // Keep pivot/rotate state clean in case Drag to Move gets toggled
      // off mid-interaction.
      w.pivot_dragging = false;
      w.pivot_drag_mesh_index = -1;
      w.mouse_first_click = true;
    } else {
      w.pivot_dragging = false;
      w.pivot_drag_mesh_index = -1;
      w.mouse_first_click = true;
      w.drag_moving = false;
    }

    if (middle_button == GLFW_PRESS) {
      bool shiftPressed =
          glfwGetKey(w.glfw_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
          glfwGetKey(w.glfw_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
      if (shiftPressed) {
        // Pan: move offsets based on mouse delta
        float sensitivity = 0.005f;
        w.camera_offset_x -= w.mouse_delta_x * sensitivity;
        w.camera_offset_y += w.mouse_delta_y * sensitivity; // inverted Y
      } else {
        // Reset view
        w.camera_yaw = 0.0f;
        w.camera_pitch = 89.999f;
        w.camera_distance = 3.5f;
        w.camera_roll = 0.0f;
        w.camera_offset_x = 0.0f;
        w.camera_offset_y = 0.0f;
      }
    }

    // Check if window should close
    if (glfwWindowShouldClose(w.glfw_window)) {
      if (w.is_import_preview) {
        // For import preview windows, just close the window and remove its
        // tab
        unsigned id = w.ID;
        // Close the window and remove from windows vector
        glfwDestroyWindow(w.glfw_window);
        // Remove from windows vector
        for (unsigned i = 0; i < windows.size(); ++i) {
          if (windows[i].ID == id) {
            windows.erase(windows.begin() + i);
            break;
          }
        }
        // Remove the tab
        removeTab(id);
        // Mark the preview as closed
        w.is_import_preview = false;
        w.import_preview.is_open = false;
        break;
      } else {
        // For normal controller windows, quit the entire program
        gQuit = true;
        break;
      }
    }
  } // end for windows
}

// ----------------------------------------------------------------------
//  GLOBAL FUNCTIONS
// ----------------------------------------------------------------------

void controller_sdl_events(SDL_Event *event) {
  if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
    spdlog::info("Game controller added. Reopening...");
  }
  if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
    SDL_JoystickID id = event->gdevice.which;
    for (auto &w : windows) {
      if (w.sdl_controller) {
        SDL_Joystick *joy = SDL_GetGamepadJoystick(w.sdl_controller);
        if (joy && SDL_GetJoystickID(joy) == id) {
          SDL_CloseGamepad(w.sdl_controller);
          w.sdl_controller = nullptr;
          w.is_gamecontroller = false;
          // Also clear any sensor references
          w.gyro_sensor = nullptr;
          w.accel_sensor = nullptr;
          w.gyro_enabled = false;
          spdlog::info("Game controller removed and closed.");
          break;
        }
      } else if (w.sdl_joystick) {
        if (SDL_GetJoystickID(w.sdl_joystick) == id) {
          SDL_CloseJoystick(w.sdl_joystick);
          w.sdl_joystick = nullptr;
          w.gyro_sensor = nullptr;
          w.accel_sensor = nullptr;
          w.gyro_enabled = false;
          spdlog::info("Joystick removed and closed.");
          break;
        }
      }
    }
  }
  if (event->type == SDL_EVENT_JOYSTICK_ADDED) {
    spdlog::info("Joystick added.");
  }
  if (event->type == SDL_EVENT_JOYSTICK_REMOVED) {
    // Similar cleanup for raw joysticks (if your app uses them)
    SDL_JoystickID id = event->jdevice.which;
    for (auto &w : windows) {
      if (!w.is_gamecontroller && w.sdl_joystick) {
        if (SDL_GetJoystickID(w.sdl_joystick) == id) {
          SDL_CloseJoystick(w.sdl_joystick);
          w.sdl_joystick = nullptr;
          w.gyro_sensor = nullptr;
          w.accel_sensor = nullptr;
          w.gyro_enabled = false;
          spdlog::info("Raw joystick removed and closed.");
          break;
        }
      }
    }
  }
  if (event->type == SDL_EVENT_SENSOR_UPDATE) {
    // Keep as is – but avoid using sensors if the device is already closed
    for (auto &w : windows) {
      if (w.gyro_sensor &&
          event->sensor.which == SDL_GetSensorID(w.gyro_sensor)) {
        spdlog::debug("Gyro update: x={:.3f} y={:.3f} z={:.3f}",
                      event->sensor.data[0], event->sensor.data[1],
                      event->sensor.data[2]);
      }
      if (w.accel_sensor &&
          event->sensor.which == SDL_GetSensorID(w.accel_sensor)) {
        spdlog::debug("Accel update: x={:.3f} y={:.3f} z={:.3f}",
                      event->sensor.data[0], event->sensor.data[1],
                      event->sensor.data[2]);
      }
    }
  }
}

void controller_window_scroll_callback(GLFWwindow *window, double xoffset,
                                       double yoffset) {
  for (auto &w : windows) {
    if (w.glfw_window == window) {
      if (w.scroll_to_resize) {
        int ww = 0, hh = 0;
        glfwGetWindowSize(window, &ww, &hh);
        const GLFWvidmode *mode = get_vid_mode();
        if (yoffset > 0) {
          ww = (int)(ww * 1.05f);
          hh = (int)(hh * 1.05f);
          if (ww > mode->width)
            ww = mode->width;
          if (hh > mode->height)
            hh = mode->height;
        } else if (yoffset < 0) {
          ww = (int)(ww * 0.95f);
          hh = (int)(hh * 0.95f);
          if (ww < 10)
            ww = 10;
          if (hh < 10)
            hh = 10;
        }
        glfwSetWindowSize(window, ww, hh);
      } else {
        if (!w.freelook) {
          float zoom_speed = 0.2f;
          w.camera_distance -= yoffset * zoom_speed;
          if (w.camera_distance < 0.5f)
            w.camera_distance = 0.5f;
          if (w.camera_distance > 20.0f)
            w.camera_distance = 20.0f;
        }
      }
      break;
    }
  }
}

void createPivotCircle(controller_window &w) {
  if (w.pivot_vao)
    return;
  const int segments = w.pivot_segments;
  std::vector<float> verts;
  for (int i = 0; i <= segments; ++i) {
    float angle = 2.0f * 3.14159265f * (float)i / (float)segments;
    verts.push_back(0.1f * cosf(angle));
    verts.push_back(0.1f * sinf(angle));
    verts.push_back(0.0f);
  }
  glGenVertexArrays(1, &w.pivot_vao);
  glGenBuffers(1, &w.pivot_vbo);
  glBindVertexArray(w.pivot_vao);
  glBindBuffer(GL_ARRAY_BUFFER, w.pivot_vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(),
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

void make_grid(controller_window &w) {
  std::vector<glm::vec3> vertices;
  std::vector<glm::uvec4> indices;
  int slices = 20;
  for (int j = 0; j <= slices; ++j) {
    for (int i = 0; i <= slices; ++i) {
      float x = (float)i / (float)slices;
      float y = 0;
      float z = (float)j / (float)slices;
      vertices.push_back(glm::vec3(x, y, z));
    }
  }
  for (int j = 0; j < slices; ++j) {
    for (int i = 0; i < slices; ++i) {
      int row1 = j * (slices + 1);
      int row2 = (j + 1) * (slices + 1);
      indices.push_back(
          glm::uvec4(row1 + i, row1 + i + 1, row1 + i + 1, row2 + i + 1));
      indices.push_back(glm::uvec4(row2 + i + 1, row2 + i, row2 + i, row1 + i));
    }
  }
  glGenVertexArrays(1, &w.grid_vao);
  glBindVertexArray(w.grid_vao);
  glGenBuffers(1, &w.grid_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, w.grid_vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3),
               glm::value_ptr(vertices[0]), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  glGenBuffers(1, &w.grid_ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, w.grid_ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(glm::uvec4),
               glm::value_ptr(indices[0]), GL_STATIC_DRAW);
  glBindVertexArray(0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  w.grid_length = (GLuint)indices.size() * 4;
}

void lightingSpecification(controller_window &w) {
  glGenVertexArrays(1, &w.lighting_vao);
  glGenBuffers(1, &w.lighting_vertex_data);
  glGenBuffers(1, &w.lighting_normal_data);
  glGenBuffers(1, &w.lighting_texture_data);
  glBindVertexArray(w.lighting_vao);
  glBindBuffer(GL_ARRAY_BUFFER, w.lighting_vertex_data);
  glBufferData(GL_ARRAY_BUFFER, CUBE_VERTICES_SIZE * sizeof(GLfloat),
               cube_vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, w.lighting_normal_data);
  glBufferData(GL_ARRAY_BUFFER, CUBE_NORMALS_SIZE * sizeof(GLfloat),
               cube_normals, GL_STATIC_DRAW);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, w.lighting_texture_data);
  glBufferData(GL_ARRAY_BUFFER, CUBE_TEX_COORDS_SIZE * sizeof(GLfloat),
               cube_tex_coords, GL_STATIC_DRAW);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);
}

void createShader(GLuint &shader_id, const char *vs_source,
                  const char *fs_source) {
  shader_id = CreateShaderProgram(vs_source, fs_source);
}

void update_camera(controller_window &w, GLuint &shader, int window_width,
                   int window_height) {
  glUseProgram(shader);
  if (w.freelook) {
    w.freelook_direction.x =
        cos(glm::radians(w.freelook_pitch)) * sin(glm::radians(w.freelook_yaw));
    w.freelook_direction.y = sin(glm::radians(w.freelook_pitch));
    w.freelook_direction.z =
        cos(glm::radians(w.freelook_pitch)) * cos(glm::radians(w.freelook_yaw));
    glm::vec3 front = w.freelook_position + w.freelook_direction;
    w.view_matrix =
        glm::lookAt(w.freelook_position, front, glm::vec3(0.0f, 1.0f, 0.0f));
  } else {
    // Orbit camera
    w.camera_position.x = cos(glm::radians(w.camera_pitch)) *
                          sin(glm::radians(w.camera_yaw)) * w.camera_distance;
    w.camera_position.y = sin(glm::radians(w.camera_pitch)) * w.camera_distance;
    w.camera_position.z = cos(glm::radians(w.camera_pitch)) *
                          cos(glm::radians(w.camera_yaw)) * w.camera_distance;

    glm::vec3 front =
        glm::normalize(glm::vec3(0.0, 0.0, 0.0) - w.camera_position);
    glm::vec3 right =
        glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::cross(right, front);
    glm::mat4 roll_mat = glm::mat4(1.0f);
    roll_mat = glm::rotate(roll_mat, glm::radians(w.camera_roll), front);
    up = glm::vec3(roll_mat * glm::vec4(up, 1.0));

    // Apply pan offset (translate camera position and target)
    glm::vec3 offset = right * w.camera_offset_x + up * w.camera_offset_y;
    glm::vec3 eye = w.camera_position + offset;
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f) + offset;

    w.view_matrix = glm::lookAt(eye, target, up);
  }
  shaderUniformMat4(shader, "view", w.view_matrix);
  // Guard against a 0 framebuffer dimension (can happen briefly during
  // an iconify/restore transition) producing Inf/NaN in the aspect
  // ratio below - degenerate frames should just render as a 1:1
  // fallback for that one frame, not propagate NaN into the shader.
  int safe_width = window_width > 0 ? window_width : 1;
  int safe_height = window_height > 0 ? window_height : 1;
  w.projection_matrix = glm::perspective(
      glm::radians(45.0f), (float)safe_width / safe_height, 0.1f, 200.0f);
  shaderUniformMat4(shader, "projection", w.projection_matrix);
  glUseProgram(0);
}

bool isControllerWindowMinimized(const controller_window &w) {
  bool minimized = glfwGetWindowAttrib(w.glfw_window, GLFW_ICONIFIED);
#if defined(_WIN32)
  minimized = minimized || w.overlay_minimized;
#endif
  return minimized;
}

void drawControllerWindows() {
  for (controller_window &w : windows) {
    if (glfwWindowShouldClose(w.glfw_window))
      continue;
    if (!isControllerWindowMinimized(w)) {
      glfwMakeContextCurrent(w.glfw_window);
      // vsync (glfwSwapInterval) is never used for controller windows -
      // always 0. Three separate incremental attempts at narrowing the
      // trigger condition (click-through only, then also transparent
      // overlay, then considering window-focus state) each turned out
      // to be too narrow: on NVIDIA specifically, glfwSwapBuffers can
      // stall waiting for a vsync signal the driver is deprioritizing
      // for this window while a fullscreen/borderless game has GPU
      // priority - and since gamepad polling and rendering share a
      // single thread (see Input()/Draw() in main.cpp), that freezes
      // input reads too. AMD doesn't exhibit this. Rather than keep
      // chasing which exact combination of settings triggers it, vsync
      // is simply never used here; frame pacing instead comes from the
      // sleep-based cap in MainLoop() (main.cpp), which never blocks on
      // anything GPU/driver-related.
      glfwSwapInterval(0);
      w.deltaTime = glfwGetTime() - w.lastTime;
      w.lastTime = glfwGetTime();

      // Network sending (if sender)
      if (w.network_enabled && w.network_mode == 0 && w.network_send_rate > 0) {
        double now = glfwGetTime();
        double interval = 1.0 / w.network_send_rate;
        if (now - w.network_last_send_time >= interval) {
          w.network_last_send_time = now;
          sendNetworkState(w);
        }
      } else if (w.network_enabled && w.network_mode == 0 &&
                 w.network_send_rate == 0) {
        // max speed – send every frame
        sendNetworkState(w);
      }

      int width = 0, height = 0;
      // glViewport needs actual framebuffer PIXELS, not window size in
      // points. On a Retina/HiDPI display (2x content scale) those differ
      // by 2x per axis, so using glfwGetWindowSize here was only covering
      // 1 / (scale^2) of the real drawable area - e.g. 1/4 of the screen
      // on a standard 2x Retina Mac.
      glfwGetFramebufferSize(w.glfw_window, &width, &height);
      glViewport(0, 0, width, height);

      update_camera(w, w.shader, width, height);
      update_camera(w, w.light_source_shader, width, height);
      update_camera(w, w.grid_shader, width, height);

      glEnable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      if (w.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      else
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

      glClearColor(w.bg_color[0] * w.bg_color[3], w.bg_color[1] * w.bg_color[3],
                   w.bg_color[2] * w.bg_color[3], w.bg_color[3]);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      if (w.grid) {
        glBindVertexArray(w.grid_vao);
        glUseProgram(w.grid_shader);
        glEnableVertexAttribArray(0);
        glm::mat4 grid_model = glm::mat4(1.0f);
        // Smaller grid, placed slightly below the model
        grid_model = glm::translate(grid_model, glm::vec3(-5.0f, -0.5f, -5.0f));
        grid_model = glm::scale(grid_model, glm::vec3(10.0f, 0.0f, 10.0f));
        shaderUniformMat4(w.grid_shader, "model", grid_model);
        shaderUniformVec3(w.grid_shader, "gridColor",
                          glm::vec3(0.8f, 0.8f, 0.8f));

        // *** THIS IS THE MISSING LINE THAT MAKES IT VISIBLE ***
        shaderUniformFloat(w.grid_shader, "alpha", 1.0f);

        glDrawElements(GL_LINES, w.grid_length, GL_UNSIGNED_INT, NULL);
      }

      glBindVertexArray(w.lighting_vao);
      glUseProgram(w.light_source_shader);
      for (point_light p : w.point_lights) {
        if (!p.hide) {
          shaderUniformVec3(w.light_source_shader, "lightColor",
                            glm::vec3(p.color[0], p.color[1], p.color[2]));
          glm::mat4 light_source_model = glm::mat4(1.0f);
          light_source_model = glm::translate(light_source_model, p.position);
          light_source_model = glm::scale(light_source_model, glm::vec3(0.2f));
          shaderUniformMat4(w.light_source_shader, "model", light_source_model);
          glDrawArrays(GL_TRIANGLES, 0, 36);
        }
      }
      for (spot_light s : w.spot_lights) {
        if (!s.hide) {
          shaderUniformVec3(w.light_source_shader, "lightColor",
                            glm::vec3(s.color[0], s.color[1], s.color[2]));
          glm::mat4 light_source_model = glm::mat4(1.0f);
          light_source_model = glm::translate(light_source_model, s.position);
          light_source_model =
              glm::rotate(light_source_model, glm::radians(s.pitch),
                          glm::vec3(1.0f, 0.0f, 0.0f));
          light_source_model =
              glm::rotate(light_source_model, glm::radians(s.yaw),
                          glm::vec3(0.0f, 1.0f, 0.0f));
          light_source_model =
              glm::scale(light_source_model, glm::vec3(0.1f, 0.1f, 0.3f));
          shaderUniformMat4(w.light_source_shader, "model", light_source_model);
          glDrawArrays(GL_TRIANGLES, 0, 36);
        }
      }

      // ---- Draw model ----
      glUseProgram(w.shader);

      if (w.freelook)
        shaderUniformVec3(w.shader, "viewPos", w.freelook_position);
      else
        shaderUniformVec3(w.shader, "viewPos", w.camera_position);

      shaderUniformFloat(w.shader, "time", glfwGetTime());

      shaderUniformInt(w.shader, "direct_lights", w.direct_lights.size());
      for (unsigned i = 0; i < w.direct_lights.size(); ++i) {
        std::string name = "dirLights[";
        name.append(std::to_string(i));
        name.append("]");
        shaderUniformVec3(w.shader,
                          std::string(name).append(".direction").c_str(),
                          w.direct_lights[i].direction);
        shaderUniformVec3(
            w.shader, std::string(name).append(".ambient").c_str(),
            glm::vec3(w.direct_lights[i].color[0] * w.direct_lights[i].ambient,
                      w.direct_lights[i].color[1] * w.direct_lights[i].ambient,
                      w.direct_lights[i].color[2] *
                          w.direct_lights[i].ambient));
        shaderUniformVec3(
            w.shader, std::string(name).append(".diffuse").c_str(),
            glm::vec3(w.direct_lights[i].color[0] * w.direct_lights[i].diffuse,
                      w.direct_lights[i].color[1] * w.direct_lights[i].diffuse,
                      w.direct_lights[i].color[2] *
                          w.direct_lights[i].diffuse));
        shaderUniformVec3(
            w.shader, std::string(name).append(".specular").c_str(),
            glm::vec3(w.direct_lights[i].color[0] * w.direct_lights[i].specular,
                      w.direct_lights[i].color[1] * w.direct_lights[i].specular,
                      w.direct_lights[i].color[2] *
                          w.direct_lights[i].specular));
      }

      shaderUniformInt(w.shader, "point_lights", w.point_lights.size());
      for (unsigned i = 0; i < w.point_lights.size(); ++i) {
        std::string name = "pointLights[";
        name.append(std::to_string(i));
        name.append("]");
        shaderUniformFloat(
            w.shader, std::string(name).append(".constant").c_str(),
            w.point_lights[i].constant - w.point_lights[i].intensity);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".linear").c_str(),
                           w.point_lights[i].linear);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".quadratic").c_str(),
                           w.point_lights[i].quadratic);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".position").c_str(),
                          w.point_lights[i].position);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".ambient").c_str(),
                          w.point_lights[i].ambient);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".diffuse").c_str(),
                          w.point_lights[i].diffuse);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".specular").c_str(),
                          w.point_lights[i].specular);
      }

      shaderUniformInt(w.shader, "spot_lights", w.spot_lights.size());
      for (unsigned i = 0; i < w.spot_lights.size(); ++i) {
        std::string name = "spotLights[";
        name.append(std::to_string(i));
        name.append("]");
        shaderUniformVec3(w.shader,
                          std::string(name).append(".position").c_str(),
                          w.spot_lights[i].position);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".direction").c_str(),
                          w.spot_lights[i].direction);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".cutoff").c_str(),
                           glm::cos(glm::radians(w.spot_lights[i].cutoff)));
        shaderUniformFloat(
            w.shader, std::string(name).append(".outer_cutoff").c_str(),
            glm::cos(glm::radians(w.spot_lights[i].cutoff +
                                  (w.spot_lights[i].outer_cutoff * 0.2f))));
        shaderUniformFloat(
            w.shader, std::string(name).append(".constant").c_str(),
            w.spot_lights[i].constant - w.spot_lights[i].intensity);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".linear").c_str(),
                           w.spot_lights[i].linear);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".quadratic").c_str(),
                           w.spot_lights[i].quadratic);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".ambient").c_str(),
                          w.spot_lights[i].ambient);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".diffuse").c_str(),
                          w.spot_lights[i].diffuse);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".specular").c_str(),
                          w.spot_lights[i].specular);
      }

      int highlight =
          w.is_import_preview ? w.import_preview.selected_mesh_index : -1;
      if (w.is_import_preview) {
        if (highlight != w.last_highlight_index) {
          spdlog::debug("Preview highlight index: {}", highlight);
          w.last_highlight_index = highlight;
        }
      }
      w.model.motion_matrix = w.gyro_matrix;

      for (int meshIdx = 0; meshIdx < (int)w.model.meshes.size(); ++meshIdx) {
        Mesh &mesh = w.model.meshes[meshIdx];
        if (mesh.isTouchpoint) {
          // Decay the glow intensity – it will fade out naturally when no new
          // input arrives
          mesh.glow_intensity *= (1.0f - w.deltaTime * 8.0f);
          if (mesh.glow_intensity < 0.001f)
            mesh.glow_intensity = 0.0f;

          // Apply the glow intensity to the material
          if (mesh.glow_intensity > 0.001f) {
            mesh.material.color[0] = 1.0f;
            mesh.material.color[1] = 1.0f;
            mesh.material.color[2] = 1.0f;
            mesh.material.alpha = mesh.glow_intensity;
            mesh.visible = true;
          } else {
            mesh.visible = false;
          }
        }
      }

      // Pass global highlight color (will be overridden per mesh if custom)
      glm::vec4 globalHighlight =
          glm::vec4(w.highlight_color[0], w.highlight_color[1],
                    w.highlight_color[2], w.highlight_color[3]);
      glm::vec3 camPos = w.freelook ? w.freelook_position : w.camera_position;
      drawModel(w.model, w.shader, highlight, globalHighlight, w.view_matrix,
                w.projection_matrix, camPos, w.global_shader_name);

      // ---- Draw Pivot Circle, Axis, and Text Overlay (always on top) ----
      // Disable depth test so they appear on top
      glDisable(GL_DEPTH_TEST);

      if (selected_tab < tabs.size()) {
        unsigned activeID = tabs[selected_tab].ID;
        if (w.ID == activeID && selected_mesh >= 0 &&
            selected_mesh < (int)w.model.meshes.size()) {
          const Mesh &mesh = w.model.meshes[selected_mesh];
          if (mesh.elements > 0) {
            glm::mat4 pivotMat =
                getMeshFinalMatrix(w.model, selected_mesh, w.gyro_matrix);
            glm::vec3 pivotPos = glm::vec3(pivotMat[3]);

            // ---- Pivot Circle ----
            createPivotCircle(w);
            glUseProgram(w.grid_shader);
            shaderUniformMat4(
                w.grid_shader, "model",
                glm::translate(glm::mat4(1.0f), pivotPos) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 0.1f)));
            shaderUniformVec3(w.grid_shader, "gridColor",
                              glm::vec3(1.0f, 0.6f, 0.0f));
            glBindVertexArray(w.pivot_vao);
            glDrawArrays(GL_LINE_LOOP, 0, w.pivot_segments + 1);
            glBindVertexArray(0);
            glUseProgram(0);

            // ---- Axis Indicator ----
            createAxisIndicator(w);
            glUseProgram(w.shader);
            glUseProgram(w.light_source_shader);
            glm::mat4 axisModel = glm::translate(glm::mat4(1.0f), pivotPos);
            shaderUniformMat4(w.light_source_shader, "model", axisModel);
            shaderUniformMat4(w.light_source_shader, "view", w.view_matrix);
            shaderUniformMat4(w.light_source_shader, "projection",
                              w.projection_matrix);
            glBindVertexArray(w.axis_vao);
            // X axis (red)
            shaderUniformVec3(w.light_source_shader, "lightColor",
                              glm::vec3(1.0f, 0.0f, 0.0f));
            glDrawArrays(GL_LINES, 0, 2);
            // Y axis (green)
            shaderUniformVec3(w.light_source_shader, "lightColor",
                              glm::vec3(0.0f, 1.0f, 0.0f));
            glDrawArrays(GL_LINES, 2, 2);
            // Z axis (blue)
            shaderUniformVec3(w.light_source_shader, "lightColor",
                              glm::vec3(0.0f, 0.0f, 1.0f));
            glDrawArrays(GL_LINES, 4, 2);
            glBindVertexArray(0);
            glUseProgram(0);
          }
        }
      }

      // ---- Show Touch Area (only for the selected mesh) ----
      if (w.show_touch_area) {
        unsigned activeID = tabs[selected_tab].ID;
        if (w.ID == activeID && selected_mesh >= 0 &&
            selected_mesh < (int)w.model.meshes.size()) {
          const Mesh &touchpad = w.model.meshes[selected_mesh];
          if (touchpad.isTouchpad && touchpad.elements > 0) {
            createTouchAreaRect(w);
            glUseProgram(w.grid_shader);
            shaderUniformFloat(w.grid_shader, "alpha", 0.5f);

            float tw = touchpad.touch_width;
            float th = touchpad.touch_height;
            if (tw < 0.01f)
              tw = 1.0f;
            if (th < 0.01f)
              th = 1.0f;

            // Build local transform first, then apply touchpad world matrix
            glm::mat4 localTransform = glm::mat4(1.0f);
            localTransform = glm::translate(
                localTransform,
                glm::vec3(touchpad.touch_offset[0], touchpad.touch_offset[1],
                          touchpad.touch_offset[2]));
            localTransform = glm::rotate(
                localTransform, glm::radians(touchpad.touch_rotation[1]),
                glm::vec3(0, 1, 0));
            localTransform = glm::rotate(
                localTransform, glm::radians(touchpad.touch_rotation[0]),
                glm::vec3(1, 0, 0));
            localTransform = glm::rotate(
                localTransform, glm::radians(touchpad.touch_rotation[2]),
                glm::vec3(0, 0, 1));
            localTransform =
                glm::scale(localTransform, glm::vec3(tw, 0.02f, th));

            glm::mat4 touchpadWorld =
                getModelMatrixWithoutGyro(w.model, selected_mesh);
            glm::mat4 rectModel = touchpadWorld * localTransform;

            shaderUniformMat4(w.grid_shader, "model", rectModel);
            shaderUniformVec3(w.grid_shader, "gridColor",
                              glm::vec3(1.0f, 0.0f, 1.0f));

            glBindVertexArray(w.touch_area_vao);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            shaderUniformFloat(w.grid_shader, "alpha", 1.0f);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glBindVertexArray(0);
            shaderUniformFloat(w.grid_shader, "alpha", 0.5f);

            glUseProgram(0);
          }
        }
      }

      glUseProgram(0);
#if defined(_WIN32)
      if (w.transparent_overlay_hwnd) {
        // The companion window is what's actually visible - copy this
        // frame into it instead of presenting to the (hidden) GLFW
        // window. See createTransparentOverlay()'s comment for why this
        // exists. glReadPixels (inside updateTransparentOverlay) must
        // run before the swap: it reads the back buffer that was just
        // rendered into, and swapping first would leave it reading
        // stale/undefined content instead.
        updateTransparentOverlay(w);
      }
#endif
      glfwSwapBuffers(w.glfw_window);
    }
  }
}

// ----------------------------------------------------------------------
//  RESOURCE CLEANUP
// ----------------------------------------------------------------------
// Releases every GPU resource and controller/sensor handle owned by a
// controller window before it's torn down.
//
// Previously, closing a window via the UI (close_window() ->
// removeControllerWindow()) only called glfwDestroyWindow() - it left the
// SDL_GameController/SDL_Joystick handle open, any active gyro/accel
// sensors open, and every shader program, VAO/VBO/EBO, and texture
// belonging to that window's grid/lighting/touch-area/pivot/axis geometry
// AND every mesh in its model leaked (both on the GPU and on SDL's side).
// Only a full app exit (SDL_Quit()) ever implicitly cleaned any of that
// up. Opening and closing windows repeatedly within one session leaked a
// little more each time. Called from both removeControllerWindow()
// (user-initiated close) and destroyWindows() (app exit).
static void releaseControllerWindowResources(controller_window &w) {
  shutdownNetwork(w);
#if defined(_WIN32)
  // Must happen before the GLFW window is destroyed below -
  // destroyTransparentOverlay() calls glfwShowWindow() on it.
  destroyTransparentOverlay(w);
#endif
  if (w.sdl_controller) {
    SDL_CloseGamepad(w.sdl_controller);
    w.sdl_controller = nullptr;
  } else if (w.sdl_joystick) {
    SDL_CloseJoystick(w.sdl_joystick);
    w.sdl_joystick = nullptr;
  }
  w.gyro_sensor = nullptr;
  w.accel_sensor = nullptr;
  w.gyro_enabled = false;

  if (!w.glfw_window)
    return;

  // GL objects belong to this window's own context. Deleting from the
  // wrong current context is a silent no-op, so make this window's
  // context current first - the same pattern used when it's created and
  // drawn (see createControllerWindow() / drawControllerWindows()).
  glfwMakeContextCurrent(w.glfw_window);

  GLuint programs[] = {w.grid_shader, w.shader, w.light_source_shader,
                       w.touch_shader};
  for (GLuint p : programs)
    if (p)
      glDeleteProgram(p);

  auto delBuf = [](GLuint &id) {
    if (id) {
      glDeleteBuffers(1, &id);
      id = 0;
    }
  };
  auto delVao = [](GLuint &id) {
    if (id) {
      glDeleteVertexArrays(1, &id);
      id = 0;
    }
  };

  delBuf(w.grid_vbo);
  delBuf(w.grid_ibo);
  delVao(w.grid_vao);

  delBuf(w.lighting_vertex_data);
  delBuf(w.lighting_normal_data);
  delBuf(w.lighting_texture_data);
  delVao(w.lighting_vao);

  delBuf(w.touch_area_vbo);
  delBuf(w.touch_area_ebo);
  delBuf(w.touch_area_wire_ebo);
  delVao(w.touch_area_vao);

  delBuf(w.pivot_vbo);
  delVao(w.pivot_vao);

  delBuf(w.axis_vbo);
  delVao(w.axis_vao);

  for (Mesh &mesh : w.model.meshes) {
    for (Texture &tex : mesh.textures)
      deleteTexture(tex.id); // glDeleteTextures no-ops safely on id==0
    delBuf(mesh.vbo);
    delBuf(mesh.ebo);
    delVao(mesh.vao);
  }
}

void destroyWindows() {
  for (controller_window &w : windows) {
    releaseControllerWindowResources(w);
    glfwDestroyWindow(w.glfw_window);
  }
}

unsigned getFrameCapHz() {
  unsigned lowest = 0;
  for (const controller_window &w : windows) {
    unsigned cap = w.frame_cap > 0 ? w.frame_cap : 60;
    if (lowest == 0 || cap < lowest)
      lowest = cap;
  }
  return lowest > 0 ? lowest : 60;
}

void minimizeControllerWindow(controller_window &w) {
#if defined(_WIN32)
  // Deliberately does NOT call glfwIconifyWindow() when a companion
  // window is active - see controller_window::overlay_minimized's
  // declaration in controller_window.h for why that combination
  // corrupts GLFW's own iconified tracking. The real GLFW window is
  // already hidden (see createTransparentOverlay()) and has no taskbar
  // presence to iconify anyway; all "minimize" actually needs to do is
  // hide the companion (the only visible window) and let
  // updateTransparentOverlay() skip its work while overlay_minimized is
  // set, which it already checks instead of GLFW_ICONIFIED.
  if (w.transparent_overlay_hwnd) {
    w.overlay_minimized = true;
    ShowWindow((HWND)w.transparent_overlay_hwnd, SW_HIDE);
    return;
  }
#endif
  glfwIconifyWindow(w.glfw_window);
}

void maximizeControllerWindow(controller_window &w) {
  glfwMaximizeWindow(w.glfw_window);
#if defined(_WIN32)
  if (w.transparent_overlay_hwnd)
    glfwHideWindow(w.glfw_window);
#endif
}

void restoreControllerWindow(controller_window &w) {
#if defined(_WIN32)
  // Reversing our own minimized state never touches the real GLFW
  // window's visibility at all (it was never shown), so there's no
  // decoration flash here the way there briefly is coming back from a
  // real glfwMaximizeWindow()/glfwRestoreWindow() cycle below.
  if (w.transparent_overlay_hwnd && w.overlay_minimized) {
    w.overlay_minimized = false;
    ShowWindow((HWND)w.transparent_overlay_hwnd, SW_SHOWNOACTIVATE);
    return;
  }
#endif
  glfwRestoreWindow(w.glfw_window);
#if defined(_WIN32)
  if (w.transparent_overlay_hwnd)
    glfwHideWindow(w.glfw_window);
#endif
}

void removeControllerWindow(unsigned ID) {
  for (unsigned i = 0; i < windows.size(); ++i) {
    if (windows[i].ID == ID) {
      releaseControllerWindowResources(windows[i]);
      glfwDestroyWindow(windows[i].glfw_window);
      windows.erase(windows.begin() + i);
      break;
    }
  }
}

controller_window *getControllerWindow(unsigned ID) {
  for (unsigned i = 0; i < windows.size(); ++i) {
    if (windows[i].ID == ID) {
      return &windows[i];
    }
  }
  return nullptr;
}

controller_window *getLastWindow() { return &windows.back(); }
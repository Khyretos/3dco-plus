#include "controller_window.h"
#include "cube_info.h"
#include "icon_data.h"
#include "keyboard_input.h"
#include "settings_window.h"
#include "shader.h"
#include "shaders.h"
#include <SDL3/SDL_joystick.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <spdlog/spdlog.h>
#include <unordered_map>

extern unsigned selected_tab;
extern int selected_mesh;
extern std::vector<window_tab> tabs;

extern bool g_log_controller;
extern bool g_log_keyboard;
extern bool g_log_mouse;

extern std::string config_base_path;
extern bool gQuit;
extern GLFWwindow *glfw_settings_window;

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
  w.glfw_window =
      glfwCreateWindow(defaultWidth, defaultHeight, title.c_str(), NULL, NULL);
  if (!w.glfw_window) {
    spdlog::error("Failed to create controller window: {}", title);
    glfwTerminate();
    return;
  }
  glfwMakeContextCurrent(w.glfw_window);
  glEnable(GL_MULTISAMPLE);

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

  windows.push_back(w);
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

  // ---- Early exit if no controller is connected ----
  if (!w.sdl_controller && !w.sdl_joystick) {
    for (auto &mesh : w.model.meshes) {
      mesh.press = 0.0f;
      mesh.highlight_value = 0.0f;
      mesh.pull = 0.0f;
      mesh.axis_highlight_value = 0.0f;
      mesh.travel_value = 0.0f;
      mesh.travel_signed = 0.0f;
    }
    return;
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
          // ---- Handle accumulated scroll ----
          if (value == "mouse_scroll_xy" || value == "mouse_scroll_x" ||
              value == "mouse_scroll_y") {
            // Accumulate scroll values with clamping to prevent overflow
            const float MAX_SCROLL = 1.0f;
            float scrollX = globalScrollDx;
            float scrollY = globalScrollDy;

            // Only accumulate if there's actual scroll movement
            if (fabs(scrollX) > 0.001f || fabs(scrollY) > 0.001f) {
              // Apply scroll sensitivity
              scrollX *= 0.5f;
              scrollY *= 0.5f;

              w.scroll_accum_x =
                  std::max(-MAX_SCROLL,
                           std::min(MAX_SCROLL, w.scroll_accum_x + scrollX));
              w.scroll_accum_y =
                  std::max(-MAX_SCROLL,
                           std::min(MAX_SCROLL, w.scroll_accum_y + scrollY));
              w.scroll_accum_magnitude =
                  sqrt(w.scroll_accum_x * w.scroll_accum_x +
                       w.scroll_accum_y * w.scroll_accum_y);
            }

            // Apply the accumulated scroll to the mesh
            if (value == "mouse_scroll_xy") {
              mesh.press = w.scroll_accum_magnitude > 0.01f ? 1.0f : 0.0f;
              mesh.highlight_value = w.scroll_accum_magnitude;
              mesh.stick_X = w.scroll_accum_x * 32767.0f;
              mesh.stick_Y = w.scroll_accum_y * 32767.0f;
              mesh.pull = w.scroll_accum_magnitude * 32767.0f;
              continue;
            } else if (value == "mouse_scroll_x") {
              mesh.press = fabs(w.scroll_accum_x) > 0.01f ? 1.0f : 0.0f;
              mesh.highlight_value = fabs(w.scroll_accum_x);
              mesh.stick_X = w.scroll_accum_x * 32767.0f;
              mesh.pull = fabs(w.scroll_accum_x) * 32767.0f;
              continue;
            } else if (value == "mouse_scroll_y") {
              mesh.press = fabs(w.scroll_accum_y) > 0.01f ? 1.0f : 0.0f;
              mesh.highlight_value = fabs(w.scroll_accum_y);
              mesh.stick_Y = w.scroll_accum_y * 32767.0f;
              mesh.pull = fabs(w.scroll_accum_y) * 32767.0f;
              continue;
            }
          }

          dx *= w.mouse_sensitivity;
          dy *= w.mouse_sensitivity;
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
          float val = dx * w.mouse_sensitivity;
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
          float val = dy * w.mouse_sensitivity;
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
                float angle = glm::angle(glm::quat_cast(w.gyro_matrix));
                if (angle > 10.0f) {
                  w.gyro_matrix = glm::mat4(1.0f);
                  if (w.gyro_debug_logging)
                    spdlog::warn("Gyro reset due to excessive drift");
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
              direction = "↗"; // Up-Right
            else if (angle >= 67.5f && angle < 112.5f)
              direction = "↑"; // Up
            else if (angle >= 112.5f && angle < 157.5f)
              direction = "↖"; // Up-Left
            else if (angle >= 157.5f && angle < 202.5f)
              direction = "←"; // Left
            else if (angle >= 202.5f && angle < 247.5f)
              direction = "↙"; // Down-Left
            else if (angle >= 247.5f && angle < 292.5f)
              direction = "↓"; // Down
            else if (angle >= 292.5f && angle < 337.5f)
              direction = "↘"; // Down-Right
            else
              direction = "→"; // Right

            spdlog::info("[mouse] moved {}", direction);
            last_log_mouse_x = static_cast<float>(globalMouseX);
            last_log_mouse_y = static_cast<float>(globalMouseY);
          }

          if (fabs(globalScrollDx) > 0.01f || fabs(globalScrollDy) > 0.01f) {
            spdlog::info("[mouse] scroll ({:.1f}, {:.1f})", globalScrollDx,
                         globalScrollDy);
          }
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

        // ---- Decay scroll accumulation (optimized) ----
        static const float DECAY = 0.98f;
        static const float EPSILON = 0.0001f;

        if (fabs(w.scroll_accum_x) > EPSILON) {
          w.scroll_accum_x *= DECAY;
          if (fabs(w.scroll_accum_x) < EPSILON)
            w.scroll_accum_x = 0.0f;
        }
        if (fabs(w.scroll_accum_y) > EPSILON) {
          w.scroll_accum_y *= DECAY;
          if (fabs(w.scroll_accum_y) < EPSILON)
            w.scroll_accum_y = 0.0f;
        }

        if (fabs(w.scroll_accum_x) > EPSILON ||
            fabs(w.scroll_accum_y) > EPSILON) {
          w.scroll_accum_magnitude = sqrt(w.scroll_accum_x * w.scroll_accum_x +
                                          w.scroll_accum_y * w.scroll_accum_y);
        } else {
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
    } else {
      w.pivot_dragging = false;
      w.pivot_drag_mesh_index = -1;
      w.mouse_first_click = true;
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
        // For import preview windows, just close the window and remove its tab
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
  int slices = 100;
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
  w.projection_matrix = glm::perspective(
      glm::radians(45.0f), (float)window_width / window_height, 0.1f, 100.0f);
  shaderUniformMat4(shader, "projection", w.projection_matrix);
  glUseProgram(0);
}

void drawControllerWindows() {
  for (controller_window &w : windows) {
    if (glfwWindowShouldClose(w.glfw_window))
      continue;
    if (!glfwGetWindowAttrib(w.glfw_window, GLFW_ICONIFIED)) {
      glfwMakeContextCurrent(w.glfw_window);
      glfwSwapInterval(w.swap_interval);
      w.deltaTime = glfwGetTime() - w.lastTime;
      w.lastTime = glfwGetTime();

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
                   w.bg_color[2] * w.bg_color[3], 1.0f * w.bg_color[3]);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      if (w.grid) {
        glBindVertexArray(w.grid_vao);
        glUseProgram(w.grid_shader);
        glEnableVertexAttribArray(0);
        glm::mat4 grid_model = glm::mat4(1.0f);
        grid_model =
            glm::translate(grid_model, glm::vec3(-50.0f, 0.0f, -50.0f));
        grid_model = glm::scale(grid_model, glm::vec3(100.0f, 0.0f, 100.0f));
        shaderUniformMat4(w.grid_shader, "model", grid_model);
        shaderUniformVec3(w.grid_shader, "gridColor",
                          glm::vec3(0.5f, 0.5f, 0.5f));
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
      drawModel(w.model, w.shader, highlight, globalHighlight);

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
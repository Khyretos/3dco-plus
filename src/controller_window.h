#ifndef CONTROLLER_WINDOW_H
#define CONTROLLER_WINDOW_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "stb_image.h"

#include <array>
#include <map>
#include <math.h>
#include <memory>
#include <string>
#include <vector>

#include "model.h"
#include <GLFW/glfw3.h>
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

// typedefs (unchanged)
typedef struct direct_light_struct {
  std::string name = "Directional Light 1";
  glm::vec3 direction = glm::vec3(0.25f, -1.0f, 0.0f);
  float color[3] = {1.0f, 1.0f, 1.0f};
  float ambient = 0.4f;
  float diffuse = 0.8f;
  float specular = 1.0f;
} direct_light;

typedef struct point_light_struct {
  std::string name = "Point Light 1";
  glm::vec3 position = glm::vec3(0.0);
  float intensity = 0.5f;
  float constant = 1.0f;
  float linear = 0.09f;
  float quadratic = 0.032f;
  float color[3] = {1.0f, 1.0f, 1.0f};
  glm::vec3 ambient = glm::vec3(0.05f, 0.05f, 0.05f);
  glm::vec3 diffuse = glm::vec3(0.8f, 0.8f, 0.8f);
  glm::vec3 specular = glm::vec3(1.0f, 1.0f, 1.0f);
  bool hide = false;
} point_light;

typedef struct spot_light_struct {
  std::string name = "Spot Light 1";
  glm::vec3 position = glm::vec3(0.0f, 0.0f, 2.0f);
  glm::vec3 direction = glm::vec3(0.0, 0.0f, -1.0f);
  float yaw = 0.0f;
  float pitch = 0.0f;
  float cutoff = 20.0f;
  float outer_cutoff = 50.0f;
  float intensity = 0.5f;
  float constant = 1.0f;
  float linear = 0.09f;
  float quadratic = 0.032f;
  float color[3] = {1.0f, 1.0f, 1.0f};
  glm::vec3 ambient = glm::vec3(0.05f, 0.05f, 0.05f);
  glm::vec3 diffuse = glm::vec3(0.8f, 0.8f, 0.8f);
  glm::vec3 specular = glm::vec3(1.0f, 1.0f, 1.0f);
  bool hide = false;
} spot_light;

typedef struct controller_window_struct {
  GLFWwindow *glfw_window;
  unsigned ID;

  union {
    SDL_GameController *sdl_controller = nullptr;
    SDL_Joystick *sdl_joystick;
  };
  bool is_gamecontroller = false; // true if opened as gamecontroller
  int joystick_index = -1;        // device index

  // Sensors
  SDL_Sensor *gyro_sensor = nullptr;
  SDL_Sensor *accel_sensor = nullptr;
  bool gyro_enabled = false;
  bool gyro_toggled = false;
  bool gyro_debug_logging = false;
  glm::mat4 gyro_matrix = glm::mat4(1.0f);
  float gyro_data[3] = {0.0f, 0.0f, 0.0f};
  Uint64 gyro_time = 0;
  int reset_gyro_button1 = -1;
  int reset_gyro_button2 = -1;
  int gyro_correction = 5;
  float gyro_sensitivity = 5.0f;
  // Logging
  std::shared_ptr<spdlog::logger> logger;

  // Mouse state (updated every frame)
  float mouse_x = 0.0f;       // current cursor X (window coords)
  float mouse_y = 0.0f;       // current cursor Y
  float last_mouse_x = 0.0f;  // previous frame X
  float last_mouse_y = 0.0f;  // previous frame Y
  float mouse_delta_x = 0.0f; // movement since last frame
  float mouse_delta_y = 0.0f;
  bool mouse_buttons[GLFW_MOUSE_BUTTON_LAST + 1] = {
      false}; // current button states
  bool mouse_buttons_prev[GLFW_MOUSE_BUTTON_LAST + 1] = {
      false}; // for edge detection
  float mouse_sensitivity =
      0.01f; // scale factor for mouse->stick mapping (reduced from 0.005)

  // ---- Scroll state accumulation ----
  float scroll_accum_x = 0.0f;
  float scroll_accum_y = 0.0f;
  float scroll_accum_magnitude = 0.0f;
  float scroll_accum_decay = 0.9f; // Decay factor per frame

  // ---- Touchpoint mouse tracking ----
  std::unordered_map<int, double> touchpoint_last_move_time;
  double mouse_idle_timeout = 0.05; // seconds (~2-3 frames @ 60fps)

  bool left_click = false;
  double left_click_x = 0;
  double left_click_y = 0;
  bool right_click = false;
  double right_click_x = 0;
  double right_click_y = 0;

  bool always_on_top = false;
  bool borderless = false;
  bool drag_to_move = false;
  bool scroll_to_resize = false;
  bool grid = false;
  int swap_interval = 1;
  bool wireframe = false;
  Uint8 frame_cap = 60;
  float bg_color[4] = {0.256f, 0.2f, 0.3f, 1.0f};
  bool freelook = false;

  double deltaTime = 0.0f;
  double lastTime = 0.0f;
  double lastFrame = 0.0f;

  bool mouse_first_click = true;
  double prev_mouse_x = 0.0;
  double prev_mouse_y = 0.0;

  int last_highlight_index = -1;

  float camera_distance = 3.5f;
  float camera_yaw = 0.0f;
  float camera_pitch = 89.999f;
  float camera_roll = 0.0f;
  glm::vec3 camera_position = glm::vec3(0.0f, 0.0f, 3.0f);
  glm::vec3 camera_target = glm::vec3(0.0f, 0.0f, 0.0f);

  int move_speed = 5;
  int turn_speed = 5;
  int mouse_sens = 5;
  float freelook_yaw = 180.0f;
  float freelook_pitch = 0.0f;
  glm::vec3 freelook_position = glm::vec3(0.0f, 0.5f, 3.0f);
  glm::vec3 freelook_direction = glm::vec3(0.0f, 0.0f, -1.0f);

  float accel_data[3] = {0.0f, 0.0f, 0.0f};
  Uint64 accel_time = 0;

  glm::mat4 view_matrix = glm::mat4(1.0f);
  glm::mat4 projection_matrix = glm::mat4(1.0f);

  GLuint grid_shader = 0;
  GLuint shader = 0;
  GLuint light_source_shader = 0;

  GLuint grid_vbo = 0;
  GLuint grid_vao = 0;
  GLuint grid_ibo = 0;
  GLuint grid_length = 0;

  std::vector<direct_light> direct_lights;
  std::vector<point_light> point_lights;
  std::vector<spot_light> spot_lights;

  GLuint lighting_vertex_data = 0;
  GLuint lighting_normal_data = 0;
  GLuint lighting_texture_data = 0;
  GLuint lighting_vao = 0;
  GLuint lighting_ebo = 0;

  std::string model_name = "";
  std::string mesh_name = "";
  Model model;

  ImportPreviewData import_preview;
  bool is_import_preview = false;

  float last_axis_values[32] = {0.0f};
  Uint8 last_hat_values[16] = {SDL_HAT_CENTERED};
  struct TouchpadState {
    Uint8 state; // 0=not touching, 1=touching, 2=released (SDL uses 1 for down,
                 // 2 for up)
    float x, y;
  };
  TouchpadState touchpad_data[4][2]; // up to 4 touchpads, 2 fingers each
  bool last_button_values[64] = {};
  bool last_joy_button_values[128] = {};
  bool highlight_enabled = false;
  float highlight_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  std::map<int, std::array<float, 3>> original_colors;
  GLuint touch_area_vao = 0;
  GLuint touch_area_vbo = 0;
  GLuint touch_area_ebo = 0;
  GLuint touch_area_elements = 0;
  bool show_touch_area = false;
  GLuint touch_shader = 0;
  GLuint touch_area_wire_ebo = 0;
  GLuint touch_area_elements_tri = 0;
  GLuint touch_area_elements_wire = 0;
  GLuint pivot_vao = 0;
  GLuint pivot_vbo = 0;
  int pivot_segments = 20;

  GLuint axis_vao = 0;
  GLuint axis_vbo = 0;
  GLuint axis_elements = 0;

  bool pivot_dragging = false;
  double pivot_drag_start_screen_x = 0.0;
  double pivot_drag_start_screen_y = 0.0;
  glm::vec3 pivot_drag_start_world = glm::vec3(0.0f);
  int pivot_drag_mesh_index = -1;

  float camera_offset_x = 0.0f; // horizontal pan
  float camera_offset_y = 0.0f; // vertical pan

} controller_window;

// Function declarations (unchanged)
void createControllerWindow(std::string title, std::string model_path);
void lightingSpecification(controller_window &w);
void createShader(GLuint &shader_id, const char *vs_source,
                  const char *fs_source);
void update_camera(controller_window &w, GLuint &shader, int window_width,
                   int window_height);
controller_window *getLastWindow();
controller_window *getControllerWindow(unsigned ID);
void controller_window_input();
void controller_sdl_events(SDL_Event *event);
void removeControllerWindow(unsigned ID);
void destroyWindows();
void make_grid(controller_window &w);
void drawControllerWindows();
void controller_framebuffer_size_callback(GLFWwindow *window, int width,
                                          int height);
void controller_window_size_callback(GLFWwindow *window, int width, int height);
void controller_window_scroll_callback(GLFWwindow *window, double xoffset,
                                       double yoffset);
void controller_window_iconify_callback(GLFWwindow *window, int iconified);

void createTouchAreaRect(controller_window &w);
#endif
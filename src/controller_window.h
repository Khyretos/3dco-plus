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
#include <SDL3/SDL.h>
#include <array>
#include <glad/glad.h>
#include <map>
#include <math.h>
#include <memory>
#include <set>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

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
    SDL_Gamepad *sdl_controller = nullptr;
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
      0.5f; // scale factor for mouse->stick mapping (reduced from 0.005)

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
  // ---- Drag-to-move tracking state ----
  // Screen-space (not window-relative) cursor position captured when the
  // drag started, and the window position at that same moment - used to
  // compute how far to move the window each frame without compounding
  // rounding error. See controller_window_input() for why this needs to
  // be screen-space: window-relative mouse coordinates change meaning
  // the moment the window itself moves, since they're relative to a
  // target that's no longer stationary.
  bool drag_moving = false;
  double drag_move_anchor_x = 0.0;
  double drag_move_anchor_y = 0.0;
  int drag_move_start_win_x = 0;
  int drag_move_start_win_y = 0;
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
  // lighting_ebo removed - declared but never created or used anywhere;
  // the lighting geometry is drawn without an index buffer.

  std::string model_name = "";
  std::string mesh_name = "";
  Model model;

  ImportPreviewData import_preview;
  bool is_import_preview = false;

  float last_axis_values[32] = {0.0f};
  Uint8 last_hat_values[16] = {SDL_HAT_CENTERED};
  struct TouchpadState {
    bool down; // true if touching, false otherwise
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

  bool transparent_bg = false;
  // Independent of transparent_bg: whether mouse clicks pass through this
  // window to whatever is behind it. Enabling transparency turns this on
  // by default (a transparent overlay usually shouldn't eat clicks), but
  // it can be toggled back off independently, e.g. to reposition a
  // transparent window, or turned on for an opaque overlay too.
  bool click_through = false;
  // Set once at window creation from glfwGetWindowAttrib(GLFW_TRANSPARENT_
  // FRAMEBUFFER) — the authoritative signal for whether this GPU driver /
  // display server combo actually granted a transparent framebuffer, as
  // opposed to just accepting the hint. Used to warn the user in the UI
  // instead of silently showing an opaque window when they enable
  // "Transparent Background" on a system that can't do it.
  bool transparency_supported = true;
  std::string window_title;

#if defined(_WIN32)
  // See createTransparentOverlay()'s comment in controller_window.cpp for
  // the full explanation. Short version: GLFW's own window class uses
  // CS_OWNDC, which Microsoft's docs say is incompatible with
  // WS_EX_LAYERED - so real per-pixel window transparency is impossible
  // on the GLFW window itself. When Transparent Background is on, this
  // becomes a second, plain HWND (no CS_OWNDC) that's the visible,
  // clickable window; the GLFW window keeps rendering normally but
  // hidden, and every frame its pixels are copied into this window via
  // UpdateLayeredWindow. Typed as void* rather than HWND so this
  // cross-platform header doesn't need <windows.h>. Null when Transparent
  // Background is off (the normal GLFW window is visible directly).
  void *transparent_overlay_hwnd = nullptr;

  // ---- Async readback state for updateTransparentOverlay() ----
  // A plain glReadPixels() straight into client memory forces the driver
  // to wait for every previously issued GPU command to finish before it
  // returns - a full pipeline stall. Confirmed on NVIDIA: while a
  // fullscreen/borderless game has the GPU's attention, the driver can
  // deprioritize this hidden background context's command execution
  // enough that the stall freezes the whole app, since gamepad polling
  // shares the same single thread (see Input()/Draw() in main.cpp). AMD
  // doesn't exhibit this. The fix is to read into a PBO (an
  // asynchronous, non-blocking issue) and poll a fence with a zero
  // timeout (guaranteed non-blocking by the GL spec) instead of waiting
  // - see updateTransparentOverlay()'s definition for the full
  // explanation. Two PBOs are used round-robin so one frame's read can
  // still be in flight while the next frame's render proceeds.
  GLuint overlay_pbo[2] = {0, 0};
  GLsync overlay_fence[2] = {nullptr, nullptr};
  bool overlay_pbo_pending[2] = {false, false};
  int overlay_pbo_write_index = 0;
  // The exact width/height each PBO slot's storage was allocated for
  // when its read was issued (via glBufferData in the "issue" phase of
  // updateTransparentOverlay()). Needed because the window can be
  // resized between when a slot's read is issued and when it's later
  // consumed (mapped) - using the CURRENT frame's width/height at map
  // time instead of what that specific slot was actually sized for
  // caused "glMapBufferRange failed" whenever a resize happened while a
  // read was still in flight (requesting a map range larger than what
  // was actually allocated fails GL's range validation).
  int overlay_pbo_width[2] = {0, 0};
  int overlay_pbo_height[2] = {0, 0};

  // Set by minimizeControllerWindow()/restoreControllerWindow() only -
  // updateTransparentOverlay() uses this instead of GLFW_ICONIFIED to
  // decide whether to hide the companion window and skip rendering.
  // Calling glfwIconifyWindow() together with glfwHideWindow() (which
  // the companion mechanism needs to keep the real GLFW window hidden)
  // turned out to corrupt GLFW's own internal iconified tracking - after
  // that combination, GLFW_ICONIFIED stopped reliably reading true on
  // later frames, so the companion never got hidden and stayed frozen
  // showing its last frame. Tracking minimized state ourselves sidesteps
  // needing to know how GLFW's internal state reacts to that unusual
  // combination of calls at all.
  bool overlay_minimized = false;
#endif

  int preferred_guid_index = -1; // ordinal among devices with same GUID
  std::string preferred_guid;
  std::string preferred_name;
  std::string global_shader_name;
  std::string preferred_serial;
  std::string preferred_path;

  // ---- Network settings (0 = sender, 1 = receiver) ----
  bool network_enabled = false;
  int network_mode = 0; // 0 = sender, 1 = receiver
  std::string network_ip = "127.0.0.1";
  int network_port = 5000;
  int network_protocol = 0;   // 0 = UDP, 1 = TCP
  int network_send_rate = 60; // Hz; 0 = max (every frame)
  double network_last_send_time = 0.0;
  double network_last_reconnect_time =
      0.0; // for throttling TCP reconnect attempts

  bool network_logging = false; // enable verbose network debug logging
  std::string network_peer_ip =
      "unknown";             // for TCP receiver: connected peer IP
  int network_peer_port = 0; // for TCP receiver: connected peer port

  // Network sockets (use int everywhere; cast on Windows if needed)
  int network_socket = -1;        // for sending (UDP) / client (TCP)
  int network_listen_socket = -1; // for TCP server (receiver)
  bool network_tcp_connected = false;
  // True from the moment a non-blocking TCP connect() is issued until
  // its outcome is confirmed via select()/getsockopt(SO_ERROR) in
  // sendNetworkState() - see initNetwork()'s TCP-sender branch in
  // controller_window.cpp for why connect() must never be allowed to
  // block here.
  bool network_tcp_connecting = false;

  std::string network_tcp_buffer; // for accumulating partial TCP messages

  // ---- Network input state (sender collects, receiver applies) ----
  bool net_gamepad_buttons[32] = {}; // button index -> pressed
  float net_gamepad_axes[8] = {};    // axis index -> value (-1..1)
  bool net_joystick_buttons[128] = {};
  float net_joystick_axes[128] = {};
  std::set<SDL_Scancode> net_keyboard_keys; // held keys
  bool net_mouse_buttons[8] = {};
  float net_mouse_dx = 0;
  float net_mouse_dy = 0;

  // Network status for UI indicator
  int network_status =
      0; // 0=off, 1=trying/connecting, 2=active/received, 3=error
  double last_network_activity_time =
      0.0;                        // last time a packet was sent/received
  bool network_connected = false; // <-- ADD THIS LINE
  // Distinct from last_network_activity_time (which only tracks when
  // WE last sent something) - this tracks when we last actually heard
  // FROM the peer (a state packet, handshake, or heartbeat ack). Used
  // to detect a peer that's gone silent (crashed, network dropped,
  // etc.) via timeout, on both the sender and receiver side. See
  // sendNetworkState()/receiveNetworkState() in controller_window.cpp.
  double last_network_receive_time = 0.0;
  static constexpr double kNetworkTimeoutSeconds = 5.0;
  // Tracks the previous frame's network_connected value, so the
  // settings UI can detect the false->true transition and show a
  // temporary "just connected" confirmation instead of only the small
  // status circle. network_connected_toast_until is a glfwGetTime()
  // deadline - the toast shows while glfwGetTime() is still before it.
  bool network_was_connected = false;
  double network_connected_toast_until = 0.0;

  int preferred_index = -1;

  // Last sent network state for diffing
  bool last_sent_gamepad_buttons[32] = {};
  float last_sent_gamepad_axes[8] = {};
  bool last_sent_joystick_buttons[128] = {};
  float last_sent_joystick_axes[128] = {};
  std::set<SDL_Scancode> last_sent_keyboard_keys;
  bool last_sent_mouse_buttons[8] = {};
  float last_sent_mouse_dx = 0;
  float last_sent_mouse_dy = 0;
  float last_sent_gyro[3] = {0, 0, 0};
  bool last_sent_touchpad_finger[4][2] = {};
  float last_sent_touchpad_x[4][2] = {};
  float last_sent_touchpad_y[4][2] = {};

  float last_sent_gyro_matrix[16] = {0}; // for diffing gyro matrix
  bool network_gyro_reset = false; // set when gyro reset button combo pressed

  // Handshake flags
  bool network_handshake_ack = false;
  double network_last_handshake_sent = 0.0;

  double last_overlay_update_time = 0.0;       // in seconds
  double overlay_update_interval = 1.0 / 60.0; // 60 FPS

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

// Lowest frame_cap among currently open controller windows (defaulting
// to 60 if none are open), used by MainLoop() in main.cpp for
// sleep-based frame pacing. See that function's comment for why pacing
// no longer relies on vsync/swap_interval at all.
unsigned getFrameCapHz();
void make_grid(controller_window &w);
void drawControllerWindows();
void controller_framebuffer_size_callback(GLFWwindow *window, int width,
                                          int height);
void controller_window_size_callback(GLFWwindow *window, int width, int height);
void controller_window_scroll_callback(GLFWwindow *window, double xoffset,
                                       double yoffset);
void controller_window_iconify_callback(GLFWwindow *window, int iconified);
void createTouchAreaRect(controller_window &w);
void recreateControllerWindow(controller_window *w);
void setWindowClickThrough(GLFWwindow *window, bool enable);

#if defined(_WIN32)
// Create/destroy/update the Win32 layered companion window used for real
// per-pixel window transparency on Windows (see controller_window::
// transparent_overlay_hwnd's declaration above, and
// createTransparentOverlay()'s definition in controller_window.cpp, for
// the full explanation of why this exists). createTransparentOverlay()
// hides the GLFW window and shows the companion window in its place;
// destroyTransparentOverlay() reverses that. updateTransparentOverlay()
// must be called once per frame (from drawControllerWindows()) instead
// of glfwSwapBuffers() while the companion window exists - it copies the
// GLFW window's just-rendered frame into the companion window and keeps
// the companion window's position/size/topmost state following it.
void createTransparentOverlay(controller_window &w);
void destroyTransparentOverlay(controller_window &w);
void updateTransparentOverlay(controller_window &w);
#endif

// Wrappers for minimize/maximize/restore that behave correctly with the
// Windows companion window (see controller_window::overlay_minimized's
// declaration above for the minimize case, and the comment on
// maximizeControllerWindow()'s definition for maximize/restore). On
// platforms without the companion window these are equivalent to
// calling glfwIconifyWindow/glfwMaximizeWindow/glfwRestoreWindow
// directly. Settings UI should call these instead of the raw GLFW
// functions for any window that might have an active companion overlay.
void minimizeControllerWindow(controller_window &w);
void maximizeControllerWindow(controller_window &w);
void restoreControllerWindow(controller_window &w);

// Translates the window's raw-joystick mesh bindings (Input Type =
// Joystick, not Gamepad - see the comment on exportGamepadMapping()'s
// definition for why) into a standard gamecontrollerdb.txt line and
// appends it to the on-disk file, applying it immediately via
// SDL_AddGamepadMapping() too. Returns false (with an explanatory
// out_message) if there's nothing exportable - no joystick open, the
// device is already a recognized Gamepad, or no raw bindings exist.
bool exportGamepadMapping(controller_window &w, std::string &out_message);

// Single source of truth for "is this window currently minimized",
// covering both GLFW_ICONIFIED (used directly on Linux/macOS, and as a
// fallback on Windows if the companion window failed to create) and
// controller_window::overlay_minimized (the Windows companion-window
// path - see its declaration above for why GLFW_ICONIFIED alone can't
// be trusted there). drawControllerWindows()'s render gate and the
// system tray's per-controller menu both need this exact same check.
bool isControllerWindowMinimized(const controller_window &w);

// Network functions
void initNetwork(controller_window &w);
void shutdownNetwork(controller_window &w);
void sendNetworkState(controller_window &w);
void receiveNetworkState(controller_window &w);
#endif
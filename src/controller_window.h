#ifndef CONTROLLER_WINDOW_H
#define CONTROLLER_WINDOW_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Forward declaration only - avoids pulling in all of imgui.h just for
// a pointer type (see controller_window::input_history_imgui_ctx).
struct ImGuiContext;

#include "stb_image.h"

#include <array>
#include <map>
#include <math.h>
#include <memory>
#include <string>
#include <vector>

#include "input_history_types.h"
#include "model.h"
#include <GLFW/glfw3.h>
#include <SDL3/SDL.h>
#include <array>
#include <deque>
#include <glad/glad.h>
#include <map>
#include <math.h>
#include <memory>
#include <set>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>
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

#if defined(_WIN32)
// Real per-pixel window transparency on Windows, via a companion
// window - reusable by any GLFW window that needs this, not tied to
// controller_window specifically. See createCompanionWindow()'s
// definition in controller_window.cpp for the full explanation of why
// this exists (short version: GLFW's own window class uses CS_OWNDC,
// which is incompatible with WS_EX_LAYERED, so real per-pixel window
// transparency is impossible on a GLFW window itself - this is a
// second, plain HWND that mirrors it instead).
//
// Previously duplicated separately for controller windows and Input
// History windows (each with its own HWND field, its own WndProc, its
// own PBO/fence state) instead of sharing one implementation - which
// is exactly how Input History's copy silently fell behind and ended
// up missing several handlers (WM_NCHITTEST, keyboard forwarding,
// SetCapture) the original already had. One shared type/WndProc/set
// of functions now, used by both, so a fix or feature only ever needs
// to be made once.
struct CompanionWindow {
  // Typed as void* rather than HWND so this cross-platform header
  // doesn't need <windows.h>. Null when the companion doesn't exist
  // (transparency off, or not yet created).
  void *hwnd = nullptr;

  // ---- Async readback state for updateCompanionWindow() ----
  // A plain glReadPixels() straight into client memory forces the
  // driver to wait for every previously issued GPU command to finish
  // first - a full pipeline stall. Confirmed on NVIDIA: while a
  // fullscreen/borderless game has the GPU's attention, the driver can
  // deprioritize this hidden background context's command execution
  // enough that the stall freezes the whole app. The fix is to read
  // into a PBO (asynchronous) and poll a fence with a zero timeout
  // (guaranteed non-blocking) instead of waiting. Two PBOs are used
  // round-robin so one frame's read can still be in flight while the
  // next frame's render proceeds.
  GLuint pbo[2] = {0, 0};
  GLsync fence[2] = {nullptr, nullptr};
  bool pbo_pending[2] = {false, false};
  int pbo_write_index = 0;
  // The exact width/height each PBO slot's storage was allocated for
  // when its read was issued - needed because the window can be
  // resized between a slot's read being issued and it later being
  // consumed, and mapping a range larger than what was actually
  // allocated fails GL's range validation.
  int pbo_width[2] = {0, 0};
  int pbo_height[2] = {0, 0};
  double last_update_time = 0.0;

  // Set once at creation, read by both updateCompanionWindow() and
  // CompanionWndProc (via GWLP_USERDATA, not a global window-list
  // search - see createCompanionWindow()'s comment for why that's an
  // improvement over how this used to work).
  GLFWwindow *source_window = nullptr;
  // Points into the owner's own click-through bool field (e.g.
  // &w.click_through) rather than copying the value, so a live
  // checkbox toggle is reflected immediately without needing an
  // explicit sync step.
  bool *click_through = nullptr;

  // Settable externally - see controller_window::overlay_minimized's
  // old declaration for the full reasoning (GLFW's own iconified
  // tracking couldn't be trusted together with glfwHideWindow()).
  // Window types with no minimize concept of their own (Input
  // History) just never set this.
  bool minimized = false;
};

// Creates the companion window and hides source_window (which keeps
// rendering normally, just not shown directly). window_title is used
// for the companion's own native title (helps tools like OBS's Window
// Capture source find it by name instead of showing a bare "null").
void createCompanionWindow(CompanionWindow &cw, GLFWwindow *source_window,
                           bool *click_through_field,
                           const std::string &window_title);
void destroyCompanionWindow(CompanionWindow &cw);
// Call once per frame instead of glfwSwapBuffers() while the
// companion exists. update_interval throttles how often the companion
// actually refreshes (reduces DWM load); always_on_top mirrors the
// owner's own always-on-top state onto the companion HWND.
void updateCompanionWindow(CompanionWindow &cw, bool always_on_top,
                           double update_interval);

// Converts one BGRA, bottom-up, straight-alpha frame (what OpenGL
// produces) into what UpdateLayeredWindow needs and blits it. Pure
// function, no CompanionWindow coupling - takes void* rather than
// HWND so this cross-platform header doesn't need <windows.h>.
void blitOverlayFrame(void *hwnd, int width, int height,
                      const unsigned char *src);
#endif

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

  // ---- Scroll wheel highlight ----
  // A scroll tick is a single-frame discrete pulse, not a held state
  // like a real button - without some minimum visible duration it
  // would flash for 1/60th of a second and be imperceptible. This is
  // a clean, instant on/off timer (stay lit until this absolute time,
  // then snap off), not a gradual fade - a previous version decayed
  // the highlight multiplicatively over ~20 frames (~300ms at 60fps),
  // which read as a visible lag before the highlight turned off
  // rather than a clean button-like tap. scroll_accum_x/y were part
  // of an even earlier "scroll intensity" design (a continuously-
  // scaled highlight) that was discarded in favor of a plain on/off
  // response matching every other button binding - unused now.
  double scroll_highlight_until = 0.0;
  float scroll_highlight_duration_ms = 120.0f;

  // ---- Touchpoint mouse tracking ----
  std::unordered_map<int, double> touchpoint_last_move_time;
  double mouse_idle_timeout = 0.05; // seconds (~2-3 frames @ 60fps)
  // How long a mouse-bound touchpoint mesh sits idle before snapping
  // back to center (0.5, 0.5) - a mouse has no hardware self-centering
  // the way an analog stick does, so without this the visual position
  // would just sit wherever it last was, drifting further from center
  // the longer someone moved the mouse in one direction, with no way
  // back to neutral short of moving it the opposite way by hand.
  float touchpoint_recenter_seconds = 3.0f;
  // Per-window frame counter for the recenter idle-check (was a
  // function-static shared across every window, meaning the same
  // global count was divided among all of them - with N controller
  // windows open, any individual window's check fired roughly 1/N as
  // often as intended, since the "every 60 counts" threshold was being
  // consumed by whichever window's turn it happened to be, round-robin,
  // not per-window. Confirmed directly: with 5 windows, a specific
  // window's check fired about once every ~4 seconds instead of the
  // intended ~once per second.
  int touchpoint_check_frame_counter = 0;

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
  // Counts changes to the model made since the last save, for the
  // "N changes made without saving" indicator next to the Model
  // section header - reset to 0 wherever writeJson(model, ...)
  // actually runs. Covers the highest-traffic mutation points
  // (textures - add/remove/type/wrap/flip/rotation/scale/offset -
  // plus mesh add/remove/duplicate/visibility and model import), not
  // literally every slider in the Model section (position/rotation/
  // pivot/lighting/etc. aren't individually instrumented - there are
  // too many to cover exhaustively in one pass), so treat the count
  // as a useful approximation rather than a perfectly exhaustive
  // per-field tracker.
  int unsaved_change_count = 0;

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
  // Real per-pixel window transparency (see CompanionWindow's own doc
  // comment above for the full explanation). Field access changed
  // from the old scattered transparent_overlay_hwnd/overlay_pbo/
  // overlay_fence/etc. to transparent_overlay.hwnd/.pbo/.fence/etc.
  CompanionWindow transparent_overlay;
  // True for exactly one frame: the one right after this window (and
  // its companion) were created. Creating the companion hides this
  // window's own GLFW window via glfwHideWindow() - immediately
  // trying to bind a fresh GL context to a window that's still mid-
  // visibility-transition can fail on some Windows graphics drivers
  // ("WGL: Failed to make context current: The requested
  // transformation operation is not supported"), seen specifically
  // right when a new controller window is created. Skipping the
  // render for that one frame gives Windows a full frame to settle
  // the transition first; every frame after renders completely
  // normally. See drawControllerWindows()'s use of this flag.
  bool companion_first_frame_pending = true;
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

  // Companion window's own last-update timestamp now lives in
  // transparent_overlay.last_update_time instead of a standalone field
  // here.
  double overlay_update_interval = 1.0 / 60.0; // 60 FPS

  // ---- Input History (1.2.0) ----
  // See input_history.h for InputHistoryEntry and the window/capture
  // functions. Settings here are persisted per-window like Network's
  // are; the deque/state below is runtime-only.
  bool input_history_enabled = false;
  // 0 = Raw text, 1 = Fighting-game notation, 2+ = index into
  // kGlyphDisplayStyles (input_history.cpp) - i.e. which bundled glyph
  // pack to render buttons as icons in.
  int input_history_display_style = 0;
  int input_history_length = 20; // visible/retained entries in the live window
  bool input_history_capture_gamepad = true;
  bool input_history_capture_keyboard = false;
  bool input_history_capture_mouse = false;
  // Groups inputs landing in the same frame into one history entry
  // (e.g. "A+B") instead of one row per input - see the checkbox's
  // tooltip in settings_window.cpp for the fighting-game-vs-everything-
  // else framing this was specifically requested with.
  bool input_history_merge_simultaneous = false;
  // Which input drives the numpad-notation direction digit (5=neutral,
  // 1-9 otherwise) in Fighting-game notation mode. 0=Auto (whichever of
  // D-Pad/Left Stick is actually deflected; see
  // computeNumpadDirection() in input_history.cpp), 1=D-Pad only,
  // 2=Left Stick only, 3=Right Stick only - for lefty/unusual control
  // setups where "movement" isn't D-Pad-or-left-stick.
  int input_history_direction_source = 0;
  float input_history_opacity = 0.85f;
  // Background and content (text/glyphs) get independent transparency
  // instead of one blanket alpha over everything - e.g. a 70% black
  // background with fully opaque icons, or the reverse. opacity above
  // is kept as the background's alpha specifically; this is the
  // separate content one.
  float input_history_content_opacity = 1.0f;
  bool input_history_log_to_file = true;
  bool input_history_click_through = true;
  // Defaults true, matching the hardcoded always-topmost behavior this
  // setting replaces - existing configurations keep working exactly as
  // before unless someone explicitly turns it off. Unlike a controller
  // window's own Always on Top (which toggles GLFW_FLOATING on the
  // GLFW window directly), this is read by updateCompanionWindow() as
  // its always_on_top parameter each frame - see the Windows companion
  // window comments in controller_window.cpp for why Input History
  // goes through that path instead on Windows.
  bool input_history_always_on_top = true;
  // How far a trigger needs to be pulled (0-1) before it registers as
  // a "press" in the history - triggers are analog, so unlike a
  // button there's no natural on/off point without one. Exposed as a
  // setting since what counts as "pressed" is genuinely a matter of
  // taste/game (a fighting game player mashing a trigger-mapped button
  // wants a low threshold; someone just resting a finger on the
  // trigger doesn't want that registering as a press).
  float input_history_trigger_threshold = 0.1f;
  // false (default) = newest entry at the bottom, oldest at top,
  // scrolling upward as new inputs arrive - matches how a terminal/log
  // reads. true = newest at the top instead, so the most recent input
  // is always the first thing visible without needing to scroll -
  // useful for a small window where you don't want to watch it scroll.
  bool input_history_newest_on_top = false;
  // Show the ms-since-previous-input timing - independent of display
  // style now (previously baked into Fighting-Game Notation only),
  // since timing is equally meaningful for Raw text or a glyph style,
  // and for any input source, not just gamepad directions.
  bool input_history_show_timing = false;
  // A gap longer than this (ms) doesn't count as measured timing - see
  // InputHistoryEntry::timingReset's doc comment in
  // input_history_types.h.
  int input_history_timing_reset_ms = 3000;
  // Timing shown as milliseconds (default) or frames, for players who
  // think in frame data rather than wall-clock time. Frames are
  // computed against a fixed 60fps reference, the same assumption
  // fighting-game frame data conventionally uses regardless of a
  // game's actual render rate.
  bool input_history_timing_in_frames = false;
  // Max total time (ms) allowed for a compound motion (236, 623, 360,
  // etc.) to complete, from its first required direction to its last -
  // see detectMotionCompletion() in input_history.cpp. Real games vary
  // a lot here: Street Fighter 6 (the default's basis) uses roughly
  // 183ms for quarter-circles, 200ms for half-circles, and 533ms for
  // full-circle (360) motions - see the setting's own tooltip in
  // Settings for the full comparison against Tekken 8 and Guilty Gear
  // Strive. This app uses one flat window for every motion rather than
  // SF6's per-motion-length values, for simplicity.
  int input_history_motion_timeout_ms = 220;
  // Hold detection - shows a live-updating "Hold" + timer next to a
  // button's glyph/label while it's held past the threshold, pinned
  // at the newest-entry end (top or bottom, matching Newest Entry On
  // Top) until released. Off by default. Multiple simultaneous holds
  // (e.g. holding two buttons at once) each get their own entry.
  bool input_history_show_holds = false;
  // How long a press has to be held before it counts as a "hold"
  // rather than a tap - 150ms is a commonly-used rough threshold in
  // games for distinguishing a deliberate hold from a quick press.
  int input_history_hold_threshold_ms = 150;
  // Icon size in pixels for glyph display styles (square). Also scales
  // the text-fallback font size proportionally so a style that mixes
  // icons and text (most controller glyph packs don't have art for
  // every button - see input_history_glyphs.h) stays visually
  // consistent rather than having oddly-small fallback text next to
  // large icons or vice versa.
  int input_history_glyph_size = 28;
  // Text size for every column in the window (Input, Timing, Date/
  // Time) - previously only scoped to Raw/Notation's Input column,
  // which left the other two columns visually inconsistent with it.
  int input_history_font_size = 16;
  // Alternating row background shading - off by default alongside a
  // fully transparent background (0 opacity), since with the
  // background already invisible the alternating stripe was the only
  // thing left rendering, which isn't "no decoration, just glyphs and
  // text" like the setting implies.
  bool input_history_alternating_rows = true;
  // Whether a "return to neutral" (digit 5, stick/D-pad centered)
  // produces its own history entry. Off by default - most players
  // don't want a fresh line every time they let go of the stick, only
  // the fighting-game-notation crowd tends to want it, so this is
  // opt-in rather than opt-out.
  bool input_history_show_neutral_direction = false;
  // How close together (ms) two inputs need to land to be merged into
  // one entry when input_history_merge_simultaneous is on - previously
  // this was implicitly "within the same capture call" (~one frame,
  // as tight as ~16ms at 60fps), which is stricter than most people
  // mean by "simultaneous" (a human press of two buttons "at once" is
  // routinely 30-80ms apart). Now an explicit, tunable window instead.
  int input_history_simultaneous_window_ms = 50;
  // Adds a Date/Time column (leftmost) showing the real wall-clock
  // time each entry was captured, for whatever reason someone wants
  // to know exactly when a button was pressed (not just how long
  // since the last one).
  bool input_history_show_timestamp = false;
  // ---- Gyro ("flick") capture ----
  // Gyro is continuous, unlike a button - showing every small motion
  // would flood the history and bury real button presses (this is
  // the overflow concern raised directly), so this doesn't log
  // "gyro activity" as a continuous stream. Instead, motion below
  // input_history_gyro_threshold (deg/sec) is ignored entirely (hand
  // tremor, idle drift), and anything above it accumulates over a
  // short rolling window - crossing input_history_gyro_flick_threshold
  // (total degrees) within that window is what actually produces one
  // history entry (a "flick"), in whichever of the four directions
  // dominated that window, with a cooldown after each one so a single
  // continued motion doesn't spam repeated entries.
  bool input_history_capture_gyro = false;
  float input_history_gyro_threshold = 50.0f;       // deg/sec
  float input_history_gyro_flick_threshold = 25.0f; // degrees, accumulated
  int input_history_gyro_flick_window_ms = 150;
  int input_history_gyro_flick_cooldown_ms = 400;
  // Same "Drag to Move"/"Scroll to Resize" mechanism the controller
  // windows already have (see controller_window_scroll_callback() and
  // the drag_to_move handling in controller_window.cpp) - only
  // meaningful while Click-Through is off, same as for a controller
  // window, since click-through means this window never receives
  // mouse events at all.
  bool input_history_drag_to_move = false;
  bool input_history_scroll_to_resize = false;

  // Runtime-only state (not persisted) - see input_history.cpp
  std::deque<InputHistoryEntry> input_history_entries;
  bool input_history_last_gamepad_button[64] = {};
  bool input_history_gamepad_state_initialized = false;
  // Triggers are SDL axes, not buttons, so they need their own
  // rising-edge tracking separate from the button array above - see
  // captureInputHistory()'s trigger handling in input_history.cpp.
  bool input_history_last_trigger_state[2] = {}; // [left, right]
  std::array<bool, SDL_SCANCODE_COUNT> input_history_last_key_state{};
  bool input_history_key_state_initialized = false;
  std::array<bool, 8> input_history_last_mouse_state{};
  bool input_history_mouse_state_initialized = false;
  int input_history_last_dpad_dir = 0; // last numpad digit (5=neutral)
  int input_history_last_stick_dir[2] = {
      5, 5}; // [left, right] stick numpad digits
  // Rolling buffer of recent (digit, timestampMs) direction changes,
  // for compound-motion detection (236, 623, 360, etc.) - see
  // detectMotionCompletion() in input_history.cpp. Capped to a modest
  // size in captureInputHistory(), not unbounded.
  std::deque<std::pair<int, Uint64>> input_history_motion_buffer;
  // Currently-held inputs - see ActiveHold's doc comment in
  // input_history_types.h.
  std::vector<ActiveHold> input_history_active_holds;
  Uint64 input_history_last_event_ms = 0;
  // Rolling-window accumulation for gyro flick detection - see
  // input_history_capture_gyro's doc comment above.
  float input_history_gyro_accum_yaw = 0.0f;
  float input_history_gyro_accum_pitch = 0.0f;
  Uint64 input_history_gyro_window_start_ms = 0;
  Uint64 input_history_gyro_last_flick_ms = 0;
  FILE *input_history_log_file = nullptr;
  std::string input_history_log_path;

  GLFWwindow *input_history_glfw_window = nullptr;
  ImGuiContext *input_history_imgui_ctx = nullptr;
  bool input_history_backend_ready = false;
  // Drag-to-move tracking - same screen-space-anchor approach as
  // controller windows (see the big comment in controller_window.cpp's
  // drag_to_move handling for why screen space, not window-relative
  // coordinates, which change meaning the instant the window moves).
  bool input_history_drag_moving = false;
  double input_history_drag_move_anchor_x = 0.0;
  double input_history_drag_move_anchor_y = 0.0;
  int input_history_drag_move_start_win_x = 0;
  int input_history_drag_move_start_win_y = 0;

#if defined(_WIN32)
  // Real per-pixel transparency for the Input History window hits the
  // exact same CS_OWNDC/WS_EX_LAYERED wall as the controller windows do
  // (see CompanionWindow's doc comment above) - GLFW's own
  // GLFW_TRANSPARENT_FRAMEBUFFER support on Windows falls back to
  // DwmEnableBlurBehindWindow, which glfw/glfw#2731 documents as
  // unreliable on AMD (shows solid black instead of transparent).
  // Now shares controller_window's own CompanionWindow type/functions
  // (transparent_overlay above) instead of a separate, duplicated
  // implementation - see CompanionWindow's own doc comment for why
  // that duplication was a mistake in the first place (a duplicate
  // copy silently fell behind the original, missing several handlers
  // it should have had from day one). Field access changed from the
  // old input_history_overlay_hwnd/_pbo/_fence/etc. to
  // input_history_overlay.hwnd/.pbo/.fence/etc.
  CompanionWindow input_history_overlay;
#endif

} controller_window;

// SDL_GamepadButton index (0-20) -> human-readable name - defined in
// controller_window.cpp, used there for debug logging and here (via
// this extern) for Input History's Raw-text labels.
extern std::string button_names[21];

// All currently open controller windows - defined in
// controller_window.cpp. input_history.cpp iterates this directly to
// draw each window's Input History overlay without needing its own
// separate registry.
extern std::vector<controller_window> windows;

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

// ---- Input History (1.2.0, see input_history.h/.cpp) ----
// Declared here (rather than only in input_history.h) since main.cpp's
// Draw()/Input() need drawInputHistoryWindows()/captureInputHistory()
// without otherwise needing the rest of input_history.h's API
// (notation formatting, glyph style list, etc).
void captureInputHistory(controller_window &w);
void drawInputHistoryWindows();
void setInputHistoryEnabled(controller_window &w, bool enabled);
// Closes the log file and destroys the GLFW window/ImGui context (if
// any) for this window's Input History - called from
// releaseControllerWindowResources() so closing a controller tab
// doesn't leak either, the same way it already cleans up the
// transparent-overlay/network/etc. resources for that window.
void cleanupInputHistory(controller_window &w);
// Number of glyph display styles available (valid input_history_display_style
// values are 0=Raw, 1=Fighting-game notation, 2..N+1=glyph styles).
int inputHistoryGlyphStyleCount();
// Human-readable label for a given input_history_display_style value,
// for the settings dropdown.
std::string inputHistoryDisplayStyleName(int displayStyleIndex);
#endif
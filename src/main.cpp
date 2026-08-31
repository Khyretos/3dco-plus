#include "keyboard_input.h"
#include "log_window.h"
#include "settings.h"
#include "settings_window.h"
#include "tray_icon.h"
#include <SDL3/SDL_joystick.h>
#include <filesystem>
#include <iostream>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#if defined(_WIN32)
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

bool gQuit = false;

#if defined(_WIN32)
// ------------------------------------------------------------------
// Windows aggressively deprioritizes background (non-foreground)
// processes via "Efficiency Mode" / EcoQoS, and starves them further
// once a fullscreen/borderless game engages Game Mode to protect its
// own performance. With click-through enabled, every click on this
// app's overlay windows passes straight through to whatever is behind
// them — meaning those windows can never become the foreground/active
// window, so Windows treats this process as an idle background app and
// throttles its scheduling. Since the whole app is single-threaded
// (Input() then Draw(), one loop), that throttling delays gamepad
// polling right along with rendering, which is exactly the "input
// detection lag" that only shows up with click-through on AND
// something else actively rendering/competing for foreground priority.
//
// This is the same technique other "must stay responsive while a game
// has focus" companion apps (macro tools, RGB control software, etc.)
// use: explicitly opt the process out of power throttling, and nudge
// its scheduling priority up slightly so it isn't starved of CPU time
// while a demanding game is running.
// ------------------------------------------------------------------
static void DisableWindowsBackgroundThrottling() {
#if defined(PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
  // SetProcessInformation (kernel32.dll, Windows 8+) isn't declared in this
  // MinGW toolchain's headers even though the PROCESS_POWER_THROTTLING_*
  // enum/struct types are — same class of header/toolchain gap as the
  // glfwGetWin32Window issue earlier. Since the symbol is a plain export
  // from kernel32.dll regardless of what the headers declare, load it
  // dynamically via GetProcAddress instead of calling it directly. This
  // also naturally no-ops on any Windows version too old to have it.
  typedef BOOL(WINAPI * SetProcessInformation_t)(
      HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  SetProcessInformation_t pSetProcessInformation =
      kernel32 ? reinterpret_cast<SetProcessInformation_t>(
                     GetProcAddress(kernel32, "SetProcessInformation"))
               : nullptr;

  if (pSetProcessInformation) {
    PROCESS_POWER_THROTTLING_STATE PowerThrottling{};
    PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    PowerThrottling.StateMask = 0; // 0 = explicitly disable throttling
    if (!pSetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                                &PowerThrottling, sizeof(PowerThrottling))) {
      spdlog::warn(
          "Failed to disable process power throttling (GetLastError={})",
          GetLastError());
    } else {
      spdlog::info("Disabled Windows process power throttling (EcoQoS).");
    }
  } else {
    spdlog::warn("SetProcessInformation unavailable on this system/kernel32 — "
                 "skipping power-throttling opt-out.");
  }
#else
  spdlog::warn("PROCESS_POWER_THROTTLING_EXECUTION_SPEED not available in "
               "this SDK/toolchain — skipping power-throttling opt-out.");
#endif

  if (!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
    spdlog::warn("Failed to raise process priority (GetLastError={})",
                 GetLastError());
  } else {
    spdlog::info("Raised process priority to ABOVE_NORMAL_PRIORITY_CLASS.");
  }
}
#endif

void InitializeProgram() {
#if defined(_WIN32)
  DisableWindowsBackgroundThrottling();
#endif

  // ---- 1. Get writable config directory FIRST ----
  char *pref = SDL_GetPrefPath("", "3dco+");
  if (pref) {
    config_base_path = pref;
    SDL_free(pref);
  } else {
    // Fallback – should rarely happen
    config_base_path = SDL_GetBasePath();
    if (!config_base_path.empty() && config_base_path.back() != '/')
      config_base_path += '/';
  }

  // ---- 2. Create logs directory ----
  std::filesystem::create_directories(config_base_path + "/logs");

  // ---- 3. Set up logging: rotate at 5 MB, keep 3 files ----
  try {
    std::string log_path = config_base_path + "/logs/3dco+.log";
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_path, 5 * 1024 * 1024, 3);
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(
        "3dco+", spdlog::sinks_init_list{file_sink, console_sink});
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("3D Controller Overlay starting...");

    // Mirror every log line into the in-app log window's ring buffer so it
    // works the same on Windows, macOS, and Linux, whether or not a
    // console is attached to the process.
    initLogWindow(logger);
  } catch (const spdlog::spdlog_ex &ex) {
    std::cerr << "Log initialization failed: " << ex.what() << std::endl;
  }

  SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");

  // ---- 4. Init SDL ----
  Uint32 init_flags = SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_SENSOR;
  if (SDL_Init(init_flags) != 0) {
    spdlog::critical("SDL_Init failed with flags 0x{:x}: {}", init_flags,
                     SDL_GetError());

    // Try individual subsystems to see which one fails
    spdlog::info("Attempting to init each subsystem separately...");
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
      spdlog::critical("SDL_INIT_JOYSTICK failed: {}", SDL_GetError());
    } else {
      spdlog::info("SDL_INIT_JOYSTICK succeeded.");
    }
    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD) != 0) {
      spdlog::critical("SDL_INIT_GAMEPAD failed: {}", SDL_GetError());
    } else {
      spdlog::info("SDL_INIT_GAMEPAD succeeded.");
    }
    if (SDL_InitSubSystem(SDL_INIT_SENSOR) != 0) {
      spdlog::critical("SDL_INIT_SENSOR failed: {}", SDL_GetError());
    } else {
      spdlog::info("SDL_INIT_SENSOR succeeded.");
    }

    // If at least JOYSTICK and GAMEPAD succeed, we can continue, else exit.
    // We'll check if JOYSTICK and GAMEPAD are both initialised.
    Uint32 subsystems = SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
    if ((subsystems & (SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) ==
        (SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
      spdlog::warn(
          "Continuing without sensor support (gyro will be unavailable).");
      // Clear the sensor flag from the init flags so we don't try to use it
      // later. We'll handle this by not attempting to enable gyro if
      // SDL_INIT_SENSOR wasn't init. But we can just set a global flag if
      // needed.
    } else {
      spdlog::critical("Essential subsystems (JOYSTICK and GAMEPAD) failed to "
                       "initialise. Exiting.");
      exit(1);
    }
  }
  spdlog::info("SDL initialized");

  // Initialize Winsock for network sockets
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    spdlog::warn("WSAStartup failed");
  }
#endif

  int num_joysticks = 0;
  SDL_JoystickID *joy_ids = SDL_GetJoysticks(&num_joysticks);
  spdlog::info("SDL_GetNumJoysticks() = {}", num_joysticks);
  for (int i = 0; i < num_joysticks; ++i) {
    SDL_JoystickID id = joy_ids[i];
    SDL_GUID guid = SDL_GetJoystickGUIDForID(id);
    char guid_str[64];
    SDL_GUIDToString(guid, guid_str, sizeof(guid_str));
    spdlog::info("  [{}] name='{}' guid={} is_gamecontroller={}", i,
                 SDL_GetJoystickNameForID(id), guid_str,
                 SDL_IsGamepad(id) ? "true" : "false");
  }
  SDL_free(joy_ids);

  // ---- 5. Start the platform-specific global keyboard backend.
  // This is independent of the GLFW window focus and is required for
  // keyboard overlays while another application is focused.
  GlobalKeyboard::initialize();

  // ---- 5. Ensure gamecontrollerdb.txt is present ----
  ensure_gamecontrollerdb();

  // ---- 6. Now create the settings window (which initialises ImGui) ----
  createSettingsWindow();

  // Wire tray icon menu actions before loadTabs(), since loading a
  // saved "tray_enabled" setting can re-create the tray icon
  // immediately - callbacks should already be in place by then.
  TrayIcon::setOnQuit([]() { gQuit = true; });
  TrayIcon::setOnShowMainWindow([]() {
    GLFWwindow *sw = getSettingsWindow();
    glfwRestoreWindow(sw);
    glfwShowWindow(sw);
    glfwFocusWindow(sw);
  });
  TrayIcon::setOnLeftClick([]() {
    // Toggles: restore+focus if currently minimized, otherwise minimize.
    GLFWwindow *sw = getSettingsWindow();
    if (glfwGetWindowAttrib(sw, GLFW_ICONIFIED)) {
      glfwRestoreWindow(sw);
      glfwShowWindow(sw);
      glfwFocusWindow(sw);
    } else {
      glfwIconifyWindow(sw);
    }
  });
  TrayIcon::setOnToggleController([](unsigned id) {
    controller_window *w = getControllerWindow(id);
    if (!w)
      return;
    if (isControllerWindowMinimized(*w)) {
      restoreControllerWindow(*w);
    } else {
      minimizeControllerWindow(*w);
    }
  });

  loadTabs();
}

void Input() {
  glfwPollEvents();
  TrayIcon::update();

  settings_window_input(gQuit);
  controller_window_input();

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    settings_sdl_events(&event);
    controller_sdl_events(&event);
  }
}

void Draw() {
  drawSettingsWindow();
  drawControllerWindows();
}

void MainLoop() {
  while (!gQuit) {
    Uint64 frame_start = SDL_GetTicks();
    try {
      Input();
      Draw();
    } catch (const std::exception &e) {
      spdlog::critical("Unhandled exception in main loop: {}", e.what());
      gQuit = true;
    } catch (...) {
      spdlog::critical("Unknown exception in main loop.");
      gQuit = true;
    }

    // Frame pacing lives here, as a plain sleep, rather than relying on
    // glfwSwapInterval/vsync anywhere: on some GPU/driver combinations
    // (confirmed on NVIDIA), waiting for vsync can stall indefinitely
    // while a fullscreen or borderless game runs behind an unfocused
    // controller window, freezing this entire loop - including input
    // polling, since Input() and Draw() share it. See
    // drawControllerWindows() in controller_window.cpp, where vsync is
    // unconditionally disabled for controller windows. A sleep can't
    // stall the same way: worst case it just sleeps for 0ms and this
    // loop runs uncapped for that iteration.
    unsigned target_hz = getFrameCapHz();
    Uint64 target_ms = target_hz > 0 ? (1000u / target_hz) : 16;
    Uint64 elapsed_ms = SDL_GetTicks() - frame_start;
    if (elapsed_ms < target_ms) {
      SDL_Delay((Uint32)(target_ms - elapsed_ms));
    }
  }
}

void Cleanup() {
  saveTabs();
  TrayIcon::disable();
  removeSettingsWindow();
  destroyWindows();
  GlobalKeyboard::shutdown();
#ifdef _WIN32
  WSACleanup();
#endif
  SDL_Quit();
  glfwTerminate();
  spdlog::info("Shutdown complete.");
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmain"
#endif

extern "C" int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  InitializeProgram();
  MainLoop();
  Cleanup();
  return 0;
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
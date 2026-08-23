#include "keyboard_input.h"
#include "log_window.h"
#include "settings.h"
#include "settings_window.h"
#include <filesystem>
#include <iostream>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

bool gQuit = false;

void InitializeProgram() {
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

  // ---- 4. Init SDL ----
  if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR) <
      0) {
    spdlog::critical("SDL_Init failed: {}", SDL_GetError());
    exit(1);
  }
  spdlog::info("SDL initialized");

  spdlog::info("SDL_NumJoysticks() = {}", SDL_NumJoysticks());
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
    char guid_str[64];
    SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
    spdlog::info("  [{}] name='{}' guid={} is_gamecontroller={}", i,
                 SDL_JoystickNameForIndex(i), guid_str,
                 SDL_IsGameController(i) ? "true" : "false");
  }

  // ---- 5. Start the platform-specific global keyboard backend.
  // This is independent of the GLFW window focus and is required for
  // keyboard overlays while another application is focused.
  GlobalKeyboard::initialize();

  // ---- 5. Ensure gamecontrollerdb.txt is present ----
  ensure_gamecontrollerdb();

  // ---- 6. Now create the settings window (which initialises ImGui) ----
  createSettingsWindow();
  loadTabs();
}

void Input() {
  glfwPollEvents();

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
  }
}

void Cleanup() {
  saveTabs();
  removeSettingsWindow();
  destroyWindows();
  GlobalKeyboard::shutdown();
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
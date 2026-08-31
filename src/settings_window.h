#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

// --------------------------------------------------------------------------
// IMPORTANT: The following includes MUST be in this exact order.
// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <SDL3/SDL.h>

#include "imfilebrowser.h"
// clang-format on
// --------------------------------------------------------------------------

#include <sys/stat.h>

#include "controller_window.h"

// ----------------------------------------------------------------------------
// Forward declarations & typedefs
// ----------------------------------------------------------------------------
typedef struct controller_window_struct controller_window;

typedef struct my_tab {
  unsigned ID;
  std::string title;
} window_tab;

// ----------------------------------------------------------------------------
// Function declarations
// ----------------------------------------------------------------------------
void createSettingsWindow();
GLFWwindow *getSettingsWindow();
void close_window(unsigned ID);
void removeTab(unsigned tab);
void saveTabs();
void loadTabs();
void removeSettingsWindow();
void drawSettingsWindow();
void settings_window_input(bool &quit);
void settings_sdl_events(SDL_Event *event);
void settings_framebuffer_size_callback(GLFWwindow *window, int width,
                                        int height);
void glfw_error_callback(int error, const char *description);
void GetOpenGLVersionInfo();
void OsOpenInShell(const char *path);
const GLFWvidmode *get_vid_mode();
bool check_filename_valid(const char *name);
std::string get_top_folder(std::string path);
std::string get_first_model();

extern bool g_log_controller;
extern bool g_log_keyboard;
extern bool g_log_mouse;
extern bool g_debug_mode_enabled;
void setDebugModeEnabled(bool enabled);

#endif
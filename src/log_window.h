#ifndef LOG_WINDOW_H
#define LOG_WINDOW_H

// ------------------------------------------------------------------------
// In-app log window.
//
// Why this exists: on Windows the executable is built as a console-subsystem
// binary, so a console window is attached automatically and log output is
// visible "for free". On macOS and Linux, launching the app via a .app
// bundle / AppImage / desktop icon attaches no terminal, so the exact same
// spdlog output silently disappears unless the app happens to be started
// from a terminal. Rather than trying to allocate/attach a native console
// per-platform (fragile, and each OS behaves differently), this adds a
// small custom spdlog sink that mirrors every log line into an in-memory
// ring buffer, which is then rendered as a normal ImGui window. That makes
// the "Open Log Window" button behave identically on all three platforms.
//
// The log window is its own real OS window (own GLFW window + own ImGui
// context), not an ImGui child window living inside the settings window's
// context. That's deliberate: an ImGui window sharing a context with the
// settings window gets sent behind the settings window the moment the user
// clicks anywhere else in that context, because ImGui's own focus/z-order
// tracking is per-context. A separate OS window with GLFW_FLOATING set
// stays on top the way any other "always on top" window does, regardless
// of what's clicked in the settings window.
// ------------------------------------------------------------------------

#include <memory>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <deque>
#include <mutex>
#include <string>
#include <vector>

class ImGuiLogSink : public spdlog::sinks::base_sink<std::mutex> {
public:
  struct Entry {
    std::string text;
    spdlog::level::level_enum level;
  };

  explicit ImGuiLogSink(size_t max_lines = 2000) : max_lines_(max_lines) {}

  // Thread-safe copy of the current buffer, for rendering.
  std::vector<Entry> snapshot();
  void clear();

protected:
  void sink_it_(const spdlog::details::log_msg &msg) override;
  void flush_() override {}

private:
  size_t max_lines_;
  std::deque<Entry> lines_;
};

// Call once at startup, right after the default spdlog logger has been
// created (see InitializeProgram() in main.cpp). Safe to call more than
// once - subsequent calls are a no-op.
void initLogWindow(std::shared_ptr<spdlog::logger> logger = nullptr);

// Visibility control - wired up to the "Open Log Window" button in the
// settings window, next to "Open Data Directory". Creates the log window's
// own GLFW window + ImGui context lazily on first use.
void toggleLogWindow();
bool isLogWindowOpen();
void setLogWindowOpen(bool open);

// Call once per frame from the main loop (e.g. in Draw(), alongside
// drawSettingsWindow()/drawControllerWindows()). No-op while hidden.
// This function is self-contained: it does its own ImGui NewFrame/Render
// and glfwSwapBuffers on its own window, and restores whatever ImGui
// context/GL context was current on entry before returning, so it's safe
// to call from anywhere in the frame without disturbing the settings
// window's own rendering.
void drawLogWindow();

#endif

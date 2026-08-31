#include "log_window.h"

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include "icon_data.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "stb_image.h"

#include <spdlog/details/log_msg.h>

#include <cfloat>

namespace {
std::shared_ptr<ImGuiLogSink> g_sink;
bool g_log_window_open = false;
bool g_autoscroll = true;

GLFWwindow *g_log_glfw_window = nullptr;
ImGuiContext *g_log_imgui_ctx = nullptr;
bool g_log_backend_ready = false;
double g_copied_flash_until = 0.0; // glfwGetTime() deadline for "Copied!" toast
} // namespace

void ImGuiLogSink::sink_it_(const spdlog::details::log_msg &msg) {
  spdlog::memory_buf_t formatted;
  formatter_->format(msg, formatted);
  std::string text(formatted.data(), formatted.size());
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    text.pop_back();

  lines_.push_back({std::move(text), msg.level});
  while (lines_.size() > max_lines_)
    lines_.pop_front();
}

std::vector<ImGuiLogSink::Entry> ImGuiLogSink::snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::vector<Entry>(lines_.begin(), lines_.end());
}

void ImGuiLogSink::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  lines_.clear();
}

void initLogWindow(std::shared_ptr<spdlog::logger> logger) {
  if (g_sink)
    return; // already initialised

  g_sink = std::make_shared<ImGuiLogSink>();
  g_sink->set_level(spdlog::level::trace);

  auto target = logger ? logger : spdlog::default_logger();
  if (target) {
    target->sinks().push_back(g_sink);
  }
}

// ------------------------------------------------------------------------
// Lazily creates the log window's own GLFW window + ImGui context the
// first time it's needed. Kept alive for the rest of the process once
// created (just hidden/shown after that) - there's no benefit to tearing
// it down between opens, and doing so would mean re-creating fonts each
// time.
// ------------------------------------------------------------------------
static void ensureLogWindowCreated() {
  if (g_log_glfw_window)
    return;

  // GLFW_FLOATING is the actual "always on top" mechanism here - it's an
  // OS-level window manager hint, so it stays above the settings window
  // (and everything else) regardless of what the user clicks on,
  // unlike an ImGui window sharing the settings window's own context,
  // which is what previously caused it to fall behind on any click
  // outside of it.
  glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

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

  g_log_glfw_window =
      glfwCreateWindow(700, 400, "3D Controller Overlay - Log", NULL, NULL);
  if (!g_log_glfw_window) {
    spdlog::error("Failed to create log window.");
    return;
  }

  GLFWimage images[1];
  images[0].pixels = stbi_load_from_memory(
      Embedded::icon_data, static_cast<int>(Embedded::icon_size),
      &images[0].width, &images[0].height, nullptr, 4);
  if (images[0].pixels) {
    glfwSetWindowIcon(g_log_glfw_window, 1, images);
    stbi_image_free(images[0].pixels);
  }

  GLFWwindow *previous_context = glfwGetCurrentContext();
  glfwMakeContextCurrent(g_log_glfw_window);
  glfwSwapInterval(0); // don't force vsync stalls on top of the main window's

  ImGuiContext *previous_imgui_ctx = ImGui::GetCurrentContext();
  g_log_imgui_ctx = ImGui::CreateContext();
  ImGui::SetCurrentContext(g_log_imgui_ctx);
  ImGui::GetIO().IniFilename = nullptr; // don't persist a second imgui.ini
  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(g_log_glfw_window, true);
  g_log_backend_ready = ImGui_ImplOpenGL3_Init(glsl_version);
  if (!g_log_backend_ready) {
    spdlog::error("Failed to initialize ImGui OpenGL3 backend for the log "
                  "window.");
  }

  // Restore whatever was current before this call so we never leave the
  // rest of the frame's rendering pointed at the log window's context.
  if (previous_imgui_ctx)
    ImGui::SetCurrentContext(previous_imgui_ctx);
  if (previous_context)
    glfwMakeContextCurrent(previous_context);
}

void toggleLogWindow() { setLogWindowOpen(!g_log_window_open); }
bool isLogWindowOpen() { return g_log_window_open; }

void setLogWindowOpen(bool open) {
  g_log_window_open = open;
  if (open) {
    ensureLogWindowCreated();
    if (g_log_glfw_window) {
      glfwShowWindow(g_log_glfw_window);
      glfwFocusWindow(g_log_glfw_window);
    }
  } else if (g_log_glfw_window) {
    glfwHideWindow(g_log_glfw_window);
  }
}

static ImVec4 colorForLevel(spdlog::level::level_enum lvl) {
  switch (lvl) {
  case spdlog::level::trace:
    return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
  case spdlog::level::debug:
    return ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
  case spdlog::level::info:
    return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
  case spdlog::level::warn:
    return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
  case spdlog::level::err:
    return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
  case spdlog::level::critical:
    return ImVec4(1.0f, 0.15f, 0.15f, 1.0f);
  default:
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  }
}

void drawLogWindow() {
  if (!g_log_window_open || !g_log_glfw_window || !g_log_backend_ready)
    return;

  // The user closing the window via its native close button/Alt-F4/etc
  // shows up here as glfwWindowShouldClose() - treat that the same as
  // unchecking "Open Log Window" rather than actually destroying the
  // window (see ensureLogWindowCreated()'s comment on why it's kept
  // around).
  if (glfwWindowShouldClose(g_log_glfw_window)) {
    glfwSetWindowShouldClose(g_log_glfw_window, GLFW_FALSE);
    setLogWindowOpen(false);
    return;
  }

  // This function renders and swaps its own window/context completely
  // independently of the settings window's frame, so it must save and
  // restore both the GL context and the ImGui context around itself -
  // otherwise every ImGui/GL call made later in the same frame (or on
  // the next call to drawSettingsWindow()) would silently operate on
  // the log window's context instead.
  GLFWwindow *previous_context = glfwGetCurrentContext();
  ImGuiContext *previous_imgui_ctx = ImGui::GetCurrentContext();

  glfwMakeContextCurrent(g_log_glfw_window);
  ImGui::SetCurrentContext(g_log_imgui_ctx);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  int win_w, win_h;
  glfwGetWindowSize(g_log_glfw_window, &win_w, &win_h);
  ImGui::SetNextWindowSize(ImVec2((float)win_w, (float)win_h));
  ImGui::Begin("LogRoot", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

  auto entries = g_sink->snapshot();

  if (ImGui::Button("Clear")) {
    g_sink->clear();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy All")) {
    std::string all;
    all.reserve(entries.size() * 64);
    for (auto &e : entries) {
      all += e.text;
      all += '\n';
    }
    ImGui::SetClipboardText(all.c_str());
    g_copied_flash_until = glfwGetTime() + 1.5;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Autoscroll", &g_autoscroll);
  if (glfwGetTime() < g_copied_flash_until) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Copied to clipboard!");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(also written to logs/3dco+.log in the data directory)");

  ImGui::Separator();

  ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

  // Individual lines are rendered as read-only, borderless, single-line
  // InputText widgets rather than plain TextUnformatted. TextUnformatted
  // has no text-selection support at all in ImGui, which was the actual
  // reason log lines couldn't be copied before - InputText (even
  // read-only) gets normal click-drag selection, double-click word
  // select, and Ctrl+C for free, while still letting us colorize each
  // line for its log level.
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(entries.size()));
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const auto &e = entries[static_cast<size_t>(i)];
      ImGui::PushID(i);
      ImGui::PushStyleColor(ImGuiCol_Text, colorForLevel(e.level));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      ImGui::SetNextItemWidth(-FLT_MIN);
      // const_cast is safe here: ImGuiInputTextFlags_ReadOnly guarantees
      // ImGui never writes back through this pointer.
      ImGui::InputText("##line", const_cast<char *>(e.text.c_str()),
                       e.text.size() + 1,
                       ImGuiInputTextFlags_ReadOnly);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(2);
      ImGui::PopID();
    }
  }

  if (g_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f)
    ImGui::SetScrollHereY(1.0f);

  ImGui::EndChild();
  ImGui::End();

  ImGui::Render();
  glViewport(0, 0, win_w, win_h);
  glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(g_log_glfw_window);

  if (previous_imgui_ctx)
    ImGui::SetCurrentContext(previous_imgui_ctx);
  if (previous_context)
    glfwMakeContextCurrent(previous_context);
}

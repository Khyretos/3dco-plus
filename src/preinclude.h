#ifndef PREINCLUDE_H
#define PREINCLUDE_H
#include <string> // IWYU pragma: keep

// IMGUI_NEEDS_GLAD_LOADER is set (CMakeLists.txt, via
// set_source_files_properties) only for third_party/imgui/backends/
// imgui_impl_opengl3.cpp, not for any other file - it has nothing to
// do with any other translation unit, including test-only files like
// shortcut_logic.cpp/model_persistence.cpp that never touch GL at
// all (an earlier version of this fix force-included glad.h for
// every single file unconditionally, which broke exactly those).
//
// Dear ImGui's OpenGL3 backend normally bundles its own, separate GL
// function loader ("imgl3w") instead of using GLAD like the rest of
// this app. imgl3w only ever loads its function pointers once,
// globally, for the whole process (lazy init: skips re-running if
// its own glGetIntegerv is already non-null) - never re-validated
// against whichever of this app's several separate, non-shared GL
// contexts is actually current when a given window's ImGui draw call
// runs. Worse, ImGui_ImplOpenGL3_Shutdown() (called whenever a
// window's own ImGui backend tears down, e.g. on close) resets that
// same global state, so the next window to render anywhere silently
// rebinds every pointer to ITS context instead, invalidating them
// for every other already-open window. Confirmed, reproduced root
// cause of a hard, deterministic Windows crash (null function
// pointer call) inside ImGui_ImplOpenGL3_RenderDrawData(), following
// a controller window close.
//
// IMGUI_IMPL_OPENGL_LOADER_CUSTOM (documented in
// imgui_impl_opengl3.cpp's own comments as the supported way to use
// an external loader) makes it skip imgl3w entirely and rely on
// whatever GL declarations are already visible - the glad.h include
// below provides those, so ImGui's GL calls go through the exact
// same, already-context-synced GLAD pointers as the rest of the app
// (see makeContextCurrentSafe(), controller_window.h).
#ifdef IMGUI_NEEDS_GLAD_LOADER
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <glad/glad.h>
#endif

#endif

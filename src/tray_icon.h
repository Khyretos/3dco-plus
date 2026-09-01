#ifndef TRAY_ICON_H
#define TRAY_ICON_H

#include <string>
#include <vector>

// ------------------------------------------------------------------
// System tray / taskbar integration.
//
// Cross-platform reality check up front, since this genuinely differs a
// lot by platform:
//
// - Windows: fully implemented (Shell_NotifyIcon + a native popup menu).
// - macOS: fully implemented (NSStatusItem via Cocoa/AppKit). Compiled
//   as Objective-C++ on this one platform only - see the `-x
//   objective-c++` compile flag applied to tray_icon.cpp for APPLE in
//   CMakeLists.txt.
// - Linux: there is no single answer. Traditional XEmbed tray icons
//   (what GLFW/Qt/GTK apps have used for 20 years) are not supported at
//   all on Wayland compositors, and increasingly not on X11 desktops
//   either (GNOME dropped its own tray years ago). The modern
//   replacement is the StatusNotifierItem D-Bus protocol, which most
//   major desktop environments (KDE, most others via
//   snixembed/appindicator support) do implement, but GNOME's stock
//   Shell still doesn't without a user-installed extension (e.g.
//   "AppIndicator and KStatusNotifierItem Support"). Implemented here
//   via that protocol when libdbus-1 is available at build time (see
//   CMakeLists.txt) - isSupported() reflects that at runtime.
//   Note: some SNI hosts (a few GNOME extensions included) always pop
//   up the assigned menu on left-click as well as right-click rather
//   than calling Activate - that's host behavior this app has no
//   control over, unlike the identical-sounding case on macOS (which
//   this app fully controls and does distinguish; see tray_icon.cpp).
//
// The API is intentionally already fully cross-platform-shaped (same
// calls compile and link everywhere) so that main.cpp and
// settings_window.cpp never need to special-case platforms themselves -
// only this module does. isSupported() lets the settings UI hide the
// "Enable Taskbar Icon" toggle entirely on platforms where it wouldn't
// do anything, the same way other platform-specific controls were
// handled elsewhere in this app.
// ------------------------------------------------------------------
namespace TrayIcon {

// True only on platforms with a real implementation (Windows, macOS,
// and Linux when built with libdbus-1). The settings UI should hide
// its tray-icon toggle entirely when this is false, rather than
// showing a control that can't do anything.
bool isSupported();

// Creates the tray icon (if isSupported() and not already created).
// Safe to call unconditionally; a no-op where isSupported() is false.
bool enable();
// Removes the tray icon. Safe to call even if never enabled.
void disable();
bool isEnabled();

// Must be called once per frame, from the same thread that calls
// glfwPollEvents() (see Input() in main.cpp). On Windows the tray
// icon's hidden window is created on that same thread, and GLFW's own
// PeekMessage-based pump already dispatches messages for every window
// on the calling thread, not just ones GLFW itself created - so this
// function doesn't need to pump messages itself. What it does need to
// do is refresh the cached menu data (controller list, network status)
// so the next right-click shows current information rather than
// whatever was true the last time this was called.
void update();

// ---- Data shown in the right-click menu's submenus ----
struct ControllerEntry {
  unsigned id;
  std::string title;
  bool minimized;
};
// Call every frame (or whenever the open-window list changes) with the
// current set of controller windows, for the "Controllers" submenu.
void setControllerList(const std::vector<ControllerEntry> &entries);

enum class NetworkStatus { Disabled, Connecting, Connected };
struct ConnectionEntry {
  // Human-readable description of one active connection, e.g.
  // "Sending to 192.168.1.42:7777" or "Receiving from Xbox-Controller-1".
  std::string label;
};
// Call whenever network status changes, for the "Network" submenu.
// Passing NetworkStatus::Disabled or Connecting with a non-empty
// connections list is a caller bug - connections should be empty
// unless status is Connected.
void setNetworkStatus(NetworkStatus status,
                      const std::vector<ConnectionEntry> &connections);

// ---- Menu action callbacks ----
// All of these fire from update() (or, on Windows, from the tray
// window's own WndProc, which - being on the main thread, per the
// comment on update() above - is always called from the same thread
// that runs the rest of the app). Safe to touch GLFW/ImGui state
// directly from any of them; nothing here runs on a background thread.
using VoidCallback = void (*)();
using ControllerCallback = void (*)(unsigned id);

void setOnLeftClick(VoidCallback cb);              // toggle main window
void setOnShowMainWindow(VoidCallback cb);         // "Show Window" menu item
void setOnQuit(VoidCallback cb);                   // "Quit" menu item
void setOnToggleController(ControllerCallback cb); // per-controller item

} // namespace TrayIcon

#endif // TRAY_ICON_H
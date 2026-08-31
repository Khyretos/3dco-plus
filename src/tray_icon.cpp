#include "tray_icon.h"

#include "icon_data.h"
#include "stb_image.h"

#include <spdlog/spdlog.h>

#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
// clang-format off
// IMPORTANT: windows.h MUST come before shellapi.h - shellapi.h uses
// basic Windows types (UINT, DWORD, HWND, WINBOOL, etc.) that are only
// defined once windows.h has been processed. The clang-format off/on
// markers below stop editor auto-formatters (e.g. VS Code's C/C++
// extension, which sorts includes alphabetically by default - and
// "shellapi.h" < "windows.h" alphabetically) from silently reordering
// these two lines back to a broken order on every save.
#include <windows.h>
#include <shellapi.h>
// clang-format on
#elif defined(__linux__) && defined(HAVE_DBUS)
#include <dbus/dbus.h>
#include <functional>
#include <unistd.h> // getpid()
#endif

namespace TrayIcon {

#if defined(_WIN32)

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kIdShowWindow = 1;
constexpr UINT kIdQuit = 2;
// Controller menu items use IDs starting here, one per open window -
// see setControllerList()'s definition below for the mapping back to
// an actual controller_window ID.
constexpr UINT kControllerIdBase = 1000;
constexpr UINT kMaxControllers = 900; // generous headroom, arbitrary cap

const wchar_t *kTrayClassName = L"3dcoPlusTrayIcon";

HWND g_hwnd = nullptr;
bool g_enabled = false;
NOTIFYICONDATAW g_nid{};

std::vector<ControllerEntry> g_controllers;
NetworkStatus g_networkStatus = NetworkStatus::Disabled;
std::vector<ConnectionEntry> g_connections;

VoidCallback g_onLeftClick = nullptr;
VoidCallback g_onShowMainWindow = nullptr;
VoidCallback g_onQuit = nullptr;
ControllerCallback g_onToggleController = nullptr;

std::wstring toWide(const std::string &s) {
  if (s.empty())
    return std::wstring();
  int size =
      MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  if (size <= 0)
    return std::wstring();
  std::wstring result((size_t)size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), result.data(),
                      size);
  return result;
}

// ------------------------------------------------------------------
// Builds a real HICON from the app's own embedded icon (icon_data.h),
// for use in the tray/taskbar notification area. Previously this used
// IDI_APPLICATION (a generic system icon) as a placeholder.
//
// The embedded PNG is decoded once via stb_image, then downsampled with
// simple nearest-neighbor sampling to the requested size and repacked
// into a 32-bit BGRA DIB section, since Windows icon bitmaps are
// natively BGRA rather than RGBA and the notification area expects a
// small icon (16-32px), not the embedded asset's native resolution.
// Nearest-neighbor is plenty for an icon this small and avoids pulling
// in an image-resizing library just for this one call site.
// ------------------------------------------------------------------
HICON createAppHIcon(int size) {
  int w = 0, h = 0;
  unsigned char *pixels = stbi_load_from_memory(
      Embedded::icon_data, static_cast<int>(Embedded::icon_size), &w, &h,
      nullptr, 4);
  if (!pixels) {
    spdlog::warn("TrayIcon: failed to decode embedded app icon.");
    return nullptr;
  }

  std::vector<unsigned char> bgra(static_cast<size_t>(size) * size * 4);
  for (int y = 0; y < size; ++y) {
    int sy = (h > 0) ? (y * h / size) : 0;
    for (int x = 0; x < size; ++x) {
      int sx = (w > 0) ? (x * w / size) : 0;
      const unsigned char *src =
          pixels + (static_cast<size_t>(sy) * w + sx) * 4;
      unsigned char *dst =
          bgra.data() + (static_cast<size_t>(y) * size + x) * 4;
      dst[0] = src[2]; // B
      dst[1] = src[1]; // G
      dst[2] = src[0]; // R
      dst[3] = src[3]; // A
    }
  }
  stbi_image_free(pixels);

  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(BITMAPV5HEADER);
  bi.bV5Width = size;
  bi.bV5Height = -size; // negative = top-down DIB
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_RGB;

  HDC screenDC = GetDC(nullptr);
  void *bits = nullptr;
  HBITMAP hbmColor = CreateDIBSection(screenDC, (BITMAPINFO *)&bi,
                                      DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screenDC);
  if (!hbmColor || !bits) {
    spdlog::warn("TrayIcon: CreateDIBSection failed for app icon.");
    return nullptr;
  }
  memcpy(bits, bgra.data(), bgra.size());

  HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, nullptr);
  if (!hbmMask) {
    DeleteObject(hbmColor);
    spdlog::warn("TrayIcon: CreateBitmap (mask) failed for app icon.");
    return nullptr;
  }

  ICONINFO ii{};
  ii.fIcon = TRUE;
  ii.hbmMask = hbmMask;
  ii.hbmColor = hbmColor;
  HICON icon = CreateIconIndirect(&ii);

  // CreateIconIndirect makes its own internal copies - these are safe to
  // free regardless of whether it succeeded.
  DeleteObject(hbmColor);
  DeleteObject(hbmMask);

  if (!icon)
    spdlog::warn("TrayIcon: CreateIconIndirect failed for app icon.");
  return icon;
}

HICON g_app_hicon = nullptr; // GetSystemMetrics(SM_CXSMICON)-sized, cached

void showContextMenu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;

  AppendMenuW(menu, MF_STRING, kIdShowWindow, L"Show Window");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // ---- Controllers submenu ----
  HMENU controllersMenu = CreatePopupMenu();
  if (g_controllers.empty()) {
    AppendMenuW(controllersMenu, MF_STRING | MF_GRAYED, 0, L"(none open)");
  } else {
    UINT count = (UINT)std::min(g_controllers.size(), (size_t)kMaxControllers);
    for (UINT i = 0; i < count; ++i) {
      const ControllerEntry &c = g_controllers[i];
      std::wstring label =
          toWide(c.title) + (c.minimized ? L"  (Restore)" : L"  (Minimize)");
      AppendMenuW(controllersMenu, MF_STRING, kControllerIdBase + i,
                  label.c_str());
    }
  }
  AppendMenuW(menu, MF_POPUP, (UINT_PTR)controllersMenu, L"Controllers");

  // ---- Network submenu ----
  // Status line and connection entries are informational only
  // (MF_GRAYED, not clickable) - this module only displays whatever
  // the app tells it via setNetworkStatus(); it has no networking logic
  // of its own.
  HMENU networkMenu = CreatePopupMenu();
  const wchar_t *statusText = L"Disabled";
  if (g_networkStatus == NetworkStatus::Connecting)
    statusText = L"Connecting...";
  else if (g_networkStatus == NetworkStatus::Connected)
    statusText = L"Connected";
  std::wstring statusLine = std::wstring(L"Status: ") + statusText;
  AppendMenuW(networkMenu, MF_STRING | MF_GRAYED, 0, statusLine.c_str());
  if (!g_connections.empty()) {
    AppendMenuW(networkMenu, MF_SEPARATOR, 0, nullptr);
    for (const ConnectionEntry &c : g_connections) {
      AppendMenuW(networkMenu, MF_STRING | MF_GRAYED, 0,
                  toWide(c.label).c_str());
    }
  }
  AppendMenuW(menu, MF_POPUP, (UINT_PTR)networkMenu, L"Network");

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kIdQuit, L"Quit");

  POINT pt;
  GetCursorPos(&pt);
  // Required for the popup to behave correctly (dismiss on click-away) -
  // a standard, well-documented Win32 quirk for tray context menus.
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x,
                 pt.y, 0, hwnd, nullptr);
  // Also standard: without this follow-up message, the menu can fail to
  // close properly on some Windows versions.
  PostMessageW(hwnd, WM_NULL, 0, 0);

  // Destroying the parent menu also destroys its attached submenus -
  // controllersMenu and networkMenu don't need separate DestroyMenu
  // calls.
  DestroyMenu(menu);
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                             LPARAM lParam) {
  if (msg == kTrayCallbackMessage) {
    switch (LOWORD(lParam)) {
    case WM_LBUTTONUP:
      if (g_onLeftClick)
        g_onLeftClick();
      break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
      showContextMenu(hwnd);
      break;
    default:
      break;
    }
    return 0;
  }
  if (msg == WM_COMMAND) {
    UINT id = LOWORD(wParam);
    if (id == kIdShowWindow) {
      if (g_onShowMainWindow)
        g_onShowMainWindow();
    } else if (id == kIdQuit) {
      if (g_onQuit)
        g_onQuit();
    } else if (id >= kControllerIdBase &&
               id < kControllerIdBase + kMaxControllers) {
      UINT idx = id - kControllerIdBase;
      if (idx < g_controllers.size() && g_onToggleController)
        g_onToggleController(g_controllers[idx].id);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool createTrayWindow() {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = TrayWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kTrayClassName;
  RegisterClassW(&wc);

  // HWND_MESSAGE: never visible, never in the taskbar/Alt-Tab - exists
  // purely to own the tray icon and receive its callback message and
  // the popup menu's WM_COMMAND messages. Created on whichever thread
  // calls enable() - see update()'s comment in tray_icon.h for why that
  // needs to be the same thread that calls glfwPollEvents().
  g_hwnd = CreateWindowExW(0, kTrayClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                           nullptr, GetModuleHandleW(nullptr), nullptr);
  return g_hwnd != nullptr;
}

} // namespace

bool isSupported() { return true; }

bool enable() {
  if (g_enabled)
    return true;
  if (!g_hwnd && !createTrayWindow()) {
    spdlog::warn("TrayIcon: failed to create tray window (GetLastError={})",
                 GetLastError());
    return false;
  }

  g_nid = {};
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = g_hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = kTrayCallbackMessage;
  // Real app icon (see createAppHIcon() above), sized to whatever the
  // system's small-icon metric is so it isn't blurry in the tray.
  if (!g_app_hicon)
    g_app_hicon = createAppHIcon(GetSystemMetrics(SM_CXSMICON));
  g_nid.hIcon =
      g_app_hicon ? g_app_hicon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
  const std::wstring tip = L"3D Controller Overlay";
  size_t n = std::min(tip.size(), (sizeof(g_nid.szTip) / sizeof(wchar_t)) - 1);
  wmemcpy(g_nid.szTip, tip.c_str(), n);
  g_nid.szTip[n] = L'\0';

  if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
    spdlog::warn("TrayIcon: Shell_NotifyIconW(NIM_ADD) failed");
    return false;
  }
  g_enabled = true;
  spdlog::info("Tray icon enabled");
  return true;
}

void disable() {
  if (!g_enabled)
    return;
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  g_enabled = false;
  spdlog::info("Tray icon disabled");
}

bool isEnabled() { return g_enabled; }

void update() {
  // Nothing to pump here - GLFW's own glfwPollEvents() already
  // dispatches messages for every window on the calling thread, not
  // just ones it created itself, as long as this module's window was
  // created on that same thread (see createTrayWindow()'s comment).
  // Reserved for any future per-frame bookkeeping.
}

void setControllerList(const std::vector<ControllerEntry> &entries) {
  g_controllers = entries;
}

void setNetworkStatus(NetworkStatus status,
                      const std::vector<ConnectionEntry> &connections) {
  g_networkStatus = status;
  g_connections = connections;
}

void setOnLeftClick(VoidCallback cb) { g_onLeftClick = cb; }
void setOnShowMainWindow(VoidCallback cb) { g_onShowMainWindow = cb; }
void setOnQuit(VoidCallback cb) { g_onQuit = cb; }
void setOnToggleController(ControllerCallback cb) { g_onToggleController = cb; }

#elif defined(__linux__) && defined(HAVE_DBUS)

// ------------------------------------------------------------------
// Linux: org.freedesktop.StatusNotifierItem + com.canonical.dbusmenu
// over D-Bus, via libdbus-1 (the minimal, universally-available D-Bus
// binding - no GLib/Qt dependency). This is what modern Linux desktop
// trays actually use; see the comment at the top of tray_icon.h for
// the GNOME caveat (needs a user-installed extension there; works out
// of the box on KDE and most others).
//
// Two object paths are exposed on our own bus name:
//   /StatusNotifierItem - the tray icon itself (org.freedesktop.
//     StatusNotifierItem interface): properties, Activate (left
//     click).
//   /MenuBar - the right-click context menu (com.canonical.dbusmenu
//     interface): GetLayout (host asks for the menu tree),
//     Event (host reports a click).
//
// This is genuinely fiddly D-Bus protocol work - nested variant/
// struct marshaling via libdbus-1's low-level DBusMessageIter API -
// and I have no way to test it without a real Linux desktop running
// a compatible tray host. Please verify each piece incrementally
// (icon appears -> tooltip -> left-click -> right-click menu opens ->
// each menu item actually does the right thing) rather than assuming
// it all works end to end on the first try.
// ------------------------------------------------------------------
namespace {

DBusConnection *g_conn = nullptr;
bool g_enabled = false;
std::string g_bus_name;

std::vector<ControllerEntry> g_controllers;
NetworkStatus g_networkStatus = NetworkStatus::Disabled;
std::vector<ConnectionEntry> g_connections;
dbus_uint32_t g_menu_revision = 1;

VoidCallback g_onLeftClick = nullptr;
VoidCallback g_onShowMainWindow = nullptr;
VoidCallback g_onQuit = nullptr;
ControllerCallback g_onToggleController = nullptr;

constexpr const char *kItemPath = "/StatusNotifierItem";
constexpr const char *kItemIface = "org.freedesktop.StatusNotifierItem";
constexpr const char *kMenuPath = "/MenuBar";
constexpr const char *kMenuIface = "com.canonical.dbusmenu";
constexpr const char *kPropsIface = "org.freedesktop.DBus.Properties";

// Fixed menu item IDs (id 0 is always the implicit root per the
// dbusmenu spec). Controller/connection entries use a small
// contiguous range each - fine for a reasonably-scoped app; if either
// list ever needs more than 100 entries, widen the ranges below.
constexpr int kIdShowWindow = 1;
constexpr int kIdControllersRoot = 3;
constexpr int kIdControllerBase = 100; // 100..199
constexpr int kIdNetworkRoot = 4;
constexpr int kIdNetworkStatusLine = 200;
constexpr int kIdNetworkSeparator = 250; // between status line and connections
constexpr int kIdConnectionBase = 201;   // 201..299
constexpr int kIdQuit = 6;

// ---- Simple in-memory menu tree, rebuilt fresh on every GetLayout
// or LayoutUpdated - our whole menu is cheap enough to reconstruct
// each time rather than maintaining it incrementally. ----
struct MenuNode {
  int id = 0;
  std::string label;
  bool enabled = true;
  bool is_separator = false;
  std::vector<MenuNode> children;
};

MenuNode buildMenuTree() {
  MenuNode root;
  root.id = 0;

  MenuNode show_window;
  show_window.id = kIdShowWindow;
  show_window.label = "Show Window";
  root.children.push_back(show_window);

  MenuNode sep1;
  sep1.id =
      2; // reserved gap between kIdShowWindow(1) and kIdControllersRoot(3)
  sep1.is_separator = true;
  root.children.push_back(sep1);

  MenuNode controllers;
  controllers.id = kIdControllersRoot;
  controllers.label = "Controllers";
  if (g_controllers.empty()) {
    MenuNode none;
    none.id = kIdControllerBase; // unused as a real target, just a label
    none.label = "(none open)";
    none.enabled = false;
    controllers.children.push_back(none);
  } else {
    int count = std::min((int)g_controllers.size(), 99);
    for (int i = 0; i < count; ++i) {
      MenuNode entry;
      entry.id = kIdControllerBase + i;
      entry.label =
          g_controllers[i].title +
          (g_controllers[i].minimized ? "  (Restore)" : "  (Minimize)");
      controllers.children.push_back(entry);
    }
  }
  root.children.push_back(controllers);

  MenuNode network;
  network.id = kIdNetworkRoot;
  network.label = "Network";
  MenuNode status_line;
  status_line.id = kIdNetworkStatusLine;
  status_line.enabled = false;
  switch (g_networkStatus) {
  case NetworkStatus::Connecting:
    status_line.label = "Status: Connecting...";
    break;
  case NetworkStatus::Connected:
    status_line.label = "Status: Connected";
    break;
  default:
    status_line.label = "Status: Disabled";
    break;
  }
  network.children.push_back(status_line);
  if (!g_connections.empty()) {
    MenuNode sep_net;
    sep_net.id = kIdNetworkSeparator;
    sep_net.is_separator = true;
    network.children.push_back(sep_net);
    int count = std::min((int)g_connections.size(), 99);
    for (int i = 0; i < count; ++i) {
      MenuNode conn;
      conn.id = kIdConnectionBase + i;
      conn.label = g_connections[i].label;
      conn.enabled = false;
      network.children.push_back(conn);
    }
  }
  root.children.push_back(network);

  MenuNode sep2;
  sep2.id = 5; // reserved gap between kIdNetworkRoot(4) and kIdQuit(6)
  sep2.is_separator = true;
  root.children.push_back(sep2);

  MenuNode quit;
  quit.id = kIdQuit;
  quit.label = "Quit";
  root.children.push_back(quit);

  return root;
}

const MenuNode *findNodeById(const MenuNode &node, int id) {
  if (node.id == id)
    return &node;
  for (const MenuNode &child : node.children) {
    const MenuNode *found = findNodeById(child, id);
    if (found)
      return found;
  }
  return nullptr;
}

// Real app icon (see icon_data.h) as ARGB32 bytes, per the
// StatusNotifierItem spec's IconPixmap format: each pixel is a
// big-endian 32-bit value structured as A<<24 | R<<16 | G<<8 | B, i.e.
// stored byte-for-byte as A, R, G, B. Nearest-neighbor downsampled from
// the embedded PNG's native resolution to `size`, same approach as the
// Windows HICON builder above - plenty for an icon this small, no extra
// resize dependency needed.
std::vector<unsigned char> buildIconArgb32(int size) {
  std::vector<unsigned char> out;
  int w = 0, h = 0;
  unsigned char *pixels = stbi_load_from_memory(
      Embedded::icon_data, static_cast<int>(Embedded::icon_size), &w, &h,
      nullptr, 4);
  if (!pixels) {
    spdlog::warn("TrayIcon: failed to decode embedded app icon.");
    return out;
  }

  out.resize(static_cast<size_t>(size) * size * 4);
  for (int y = 0; y < size; ++y) {
    int sy = (h > 0) ? (y * h / size) : 0;
    for (int x = 0; x < size; ++x) {
      int sx = (w > 0) ? (x * w / size) : 0;
      const unsigned char *src =
          pixels + (static_cast<size_t>(sy) * w + sx) * 4;
      unsigned char *dst = out.data() + (static_cast<size_t>(y) * size + x) * 4;
      dst[0] = src[3]; // A
      dst[1] = src[0]; // R
      dst[2] = src[1]; // G
      dst[3] = src[2]; // B
    }
  }
  stbi_image_free(pixels);
  return out;
}

void appendStrPropTo(DBusMessageIter *dict_iter, const char *name,
                     const std::string &value) {
  DBusMessageIter entry, var;
  dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
  const char *cstr = value.c_str();
  dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &cstr);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(dict_iter, &entry);
}

void appendBoolPropTo(DBusMessageIter *dict_iter, const char *name,
                      bool value) {
  DBusMessageIter entry, var;
  dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
  dbus_bool_t v = value ? TRUE : FALSE;
  dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &v);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(dict_iter, &entry);
}

void appendUint32PropTo(DBusMessageIter *dict_iter, const char *name,
                        dbus_uint32_t value) {
  DBusMessageIter entry, var;
  dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &var);
  dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &value);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(dict_iter, &entry);
}

void appendObjectPathPropTo(DBusMessageIter *dict_iter, const char *name,
                            const std::string &path) {
  DBusMessageIter entry, var;
  dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &var);
  const char *cstr = path.c_str();
  dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &cstr);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(dict_iter, &entry);
}

// ToolTip's D-Bus type is the struct "(sa(iiay)ss)": icon name, an
// array of raw icon pixmaps (left empty here - IconName covers our
// case), title, and a longer description.
void appendToolTipProp(DBusMessageIter *dict_iter, const std::string &title) {
  DBusMessageIter entry, var, str;
  dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  const char *name = "ToolTip";
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "(sa(iiay)ss)",
                                   &var);
  dbus_message_iter_open_container(&var, DBUS_TYPE_STRUCT, nullptr, &str);
  const char *icon_name = "";
  dbus_message_iter_append_basic(&str, DBUS_TYPE_STRING, &icon_name);
  DBusMessageIter pixmap_array;
  dbus_message_iter_open_container(&str, DBUS_TYPE_ARRAY, "(iiay)",
                                   &pixmap_array);
  dbus_message_iter_close_container(&str, &pixmap_array); // left empty
  const char *title_c = title.c_str();
  dbus_message_iter_append_basic(&str, DBUS_TYPE_STRING, &title_c);
  const char *desc = "";
  dbus_message_iter_append_basic(&str, DBUS_TYPE_STRING, &desc);
  dbus_message_iter_close_container(&var, &str);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(dict_iter, &entry);
}

// IconPixmap's D-Bus type is "a(iiay)": an array of (width, height, raw
// ARGB32 bytes) structs. Real-world hosts generally only look at the
// first entry, but the type is an array regardless.
void appendIconPixmapProp(DBusMessageIter *dict_iter, const char *name,
                          int size, const std::vector<unsigned char> &argb) {
  DBusMessageIter entry, var, arr;
  dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(iiay)", &var);
  dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "(iiay)", &arr);
  if (!argb.empty()) {
    DBusMessageIter strct, bytes;
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr, &strct);
    dbus_int32_t w = size, h = size;
    dbus_message_iter_append_basic(&strct, DBUS_TYPE_INT32, &w);
    dbus_message_iter_append_basic(&strct, DBUS_TYPE_INT32, &h);
    dbus_message_iter_open_container(&strct, DBUS_TYPE_ARRAY, "y", &bytes);

    const unsigned char *pixels_ptr = argb.data();
    dbus_message_iter_append_fixed_array(&bytes, DBUS_TYPE_BYTE, &pixels_ptr,
                                         static_cast<int>(argb.size()));

    dbus_message_iter_close_container(&strct, &bytes);
    dbus_message_iter_close_container(&arr, &strct);
  }
  dbus_message_iter_close_container(&var, &arr);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(dict_iter, &entry);
}

void appendStatusNotifierItemProperties(DBusMessageIter *array_iter) {
  appendStrPropTo(array_iter, "Category", "ApplicationStatus");
  appendStrPropTo(array_iter, "Id", "3dcoplus");
  appendStrPropTo(array_iter, "Title", "3D Controller Overlay");
  appendStrPropTo(array_iter, "Status", "Active");
  appendUint32PropTo(array_iter, "WindowId", 0);
  // Real app icon as raw pixels - most StatusNotifierItem hosts (KDE
  // Plasma, most others via snixembed/appindicator) prefer IconPixmap
  // over IconName when both are present. IconName is kept as a themed
  // fallback for hosts that only support named icons or that fail to
  // parse IconPixmap for some reason, rather than left blank.
  static const std::vector<unsigned char> icon_argb = buildIconArgb32(48);
  appendIconPixmapProp(array_iter, "IconPixmap", 48, icon_argb);
  appendStrPropTo(array_iter, "IconName",
                  icon_argb.empty() ? "input-gaming-symbolic" : "");
  appendStrPropTo(array_iter, "OverlayIconName", "");
  appendStrPropTo(array_iter, "AttentionIconName", "");
  appendStrPropTo(array_iter, "IconThemePath", "");
  appendToolTipProp(array_iter, "3D Controller Overlay");
  appendObjectPathPropTo(array_iter, "Menu", kMenuPath);
  appendBoolPropTo(array_iter, "ItemIsMenu", false);
}

void appendDbusMenuProperties(DBusMessageIter *array_iter) {
  appendUint32PropTo(array_iter, "Version", 3);
  appendStrPropTo(array_iter, "TextDirection", "ltr");
  appendStrPropTo(array_iter, "Status", "normal");
  // IconThemePath is type "as" (array of strings), not a plain string -
  // append it directly rather than via appendStrPropTo.
  DBusMessageIter entry, var, arr;
  dbus_message_iter_open_container(array_iter, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  const char *name = "IconThemePath";
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
  dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);
  dbus_message_iter_close_container(&var, &arr); // left empty
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(array_iter, &entry);
}

// Appends one dbusmenu properties dict (a{sv}) for a single MenuNode -
// shared by both GetLayout (recursive tree) and GetGroupProperties
// (flat list) below.
void appendMenuNodeProperties(DBusMessageIter *props_iter,
                              const MenuNode &node) {
  if (node.is_separator) {
    appendStrPropTo(props_iter, "type", "separator");
    appendBoolPropTo(props_iter, "enabled", true);
    appendBoolPropTo(props_iter, "visible", true);
    return;
  }
  appendStrPropTo(props_iter, "label", node.label);
  appendBoolPropTo(props_iter, "enabled", node.enabled);
  appendBoolPropTo(props_iter, "visible", true);
  if (!node.children.empty())
    appendStrPropTo(props_iter, "children-display", "submenu");
}

// Recursively marshals one MenuNode into the dbusmenu wire format.
// is_root: GetLayout's "layout" out-parameter is the (ia{sv}av) struct
// itself, not wrapped in a variant - only ITS children (and every
// deeper level) are each wrapped in a variant. parent_iter is that
// struct iter directly for is_root, or the "av" (array-of-variants)
// container this node should append one new variant entry into
// otherwise.
void marshalMenuNode(DBusMessageIter *parent_iter, const MenuNode &node,
                     bool is_root) {
  DBusMessageIter var, own_struct;
  DBusMessageIter *target;
  if (is_root) {
    target = parent_iter;
  } else {
    dbus_message_iter_open_container(parent_iter, DBUS_TYPE_VARIANT,
                                     "(ia{sv}av)", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_STRUCT, nullptr,
                                     &own_struct);
    target = &own_struct;
  }

  dbus_int32_t id = node.id;
  dbus_message_iter_append_basic(target, DBUS_TYPE_INT32, &id);

  DBusMessageIter props;
  dbus_message_iter_open_container(target, DBUS_TYPE_ARRAY, "{sv}", &props);
  appendMenuNodeProperties(&props, node);
  dbus_message_iter_close_container(target, &props);

  DBusMessageIter children_arr;
  dbus_message_iter_open_container(target, DBUS_TYPE_ARRAY, "v", &children_arr);
  for (const MenuNode &child : node.children) {
    marshalMenuNode(&children_arr, child, false);
  }
  dbus_message_iter_close_container(target, &children_arr);

  if (!is_root) {
    dbus_message_iter_close_container(&var, &own_struct);
    dbus_message_iter_close_container(parent_iter, &var);
  }
}

void sendEmptyReply(DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (reply) {
    dbus_connection_send(g_conn, reply, nullptr);
    dbus_message_unref(reply);
  }
}

// Dispatches a menu-item click (dbusmenu's Event with eventId ==
// "clicked") to whichever of our own callbacks it corresponds to,
// based on the fixed ID ranges defined above.
void handleMenuActivation(int id) {
  if (id == kIdShowWindow) {
    if (g_onShowMainWindow)
      g_onShowMainWindow();
  } else if (id == kIdQuit) {
    if (g_onQuit)
      g_onQuit();
  } else if (id >= kIdControllerBase && id < kIdControllerBase + 99) {
    int idx = id - kIdControllerBase;
    if (idx >= 0 && idx < (int)g_controllers.size() && g_onToggleController)
      g_onToggleController(g_controllers[idx].id);
  }
  // Network submenu entries are informational only (enabled=false) -
  // no action on click.
}

void emitLayoutUpdated() {
  if (!g_conn)
    return;
  g_menu_revision++;
  DBusMessage *sig =
      dbus_message_new_signal(kMenuPath, kMenuIface, "LayoutUpdated");
  if (!sig)
    return;
  dbus_uint32_t rev = g_menu_revision;
  dbus_int32_t parent = 0;
  dbus_message_append_args(sig, DBUS_TYPE_UINT32, &rev, DBUS_TYPE_INT32,
                           &parent, DBUS_TYPE_INVALID);
  dbus_connection_send(g_conn, sig, nullptr);
  dbus_message_unref(sig);
}

DBusHandlerResult messageFilter(DBusConnection *, DBusMessage *msg, void *) {
  const char *path = dbus_message_get_path(msg);
  bool is_item_path = path && std::string(path) == kItemPath;
  bool is_menu_path = path && std::string(path) == kMenuPath;

  // Let D-Bus handle messages destined for other paths (or root)
  if (!is_item_path && !is_menu_path) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  // ---- org.freedesktop.DBus.Properties ----
  if (dbus_message_is_method_call(msg, kPropsIface, "GetAll")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, array;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array);
    if (is_menu_path)
      appendDbusMenuProperties(&array);
    else
      appendStatusNotifierItemProperties(&array);
    dbus_message_iter_close_container(&iter, &array);
    dbus_connection_send(g_conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  if (dbus_message_is_method_call(msg, kPropsIface, "Get")) {
    DBusError err;
    dbus_error_init(&err);
    const char *iface = nullptr;
    const char *prop = nullptr;

    if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &iface,
                               DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID)) {
      DBusMessage *err_reply =
          dbus_message_new_error(msg, err.name, err.message);
      dbus_connection_send(g_conn, err_reply, nullptr);
      dbus_message_unref(err_reply);
      dbus_error_free(&err);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    DBusMessage *err_reply = dbus_message_new_error(
        msg, "org.freedesktop.DBus.Error.UnknownProperty",
        "Individual property Get is not implemented; use GetAll instead.");

    dbus_connection_send(g_conn, err_reply, nullptr);
    dbus_message_unref(err_reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  // ---- org.freedesktop.StatusNotifierItem ----
  if (is_item_path &&
      dbus_message_is_method_call(msg, kItemIface, "Activate")) {
    if (g_onLeftClick)
      g_onLeftClick();
    sendEmptyReply(msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (is_item_path &&
      dbus_message_is_method_call(msg, kItemIface, "SecondaryActivate")) {
    sendEmptyReply(msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (is_item_path &&
      dbus_message_is_method_call(msg, kItemIface, "ContextMenu")) {
    // Hosts that support the Menu property (advertised above) use
    // dbusmenu directly and shouldn't need this fallback - left as a
    // no-op for hosts that call it anyway.
    sendEmptyReply(msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (is_item_path && dbus_message_is_method_call(msg, kItemIface, "Scroll")) {
    sendEmptyReply(msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  // ---- com.canonical.dbusmenu ----
  if (is_menu_path &&
      dbus_message_is_method_call(msg, kMenuIface, "GetLayout")) {
    dbus_int32_t parentId = 0, recursionDepth = -1;
    DBusMessageIter args;
    if (dbus_message_iter_init(msg, &args)) {
      if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_basic(&args, &parentId);
        dbus_message_iter_next(&args);
        if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_INT32) {
          dbus_message_iter_get_basic(&args, &recursionDepth);
        }
      }
    }
    MenuNode root = buildMenuTree();
    const MenuNode *target = findNodeById(root, parentId);
    if (!target)
      target = &root;

    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    dbus_uint32_t revision = g_menu_revision;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &revision);
    DBusMessageIter layout_struct;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, nullptr,
                                     &layout_struct);
    marshalMenuNode(&layout_struct, *target, true);
    dbus_message_iter_close_container(&iter, &layout_struct);
    dbus_connection_send(g_conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (is_menu_path &&
      dbus_message_is_method_call(msg, kMenuIface, "GetGroupProperties")) {
    // Batch property fetch - implemented via the same per-node
    // property builder GetLayout uses. Ignores the requested ids
    // filter's propertyNames the same way GetLayout does.
    MenuNode root = buildMenuTree();
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, arr;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
    // Flatten the tree and emit every node's properties - simplest
    // correct behavior even though most hosts only ask for GetLayout.
    std::vector<const MenuNode *> flat;
    std::function<void(const MenuNode &)> collect = [&](const MenuNode &n) {
      flat.push_back(&n);
      for (const MenuNode &c : n.children)
        collect(c);
    };
    collect(root);
    for (const MenuNode *n : flat) {
      DBusMessageIter entry_struct, props;
      dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr,
                                       &entry_struct);
      dbus_int32_t id = n->id;
      dbus_message_iter_append_basic(&entry_struct, DBUS_TYPE_INT32, &id);
      dbus_message_iter_open_container(&entry_struct, DBUS_TYPE_ARRAY, "{sv}",
                                       &props);
      appendMenuNodeProperties(&props, *n);
      dbus_message_iter_close_container(&entry_struct, &props);
      dbus_message_iter_close_container(&arr, &entry_struct);
    }
    dbus_message_iter_close_container(&iter, &arr);
    dbus_connection_send(g_conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (is_menu_path && dbus_message_is_method_call(msg, kMenuIface, "Event")) {
    DBusMessageIter args;
    if (dbus_message_iter_init(msg, &args)) {
      dbus_int32_t id = 0;
      if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_basic(&args, &id);
        dbus_message_iter_next(&args);

        const char *eventId = nullptr;
        if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
          dbus_message_iter_get_basic(&args, &eventId);

          if (eventId && std::string(eventId) == "clicked") {
            handleMenuActivation(id);
          }
        }
      }
    }
    sendEmptyReply(msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (is_menu_path &&
      dbus_message_is_method_call(msg, kMenuIface, "EventGroup")) {
    // Batch events - not expected to be sent by most hosts for simple
    // click interactions (Event is), but handled defensively so it
    // doesn't go unanswered. Returns an empty error-id list.
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, arr;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &arr);
    dbus_message_iter_close_container(&iter, &arr);
    dbus_connection_send(g_conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (is_menu_path &&
      dbus_message_is_method_call(msg, kMenuIface, "AboutToShow")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    dbus_bool_t need_update = FALSE; // our tree is already current
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &need_update);
    dbus_connection_send(g_conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (is_menu_path &&
      dbus_message_is_method_call(msg, kMenuIface, "AboutToShowGroup")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, upd, err;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &upd);
    dbus_message_iter_close_container(&iter, &upd);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &err);
    dbus_message_iter_close_container(&iter, &err);
    dbus_connection_send(g_conn, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void registerWithWatcher() {
  DBusMessage *msg = dbus_message_new_method_call(
      "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
      "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
  if (!msg)
    return;
  const char *name = g_bus_name.c_str();
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
  // Fire-and-forget: queued for the next dispatch, not waited on. If
  // no StatusNotifierWatcher is running (e.g. a desktop with no tray
  // host at all), this simply has no effect - there's nothing to
  // register with, matching how Windows behaves with no tray area.
  dbus_connection_send(g_conn, msg, nullptr);
  dbus_message_unref(msg);
}

} // namespace

bool isSupported() { return true; }

bool enable() {
  if (g_enabled)
    return true;

  DBusError err;
  dbus_error_init(&err);
  g_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (dbus_error_is_set(&err) || !g_conn) {
    spdlog::warn("TrayIcon: failed to connect to D-Bus session bus: {}",
                 err.message);
    dbus_error_free(&err);
    return false;
  }

  // Historical KDE convention, widely adopted as the de facto standard
  // bus name format for StatusNotifierItem clients regardless of
  // desktop environment.
  g_bus_name = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
  int result = dbus_bus_request_name(g_conn, g_bus_name.c_str(),
                                     DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
  if (dbus_error_is_set(&err) ||
      result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    spdlog::warn("TrayIcon: failed to claim D-Bus name '{}': {}", g_bus_name,
                 dbus_error_is_set(&err) ? err.message : "name already taken");
    dbus_error_free(&err);
    g_conn = nullptr;
    return false;
  }

  dbus_connection_add_filter(g_conn, messageFilter, nullptr, nullptr);
  registerWithWatcher();

  g_enabled = true;
  spdlog::info("Tray icon enabled ({})", g_bus_name);
  return true;
}

void disable() {
  if (!g_enabled)
    return;
  if (g_conn) {
    dbus_connection_remove_filter(g_conn, messageFilter, nullptr);
    dbus_bus_release_name(g_conn, g_bus_name.c_str(), nullptr);
    dbus_connection_unref(g_conn);
    g_conn = nullptr;
  }
  g_enabled = false;
  spdlog::info("Tray icon disabled");
}

bool isEnabled() { return g_enabled; }

void update() {
  if (!g_conn)
    return;
  // dbus_connection_read_write_dispatch() returns TRUE as long as the
  // connection remains open - NOT only when there's something to
  // process. Looping on it (as an earlier version of this function
  // did) spins forever the instant there's nothing new to dispatch,
  // which is essentially always - a 100%-CPU infinite loop on the very
  // first idle frame, hanging this single-threaded app completely.
  // read_write() (non-blocking, timeout 0) pulls in whatever's
  // available on the wire without dispatching it yet; dispatch()'s
  // return value correctly distinguishes "more queued work" from
  // "nothing left," so looping on THAT is safe.
  dbus_connection_read_write(g_conn, 0);
  while (dbus_connection_dispatch(g_conn) == DBUS_DISPATCH_DATA_REMAINS) {
  }
}

void setControllerList(const std::vector<ControllerEntry> &entries) {
  // Only bump the menu revision (and tell the host to re-fetch) if
  // something actually changed - comparing the whole vector is cheap
  // for the small lists this app deals with, and avoids spamming
  // LayoutUpdated every single frame regardless of whether anything
  // changed.
  bool changed = entries.size() != g_controllers.size();
  if (!changed) {
    for (size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].id != g_controllers[i].id ||
          entries[i].title != g_controllers[i].title ||
          entries[i].minimized != g_controllers[i].minimized) {
        changed = true;
        break;
      }
    }
  }
  g_controllers = entries;
  if (changed && g_enabled)
    emitLayoutUpdated();
}

void setNetworkStatus(NetworkStatus status,
                      const std::vector<ConnectionEntry> &connections) {
  bool changed =
      status != g_networkStatus || connections.size() != g_connections.size();
  if (!changed) {
    for (size_t i = 0; i < connections.size(); ++i) {
      if (connections[i].label != g_connections[i].label) {
        changed = true;
        break;
      }
    }
  }
  g_networkStatus = status;
  g_connections = connections;
  if (changed && g_enabled)
    emitLayoutUpdated();
}

void setOnLeftClick(VoidCallback cb) { g_onLeftClick = cb; }
void setOnShowMainWindow(VoidCallback cb) { g_onShowMainWindow = cb; }
void setOnQuit(VoidCallback cb) { g_onQuit = cb; }
void setOnToggleController(ControllerCallback cb) { g_onToggleController = cb; }

#else // macOS / other - no real implementation yet

// ------------------------------------------------------------------
// No real implementation yet on this platform - see the comment at
// the top of tray_icon.h for why (macOS needs Objective-C for
// NSStatusItem; on Linux this also lands here if libdbus-1 wasn't
// found at build time - see CMakeLists.txt). Every function here is a
// safe no-op so callers in main.cpp and settings_window.cpp never need
// to special-case the platform themselves.
// ------------------------------------------------------------------

bool isSupported() { return false; }
bool enable() { return false; }
void disable() {}
bool isEnabled() { return false; }
void update() {}
void setControllerList(const std::vector<ControllerEntry> &) {}
void setNetworkStatus(NetworkStatus, const std::vector<ConnectionEntry> &) {}
void setOnLeftClick(VoidCallback) {}
void setOnShowMainWindow(VoidCallback) {}
void setOnQuit(VoidCallback) {}
void setOnToggleController(ControllerCallback) {}

#endif

} // namespace TrayIcon
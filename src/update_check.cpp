#include "update_check.h"

#include "app_version.h"
#include "version_utils.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

namespace UpdateCheck {
namespace {

constexpr const char *kApiHost = "api.github.com";
constexpr const char *kApiPath = "/repos/Khyretos/3dco-plus/releases/latest";

std::atomic<Status> g_status{Status::Idle};
std::atomic<bool> g_manual{false};
std::mutex g_mutex; // guards g_latest
Release g_latest;

// Fetches https://<kApiHost><kApiPath>. Returns false on any failure.
#if defined(_WIN32)
bool httpGet(std::string &body) {
  // WinHTTP ships with Windows - no extra dependency, and it honours the
  // system proxy settings.
  HINTERNET session =
      WinHttpOpen(L"3dco-plus-update-check", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session)
    return false;
  WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
  HINTERNET connection = nullptr, request = nullptr;
  bool ok = false;
  std::wstring host(kApiHost, kApiHost + strlen(kApiHost));
  std::wstring path(kApiPath, kApiPath + strlen(kApiPath));
  connection =
      WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (connection)
    request = WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (request &&
      WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n",
                         (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD code = 0, size = sizeof(code);
    WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
    if (code == 200) {
      ok = true;
      DWORD available = 0;
      while (WinHttpQueryDataAvailable(request, &available) && available) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) {
          ok = false;
          break;
        }
        body.append(chunk.data(), read);
      }
    }
  }
  if (request)
    WinHttpCloseHandle(request);
  if (connection)
    WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  return ok;
}
#else
bool httpGet(std::string &body) {
  // curl is present on every macOS install and virtually every Linux
  // desktop, so shelling out to it avoids bundling an HTTP/TLS stack.
  // The command is a fixed string - nothing user-controlled goes in.
  const std::string cmd = std::string(
#if defined(__linux__)
                              // An AppImage may point these at its own
                              // bundled libraries, which the system curl
                              // must not pick up.
                              "env -u LD_LIBRARY_PATH -u LD_PRELOAD "
#endif
                              "curl -fsSL --max-time 15 "
                              "-H 'Accept: application/vnd.github+json' "
                              "-A 3dco-plus-update-check "
                              "'https://") +
                          kApiHost + kApiPath + "' 2>/dev/null";
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    return false;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0)
    body.append(buf, n);
  return pclose(pipe) == 0 && !body.empty();
}
#endif

void run() {
  std::string body;
  if (!httpGet(body)) {
    spdlog::info("Update check: could not reach GitHub (skipped)");
    g_status = Status::Failed;
    return;
  }
  try {
    auto j = nlohmann::json::parse(body);
    Release r;
    AppVersion latest = parseAppVersion(j.value("tag_name", ""));
    r.notes = j.value("body", "");
    r.url = j.value("html_url", std::string(kReleasesPageUrl));
    AppVersion current = parseAppVersion(APP_VERSION_BASE);
    if (!latest.valid) {
      g_status = Status::Failed;
      return;
    }
    r.version = latest.str();
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_latest = r;
    }
    // A dev build (0.0.0) has no meaningful "newer" - treat as up to date.
    bool newer = current.valid && current < latest;
    spdlog::info("Update check: latest release is v{}{}", r.version,
                 newer ? " (newer than this build)" : "");
    g_status = newer ? Status::UpdateAvailable : Status::UpToDate;
  } catch (const std::exception &e) {
    spdlog::info("Update check: unexpected response ({})", e.what());
    g_status = Status::Failed;
  }
}

} // namespace

void start(bool manual) {
  Status expected = g_status.load();
  if (expected == Status::Checking)
    return;
  if (!g_status.compare_exchange_strong(expected, Status::Checking))
    return;
  g_manual = manual;
  std::thread(run).detach();
}

Status status() { return g_status.load(); }
bool lastCheckWasManual() { return g_manual.load(); }

Release latest() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_latest;
}

} // namespace UpdateCheck

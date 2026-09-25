#pragma once
// Small, dependency-free helpers for comparing version strings like
// "v1.3.4" or "1.3.4-2-gabc1234" (git describe). Header-only so the
// test suite can use them without linking the app.

#include <cstdio>
#include <string>
#include <tuple>

struct AppVersion {
  int major = 0, minor = 0, patch = 0;
  bool valid = false;

  bool operator<(const AppVersion &o) const {
    return std::tie(major, minor, patch) < std::tie(o.major, o.minor, o.patch);
  }
  bool operator==(const AppVersion &o) const {
    return std::tie(major, minor, patch) == std::tie(o.major, o.minor, o.patch);
  }
  bool operator!=(const AppVersion &o) const { return !(*this == o); }

  std::string str() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." +
           std::to_string(patch);
  }
};

// Parses the leading "X.Y.Z" (optionally prefixed with v/V); anything
// after it (a "-2-gabc1234" suffix, say) is ignored. valid=false if the
// string doesn't start with a version, or is 0.0.0 (a dev build).
inline AppVersion parseAppVersion(const std::string &text) {
  AppVersion v;
  const char *p = text.c_str();
  if (*p == 'v' || *p == 'V')
    ++p;
  if (std::sscanf(p, "%d.%d.%d", &v.major, &v.minor, &v.patch) == 3 &&
      v.major >= 0 && v.minor >= 0 && v.patch >= 0)
    v.valid = v.major || v.minor || v.patch;
  return v;
}

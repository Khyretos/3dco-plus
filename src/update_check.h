#pragma once
// Optional "is there a newer release?" check against this project's
// GitHub releases. Runs on a background thread so it never blocks a
// frame; any failure (offline, firewall, rate limit) just leaves the
// status at Failed, silently.

#include <string>

namespace UpdateCheck {

enum class Status { Idle, Checking, UpToDate, UpdateAvailable, Failed };

struct Release {
  std::string version; // "1.3.5"
  std::string notes;   // release description (Markdown)
  std::string url;     // the release's GitHub page
};

// Starts a check unless one is already running. `manual` marks it as
// user-initiated (Help section's Check Now), which the UI uses to show
// the result even for a version the user chose to skip.
void start(bool manual = false);

Status status();
bool lastCheckWasManual();
// Valid while status() == UpdateAvailable.
Release latest();

// Where the Help section's download button points.
constexpr const char *kReleasesPageUrl =
    "https://github.com/Khyretos/3dco-plus/releases/latest";

} // namespace UpdateCheck

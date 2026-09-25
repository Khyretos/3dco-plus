// Tests for parseAppVersion() and AppVersion comparisons
// (version_utils.h) - what the "What's New" dialog and the update check
// use to decide whether one version is newer than another.

#include "doctest.h"
#include "version_utils.h"

TEST_CASE("parses plain, v-prefixed and git-describe versions") {
  AppVersion v = parseAppVersion("1.3.4");
  CHECK(v.valid);
  CHECK(v.major == 1);
  CHECK(v.minor == 3);
  CHECK(v.patch == 4);
  CHECK(parseAppVersion("v1.3.4") == v);
  CHECK(parseAppVersion("V1.3.4") == v);
  // Commits past a tag still count as that tag's version.
  CHECK(parseAppVersion("1.3.4-2-gabc1234") == v);
  CHECK(parseAppVersion("1.3.4-2-gabc1234").str() == "1.3.4");
}

TEST_CASE("rejects non-versions and dev builds") {
  CHECK_FALSE(parseAppVersion("").valid);
  CHECK_FALSE(parseAppVersion("main").valid);
  CHECK_FALSE(parseAppVersion("1.3").valid);
  CHECK_FALSE(parseAppVersion("0.0.0-dev").valid);
  CHECK_FALSE(parseAppVersion("-1.2.3").valid);
}

TEST_CASE("orders versions numerically, not as text") {
  CHECK(parseAppVersion("1.3.9") < parseAppVersion("1.3.10"));
  CHECK(parseAppVersion("1.9.0") < parseAppVersion("1.10.0"));
  CHECK(parseAppVersion("1.3.3") < parseAppVersion("2.0.0"));
  CHECK_FALSE(parseAppVersion("1.3.3") < parseAppVersion("1.3.3"));
  CHECK(parseAppVersion("1.3.3") != parseAppVersion("1.3.4"));
}

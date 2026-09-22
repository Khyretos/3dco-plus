// Regression tests for loadModelMetadata() (model_persistence.cpp) -
// the extracted slice of readInfoJson() that handles source,
// description, and globalMaterial. This links against the real
// production function, not a copy of it: model_persistence.cpp is
// compiled into this test binary directly (see tests/CMakeLists.txt),
// so a future edit to the real function that reintroduces the bug
// class below will fail here, not just get noticed by a human
// re-reading the code.
//
// The bug class these are built around: a field only ever got *set*
// when its own JSON key existed, never *reset* when it didn't - and
// since the same Model object is reused across a model switch rather
// than freshly constructed, a model with no value of its own for a
// field could silently inherit whatever the *previous* model had
// there. Found three times in one session (source, description,
// globalMaterial) before being fixed - these tests exist so a fourth
// occurrence, or a regression in one of the first three, fails loudly
// instead of waiting to be noticed by a user switching models.

#include "doctest.h"
#include "model_persistence.h"

using json = nlohmann::json;

TEST_CASE("loadModelMetadata: source and description round-trip when present") {
  Model m;
  json data = json::parse(
      R"({"source": "https://example.com/a", "description": "Credit: Alice"})");
  loadModelMetadata(m, data);
  CHECK(m.source == "https://example.com/a");
  CHECK(m.description == "Credit: Alice");
}

TEST_CASE("loadModelMetadata: source and description reset to blank when "
         "absent - the exact bug pattern") {
  Model m;
  // First, a model WITH both fields set (simulating the "previous
  // model" in the reused-Model scenario).
  loadModelMetadata(
      m, json::parse(
             R"({"source": "https://example.com/a", "description": "Credit: Alice"})"));
  REQUIRE(m.source == "https://example.com/a");
  REQUIRE(m.description == "Credit: Alice");

  // Then load a SECOND model's data into the SAME Model object, where
  // that second model's JSON has neither key at all - this is the
  // exact scenario that used to leave the first model's values
  // displayed as if they belonged to the second.
  loadModelMetadata(m, json::parse(R"({"parts": []})"));
  CHECK(m.source == "");
  CHECK(m.description == "");
}

TEST_CASE("loadModelMetadata: explicit empty string is preserved alongside "
         "a real value in the same load") {
  Model m;
  loadModelMetadata(
      m, json::parse(R"({"source": "", "description": "Credit: Bob"})"));
  CHECK(m.source == "");
  CHECK(m.description == "Credit: Bob");
}

TEST_CASE("loadModelMetadata: switching back to a described model after an "
         "undescribed one restores its own values") {
  Model m;
  json described = json::parse(
      R"({"source": "https://example.com/a", "description": "Credit: Alice"})");
  loadModelMetadata(m, described);
  loadModelMetadata(m, json::parse(R"({})"));
  loadModelMetadata(m, described);
  CHECK(m.source == "https://example.com/a");
  CHECK(m.description == "Credit: Alice");
}

TEST_CASE("loadModelMetadata: globalMaterial resets to struct defaults when "
         "the model's JSON has no global_material key at all") {
  Model m;
  // A model that DID customize its global material...
  loadModelMetadata(
      m, json::parse(
             R"({"global_material": {"ambient": 0.9, "diffuse": 0.1, "color": [1.0, 0.0, 0.0]}})"));
  REQUIRE(m.globalMaterial.ambient == doctest::Approx(0.9));
  REQUIRE(m.globalMaterial.diffuse == doctest::Approx(0.1));

  // ...followed by a model that never customized its own - this used
  // to skip the whole reset entirely and silently keep the previous
  // model's ambient/diffuse/color values.
  Material defaults; // struct's own defaults, whatever they are
  loadModelMetadata(m, json::parse(R"({"parts": []})"));
  CHECK(m.globalMaterial.ambient == doctest::Approx(defaults.ambient));
  CHECK(m.globalMaterial.diffuse == doctest::Approx(defaults.diffuse));
  CHECK(m.globalMaterial.color[0] == doctest::Approx(defaults.color[0]));
}

TEST_CASE("loadModelMetadata: globalMaterial field present in JSON but "
         "missing individual keys falls back to struct defaults for those "
         "keys, not the previous model's values") {
  Model m;
  loadModelMetadata(
      m, json::parse(R"({"global_material": {"ambient": 0.9}})"));
  REQUIRE(m.globalMaterial.ambient == doctest::Approx(0.9));

  Material defaults;
  // global_material key exists, but doesn't mention ambient this time
  // - should fall back to the struct default, not stay at 0.9.
  loadModelMetadata(
      m, json::parse(R"({"global_material": {"diffuse": 0.5}})"));
  CHECK(m.globalMaterial.ambient == doctest::Approx(defaults.ambient));
  CHECK(m.globalMaterial.diffuse == doctest::Approx(0.5));
}

// ---- resolveUseCustomToggle ----
// Three-generation migration: current key wins if present; else the
// older, inverted-sense legacy key if THAT's present; else true
// (custom) for a mesh with neither - matching how this looked before
// either key existed. Same branch used for both
// use_custom_material/use_global_material and
// use_custom_textures/use_global_textures, so these tests exercise
// the shared function generically rather than duplicating cases for
// each pair of key names.

TEST_CASE("resolveUseCustomToggle: current key present wins outright, "
         "legacy key ignored even if also present") {
  json p = json::parse(
      R"({"use_custom_material": false, "use_global_material": false})");
  // If the legacy key were consulted here it would (inverted) also
  // say true - picking false instead proves the current key actually
  // won, not merely that both happened to agree.
  CHECK_FALSE(resolveUseCustomToggle(p, "use_custom_material",
                                     "use_global_material"));
}

TEST_CASE("resolveUseCustomToggle: current key absent, legacy key present "
         "and inverted correctly") {
  json p = json::parse(R"({"use_global_material": true})");
  // use_global_material: true means NOT custom - i.e. false here.
  CHECK_FALSE(resolveUseCustomToggle(p, "use_custom_material",
                                     "use_global_material"));

  json p2 = json::parse(R"({"use_global_material": false})");
  CHECK(resolveUseCustomToggle(p2, "use_custom_material",
                               "use_global_material"));
}

TEST_CASE("resolveUseCustomToggle: neither key present defaults to true "
         "(custom) - matching a mesh saved before either key existed") {
  json p = json::parse(R"({"name": "Button_A"})");
  CHECK(resolveUseCustomToggle(p, "use_custom_material",
                               "use_global_material"));
}

TEST_CASE("resolveUseCustomToggle: works identically for the textures pair "
         "of key names, not just materials") {
  json p = json::parse(R"({"use_global_textures": true})");
  CHECK_FALSE(
      resolveUseCustomToggle(p, "use_custom_textures", "use_global_textures"));
}

// ---- selectEffectiveTextures ----
// All-or-nothing selection, not a per-type merge - see this
// function's own doc comment (model_persistence.h) for the stale
// "per-type merge" comment this replaced at the real call site
// (model.cpp's drawMesh()). These tests exist specifically to lock in
// the actual (all-or-nothing) behavior as a regression check, given
// the nearby comment describing different behavior went unnoticed for
// as long as it did.

TEST_CASE("selectEffectiveTextures: custom on returns only the mesh's own "
         "textures, even when the mesh's list is missing types the global "
         "list has") {
  std::vector<Texture> meshTextures(1); // one texture, e.g. just Diffuse
  std::vector<Texture> globalTextures(3); // e.g. Diffuse, Normal, AO
  auto result = selectEffectiveTextures(true, meshTextures, &globalTextures,
                                        16);
  // If this merged per-type (matching the STALE comment) this would
  // be some mix reaching toward 3; all-or-nothing means exactly the
  // mesh's own 1, global entries never entering the result at all.
  CHECK(result.size() == 1);
  CHECK(result[0] == &meshTextures[0]);
}

TEST_CASE("selectEffectiveTextures: custom off returns only the global "
         "textures, even when the mesh has its own textures too") {
  std::vector<Texture> meshTextures(2);
  std::vector<Texture> globalTextures(3);
  auto result = selectEffectiveTextures(false, meshTextures, &globalTextures,
                                        16);
  CHECK(result.size() == 3);
  for (size_t i = 0; i < result.size(); ++i)
    CHECK(result[i] == &globalTextures[i]);
}

TEST_CASE("selectEffectiveTextures: custom off with a null globalTextures "
         "pointer returns empty rather than crashing") {
  std::vector<Texture> meshTextures(2);
  auto result = selectEffectiveTextures(false, meshTextures, nullptr, 16);
  CHECK(result.empty());
}

TEST_CASE("selectEffectiveTextures: truncates to maxTextures rather than "
         "overflowing the shader's own texture array") {
  std::vector<Texture> meshTextures(20);
  auto result = selectEffectiveTextures(true, meshTextures, nullptr, 16);
  CHECK(result.size() == 16);
}

TEST_CASE("selectEffectiveTextures: an empty mesh texture list with custom "
         "on returns empty, not a fallback to global") {
  std::vector<Texture> meshTextures; // empty - "on" but nothing added yet
  std::vector<Texture> globalTextures(3);
  auto result = selectEffectiveTextures(true, meshTextures, &globalTextures,
                                        16);
  CHECK(result.empty());
}

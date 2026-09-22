#include "model_persistence.h"

using json = nlohmann::json;

void loadModelMetadata(Model &m, const json &data) {
  // Unconditional assignment (defaulting to empty), not "only if the
  // key exists" - the same Model object gets reused across a
  // loadModel() call for a different model, not freshly constructed
  // each time (switching models via the dropdown calls loadModel()
  // again on the same current_window->model), so a conditional set
  // here left whatever the PREVIOUS model's source/description
  // happened to be sitting in memory, displayed as if it belonged to
  // the new model, for any model whose own JSON simply doesn't have
  // that key at all (never described, or saved before this field
  // existed) - most visible as description never actually going
  // blank switching from a described model to an undescribed one.
  m.source = data.value("source", "");
  m.description = data.value("description", "");

  // Reset to Material's own struct defaults first, then conditionally
  // apply what this model's own JSON actually has - same reused-Model
  // reasoning as source/description just above. Previously this both
  // skipped the whole block entirely when a model's JSON has no
  // "global_material" key at all (never customized, or saved before
  // this feature existed), and even within the block defaulted each
  // field to ITS OWN CURRENT VALUE via gm.value("ambient",
  // m.globalMaterial.ambient) rather than a real default - so a model
  // with no global material of its own, loaded right after one that
  // did customize it, silently inherited that other model's values
  // instead of this model's own (implicit, default) material.
  m.globalMaterial = Material();
  if (data.contains("global_material") && data["global_material"].is_object()) {
    const auto &gm = data["global_material"];
    m.globalMaterial.ambient = gm.value("ambient", m.globalMaterial.ambient);
    m.globalMaterial.diffuse = gm.value("diffuse", m.globalMaterial.diffuse);
    m.globalMaterial.specular = gm.value("specular", m.globalMaterial.specular);
    m.globalMaterial.shininess =
        gm.value("shininess", m.globalMaterial.shininess);
    m.globalMaterial.alpha = gm.value("alpha", m.globalMaterial.alpha);
    if (gm.contains("color") && gm["color"].is_array() &&
        gm["color"].size() >= 3) {
      m.globalMaterial.color[0] = gm["color"][0].get<float>();
      m.globalMaterial.color[1] = gm["color"][1].get<float>();
      m.globalMaterial.color[2] = gm["color"][2].get<float>();
    }
  }
}

bool resolveUseCustomToggle(const json &meshJson, const char *currentKey,
                            const char *legacyKey) {
  if (meshJson.contains(currentKey))
    return meshJson.value(currentKey, true);
  if (meshJson.contains(legacyKey))
    return !meshJson.value(legacyKey, false);
  return true;
}

std::vector<const Texture *>
selectEffectiveTextures(bool useCustomTextures,
                        const std::vector<Texture> &meshTextures,
                        const std::vector<Texture> *globalTextures,
                        size_t maxTextures) {
  std::vector<const Texture *> effectiveTextures;
  if (useCustomTextures) {
    for (const Texture &t : meshTextures)
      effectiveTextures.push_back(&t);
  } else if (globalTextures) {
    for (const Texture &t : *globalTextures)
      effectiveTextures.push_back(&t);
  }
  if (effectiveTextures.size() > maxTextures)
    effectiveTextures.resize(maxTextures);
  return effectiveTextures;
}

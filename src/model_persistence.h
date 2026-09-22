#ifndef MODEL_PERSISTENCE_H
#define MODEL_PERSISTENCE_H

#include "model.h"

#include <nlohmann/json.hpp>
#include <vector>

// Extracted from readInfoJson() (model.cpp) specifically because this
// slice of it - unlike the rest of that function - touches no GL
// state and needs nothing beyond the Model/Material struct
// definitions and nlohmann::json, so it can be compiled and unit
// tested in isolation from the rest of the app (no GLFW/SDL/assimp
// context required).
//
// Handles source, description, and globalMaterial - the exact three
// fields where the same bug shape was found three times in one
// session: a field only ever got *set* when its JSON key existed,
// never *reset* when it didn't, so a model reusing the same in-memory
// Model object (switching models via the dropdown reuses
// current_window->model rather than constructing a fresh one) could
// silently inherit the previous model's values for any field its own
// JSON simply doesn't mention. Deliberately does NOT handle
// global_textures - that part of the original block calls
// loadTexture()/deleteTexture(), which need a real GL context, so it
// stays inline in readInfoJson() rather than being pulled in here.
//
// Always resets every field it owns before conditionally applying
// what's actually in `data` - never left as "whatever m already
// held" - which is the property this function's own tests are built
// to check for.
void loadModelMetadata(Model &m, const nlohmann::json &data);

// Extracted from readInfoJson()'s per-mesh loop (model.cpp). Migrates
// three generations of the same on/off toggle to today's single
// bool: the current "use_custom_material"/"use_custom_textures" keys
// if present; else the older, inverted-sense "use_global_material"/
// "use_global_textures" keys if THOSE are present (from before the
// toggle was reframed the other way around); else true (custom) -
// matching "untagged meshes default to custom" from when this
// feature was first introduced, before either key existed at all.
// Two independent instances of the identical three-way branch in the
// real code (once for material, once for textures) - a single
// function used for both, rather than the same logic hand-written
// twice, so a fix to the migration order only has to happen once.
bool resolveUseCustomToggle(const nlohmann::json &meshJson,
                            const char *currentKey, const char *legacyKey);

// Extracted from drawMesh()'s texture-selection block (model.cpp).
// Despite that block's own nearby comment describing a "per-type
// merge" (a mesh's own Diffuse coexisting with an inherited global
// Normal Map) - that description is stale relative to the code: the
// actual selection has been a plain all-or-nothing toggle since the
// use_custom_textures redesign, not a per-type merge, and the comment
// was never updated to say so. This function - and its own tests -
// exist to make that actual behavior explicit and locked in, rather
// than left to silently drift further from whatever the next reader
// assumes from the comment.
//
// Returns pointers into whichever source list was selected (never
// copies), truncated to maxTextures - mirroring the MAX_TEXTURES cap
// the real shader's texture array enforces.
std::vector<const Texture *>
selectEffectiveTextures(bool useCustomTextures,
                        const std::vector<Texture> &meshTextures,
                        const std::vector<Texture> *globalTextures,
                        size_t maxTextures);

#endif // MODEL_PERSISTENCE_H

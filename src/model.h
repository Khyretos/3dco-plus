#ifndef MODEL_H
#define MODEL_H

#include <SDL3/SDL.h>

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>

// Existing structs
typedef struct pos_struct {
  GLfloat x = 0;
  GLfloat y = 0;
  GLfloat z = 0;
} vertex_position;

typedef struct norm_struct {
  GLfloat x = 0;
  GLfloat y = 0;
  GLfloat z = 0;
} vertex_normal;

typedef struct texcoord_struct {
  GLfloat x = 0;
  GLfloat y = 0;
} vertex_texcoord;

typedef struct vertex_struct {
  int position = 0;
  int normal = 0;
  int texcoord = 0;
} Vertex;

typedef struct material_struct {
  float ambient = 0.2f;
  float diffuse = 1.0f;
  float specular = 0.1f;
  float shininess = 32.0f;
  float color[3] = {0.3f, 0.3f, 0.3f};
  float highlight[3] = {0.0f, 1.0f, 0.0f};
  float alpha = 1.0f;
} Material;

typedef struct face_struct {
  std::vector<int> indices;
} Face;

typedef struct texture_struct {
  GLuint id = 0;
  std::string name = "";
  std::string path;
  int type = 0;
  int wrapX = 0;
  int wrapY = 0;
  float offsetX = 0;
  // Left at 1.0 rather than 0 mostly for history - functionally the
  // two are identical, since GL_TEXTURE_WRAP_T is unconditionally
  // GL_REPEAT (see loadTexture()/loadModel() in model.cpp) and a full
  // period offset on a repeating coordinate wraps back to exactly
  // where it started. Only affects a *newly added* texture's starting
  // values - an existing texture loaded from a saved model keeps
  // whatever it was actually saved with, even if that model predates
  // this default and has no offsetY key at all (see readInfoJson() in
  // model.cpp, whose fallback for a missing key is unchanged at 0).
  float offsetY = 1.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  float rotation = 0.0f;
  float border[4] = {0.8f, 0.8f, 0.8f, 1.0f};
  // Mirrors the texture horizontally/vertically within its own UV
  // space, before rotation/offset are applied - for source images
  // whose content reads backwards on a mesh (e.g. exported/authored
  // mirrored relative to how the mesh's own UV island is laid out,
  // like text appearing reversed) with no way to fix that by editing
  // the source image itself without also breaking its alignment.
  bool flipX = false;
  // Was true by default, on the strength of one real model+texture
  // pair it had been checked against - user feedback across a wider
  // range of real textures showed that combination was wrong more
  // often than right, so this now starts off, matching what most
  // images exported the ordinary way actually need. Only affects a
  // *newly added* texture's starting value - see offsetY's own doc
  // comment just above for why an existing texture loaded from a
  // saved model is entirely unaffected by this default either way.
  bool flipY = false;
} Texture;

enum InputType {
  INPUT_TYPE_GAMEPAD = 0,
  INPUT_TYPE_JOYSTICK = 1,
  INPUT_TYPE_KEYBOARD = 2,
  INPUT_TYPE_MOUSE = 3
};

// One additional input binding on a mesh that already has its own,
// primary inputBinding - see Mesh::extraBindings below for the full
// reasoning. Deliberately holds only what's needed to independently
// evaluate one more "press this input -> move/rotate this much"
// contribution: no rendering data (vao/vbo/material/textures) is
// duplicated, since the mesh itself still renders exactly once.
//
// Scope: covers the same "digital press" style bindings as the
// primary one does for hats/buttons/axis-as-direction/keyboard/
// mouse-button - NOT leftstick/rightstick/raw-axis-passthrough/
// touchpad, which drive persistent single-value visual state
// (stick_X/Y, touch_X/Y) that doesn't fit the "several independent
// contributions summed together" model this exists for.
typedef struct mesh_binding_struct {
  std::string inputBinding;
  int inputType = 0; // 0=Gamepad, 1=Joystick, 2=Keyboard, 3=Mouse
  bool invert = false;

  float travel[3] = {0.0f, 0.0f, 0.0f};
  float travel_rotation[3] = {0.0f, 0.0f, 0.0f}; // degrees

  bool smooth_travel_enabled = false;
  float smooth_travel_duration = 0.15f; // seconds

  // Runtime-only (not persisted) - mirrors the primary binding's own
  // travel_value/travel_signed/travel_value_display/
  // travel_signed_display fields, one full independent copy per
  // extra binding so each one can be at a different point in its own
  // smooth-travel ease at the same time (e.g. a hat moving from Up to
  // Left-Up eases the newly-active binding in while the one going
  // inactive eases back out, rather than both snapping together).
  float travel_value = 0.0f;
  float travel_signed = 0.0f;
  float travel_value_display = 0.0f;
  float travel_signed_display = 0.0f;
} MeshBinding;

typedef struct mesh_struct {
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
  unsigned int elements = 0;

  Material material;
  std::vector<Texture> textures;

  // When false (default), this mesh uses Model::globalMaterial. When
  // true, it uses its own `material` values below instead. Global is
  // the default so a model-wide material change applies everywhere
  // unless a specific mesh deliberately opts out by turning this on.
  bool use_custom_material = false;

  // When false (default), this mesh uses Model::globalTextures. When
  // true, it uses its own `textures` list below instead.
  bool use_custom_textures = false;

  // Position / motion data
  float position[3] = {0.0f, 0.0f, 0.0f};
  float travel[3] = {0.0f, 0.0f, 0.0f};
  float popup_offset[3] = {0.0f, 0.0f, 0.0f};
  float popup_rotation[3] = {0.0f, 0.0f, 0.0f};
  float trigger_max = 0.0f;
  float stick_max = 0.0f;
  float touch_width = 0.0f;
  float touch_height = 0.0f;

  // Press-induced rotation (for flightsticks, etc.)
  float travel_rotation[3] = {0.0f, 0.0f, 0.0f}; // degrees

  // ---- Smooth travel animation (optional, 1.1.1) ----
  // Off by default (identical to pre-1.1.1 behavior: travel/travel_rotation
  // snap instantly to their target every frame). When enabled, the
  // *rendered* travel amount eases toward its target over
  // smooth_travel_duration seconds instead of snapping - see
  // computeMeshTransform() in model.cpp for where this is applied, and
  // the per-frame update in controller_window.cpp for where the easing
  // itself happens. travel_value/travel_signed below are deliberately
  // left as the raw, instantaneous input state (network sync and
  // anything else that wants the true current input still reads those
  // directly) - travel_value_display/travel_signed_display are the
  // eased copies actually used for rendering, and are runtime-only
  // (not persisted - there's nothing meaningful to save mid-animation).
  bool smooth_travel_enabled = false;
  float smooth_travel_duration = 0.15f; // seconds
  float travel_value_display = 0.0f;
  float travel_signed_display = 0.0f;

  // Dual highlight for axes
  bool use_dual_highlight = false;
  float axis_deadzone = 0.1f; // 0-1 range, default 10%
  float highlight_color_positive[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  float highlight_color_negative[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  float custom_highlight_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  float axis_highlight_value = 0.0f; // signed value from axis

  glm::mat4 base_transform = glm::mat4(1.0f);

  float stick_X = 0;
  float stick_Y = 0;
  float pull = 0;
  float press = 0.0f;
  float anim_value = 0.0f; // normalised axis value for travel animation (0..1)
  float travel_value = 0.0f;  // 0..1 for buttons/sticks
  float travel_signed = 0.0f; // signed axis value for travel (-1..1)

  bool visible = true;
  bool popup = false;
  float highlight_value = 0.0f;
  int ring_highlight_deadzone = 15;

  Uint8 touch_state = 0;
  float touch_X = 0.0f;
  float touch_Y = 0.0f;

  int parentIndex = -1;
  float pivot_offset[3] = {0.0f, 0.0f, 0.0f};
  float rotation[3] = {0.0f, 0.0f, 0.0f}; // Euler angles in radians
  float scale[3] = {1.0f, 1.0f, 1.0f};
  bool useCustomScale = false;
  glm::vec3 bboxMin = glm::vec3(FLT_MAX);
  glm::vec3 bboxMax = glm::vec3(-FLT_MAX);
  bool hasBBox = false;

  std::string name;      // mesh name from file (e.g., "left_stick")
  int assignedPart = -1; // controller part index (0..34) or -1 if unassigned

  std::string filename; // OBJ file name (e.g., "left_stick.obj")
  // Input type: 0=Gamepad, 1=Joystick, 2=Keyboard, 3=Mouse
  int inputType = 0; // default to Gamepad
  float glow_intensity = 0.0f;
  // Removed press_color – press uses highlight color

  float original_color[3] = {0.8f, 0.8f, 0.8f};
  float original_alpha = 1.0f;
  std::string inputBinding; // e.g., "gamepad:b0", "joystick:a1+",
                            // "keyboard:key_w", "mouse:mouse_left"
  bool invert = false;

  // Additional, independent bindings beyond the primary one above -
  // for a mesh that needs to respond differently to several distinct
  // inputs (the motivating case: a joystick hat's 8 directions, one
  // physical mesh, each direction wanting its own movement). Every
  // binding here (plus the primary one above) is evaluated every
  // frame from its own live input state; computeMeshTransform() sums
  // every active one's travel and travel_rotation contribution
  // together rather than picking just one - two opposite-signed
  // contributions on the same axis naturally cancel out this way,
  // with no special-case logic needed for that. See MeshBinding's own
  // doc comment just above for what is/isn't supported per binding.
  std::vector<MeshBinding> extraBindings;
  bool isTouchpad = false;
  bool isBumper = false;
  bool isTrigger = false;
  bool isPaddle = false;
  bool isTouchpoint = false;

  float touch_offset[3] = {0.0f, 0.0f, 0.0f};
  float touch_rotation[3] = {0.0f, 0.0f, 0.0f};

  // Per‑mesh highlight override - use_custom_highlight gates both the
  // color (custom_highlight_color above) and this blend mode
  // together, rather than a second, separate toggle: they're the two
  // halves of the same "this mesh's highlight looks different from
  // the model's global one" override. Same 0=Add/1=Replace encoding
  // as controller_window::highlight_blend_mode (see that field's own
  // doc comment) - drawMesh() (model.cpp) picks this over the global
  // value when use_custom_highlight is on, the same way it already
  // does for custom_highlight_color.
  bool use_custom_highlight = false;
  int custom_highlight_blend_mode = 0;

  std::string shader_name; // empty => default
} Mesh;

// ----- Imported mesh data for custom model mapping -----
typedef struct imported_mesh_struct {
  std::string name;       // mesh name from the file
  int assigned_part = -1; // index into the 32 controller parts (0..31)

  // Raw vertex data (will be converted to GL buffers when applied)
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec2> texcoords;
  std::vector<unsigned int> indices;

  int parent_part = -1; // -1 = no parent
} ImportedMesh;

typedef struct model_struct {
  std::string path;
  std::vector<Mesh> meshes;
  glm::mat4 motion_matrix = glm::mat4(1.0f);
  bool popup_bumpers = false;
  bool popup_triggers = false;
  bool popup_paddles = false;

  std::string source;
  // Free-form notes about the model - credits, a contributor shout-out,
  // build notes, anything the person setting it up wants attached to
  // it. Purely informational: nothing reads this to change any
  // behavior, it just displays and saves/loads alongside source.
  std::string description;

  // A single texture applied to every part that doesn't have its own
  // texture assigned - lets a user apply one custom image across the
  // whole model at once instead of assigning the same texture to each
  // part individually. A per-part texture always overrides its
  // matching global one, but ONLY for the specific type that part
  // defines itself - a part with only its own Diffuse still falls
  // back to the global Normal Map, AO, etc. if those are set here and
  // that part doesn't define them itself (see drawMesh()'s merge
  // logic in model.cpp). Same list-of-Texture structure as a mesh's
  // own textures (so the same Type dropdown, UV controls, and the
  // same 0=Diffuse/1=Specular/.../6=AO type numbering apply here too)
  // rather than a single texture forced to Diffuse - that was this
  // feature's original, more limited form, which made it impossible
  // to apply a normal/AO/roughness/metallic map globally at all.
  // Applied against each mesh's own UVs individually - not a single
  // shared UV layout across the whole model, so results will vary per
  // part depending on how that part's own UVs happen to be laid out,
  // same as it would if the same image were assigned to each part by
  // hand one at a time.
  std::vector<Texture> globalTextures;

  // Model-wide material defaults, applied to every mesh whose
  // use_custom_material is false (see Mesh above). Same idea as
  // globalTextures - one place to tune ambient/diffuse/specular/
  // shininess/color/alpha for the whole model at once, rather than
  // editing it mesh by mesh. Particularly useful for normal maps:
  // they're most visible when specular is high and shininess is low
  // (a broad, responsive highlight that the perturbed normal can
  // swing around), which is not what a mesh's own saved values
  // usually are.
  Material globalMaterial;

  // ----- Temporary storage for imported meshes (used by importModelFile and
  // preview) -----
  std::vector<ImportedMesh> imported_meshes;
  bool has_imported_meshes = false;
} Model;

// model.h
struct ImportAssignment {
  std::string mesh_name;
  int assigned_part = -1;
  float max_angle = 0.0f;
  int parent_part = -1;
  float touch_width = 1.0f;
  float touch_height = 1.0f;
  // Optional - lets the user assign which input drives this part right
  // in the import dialog, the same Type + Specific Input picker the
  // Model table's own per-mesh assignment uses (see
  // drawInputBindingPicker() in settings_window.cpp), rather than
  // needing to import first and then set this up as a separate step
  // afterward. Empty means unbound, same convention as a Mesh's own
  // inputBinding - carried over onto the resulting Mesh once the
  // import is actually applied.
  std::string inputBinding;
};

struct ImportPreviewData {
  Model imported_model;
  std::vector<ImportAssignment> assignments;
  int selected_mesh_index = -1;
  bool is_open = false;
  std::string save_name = "NewModel";
};

// ----- Existing function declarations -----

void loadModel(Model &m, std::string path);

void loadMesh(Mesh &m, std::string path);

// readInfo(...) / writeInfo(...) removed - dead code with zero call sites
// anywhere in the codebase. They implemented the legacy plain-text
// info.txt format, which loadModel()'s own inline fallback reader already
// handles directly, and every save path goes through writeJson() instead.

void loadTexture(GLuint &id, std::string path);
// Just the filename, no directory - defined in model.cpp (used there
// for path healing), also used in settings_window.cpp for display
// names so the Textures list shows e.g. "1: diffuse.jpg" instead of
// the full stored path.
std::string extractFilenameCrossPlatform(const std::string &p);

void deleteTexture(GLuint &id);

// Takes the model by reference (not by value) — the model can hold a large
// imported mesh library, and this runs every frame per open window, so a
// deep copy here would be wasteful. See model.cpp for why this is safe.
void drawMesh(const Mesh &mesh, const glm::mat4 &modelMatrix, GLuint shader,
              const glm::vec4 &highlightColor,
              const glm::vec3 *baseColorOverride = nullptr,
              const glm::mat4 &view = glm::mat4(1.0f),
              const glm::mat4 &projection = glm::mat4(1.0f),
              const glm::vec3 &cameraPos = glm::vec3(0.0f, 0.0f, 0.0f),
              const std::string &globalShaderName = "",
              const std::vector<Texture> *globalTextures = nullptr,
              const Model *globalMaterial = nullptr,
              // 0 = Replace, 1 = Add - see controller_window.h's own
              // doc comment on highlight_blend_mode for what each
              // means. Defaulted so every other existing call site
              // (there's only the one, from drawModel below, but the
              // default keeps this non-breaking regardless) keeps
              // compiling and keeps today's Replace behavior without
              // being touched.
              int highlightBlendMode = 0);

void drawModel(Model &m, GLuint shader, int highlight_mesh_index = -1,
               const glm::vec4 &globalHighlightColor = glm::vec4(1.0f, 0.0f,
                                                                 0.0f, 1.0f),
               const glm::mat4 &view = glm::mat4(1.0f),
               const glm::mat4 &projection = glm::mat4(1.0f),
               const glm::vec3 &cameraPos = glm::vec3(0.0f, 0.0f, 0.0f),
               const std::string &globalShaderName = "",
               int highlightBlendMode = 0);

// ----- functions for custom mesh import and mapping -----
void importModelFile(Model &m, const std::string &filepath);

void convertImportedToMeshes(Model &m);

glm::mat4 computeMeshTransform(const Model &m, int meshIndex,
                               const glm::mat4 &parentMatrix);

glm::mat4 getMeshFinalMatrix(const Model &m, int idx,
                             const glm::mat4 &parent = glm::mat4(1.0f));

glm::vec3 computeMeshCenter(const Mesh &mesh);

// Smooth Travel Animation only makes sense for a mesh whose travel is
// driven by a discrete/digital input (a button, fully on or off) - a
// joystick cap (stick_max > 0, driven every frame by the physical
// stick's live tilt), a trigger (isTrigger, driven by how far it's
// actually pulled right now), or a touchpad/touchpoint (isTouchpad /
// isTouchpoint, driven by live touch position) are all continuous
// analog inputs, and easing any of those would make the rendered part
// visibly lag behind the real physical position under the user's
// fingers. Bumpers and paddles (isBumper/isPaddle) are NOT excluded -
// physically they're just buttons (fully pressed or not), regardless
// of whether they also happen to use the separate Popup offset/
// rotation. Shared between the settings UI (to hide/disable the
// control for these meshes) and the per-frame update in
// controller_window.cpp (as a runtime safety net, in case
// smooth_travel_enabled is set on one of these anyway - e.g. from an
// older save, or an unusual custom model).
bool isAnalogTravelMesh(const Mesh &mesh);

void writeJson(Model &m, const std::string &path);

glm::mat4 getModelMatrixWithoutGyro(const Model &m, int meshIdx);
glm::vec3 getModelWorldPositionWithoutGyro(const Model &m, int meshIdx);
bool wouldCreateCycle(const Model &m, int childIdx, int parentIdx);

int getTouchpadAncestor(const Model &m, int meshIndex);

#endif
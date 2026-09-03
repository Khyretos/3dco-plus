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
  float offsetY = 0;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  float rotation = 0.0f;
  float border[4] = {0.8f, 0.8f, 0.8f, 1.0f};
} Texture;

enum InputType {
  INPUT_TYPE_GAMEPAD = 0,
  INPUT_TYPE_JOYSTICK = 1,
  INPUT_TYPE_KEYBOARD = 2,
  INPUT_TYPE_MOUSE = 3
};

typedef struct mesh_struct {
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
  unsigned int elements = 0;

  Material material;
  std::vector<Texture> textures;

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
  bool isTouchpad = false;
  bool isBumper = false;
  bool isTrigger = false;
  bool isPaddle = false;
  bool isTouchpoint = false;

  float touch_offset[3] = {0.0f, 0.0f, 0.0f};
  float touch_rotation[3] = {0.0f, 0.0f, 0.0f};

  // Per‑mesh highlight override
  bool use_custom_highlight = false;

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
};

struct ImportPreviewData {
  Model imported_model;
  std::vector<ImportAssignment> assignments;
  int selected_mesh_index = -1;
  bool is_open = false;
  std::string save_name = "NewModel";
};

// ----- Existing function declarations -----
bool isFloat(std::string myString);

void loadModel(Model &m, std::string path);

void loadMesh(Mesh &m, std::string path);

// readInfo(...) / writeInfo(...) removed - dead code with zero call sites
// anywhere in the codebase. They implemented the legacy plain-text
// info.txt format, which loadModel()'s own inline fallback reader already
// handles directly, and every save path goes through writeJson() instead.

void loadTexture(GLuint &id, std::string path);

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
              const std::string &globalShaderName = "");

void drawModel(Model &m, GLuint shader, int highlight_mesh_index = -1,
               const glm::vec4 &globalHighlightColor = glm::vec4(1.0f, 0.0f,
                                                                 0.0f, 1.0f),
               const glm::mat4 &view = glm::mat4(1.0f),
               const glm::mat4 &projection = glm::mat4(1.0f),
               const glm::vec3 &cameraPos = glm::vec3(0.0f, 0.0f, 0.0f),
               const std::string &globalShaderName = "");

// ----- functions for custom mesh import and mapping -----
void importModelFile(Model &m, const std::string &filepath);
void applyMeshMapping(Model &m);

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
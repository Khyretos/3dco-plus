#include "shader.h"
#include "settings.h"     // for config_base_path
#include "shaders_data.h" // will be generated
#include "stb_image.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

namespace {
std::unordered_map<std::string, GLuint> g_shader_cache;
std::unordered_map<std::string, std::string> g_embedded_shaders;

// ---- ShaderToy channel textures (iChannel0..3) ----
// Cached per "<shaderName>#<channelIndex>" key. Each entry is either a
// texture loaded from an image file the user (or the shader's own
// unpacked embedded folder) placed in the shader's directory, or - if no
// such file exists - a procedurally generated tileable noise texture.
// This is what actually fixes ShaderToy shaders that sample a noise/
// gradient channel: previously iChannel0-3 were declared as uniforms in
// adaptShaderToy()'s wrapper but nothing ever bound a texture to them,
// so they sampled whatever (or nothing) happened to be left in that
// texture unit.
struct ChannelTexture {
  GLuint id = 0;
  int width = 0;
  int height = 0;
};
std::unordered_map<std::string, ChannelTexture> g_channel_cache;

std::string channelCacheKey(const std::string &shaderName, int channelIndex) {
  return shaderName + "#" + std::to_string(channelIndex);
}

// A small, fast, deterministic hash - not cryptographic, just needs to
// look like noise and be stable across runs so the same shader always
// gets the same generated texture rather than a new random one every
// launch.
unsigned int noiseHash(unsigned int x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

GLuint uploadChannelTexture(const unsigned char *pixels, int w, int h) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
              pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  // Repeat wrapping matches ShaderToy's default channel sampler settings
  // (most noise/gradient channels are authored expecting to tile).
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

// Generates a tileable RGBA8 white-noise texture. This is the "or
// noises" half of making channel loading smart/friendly - a shader that
// expects a noise channel just works out of the box with a plausible
// noise texture instead of failing to render, and the user can still
// drop in their own channel0.png/etc (see getShaderDirectory() callers)
// to override it with real Shadertoy channel art.
ChannelTexture generateNoiseChannelTexture(const std::string &shaderName,
                                           int channelIndex) {
  constexpr int size = 256;
  std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
  unsigned int seed =
      std::hash<std::string>{}(shaderName) * 2654435761u + channelIndex * 97u;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      unsigned int base = seed + static_cast<unsigned int>(y * size + x);
      unsigned char *px = &pixels[(static_cast<size_t>(y) * size + x) * 4];
      px[0] = static_cast<unsigned char>(noiseHash(base * 4u + 0u) & 0xFF);
      px[1] = static_cast<unsigned char>(noiseHash(base * 4u + 1u) & 0xFF);
      px[2] = static_cast<unsigned char>(noiseHash(base * 4u + 2u) & 0xFF);
      px[3] = static_cast<unsigned char>(noiseHash(base * 4u + 3u) & 0xFF);
    }
  }
  ChannelTexture ct;
  ct.id = uploadChannelTexture(pixels.data(), size, size);
  ct.width = size;
  ct.height = size;
  return ct;
}

// Tries to load an actual image file for this channel from the shader's
// own directory - channel0.png/.jpg/.jpeg, channel1.*, etc. - so a
// shader author (or a user who dropped in art via the "Add Resource"
// button next to the shader list, see settings_window.cpp) gets their
// real texture instead of generated noise.
bool tryLoadChannelImageFile(const std::string &shaderName, int channelIndex,
                             ChannelTexture &out) {
  std::string dir = config_base_path + "/shaders/" + shaderName;
  for (const char *ext : {".png", ".jpg", ".jpeg"}) {
    std::string path =
        dir + "/channel" + std::to_string(channelIndex) + ext;
    if (!std::filesystem::exists(path))
      continue;
    int w = 0, h = 0, channels = 0;
    unsigned char *pixels =
        stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
      spdlog::warn("Failed to decode channel image '{}' for shader '{}'",
                   path, shaderName);
      continue;
    }
    out.id = uploadChannelTexture(pixels, w, h);
    out.width = w;
    out.height = h;
    stbi_image_free(pixels);
    return true;
  }
  return false;
}


// Load embedded shaders from the generated header
void loadEmbeddedShaders() {
  if (!g_embedded_shaders.empty())
    return;
  try {
    nlohmann::json data = nlohmann::json::parse(Embedded::shaders_data);
    for (auto &[name, src] : data.items()) {
      if (src.contains("fragment")) {
        g_embedded_shaders[name] = src["fragment"].get<std::string>();
      }
    }
  } catch (...) {
    spdlog::warn("Failed to parse embedded shaders data");
  }
}

// Helper to ensure the shader directory exists
void ensureDirectory(const std::string &path) {
  std::filesystem::create_directories(path);
}

// Unpack all embedded shaders into the user folder (config_base_path/shaders/)
void unpackEmbeddedShaders() {
  loadEmbeddedShaders(); // ensure g_embedded_shaders is populated
  if (g_embedded_shaders.empty())
    return;

  for (const auto &[name, fragSrc] : g_embedded_shaders) {
    std::string shaderDir = config_base_path + "/shaders/" + name;
    std::string fragPath = shaderDir + "/fragment.glsl";

    // Only create if the fragment file doesn't already exist
    if (!std::filesystem::exists(fragPath)) {
      ensureDirectory(shaderDir);
      std::ofstream out(fragPath);
      if (out) {
        out << fragSrc;
        spdlog::info("Unpacked embedded shader '{}' to {}", name, fragPath);
      } else {
        spdlog::warn("Failed to write shader '{}' to {}", name, fragPath);
      }
    }
  }
}

// ---- ShaderToy compatibility ----
std::string adaptShaderToy(const std::string &source) {
  std::string src = source;

  // Remove any #version lines (we will add our own at the end)
  std::istringstream iss(src);
  std::string line;
  std::string cleaned;
  while (std::getline(iss, line)) {
    if (line.rfind("#version", 0) == 0)
      continue;
    cleaned += line + "\n";
  }
  src = cleaned;

  // If the shader already has a main() (e.g., compiled ShaderToy),
  // just prepend the version header
  if (src.find("void main(") != std::string::npos) {
    return "#version 330 core\n" + src;
  }

  // Standard ShaderToy: wrap mainImage
  if (src.find("void mainImage") != std::string::npos) {
    std::string wrapped = "#version 330 core\n";
    wrapped += "out vec4 fragColor;\n";
    // Standard ShaderToy uniforms
    wrapped += "uniform float iTime;\n";
    wrapped += "uniform vec3  iResolution;\n"; // <-- vec3, not vec2
    wrapped += "uniform vec4  iMouse;\n";
    wrapped += "uniform float iTimeDelta;\n";
    wrapped += "uniform int   iFrame;\n";
    wrapped += "uniform float iFrameRate;\n";
    wrapped += "uniform float iChannelTime[4];\n";
    wrapped += "uniform vec3  iChannelResolution[4];\n";
    wrapped += "uniform sampler2D iChannel0;\n";
    wrapped += "uniform sampler2D iChannel1;\n";
    wrapped += "uniform sampler2D iChannel2;\n";
    wrapped += "uniform sampler2D iChannel3;\n";
    // Put the source FIRST so mainImage is defined
    wrapped += src;
    wrapped += "\nvoid main() {\n";
    wrapped += "    mainImage(fragColor, gl_FragCoord.xy);\n";
    wrapped += "}\n";
    return wrapped;
  }

  // Otherwise, it's a regular custom shader – just prepend version
  return "#version 330 core\n" + src;
}

// Get fragment source for a shader name (first from user folder, then embedded)
std::string getShaderFragmentSource(const std::string &name) {
  // 1. Try user folder: config_base_path/shaders/<name>/fragment.glsl
  std::string user_path =
      config_base_path + "/shaders/" + name + "/fragment.glsl";
  if (std::filesystem::exists(user_path)) {
    std::ifstream f(user_path);
    if (f) {
      std::string src((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
      return src;
    }
  }
  // 2. Try embedded
  loadEmbeddedShaders();
  auto it = g_embedded_shaders.find(name);
  if (it != g_embedded_shaders.end()) {
    return it->second;
  }
  return "";
}

// Build a complete fragment shader by combining the default lighting code
// with the custom effect function.
std::string buildCustomFragmentShader(const std::string &customEffect) {
  // We'll prepend the default lighting code (from shaders.cpp) and
  // then append the custom function and a main that calls it.
  // To avoid duplication, we'll use the existing fragment_shader_code
  // but we need to strip out its main and replace with our own.
  // Simpler approach: we'll provide the custom shader as a full
  // replacement – the user must copy the lighting code.
  // For simplicity, we'll return customEffect as-is.
  // The user's shader must be a complete fragment shader.
  return customEffect;
}
} // namespace

// ------------------------------------------------------------------
// Returns (loading/generating and caching as needed) the GL texture to
// bind for iChannel<channelIndex> of the given shader. See the
// ChannelTexture-related helpers above for the file-vs-noise fallback
// logic. Safe to call every frame - actual work only happens once per
// shader+channel combination.
// ------------------------------------------------------------------
GLuint getShaderChannelTexture(const std::string &shaderName,
                               int channelIndex, int *outWidth,
                               int *outHeight) {
  std::string key = channelCacheKey(shaderName, channelIndex);
  auto it = g_channel_cache.find(key);
  if (it == g_channel_cache.end()) {
    ChannelTexture ct;
    if (!tryLoadChannelImageFile(shaderName, channelIndex, ct))
      ct = generateNoiseChannelTexture(shaderName, channelIndex);
    it = g_channel_cache.emplace(key, ct).first;
  }
  if (outWidth)
    *outWidth = it->second.width;
  if (outHeight)
    *outHeight = it->second.height;
  return it->second.id;
}

// Directory a shader's own resource files (fragment.glsl, channelN.png,
// etc.) live in, creating it if necessary. Used by the "Add Resource" button
// in the settings UI to know where to copy a user-picked image to.
std::string getShaderResourceDirectory(const std::string &shaderName) {
  std::string dir = config_base_path + "/shaders/" + shaderName;
  std::filesystem::create_directories(dir);
  return dir;
}

// Drops a shader+channel's cached texture (deleting the GL object) so
// the next getShaderChannelTexture() call picks up a freshly-added or
// replaced channel image file immediately, rather than needing an app
// restart. Called by the "Add Resource" button's handler once it's
// copied a new file into place.
void invalidateShaderChannelCache(const std::string &shaderName,
                                  int channelIndex) {
  std::string key = channelCacheKey(shaderName, channelIndex);
  auto it = g_channel_cache.find(key);
  if (it != g_channel_cache.end()) {
    if (it->second.id)
      glDeleteTextures(1, &it->second.id);
    g_channel_cache.erase(it);
  }
}

// ------------------------------------------------------------------
// Inverted-hull outline shader (silhouette only)
// ------------------------------------------------------------------
const char *outline_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float outlineSize;

void main()
{
    // Inflate along the normal
    vec3 inflatedPos = aPos + aNormal * outlineSize;
    vec4 worldPos = model * vec4(inflatedPos, 1.0);
    gl_Position = projection * view * worldPos;
}
)";

const char *outline_fragment_shader = R"(
#version 330 core
out vec4 FragColor;
void main()
{
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

GLuint g_outline_program = 0;

GLuint getOutlineProgram() {
  if (g_outline_program == 0) {
    g_outline_program =
        CreateShaderProgram(outline_vertex_shader, outline_fragment_shader);
  }
  return g_outline_program;
}

const char *wireframe_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char *wireframe_fragment_shader = R"(
#version 330 core
out vec4 FragColor;
void main()
{
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

GLuint g_wireframe_program = 0;

GLuint getWireframeProgram() {
  if (g_wireframe_program == 0) {
    g_wireframe_program =
        CreateShaderProgram(wireframe_vertex_shader, wireframe_fragment_shader);
  }
  return g_wireframe_program;
}

GLuint LoadShaderProgram(const std::string &name) {
  unpackEmbeddedShaders();
  if (name.empty())
    return 0; // default shader
  // Check cache
  auto it = g_shader_cache.find(name);
  if (it != g_shader_cache.end() && it->second != 0) {
    // Check if program is still valid? We'll assume it is.
    return it->second;
  }

  std::string fragSrc = getShaderFragmentSource(name);
  if (fragSrc.empty()) {
    spdlog::warn("Shader '{}' not found", name);
    return 0;
  }

  fragSrc = adaptShaderToy(fragSrc);

  // Use the default vertex shader (from shaders.cpp)
  extern const std::string vertex_shader_code;
  GLuint prog =
      CreateShaderProgram(vertex_shader_code.c_str(), fragSrc.c_str());
  if (prog == 0) {
    spdlog::error("Failed to compile shader '{}'", name);
    return 0;
  }

  g_shader_cache[name] = prog;
  return prog;
}

void ReloadShaderPrograms() {
  // Delete all cached programs and clear the map
  for (auto &[name, prog] : g_shader_cache) {
    if (prog)
      glDeleteProgram(prog);
  }
  g_shader_cache.clear();
  // Embedded shaders are already loaded; no need to reload them.
}

GLuint CompileShader(GLuint type, const char *shaderSource) {
  GLuint shaderObject;

  if (type == GL_VERTEX_SHADER) {
    shaderObject = glCreateShader(GL_VERTEX_SHADER);
  } else if (type == GL_FRAGMENT_SHADER) {
    shaderObject = glCreateShader(GL_FRAGMENT_SHADER);
  } else {
    return 1;
  }

  glShaderSource(shaderObject, 1, &shaderSource, NULL);
  glCompileShader(shaderObject);

  int result;
  glGetShaderiv(shaderObject, GL_COMPILE_STATUS, &result);

  if (result == GL_FALSE) {
    int length;
    glGetShaderiv(shaderObject, GL_INFO_LOG_LENGTH, &length);
    char errorMessages[length];
    glGetShaderInfoLog(shaderObject, length, &length, errorMessages);

    if (type == GL_VERTEX_SHADER) {
      printf("ERROR: GL_VERTEX_SHADER compilation failed!\n%s", errorMessages);
    } else if (type == GL_FRAGMENT_SHADER) {
      printf("ERROR: GL_FRAGMENT_SHADER compilation failed!\n%s",
             errorMessages);
    }

    glDeleteShader(shaderObject);

    return 0;
  }

  return shaderObject;
}

GLuint CreateShaderProgram(const char *vertexShaderSource,
                           const char *fragmentShaderSource) {
  GLuint programObject = glCreateProgram();

  GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
  GLuint fragmentShader =
      CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

  glAttachShader(programObject, vertexShader);
  glAttachShader(programObject, fragmentShader);
  glLinkProgram(programObject);

  int result = 0;
  glGetProgramiv(programObject, GL_LINK_STATUS, &result);

  if (result == GL_FALSE) {
    int length;
    glGetProgramiv(programObject, GL_INFO_LOG_LENGTH, &length);
    char errorMessages[length];
    glGetProgramInfoLog(programObject, length, &length, errorMessages);

    printf("ERROR: Shader Program linking failed! : %s\n", errorMessages);

    // Delete the invalid program and return 0
    glDeleteProgram(programObject);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return 0;
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return programObject;
}

std::string GetShaderSource(std::string path) {
  const char *base_path = SDL_GetBasePath();
  std::filesystem::path file_path;
  file_path = std::filesystem::path(base_path);
  std::filesystem::path sub_path(path);
  file_path /= sub_path;

  std::ifstream ifs;
  std::string shader_source;

  ifs = std::ifstream(file_path);
  if (!ifs) {
    printf("Uh oh, file could not be opened for reading!\n");
  } else {
    while (ifs) {
      std::string line;
      std::getline(ifs, line);
      shader_source.append(line);
      shader_source.append("\n");
    }
    // printf(shader_source.c_str());
  }

  return shader_source;
}

std::vector<std::string> GetShaderNames() {
  unpackEmbeddedShaders(); // ensure user folder is populated

  std::vector<std::string> names;
  names.push_back("None");

  // Add user shaders from config_base_path/shaders/
  std::string user_dir = config_base_path + "/shaders";
  if (std::filesystem::exists(user_dir) &&
      std::filesystem::is_directory(user_dir)) {
    for (const auto &entry : std::filesystem::directory_iterator(user_dir)) {
      if (entry.is_directory()) {
        std::string name = entry.path().filename().string();
        std::string frag = entry.path().string() + "/fragment.glsl";
        if (std::filesystem::exists(frag)) {
          names.push_back(name);
        }
      }
    }
  }
  return names;
}

void shaderUniformBool(GLuint ID, const char *name, bool value) {
  glUniform1i(glGetUniformLocation(ID, name), (int)value);
}

void shaderUniformInt(GLuint ID, const char *name, int value) {
  glUniform1i(glGetUniformLocation(ID, name), value);
}

void shaderUniformFloat(GLuint ID, const char *name, float value) {
  glUniform1f(glGetUniformLocation(ID, name), value);
}

void shaderUniformMat4(GLuint ID, const char *name, glm::mat4 mat) {
  glUniformMatrix4fv(glGetUniformLocation(ID, name), 1, false,
                     glm::value_ptr(mat));
}

void shaderUniformMat3(GLuint ID, const char *name, glm::mat3 mat) {
  glUniformMatrix3fv(glGetUniformLocation(ID, name), 1, false,
                     glm::value_ptr(mat));
}

void shaderUniformVec3(GLuint ID, const char *name, glm::vec3 vec) {

  glUniform3fv(glGetUniformLocation(ID, name), 1, &vec[0]);
}

void shaderUniformVec4(GLuint ID, const char *name, glm::vec4 vec) {

  glUniform4fv(glGetUniformLocation(ID, name), 1, &vec[0]);
}

void shaderUniform2f(GLuint ID, const char *name, float value1, float value2) {
  glUniform2f(glGetUniformLocation(ID, name), value1, value2);
}

void shaderUniform3f(GLuint ID, const char *name, float value1, float value2,
                     float value3) {
  glUniform3f(glGetUniformLocation(ID, name), value1, value2, value3);
}

void shaderUniform4f(GLuint ID, const char *name, float value1, float value2,
                     float value3, float value4) {
  glUniform4f(glGetUniformLocation(ID, name), value1, value2, value3, value4);
}
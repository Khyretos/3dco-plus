#include "shader.h"
#include "settings.h"     // for config_base_path
#include "shaders_data.h" // will be generated
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace {
std::unordered_map<std::string, GLuint> g_shader_cache;
std::unordered_map<std::string, std::string> g_embedded_shaders;

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
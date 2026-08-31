#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <SDL3/SDL.h>
#include <vector>

#include <string>

GLuint CompileShader(GLuint type, const char *shaderSource);

GLuint CreateShaderProgram(const char *vertexShaderSource,
                           const char *fragmentShaderSource);

std::string GetShaderSource(std::string path);

void shaderUniformBool(GLuint ID, const char *name, bool value);

void shaderUniformInt(GLuint ID, const char *name, int value);

void shaderUniformFloat(GLuint ID, const char *name, float value);

void shaderUniformMat4(GLuint ID, const char *name, glm::mat4 mat);

void shaderUniformMat3(GLuint ID, const char *name, glm::mat3 mat);

void shaderUniformVec3(GLuint ID, const char *name, glm::vec3 vec);

void shaderUniformVec4(GLuint ID, const char *name, glm::vec4 vec);

void shaderUniform2f(GLuint ID, const char *name, float value1, float value2);

void shaderUniform3f(GLuint ID, const char *name, float value1, float value2,
                     float value3);

void shaderUniform4f(GLuint ID, const char *name, float value1, float value2,
                     float value3, float value4);

// Shader caching and loading
GLuint LoadShaderProgram(const std::string &name);
void ReloadShaderPrograms(); // for hot-reload (optional)

std::vector<std::string> GetShaderNames();

GLuint getOutlineProgram();

GLuint getWireframeProgram();

// ShaderToy-style channel textures (iChannel0..3). See shader.cpp for
// the file-vs-generated-noise fallback. outWidth/outHeight (optional)
// receive the texture's dimensions, for iChannelResolution.
GLuint getShaderChannelTexture(const std::string &shaderName,
                               int channelIndex, int *outWidth = nullptr,
                               int *outHeight = nullptr);

// Directory a shader's own resource files (fragment.glsl, channelN.png,
// etc.) live in, creating it if needed. Used by the settings UI's "Add
// Resource" button to know where to copy a user-picked image.
std::string getShaderResourceDirectory(const std::string &shaderName);

// Clears the cached texture for one shader+channel (e.g. after the "Add
// Resource" button copies in a new file), so the change takes effect on
// the next frame instead of needing an app restart.
void invalidateShaderChannelCache(const std::string &shaderName,
                                  int channelIndex);

#endif

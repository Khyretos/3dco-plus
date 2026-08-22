#include "model.h"
#include "shader.h"
#include "stb_image.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
using json = nlohmann::json;
#include <iomanip> // for std::fixed, std::setprecision

#include "strings.h"

static std::string escapeJson(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

std::string model_filenames[35] = {
    "top_shell.obj",    "bottom_shell.obj",  "extra.obj",
    "left_trigger.obj", "right_trigger.obj", "left_stick.obj",
    "right_stick.obj",  "left_ring.obj",     "right_ring.obj",
    "a_button.obj",     "b_button.obj",      "x_button.obj",
    "y_button.obj",     "back_button.obj",   "guide_button.obj",
    "start_button.obj", "left_cap.obj",      "right_cap.obj",
    "left_bumper.obj",  "right_bumper.obj",  "dpad_up.obj",
    "dpad_down.obj",    "dpad_left.obj",     "dpad_right.obj",
    "misc.obj",         "paddle1.obj",       "paddle2.obj",
    "paddle3.obj",      "paddle4.obj",       "touchpad.obj",
    "touch_point1.obj", "touch_point2.obj",  "touchpad2.obj",
    "touch_point3.obj", "touch_point4.obj"};

void writeJson(Model &m, const std::string &path) {
  std::ofstream json(path);
  if (!json) {
    spdlog::error("Failed to write JSON to {}", path);
    return;
  }

  if (m.meshes.empty()) {
    spdlog::warn("Attempted to write empty model to {} – skipping.", path);
    return;
  }

  json << std::fixed << std::setprecision(6);
  json << "{\n  \"parts\": [\n";
  for (size_t i = 0; i < m.meshes.size(); ++i) {
    const Mesh &mesh = m.meshes[i];
    json << "    {\n";
    json << "      \"filename\": \"" << escapeJson(mesh.filename) << "\",\n";
    json << "      \"name\": \"" << escapeJson(mesh.name) << "\",\n";
    json << "      \"assigned_part\": " << mesh.assignedPart << ",\n";
    json << "      \"position\": [" << mesh.position[0] << ", "
         << mesh.position[1] << ", " << mesh.position[2] << "],\n";
    json << "      \"travel\": [" << mesh.travel[0] << ", " << mesh.travel[1]
         << ", " << mesh.travel[2] << "],\n";
    json << "      \"popup_offset\": [" << mesh.popup_offset[0] << ", "
         << mesh.popup_offset[1] << ", " << mesh.popup_offset[2] << "],\n";
    json << "      \"popup_rotation\": [" << mesh.popup_rotation[0] << ", "
         << mesh.popup_rotation[1] << ", " << mesh.popup_rotation[2] << "],\n";
    json << "      \"trigger_max\": " << mesh.trigger_max << ",\n";
    json << "      \"stick_max\": " << mesh.stick_max << ",\n";
    json << "      \"touch_width\": " << mesh.touch_width << ",\n";
    json << "      \"touch_height\": " << mesh.touch_height << ",\n";
    json << "      \"touch_offset\": [" << mesh.touch_offset[0] << ", "
         << mesh.touch_offset[1] << ", " << mesh.touch_offset[2] << "],\n";
    json << "      \"touch_rotation\": [" << mesh.touch_rotation[0] << ", "
         << mesh.touch_rotation[1] << ", " << mesh.touch_rotation[2] << "],\n";
    json << "      \"pivot_offset\": [" << mesh.pivot_offset[0] << ", "
         << mesh.pivot_offset[1] << ", " << mesh.pivot_offset[2] << "],\n";
    json << "      \"rotation\": [" << mesh.rotation[0] << ", "
         << mesh.rotation[1] << ", " << mesh.rotation[2] << "],\n";
    json << "      \"parent\": " << mesh.parentIndex << ",\n";
    json << "      \"input_type\": " << mesh.inputType << ",\n";
    json << "      \"input_binding\": \"" << escapeJson(mesh.inputBinding)
         << "\",\n";
    json << "      \"invert\": " << (mesh.invert ? "true" : "false") << ",\n";
    json << "      \"isTouchpad\": " << (mesh.isTouchpad ? "true" : "false")
         << ",\n";
    json << "      \"isTouchpoint\": " << (mesh.isTouchpoint ? "true" : "false")
         << ",\n";
    json << "      \"isBumper\": " << (mesh.isBumper ? "true" : "false")
         << ",\n";
    json << "      \"isTrigger\": " << (mesh.isTrigger ? "true" : "false")
         << ",\n";
    json << "      \"isPaddle\": " << (mesh.isPaddle ? "true" : "false")
         << ",\n";
    json << "      \"ambient\": " << mesh.material.ambient << ",\n";
    json << "      \"diffuse\": " << mesh.material.diffuse << ",\n";
    json << "      \"specular\": " << mesh.material.specular << ",\n";
    json << "      \"shininess\": " << mesh.material.shininess << ",\n";
    json << "      \"color\": [" << mesh.material.color[0] << ", "
         << mesh.material.color[1] << ", " << mesh.material.color[2] << "],\n";
    json << "      \"alpha\": " << mesh.material.alpha << ",\n";
    json << "      \"use_custom_highlight\": "
         << (mesh.use_custom_highlight ? "true" : "false") << ",\n";
    json << "      \"custom_highlight_color\": ["
         << mesh.custom_highlight_color[0] << ", "
         << mesh.custom_highlight_color[1] << ", "
         << mesh.custom_highlight_color[2] << ", "
         << mesh.custom_highlight_color[3] << "],\n";
    json << "      \"travel_rotation\": [" << mesh.travel_rotation[0] << ", "
         << mesh.travel_rotation[1] << ", " << mesh.travel_rotation[2]
         << "],\n";
    json << "      \"use_dual_highlight\": "
         << (mesh.use_dual_highlight ? "true" : "false") << ",\n";
    json << "      \"axis_deadzone\": " << mesh.axis_deadzone << ",\n";
    json << "      \"highlight_color_positive\": ["
         << mesh.highlight_color_positive[0] << ", "
         << mesh.highlight_color_positive[1] << ", "
         << mesh.highlight_color_positive[2] << ", "
         << mesh.highlight_color_positive[3] << "],\n";
    json << "      \"highlight_color_negative\": ["
         << mesh.highlight_color_negative[0] << ", "
         << mesh.highlight_color_negative[1] << ", "
         << mesh.highlight_color_negative[2] << ", "
         << mesh.highlight_color_negative[3] << "]\n";
    json << "    }" << (i < m.meshes.size() - 1 ? "," : "") << "\n";
  }
  json << "  ],\n";
  json << "  \"source\": \"" << escapeJson(m.source) << "\"\n";
  json << "}\n";
}

void readInfoJson(Model &m, const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return;
  json data;
  try {
    f >> data;
  } catch (...) {
    spdlog::warn("Failed to parse JSON: {}", path);
    return;
  }

  if (!data.contains("parts") || !data["parts"].is_array())
    return;
  auto &parts = data["parts"];
  m.meshes.clear();
  m.meshes.reserve(parts.size());

  for (auto &p : parts) {
    Mesh mesh;
    // Load OBJ file
    std::string filename = p.value("filename", "");
    mesh.filename = filename;
    if (filename.empty())
      continue;
    std::string objPath = m.path + "/" + filename;
    loadMesh(mesh, objPath);
    if (mesh.elements == 0) {
      spdlog::warn("Failed to load OBJ: {}", objPath);
      continue;
    }

    // Read properties
    if (p.contains("position")) {
      auto arr = p["position"].get<std::array<float, 3>>();
      mesh.position[0] = arr[0];
      mesh.position[1] = arr[1];
      mesh.position[2] = arr[2];
    }
    if (p.contains("travel")) {
      auto arr = p["travel"].get<std::array<float, 3>>();
      mesh.travel[0] = arr[0];
      mesh.travel[1] = arr[1];
      mesh.travel[2] = arr[2];
    }
    if (p.contains("popup_offset")) {
      auto arr = p["popup_offset"].get<std::array<float, 3>>();
      mesh.popup_offset[0] = arr[0];
      mesh.popup_offset[1] = arr[1];
      mesh.popup_offset[2] = arr[2];
    }
    if (p.contains("popup_rotation")) {
      auto arr = p["popup_rotation"].get<std::array<float, 3>>();
      mesh.popup_rotation[0] = arr[0];
      mesh.popup_rotation[1] = arr[1];
      mesh.popup_rotation[2] = arr[2];
    }
    if (p.contains("trigger_max"))
      mesh.trigger_max = p["trigger_max"].get<float>();
    if (p.contains("stick_max"))
      mesh.stick_max = p["stick_max"].get<float>();
    if (p.contains("touch_width"))
      mesh.touch_width = p["touch_width"].get<float>();
    if (p.contains("touch_height"))
      mesh.touch_height = p["touch_height"].get<float>();
    if (p.contains("touch_offset")) {
      auto arr = p["touch_offset"].get<std::array<float, 3>>();
      mesh.touch_offset[0] = arr[0];
      mesh.touch_offset[1] = arr[1];
      mesh.touch_offset[2] = arr[2];
    }
    if (p.contains("touch_rotation")) {
      auto arr = p["touch_rotation"].get<std::array<float, 3>>();
      mesh.touch_rotation[0] = arr[0];
      mesh.touch_rotation[1] = arr[1];
      mesh.touch_rotation[2] = arr[2];
    }
    if (p.contains("pivot_offset")) {
      auto arr = p["pivot_offset"].get<std::array<float, 3>>();
      mesh.pivot_offset[0] = arr[0];
      mesh.pivot_offset[1] = arr[1];
      mesh.pivot_offset[2] = arr[2];
    }
    if (p.contains("rotation")) {
      auto arr = p["rotation"].get<std::array<float, 3>>();
      mesh.rotation[0] = arr[0];
      mesh.rotation[1] = arr[1];
      mesh.rotation[2] = arr[2];
    }
    if (p.contains("parent"))
      mesh.parentIndex = p["parent"].get<int>();
    if (p.contains("input_type")) {
      mesh.inputType = p["input_type"].get<int>();
    } else if (p.contains("use_joystick")) {
      // Legacy: map use_joystick to inputType
      bool useRaw = p["use_joystick"].get<bool>();
      mesh.inputType = useRaw ? INPUT_TYPE_JOYSTICK : INPUT_TYPE_GAMEPAD;
    } else {
      mesh.inputType = INPUT_TYPE_GAMEPAD; // default
    }
    if (p.contains("input_binding"))
      mesh.inputBinding = p["input_binding"].get<std::string>();
    if (p.contains("invert"))
      mesh.invert = p["invert"].get<bool>();
    if (p.contains("isTouchpad"))
      mesh.isTouchpad = p["isTouchpad"].get<bool>();
    else
      mesh.isTouchpad = false; // default
    if (p.contains("isTouchpoint"))
      mesh.isTouchpoint = p["isTouchpoint"].get<bool>();
    else
      mesh.isTouchpoint = false;
    if (p.contains("isBumper"))
      mesh.isBumper = p["isBumper"].get<bool>();
    if (p.contains("isTrigger"))
      mesh.isTrigger = p["isTrigger"].get<bool>();
    if (p.contains("isPaddle"))
      mesh.isPaddle = p["isPaddle"].get<bool>();
    if (p.contains("assigned_part"))
      mesh.assignedPart = p["assigned_part"].get<int>();
    mesh.name = p.value("name", filename);

    // Read material properties – top‑level (new format) with fallback to nested
    // "material" (legacy)
    auto getFloat = [&](const std::string &key, float &dest) {
      if (p.contains(key)) {
        dest = p[key].get<float>();
      } else if (p.contains("material") && p["material"].contains(key)) {
        dest = p["material"][key].get<float>();
      }
    };
    auto getColor = [&](const std::string &key, float dest[3]) {
      if (p.contains(key)) {
        auto arr = p[key].get<std::array<float, 3>>();
        dest[0] = arr[0];
        dest[1] = arr[1];
        dest[2] = arr[2];
      } else if (p.contains("material") && p["material"].contains(key)) {
        auto arr = p["material"][key].get<std::array<float, 3>>();
        dest[0] = arr[0];
        dest[1] = arr[1];
        dest[2] = arr[2];
      }
    };
    getFloat("ambient", mesh.material.ambient);
    getFloat("diffuse", mesh.material.diffuse);
    getFloat("specular", mesh.material.specular);
    getFloat("shininess", mesh.material.shininess);
    getColor("color", mesh.material.color);
    getFloat("alpha", mesh.material.alpha);

    // per-mesh highlight override
    if (p.contains("travel_rotation")) {
      auto arr = p["travel_rotation"].get<std::array<float, 3>>();
      mesh.travel_rotation[0] = arr[0];
      mesh.travel_rotation[1] = arr[1];
      mesh.travel_rotation[2] = arr[2];
    }
    mesh.use_dual_highlight = p.value("use_dual_highlight", false);
    if (p.contains("highlight_color_positive")) {
      auto arr = p["highlight_color_positive"].get<std::array<float, 3>>();
      mesh.highlight_color_positive[0] = arr[0];
      mesh.highlight_color_positive[1] = arr[1];
      mesh.highlight_color_positive[2] = arr[2];
    }
    if (p.contains("highlight_color_negative")) {
      auto arr = p["highlight_color_negative"].get<std::array<float, 3>>();
      mesh.highlight_color_negative[0] = arr[0];
      mesh.highlight_color_negative[1] = arr[1];
      mesh.highlight_color_negative[2] = arr[2];
    }
    if (p.contains("axis_deadzone")) {
      mesh.axis_deadzone = p["axis_deadzone"].get<float>();
    }

    // After setting mesh.material.color (possibly from JSON)
    mesh.original_color[0] = mesh.material.color[0];
    mesh.original_color[1] = mesh.material.color[1];
    mesh.original_color[2] = mesh.material.color[2];
    mesh.original_alpha = mesh.material.alpha;

    m.meshes.push_back(std::move(mesh));
  }

  if (data.contains("source"))
    m.source = data["source"].get<std::string>();

  // ---- Sanitize parentIndex ----
  // parentIndex is read directly from the file above with no validation.
  // It can be invalid for two reasons: (1) it was written by older code
  // that stored a controller-part number instead of a real mesh-vector
  // index, or (2) any earlier part in the file failed to load (missing/
  // empty filename, bad OBJ) and was skipped via `continue` above, which
  // shifts every subsequent mesh's real position in m.meshes relative to
  // what was recorded when the file was written. Either way, an
  // out-of-range or cyclic parentIndex will crash the first time something
  // walks the parent chain (e.g. getTouchpadAncestor), so clamp it here
  // instead of trusting the file.
  for (size_t i = 0; i < m.meshes.size(); ++i) {
    int p = m.meshes[i].parentIndex;
    if (p == -1)
      continue;
    if (p < 0 || p >= (int)m.meshes.size() || p == (int)i) {
      spdlog::warn("Mesh '{}' had an invalid parent index {} in {}; "
                   "clearing it.",
                   m.meshes[i].name, p, path);
      m.meshes[i].parentIndex = -1;
      continue;
    }
  }
  // Second pass: break any cycles that are individually valid indices but
  // form a loop (A parents to B, B parents to A, etc.) - walk each chain
  // with a visited set and cut it at the point it would revisit a node.
  for (size_t i = 0; i < m.meshes.size(); ++i) {
    std::vector<bool> visited(m.meshes.size(), false);
    int current = (int)i;
    visited[current] = true;
    int next = m.meshes[current].parentIndex;
    while (next != -1) {
      if (next < 0 || next >= (int)m.meshes.size() || visited[next]) {
        spdlog::warn("Mesh '{}' had a cyclic parent chain in {}; "
                     "clearing its parent.",
                     m.meshes[i].name, path);
        m.meshes[i].parentIndex = -1;
        break;
      }
      visited[next] = true;
      current = next;
      next = m.meshes[current].parentIndex;
    }
  }
}

// ------------------------------------------------------------------
// LEGACY OBJ LOADER (32 meshes from folder)
// ------------------------------------------------------------------

void loadModel(Model &m, std::string path) {
  m.path = path;
  m.meshes.clear();

  // 1. Try to read info.json first
  std::string jsonPath = path + "/info.json";
  if (std::filesystem::exists(jsonPath)) {
    readInfoJson(m, jsonPath);
    spdlog::info("Loaded model from JSON: {}", path);
    return;
  }

  // 2. Fallback to info.txt (legacy)
  std::string txtPath = path + "/info.txt";
  if (std::filesystem::exists(txtPath)) {
    // Read legacy info.txt (35 entries)
    std::ifstream info_file(txtPath);
    if (!info_file) {
      spdlog::warn("Info file not found: {}", txtPath);
      return;
    }

    m.meshes.resize(35); // legacy fixed size

    std::string line;
    int meshIdx = 0;
    while (std::getline(info_file, line)) {
      if (line.empty())
        continue;
      if (meshIdx >= 35)
        break; // ignore extra

      // Skip the filename line (we already have it)
      // Read 16 numbers
      float vals[16];
      int count = 0;
      while (count < 16 && std::getline(info_file, line)) {
        if (line.empty())
          continue;
        try {
          vals[count] = std::stof(line);
        } catch (...) {
          vals[count] = 0.0f;
        }
        count++;
      }
      if (count < 16) {
        spdlog::warn("Info.txt ended early for mesh {}", meshIdx);
        break;
      }

      Mesh &mesh = m.meshes[meshIdx];
      mesh.position[0] = vals[0];
      mesh.position[1] = vals[1];
      mesh.position[2] = vals[2];
      mesh.travel[0] = vals[3];
      mesh.travel[1] = vals[4];
      mesh.travel[2] = vals[5];
      mesh.popup_offset[0] = vals[6];
      mesh.popup_offset[1] = vals[7];
      mesh.popup_offset[2] = vals[8];
      mesh.popup_rotation[0] = vals[9];
      mesh.popup_rotation[1] = vals[10];
      mesh.popup_rotation[2] = vals[11];
      mesh.trigger_max = vals[12];
      mesh.stick_max = vals[13];
      mesh.touch_width = vals[14];
      mesh.touch_height = vals[15];

      // Load the OBJ file
      std::string objPath = path + "/" + model_filenames[meshIdx];
      mesh.filename = model_filenames[meshIdx];
      loadMesh(mesh, objPath);
      // Set assignedPart to the index (legacy: part index = mesh index)
      mesh.assignedPart = meshIdx;
      mesh.name = mesh_names[meshIdx]; // use the fixed name from arrays

      mesh.original_color[0] = mesh.material.color[0];
      mesh.original_color[1] = mesh.material.color[1];
      mesh.original_color[2] = mesh.material.color[2];
      mesh.original_alpha = mesh.material.alpha;

      meshIdx++;
    }

    // Convert to JSON for future use
    writeJson(m, jsonPath);
    spdlog::info("Converted legacy info.txt to JSON for model at '{}'.", path);
    return;
  }

  // 3. No info file – create an empty model (no meshes)
  spdlog::warn("No info file found for model at '{}'.", path);
}

bool isFloat(std::string myString) {
  std::istringstream iss(myString);
  float f;
  iss >> std::noskipws >> f;
  return iss.eof() && !iss.fail();
}

void loadMesh(Mesh &m, std::string path) {
  std::ifstream ifs = std::ifstream(path);
  if (!ifs.is_open()) {
    spdlog::warn("Could not open mesh file: {}", path);
    m.elements = 0;
    m.vao = 0;
    m.vbo = 0;
    m.ebo = 0;
    return;
  }

  std::vector<vertex_position> positions;
  std::vector<vertex_normal> normals;
  std::vector<vertex_texcoord> texcoords;
  std::vector<Vertex> vertices;
  std::vector<int> indices;

  while (ifs) {
    std::vector<std::string> words;
    std::string line;
    std::string word;

    std::getline(ifs, line);
    std::stringstream line_stream(line);

    while (std::getline(line_stream, word, ' ')) {
      if (!word.empty())
        words.push_back(word);
    }

    if (words.size() > 3 && words[0] == "v") {
      vertex_position pos;
      try {
        pos.x = std::stof(words[1]);
        pos.y = std::stof(words[2]);
        pos.z = std::stof(words[3]);
      } catch (...) {
        spdlog::warn("Invalid vertex data in {}", path);
        continue;
      }
      positions.push_back(pos);
    }

    if (words.size() > 3 && words[0] == "vn") {
      vertex_normal norm;
      try {
        norm.x = std::stof(words[1]);
        norm.y = std::stof(words[2]);
        norm.z = std::stof(words[3]);
      } catch (...) {
        spdlog::warn("Invalid normal data in {}", path);
        continue;
      }
      normals.push_back(norm);
    }

    if (words.size() > 2 && words[0] == "vt") {
      vertex_texcoord tex;
      try {
        tex.x = std::stof(words[1]);
        tex.y = std::stof(words[2]);
      } catch (...) {
        spdlog::warn("Invalid texcoord data in {}", path);
        continue;
      }
      texcoords.push_back(tex);
    }

    if (words.size() > 3 && words[0] == "f") {
      // Parse all vertex indices for this face
      std::vector<int> posIndices, texIndices, normIndices;

      for (unsigned long i = 1; i < words.size(); i++) {
        std::vector<int> ind;
        std::string value;
        std::stringstream word_stream(words[i]);
        while (std::getline(word_stream, value, '/')) {
          if (value.empty()) {
            ind.push_back(-1);
          } else {
            try {
              ind.push_back(std::stoi(value) - 1);
            } catch (...) {
              ind.push_back(-1);
            }
          }
        }

        // Store indices (position, texture, normal)
        int posIdx =
            (ind.size() > 0 && ind[0] >= 0 && ind[0] < (int)positions.size())
                ? ind[0]
                : -1;
        int texIdx =
            (ind.size() > 1 && ind[1] >= 0 && ind[1] < (int)texcoords.size())
                ? ind[1]
                : -1;
        int normIdx =
            (ind.size() > 2 && ind[2] >= 0 && ind[2] < (int)normals.size())
                ? ind[2]
                : -1;

        // Skip invalid faces
        if (posIdx < 0)
          continue;

        posIndices.push_back(posIdx);
        texIndices.push_back(texIdx);
        normIndices.push_back(normIdx);
      }

      // Need at least 3 vertices for a face
      if (posIndices.size() < 3)
        continue;

      // Triangulate - handle quads, ngons, etc.
      for (size_t i = 1; i < posIndices.size() - 1; ++i) {
        // Create triangle: 0, i, i+1
        Vertex v0, v1, v2;

        // First vertex (always the first vertex of the face)
        v0.position = posIndices[0];
        v0.texcoord = texIndices[0];
        v0.normal = normIndices[0];

        // Second vertex (i)
        v1.position = posIndices[i];
        v1.texcoord = texIndices[i];
        v1.normal = normIndices[i];

        // Third vertex (i+1)
        v2.position = posIndices[i + 1];
        v2.texcoord = texIndices[i + 1];
        v2.normal = normIndices[i + 1];

        // Add the triangle
        int baseIndex = vertices.size();
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        indices.push_back(baseIndex);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
      }
    }
  }

  if (vertices.empty() || indices.empty()) {
    spdlog::warn("No valid geometry in {}", path);
    m.elements = 0;
    return;
  }

  // After reading all vertices, compute bounding box
  if (!positions.empty()) {
    m.hasBBox = true;
    m.bboxMin = glm::vec3(FLT_MAX);
    m.bboxMax = glm::vec3(-FLT_MAX);
    for (const auto &v : positions) {
      m.bboxMin.x = std::min(m.bboxMin.x, v.x);
      m.bboxMin.y = std::min(m.bboxMin.y, v.y);
      m.bboxMin.z = std::min(m.bboxMin.z, v.z);
      m.bboxMax.x = std::max(m.bboxMax.x, v.x);
      m.bboxMax.y = std::max(m.bboxMax.y, v.y);
      m.bboxMax.z = std::max(m.bboxMax.z, v.z);
    }
  } else {
    m.hasBBox = false;
  }

  GLfloat vertex_data[vertices.size() * 8];
  for (unsigned long i = 0; i < vertices.size(); i++) {
    int pos_idx = vertices[i].position;
    int norm_idx = vertices[i].normal;
    int tex_idx = vertices[i].texcoord;
    if (pos_idx >= 0 && pos_idx < (int)positions.size()) {
      vertex_data[0 + (8 * i)] = positions[pos_idx].x;
      vertex_data[1 + (8 * i)] = positions[pos_idx].y;
      vertex_data[2 + (8 * i)] = positions[pos_idx].z;
    } else {
      vertex_data[0 + (8 * i)] = 0.0f;
      vertex_data[1 + (8 * i)] = 0.0f;
      vertex_data[2 + (8 * i)] = 0.0f;
    }
    if (norm_idx >= 0 && norm_idx < (int)normals.size()) {
      vertex_data[3 + (8 * i)] = normals[norm_idx].x;
      vertex_data[4 + (8 * i)] = normals[norm_idx].y;
      vertex_data[5 + (8 * i)] = normals[norm_idx].z;
    } else {
      vertex_data[3 + (8 * i)] = 0.0f;
      vertex_data[4 + (8 * i)] = 1.0f;
      vertex_data[5 + (8 * i)] = 0.0f;
    }
    if (tex_idx >= 0 && tex_idx < (int)texcoords.size()) {
      vertex_data[6 + (8 * i)] = texcoords[tex_idx].x;
      vertex_data[7 + (8 * i)] = texcoords[tex_idx].y;
    } else {
      vertex_data[6 + (8 * i)] = 0.0f;
      vertex_data[7 + (8 * i)] = 0.0f;
    }
  }

  m.elements = indices.size();
  GLuint index_data[m.elements];
  for (unsigned long i = 0; i < m.elements; i++) {
    index_data[i] = indices[i];
  }

  glGenVertexArrays(1, &m.vao);
  glGenBuffers(1, &m.vbo);
  glBindVertexArray(m.vao);

  glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat),
                        (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat),
                        (void *)(3 * sizeof(GLfloat)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat),
                        (void *)(6 * sizeof(GLfloat)));
  glEnableVertexAttribArray(2);

  glGenBuffers(1, &m.ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_data), index_data,
               GL_STATIC_DRAW);

  glBindVertexArray(0);
}

void deleteTexture(GLuint &id) {
  glDeleteTextures(1, &id);
  id = 0;
}

void loadTexture(GLuint &id, std::string path) {
  if (id == 0)
    glGenTextures(1, &id);

  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  int width, height, nrChannels;
  unsigned char *data =
      stbi_load(path.c_str(), &width, &height, &nrChannels, 0);

  GLenum format;
  if (nrChannels == 1)
    format = GL_RED;
  else if (nrChannels == 3)
    format = GL_RGB;
  else if (nrChannels == 4)
    format = GL_RGBA;
  else {
    spdlog::error("Unknown channel count {} for texture {}", nrChannels, path);
    return;
  }

  if (data) {
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format,
                 GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
  } else {
    spdlog::error("Failed to load texture: {}", path);
  }
  stbi_image_free(data);
}

void drawMesh(const Mesh &mesh, const glm::mat4 &modelMatrix, GLuint shader,
              const glm::vec4 &highlightColor,
              const glm::vec3 *baseColorOverride) {
  if (!mesh.vao || mesh.elements == 0)
    return;

  glBindVertexArray(mesh.vao);

  shaderUniformInt(shader, "num_textures", mesh.textures.size());
  for (size_t i = 0; i < mesh.textures.size(); i++) {
    std::string name = "textures[";
    name.append(std::to_string(i));
    name.append("]");
    shaderUniformInt(shader, std::string(name).append(".id").c_str(), i);
    shaderUniformInt(shader, std::string(name).append(".type").c_str(),
                     mesh.textures[i].type);
    shaderUniformFloat(shader, std::string(name).append(".offsetX").c_str(),
                       mesh.textures[i].offsetX);
    shaderUniformFloat(shader, std::string(name).append(".offsetY").c_str(),
                       mesh.textures[i].offsetY);
    shaderUniformFloat(shader, std::string(name).append(".scaleX").c_str(),
                       mesh.textures[i].scaleX);
    shaderUniformFloat(shader, std::string(name).append(".scaleY").c_str(),
                       mesh.textures[i].scaleY);
    shaderUniformFloat(shader, std::string(name).append(".rotation").c_str(),
                       mesh.textures[i].rotation);
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, mesh.textures[i].id);
  }

  shaderUniformFloat(shader, "material.ambient", mesh.material.ambient);
  shaderUniformFloat(shader, "material.diffuse", mesh.material.diffuse);
  shaderUniformFloat(shader, "material.specular", mesh.material.specular);
  glm::vec4 matColor;
  if (baseColorOverride) {
    matColor = glm::vec4(*baseColorOverride, 1.0f);
  } else {
    matColor = glm::vec4(mesh.material.color[0], mesh.material.color[1],
                         mesh.material.color[2],
                         1.0f); // <-- explicit alpha = 1.0
  }
  shaderUniformVec4(shader, "material.color", matColor);
  shaderUniformFloat(shader, "material.shininess", mesh.material.shininess);
  shaderUniformFloat(shader, "material.alpha", mesh.material.alpha);

  // ---- Determine highlight color and value ----
  glm::vec4 effectiveHighlight = highlightColor;
  float effectiveHighlightValue = mesh.highlight_value;
  float pressValForShader = mesh.press; // will be used for shader uniform

  // If dual highlight is enabled, we always suppress press for the shader,
  // because we want the highlight to come solely from the axis value.
  if (mesh.use_dual_highlight) {
    pressValForShader = 0.0f; // <-- UNCONDITIONAL suppression

    // If we have a non‑zero axis value, compute the highlight colour and value.
    if (fabs(mesh.axis_highlight_value) > 0.001f) {
      float rawVal = fabs(mesh.axis_highlight_value);
      float deadzone = mesh.axis_deadzone;
      float effectiveVal = 0.0f;
      if (rawVal > deadzone) {
        effectiveVal = (rawVal - deadzone) / (1.0f - deadzone);
        if (effectiveVal > 1.0f)
          effectiveVal = 1.0f;
      }
      effectiveHighlightValue = effectiveVal;
      if (effectiveVal > 0.001f) {
        if (mesh.axis_highlight_value > 0) {
          effectiveHighlight = glm::vec4(mesh.highlight_color_positive[0],
                                         mesh.highlight_color_positive[1],
                                         mesh.highlight_color_positive[2],
                                         mesh.highlight_color_positive[3]);
        } else {
          effectiveHighlight = glm::vec4(mesh.highlight_color_negative[0],
                                         mesh.highlight_color_negative[1],
                                         mesh.highlight_color_negative[2],
                                         mesh.highlight_color_negative[3]);
        }
      } else {
        // Axis is within deadzone -> no highlight
        effectiveHighlight = highlightColor;
      }
    } else {
      // Axis is neutral -> no highlight
      effectiveHighlightValue = 0.0f;
      effectiveHighlight = highlightColor;
    }
  }

  // Use the computed highlight color and value
  shaderUniformVec4(shader, "highlight_color", effectiveHighlight);
  shaderUniformFloat(shader, "highlight_value", effectiveHighlightValue);
  shaderUniformFloat(shader, "pressValue", pressValForShader);

  shaderUniformMat4(shader, "model", modelMatrix);
  glm::mat3 normal = glm::mat3(modelMatrix);
  shaderUniformMat3(shader, "normal_model",
                    glm::transpose(glm::inverse(normal)));

  if (mesh.visible) {
    glDrawElements(GL_TRIANGLES, mesh.elements, GL_UNSIGNED_INT, 0);
  }
}
int getTouchpadAncestor(const Model &m, int meshIndex) {
  if (meshIndex < 0 || meshIndex >= (int)m.meshes.size())
    return -1;
  int current = meshIndex;
  int steps = 0;
  int maxSteps = (int)m.meshes.size() + 1; // cycle guard
  while (current != -1) {
    if (current < 0 || current >= (int)m.meshes.size())
      return -1; // broken/stale parent chain - fail safe instead of crashing
    if (m.meshes[current].isTouchpad)
      return current;
    current = m.meshes[current].parentIndex;
    if (++steps > maxSteps)
      return -1; // parentIndex cycle - bail out instead of looping forever
  }
  return -1;
}

glm::mat4 computeMeshTransform(const Model &m, int meshIndex,
                               const glm::mat4 &parentMatrix) {
  if (meshIndex < 0 || meshIndex >= (int)m.meshes.size())
    return parentMatrix;
  const Mesh &mesh = m.meshes[meshIndex];
  glm::mat4 model = parentMatrix;

  // Apply mesh position
  model = glm::translate(
      model, glm::vec3(mesh.position[0], mesh.position[1], mesh.position[2]));

  // Translate to pivot point
  model = glm::translate(model,
                         glm::vec3(mesh.pivot_offset[0], mesh.pivot_offset[1],
                                   mesh.pivot_offset[2]));

  // ---- Apply ALL rotations while at the pivot ----
  // Mesh Euler rotation
  model =
      glm::rotate(model, glm::radians(mesh.rotation[0]), glm::vec3(1, 0, 0));
  model =
      glm::rotate(model, glm::radians(mesh.rotation[1]), glm::vec3(0, 1, 0));
  model =
      glm::rotate(model, glm::radians(mesh.rotation[2]), glm::vec3(0, 0, 1));

  // Stick rotation (always applied; zero for non‑sticks)
  model = glm::rotate(model, mesh.stick_X / -32767 * mesh.stick_max,
                      glm::vec3(0.0f, 0.0f, 1.0f));
  model = glm::rotate(model, mesh.stick_Y / 32767 * mesh.stick_max,
                      glm::vec3(1.0f, 0.0f, 0.0f));
  // Trigger rotation
  model = glm::rotate(model, mesh.pull / -32767 * mesh.trigger_max,
                      glm::vec3(1.0f, 0.0f, 0.0f));

  // ---- Translate back from pivot ----
  model = glm::translate(model,
                         -glm::vec3(mesh.pivot_offset[0], mesh.pivot_offset[1],
                                    mesh.pivot_offset[2]));

  // ---- Custom scale (if enabled) ----
  if (mesh.useCustomScale) {
    model = glm::scale(model,
                       glm::vec3(mesh.scale[0], mesh.scale[1], mesh.scale[2]));
  }

  // ---- Popup or button travel (translations) ----
  if (mesh.popup) {
    model = glm::translate(model,
                           glm::vec3(mesh.popup_offset[0], mesh.popup_offset[1],
                                     mesh.popup_offset[2]));
    model =
        glm::rotate(model, mesh.popup_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f));
    model =
        glm::rotate(model, mesh.popup_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model =
        glm::rotate(model, mesh.popup_rotation[2], glm::vec3(0.0f, 0.0f, 1.0f));
  } else {
    // ---- Travel translation and rotation (button press / axis deflection)
    // ---- Use signed value if available, otherwise fallback to absolute
    // travel_value.
    float travelMult = 0.0f;
    if (fabs(mesh.travel_signed) > 0.001f) {
      travelMult = mesh.travel_signed;
    } else if (mesh.travel_value > 0.001f) {
      travelMult = mesh.travel_value;
    }

    if (fabs(travelMult) > 0.001f) {
      // Translation
      model = glm::translate(model, glm::vec3(mesh.travel[0] * travelMult,
                                              mesh.travel[1] * travelMult,
                                              mesh.travel[2] * travelMult));
      // Rotation (around pivot – already translated to pivot above)
      model =
          glm::rotate(model, glm::radians(mesh.travel_rotation[0] * travelMult),
                      glm::vec3(1.0f, 0.0f, 0.0f));
      model =
          glm::rotate(model, glm::radians(mesh.travel_rotation[1] * travelMult),
                      glm::vec3(0.0f, 1.0f, 0.0f));
      model =
          glm::rotate(model, glm::radians(mesh.travel_rotation[2] * travelMult),
                      glm::vec3(0.0f, 0.0f, 1.0f));
    }
  }

  // ---- Touchpad offset ----
  if (mesh.touch_state > 0) {
    int touchpadIdx = getTouchpadAncestor(m, meshIndex);
    if (touchpadIdx != -1) {
      const Mesh &touchpad = m.meshes[touchpadIdx];
      float tw = touchpad.touch_width;
      float th = touchpad.touch_height;

      // Build the touchpad's full transform (without parent matrix)
      glm::mat4 touchpadTransform = glm::mat4(1.0f);
      touchpadTransform = glm::translate(touchpadTransform,
                                         glm::vec3(touchpad.touch_offset[0],
                                                   touchpad.touch_offset[1],
                                                   touchpad.touch_offset[2]));
      touchpadTransform = glm::rotate(touchpadTransform,
                                      glm::radians(touchpad.touch_rotation[1]),
                                      glm::vec3(0, 1, 0));
      touchpadTransform = glm::rotate(touchpadTransform,
                                      glm::radians(touchpad.touch_rotation[0]),
                                      glm::vec3(1, 0, 0));
      touchpadTransform = glm::rotate(touchpadTransform,
                                      glm::radians(touchpad.touch_rotation[2]),
                                      glm::vec3(0, 0, 1));

      // Calculate the touch position within the touchpad area (centered)
      glm::vec3 localPos = glm::vec3((mesh.touch_X * tw) - tw * 0.5f,
                                     0.02f, // Lift above surface
                                     (mesh.touch_Y * th) - th * 0.5f);

      // Apply the touchpad's transform to the local position
      glm::vec4 worldPos = touchpadTransform * glm::vec4(localPos, 1.0f);

      // Apply the translation to the model
      model = glm::translate(model, glm::vec3(worldPos));
    } else {
      // Fallback: use own dimensions and zero offset/rotation
      model = glm::translate(
          model,
          glm::vec3(
              (mesh.touch_X * mesh.touch_width) - mesh.touch_width * 0.5, 0.02f,
              (mesh.touch_Y * mesh.touch_height) - mesh.touch_height * 0.5));
    }
  }

  return model;
}

glm::mat4 getMeshFinalMatrix(const Model &m, int idx, const glm::mat4 &parent) {
  if (idx < 0 || idx >= (int)m.meshes.size())
    return glm::mat4(1.0f);
  const Mesh &mesh = m.meshes[idx];
  if (mesh.parentIndex != -1) {
    glm::mat4 parentMat = getMeshFinalMatrix(m, mesh.parentIndex, parent);
    return computeMeshTransform(m, idx, parentMat);
  } else {
    return computeMeshTransform(m, idx, parent);
  }
}

void drawModel(Model &m, GLuint shader, int highlight_mesh_index,
               const glm::vec4 &globalHighlightColor) {
  int num_meshes = (int)m.meshes.size();
  std::vector<glm::mat4> finalMatrices(num_meshes, glm::mat4(1.0f));
  std::vector<bool> computed(num_meshes, false);

  // ---- Set popup flags ----
  // `m` is now taken by reference instead of by value: drawModel runs every
  // frame per open window, and the model can hold a large imported mesh
  // library, so a full deep copy here was the single most expensive thing
  // in the render loop. `popup` is safe to write back onto the real mesh
  // because it's fully re-derived from m.popup_bumpers/triggers/paddles
  // every frame anyway — nothing is lost by making it "stick" between
  // frames. The selection-highlight color is handled differently below
  // (see baseColorOverride) specifically because that one must NOT persist
  // into the mesh's real material.
  for (int i = 0; i < num_meshes; ++i) {
    Mesh &mesh = m.meshes[i];
    mesh.popup = (mesh.isBumper && m.popup_bumpers) ||
                 (mesh.isTrigger && m.popup_triggers) ||
                 (mesh.isPaddle && m.popup_paddles);
  }

  // ---- Compute matrices (parent-child hierarchy) ----
  // Use getMeshFinalMatrix which correctly follows the parent chain
  // recursively.
  for (int i = 0; i < num_meshes; ++i) {
    finalMatrices[i] = getMeshFinalMatrix(m, i, m.motion_matrix);
  }

  // ---- Draw ----
  const glm::vec3 selectionColor(0.0f, 1.0f, 0.0f);
  for (int i = 0; i < num_meshes; ++i) {
    Mesh &mesh = m.meshes[i];

    // ---- Determine effective highlight color ----
    glm::vec4 highlightCol;
    if (mesh.use_custom_highlight) {
      highlightCol = glm::vec4(
          mesh.custom_highlight_color[0], mesh.custom_highlight_color[1],
          mesh.custom_highlight_color[2], mesh.custom_highlight_color[3]);
    } else {
      highlightCol = globalHighlightColor;
    }

    // ---- Draw the mesh ----
    // When this mesh is the selected one (highlight_mesh_index), force its
    // base color to green for the draw call only, via baseColorOverride,
    // rather than overwriting mesh.material.color — which used to require
    // drawModel to take the whole Model by value just to make that
    // overwrite disposable.
    const glm::vec3 *baseColorOverride =
        (i == highlight_mesh_index) ? &selectionColor : nullptr;
    drawMesh(mesh, finalMatrices[i], shader, highlightCol, baseColorOverride);
  }
}

// ------------------------------------------------------------------
// CUSTOM MODEL IMPORT (using Assimp) and MAPPING
// ------------------------------------------------------------------

void importModelFile(Model &m, const std::string &filepath) {
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(
      filepath, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                    aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
  if (!scene || !scene->mRootNode) {
    spdlog::error("Failed to import model: {}", importer.GetErrorString());
    return;
  }

  m.imported_meshes.clear();

  // Recursively collect all meshes from the scene
  std::function<void(aiNode *)> processNode = [&](aiNode *node) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
      aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
      ImportedMesh imported;
      imported.name = mesh->mName.length ? mesh->mName.C_Str() : "Unnamed";
      imported.assigned_part = -1;

      // Copy vertex data
      for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
        imported.positions.push_back(glm::vec3(
            mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z));
        if (mesh->HasNormals())
          imported.normals.push_back(glm::vec3(
              mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z));
        else
          imported.normals.push_back(glm::vec3(0, 1, 0));
        if (mesh->HasTextureCoords(0))
          imported.texcoords.push_back(glm::vec2(mesh->mTextureCoords[0][v].x,
                                                 mesh->mTextureCoords[0][v].y));
        else
          imported.texcoords.push_back(glm::vec2(0, 0));
      }
      // Copy indices
      for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        aiFace face = mesh->mFaces[f];
        for (unsigned int j = 0; j < face.mNumIndices; ++j)
          imported.indices.push_back(face.mIndices[j]);
      }
      m.imported_meshes.push_back(imported);
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
      processNode(node->mChildren[i]);
  };
  processNode(scene->mRootNode);

  m.has_imported_meshes = !m.imported_meshes.empty();
  if (m.has_imported_meshes)
    spdlog::info("Imported {} meshes from {}", m.imported_meshes.size(),
                 filepath);
}

void applyMeshMapping(Model &m) {
  // For each imported mesh that has an assigned part, replace the corresponding
  // mesh in m.meshes
  for (auto &imported : m.imported_meshes) {
    if (imported.assigned_part < 0 || imported.assigned_part >= 35)
      continue;

    // Build vertex_data from imported data
    std::vector<float> vertex_data;
    for (size_t i = 0; i < imported.positions.size(); ++i) {
      vertex_data.push_back(imported.positions[i].x);
      vertex_data.push_back(imported.positions[i].y);
      vertex_data.push_back(imported.positions[i].z);
      vertex_data.push_back(imported.normals[i].x);
      vertex_data.push_back(imported.normals[i].y);
      vertex_data.push_back(imported.normals[i].z);
      vertex_data.push_back(imported.texcoords[i].x);
      vertex_data.push_back(imported.texcoords[i].y);
    }

    Mesh &target = m.meshes[imported.assigned_part];
    // Delete old GL objects
    if (target.vao)
      glDeleteVertexArrays(1, &target.vao);
    if (target.vbo)
      glDeleteBuffers(1, &target.vbo);
    if (target.ebo)
      glDeleteBuffers(1, &target.ebo);

    // Upload new data
    glGenVertexArrays(1, &target.vao);
    glGenBuffers(1, &target.vbo);
    glGenBuffers(1, &target.ebo);
    glBindVertexArray(target.vao);
    glBindBuffer(GL_ARRAY_BUFFER, target.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float),
                 vertex_data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, target.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 imported.indices.size() * sizeof(unsigned int),
                 imported.indices.data(), GL_STATIC_DRAW);
    target.elements = imported.indices.size();
    glBindVertexArray(0);

    // Preserve existing material and motion data (they are kept as-is)
    spdlog::info("Assigned mesh '{}' to part '{}'", imported.name,
                 mesh_names[imported.assigned_part]);
  }

  // Clear imported list to indicate mapping applied
  m.imported_meshes.clear();
  m.has_imported_meshes = false;
}

void convertImportedToMeshes(Model &m) {
  m.meshes.clear();

  for (auto &imported : m.imported_meshes) {
    Mesh mesh;
    mesh.material.ambient = 0.2f;
    mesh.material.diffuse = 1.0f;
    mesh.material.specular = 0.1f;
    mesh.material.shininess = 32.0f;
    mesh.material.color[0] = 0.8f;
    mesh.material.color[1] = 0.8f;
    mesh.material.color[2] = 0.8f;
    mesh.material.highlight[0] = 0.0f;
    mesh.material.highlight[1] = 1.0f;
    mesh.material.highlight[2] = 0.0f;
    mesh.touch_width = 1.0f;
    mesh.touch_height = 1.0f;
    mesh.isTouchpad = false;
    // parentIndex will be set when mapping is applied; we don't set it here.

    std::vector<float> vertex_data;
    for (size_t i = 0; i < imported.positions.size(); ++i) {
      vertex_data.push_back(imported.positions[i].x);
      vertex_data.push_back(imported.positions[i].y);
      vertex_data.push_back(imported.positions[i].z);
      vertex_data.push_back(imported.normals[i].x);
      vertex_data.push_back(imported.normals[i].y);
      vertex_data.push_back(imported.normals[i].z);
      vertex_data.push_back(imported.texcoords[i].x);
      vertex_data.push_back(imported.texcoords[i].y);
    }

    // Compute bounding box from imported positions
    mesh.hasBBox = true;
    mesh.bboxMin = glm::vec3(FLT_MAX);
    mesh.bboxMax = glm::vec3(-FLT_MAX);
    for (const auto &pos : imported.positions) {
      mesh.bboxMin.x = std::min(mesh.bboxMin.x, pos.x);
      mesh.bboxMin.y = std::min(mesh.bboxMin.y, pos.y);
      mesh.bboxMin.z = std::min(mesh.bboxMin.z, pos.z);
      mesh.bboxMax.x = std::max(mesh.bboxMax.x, pos.x);
      mesh.bboxMax.y = std::max(mesh.bboxMax.y, pos.y);
      mesh.bboxMax.z = std::max(mesh.bboxMax.z, pos.z);
    }

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float),
                 vertex_data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 imported.indices.size() * sizeof(unsigned int),
                 imported.indices.data(), GL_STATIC_DRAW);
    mesh.elements = imported.indices.size();
    glBindVertexArray(0);

    m.meshes.push_back(mesh);
  }
  m.has_imported_meshes = true;
}

glm::vec3 computeMeshCenter(const Mesh &mesh) {
  if (!mesh.hasBBox)
    return glm::vec3(0.0f);
  return (mesh.bboxMin + mesh.bboxMax) * 0.5f;
}

// Compute the world matrix of a mesh, ignoring the global gyro matrix
glm::mat4 getModelMatrixWithoutGyro(const Model &m, int meshIdx) {
  if (meshIdx < 0 || meshIdx >= (int)m.meshes.size())
    return glm::mat4(1.0f);
  const Mesh &mesh = m.meshes[meshIdx];
  if (mesh.parentIndex != -1) {
    glm::mat4 parentMat = getModelMatrixWithoutGyro(m, mesh.parentIndex);
    return computeMeshTransform(m, meshIdx, parentMat);
  } else {
    // Root: use identity instead of m.motion_matrix
    return computeMeshTransform(m, meshIdx, glm::mat4(1.0f));
  }
}

// Compute the world position of a mesh, ignoring gyro
glm::vec3 getModelWorldPositionWithoutGyro(const Model &m, int meshIdx) {
  glm::mat4 worldMat = getModelMatrixWithoutGyro(m, meshIdx);
  return glm::vec3(worldMat[3]);
}

bool wouldCreateCycle(const Model &m, int childIdx, int parentIdx) {
  if (childIdx < 0 || childIdx >= (int)m.meshes.size())
    return false;
  if (parentIdx < 0)
    return false; // no parent, so no cycle
  int current = childIdx;
  while (current != -1) {
    if (current == parentIdx)
      return true;
    const Mesh &mesh = m.meshes[current];
    current = mesh.parentIndex;
  }
  return false;
}
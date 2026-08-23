#include "settings.h"
#include "gamecontrollerdb_data.h"
#include "miniz.h"
#include "models_zip_data.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>

// Base directory of the executable itself (as opposed to config_base_path,
// the writable per-user data directory) - used below to look for a
// portable/installed "models" folder sitting next to the binary before
// falling back to the embedded model library.
const char *base_path = SDL_GetBasePath();

std::string config_base_path;

namespace {

bool is_usable_dir(const std::filesystem::path &p) {
  std::error_code ec;
  return std::filesystem::exists(p, ec) &&
         std::filesystem::is_directory(p, ec) &&
         !std::filesystem::is_empty(p, ec);
}

static bool extract_zip_from_memory(const unsigned char *data, size_t size,
                                    const std::string &dest_dir) {
  if (!data || size == 0) {
    spdlog::error("Embedded ZIP data is empty");
    return false;
  }

  // Debug: log first 16 bytes to verify ZIP signature
  std::string hex;
  for (size_t i = 0; i < std::min<size_t>(16, size); ++i) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x ", data[i]);
    hex += buf;
  }
  spdlog::info("Embedded ZIP size: {} bytes, first bytes: {}", size, hex);

  mz_zip_archive zip_archive;
  memset(&zip_archive, 0, sizeof(zip_archive));

  if (!mz_zip_reader_init_mem(&zip_archive, data, size, 0)) {
    spdlog::error("Failed to initialize ZIP reader from embedded data");
    return false;
  }

  unsigned int num_files = mz_zip_reader_get_num_files(&zip_archive);
  spdlog::info("Embedded ZIP contains {} entries", num_files);

  // Determine common root prefix (e.g., "models/")
  std::string common_prefix;
  if (num_files > 0) {
    bool all_same = true;
    std::string first_name;
    mz_zip_archive_file_stat first_stat;
    if (mz_zip_reader_file_stat(&zip_archive, 0, &first_stat)) {
      first_name = first_stat.m_filename;
      size_t slash = first_name.find('/');
      if (slash != std::string::npos) {
        common_prefix = first_name.substr(0, slash + 1);
      } else {
        common_prefix.clear();
      }
    }
    if (!common_prefix.empty()) {
      for (unsigned int i = 1; i < num_files; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip_archive, i, &stat)) {
          std::string name = stat.m_filename;
          if (name.find(common_prefix) != 0) {
            all_same = false;
            break;
          }
        } else {
          all_same = false;
          break;
        }
      }
      if (!all_same) {
        common_prefix.clear();
      }
    }
    if (!common_prefix.empty()) {
      spdlog::info("Stripping common prefix: {}", common_prefix);
    }
  }

  for (unsigned int i = 0; i < num_files; ++i) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
      spdlog::warn("Failed to get file stat for entry {}", i);
      continue;
    }

    std::string filename = file_stat.m_filename;
    // Strip common prefix if present
    if (!common_prefix.empty() && filename.find(common_prefix) == 0) {
      filename = filename.substr(common_prefix.length());
    }
    if (filename.empty())
      continue; // skip if only prefix

    std::string full_path = dest_dir + "/" + filename;

    if (file_stat.m_is_directory) {
      std::filesystem::create_directories(full_path);
    } else {
      std::filesystem::create_directories(
          std::filesystem::path(full_path).parent_path());
      if (!mz_zip_reader_extract_to_file(&zip_archive, i, full_path.c_str(),
                                         0)) {
        spdlog::error("Failed to extract file: {}", file_stat.m_filename);
        mz_zip_reader_end(&zip_archive);
        return false;
      }
    }
  }

  mz_zip_reader_end(&zip_archive);
  return true;
}

} // namespace

std::string get_models_root() {
  namespace fs = std::filesystem;

  // User's writable models directory
  fs::path user_models = fs::path(config_base_path) / "models";

  // If user_models already exists and is not empty, use it
  if (is_usable_dir(user_models)) {
    return user_models.string();
  }

  // ---- Try to find an external source folder ----
  fs::path exe_dir(base_path);
  fs::path portable_models = exe_dir / "models";
  fs::path installed_models = exe_dir / ".." / "share" / "3dco+" / "models";

  fs::path source;
  std::error_code ec;
  if (fs::exists(portable_models, ec) &&
      fs::is_directory(portable_models, ec)) {
    source = portable_models;
  } else if (fs::exists(installed_models, ec) &&
             fs::is_directory(installed_models, ec)) {
    source = installed_models;
  }

  if (!source.empty()) {
    try {
      fs::create_directories(user_models);
      // Copy each child of source into user_models (not the source directory
      // itself)
      for (const auto &entry : fs::directory_iterator(source)) {
        const auto dest_path = user_models / entry.path().filename();
        if (fs::is_directory(entry.path())) {
          fs::copy(entry.path(), dest_path,
                   fs::copy_options::recursive |
                       fs::copy_options::overwrite_existing);
        } else {
          fs::copy(entry.path(), dest_path,
                   fs::copy_options::overwrite_existing);
        }
      }
      spdlog::info("Copied default models from '{}' to '{}'", source.string(),
                   user_models.string());
      return user_models.string();
    } catch (const std::exception &e) {
      spdlog::warn("Could not copy default models into '{}': {}",
                   user_models.string(), e.what());
    }
  }

  // ---- No external source – try embedded ZIP ----
  if (Embedded::models_zip_size > 0) {
    spdlog::info("No external models folder found; extracting embedded models");
    // Ensure destination exists
    fs::create_directories(user_models);
    if (extract_zip_from_memory(Embedded::models_zip_data,
                                Embedded::models_zip_size,
                                user_models.string())) {
      spdlog::info("Successfully extracted embedded models to {}",
                   user_models.string());
      return user_models.string();
    } else {
      spdlog::error("Failed to extract embedded models");
    }
  }

  // Fallback: create empty directory
  fs::create_directories(user_models);
  return user_models.string();
}

std::string get_gamecontrollerdb_path() {
  return config_base_path + "/gamecontrollerdb.txt";
}

void ensure_gamecontrollerdb() {
  static bool loaded = false;
  if (loaded)
    return;
  loaded = true;

  if (Embedded::gamecontrollerdb_size == 0) {
    spdlog::warn(
        "Embedded gamecontrollerdb data is empty; no mappings loaded.");
    return;
  }

  SDL_IOStream *rw = SDL_IOFromConstMem(Embedded::gamecontrollerdb_data,
                                        Embedded::gamecontrollerdb_size);
  if (!rw) {
    spdlog::error("Failed to create RWops from embedded gamecontrollerdb: {}",
                  SDL_GetError());
    return;
  }

  int count = SDL_AddGamepadMappingsFromIO(rw, 1); // 1 = SDL frees the RWops
  if (count < 0) {
    spdlog::error("SDL_AddGamepadMappingsFromIO failed: {}", SDL_GetError());
  } else {
    spdlog::info("Loaded {} gamecontroller mappings from embedded data", count);
  }
}
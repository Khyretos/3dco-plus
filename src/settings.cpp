#include "settings.h"
#include "gamecontrollerdb_data.h"
#include "miniz.h"
#include "models_zip_data.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
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

// A "models" folder shipped next to the executable (portable layout) or
// in ../share/3dco+/models (installed layout, e.g. the AppImage), if
// there is one - used in preference to the embedded model library.
// Empty if neither exists.
std::filesystem::path find_external_models_source() {
  namespace fs = std::filesystem;
  if (!base_path)
    return {};
  fs::path exe_dir(base_path);
  fs::path portable_models = exe_dir / "models";
  fs::path installed_models = exe_dir / ".." / "share" / "3dco+" / "models";
  std::error_code ec;
  if (fs::is_directory(portable_models, ec))
    return portable_models;
  if (fs::is_directory(installed_models, ec))
    return installed_models;
  return {};
}

} // namespace

// Extracts an in-memory ZIP (typically one of the embedded
// Embedded::*_zip_data blobs baked in by a tools/generate_*_zip.py
// script) to dest_dir. Exposed via settings.h rather than kept
// file-local, since more than one embedded asset pack now uses this
// exact "extract once to the user's data directory on first use"
// pattern - see get_models_root() below for the original, and
// input_history.cpp's glyph extraction for the other caller.
bool extract_zip_from_memory(const unsigned char *data, size_t size,
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

std::string get_models_root() {
  namespace fs = std::filesystem;

  // User's writable models directory
  fs::path user_models = fs::path(config_base_path) / "models";

  // If user_models already exists and is not empty, use it
  if (is_usable_dir(user_models)) {
    return user_models.string();
  }

  // ---- Try to find an external source folder ----
  fs::path source = find_external_models_source();

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

  std::string path = get_gamecontrollerdb_path();

  // Seed the on-disk file from the embedded copy only if it doesn't
  // already exist - this runs once, on first launch (or if the user
  // deleted the file). Every subsequent launch finds the file already
  // there and skips straight to loading from it. This is what makes
  // exported/appended mappings (see settings_window.cpp's "Export
  // Mapping" button) actually persist and take effect: appending to
  // the embedded copy would do nothing, since it's baked into the
  // executable and re-extracted fresh every time otherwise.
  if (!std::filesystem::exists(path)) {
    if (Embedded::gamecontrollerdb_size == 0) {
      spdlog::warn(
          "Embedded gamecontrollerdb data is empty; nothing to seed {} with.",
          path);
    } else {
      std::ofstream out(path, std::ios::binary);
      if (out) {
        out.write(
            reinterpret_cast<const char *>(Embedded::gamecontrollerdb_data),
            (std::streamsize)Embedded::gamecontrollerdb_size);
        out.close();
        spdlog::info("Seeded {} from embedded gamecontrollerdb data ({} "
                     "bytes) for first run.",
                     path, Embedded::gamecontrollerdb_size);
      } else {
        spdlog::error("Failed to create {} for first-run seeding.", path);
      }
    }
  }

  // Always load from disk from here on - never re-reads the embedded
  // copy once the on-disk file exists, so any mappings added to it
  // (including the user's own machine-local additions, or ones copied
  // in from someone else's exported mapping) are picked up normally.
  int count = SDL_AddGamepadMappingsFromFile(path.c_str());
  if (count < 0) {
    spdlog::error("SDL_AddGamepadMappingsFromFile({}) failed: {}", path,
                  SDL_GetError());
  } else {
    spdlog::info("Loaded {} gamecontroller mappings from {}", count, path);
  }
}
// ------------------------------------------------------------------
// Bundled models
//
// The "bundled" set is whatever this build ships: the external models
// folder if there is one (see find_external_models_source()), otherwise
// the embedded ZIP - the same priority get_models_root() uses for the
// first-run copy. Entries in the embedded ZIP look like
// "models/<Model Name>/<file>".
// ------------------------------------------------------------------

namespace {

constexpr const char *kZipModelsPrefix = "models/";

// Runs fn(entry_index, path_inside_models_dir) for every file in the
// embedded models ZIP. Returns false if the ZIP couldn't be opened.
template <typename Fn> bool for_each_embedded_model_file(Fn &&fn) {
  if (Embedded::models_zip_size == 0)
    return false;
  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_mem(&zip, Embedded::models_zip_data,
                              Embedded::models_zip_size, 0))
    return false;
  const mz_uint count = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
      continue;
    std::string name = st.m_filename;
    if (name.rfind(kZipModelsPrefix, 0) == 0)
      name = name.substr(strlen(kZipModelsPrefix));
    if (!fn(zip, i, name))
      break;
  }
  mz_zip_reader_end(&zip);
  return true;
}

// True if a relative path from the ZIP stays inside its destination
// (no absolute paths, no ".." components).
bool is_safe_relative_path(const std::filesystem::path &rel) {
  if (rel.empty() || rel.is_absolute() || rel.has_root_name())
    return false;
  for (const auto &part : rel)
    if (part == "..")
      return false;
  return true;
}

// Copies/extracts bundled model `name` into `dest` (which must not
// exist yet).
bool write_bundled_model_to(const std::string &name,
                            const std::filesystem::path &dest,
                            std::string &error) {
  namespace fs = std::filesystem;
  fs::path external = find_external_models_source();
  if (!external.empty()) {
    std::error_code ec;
    fs::copy(external / name, dest, fs::copy_options::recursive, ec);
    if (ec) {
      error = ec.message();
      return false;
    }
    return true;
  }

  const std::string prefix = name + "/";
  bool ok = true;
  bool any = false;
  for_each_embedded_model_file([&](mz_zip_archive &zip, mz_uint index,
                                   const std::string &file) {
    if (file.rfind(prefix, 0) != 0)
      return true;
    fs::path rel = fs::u8path(file.substr(prefix.size()));
    if (!is_safe_relative_path(rel))
      return true;
    fs::path out = dest / rel;
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    if (!mz_zip_reader_extract_to_file(&zip, index, out.string().c_str(), 0)) {
      error = "could not extract " + file;
      ok = false;
      return false;
    }
    any = true;
    return true;
  });
  if (ok && !any)
    error = "not found in this build";
  return ok && any;
}

} // namespace

const std::vector<std::string> &list_bundled_models() {
  // Fixed for the lifetime of the process - computed once.
  static const std::vector<std::string> models = [] {
    namespace fs = std::filesystem;
    std::set<std::string> names;
    fs::path external = find_external_models_source();
    if (!external.empty()) {
      std::error_code ec;
      for (const auto &entry : fs::directory_iterator(external, ec))
        if (entry.is_directory(ec))
          names.insert(entry.path().filename().u8string());
    } else {
      for_each_embedded_model_file(
          [&](mz_zip_archive &, mz_uint, const std::string &file) {
            size_t slash = file.find('/');
            if (slash != std::string::npos && slash > 0)
              names.insert(file.substr(0, slash));
            return true;
          });
    }
    return std::vector<std::string>(names.begin(), names.end());
  }();
  return models;
}

bool is_bundled_model_installed(const std::string &name) {
  std::error_code ec;
  return std::filesystem::is_directory(
      std::filesystem::path(get_models_root()) / std::filesystem::u8path(name),
      ec);
}

bool install_bundled_model(const std::string &name, std::string *backup_path,
                           std::string *error) {
  namespace fs = std::filesystem;
  std::string err;
  auto fail = [&](const std::string &what) {
    spdlog::warn("Could not install bundled model '{}': {}", name, what);
    if (error)
      *error = what;
    return false;
  };

  const fs::path models_root = get_models_root();
  const fs::path target = models_root / fs::u8path(name);
  const fs::path staging = models_root / fs::u8path("." + name + ".installing");

  // Write the fresh copy to a staging folder first, so a failure halfway
  // through never leaves a half-written model (or no model at all)
  // behind.
  std::error_code ec;
  fs::remove_all(staging, ec);
  if (!write_bundled_model_to(name, staging, err)) {
    fs::remove_all(staging, ec);
    return fail(err);
  }

  // Restoring over an existing model: move the current one aside into
  // model_backups/ rather than deleting it - it holds the user's own
  // bindings, travel, textures, etc.
  if (fs::exists(target, ec)) {
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H-%M-%S",
                  std::localtime(&now));
    const fs::path backups = fs::path(config_base_path) / "model_backups";
    const fs::path backup = backups / fs::u8path(name + " " + stamp);
    fs::create_directories(backups, ec);
    fs::rename(target, backup, ec);
    if (ec) {
      // Different filesystem or locked file - fall back to copy+remove.
      ec.clear();
      fs::copy(target, backup, fs::copy_options::recursive, ec);
      if (!ec)
        fs::remove_all(target, ec);
    }
    if (ec) {
      fs::remove_all(staging, ec);
      return fail("could not back up the existing folder");
    }
    if (backup_path)
      *backup_path = backup.u8string();
  }

  fs::rename(staging, target, ec);
  if (ec) {
    std::string msg = ec.message();
    fs::remove_all(staging, ec);
    return fail(msg);
  }
  spdlog::info("Installed bundled model '{}' to {}", name, target.string());
  return true;
}

#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
#include <vector>

extern std::string config_base_path;

std::string get_models_root();

// Extracts an in-memory ZIP (one of the embedded Embedded::*_zip_data
// blobs, e.g. models_zip_data or glyphs_zip_data) to dest_dir. See
// settings.cpp for the "extract once to the user's data directory on
// first use" pattern this supports.
bool extract_zip_from_memory(const unsigned char *data, size_t size,
                             const std::string &dest_dir);

// Names of the models this build ships (sorted). Computed once.
const std::vector<std::string> &list_bundled_models();

// Whether a folder for bundled model `name` exists in the user's models
// directory.
bool is_bundled_model_installed(const std::string &name);

// Copies bundled model `name` into the user's models directory. If it's
// already there it is replaced, after moving the existing folder to
// <data dir>/model_backups/ (its path is returned in backup_path).
bool install_bundled_model(const std::string &name,
                           std::string *backup_path = nullptr,
                           std::string *error = nullptr);

std::string get_gamecontrollerdb_path();

void ensure_gamecontrollerdb();

#endif
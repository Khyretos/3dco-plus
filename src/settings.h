#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>

extern std::string config_base_path;

std::string get_models_root();

// Extracts an in-memory ZIP (one of the embedded Embedded::*_zip_data
// blobs, e.g. models_zip_data or glyphs_zip_data) to dest_dir. See
// settings.cpp for the "extract once to the user's data directory on
// first use" pattern this supports.
bool extract_zip_from_memory(const unsigned char *data, size_t size,
                             const std::string &dest_dir);

std::string get_gamecontrollerdb_path();

void ensure_gamecontrollerdb();

#endif
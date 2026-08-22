#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>

extern std::string config_base_path;

std::string get_models_root();

std::string get_gamecontrollerdb_path();

void ensure_gamecontrollerdb();

#endif
#pragma once

#include "mods/svc/config.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

bool cfg_bool(ConfigVarHandle var, bool fallback);
int64_t cfg_int(ConfigVarHandle var, int64_t fallback);
std::string cfg_string(ConfigVarHandle var, const char* fallback);

bool peer_on_our_stage();

int power_class_to_damage(int atp);

std::filesystem::path path_ci(const std::filesystem::path& p);
inline bool exists_ci(const std::filesystem::path& p, std::error_code& ec) {
    return std::filesystem::exists(path_ci(p), ec);
}
inline bool is_directory_ci(const std::filesystem::path& p, std::error_code& ec) {
    return std::filesystem::is_directory(path_ci(p), ec);
}

#pragma once

#include "mods/svc/config.h"

#include <cstdint>
#include <string>

bool cfg_bool(ConfigVarHandle var, bool fallback);
int64_t cfg_int(ConfigVarHandle var, int64_t fallback);
std::string cfg_string(ConfigVarHandle var, const char* fallback);

bool peer_on_our_stage();

int power_class_to_damage(int atp);

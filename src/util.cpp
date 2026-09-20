#include "util.hpp"
#include "mod.hpp"

#include "d/d_com_inf_game.h"

bool cfg_bool(ConfigVarHandle var, bool fallback) {
    bool v = fallback;
    if (var == 0 || svc_config == nullptr || svc_config->get_bool(mod_ctx, var, &v) != MOD_OK) {
        return fallback;
    }
    return v;
}

int64_t cfg_int(ConfigVarHandle var, int64_t fallback) {
    int64_t v = fallback;
    if (var == 0 || svc_config == nullptr || svc_config->get_int(mod_ctx, var, &v) != MOD_OK) {
        return fallback;
    }
    return v;
}

std::string cfg_string(ConfigVarHandle var, const char* fallback) {
    if (var == 0 || svc_config == nullptr) return fallback;
    size_t length = 0;
    svc_config->get_string(mod_ctx, var, nullptr, 0, &length);
    if (length == 0) return fallback;
    std::string buffer(length + 1, '\0');
    size_t written = 0;
    if (svc_config->get_string(mod_ctx, var, buffer.data(), buffer.size(), &written) != MOD_OK) {
        return fallback;
    }
    buffer.resize(written);
    return buffer;
}

bool peer_on_our_stage() {
    return features_any_peer_on_stage(dComIfGp_getStartStageName());
}

int power_class_to_damage(int atp) {
    if (atp <= 1) return atp;
    if (atp == 2) return 10;
    if (atp == 3) return 30;
    if (atp == 6) return 80;
    return 200;
}

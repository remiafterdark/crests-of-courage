#include "util.hpp"

#include <cctype>
#include "mod.hpp"

#include "d/d_com_inf_game.h"
#include "JSystem/JUtility/JUTFont.h"

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

std::filesystem::path path_ci(const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return p;
    std::filesystem::path cur;
    for (const std::filesystem::path& part : p) {
        const std::filesystem::path next = cur.empty() ? part : cur / part;
        if (std::filesystem::exists(next, ec)) {
            cur = next;
            continue;
        }
        std::string want = part.string();
        for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool found = false;
        const std::filesystem::path dir = cur.empty() ? std::filesystem::path(".") : cur;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            std::string have = entry.path().filename().string();
            for (char& c : have) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (have == want) {
                cur = entry.path();
                found = true;
                break;
            }
        }
        if (!found) return p;
    }
    return cur;
}

float coop_text_width(JUTFont* font, const char* text, float cell) {
    if (font == nullptr || text == nullptr) return 0.0f;
    float width = 0.0f;
    const float cellWidth = static_cast<float>(font->getCellWidth());
    for (const char* c = text; *c != 0; ++c) {
        const float advance = font->isFixed() ? static_cast<float>(font->getFixedWidth())
                                              : static_cast<float>(font->getWidth(static_cast<u8>(*c)));
        width += cellWidth > 0.0f ? advance * (cell / cellWidth) : cell * 0.6f;
    }
    return width;
}

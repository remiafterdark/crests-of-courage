

#include "mod.hpp"
#include "print.hpp"

#include "mods/service.hpp"
#include "mods/svc/texture.h"

#include "dolphin/gx/GXEnum.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

#pragma pack(push, 1)
struct IconEntry {
    uint64_t hash;
    uint16_t width;
    uint16_t height;
    uint8_t srcFormat;
    uint8_t hasTlut;
    uint8_t pad[2];
    uint32_t size;
};
#pragma pack(pop)

std::vector<TextureReplacementHandle> s_handles;
std::string s_loaded;

void unregister_all() {
    if (svc_texture != nullptr) {
        for (TextureReplacementHandle handle : s_handles) {
            svc_texture->unregister(mod_ctx, handle);
        }
    }
    s_handles.clear();
    s_loaded.clear();
}

void load_for(const std::string& model) {
    const std::string path = skins_folder_path() + "/" + model + "/icons.bin";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        s_loaded = model;
        return;
    }
    char magic[9] = {};
    uint32_t count = 0;
    file.read(magic, 9);
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (std::memcmp(magic, "COOPICON1", 9) != 0 || count == 0 || count > 4096) {
        coop_log::warn("coop_mod: [ICON] '{}' icons.bin is not one of ours", model);
        s_loaded = model;
        return;
    }

    std::vector<uint8_t> pixels;
    int registered = 0;
    for (uint32_t i = 0; i < count; ++i) {
        IconEntry entry{};
        if (!file.read(reinterpret_cast<char*>(&entry), sizeof(entry))) break;
        if (entry.size == 0 || entry.size > 4 * 1024 * 1024) break;
        pixels.resize(entry.size);
        if (!file.read(reinterpret_cast<char*>(pixels.data()), entry.size)) break;

        TextureKey key = TEXTURE_KEY_INIT;
        key.kind = TEXTURE_KEY_SOURCE;
        key.texture_hash = entry.hash;

        key.tlut_hash = TEXTURE_TLUT_WILDCARD;
        key.width = entry.width;
        key.height = entry.height;
        key.gx_format = entry.srcFormat;
        key.has_tlut = entry.hasTlut != 0;

        TextureData data = TEXTURE_DATA_INIT;
        data.data = pixels.data();
        data.size = pixels.size();
        data.width = entry.width;
        data.height = entry.height;
        data.mip_count = 1;
        data.gx_format = GX_TF_RGBA8_PC;

        TextureReplacementHandle handle = 0;
        if (svc_texture->register_data(mod_ctx, &key, &data, &handle) == MOD_OK) {
            s_handles.push_back(handle);
            ++registered;
        }
    }
    s_loaded = model;
    coop_log::info("coop_mod: [ICON] '{}': {} icon(s) registered", model, registered);
}

}

void icons_update() {
    if (svc_texture == nullptr) return;
    const std::string want = skins_local_slot(kSkinChoiceEquipment);
    if (want == s_loaded) return;
    unregister_all();
    if (!want.empty()) load_for(want);
    else s_loaded.clear();
}

void icons_shutdown() {
    unregister_all();
}

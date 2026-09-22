

#include "mod.hpp"
#include "models.hpp"
#include "print.hpp"
#include "util.hpp"

#include "mods/api.h"
#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/host.h"
#include "mods/svc/overlay.h"
#include "mods/svc/resource.h"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_OSX) ||     (defined(__linux__) && !defined(__ANDROID__))
#define COOP_HAS_FILE_MANAGER 1
#else
#define COOP_HAS_FILE_MANAGER 0
#endif

namespace {

const bool kCanOpenFolder = COOP_HAS_FILE_MANAGER != 0;

const char* const kPartFile[kSkinPartCount] = {
    "body.bmd", "face.bmd", "head.bmd", "hands.bmd", "sword.bmd", "shield.bmd"};

const char* const kOutfitDir[kSkinOutfitCount] = {"", "ordon", "zora", "magic", "wolf"};

struct Skin {
    std::string name;

    std::string title;
    std::string author;
    std::string about;
    std::string path;
    bool has[kSkinOutfitCount][kSkinPartCount] = {};

    bool hasEquipment = false;
    bool hasCutscenes = false;
    bool hasVoice = false;
    uint32_t hash = 0;
};

std::vector<Skin> s_skins;
bool s_scanned = false;

struct LoadedPart {
    std::string skin;
    int outfit = 0;
    int part = 0;
    J3DModelData* data = nullptr;
    bool failed = false;
};
std::vector<LoadedPart> s_loadedParts;

struct LoadedCutscene {
    std::string skin;
    std::string file;
    J3DModelData* data = nullptr;
    bool failed = false;
};
std::vector<LoadedCutscene> s_loadedCutscenes;

const char* const kSlotVarName[kSkinChoiceCount] = {
    "model_hero", "model_ordon", "model_zora", "model_magic",
    "model_wolf", "model_equipment", "model_cutscenes", "model_voice",
    "model_sword_wood", "model_sword_ordon", "model_sword_master",
    "model_shield_hylian", "model_shield_ordon",
    "model_hookshot", "model_boomerang", "model_bombs", "model_bow", "model_slingshot",
    "model_ball_chain", "model_spinner", "model_fishing_rod", "model_bottle",
};
const char* const kSlotLabel[kSkinChoiceCount] = {
    "Hero's clothes", "Ordon clothes", "Zora armor", "Magic armor",
    "Wolf", "Equipment", "Cutscenes", "Voice",
    "Wooden Sword", "Ordon Sword", "Master Sword", "Hylian Shield", "Ordon Shield",
    "Clawshot", "Boomerang", "Bombs", "Bow", "Slingshot",
    "Ball and Chain", "Spinner", "Fishing Rod", "Bottle",
};

struct EquipSlotFile {
    const char* file;
    int slot;
};
const EquipSlotFile kEquipSlotFiles[] = {
    {"al_swb.bmd", kSkinChoiceWoodSword},
    {"al_swa.bmd", kSkinChoiceOrdonSword},
    {"al_poda.bmd", kSkinChoiceOrdonSword},
    {"al_swm.bmd", kSkinChoiceMasterSword},
    {"al_podm.bmd", kSkinChoiceMasterSword},
    {"o_al_swm.bmd", kSkinChoiceMasterSword},
    {"al_sha.bmd", kSkinChoiceHylianShield},
    {"al_shc.bmd", kSkinChoiceOrdonShield},

};
ConfigVarHandle s_slotVar[kSkinChoiceCount] = {};
ConfigVarHandle s_filesVar = 0;

bool models_dir(std::filesystem::path& out) {
    if (svc_host == nullptr || !SERVICE_HAS(svc_host, HostService, data_dir)) return false;
    const char* dir = nullptr;
    if (svc_host->data_dir(mod_ctx, &dir) != MOD_OK || dir == nullptr) return false;
    out = std::filesystem::path(dir) / "models";
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    return !ec;
}

void read_about(const std::filesystem::path& dir, Skin& skin) {
    std::ifstream file(dir / "model.txt");
    if (!file) return;
    std::string line;
    while (std::getline(file, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        const auto trim = [](std::string& t) {
            while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
            while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) {
                t.pop_back();
            }
        };
        trim(key);
        trim(value);
        if (key == "name") skin.title = value;
        else if (key == "by" || key == "author") skin.author = value;
        else if (key == "about") skin.about = value;
    }
}

bool scan_one(const std::filesystem::path& path, Skin& skin) {
    skin.name = path.filename().string();
    skin.path = path.string();
    read_about(path, skin);
    if (skin.name.size() >= kSkinNameMax) skin.name.resize(kSkinNameMax - 1);
    bool any = false;
    for (int o = 0; o < kSkinOutfitCount; ++o) {
        const std::filesystem::path dir =
            kOutfitDir[o][0] == '\0' ? path : path / kOutfitDir[o];
        for (int i = 0; i < kSkinPartCount; ++i) {
            std::error_code fec;
            const auto size = std::filesystem::file_size(dir / kPartFile[i], fec);
            if (fec) continue;
            skin.has[o][i] = true;
            any = true;

            skin.hash = skin.hash * 31u + static_cast<uint32_t>(size) +
                        static_cast<uint32_t>(i * 8 + o);
        }
    }
    std::error_code dec;
    skin.hasEquipment = is_directory_ci(path / "equipment", dec) && !dec &&
        !std::filesystem::is_empty(path / "equipment", dec);
    skin.hasCutscenes = is_directory_ci(path / "cutscene", dec) && !dec &&
        !std::filesystem::is_empty(path / "cutscene", dec);
    skin.hasVoice = exists_ci(path / "voices.bin", dec) && !dec;

    return any || skin.hasEquipment || skin.hasCutscenes || skin.hasVoice;
}

void scan_dir(const std::filesystem::path& dir, int& count) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(path_ci(dir), ec)) {
        if (!entry.is_directory()) continue;
        Skin skin;
        if (!scan_one(entry.path(), skin)) continue;

        bool taken = false;
        for (const Skin& have : s_skins) taken = taken || have.name == skin.name;
        if (taken) continue;
        s_skins.push_back(std::move(skin));
        ++count;
    }
}

void rescan() {
    s_skins.clear();
    s_scanned = true;
    int found = 0;
    std::filesystem::path dir;
    if (models_dir(dir)) scan_dir(dir, found);
    std::sort(s_skins.begin(), s_skins.end(),
        [](const Skin& a, const Skin& b) { return a.name < b.name; });
    coop_log::info("coop_mod: [SKIN] {} model(s) in {}", s_skins.size(), dir.string());
}

const Skin* find(const char* name) {
    if (name == nullptr || name[0] == '\0') return nullptr;
    if (!s_scanned) rescan();
    for (const Skin& s : s_skins) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

void forget_failures() {
    for (size_t i = 0; i < s_loadedParts.size();) {
        if (s_loadedParts[i].failed) {
            s_loadedParts.erase(s_loadedParts.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
}

}

std::vector<OverlayHandle> s_overlays;

void clear_overlays() {
    if (svc_overlay == nullptr) return;
    for (OverlayHandle h : s_overlays) svc_overlay->remove(mod_ctx, h);
    s_overlays.clear();
}

void apply_overlays() {
    clear_overlays();
}

void unpack_shipped_models();

ConfigVarHandle s_cycleVar = 0;
ConfigVarHandle s_outfitCycleVar = 0;
ConfigVarHandle s_noLocalSkinVar = 0;

void skins_cycle_update() {
    const int64_t period = cfg_int(s_cycleVar, 0);
    if (period <= 0) return;
    static int tick = 0;
    if (++tick < period) return;
    tick = 0;

    const int count = skins_count();
    if (count <= 0) return;
    static int next = 0;
    const std::string want = (next == 0) ? std::string("") : std::string(skins_name(next - 1));
    next = (next + 1) % (count + 1);
    coop_log::info("coop_mod: [SKIN-CYCLE] switching to '{}'", want.empty() ? "Default" : want);
    skins_set_local_all(want.c_str());
}

const int kOutfitSettleTicks = 240;

void skins_outfit_cycle_update() {
    const int64_t period = cfg_int(s_outfitCycleVar, 0);
    if (period <= 0) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;

    static int settle = 0;
    if (alink->mClothesChangeWaitTimer != 0) {
        settle = kOutfitSettleTicks;
        return;
    }
    if (settle > 0) {
        --settle;
        return;
    }

    static int tick = 0;
    if (++tick < period) return;
    tick = 0;
    settle = kOutfitSettleTicks;

    if (alink->checkWolf()) {
        coop_log::info("coop_mod: [OUTFIT-CYCLE] transforming back");
        coop_debug_force_transform();
        return;
    }

    static const u8 kWear[] = {
        dItemNo_WEAR_KOKIRI_e,
        dItemNo_WEAR_CASUAL_e,
        dItemNo_WEAR_ZORA_e,
        dItemNo_ARMOR_e,
    };
    const int wearCount = static_cast<int>(sizeof(kWear) / sizeof(kWear[0]));
    static int next = 0;
    if (next == wearCount) {
        next = 0;
        coop_log::info("coop_mod: [OUTFIT-CYCLE] transforming");
        coop_debug_force_transform();
        return;
    }
    const u8 want = kWear[next];
    ++next;
    coop_log::info("coop_mod: [OUTFIT-CYCLE] changing clothes to item {}", static_cast<int>(want));
    dComIfGs_setSelectEquipClothes(want);
    alink->setClothesChange(0);
}

void skins_init() {
    unpack_shipped_models();
    for (int i = 0; i < kSkinChoiceCount; ++i) {
        ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
        desc.name = kSlotVarName[i];
        desc.type = CONFIG_VAR_STRING;
        desc.default_string = "";
        if (svc_config == nullptr ||
            svc_config->register_var(mod_ctx, &desc, &s_slotVar[i]) != MOD_OK) {
            s_slotVar[i] = 0;
        }
    }
    ConfigVarDesc cycle = CONFIG_VAR_DESC_INIT;
    cycle.name = "debug_skin_cycle_ticks";
    cycle.type = CONFIG_VAR_INT;
    cycle.default_int = 0;
    if (svc_config == nullptr ||
        svc_config->register_var(mod_ctx, &cycle, &s_cycleVar) != MOD_OK) {
        s_cycleVar = 0;
    }

    ConfigVarDesc outfitCycle = CONFIG_VAR_DESC_INIT;
    outfitCycle.name = "debug_outfit_cycle_ticks";
    outfitCycle.type = CONFIG_VAR_INT;
    outfitCycle.default_int = 0;
    if (svc_config == nullptr ||
        svc_config->register_var(mod_ctx, &outfitCycle, &s_outfitCycleVar) != MOD_OK) {
        s_outfitCycleVar = 0;
    }

    ConfigVarDesc noLocal = CONFIG_VAR_DESC_INIT;
    noLocal.name = "debug_no_local_skin";
    noLocal.type = CONFIG_VAR_BOOL;
    noLocal.default_bool = false;
    if (svc_config == nullptr ||
        svc_config->register_var(mod_ctx, &noLocal, &s_noLocalSkinVar) != MOD_OK) {
        s_noLocalSkinVar = 0;
    }

    ConfigVarDesc files = CONFIG_VAR_DESC_INIT;
    files.name = "model_files";
    files.type = CONFIG_VAR_BOOL;
    files.default_bool = false;
    if (svc_config == nullptr || svc_config->register_var(mod_ctx, &files, &s_filesVar) != MOD_OK) {
        s_filesVar = 0;
    }
    rescan();
    apply_overlays();
}

void unpack_shipped_models() {
    if (svc_resource == nullptr) return;
    std::filesystem::path dir;
    if (!models_dir(dir)) return;

    const std::string stamp = std::string("crests ") + kCoopShippedModelsVersion;
    const std::filesystem::path marker = dir / ".shipped";
    {
        std::ifstream have(marker);
        std::string line;
        if (have && std::getline(have, line) && line == stamp) return;
    }

    ResourceBuffer index = RESOURCE_BUFFER_INIT;
    if (svc_resource->load(mod_ctx, "models/index.txt", &index) != MOD_OK || index.data == nullptr) {
        coop_log::info("coop_mod: [SKIN] no models ship with this build");
        return;
    }
    const std::string list(static_cast<const char*>(index.data), index.size);
    svc_resource->free(mod_ctx, &index);

    int written = 0;
    int failed = 0;
    size_t at = 0;
    while (at < list.size()) {
        size_t nl = list.find('\n', at);
        if (nl == std::string::npos) nl = list.size();
        std::string rel = list.substr(at, nl - at);
        at = nl + 1;
        while (!rel.empty() && (rel.back() == '\r' || rel.back() == ' ')) rel.pop_back();
        if (rel.empty() || rel[0] == '#') continue;

        if (rel.find("..") != std::string::npos || rel.find(':') != std::string::npos ||
            rel[0] == '/' || rel[0] == '\\') {
            continue;
        }

        ResourceBuffer file = RESOURCE_BUFFER_INIT;
        if (svc_resource->load(mod_ctx, ("models/" + rel).c_str(), &file) != MOD_OK) {
            ++failed;
            continue;
        }
        const std::filesystem::path out = dir / std::filesystem::path(rel);
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream dst(out, std::ios::binary | std::ios::trunc);
        if (dst && file.data != nullptr && file.size > 0) {
            dst.write(static_cast<const char*>(file.data), static_cast<std::streamsize>(file.size));
        }
        if (dst) {
            ++written;
        } else {
            ++failed;
        }
        svc_resource->free(mod_ctx, &file);
    }

    std::ofstream(marker, std::ios::trunc) << stamp << "\n";
    coop_log::info("coop_mod: [SKIN] unpacked {} shipped model file(s) into {} ({} failed)",
        written, dir.string(), failed);
}

void skins_refresh() {
    rescan();
    forget_failures();
}

int skins_count() {
    if (!s_scanned) rescan();
    return static_cast<int>(s_skins.size());
}

const char* skins_name(int index) {
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return "";
    return s_skins[index].name.c_str();
}

std::string skins_title(int index) {
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return "";
    return s_skins[index].title.empty() ? s_skins[index].name : s_skins[index].title;
}

std::string skins_about_text(int index) {
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return "";
    const Skin& skin = s_skins[index];
    std::string out;
    if (!skin.author.empty()) out += "By " + skin.author + ". ";
    if (!skin.about.empty()) out += skin.about + " ";
    out += "Covers " + skins_parts_text(index) + ".";
    return out;
}

struct EquipName {
    const char* file;
    const char* label;
};

const EquipName kEquipNames[] = {

    {"al_swb.bmd", "Wooden Sword"},
    {"al_swa.bmd", "Ordon Sword"},
    {"al_swm.bmd", "Master Sword"},
    {"al_sha.bmd", "Hylian Shield"},
    {"al_shc.bmd", "Ordon Shield"},
};

bool skins_ships_equipment_file(const char* name, const char* file);

void skins_equipment_list(int index, std::vector<std::string>& has,
    std::vector<std::string>& missing) {
    has.clear();
    missing.clear();
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return;
    const std::filesystem::path dir = std::filesystem::path(s_skins[index].path) / "equipment";
    std::error_code ec;
    const bool haveDir = is_directory_ci(dir, ec) && !ec;

    auto have_file = [&](const char* file) {
        return skins_ships_equipment_file(s_skins[index].name.c_str(), file);
    };

    auto already = [](const std::vector<std::string>& list, const std::string& label) {
        for (const std::string& item : list) {
            if (item == label) return true;
        }
        return false;
    };
    for (const EquipName& known : kEquipNames) {
        const std::string label = known.label;
        if (already(has, label) || already(missing, label)) continue;
        if (have_file(known.file)) has.push_back(label);
        else missing.push_back(label);
    }

    int others = 0;
    if (haveDir) {
        for (const auto& entry : std::filesystem::directory_iterator(path_ci(dir), ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            bool named = false;
            for (const EquipName& known : kEquipNames) {
                if (name == known.file) named = true;
            }
            if (!named) ++others;
        }
    }
    if (others > 0) {
        has.push_back(std::to_string(others) +
                      (others == 1 ? " other item" : " other items"));
    }
}

void skins_outfit_list(int index, std::vector<std::string>& out) {
    out.clear();
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return;
    static const char* const kOutfitName[kSkinOutfitCount] = {
        "Hero's clothes", "Ordon clothes", "Zora armor", "Magic armor", "Wolf"};
    for (int o = 0; o < kSkinOutfitCount; ++o) {
        bool anyPart = false;
        for (int i = 0; i < kSkinPartCount; ++i) anyPart = anyPart || s_skins[index].has[o][i];
        if (anyPart) out.push_back(kOutfitName[o]);
    }
}

std::string skins_equipment_text(int index) {
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return "";
    const std::filesystem::path dir = std::filesystem::path(s_skins[index].path) / "equipment";
    std::error_code ec;
    if (!is_directory_ci(dir, ec) || ec) return "";
    std::string out;
    int extra = 0;
    for (const auto& entry : std::filesystem::directory_iterator(path_ci(dir), ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        const char* label = nullptr;
        for (const EquipName& known : kEquipNames) {
            if (name == known.file) label = known.label;
        }
        if (label == nullptr) {
            ++extra;
            continue;
        }
        if (!out.empty()) out += ", ";
        out += label;
    }
    if (out.empty() && extra == 0) return "";
    if (extra > 0) {
        if (!out.empty()) out += ", ";
        out += "and " + std::to_string(extra) + " more";
    }
    return out;
}

std::string skins_author_text(int index) {
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return "";
    const std::string& author = s_skins[index].author;
    return author.empty() ? std::string() : "By " + author;
}

std::string skins_own_words(int index) {
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return "";
    return s_skins[index].about;
}

std::string skins_parts_text(int index) {
    if (!s_scanned) rescan();
    if (index < 0 || index >= static_cast<int>(s_skins.size())) return "";
    static const char* const kOutfitName[kSkinOutfitCount] = {
        "hero's clothes", "Ordon", "Zora", "magic armor", "wolf"};
    std::string out;
    for (int o = 0; o < kSkinOutfitCount; ++o) {
        bool anyPart = false;
        for (int i = 0; i < kSkinPartCount; ++i) anyPart = anyPart || s_skins[index].has[o][i];
        if (!anyPart) continue;
        if (!out.empty()) out += ", ";
        out += kOutfitName[o];
    }
    return out.empty() ? "nothing usable" : out;
}

bool skins_can_open_folder() {
    return kCanOpenFolder;
}

void skins_open_folder() {
    if (!kCanOpenFolder) return;
#if COOP_HAS_FILE_MANAGER
    const std::string dir = skins_folder_path();
    if (dir.empty()) return;
#if defined(_WIN32)
    const std::string command = "explorer \"" + dir + "\"";
#elif defined(__APPLE__)
    const std::string command = "open \"" + dir + "\"";
#else
    const std::string command = "xdg-open \"" + dir + "\" &";
#endif
    std::system(command.c_str());
#endif
}

std::string skins_folder_path() {
    std::filesystem::path dir;
    if (!models_dir(dir)) return "";
    return dir.string();
}

ConfigVarHandle skins_files_var() {
    return s_filesVar;
}

void skins_files_changed() {
    apply_overlays();
}

const char* skins_slot_label(int slot) {
    if (slot < 0 || slot >= kSkinChoiceCount) return "";
    return kSlotLabel[slot];
}

std::string skins_local_slot(int slot) {
    if (slot < 0 || slot >= kSkinChoiceCount) return "";
    return cfg_string(s_slotVar[slot], "");
}

void skins_local_choices(SkinChoices* out) {
    if (out == nullptr) return;
    std::memset(out, 0, sizeof(*out));
    for (int i = 0; i < kSkinChoiceCount; ++i) {
        const Skin* skin = find(skins_local_slot(i).c_str());
        if (skin == nullptr) continue;
        std::strncpy(out->name[i], skin->name.c_str(), kSkinNameMax - 1);
        out->hash[i] = skin->hash;
    }
}

bool skins_all_same(const char* name) {
    const std::string want = name != nullptr ? name : "";
    for (int i = 0; i < kSkinChoiceCount; ++i) {

        if (i >= kSkinChoiceFirstItem) {
            if (!skins_local_slot(i).empty()) return false;
            continue;
        }
        if (skins_local_slot(i) != want) return false;
    }
    return true;
}

void after_choice_change() {
    forget_failures();
    apply_overlays();
    local_skin_rebuild_link();
    colors_invalidate_self();

    send_skin_choices();
}

void skins_set_local_slot(int slot, const char* name) {
    if (slot < 0 || slot >= kSkinChoiceCount || s_slotVar[slot] == 0 || svc_config == nullptr) {
        return;
    }
    svc_config->set_string(mod_ctx, s_slotVar[slot], name != nullptr ? name : "");
    after_choice_change();
}

void skins_set_local_all(const char* name) {
    if (svc_config == nullptr) return;
    for (int i = 0; i < kSkinChoiceCount; ++i) {
        if (s_slotVar[i] == 0) continue;

        const bool piece = i >= kSkinChoiceFirstItem;
        svc_config->set_string(mod_ctx, s_slotVar[i], (piece || name == nullptr) ? "" : name);
    }
    after_choice_change();
}

int skins_slot_for_outfit(int outfit) {
    switch (outfit) {
    case kSkinOutfitOrdon: return kSkinChoiceOrdon;
    case kSkinOutfitZora: return kSkinChoiceZora;
    case kSkinOutfitMagic: return kSkinChoiceMagic;
    case kSkinOutfitWolf: return kSkinChoiceWolf;
    default: return kSkinChoiceHero;
    }
}

bool skins_have(const char* name, uint32_t hash) {
    const Skin* skin = find(name);
    return skin != nullptr && skin->hash == hash;
}

J3DModelData* skins_part_data(const char* name, int outfit, int part) {
    if (part < 0 || part >= kSkinPartCount) return nullptr;
    if (outfit < 0 || outfit >= kSkinOutfitCount) return nullptr;
    const Skin* skin = find(name);
    if (skin == nullptr || !skin->has[outfit][part]) return nullptr;

    for (const LoadedPart& loaded : s_loadedParts) {
        if (loaded.part != part || loaded.outfit != outfit || loaded.skin != skin->name) continue;
        if (loaded.failed) return nullptr;
        return loaded.data;
    }

    const std::filesystem::path dir = kOutfitDir[outfit][0] == '\0'
                                          ? std::filesystem::path(skin->path)
                                          : std::filesystem::path(skin->path) / kOutfitDir[outfit];
    const std::string file = (dir / kPartFile[part]).string();
    LoadedPart loaded;
    loaded.skin = skin->name;
    loaded.outfit = outfit;
    loaded.part = part;
    loaded.data = loadBmdDataFromFile(file.c_str());
    loaded.failed = loaded.data == nullptr;
    s_loadedParts.push_back(loaded);
    if (loaded.failed) {
        coop_log::warn("coop_mod: [SKIN] '{}' {} did not load - that part stays Link's own",
            skin->name, kPartFile[part]);
        return nullptr;
    }
    return loaded.data;
}

J3DModel* skins_part_model(const char* name, int outfit, int part, float scale) {
    J3DModelData* data = skins_part_data(name, outfit, part);
    return data != nullptr ? modelFromData(data, cXyz(scale, scale, scale)) : nullptr;
}

J3DModelData* skins_local_cutscene_data(const char* file) {
    const std::string mine = skins_local_slot(skins_slot_for_outfit(local_skin_outfit()));
    if (mine.empty() || file == nullptr || file[0] == '\0') return nullptr;
    const Skin* skin = find(mine.c_str());
    if (skin == nullptr) return nullptr;

    for (const LoadedCutscene& loaded : s_loadedCutscenes) {
        if (loaded.skin != skin->name || loaded.file != file) continue;
        return loaded.failed ? nullptr : loaded.data;
    }
    const std::filesystem::path path =
        std::filesystem::path(skin->path) / "cutscene" / file;
    std::error_code ec;
    LoadedCutscene loaded;
    loaded.skin = skin->name;
    loaded.file = file;
    if (exists_ci(path, ec) && !ec) {
        loaded.data = loadBmdDataFromFile(path.string().c_str());
    }
    loaded.failed = loaded.data == nullptr;
    s_loadedCutscenes.push_back(loaded);
    return loaded.data;
}

bool skins_local_disabled() {
    return cfg_bool(s_noLocalSkinVar, false);
}

J3DModelData* skins_local_part_data(int outfit, int part) {
    if (skins_local_disabled()) return nullptr;
    const std::string mine = skins_local_slot(skins_slot_for_outfit(outfit));
    if (mine.empty()) return nullptr;
    return skins_part_data(mine.c_str(), outfit, part);
}

J3DModelData* skins_equipment_data(const char* name, const char* file) {
    if (file == nullptr || file[0] == '\0') return nullptr;
    const Skin* skin = find(name);
    if (skin == nullptr) return nullptr;
    const std::string key = std::string(skin->name) + "/equipment/" + file;
    for (const LoadedCutscene& loaded : s_loadedCutscenes) {
        if (loaded.skin != key) continue;
        return loaded.failed ? nullptr : loaded.data;
    }
    std::filesystem::path path = std::filesystem::path(skin->path) / "equipment" / file;
    std::error_code ec;
    LoadedCutscene loaded;
    loaded.skin = key;
    if (exists_ci(path, ec) && !ec) {
        loaded.data = loadBmdDataFromFile(path.string().c_str());
    }
    loaded.failed = loaded.data == nullptr;
    s_loadedCutscenes.push_back(loaded);
    return loaded.data;
}

bool skins_ships_equipment_file(const char* name, const char* file) {
    if (!s_scanned) rescan();
    const Skin* skin = find(name);
    if (skin == nullptr || file == nullptr) return false;
    std::error_code ec;
    const std::filesystem::path root(skin->path);
    if (exists_ci(root / "equipment" / file, ec) && !ec) return true;
    for (int o = 0; o < kSkinOutfitCount; ++o) {
        const std::filesystem::path dir =
            (kOutfitDir[o][0] == '\0') ? root : root / kOutfitDir[o];
        if (exists_ci(dir / file, ec) && !ec) return true;
    }
    return false;
}

struct AramFile {
    uint16_t index;
    const char* file;
    int slot;
};
const AramFile kAramFiles[] = {
    {0x310, "al_bottle.bmd", kSkinChoiceBottle},
    {0x31C, "o_gd_hk.bmd", kSkinChoiceBottle},
    {0x31D, "o_gd_nv.bmd", kSkinChoiceBottle},
    {0x31E, "o_gd_worm.bmd", kSkinChoiceBottle},
    {0x311, "al_ib.bmd", kSkinChoiceBallAndChain},
    {0x314, "al_bow.bmd", kSkinChoiceBow},
    {0x315, "al_crod.bmd", kSkinChoiceFishingRod},
    {0x316, "al_hs.bmd", kSkinChoiceHookshot},
    {0x317, "al_hs_kusari.bmd", kSkinChoiceHookshot},
    {0x318, "al_hs_tip.bmd", kSkinChoiceHookshot},
    {0x319, "al_pachi.bmd", kSkinChoiceSlingshot},
};

const char* skins_aram_file_for_index(uint16_t index) {
    for (const AramFile& entry : kAramFiles) {
        if (entry.index == index) return entry.file;
    }
    return nullptr;
}

J3DModelData* skins_local_aram_data(uint16_t index) {
    for (const AramFile& entry : kAramFiles) {
        if (entry.index != index) continue;
        std::string mine = skins_local_slot(entry.slot);
        if (mine.empty()) mine = skins_local_slot(kSkinChoiceEquipment);
        if (mine.empty()) return nullptr;
        return skins_equipment_data(mine.c_str(), entry.file);
    }
    return nullptr;
}

int skins_slot_for_equipment_file(const char* file) {
    if (file == nullptr) return kSkinChoiceEquipment;
    for (const EquipSlotFile& entry : kEquipSlotFiles) {
        if (std::strcmp(entry.file, file) == 0) return entry.slot;
    }
    return kSkinChoiceEquipment;
}

J3DModelData* skins_local_equipment_data(const char* file) {
    if (file == nullptr || file[0] == '\0') return nullptr;

    if (std::strcmp(file, "footmark.bmd") == 0) {
        const std::string worn = skins_local_slot(skins_slot_for_outfit(local_skin_outfit()));
        return worn.empty() ? nullptr : skins_equipment_data(worn.c_str(), file);
    }

    const int slot = skins_slot_for_equipment_file(file);
    std::string mine = skins_local_slot(slot);
    if (mine.empty() && slot != kSkinChoiceEquipment) {
        mine = skins_local_slot(kSkinChoiceEquipment);
    }
    if (mine.empty()) return nullptr;
    return skins_equipment_data(mine.c_str(), file);
}

bool skins_covers_outfit(const char* name, int outfit) {
    if (name == nullptr || name[0] == '\0') return true;
    if (outfit < 0 || outfit >= kSkinOutfitCount) return true;
    const Skin* skin = find(name);
    if (skin == nullptr) return true;
    for (int part = 0; part < kSkinPartCount; ++part) {
        if (skin->has[outfit][part]) return true;
    }
    return false;
}

bool skins_covers_slot(const char* name, int slot) {
    if (name == nullptr || name[0] == '\0') return true;
    const Skin* skin = find(name);
    if (skin == nullptr) return true;
    switch (slot) {
    case kSkinChoiceEquipment: return skin->hasEquipment;
    case kSkinChoiceCutscenes: return skin->hasCutscenes;
    case kSkinChoiceVoice: return skin->hasVoice;
    default: break;
    }

    if (slot >= kSkinChoiceFirstItem) {
        for (const EquipSlotFile& entry : kEquipSlotFiles) {
            if (entry.slot != slot) continue;
            if (skins_ships_equipment_file(name, entry.file)) return true;
        }

        for (const AramFile& entry : kAramFiles) {
            if (entry.slot != slot) continue;
            if (skins_ships_equipment_file(name, entry.file)) return true;
        }
        return false;
    }
    for (int outfit = 0; outfit < kSkinOutfitCount; ++outfit) {
        if (skins_slot_for_outfit(outfit) == slot) return skins_covers_outfit(name, outfit);
    }
    return true;
}

std::string slot_missing_text(int slot) {
    if (slot == kSkinChoiceWolf) return "This model does not have a wolf.";
    std::string what = skins_slot_label(slot);
    if (!what.empty()) what[0] = static_cast<char>(std::tolower(what[0]));
    return "This model does not have " + what + ".";
}

void skins_warn_update(int outfit) {

    static int s_lastOutfit = -1;
    static std::string s_lastName;
    if (outfit < 0 || outfit >= kSkinOutfitCount) return;
    const std::string name = skins_local_slot(skins_slot_for_outfit(outfit));
    if (outfit == s_lastOutfit && name == s_lastName) return;
    s_lastOutfit = outfit;
    s_lastName = name;

    if (name.empty() || skins_covers_outfit(name.c_str(), outfit)) return;

    const Skin* skin = find(name.c_str());
    const std::string title = skin != nullptr && !skin->title.empty() ? skin->title : name;
    coop_toast(title.c_str(), slot_missing_text(skins_slot_for_outfit(outfit)).c_str());
}

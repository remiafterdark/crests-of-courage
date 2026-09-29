

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "mods/svc/game_mode.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/host.h"
#include "mods/svc/save.h"
#include "mods/svc/ui.h"

#include "d/actor/d_a_alink.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

extern const HookService* svc_hook;
extern const HostService* svc_host;
extern const GameModeService* svc_game_mode;
extern const SaveService* svc_save;
extern const UiService* svc_ui;

#if defined(_WIN32)
DEFINE_HOOK_SYMBOL("dusk::mods::svc::`anonymous namespace'::ui_pane_add_control",
    ModResult(ModContext*, UiElementHandle, const UiControlDesc*, UiElementHandle*), RandoUiControl);
#else
DEFINE_HOOK_SYMBOL("dusk::mods::svc::(anonymous namespace)::ui_pane_add_control",
    ModResult(ModContext*, UiElementHandle, const UiControlDesc*, UiElementHandle*), RandoUiControl);
#endif
#if defined(_WIN32)
DEFINE_HOOK_SYMBOL("dusk::mods::svc::`anonymous namespace'::save_set_blob",
    ModResult(ModContext*, const char*, const void*, size_t), RandoSetBlob);
DEFINE_HOOK_SYMBOL("dusk::mods::svc::`anonymous namespace'::save_get_blob",
    ModResult(ModContext*, const char*, void*, size_t*), RandoGetBlob);
#else
DEFINE_HOOK_SYMBOL("dusk::mods::svc::(anonymous namespace)::save_set_blob",
    ModResult(ModContext*, const char*, const void*, size_t), RandoSetBlob);
DEFINE_HOOK_SYMBOL("dusk::mods::svc::(anonymous namespace)::save_get_blob",
    ModResult(ModContext*, const char*, void*, size_t*), RandoGetBlob);
#endif

DEFINE_HOOK_SYMBOL("dusk::gamemode::GameModeManager::setCurrentGameMode",
    bool(void*, std::string&), RandoSetModeHook);

namespace {

const char* const kRandoModeId = "randomizer_dev.twilitrealm.randomizer";
const char* const kRandoModId = "dev.twilitrealm.randomizer";
const char* const kOurModeId = "coop_rando";
const char* const kSeedBlob = "seed_hash";

struct ManagerView {
    std::string current;
    std::map<std::string, char> modes;
};
using SetModeFn = bool (*)(void* self, const std::string& id);

ManagerView* s_manager = nullptr;
SetModeFn s_setMode = nullptr;
bool s_resolved = false;

bool s_registered = false;
bool s_intent = false;

bool s_prompted = false;
int s_loadedTicks = 0;

bool s_titlePrompted = false;
int s_titleTicks = 0;
const int kPromptOnTitleTicks = 60;
const int kPromptAfterLoadTicks = 45;
uint32_t s_tick = 0;

std::string s_localSeed;

std::string s_announcedSeed;
uint32_t s_announcedRoster = 0;

struct SeedInfo {
    std::string hash;
    uint32_t size = 0;
    uint32_t crc = 0;
};
SeedInfo s_hostSeed;
bool s_warnedMismatch = false;
bool s_requested = false;
uint32_t s_requestTick = 0;

const uint32_t kSeedRetryTicks = 300;
std::vector<uint8_t> s_incoming;
uint32_t s_incomingCrc = 0;
uint32_t s_incomingGot = 0;

std::vector<uint8_t> s_incomingHave;

struct Outgoing {
    uint8_t to = kCoopNoPlayer;
    std::vector<uint8_t> data;
    uint32_t crc = 0;
    uint32_t offset = 0;
};
Outgoing s_outs[kCoopMaxPlayers];

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void resolve_once() {
    if (s_resolved || svc_hook == nullptr) return;
    s_resolved = true;
    void* addr = nullptr;

    static const char* const kManagerNames[] = {
        "dusk::gamemode::g_GameModeManager",
        "?g_GameModeManager@gamemode@dusk@@3VGameModeManager@12@A",
        "_ZN4dusk8gamemode17g_GameModeManagerE",
    };
    for (const char* name : kManagerNames) {
        if (s_manager != nullptr) break;
        addr = nullptr;
        if (svc_hook->resolve(mod_ctx, name, &addr, nullptr) == MOD_OK && addr != nullptr) {
            s_manager = static_cast<ManagerView*>(addr);
            coop_log::info("coop_mod: [RANDO] game mode manager is '{}'", name);
        }
    }
    addr = nullptr;
    if (svc_hook->resolve(mod_ctx, "dusk::gamemode::GameModeManager::setCurrentGameMode", &addr,
            nullptr) == MOD_OK) {
        s_setMode = reinterpret_cast<SetModeFn>(addr);
    }
    coop_log::info("coop_mod: [RANDO] game mode manager {}, switcher {}",
        s_manager != nullptr ? "found" : "NOT FOUND", s_setMode != nullptr ? "found" : "NOT FOUND");
}

bool rando_installed() {
    return s_manager != nullptr && s_manager->modes.find(kRandoModeId) != s_manager->modes.end();
}

std::filesystem::path seeds_dir() {
    const char* dir = nullptr;
    if (svc_host == nullptr || svc_host->data_dir(mod_ctx, &dir) != MOD_OK || dir == nullptr) {
        return {};
    }
    return std::filesystem::path(dir).parent_path() / kRandoModId / "seeds";
}

bool safe_hash(const std::string& hash) {
    if (hash.empty() || hash.size() >= sizeof(MsgRandoSeed::hash)) return false;
    for (char c : hash) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == ' ' || c == '-' || c == '_' || c == '\'';
        if (!ok) return false;
    }
    return hash.find("..") == std::string::npos;
}

bool read_seed(const std::string& hash, std::vector<uint8_t>& out) {
    if (!safe_hash(hash)) return false;
    const std::filesystem::path path = seeds_dir() / hash / "seed.dat";
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

bool have_seed(const SeedInfo& seed) {
    std::vector<uint8_t> data;
    return read_seed(seed.hash, data) && data.size() == seed.size &&
           crc32(data.data(), data.size()) == seed.crc;
}

void write_seed(const std::string& hash, const std::vector<uint8_t>& data) {
    const std::filesystem::path dir = seeds_dir() / hash;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path tmp = dir / "seed.dat.part";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            coop_log::warn("coop_mod: [RANDO] could not write the host's seed to {}", dir.string());
            return;
        }
    }
    std::filesystem::rename(tmp, dir / "seed.dat", ec);
    if (ec) {
        coop_log::warn("coop_mod: [RANDO] could not finish writing the host's seed: {}", ec.message());
        return;
    }
    coop_log::info("coop_mod: [RANDO] saved the host's seed '{}' ({} bytes)", hash, data.size());
    coop_notify_c(kNotifyRando, "Got the host's randomizer seed",
        "Start a new Randomizer file - it will use their seed.");
}

void capture_seed(const char* name, const void* data, size_t size) {
    if (name == nullptr || data == nullptr || size == 0 || std::strcmp(name, kSeedBlob) != 0) return;
    std::string hash(static_cast<const char*>(data), size);
    if (hash == s_localSeed) return;
    s_localSeed = hash;
    s_warnedMismatch = false;
    coop_log::info("coop_mod: [RANDO] this file plays seed '{}'", s_localSeed);
}

HookAction on_set_blob_pre(ModContext*, void* args, void*, void*) {
    capture_seed(mods::arg<const char*>(args, 1), mods::arg<const void*>(args, 2),
        mods::arg<size_t>(args, 3));
    return HOOK_CONTINUE;
}

bool s_answeredSize = false;

bool host_seed_ready();

void on_get_blob_post(ModContext*, void* args, void* retval, void*) {
    if (retval == nullptr) return;
    const char* name = mods::arg<const char*>(args, 1);
    void* buf = mods::arg<void*>(args, 2);
    size_t* size = mods::arg<size_t*>(args, 3);
    if (size == nullptr || name == nullptr || std::strcmp(name, kSeedBlob) != 0) return;
    auto* result = static_cast<ModResult*>(retval);
    if (host_seed_ready()) {
        const std::string& hash = s_hostSeed.hash;
        if (buf == nullptr) {
            *size = hash.size();
            *result = MOD_OK;
            s_answeredSize = true;
            return;
        }
        if (s_answeredSize) {
            s_answeredSize = false;
            std::memcpy(buf, hash.data(), hash.size());
            *size = hash.size();
            *result = MOD_OK;
            capture_seed(name, buf, *size);
            return;
        }
    }
    s_answeredSize = false;
    if (*result == MOD_OK && buf != nullptr) capture_seed(name, buf, *size);
}

void on_set_mode_post(ModContext*, void* args, void*, void*) {
    void* self = mods::arg<void*>(args, 0);
    if (self != nullptr && s_manager == nullptr) {
        s_manager = static_cast<ManagerView*>(self);
        coop_log::info("coop_mod: [RANDO] game mode manager found through a mode change");
    }
}

ModResult on_our_mode_activated(void*, ModError*) {
    version_remind();
    report_hint_arm();
    return MOD_OK;
}

void forward_to_rando();

ModResult on_our_mode_play(void*, ModError*) {
    s_intent = true;
    s_prompted = false;
    s_loadedTicks = 0;
    s_titlePrompted = false;
    s_titleTicks = 0;
    forward_to_rando();
    return MOD_OK;
}

ModResult on_our_mode_deactivated(void*, ModError*) { return MOD_OK; }

void register_our_mode() {
    if (s_registered || svc_game_mode == nullptr) return;
    GameModeDesc desc = {};
    desc.struct_size = sizeof(desc);
    desc.game_mode_id = kOurModeId;
    desc.full_name = "Co-op + Randomizer";
    std::strncpy(const_cast<char*>(desc.save_name), "coop-rando", sizeof(desc.save_name) - 1);
    desc.on_activated = on_our_mode_activated;
    desc.on_deactivated = on_our_mode_deactivated;
    desc.on_play = on_our_mode_play;
    if (svc_game_mode->register_game_mode(mod_ctx, &desc) == MOD_OK) {
        s_registered = true;
        coop_log::info("coop_mod: [RANDO] randomizer found - Co-op + Randomizer is on the menu");
    }
}

void forward_to_rando() {
    if (s_manager == nullptr || s_setMode == nullptr || !rando_installed()) {
        coop_notify_c(kNotifyRando, "Co-op + Randomizer", "Could not open the randomizer. Pick Randomizer yourself; "
                                    "co-op works there too.");
        return;
    }
    const bool ok = s_setMode(s_manager, std::string(kRandoModeId));
    coop_log::info("coop_mod: [RANDO] forwarded to the randomizer: {}", ok ? "ok" : "refused");
}

bool in_rando_mode() {
    return s_manager != nullptr && s_manager->current == kRandoModeId;
}

bool s_hostSeedFileOk = false;

bool host_seed_ready() {
    return coop_net_connected() && !coop_net_is_host() && in_rando_mode() &&
           !s_hostSeed.hash.empty() && s_hostSeedFileOk;
}

using InvokeFn = bool (*)(const void* self);
InvokeFn s_invokeSaveLoaded = nullptr;
int s_forceTries = 0;
std::string s_forcedFrom;

void force_host_seed() {
    if (!host_seed_ready() || s_localSeed.empty() || s_localSeed == s_hostSeed.hash) return;
    if (daAlink_getAlinkActorClass() == nullptr) return;
    if (s_forcedFrom == s_localSeed && s_forceTries >= 3) return;
    if (s_forcedFrom != s_localSeed) {
        s_forcedFrom = s_localSeed;
        s_forceTries = 0;
    }
    ++s_forceTries;
    if (s_invokeSaveLoaded == nullptr && svc_hook != nullptr) {
        void* addr = nullptr;
        if (svc_hook->resolve(mod_ctx, "dusk::gamemode::GameMode::invokeOnSaveLoadedFunction", &addr,
                nullptr) == MOD_OK) {
            s_invokeSaveLoaded = reinterpret_cast<InvokeFn>(addr);
        }
    }
    const auto it = s_manager->modes.find(kRandoModeId);
    if (s_invokeSaveLoaded == nullptr || it == s_manager->modes.end()) {
        coop_log::warn("coop_mod: [RANDO] cannot switch this file to the host's seed by itself");
        return;
    }

    const void* mode = &it->second;
    coop_log::info("coop_mod: [RANDO] this file was on '{}' - switching it to the host's '{}'",
        s_localSeed, s_hostSeed.hash);
    s_invokeSaveLoaded(mode);
    if (s_localSeed == s_hostSeed.hash) {
        coop_notify_c(kNotifyRando, "Playing the host's seed", ("Switched to \"" + s_hostSeed.hash + "\".").c_str());
    }
}

void announce_seed() {
    if (!coop_net_is_host() || !in_rando_mode() || s_localSeed.empty()) return;
    std::vector<uint8_t> data;
    if (!read_seed(s_localSeed, data)) return;
    MsgRandoSeed msg{};
    std::strncpy(msg.hash, s_localSeed.c_str(), sizeof(msg.hash) - 1);
    msg.size = static_cast<uint32_t>(data.size());
    msg.crc = crc32(data.data(), data.size());
    coop_net_send(kMsgRandoSeed, &msg, sizeof(msg));
}

void pump_one(Outgoing& s_out) {
    if (s_out.to == kCoopNoPlayer) return;
    if (!coop_net_player_present(s_out.to)) {
        s_out = Outgoing{};
        return;
    }
    const uint32_t kPerTick = 4;
    for (uint32_t i = 0; i < kPerTick && s_out.offset < s_out.data.size(); ++i) {
        uint8_t buf[sizeof(MsgRandoChunk) + kRandoChunkBytes];
        MsgRandoChunk head{};
        head.crc = s_out.crc;
        head.offset = s_out.offset;
        const uint32_t left = static_cast<uint32_t>(s_out.data.size()) - s_out.offset;
        head.length = static_cast<uint16_t>(left < kRandoChunkBytes ? left : kRandoChunkBytes);
        std::memcpy(buf, &head, sizeof(head));
        std::memcpy(buf + sizeof(head), s_out.data.data() + s_out.offset, head.length);
        coop_net_send_to(s_out.to, kMsgRandoChunk, buf, sizeof(head) + head.length);
        s_out.offset += head.length;
    }
    if (s_out.offset >= s_out.data.size()) s_out = Outgoing{};
}

void pump_outgoing() {
    for (Outgoing& out : s_outs) pump_one(out);
}

void request_seed(const SeedInfo& seed) {
    s_requested = true;
    s_requestTick = s_tick;
    s_incoming.assign(seed.size, 0);
    s_incomingHave.assign((seed.size + kRandoChunkBytes - 1) / kRandoChunkBytes, 0);
    s_incomingCrc = seed.crc;
    s_incomingGot = 0;
    MsgRandoSeedRequest req{};
    std::strncpy(req.hash, seed.hash.c_str(), sizeof(req.hash) - 1);
    coop_net_send_to(kCoopHostId, kMsgRandoSeedRequest, &req, sizeof(req));
    coop_log::info("coop_mod: [RANDO] asking the host for seed '{}'", seed.hash);
}

void on_seed_announced(const MsgRandoSeed& msg) {
    SeedInfo seed;
    seed.hash.assign(msg.hash, strnlen(msg.hash, sizeof(msg.hash)));
    seed.size = msg.size;
    seed.crc = msg.crc;
    if (!safe_hash(seed.hash) || seed.size == 0 || seed.size > kRandoSeedMaxBytes) return;
    const bool changed = seed.hash != s_hostSeed.hash || seed.crc != s_hostSeed.crc;
    s_hostSeed = seed;
    if (changed) {
        s_requested = false;
        s_warnedMismatch = false;
        coop_log::info("coop_mod: [RANDO] the host plays seed '{}'", seed.hash);
    }
    s_hostSeedFileOk = have_seed(seed);
    if (!s_requested && !s_hostSeedFileOk) request_seed(seed);

    if (in_rando_mode() && !s_localSeed.empty() && s_localSeed != seed.hash && !s_warnedMismatch &&
        s_forceTries >= 3) {
        s_warnedMismatch = true;
        coop_notify_c(kNotifyRando, "Different randomizer seed",
            ("The host is on \"" + seed.hash + "\". Start a new Randomizer file to play "
             "together - it will use their seed.").c_str());
    }
}

void on_chunk(const uint8_t* payload, size_t size) {
    if (size < sizeof(MsgRandoChunk) || s_incoming.empty()) return;
    MsgRandoChunk head;
    std::memcpy(&head, payload, sizeof(head));
    if (head.crc != s_incomingCrc || size < sizeof(head) + head.length) return;
    if (static_cast<size_t>(head.offset) + head.length > s_incoming.size()) return;
    if (head.offset % kRandoChunkBytes != 0) return;
    const size_t chunk = head.offset / kRandoChunkBytes;
    if (chunk >= s_incomingHave.size()) return;
    std::memcpy(s_incoming.data() + head.offset, payload + sizeof(head), head.length);
    if (s_incomingHave[chunk] == 0) {
        s_incomingHave[chunk] = 1;
        s_incomingGot += head.length;
    }
    s_requestTick = s_tick;
    if (s_incomingGot < s_incoming.size()) return;
    if (crc32(s_incoming.data(), s_incoming.size()) != s_incomingCrc) {
        coop_log::warn("coop_mod: [RANDO] the host's seed arrived damaged - asking again");
        s_requested = false;
    } else {
        write_seed(s_hostSeed.hash, s_incoming);
        s_hostSeedFileOk = have_seed(s_hostSeed);
        s_requested = false;
    }
    s_incoming.clear();
    s_incomingHave.clear();
}

struct SeedPicker {
    ModContext* ctx = nullptr;
    UiControlSetFn set = nullptr;
    void* setData = nullptr;
    std::vector<std::string> options;
    UiPressedFn start = nullptr;
    UiPredicateFn startDisabled = nullptr;
    void* startData = nullptr;
    uint32_t tick = 0;
    bool done = false;
};
SeedPicker s_picker;

const uint32_t kPickAfterTicks = 20;
const uint32_t kPickWithinTicks = 120;

void on_ui_control_post(ModContext*, void* args, void*, void*) {
    ModContext* ctx = mods::arg<ModContext*>(args, 0);
    const UiControlDesc* d = mods::arg<const UiControlDesc*>(args, 2);
    if (ctx == nullptr || ctx == mod_ctx || d == nullptr || d->label == nullptr) return;
    if (d->kind == UI_CONTROL_SELECT && d->binding == UI_BINDING_CALLBACKS && d->set != nullptr &&
        d->options != nullptr && std::strcmp(d->label, "Selected Seed") == 0) {
        s_picker = SeedPicker{};
        s_picker.ctx = ctx;
        s_picker.set = d->set;
        s_picker.setData = d->user_data;
        for (size_t i = 0; i < d->option_count; ++i) {
            s_picker.options.emplace_back(d->options[i] != nullptr ? d->options[i] : "");
        }
        s_picker.tick = s_tick;
    } else if (d->kind == UI_CONTROL_BUTTON && d->on_pressed != nullptr && ctx == s_picker.ctx &&
               std::strcmp(d->label, "Start Randomizer") == 0) {
        s_picker.start = d->on_pressed;
        s_picker.startDisabled = d->is_disabled;
        s_picker.startData = d->user_data;
    }
}

void drive_seed_picker() {
    if (s_picker.ctx == nullptr || s_picker.done) return;
    const uint32_t age = s_tick - s_picker.tick;
    if (age < kPickAfterTicks) return;
    if (age > kPickWithinTicks) {
        s_picker.done = true;
        return;
    }
    if (!s_intent || !coop_net_connected() || coop_net_is_host() || !in_rando_mode()) {
        s_picker.done = true;
        return;
    }
    s_picker.done = true;
    if (s_hostSeed.hash.empty()) {
        coop_notify_c(kNotifyRando, "Waiting for the host",
            "They haven't loaded their randomizer file yet. Back out, and start the new file again "
            "once they have.");
        return;
    }
    const auto it = std::find(s_picker.options.begin(), s_picker.options.end(), s_hostSeed.hash);
    if (!s_hostSeedFileOk || it == s_picker.options.end()) {
        coop_notify_c(kNotifyRando, "The host's seed isn't here yet",
            "Back out, and start the new file again in a moment.");
        coop_log::info("coop_mod: [RANDO] seed window opened without the host's '{}' in it",
            s_hostSeed.hash);
        return;
    }
    UiControlValue value = UI_CONTROL_VALUE_INIT;
    value.int_value = static_cast<int64_t>(it - s_picker.options.begin());
    s_picker.set(s_picker.ctx, s_picker.setData, &value);
    coop_log::info("coop_mod: [RANDO] picked the host's seed '{}' in the randomizer's window",
        s_hostSeed.hash);
    if (s_picker.start == nullptr ||
        (s_picker.startDisabled != nullptr && s_picker.startDisabled(s_picker.ctx, s_picker.startData))) {
        coop_notify_c(kNotifyRando, "Host's seed selected", "Press Start Randomizer.");
        return;
    }
    s_picker.start(s_picker.ctx, s_picker.startData);
    coop_log::info("coop_mod: [RANDO] started the new file on it");
    coop_notify_c(kNotifyRando, "Playing the host's seed", ("\"" + s_hostSeed.hash + "\"").c_str());
}

}

template <class Entry>
void target_or(void* fallback) {
    if (Entry::target == nullptr && Entry::resolved_target() == nullptr && fallback != nullptr) {
        Entry::target = fallback;
    }
}

void rando_init() {
    if (svc_hook == nullptr) return;
    if (svc_save != nullptr) {
        target_or<RandoSetBlob>(reinterpret_cast<void*>(svc_save->set_blob));
        target_or<RandoGetBlob>(reinterpret_cast<void*>(svc_save->get_blob));
    }
    if (svc_ui != nullptr) target_or<RandoUiControl>(reinterpret_cast<void*>(svc_ui->pane_add_control));
    bool set = false;
    bool get = false;
    set = mods::hook::add_pre<RandoSetBlob>(on_set_blob_pre) == MOD_OK;
    get = mods::hook::add_post<RandoGetBlob>(on_get_blob_post) == MOD_OK;
    const bool mode = mods::hook::add_post<RandoSetModeHook>(on_set_mode_post) == MOD_OK;
    const bool ui = mods::hook::add_post<RandoUiControl>(on_ui_control_post) == MOD_OK;
    coop_log::info("coop_mod: [RANDO] seed watch {}, mode watch {}, seed picker {}",
        set && get ? "attached" : "FAILED", mode ? "attached" : "FAILED",
        ui ? "attached" : "FAILED");
    resolve_once();
}

void rando_update() {
    ++s_tick;
    resolve_once();
    if (!s_registered && s_tick % 30 == 0 && rando_installed()) register_our_mode();
    if (s_intent && !s_titlePrompted && in_rando_mode()) {
        if (!coop_on_title_screen()) {
            s_titleTicks = 0;
        } else if (++s_titleTicks >= kPromptOnTitleTicks) {
            s_titlePrompted = true;
            if (!coop_net_connected() && !coop_net_connecting()) game_mode_prompt_connect();
        }
    }
    drive_seed_picker();
    if (s_intent && !s_prompted && in_rando_mode() && !coop_on_title_screen()) {
        if (daAlink_getAlinkActorClass() == nullptr) {
            s_loadedTicks = 0;
        } else if (++s_loadedTicks >= kPromptAfterLoadTicks) {
            s_prompted = true;
            if (!coop_net_connected() && !coop_net_connecting()) game_mode_prompt_connect();
        }
    }
    if (!in_rando_mode()) {

        if (s_manager != nullptr && s_manager->current.rfind(kOurModeId, 0) != 0) s_intent = false;
        s_localSeed.clear();
    }
    if (!coop_net_connected()) {
        s_announcedRoster = 0;
        s_announcedSeed.clear();
        s_hostSeedFileOk = false;
        s_forceTries = 0;
        s_forcedFrom.clear();
        s_hostSeed = SeedInfo{};
        s_requested = false;
        for (Outgoing& out : s_outs) out = Outgoing{};
        s_incoming.clear();
        s_incomingHave.clear();
        return;
    }

    if (s_requested && !s_hostSeedFileOk && !s_hostSeed.hash.empty() &&
        s_tick - s_requestTick > kSeedRetryTicks) {
        coop_log::warn("coop_mod: [RANDO] the host's seed stopped arriving - asking again");
        request_seed(s_hostSeed);
    }
    const uint32_t roster = coop_net_roster();
    if (s_tick % 300 == 0 || roster != s_announcedRoster || s_localSeed != s_announcedSeed) {
        s_announcedRoster = roster;
        s_announcedSeed = s_localSeed;
        announce_seed();
    }
    pump_outgoing();
    if (s_tick % 30 == 0) force_host_seed();
}

void rando_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from) {
    if (type == kMsgRandoSeed && size >= sizeof(MsgRandoSeed) && !coop_net_is_host()) {
        MsgRandoSeed msg;
        std::memcpy(&msg, payload, sizeof(msg));
        on_seed_announced(msg);
    } else if (type == kMsgRandoSeedRequest && size >= sizeof(MsgRandoSeedRequest) &&
               coop_net_is_host() && from < kCoopMaxPlayers) {
        MsgRandoSeedRequest req;
        std::memcpy(&req, payload, sizeof(req));
        const std::string hash(req.hash, strnlen(req.hash, sizeof(req.hash)));
        Outgoing out;
        if (hash != s_localSeed || !read_seed(hash, out.data)) return;
        out.to = from;
        out.crc = crc32(out.data.data(), out.data.size());
        s_outs[from] = std::move(out);
        coop_log::info("coop_mod: [RANDO] sending seed '{}' to player {}", hash, from);
    } else if (type == kMsgRandoChunk && !coop_net_is_host()) {
        on_chunk(payload, size);
    }
}

bool rando_active() {
    return in_rando_mode();
}

bool rando_join_sync_wait() {
    if (!in_rando_mode() || coop_net_is_host() || s_hostSeed.hash.empty()) return false;
    if (!s_hostSeedFileOk) return true;
    return !s_localSeed.empty() && s_localSeed != s_hostSeed.hash && s_forceTries < 3;
}

bool rando_join_sync_allowed() {
    if (!in_rando_mode()) return true;

    if (s_hostSeed.hash.empty() || s_localSeed.empty()) return true;
    return s_hostSeed.hash == s_localSeed;
}

void rando_debug_enter_randomizer() {
    resolve_once();
    forward_to_rando();
}

void rando_debug_set_local_seed(const char* hash) {
    s_localSeed = hash != nullptr ? hash : "";
}

std::string rando_debug_local_seed() {
    return s_localSeed;
}

std::string rando_debug_any_seed() {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(seeds_dir(), ec)) {
        if (!entry.is_directory()) continue;
        std::error_code fec;
        if (std::filesystem::exists(entry.path() / "seed.dat", fec)) return entry.path().filename().string();
    }
    return "";
}

bool rando_debug_host_seed_ready() {
    return host_seed_ready();
}

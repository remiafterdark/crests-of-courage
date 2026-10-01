

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"
#include "util.hpp"

#include "mods/svc/config.h"
#include "mods/svc/game_mode.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/host.h"
#include "mods/svc/item.h"
#include "mods/svc/save.h"

#include "d/actor/d_a_alink.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

extern const ConfigService* svc_config;
extern const GameModeService* svc_game_mode;
extern const HookService* svc_hook;
extern const HostService* svc_host;
extern const ItemService* svc_item;
extern const SaveService* svc_save;
extern const UiService* svc_ui;

#if defined(_WIN32)
DEFINE_HOOK_SYMBOL("dusk::mods::svc::`anonymous namespace'::ui_pane_add_control",
    ModResult(ModContext*, UiElementHandle, const UiControlDesc*, UiElementHandle*), RandoUiControl);
#else
DEFINE_HOOK_SYMBOL("dusk::mods::svc::(anonymous namespace)::ui_pane_add_control",
    ModResult(ModContext*, UiElementHandle, const UiControlDesc*, UiElementHandle*), RandoUiControl);
#endif

namespace {

const char* const kRandoModId = "dev.twilitrealm.randomizer";

const char* const kRandoModeId = "randomizer_dev.twilitrealm.randomizer";
const char* const kOurModeId = "coop_rando";
const char* const kSeedBlob = "seed_hash";

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

std::filesystem::path seeds_dir();
bool safe_hash(const std::string& hash);

bool s_hostSeedFileOk = false;

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
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
        coop_log::warn("coop_mod: [RANDO] seed write failed: {}", ec.message());
        return;
    }
    coop_log::info("coop_mod: [RANDO] saved the host's seed '{}' ({} bytes)", hash, data.size());
    coop_notify_c(kNotifyRando, "Got the host's seed", ("New Randomizer file on " + hash).c_str());
}

const char* const kProbeChecks[] = {
    "chest:D_MN01:1",  "chest:D_MN04:30", "chest:D_MN07:19", "chest:D_MN09:17",
    "chest:D_MN10B:11", "chest:D_SB05:0", "chest:F_SP115:12", "chest:F_SP124:5",
    "freestanding:F_SP109:133", "freestanding:F_SP114:139", "freestanding:F_SP121:155",
    "poe:D_SB07:15", "sky:F_SP108:8",
};
bool s_seedActive = false;

bool probe_seed_active() {
    if (svc_item == nullptr || svc_item->resolve_check_full == nullptr) return false;
    for (const char* check : kProbeChecks) {
        ItemCheckResolution r{};
        if (svc_item->resolve_check_full(mod_ctx, check, 0xFF, &r) == MOD_OK && r.was_resolved) {
            return true;
        }
    }
    return false;
}

bool rando_installed() {
    std::error_code ec;
    return !seeds_dir().empty() && std::filesystem::is_directory(seeds_dir().parent_path(), ec);
}

int s_slot = -1;
std::filesystem::path s_sidecar;
uint32_t s_sidecarSearchTick = 0;

std::filesystem::path user_dir() {
    const char* dir = nullptr;
    if (svc_host == nullptr || svc_host->data_dir(mod_ctx, &dir) != MOD_OK || dir == nullptr) {
        return {};
    }
    return std::filesystem::path(dir).parent_path().parent_path();
}

std::filesystem::path find_sidecar() {
    const std::filesystem::path root = user_dir();
    if (root.empty()) return {};
    const std::string want = std::string(kRandoModId) + ".json";
    std::filesystem::path best;
    std::filesystem::file_time_type bestTime{};
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 4) {
            it.disable_recursion_pending();
            continue;
        }
        const std::filesystem::path& p = it->path();
        if (it->is_directory(ec)) {
            const std::string name = p.filename().string();

            if (name == "mods" || name == "mod_data" || name == "logs" || name == "texture_dumps" ||
                name == "texture_replacements") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (p.filename().string() != want) continue;
        if (p.parent_path().string().find(".mods") == std::string::npos) continue;
        const auto when = std::filesystem::last_write_time(p, ec);
        if (ec) continue;
        if (best.empty() || when > bestTime) {
            best = p;
            bestTime = when;
        }
    }
    return best;
}

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

std::string b64_decode(const std::string& in) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        const int v = b64_value(c);
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

std::string seed_from_sidecar(const std::filesystem::path& path, int slot) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t at = 0;
    for (int i = 0; i <= slot; ++i) {
        at = json.find("\"blobs\"", at);
        if (at == std::string::npos) return {};
        if (i < slot) at += 7;
    }
    const size_t open = json.find('{', at);
    const size_t close = open == std::string::npos ? std::string::npos : json.find('}', open);
    if (close == std::string::npos) return {};
    const std::string blobs = json.substr(open, close - open);
    const size_t key = blobs.find(std::string("\"") + kSeedBlob + "\"");
    if (key == std::string::npos) return {};
    const size_t q1 = blobs.find('"', blobs.find(':', key) + 1);
    const size_t q2 = q1 == std::string::npos ? std::string::npos : blobs.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return b64_decode(blobs.substr(q1 + 1, q2 - q1 - 1));
}

void read_local_seed() {
    if (s_slot < 0) return;
    if (s_sidecar.empty() || !std::filesystem::exists(s_sidecar)) {
        if (s_sidecarSearchTick != 0 && s_tick - s_sidecarSearchTick < 600) return;
        s_sidecarSearchTick = s_tick == 0 ? 1 : s_tick;
        s_sidecar = find_sidecar();
        if (s_sidecar.empty()) return;
        coop_log::info("coop_mod: [RANDO] the randomizer's save data is {}", s_sidecar.string());
    }
    const std::string hash = seed_from_sidecar(s_sidecar, s_slot);
    if (hash.empty() || !safe_hash(hash) || hash == s_localSeed) return;
    s_localSeed = hash;
    s_warnedMismatch = false;
    coop_log::info("coop_mod: [RANDO] this file plays seed '{}'", s_localSeed);
}

const char kRandoStages[78][8] = {"D_MN01", "D_MN01A", "D_MN01B", "D_MN04", "D_MN04A", "D_MN04B",
    "D_MN05", "D_MN05A", "D_MN05B", "D_MN06", "D_MN06A", "D_MN06B", "D_MN07", "D_MN07A", "D_MN07B",
    "D_MN08", "D_MN08A", "D_MN08B", "D_MN08C", "D_MN08D", "D_MN09", "D_MN09A", "D_MN09B", "D_MN09C",
    "D_MN10", "D_MN10A", "D_MN10B", "D_MN11", "D_MN11A", "D_MN11B", "D_SB00", "D_SB01", "D_SB02",
    "D_SB03", "D_SB04", "D_SB05", "D_SB06", "D_SB07", "D_SB08", "D_SB09", "D_SB10", "F_SP00",
    "F_SP102", "F_SP103", "F_SP104", "F_SP108", "F_SP109", "F_SP110", "F_SP111", "F_SP112", "F_SP113",
    "F_SP114", "F_SP115", "F_SP116", "F_SP117", "F_SP118", "F_SP121", "F_SP122", "F_SP123", "F_SP124",
    "F_SP125", "F_SP126", "F_SP127", "F_SP128", "F_SP200", "R_SP01", "R_SP107", "R_SP108", "R_SP109",
    "R_SP110", "R_SP116", "R_SP127", "R_SP128", "R_SP160", "R_SP161", "R_SP209", "R_SP300", "R_SP301"};
const int kSeedSampleChests = 24;
bool s_matchTried = false;

float seed_match(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return 0.0f;
    std::string line;
    bool inChests = false;
    int sampled = 0;
    int matched = 0;
    while (sampled < kSeedSampleChests && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != ' ') {
            if (inChests) break;
            inChests = line.rfind("mTreasureChestOverrides:", 0) == 0;
            continue;
        }
        if (!inChests) continue;
        unsigned key = 0;
        unsigned item = 0;
        if (std::sscanf(line.c_str(), " %u: %u", &key, &item) != 2) continue;
        const unsigned stage = key >> 8;
        if (stage >= 78) continue;
        char check[32];
        std::snprintf(check, sizeof(check), "chest:%s:%u", kRandoStages[stage], key & 0xFF);
        ItemCheckResolution r{};
        if (svc_item->resolve_check_full(mod_ctx, check, 0xFF, &r) != MOD_OK || !r.was_resolved) {
            continue;
        }
        ++sampled;
        if (r.item == item) ++matched;
    }
    return sampled >= 8 ? static_cast<float>(matched) / static_cast<float>(sampled) : 0.0f;
}

void match_local_seed() {
    if (!s_seedActive || !s_localSeed.empty() || svc_item == nullptr ||
        svc_item->resolve_check_full == nullptr) {
        return;
    }
    s_matchTried = true;
    std::string best;
    float bestScore = 0.0f;
    float second = 0.0f;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(seeds_dir(), ec)) {
        if (!entry.is_directory()) continue;
        const float score = seed_match(entry.path() / "seed.dat");
        if (score > bestScore) {
            second = bestScore;
            bestScore = score;
            best = entry.path().filename().string();
        } else if (score > second) {
            second = score;
        }
    }

    if (bestScore >= 0.4f && bestScore - second >= 0.3f && safe_hash(best)) {
        s_localSeed = best;
        coop_log::info("coop_mod: [RANDO] seed '{}' identified from chests score={:.2f}", best,
            bestScore);
    } else {
        coop_log::info("coop_mod: [RANDO] seed not identified best={:.2f} second={:.2f}", bestScore,
            second);
    }
}

void on_save_event(ModContext*, uint32_t slot, void*) {
    s_slot = static_cast<int>(slot);
    s_localSeed.clear();
    s_matchTried = false;
    read_local_seed();
}

void on_new_save(ModContext*, uint32_t slot, void*) {
    s_slot = static_cast<int>(slot);
    s_matchTried = false;
    s_localSeed.clear();
}

std::string s_hostToldSeed;

void announce_seed() {
    if (!coop_net_is_host() || !s_seedActive || s_localSeed.empty()) return;
    std::vector<uint8_t> data;
    if (!read_seed(s_localSeed, data)) return;
    if (s_hostToldSeed != s_localSeed) {
        s_hostToldSeed = s_localSeed;
        coop_notify_c(kNotifyRando, "Seed shared", s_localSeed.c_str());
    }
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

    if (!rando_installed()) return;
    s_hostSeedFileOk = have_seed(seed);
    if (!s_requested && !s_hostSeedFileOk) request_seed(seed);

    if (changed && s_hostSeedFileOk) {
        if (s_seedActive && s_localSeed == seed.hash) {
            coop_notify_c(kNotifyRando, "On the host's seed", seed.hash.c_str());
        } else if (!s_seedActive) {
            coop_notify_c(kNotifyRando, "Host's seed", ("New Randomizer file on " + seed.hash).c_str());
        }
    }

    if (s_seedActive && !s_localSeed.empty() && s_localSeed != seed.hash && !s_warnedMismatch) {
        s_warnedMismatch = true;
        coop_notify_c(kNotifyRando, "Wrong seed", ("Host is on " + seed.hash).c_str());
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
        coop_log::warn("coop_mod: [RANDO] host seed corrupt, requesting again");
        s_requested = false;
    } else {
        write_seed(s_hostSeed.hash, s_incoming);
        s_hostSeedFileOk = have_seed(s_hostSeed);
        s_requested = false;
    }
    s_incoming.clear();
    s_incomingHave.clear();
}

struct ManagerView {
    std::string current;
    std::map<std::string, char> modes;
};
using SetModeFn = bool (*)(void* self, const std::string& id);
ManagerView* s_manager = nullptr;
SetModeFn s_setMode = nullptr;
bool s_resolved = false;
bool s_registered = false;
struct SeedPicker {
    ModContext* ctx = nullptr;
    UiControlSetFn set = nullptr;
    void* setData = nullptr;
    std::vector<std::string> options;
    UiPressedFn start = nullptr;
    UiPredicateFn startDisabled = nullptr;
    void* startData = nullptr;
    uint32_t tick = 0;
    bool pending = false;
};
SeedPicker s_picker;
bool s_pickerAttached = false;
std::string s_pickerStarted;
const uint32_t kPickDelayTicks = 20;

void on_ui_control_post(ModContext*, void* args, void* retval, void*) {
    if (args == nullptr || retval == nullptr || *static_cast<ModResult*>(retval) != MOD_OK) return;
    ModContext* ctx = mods::arg<ModContext*>(args, 0);
    const UiControlDesc* d = mods::arg<const UiControlDesc*>(args, 2);
    if (ctx == nullptr || ctx == mod_ctx || d == nullptr || d->label == nullptr) return;
    if (d->kind == UI_CONTROL_BUTTON && s_picker.pending && ctx == s_picker.ctx &&
        s_tick == s_picker.tick && d->on_pressed != nullptr &&
        std::strcmp(d->label, "Start Randomizer") == 0) {
        s_picker.start = d->on_pressed;
        s_picker.startDisabled = d->is_disabled;
        s_picker.startData = d->user_data;
        return;
    }
    if (d->kind != UI_CONTROL_SELECT || d->binding != UI_BINDING_CALLBACKS || d->set == nullptr ||
        d->options == nullptr || d->option_count == 0 || d->option_count > 4096) {
        return;
    }
    if (std::strcmp(d->label, "Selected Seed") != 0) return;
    if (svc_host == nullptr || std::strcmp(svc_host->mod_id(ctx), kRandoModId) != 0) return;
    s_picker = SeedPicker{};
    s_picker.ctx = ctx;
    s_picker.set = d->set;
    s_picker.setData = d->user_data;
    for (size_t i = 0; i < d->option_count; ++i) {
        s_picker.options.emplace_back(d->options[i] != nullptr ? d->options[i] : "");
    }
    s_picker.tick = s_tick;
    s_picker.pending = true;
}

void pick_host_seed() {
    if (!s_picker.pending || s_tick - s_picker.tick < kPickDelayTicks) return;
    s_picker.pending = false;
    if (!coop_net_connected() || coop_net_is_host() || s_hostSeed.hash.empty()) return;
    const auto it = std::find(s_picker.options.begin(), s_picker.options.end(), s_hostSeed.hash);
    if (!s_hostSeedFileOk || it == s_picker.options.end()) {
        coop_log::info("coop_mod: [RANDO] picker missing host seed '{}'", s_hostSeed.hash);
        coop_notify_c(kNotifyRando, "Host's seed", ("Pick " + s_hostSeed.hash).c_str());
        return;
    }
    UiControlValue value = UI_CONTROL_VALUE_INIT;
    value.int_value = static_cast<int64_t>(it - s_picker.options.begin());
    s_picker.set(s_picker.ctx, s_picker.setData, &value);
    coop_log::info("coop_mod: [RANDO] picker selected '{}'", s_hostSeed.hash);
    const bool canStart = s_picker.start != nullptr && s_pickerStarted != s_hostSeed.hash &&
        (s_picker.startDisabled == nullptr ||
         !s_picker.startDisabled(s_picker.ctx, s_picker.startData));
    if (!canStart) {
        coop_notify_c(kNotifyRando, "Host's seed selected", s_hostSeed.hash.c_str());
        return;
    }
    s_pickerStarted = s_hostSeed.hash;
    s_picker.start(s_picker.ctx, s_picker.startData);
    coop_log::info("coop_mod: [RANDO] picker pressed start");
    coop_notify_c(kNotifyRando, "Host's seed", s_hostSeed.hash.c_str());
}

bool s_intent = false;
bool s_titlePrompted = false;
int s_titleTicks = 0;
bool s_prompted = false;
int s_loadedTicks = 0;
const int kPromptOnTitleTicks = 60;
const int kPromptAfterLoadTicks = 45;

ConfigVarHandle s_lastUsedVar = 0;
bool s_highlightRestored = false;

void resolve_mode_switch() {
    if (s_resolved || svc_hook == nullptr) return;
    s_resolved = true;
    static const char* const kManagerNames[] = {
        "dusk::gamemode::g_GameModeManager",
        "?g_GameModeManager@gamemode@dusk@@3VGameModeManager@12@A",
        "_ZN4dusk8gamemode17g_GameModeManagerE",
    };
    for (const char* name : kManagerNames) {
        void* addr = nullptr;
        if (svc_hook->resolve(mod_ctx, name, &addr, nullptr) == MOD_OK && addr != nullptr) {
            s_manager = static_cast<ManagerView*>(addr);
            break;
        }
    }
    void* addr = nullptr;
    if (svc_hook->resolve(mod_ctx, "dusk::gamemode::GameModeManager::setCurrentGameMode", &addr,
            nullptr) == MOD_OK) {
        s_setMode = reinterpret_cast<SetModeFn>(addr);
    }
    coop_log::info("coop_mod: [RANDO] mode switch manager={} switcher={}", s_manager != nullptr,
        s_setMode != nullptr);
}

bool in_rando_mode() {
    return s_manager != nullptr && s_manager->current == kRandoModeId;
}

void forward_to_rando() {
    if (s_manager == nullptr || s_setMode == nullptr ||
        s_manager->modes.find(kRandoModeId) == s_manager->modes.end()) {
        coop_notify_c(kNotifyRando, "Co-op + Randomizer", "Couldn't open it. Pick Randomizer instead.");
        return;
    }
    const bool ok = s_setMode(s_manager, std::string(kRandoModeId));
    coop_log::info("coop_mod: [RANDO] switched to randomizer mode ok={}", ok);
}

ModResult on_our_mode_activated(void*, ModError*) {
    report_hint_arm();
    return MOD_OK;
}

ModResult on_our_mode_play(void*, ModError*) {
    s_intent = true;
    if (svc_config != nullptr && s_lastUsedVar != 0) svc_config->set_bool(mod_ctx, s_lastUsedVar, true);
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
        coop_log::info("coop_mod: [RANDO] Co-op + Randomizer mode registered");
    }
}

void restore_highlight() {
    if (s_highlightRestored || !s_registered || s_manager == nullptr || s_setMode == nullptr) return;
    s_highlightRestored = true;
    if (!cfg_bool(s_lastUsedVar, false) || s_manager->current != kRandoModeId) return;
    if (daAlink_getAlinkActorClass() != nullptr || svc_host == nullptr) return;
    std::string ours = std::string(kOurModeId) + "_" + svc_host->mod_id(mod_ctx);
    for (char& c : ours) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s_manager->modes.find(ours) == s_manager->modes.end()) return;
    s_setMode(s_manager, ours);
    coop_log::info("coop_mod: [RANDO] startup mode set back to Co-op + Randomizer");
}

void update_mode_prompts() {
    resolve_mode_switch();
    if (!s_registered && s_tick % 30 == 0 && rando_installed()) register_our_mode();
    restore_highlight();

    if (!s_intent && in_rando_mode() && daAlink_getAlinkActorClass() != nullptr &&
        !coop_on_title_screen() && cfg_bool(s_lastUsedVar, false) && svc_config != nullptr) {
        svc_config->set_bool(mod_ctx, s_lastUsedVar, false);
    }
    if (!s_intent) return;
    if (!in_rando_mode()) {

        if (s_manager != nullptr && s_manager->current.rfind(kOurModeId, 0) != 0) s_intent = false;
        return;
    }
    if (!s_titlePrompted) {
        if (!coop_on_title_screen()) {
            s_titleTicks = 0;
        } else if (++s_titleTicks >= kPromptOnTitleTicks) {
            s_titlePrompted = true;
            if (!coop_net_connected() && !coop_net_connecting()) game_mode_prompt_connect();
        }
    }
    if (!s_prompted && !coop_on_title_screen()) {
        if (daAlink_getAlinkActorClass() == nullptr) {
            s_loadedTicks = 0;
        } else if (++s_loadedTicks >= kPromptAfterLoadTicks) {
            s_prompted = true;
            if (!coop_net_connected() && !coop_net_connecting()) game_mode_prompt_connect();
        }
    }
}

}

void rando_init() {
    if (svc_config != nullptr) {
        ConfigVarDesc last = CONFIG_VAR_DESC_INIT;
        last.name = "coop_rando_last_used";
        last.type = CONFIG_VAR_BOOL;
        last.default_bool = false;
        if (svc_config->register_var(mod_ctx, &last, &s_lastUsedVar) != MOD_OK) s_lastUsedVar = 0;
    }
    if (svc_hook != nullptr) {
        s_pickerAttached = mods::hook::add_post<RandoUiControl>(on_ui_control_post) == MOD_OK;
    }
    coop_log::info("coop_mod: [RANDO] seed picker {}", s_pickerAttached ? "attached" : "unavailable");
    if (svc_save != nullptr) {
        svc_save->observe_saves(mod_ctx, on_new_save, on_save_event, on_save_event, nullptr, nullptr);
    }
    coop_log::info("coop_mod: [RANDO] randomizer {}", rando_installed() ? "installed" : "not installed");
}

static bool s_saveHintShown = false;
static bool s_coopHintShown = false;

void rando_update() {
    ++s_tick;
    update_mode_prompts();
    pick_host_seed();
    const bool inGame = daAlink_getAlinkActorClass() != nullptr && !coop_on_title_screen();

    if (s_tick % 60 == 0 && (inGame || coop_on_title_screen())) {
        const bool active = inGame && probe_seed_active();
        if (active != s_seedActive) {
            s_seedActive = active;
            coop_log::info("coop_mod: [RANDO] a randomizer seed is {}", active ? "running" : "not running");
        }
    }
    if (s_seedActive && s_tick % 300 == 0) read_local_seed();

    if (s_seedActive && s_localSeed.empty() && s_tick % (s_matchTried ? 600 : 60) == 0) {
        match_local_seed();
    }

    if (s_seedActive && !s_intent && !s_coopHintShown && !coop_net_connected() &&
        !coop_net_connecting()) {
        s_coopHintShown = true;
        coop_notify_c(kNotifyRando, "Co-op", "Host or join from the Co-op tab");
    }
    if (!coop_net_connected()) {
        s_announcedRoster = 0;
        s_announcedSeed.clear();
        s_hostSeedFileOk = false;
        s_hostSeed = SeedInfo{};
        s_pickerStarted.clear();
        s_hostToldSeed.clear();
        s_requested = false;
        s_saveHintShown = false;
        for (Outgoing& out : s_outs) out = Outgoing{};
        s_incoming.clear();
        s_incomingHave.clear();
        return;
    }

    if (coop_net_is_host() && s_seedActive && s_localSeed.empty() && s_matchTried &&
        !s_saveHintShown && s_tick % 300 == 0) {
        s_saveHintShown = true;
        coop_notify_c(kNotifyRando, "Save once to share your seed", "");
    }

    if (s_requested && !s_hostSeedFileOk && !s_hostSeed.hash.empty() &&
        s_tick - s_requestTick > kSeedRetryTicks) {
        coop_log::warn("coop_mod: [RANDO] host seed transfer stalled, requesting again");
        request_seed(s_hostSeed);
    }
    const uint32_t roster = coop_net_roster();
    if (s_tick % 300 == 0 || roster != s_announcedRoster || s_localSeed != s_announcedSeed) {
        s_announcedRoster = roster;
        s_announcedSeed = s_localSeed;
        announce_seed();
    }
    pump_outgoing();
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
    return s_seedActive;
}

bool rando_join_sync_wait() {
    if (!s_seedActive || coop_net_is_host() || s_hostSeed.hash.empty()) return false;
    return !s_hostSeedFileOk;
}

bool rando_join_sync_allowed() {
    if (!s_seedActive) return true;

    if (s_hostSeed.hash.empty() || s_localSeed.empty()) return true;
    return s_hostSeed.hash == s_localSeed;
}

void rando_debug_enter_randomizer() {
    resolve_mode_switch();
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
    return coop_net_connected() && !coop_net_is_host() && !s_hostSeed.hash.empty() &&
           s_hostSeedFileOk;
}

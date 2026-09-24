

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/host.h"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_save.h"
#include "d/d_stage.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

const uint16_t kJoinSyncVersion = 1;
const size_t kSaveSize = sizeof(dSv_save_c);

static_assert(sizeof(dSv_save_c) == 0x958, "dSv_save_c size moved - the join-sync blob length is wrong");

ConfigVarHandle s_acceptVar = 0;

bool s_hostSentTo[kCoopMaxPlayers] = {};
bool s_joinerApplied = false;
bool s_havePending = false;
std::vector<uint8_t> s_pending;
uint32_t s_settledTicks = 0;

bool in_gameplay_settled() {
    if (daAlink_getAlinkActorClass() == nullptr || dComIfGp_event_runCheck() ||
        dComIfGp_isEnableNextStage() || dComIfGp_getStageStagInfo() == nullptr)
    {
        s_settledTicks = 0;
        return false;
    }
    return ++s_settledTicks >= 30;
}

int current_save_slot() {
    stage_stag_info_class* info = dComIfGp_getStageStagInfo();
    if (info == nullptr) return -1;
    const int slot = dStage_stagInfo_GetSaveTbl(info);
    return (slot >= 0 && slot < dSv_save_c::STAGE_MAX) ? slot : -1;
}

void send_snapshot(uint8_t to) {
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr) return;
    std::vector<uint8_t> buffer(sizeof(MsgJoinSyncHeader) + kSaveSize);
    MsgJoinSyncHeader header{};
    header.version = kJoinSyncVersion;
    header.size = static_cast<uint16_t>(kSaveSize);
    std::memcpy(buffer.data(), &header, sizeof(header));

    dSv_save_c& save = info->getSavedata();
    std::memcpy(buffer.data() + sizeof(header), &save, kSaveSize);

    const int slot = current_save_slot();
    if (slot >= 0) {
        const size_t slotOffset = offsetof(dSv_save_c, mSave) + slot * sizeof(dSv_memory_c);
        std::memcpy(buffer.data() + sizeof(header) + slotOffset, &info->getMemory(),
            sizeof(dSv_memory_c));
    }

    coop_net_send_to(to, kMsgJoinSync, buffer.data(), buffer.size());
    s_hostSentTo[to] = true;
    coop_log::info("coop_mod: [JOIN] sent our progress to player {} ({} bytes)", to, kSaveSize);
}

const char kBackupMagic[4] = {'T', 'P', 'C', 'B'};
const uint16_t kBackupVersion = 1;
const int kBackupsKept = 5;

#pragma pack(push, 1)
struct SaveBackupHeader {
    char magic[4];
    uint16_t version;
    uint16_t size;
    int64_t unixTime;
    char stage[8];
};
#pragma pack(pop)

bool s_carryingJoinedWorld = false;

bool backup_dir(std::filesystem::path& out) {
    if (!SERVICE_HAS(svc_host, HostService, data_dir)) return false;
    const char* dir = nullptr;
    if (svc_host->data_dir(mod_ctx, &dir) != MOD_OK || dir == nullptr) return false;
    out = std::filesystem::u8path(dir) / "save_backups";
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    return !ec;
}

std::string path_text(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::vector<std::filesystem::path> list_backups() {
    std::vector<std::filesystem::path> out;
    std::filesystem::path dir;
    if (!backup_dir(dir)) return out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = path_text(entry.path().filename());
        if (name.rfind("before_join_", 0) == 0 && entry.path().extension() == ".bin") {
            out.push_back(entry.path());
        }
    }

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return path_text(a.filename()) > path_text(b.filename());
    });
    return out;
}

void live_save_blob(dSv_info_c* info, uint8_t* out) {
    std::memcpy(out, &info->getSavedata(), kSaveSize);
    const int slot = current_save_slot();
    if (slot >= 0) {
        const size_t slotOffset = offsetof(dSv_save_c, mSave) + slot * sizeof(dSv_memory_c);
        std::memcpy(out + slotOffset, &info->getMemory(), sizeof(dSv_memory_c));
    }
}

void write_backup(dSv_info_c* info) {
    if (s_carryingJoinedWorld) {
        coop_log::info("coop_mod: [BACKUP] not backing up - the game is still holding a world "
                        "from an earlier join, not your own progress");
        return;
    }
    std::filesystem::path dir;
    if (!backup_dir(dir)) {
        coop_log::warn("coop_mod: [BACKUP] no data folder available - joining WITHOUT a backup");
        return;
    }
    SaveBackupHeader header{};
    std::memcpy(header.magic, kBackupMagic, 4);
    header.version = kBackupVersion;
    header.size = static_cast<uint16_t>(kSaveSize);
    const std::time_t now = std::time(nullptr);
    header.unixTime = static_cast<int64_t>(now);
    const char* stage = dComIfGp_getStartStageName();
    if (stage != nullptr) std::strncpy(header.stage, stage, sizeof(header.stage));

    std::vector<uint8_t> blob(kSaveSize);
    live_save_blob(info, blob.data());

    char name[64];
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    std::strftime(name, sizeof(name), "before_join_%Y%m%d_%H%M%S.bin", &tmv);
    const std::filesystem::path path = dir / name;
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(&header), sizeof(header));
        f.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        if (!f) {
            coop_log::warn("coop_mod: [BACKUP] could not write {}", path_text(path));
            return;
        }
    }
    coop_log::info("coop_mod: [BACKUP] your progress was saved to {}", path_text(path));

    const auto all = list_backups();
    for (size_t i = kBackupsKept; i + 1 < all.size(); ++i) {
        std::error_code ec;
        std::filesystem::remove(all[i], ec);
    }
}

bool read_backup(const std::filesystem::path& path, std::vector<uint8_t>& blob,
    SaveBackupHeader& header) {
    std::ifstream f(path, std::ios::binary);
    if (!f.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;
    if (std::memcmp(header.magic, kBackupMagic, 4) != 0 || header.version != kBackupVersion ||
        header.size != kSaveSize)
    {
        return false;
    }
    blob.resize(kSaveSize);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(blob.data()), kSaveSize));
}

void merge_into_live_save(dSv_info_c* info, const uint8_t* blob) {
    dSv_save_c& mine = info->getSavedata();

    static dSv_save_c result;
    std::memcpy(&result, blob, kSaveSize);

    dSv_player_status_a_c& hostA = result.mPlayer.mPlayerStatusA;
    const dSv_player_status_a_c& myA = mine.mPlayer.mPlayerStatusA;
    std::memcpy(hostA.mSelectEquip, myA.mSelectEquip, sizeof(hostA.mSelectEquip));
    hostA.mTransformStatus = myA.mTransformStatus;

    const u16 hostMaxLife = hostA.mMaxLife;
    u16 life = myA.mLife;
    const u16 lifeCap = static_cast<u16>((hostMaxLife / 5) * 4);
    if (life > lifeCap) life = lifeCap;
    if (life == 0) life = lifeCap > 0 ? lifeCap : 4;
    hostA.mLife = life;

    {
        const u8 hostTransform = result.mPlayer.mPlayerStatusB.mTransformLevelFlag;
        const u8 hostDarkClear = result.mPlayer.mPlayerStatusB.mDarkClearLevelFlag;
        result.mPlayer.mPlayerStatusB = mine.mPlayer.mPlayerStatusB;
        result.mPlayer.mPlayerStatusB.mTransformLevelFlag = hostTransform;
        result.mPlayer.mPlayerStatusB.mDarkClearLevelFlag = hostDarkClear;
    }
    result.mPlayer.mHorsePlace = mine.mPlayer.mHorsePlace;
    result.mPlayer.mPlayerReturnPlace = mine.mPlayer.mPlayerReturnPlace;
    result.mPlayer.mPlayerFieldLastStayInfo = mine.mPlayer.mPlayerFieldLastStayInfo;
    result.mPlayer.mPlayerLastMarkInfo = mine.mPlayer.mPlayerLastMarkInfo;
    result.mPlayer.mPlayerInfo = mine.mPlayer.mPlayerInfo;
    result.mPlayer.mConfig = mine.mPlayer.mConfig;

    std::memcpy(&mine, &result, kSaveSize);

    const int slot = current_save_slot();
    if (slot >= 0) {
        info->getMemory() = mine.getSave(slot);
    }

    features_reset_sync_baselines();
    world_on_connected();
}

struct PendingSession {
    bool active = false;
    u8 selectEquip[sizeof(dSv_player_status_a_c::mSelectEquip)] = {};
    u8 transform = 0;
    u16 life = 0;
    uint32_t waitedTicks = 0;
};
PendingSession s_session;

const uint32_t kSessionGiveUpTicks = 600;

void load_host_save(dSv_info_c* info, const uint8_t* blob) {
    dSv_save_c& mine = info->getSavedata();
    static dSv_save_c result;
    std::memcpy(&result, blob, kSaveSize);

    dSv_player_status_a_c& a = result.mPlayer.mPlayerStatusA;
    const dSv_player_status_a_c& myA = mine.mPlayer.mPlayerStatusA;

    std::memcpy(s_session.selectEquip, a.mSelectEquip, sizeof(s_session.selectEquip));
    s_session.transform = a.mTransformStatus;
    s_session.life = a.mLife;

    std::memcpy(a.mSelectEquip, myA.mSelectEquip, sizeof(a.mSelectEquip));
    a.mTransformStatus = myA.mTransformStatus;

    const u16 maxLife = a.mMaxLife;
    const u16 myLife = myA.mLife;
    const u16 lifeCap = static_cast<u16>((maxLife / 5) * 4);
    a.mLife = std::min<u16>(myLife == 0 ? u16{4} : myLife, lifeCap > 0 ? lifeCap : u16{4});

    result.mPlayer.mConfig = mine.mPlayer.mConfig;

    if (result.mPlayer.mHorsePlace.mName[0] != '\0') {
        std::memset(result.mPlayer.mHorsePlace.mName, 0, sizeof(result.mPlayer.mHorsePlace.mName));
        std::memcpy(result.mPlayer.mHorsePlace.mName, "coop", 4);
    }

    std::memcpy(&mine, &result, kSaveSize);

    const int slot = current_save_slot();
    if (slot >= 0) info->getMemory() = mine.getSave(slot);

    features_reset_sync_baselines();
    world_on_connected();

    s_session.active = features_reload_at_player(kCoopHostId);
    s_session.waitedTicks = 0;
    if (!s_session.active) {
        coop_log::warn("coop_mod: [JOIN] could not tell where the host is - keeping our own gear "
                       "and position this time");
    }
}

void apply_pending_session() {
    if (!s_session.active) return;
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr) return;
    if (daAlink_getAlinkActorClass() != nullptr) {
        if (++s_session.waitedTicks >= kSessionGiveUpTicks) {
            coop_log::warn("coop_mod: [JOIN] the load never happened - keeping our own gear");
            s_session = PendingSession{};
        }
        return;
    }
    dSv_player_status_a_c& a = info->getSavedata().mPlayer.mPlayerStatusA;
    std::memcpy(a.mSelectEquip, s_session.selectEquip, sizeof(a.mSelectEquip));
    a.mTransformStatus = s_session.transform;
    a.mLife = s_session.life;
    coop_log::info("coop_mod: [JOIN] loading in with the host's gear and form");
    s_session = PendingSession{};
}

void apply_snapshot() {
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr || s_pending.size() != kSaveSize) return;

    if (!rando_join_sync_allowed()) {
        s_havePending = false;
        s_pending.clear();
        s_joinerApplied = true;
        features_toast("Kept your own file",
            "You and the host are on different randomizer seeds, so their progress was not "
            "copied over.");
        return;
    }

    if (!game_mode_is_coop()) {
        write_backup(info);
    } else {
        coop_log::info("coop_mod: [JOIN] co-op save - taking the host's world without a backup");
    }
    load_host_save(info, s_pending.data());
    s_carryingJoinedWorld = true;
    s_joinerApplied = true;
    s_havePending = false;
    s_pending.clear();

    const CoopPeer& peer = features_peer_of(kCoopHostId);
    features_toast(("Synced with " + (peer.present ? peer.name : std::string("the host"))).c_str(),
        game_mode_is_coop() ? "You're using their progress now."
                            : "You're using their progress now. Your own save is backed up.");

    {
        const std::string address = coop_net_join_address();
        game_mode_remember_host(peer.present ? peer.name.c_str() : nullptr, address.c_str());
    }
    coop_log::info("coop_mod: [JOIN] applied the host's progress");
}

void restore_backup(bool oldest) {
    if (coop_net_connected()) {
        features_toast("Disconnect first", "You can only restore while disconnected.");
        return;
    }
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr || daAlink_getAlinkActorClass() == nullptr || dComIfGp_event_runCheck()) {
        features_toast("Not right now", "Load your save first.");
        return;
    }
    const auto all = list_backups();
    if (all.empty()) {
        features_toast("No backup found", "One is made each time you join someone.");
        return;
    }
    std::vector<uint8_t> blob;
    SaveBackupHeader header{};
    const std::filesystem::path& chosen = oldest ? all.back() : all.front();
    if (!read_backup(chosen, blob, header)) {
        features_toast("Backup unreadable", "It might be from a different mod version.");
        coop_log::warn("coop_mod: [BACKUP] could not read {}", path_text(chosen));
        return;
    }
    merge_into_live_save(info, blob.data());
    s_carryingJoinedWorld = false;
    coop_log::info("coop_mod: [BACKUP] restored {}", path_text(chosen));
    features_toast("Progress restored", "Save the game to keep it.");
}

}

void joinsync_register_vars() {
    ConfigVarDesc accept = CONFIG_VAR_DESC_INIT;
    accept.name = "join_copies_host_progress";
    accept.type = CONFIG_VAR_BOOL;
    accept.default_bool = true;
    if (svc_config->register_var(mod_ctx, &accept, &s_acceptVar) != MOD_OK) s_acceptVar = 0;
}

void joinsync_restore_backup(bool oldest) {
    restore_backup(oldest);
}

std::string joinsync_backup_summary() {
    const auto all = list_backups();
    if (all.empty()) return "No backups yet.";
    std::vector<uint8_t> blob;
    SaveBackupHeader header{};
    if (!read_backup(all.front(), blob, header)) return "Latest backup could not be read.";
    char when[64] = "an unknown time";
    const std::time_t t = static_cast<std::time_t>(header.unixTime);
    std::tm tmv{};
#ifdef _WIN32
    const bool ok = localtime_s(&tmv, &t) == 0;
#else
    const bool ok = localtime_r(&t, &tmv) != nullptr;
#endif
    if (ok) std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tmv);
    char stage[9] = {};
    std::memcpy(stage, header.stage, 8);
    return std::string("Latest backup: ") + when + (stage[0] ? std::string(" (") + stage + ")" : "") +
           ". " + std::to_string(all.size()) + " kept. The oldest is your first one.";
}

void joinsync_on_connected() {
    s_session = PendingSession{};
    for (int i = 0; i < kCoopMaxPlayers; ++i) s_hostSentTo[i] = false;
    s_joinerApplied = false;
    s_havePending = false;
    s_pending.clear();
    s_settledTicks = 0;
}

void joinsync_update() {

    apply_pending_session();
    if (!coop_net_connected()) return;
    if (coop_net_is_host()) {
        if (!in_gameplay_settled()) return;
        for (int i = 0; i < kCoopMaxPlayers; ++i) {
            const uint8_t id = static_cast<uint8_t>(i);
            if (id == coop_net_local_id() || s_hostSentTo[i]) continue;
            if (!coop_net_player_present(id) || !features_peer_of(id).present) continue;
            send_snapshot(id);
        }
        return;
    }
    if (s_havePending && !s_joinerApplied && in_gameplay_settled()) apply_snapshot();
}

void joinsync_on_message(const uint8_t* payload, size_t size) {
    if (coop_net_is_host() || s_joinerApplied) return;
    if (size < sizeof(MsgJoinSyncHeader)) return;
    MsgJoinSyncHeader header;
    std::memcpy(&header, payload, sizeof(header));
    if (header.version != kJoinSyncVersion || header.size != kSaveSize ||
        size < sizeof(header) + kSaveSize)
    {
        coop_log::warn("coop_mod: [JOIN] ignored a progress snapshot from a different build");
        return;
    }

    if (false) {
        coop_log::info("coop_mod: [JOIN] host progress received but copying is turned off");
        return;
    }
    s_pending.assign(payload + sizeof(header), payload + sizeof(header) + kSaveSize);
    s_havePending = true;
    coop_log::info("coop_mod: [JOIN] received the host's progress - applying once in game");
}

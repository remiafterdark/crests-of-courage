

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_save.h"
#include "d/actor/d_a_obj_drop.h"
#include "d/actor/d_a_obj_smallkey.h"
#include "d/actor/d_a_tbox.h"
#include "d/actor/d_a_tbox2.h"
#include "d/d_tresure.h"
#include "d/d_bg_w.h"
#include "d/d_stage.h"
#include "f_op/f_op_actor_iter.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "f_op/f_op_actor_mng.h"

#include <cstdio>
#include <cstring>

DEFINE_HOOK(&daTbox_c::actionOpenWait, TboxOpenWaitHook);

namespace {

ConfigVarHandle s_dungeonVar = 0;
ConfigVarHandle s_storyVar = 0;

const int kMemSize = 0x20;
const int kKeyOffset = 0x1C;
const int kDanOffset = 0x04;
const int kDanSize = 0x18;
const int kZoneBitSize = 0x0E;
const int kZoneActorSize = 0x10;
const int kEventSize = 256;
const int16_t kProcTbox = 0x0FB;

const int16_t kProcObjDrop = 0x21F;
const int16_t kProcSmallKey = 0x219;
const int kRooms = 64;
const int kZones = 32;

enum WorldRegion : uint8_t {
    kRegionMemory = 0,
    kRegionDan = 1,
    kRegionZone = 2,
    kRegionEvent = 3,
    kRegionKeys = 4,
    kRegionZoneActor = 5,

    kRegionTmp = 6,

    kRegionVisited = 7,

    kRegionLightDrop = 8,

    kRegionCollect = 9,

    kRegionStatusB = 10,
};

const int kStatusBSize = 2;

const int kCollectSize = 16;
const int kCollectPohIndex = 12;

const int kLightDropSize = 8;
const int kLightDropCounts = 4;

const int kMaps = 64;
const int kVisitedSize = 8;

struct VisitedBaseline {
    bool have = false;
    uint8_t maps[kMaps][kVisitedSize] = {};
};
VisitedBaseline s_visited;

struct LightDropBaseline {
    bool have = false;
    uint8_t bytes[kLightDropSize] = {};
};
LightDropBaseline s_lightDrop;
struct CollectBaseline {
    bool have = false;
    uint8_t bytes[kCollectSize] = {};
};
CollectBaseline s_collect;
struct StatusBBaseline {
    bool have = false;
    uint8_t bytes[kStatusBSize] = {};
};
StatusBBaseline s_statusB;

bool s_visitedShareDue = false;

int s_storyDiffFor = 0;
bool s_storyWarned = false;
const int kStoryWarnDigests = 12;

struct Baseline {
    bool have = false;
    char stage[8] = {};
    int saveNo = -1;
    uint8_t mem[kMemSize] = {};
    bool haveDan = false;
    int8_t danStage = -1;
    uint8_t dan[kDanSize] = {};
    bool haveZone[kRooms] = {};
    uint8_t zone[kRooms][kZoneBitSize] = {};
    uint8_t zoneActor[kRooms][kZoneActorSize] = {};
    bool haveEvent = false;
    uint8_t event[kEventSize] = {};
    bool haveTmp = false;
    uint8_t tmp[kEventSize] = {};
};
Baseline s_base;
uint32_t s_tick = 0;

bool s_askedRoom[kRooms] = {};
bool s_askedStage = false;

ConfigVarHandle s_selfTestVar = 0;
uint32_t s_selfTestTicks = 0;
bool s_selfTestDone = false;

const int kSelfTestSwitch = 0x3F;
const int kSelfTestZoneSwitch = 0x1F;

static_assert(sizeof(dSv_memBit_c) == kMemSize, "dSv_memBit_c size moved - kMemSize is a memcpy length");
static_assert(sizeof(dSv_zoneBit_c) == kZoneBitSize, "dSv_zoneBit_c size moved - kZoneBitSize is a memcpy length");
static_assert(sizeof(dSv_zoneActor_c) == kZoneActorSize, "dSv_zoneActor_c size moved - kZoneActorSize is a memcpy length");

bool current_stage(char out[8], int& saveNo) {
    if (daAlink_getAlinkActorClass() == nullptr) return false;
    const char* name = dComIfGp_getStartStageName();
    stage_stag_info_class* info = dComIfGp_getStageStagInfo();
    if (name == nullptr || info == nullptr) return false;
    saveNo = dStage_stagInfo_GetSaveTbl(info);
    if (saveNo < 0 || saveNo >= dSv_save_c::STAGE_MAX) return false;
    std::memset(out, 0, 8);
    std::strncpy(out, name, 8);
    return true;
}

uint8_t* mem_bytes(dSv_memory_c& memory) {
    return reinterpret_cast<uint8_t*>(&memory.getBit());
}

void send_delta(const Baseline& b, WorldRegion region, int8_t room, int offset, int size,
    const uint8_t* set, const uint8_t* clr) {
    MsgWorldDelta msg{};
    std::memcpy(msg.stage, b.stage, 8);
    msg.saveNo = static_cast<int8_t>(b.saveNo);
    msg.region = region;
    msg.room = room;
    msg.offset = static_cast<uint8_t>(offset);
    msg.size = static_cast<uint8_t>(size);
    std::memcpy(msg.set, set, size);
    std::memcpy(msg.clr, clr, size);
    coop_net_send(kMsgWorldDelta, &msg, sizeof(msg));
}

void send_sync_request(const char stage[8], int saveNo, int room) {
    MsgWorldSyncRequest req{};
    std::memcpy(req.stage, stage, 8);
    req.saveNo = static_cast<int8_t>(saveNo);
    req.room = static_cast<int8_t>(room);
    coop_net_send(kMsgWorldSyncRequest, &req, sizeof(req));
    coop_log::info("coop_mod: [WORLD] asking the other player for {:.8s} {}", stage,
        room < 0 ? "stage memory" : "room bits");
}

void send_full(const char stage[8], int saveNo, WorldRegion region, int room, const uint8_t* data,
    int size) {
    MsgWorldFull msg{};
    std::memcpy(msg.stage, stage, 8);
    msg.saveNo = static_cast<int8_t>(saveNo);
    msg.region = region;
    msg.room = static_cast<int8_t>(room);
    msg.size = static_cast<uint8_t>(size);
    std::memcpy(msg.data, data, size);
    coop_net_send(kMsgWorldFull, &msg, sizeof(msg));
}

void keep_private(uint8_t* bytes, const uint8_t* before, uint8_t (*mask_of)(int)) {
    for (int b = 0; b < kEventSize; ++b) {
        const uint8_t mask = mask_of(b);
        if (mask != 0) bytes[b] = static_cast<uint8_t>((bytes[b] & ~mask) | (before[b] & mask));
    }
}

void keep_private_tmp(uint8_t* tmp, const uint8_t* before) {
    keep_private(tmp, before, skills_tmp_private);
}

void keep_private_event(uint8_t* ev, const uint8_t* before) {
    keep_private(ev, before, skills_event_private);
}

uint32_t hash_bytes(const uint8_t* data, int size, int skipByte);

uint32_t hash_shared(const uint8_t* bytes, uint8_t (*mask_of)(int)) {
    uint8_t copy[kEventSize];
    std::memcpy(copy, bytes, kEventSize);
    for (int b = 0; b < kEventSize; ++b) copy[b] = static_cast<uint8_t>(copy[b] & ~mask_of(b));
    return hash_bytes(copy, kEventSize, -1);
}

void diff_region(WorldRegion region, int8_t room, uint8_t* cur, uint8_t* base, int size,
    int skipByte = -1) {
    for (int start = 0; start < size; start += 32) {
        const int n = (size - start) < 32 ? (size - start) : 32;
        uint8_t set[32] = {};
        uint8_t clr[32] = {};
        bool any = false;
        for (int i = 0; i < n; ++i) {
            if (start + i == skipByte) continue;
            set[i] = static_cast<uint8_t>(cur[start + i] & ~base[start + i]);
            clr[i] = static_cast<uint8_t>(base[start + i] & ~cur[start + i]);
            if (set[i] != 0 || clr[i] != 0) any = true;
        }
        if (any) send_delta(s_base, region, room, start, n, set, clr);
    }
    for (int i = 0; i < size; ++i) {
        if (i != skipByte) base[i] = cur[i];
    }
}

void take_baseline(dSv_info_c* info, const char stage[8], int saveNo) {
    s_base = Baseline{};
    for (int i = 0; i < kRooms; ++i) s_askedRoom[i] = false;
    s_askedStage = false;
    s_base.have = true;
    std::memcpy(s_base.stage, stage, 8);
    s_base.saveNo = saveNo;
    std::memcpy(s_base.mem, mem_bytes(info->getMemory()), kMemSize);
    dSv_danBit_c& dan = info->getDan();
    if (dan.mStageNo == saveNo) {
        s_base.haveDan = true;
        s_base.danStage = dan.mStageNo;
        std::memcpy(s_base.dan, reinterpret_cast<uint8_t*>(&dan) + kDanOffset, kDanSize);
    }
    for (int i = 0; i < kZones; ++i) {
        dSv_zone_c& zone = info->getZone(i);
        const int room = zone.getRoomNo();
        if (room < 0 || room >= kRooms) continue;
        s_base.haveZone[room] = true;
        std::memcpy(s_base.zone[room], &zone.getBit(), kZoneBitSize);
        std::memcpy(s_base.zoneActor[room], &zone.getActor(), kZoneActorSize);
    }
    s_base.haveEvent = true;
    std::memcpy(s_base.event, info->getSavedata().getEvent().mEvent, kEventSize);
    s_base.haveTmp = true;
    std::memcpy(s_base.tmp, info->getTmp().mEvent, kEventSize);
}

bool peer_on_stage(const char stage[8]) {

    return features_any_peer_on_stage(stage);
}

void ask_for_new_regions(dSv_info_c* info, const char stage[8], int saveNo) {
    if (!peer_on_stage(stage)) {

        s_askedStage = false;
        for (int i = 0; i < kRooms; ++i) s_askedRoom[i] = false;
        return;
    }
    if (!s_askedStage) {
        s_askedStage = true;

        if (!coop_net_is_host()) send_sync_request(stage, saveNo, -1);
    }
    for (int i = 0; i < kZones; ++i) {
        const int room = info->getZone(i).getRoomNo();
        if (room < 0 || room >= kRooms || s_askedRoom[room]) continue;
        s_askedRoom[room] = true;

        send_sync_request(stage, saveNo, room);
    }
}

void diff_visited(dSv_info_c* info) {
    dSv_save_c& save = info->getSavedata();
    if (!s_visited.have) {
        s_visited.have = true;
        int shared = 0;
        for (int m = 0; m < kMaps; ++m) {
            auto* cur = reinterpret_cast<uint8_t*>(save.getSave2(m));
            std::memcpy(s_visited.maps[m], cur, kVisitedSize);
            if (!s_visitedShareDue) continue;
            bool any = false;
            for (int i = 0; i < kVisitedSize; ++i) {
                if (cur[i] != 0) { any = true; break; }
            }
            if (!any) continue;
            uint8_t set[32] = {};
            uint8_t clr[32] = {};
            std::memcpy(set, cur, kVisitedSize);
            send_delta(s_base, kRegionVisited, static_cast<int8_t>(m), 0, kVisitedSize, set, clr);
            ++shared;
        }
        if (s_visitedShareDue) {
            s_visitedShareDue = false;
            coop_log::info("coop_mod: [WORLD] shared {} explored map(s) with the other player",
                shared);
        }
        return;
    }
    for (int m = 0; m < kMaps; ++m) {
        auto* cur = reinterpret_cast<uint8_t*>(save.getSave2(m));
        uint8_t set[32] = {};
        uint8_t clr[32] = {};
        bool any = false;
        for (int i = 0; i < kVisitedSize; ++i) {
            set[i] = static_cast<uint8_t>(cur[i] & ~s_visited.maps[m][i]);

            if (set[i] != 0) any = true;
        }
        if (any) {
            send_delta(s_base, kRegionVisited, static_cast<int8_t>(m), 0, kVisitedSize, set, clr);
        }
        std::memcpy(s_visited.maps[m], cur, kVisitedSize);
    }
}

void diff_light_drop(dSv_info_c* info) {
    auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getLightDrop());
    if (!s_lightDrop.have) {
        s_lightDrop.have = true;
        std::memcpy(s_lightDrop.bytes, cur, kLightDropSize);
        return;
    }
    if (std::memcmp(cur, s_lightDrop.bytes, kLightDropSize) == 0) return;
    uint8_t set[32] = {};
    uint8_t clr[32] = {};

    for (int i = 0; i < kLightDropCounts; ++i) {
        set[i] = cur[i] > s_lightDrop.bytes[i]
                     ? static_cast<uint8_t>(cur[i] - s_lightDrop.bytes[i])
                     : 0;
    }
    set[4] = cur[4];
    send_delta(s_base, kRegionLightDrop, -1, 0, kLightDropSize, set, clr);
    std::memcpy(s_lightDrop.bytes, cur, kLightDropSize);
    coop_log::info("coop_mod: [WORLD] tears of light now {}/{}/{}/{} (flags {:#04x})",
        cur[0], cur[1], cur[2], cur[3], cur[4]);
}

void diff_collect(dSv_info_c* info) {
    auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getCollect());
    if (!s_collect.have) {
        s_collect.have = true;
        std::memcpy(s_collect.bytes, cur, kCollectSize);
        return;
    }
    if (std::memcmp(cur, s_collect.bytes, kCollectSize) == 0) return;
    uint8_t set[32] = {};
    uint8_t clr[32] = {};
    std::memcpy(set, cur, kCollectSize);
    send_delta(s_base, kRegionCollect, -1, 0, kCollectSize, set, clr);
    std::memcpy(s_collect.bytes, cur, kCollectSize);
    coop_log::info("coop_mod: [WORLD] collectibles changed (poe souls {})",
        cur[kCollectPohIndex]);
}

uint8_t* status_b_flags(dSv_info_c* info) {
    return &info->getSavedata().getPlayer().mPlayerStatusB.mTransformLevelFlag;
}

void diff_status_b(dSv_info_c* info) {
    uint8_t* cur = status_b_flags(info);
    if (!s_statusB.have) {
        s_statusB.have = true;
        std::memcpy(s_statusB.bytes, cur, kStatusBSize);
        return;
    }
    uint8_t set[32] = {};
    uint8_t clr[32] = {};
    bool any = false;
    for (int i = 0; i < kStatusBSize; ++i) {
        set[i] = static_cast<uint8_t>(cur[i] & ~s_statusB.bytes[i]);
        if (set[i] != 0) any = true;
    }
    if (any) {
        send_delta(s_base, kRegionStatusB, -1, 0, kStatusBSize, set, clr);
        coop_log::info("coop_mod: [WORLD] transform/twilight flags now {:#04x}/{:#04x}",
            cur[0], cur[1]);
    }
    std::memcpy(s_statusB.bytes, cur, kStatusBSize);
}

bool local_mid_sequence() {
    return dComIfGp_event_runCheck() != 0;
}

bool dungeon_stage(const char* stage) {
    return stage != nullptr && stage[0] == 'D' && stage[1] == '_';
}

struct HeldWorldMsg {
    uint8_t type;
    uint8_t from;
    uint16_t size;
    uint8_t bytes[sizeof(MsgWorldFull) > sizeof(MsgWorldDelta) ? sizeof(MsgWorldFull)
                                                                : sizeof(MsgWorldDelta)];
};
const int kHeldWorldMax = 512;
HeldWorldMsg* s_heldWorld = nullptr;
int s_heldWorldCount = 0;
bool s_replayingWorld = false;

uint32_t s_heldSequenceTicks = 0;

void scan() {

    if (local_mid_sequence()) {
        if (s_heldSequenceTicks == 0) {
            coop_log::info("coop_mod: [WORLD] holding world state - our own sequence is running");
        }
        ++s_heldSequenceTicks;
        return;
    }
    if (s_heldSequenceTicks != 0) {
        coop_log::info("coop_mod: [WORLD] sequence over after {} ticks - publishing what changed "
                        "during it now", s_heldSequenceTicks);
        s_heldSequenceTicks = 0;
    }
    char stage[8];
    int saveNo = -1;
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr || !current_stage(stage, saveNo)) {
        s_base.have = false;
        return;
    }
    if (!s_base.have || std::memcmp(stage, s_base.stage, 8) != 0 || saveNo != s_base.saveNo) {

        take_baseline(info, stage, saveNo);
        return;
    }
    const bool dungeon = coop_session(kSessDungeon, cfg_bool(s_dungeonVar, true));
    const bool story = coop_session(kSessStory, cfg_bool(s_storyVar, false));
    if (dungeon) ask_for_new_regions(info, stage, saveNo);

    uint8_t* mem = mem_bytes(info->getMemory());
    if (dungeon) {
        if (mem[kKeyOffset] != s_base.mem[kKeyOffset]) {
            uint8_t value[1] = {mem[kKeyOffset]};
            uint8_t zero[1] = {0};
            send_delta(s_base, kRegionKeys, -1, 0, 1, value, zero);
            coop_log::info("coop_mod: [WORLD] small keys now {}", mem[kKeyOffset]);
        }
        diff_region(kRegionMemory, -1, mem, s_base.mem, kMemSize, kKeyOffset);
    }
    s_base.mem[kKeyOffset] = mem[kKeyOffset];
    if (!dungeon) std::memcpy(s_base.mem, mem, kMemSize);

    dSv_danBit_c& dan = info->getDan();
    uint8_t* danBytes = reinterpret_cast<uint8_t*>(&dan) + kDanOffset;
    if (dan.mStageNo != saveNo) {
        s_base.haveDan = false;
    } else if (!s_base.haveDan || s_base.danStage != dan.mStageNo || !dungeon) {
        s_base.haveDan = true;
        s_base.danStage = dan.mStageNo;
        std::memcpy(s_base.dan, danBytes, kDanSize);
    } else {
        diff_region(kRegionDan, -1, danBytes, s_base.dan, kDanSize);
    }

    bool seen[kRooms] = {};
    for (int i = 0; i < kZones; ++i) {
        dSv_zone_c& zone = info->getZone(i);
        const int room = zone.getRoomNo();
        if (room < 0 || room >= kRooms) continue;
        seen[room] = true;
        uint8_t* bits = reinterpret_cast<uint8_t*>(&zone.getBit());
        uint8_t* actorBits = reinterpret_cast<uint8_t*>(&zone.getActor());
        if (!s_base.haveZone[room] || !dungeon) {
            s_base.haveZone[room] = true;
            std::memcpy(s_base.zone[room], bits, kZoneBitSize);
            std::memcpy(s_base.zoneActor[room], actorBits, kZoneActorSize);
        } else {

            if (dungeon_stage(stage)) {
                diff_region(kRegionZone, static_cast<int8_t>(room), bits, s_base.zone[room],
                    kZoneBitSize);
            } else {
                std::memcpy(s_base.zone[room], bits, kZoneBitSize);
            }
            diff_region(kRegionZoneActor, static_cast<int8_t>(room), actorBits,
                s_base.zoneActor[room], kZoneActorSize);
        }
    }
    for (int r = 0; r < kRooms; ++r) {
        if (!seen[r]) s_base.haveZone[r] = false;
    }

    uint8_t* tmp = info->getTmp().mEvent;
    if (dungeon && dungeon_stage(stage) && s_base.haveTmp) {
        uint8_t view[kEventSize];
        std::memcpy(view, tmp, kEventSize);
        keep_private_tmp(view, s_base.tmp);
        diff_region(kRegionTmp, -1, view, s_base.tmp, kEventSize);
    } else {
        s_base.haveTmp = true;
        std::memcpy(s_base.tmp, tmp, kEventSize);
    }

    if (dungeon) diff_visited(info);
    if (dungeon) diff_light_drop(info);
    if (dungeon) diff_collect(info);
    if (dungeon) diff_status_b(info);

    uint8_t* events = info->getSavedata().getEvent().mEvent;
    if (story && s_base.haveEvent) {
        uint8_t view[kEventSize];
        std::memcpy(view, events, kEventSize);
        keep_private_event(view, s_base.event);
        diff_region(kRegionEvent, -1, view, s_base.event, kEventSize);
    } else {
        s_base.haveEvent = true;
        std::memcpy(s_base.event, events, kEventSize);
    }
}

struct ChestSweep {
    const uint8_t* wantedBits;
    int count;
};

bool tbox_bit_set(const uint8_t* bytes, int no) {
    if (bytes == nullptr || no < 0 || no >= 64) return false;
    const int word = no >> 5;
    const int bitInWord = no & 0x1F;

    const int byteIndex = word * 4 + (3 - (bitInWord >> 3));
    return (bytes[byteIndex] & (1 << (no & 7))) != 0;
}

bool s_scrubbedByRemote[64] = {};

uint8_t s_pendingChestBits[8] = {};
char s_pendingChestStage[8] = {};

void clear_tbox_bit(uint8_t* bytes, int no) {
    if (bytes == nullptr || no < 0 || no >= 64) return;
    const int word = no >> 5;
    const int bitInWord = no & 0x1F;
    const int byteIndex = word * 4 + (3 - (bitInWord >> 3));
    bytes[byteIndex] = static_cast<uint8_t>(bytes[byteIndex] & ~(1 << (no & 7)));
}

void* open_matching_chest(void* proc, void* data) {
    auto* sweep = static_cast<ChestSweep*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || fopAcM_GetName(actor) != kProcTbox) return nullptr;
    auto* chest = reinterpret_cast<daTbox_c*>(actor);
    const int no = chest->getTboxNo();
    if (no < 0 || no >= 64) return nullptr;
    if (!tbox_bit_set(sweep->wantedBits, no)) return nullptr;

    if (chest->mpAnm == nullptr) return nullptr;

    if (chest->mpAnm->getFrame() > 0.0f && !s_scrubbedByRemote[no]) return nullptr;
    if (dComIfGp_event_runCheck()) return nullptr;
    s_scrubbedByRemote[no] = false;

    if (chest->mpAnm != nullptr) chest->mpAnm->setFrame(chest->mpAnm->getEndFrame());

    static bool reported = false;
    if (!reported) {
        reported = true;
        daTbox_actionFn ours = &daTbox_c::actionWait;
        unsigned char oursBytes[sizeof(daTbox_actionFn)];
        unsigned char theirs[sizeof(daTbox_actionFn)];
        std::memcpy(oursBytes, &ours, sizeof(oursBytes));
        std::memcpy(theirs, &chest->mpActionFn, sizeof(theirs));
        char a[96] = {};
        char b[96] = {};
        int ai = 0, bi = 0;
        for (size_t i = 0; i < sizeof(oursBytes) && ai < 80; ++i) {
            ai += std::snprintf(a + ai, sizeof(a) - ai, "%02x", oursBytes[i]);
            bi += std::snprintf(b + bi, sizeof(b) - bi, "%02x", theirs[i]);
        }
        coop_log::info("coop_mod: [CHEST] pmf size={} ours(actionWait)={} theirs(current)={}",
            static_cast<int>(sizeof(daTbox_actionFn)), a, b);
    }

    chest->setAction(&daTbox_c::actionWait);

    coop_log::info("coop_mod: [CHEST] closing {} remotely: isTbox={}", no,
        dComIfGs_isTbox(no) ? 1 : 0);
    if (!dComIfGs_isTbox(no)) dComIfGs_onTbox(no);
    dTres_c::offStatus(0, no, 1);

    chest->setDzb();
    if (chest->mpBgCollision != nullptr) chest->mpBgCollision->Move();
    clear_tbox_bit(s_pendingChestBits, no);
    ++sweep->count;
    return nullptr;
}

void retry_pending_chests() {
    bool any = false;
    for (int i = 0; i < 8; ++i) any = any || s_pendingChestBits[i] != 0;
    if (!any) return;
    const char* here = dComIfGp_getStartStageName();
    if (here == nullptr || std::strncmp(here, s_pendingChestStage, 8) != 0) {
        std::memset(s_pendingChestBits, 0, sizeof(s_pendingChestBits));
        return;
    }
    uint8_t wanted[8];
    std::memcpy(wanted, s_pendingChestBits, sizeof(wanted));
    ChestSweep sweep;
    sweep.wantedBits = wanted;
    sweep.count = 0;
    fopAcM_Search(open_matching_chest, &sweep);
    if (sweep.count > 0) {
        coop_log::info("coop_mod: [WORLD] {} chest(s) the other player emptied are shown open now",
            sweep.count);
    }
}

const int kChestAnmSlots = 3;
u16 s_chestOpenRes[kChestAnmSlots] = {0xFFFF, 0xFFFF, 0xFFFF};
bool s_chestOpenResReady = false;

void resolve_chest_open_res() {
    if (s_chestOpenResReady) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;
    const daAlink_BckData* a = alink->getMainBckData(daAlink_c::ANM_TRES_OPEN_SMALL);
    const daAlink_BckData* b = alink->getMainBckData(daAlink_c::ANM_TRES_OPEN_KICK);
    const daAlink_BckData* c = alink->getMainBckData(daAlink_c::ANM_TRES_OPEN_BIG);
    if (a == nullptr || b == nullptr || c == nullptr) return;
    s_chestOpenRes[0] = a->m_underID;
    s_chestOpenRes[1] = b->m_underID;
    s_chestOpenRes[2] = c->m_underID;
    s_chestOpenResReady = true;
    coop_log::info("coop_mod: [WORLD] chest-open clips are res {} / {} / {}", s_chestOpenRes[0],
        s_chestOpenRes[1], s_chestOpenRes[2]);
}

bool is_chest_open_clip(u16 resIdx) {
    if (!s_chestOpenResReady || resIdx == 0xFFFF) return false;
    for (int i = 0; i < kChestAnmSlots; ++i) {
        if (s_chestOpenRes[i] == resIdx) return true;
    }
    return false;
}

struct ChestPick {
    cXyz at;
    daTbox_c* best;
    f32 bestDistSq;
};

void* pick_closed_chest(void* proc, void* data) {
    auto* pick = static_cast<ChestPick*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || fopAcM_GetName(actor) != kProcTbox) return nullptr;
    auto* chest = reinterpret_cast<daTbox_c*>(actor);

    if (chest->mpAnm == nullptr) return nullptr;
    if (chest->mpAnm->getFrame() >= chest->mpAnm->getEndFrame()) return nullptr;
    const f32 dx = actor->current.pos.x - pick->at.x;
    const f32 dy = actor->current.pos.y - pick->at.y;
    const f32 dz = actor->current.pos.z - pick->at.z;
    const f32 d = dx * dx + dy * dy + dz * dz;
    if (pick->best == nullptr || d < pick->bestDistSq) {
        pick->best = chest;
        pick->bestDistSq = d;
    }
    return nullptr;
}

const f32 kChestReach = 180.0f;

void drive_remote_chest_lids() {
    if (!coop_session(kSessDungeon, cfg_bool(s_dungeonVar, true)) || !coop_net_connected()) return;
    resolve_chest_open_res();
    if (!s_chestOpenResReady) return;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id()) continue;
        u16 res = 0xFFFF;
        f32 frame = 0.0f;
        if (!puppet_hook_get_anim(id, &res, &frame)) continue;
        if (!is_chest_open_clip(res)) continue;
        f32 px = 0.0f, py = 0.0f, pz = 0.0f;
        if (!puppet_hook_get_pose_of(id, &px, &py, &pz, nullptr, nullptr, nullptr)) continue;

        ChestPick pick;
        pick.at.set(px, py, pz);
        pick.best = nullptr;
        pick.bestDistSq = 0.0f;
        fopAcM_Search(pick_closed_chest, &pick);
        if (pick.best == nullptr || pick.bestDistSq > kChestReach * kChestReach) continue;

        const f32 end = pick.best->mpAnm->getEndFrame();
        pick.best->mpAnm->setFrame(frame > end ? end : frame);
        pick.best->mpAnm->play();
        const int no = pick.best->getTboxNo();
        if (no >= 0 && no < 64) s_scrubbedByRemote[no] = true;
    }
}

struct ChestCensus { int total; int inRoom; };

void* census_chest(void* proc, void* data) {
    auto* c = static_cast<ChestCensus*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || fopAcM_GetName(actor) != kProcTbox) return nullptr;
    ++c->total;
    auto* chest = reinterpret_cast<daTbox_c*>(actor);
    coop_log::info("coop_mod: [CHEST] present: no={} frame={} end={}",
        chest->getTboxNo(),
        chest->mpAnm != nullptr ? chest->mpAnm->getFrame() : -1.0f,
        chest->mpAnm != nullptr ? chest->mpAnm->getEndFrame() : -1.0f);
    return nullptr;
}

void open_chests_from_bits(const uint8_t* newlySet) {
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        if (newlySet[i] != 0) { any = true; break; }
    }
    if (!any) return;

    const char* here = dComIfGp_getStartStageName();
    if (here != nullptr && std::strncmp(here, s_pendingChestStage, 8) != 0) {
        std::memset(s_pendingChestBits, 0, sizeof(s_pendingChestBits));
        std::strncpy(s_pendingChestStage, here, sizeof(s_pendingChestStage));
    }
    for (int i = 0; i < 8; ++i) s_pendingChestBits[i] |= newlySet[i];
    ChestSweep sweep;
    sweep.wantedBits = newlySet;
    sweep.count = 0;
    fopAcM_Search(open_matching_chest, &sweep);
    if (sweep.count > 0) {
        coop_log::info("coop_mod: [WORLD] {} chest(s) the other player emptied are now shown open",
            sweep.count);
        return;
    }

    char wanted[128];
    int at = 0;
    for (int no = 0; no < 64 && at < 100; ++no) {
        if (!tbox_bit_set(newlySet, no)) continue;
        at += std::snprintf(wanted + at, sizeof(wanted) - at, "%d ", no);
    }
    if (at == 0) std::snprintf(wanted, sizeof(wanted), "(none decoded)");
    coop_log::warn("coop_mod: [CHEST] a treasure bit arrived and nothing matched it. "
                    "raw={:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x} wanted={}",
        newlySet[0], newlySet[1], newlySet[2], newlySet[3], newlySet[4], newlySet[5], newlySet[6],
        newlySet[7], wanted);
    ChestCensus census{0, 0};
    fopAcM_Search(census_chest, &census);
    coop_log::warn("coop_mod: [CHEST] {} chest actor(s) loaded here", census.total);
}

struct DropSweep {
    const uint8_t* wantedBits;
    int count;
};

void* remove_matching_drop(void* proc, void* data) {
    auto* sweep = static_cast<DropSweep*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr) return nullptr;
    const s16 name = fopAcM_GetName(actor);

    int no = -1;
    if (name == kProcObjDrop) {
        auto* drop = reinterpret_cast<daObjDrop_c*>(actor);

        if (drop->mSetCollectDrop == 0) return nullptr;
        no = drop->getSave();
    } else if (name == kProcSmallKey) {

        no = reinterpret_cast<daKey_c*>(actor)->getSaveBitNo();
    } else {
        return nullptr;
    }

    if (no < 0 || no >= 64) return nullptr;
    if (!tbox_bit_set(sweep->wantedBits, no)) return nullptr;
    fopAcM_delete(actor);
    ++sweep->count;
    return nullptr;
}

void remove_collected_from_bits(const uint8_t* newlySet) {
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        if (newlySet[i] != 0) { any = true; break; }
    }
    if (!any) return;
    DropSweep sweep;
    sweep.wantedBits = newlySet;
    sweep.count = 0;
    fopAcM_Search(remove_matching_drop, &sweep);
    if (sweep.count > 0) {
        coop_log::info("coop_mod: [WORLD] removed {} collectible(s) the other player already "
                        "took", sweep.count);
    }
}

void apply_bytes(uint8_t* data, uint8_t* base, const MsgWorldDelta& msg, int skipIndex = -1) {
    for (int i = 0; i < msg.size; ++i) {
        const int at = msg.offset + i;
        if (at == skipIndex) continue;
        data[at] = static_cast<uint8_t>((data[at] | msg.set[i]) & ~msg.clr[i]);
        if (base != nullptr) base[at] = static_cast<uint8_t>((base[at] | msg.set[i]) & ~msg.clr[i]);
    }
}

const int kDigestEveryTicks = 60 * 5;

uint32_t hash_bytes(const uint8_t* data, int size, int skipByte = -1) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < size; ++i) {
        if (i == skipByte) continue;
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t visited_hash(dSv_info_c* info) {
    uint32_t h = 2166136261u;
    dSv_save_c& save = info->getSavedata();
    for (int m = 0; m < kMaps; ++m) {
        auto* cur = reinterpret_cast<uint8_t*>(save.getSave2(m));
        for (int i = 0; i < kVisitedSize; ++i) {
            h ^= cur[i];
            h *= 16777619u;
        }
    }
    return h;
}

uint32_t collect_hash(dSv_info_c* info) {
    auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getCollect());
    return hash_bytes(cur, kCollectSize);
}

uint32_t status_b_hash(dSv_info_c* info) {
    return hash_bytes(status_b_flags(info), kStatusBSize);
}

uint32_t light_drop_hash(dSv_info_c* info) {
    auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getLightDrop());
    return hash_bytes(cur, kLightDropSize);
}

void republish_globals(dSv_info_c* info) {
    s_visited.have = false;
    s_visitedShareDue = true;
    auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getLightDrop());
    uint8_t set[32] = {};
    uint8_t clr[32] = {};

    for (int i = 0; i < kLightDropCounts; ++i) {
        set[i] = cur[i] > s_lightDrop.bytes[i]
                     ? static_cast<uint8_t>(cur[i] - s_lightDrop.bytes[i])
                     : 0;
    }
    set[4] = cur[4];
    send_delta(s_base, kRegionLightDrop, -1, 0, kLightDropSize, set, clr);
    std::memcpy(s_lightDrop.bytes, cur, kLightDropSize);
    s_lightDrop.have = true;

    auto* col = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getCollect());
    uint8_t colSet[32] = {};
    std::memcpy(colSet, col, kCollectSize);
    send_delta(s_base, kRegionCollect, -1, 0, kCollectSize, colSet, clr);
    std::memcpy(s_collect.bytes, col, kCollectSize);
    s_collect.have = true;

    uint8_t* sb = status_b_flags(info);
    uint8_t sbSet[32] = {};
    std::memcpy(sbSet, sb, kStatusBSize);
    send_delta(s_base, kRegionStatusB, -1, 0, kStatusBSize, sbSet, clr);
    std::memcpy(s_statusB.bytes, sb, kStatusBSize);
    s_statusB.have = true;
}

void send_digest(dSv_info_c* info, const char stage[8], int saveNo) {
    MsgWorldDigest msg{};
    std::memcpy(msg.stage, stage, 8);
    msg.saveNo = static_cast<int8_t>(saveNo);

    msg.memoryHash = hash_bytes(mem_bytes(info->getMemory()), kMemSize, kKeyOffset);
    dSv_danBit_c& dan = info->getDan();
    msg.danHash = dan.mStageNo == saveNo
                      ? hash_bytes(reinterpret_cast<uint8_t*>(&dan) + kDanOffset, kDanSize)
                      : 0u;
    msg.tmpHash = hash_shared(info->getTmp().mEvent, skills_tmp_private);
    const int here = dComIfGp_roomControl_getStayNo();
    msg.room = static_cast<int8_t>(here);
    msg.zoneHash = 0;
    for (int i = 0; i < kZones; ++i) {
        dSv_zone_c& zone = info->getZone(i);
        if (zone.getRoomNo() != here) continue;
        msg.zoneHash = hash_bytes(reinterpret_cast<uint8_t*>(&zone.getBit()), kZoneBitSize);
        break;
    }
    msg.visitedHash = visited_hash(info);
    msg.lightDropHash = light_drop_hash(info);
    msg.collectHash = collect_hash(info);
    msg.eventHash = hash_shared(info->getSavedata().getEvent().mEvent, skills_event_private);
    msg.statusBHash = status_b_hash(info);
    coop_net_send(kMsgWorldDigest, &msg, sizeof(msg));
}

void handle_digest(const MsgWorldDigest& msg, uint8_t from) {
    if (!coop_session(kSessDungeon, cfg_bool(s_dungeonVar, true))) return;

    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr) return;
    char stage[8];
    int saveNo = -1;
    if (!current_stage(stage, saveNo)) return;

    if (visited_hash(info) != msg.visitedHash || light_drop_hash(info) != msg.lightDropHash ||
        collect_hash(info) != msg.collectHash || status_b_hash(info) != msg.statusBHash) {
        coop_log::info("coop_mod: [WORLD] global state differs (maps {:#010x}/{:#010x} tears "
                        "{:#010x}/{:#010x} collect {:#010x}/{:#010x}) - republishing ours",
            visited_hash(info), msg.visitedHash, light_drop_hash(info), msg.lightDropHash,
            collect_hash(info), msg.collectHash);
        republish_globals(info);
    }

    if (!coop_session(kSessStory, cfg_bool(s_storyVar, false))) {
        const uint32_t mine = hash_shared(info->getSavedata().getEvent().mEvent, skills_event_private);
        if (mine == msg.eventHash) {
            s_storyDiffFor = 0;
            s_storyWarned = false;
        } else if (!s_storyWarned && ++s_storyDiffFor >= kStoryWarnDigests) {
            s_storyWarned = true;
            coop_log::warn("coop_mod: [WORLD] story flags have differed for {} seconds "
                            "({:#010x} vs {:#010x}) - share_story_progress is off",
                kStoryWarnDigests * kDigestEveryTicks / 60, mine, msg.eventHash);
            features_toast("Story progress doesn't match",
                "If you get stuck, the host can turn on \"Share story progress\".");
        }
    } else {
        s_storyDiffFor = 0;
        s_storyWarned = false;
    }

    if (coop_net_is_host()) return;

    if (from != kCoopHostId) return;

    if (std::memcmp(stage, msg.stage, 8) != 0 || saveNo != msg.saveNo) return;

    const uint32_t mine = hash_bytes(mem_bytes(info->getMemory()), kMemSize, kKeyOffset);
    dSv_danBit_c& dan = info->getDan();
    const uint32_t myDan = dan.mStageNo == saveNo
                               ? hash_bytes(reinterpret_cast<uint8_t*>(&dan) + kDanOffset, kDanSize)
                               : 0u;
    const uint32_t myTmp = hash_shared(info->getTmp().mEvent, skills_tmp_private);

    bool zoneDiffers = false;
    const int here = dComIfGp_roomControl_getStayNo();
    if (msg.room == here) {
        uint32_t myZone = 0;
        for (int i = 0; i < kZones; ++i) {
            dSv_zone_c& zone = info->getZone(i);
            if (zone.getRoomNo() != here) continue;
            myZone = hash_bytes(reinterpret_cast<uint8_t*>(&zone.getBit()), kZoneBitSize);
            break;
        }
        if (myZone != msg.zoneHash) {
            zoneDiffers = true;
            coop_log::info("coop_mod: [WORLD] room {} does not match ({:#010x} vs {:#010x})", here,
                myZone, msg.zoneHash);
            send_sync_request(stage, saveNo, static_cast<int8_t>(here));
        }
    }

    const uint32_t myStatusB = status_b_hash(info);
    const uint32_t myCollect = collect_hash(info);
    const uint32_t myLightDrop = light_drop_hash(info);

    const bool storyOn = coop_session(kSessStory, cfg_bool(s_storyVar, false));
    const uint32_t myEvent = hash_shared(info->getSavedata().getEvent().mEvent, skills_event_private);
    const bool eventOk = !storyOn || myEvent == msg.eventHash;
    if (mine == msg.memoryHash && myDan == msg.danHash && myTmp == msg.tmpHash &&
        myStatusB == msg.statusBHash && myCollect == msg.collectHash &&
        myLightDrop == msg.lightDropHash && eventOk) {
        return;
    }
    (void)zoneDiffers;

    coop_log::info("coop_mod: [WORLD] our copy of {:.8s} does not match theirs "
                    "(memory {:#010x}/{:#010x} dungeon {:#010x}/{:#010x} temp {:#010x}/{:#010x} "
                    "statusB {:#010x}/{:#010x} collect {:#010x}/{:#010x} "
                    "lightDrop {:#010x}/{:#010x} event {:#010x}/{:#010x} "
                    "| not repaired: visited {:#010x}/{:#010x}) - asking for theirs",
        stage, mine, msg.memoryHash, myDan, msg.danHash, myTmp, msg.tmpHash,
        myStatusB, msg.statusBHash, myCollect, msg.collectHash,
        myLightDrop, msg.lightDropHash,
        myEvent, msg.eventHash, visited_hash(info), msg.visitedHash);

    send_sync_request(stage, saveNo, -1);
}

void answer_sync_request(const MsgWorldSyncRequest& req) {

    if (!coop_net_is_host()) return;
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr) return;
    char stage[8];
    int saveNo = -1;
    if (!current_stage(stage, saveNo)) return;
    if (std::memcmp(stage, req.stage, 8) != 0 || saveNo != req.saveNo) return;

    if (req.room < 0) {
        send_full(stage, saveNo, kRegionMemory, -1, mem_bytes(info->getMemory()), kMemSize);
        dSv_danBit_c& dan = info->getDan();
        if (dan.mStageNo == saveNo) {
            send_full(stage, saveNo, kRegionDan, -1,
                reinterpret_cast<uint8_t*>(&dan) + kDanOffset, kDanSize);
        }

        uint8_t* tmp = info->getTmp().mEvent;
        for (int off = 0; off < kEventSize; off += 32) {
            send_full(stage, saveNo, kRegionTmp, static_cast<int8_t>(off / 32), tmp + off, 32);
        }

        send_full(stage, saveNo, kRegionStatusB, -1, status_b_flags(info), kStatusBSize);
        send_full(stage, saveNo, kRegionCollect, -1,
            reinterpret_cast<const uint8_t*>(&info->getSavedata().getPlayer().getCollect()),
            kCollectSize);
        send_full(stage, saveNo, kRegionLightDrop, -1,
            reinterpret_cast<const uint8_t*>(&info->getSavedata().getPlayer().getLightDrop()),
            kLightDropSize);

        if (coop_session(kSessStory, cfg_bool(s_storyVar, false))) {
            uint8_t* ev = info->getSavedata().getEvent().mEvent;
            for (int off = 0; off < kEventSize; off += 32) {
                send_full(stage, saveNo, kRegionEvent, static_cast<int8_t>(off / 32), ev + off, 32);
            }
        }
        coop_log::info("coop_mod: [WORLD] answering stage-memory request for {:.8s}", stage);
        return;
    }
    for (int i = 0; i < kZones; ++i) {
        dSv_zone_c& zone = info->getZone(i);
        if (zone.getRoomNo() != req.room) continue;
        send_full(stage, saveNo, kRegionZone, req.room,
            reinterpret_cast<uint8_t*>(&zone.getBit()), kZoneBitSize);
        send_full(stage, saveNo, kRegionZoneActor, req.room,
            reinterpret_cast<uint8_t*>(&zone.getActor()), kZoneActorSize);
        coop_log::info("coop_mod: [WORLD] answering room {} request for {:.8s}",
            static_cast<int>(req.room), stage);
        return;
    }
}

void merge_full(uint8_t* target, uint8_t* base, const uint8_t* data, int size, bool adopt) {
    for (int i = 0; i < size; ++i) {
        target[i] = adopt ? data[i] : static_cast<uint8_t>(target[i] | data[i]);

        if (base != nullptr) base[i] = data[i];
    }
}

void handle_full(const MsgWorldFull& msg) {
    if (msg.size > 32) return;

    if ((msg.region == kRegionZone || msg.region == kRegionTmp) && !dungeon_stage(msg.stage)) {
        return;
    }

    if (msg.region == kRegionEvent) {
        if (!coop_session(kSessStory, cfg_bool(s_storyVar, false))) return;
    } else if (!coop_session(kSessDungeon, cfg_bool(s_dungeonVar, true))) {
        return;
    }
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr) return;
    char stage[8];
    int saveNo = -1;
    if (!current_stage(stage, saveNo)) return;

    if (std::memcmp(stage, msg.stage, 8) != 0 || saveNo != msg.saveNo) return;
    const bool baselineValid = s_base.have && std::memcmp(s_base.stage, stage, 8) == 0 &&
                               s_base.saveNo == saveNo;

    switch (msg.region) {
    case kRegionMemory: {
        if (msg.size != kMemSize) return;
        uint8_t* mem = mem_bytes(info->getMemory());
        uint8_t newlySet[8] = {};
        for (int i = 0; i < 8; ++i) newlySet[i] = static_cast<uint8_t>(msg.data[i] & ~mem[i]);
        merge_full(mem, baselineValid ? s_base.mem : nullptr, msg.data, kMemSize, true);
        open_chests_from_bits(newlySet);
        remove_collected_from_bits(newlySet);
        break;
    }
    case kRegionDan: {
        dSv_danBit_c& dan = info->getDan();
        if (msg.size != kDanSize || dan.mStageNo != saveNo) return;
        merge_full(reinterpret_cast<uint8_t*>(&dan) + kDanOffset,
            (baselineValid && s_base.haveDan) ? s_base.dan : nullptr, msg.data, kDanSize, false);
        break;
    }
    case kRegionTmp: {

        const int off = static_cast<int>(msg.room) * 32;
        if (msg.size != 32 || off < 0 || off + 32 > kEventSize) return;

        uint8_t before[kEventSize];
        std::memcpy(before, info->getTmp().mEvent, kEventSize);
        merge_full(info->getTmp().mEvent + off,
            (baselineValid && s_base.haveTmp) ? s_base.tmp + off : nullptr, msg.data, 32, false);
        keep_private_tmp(info->getTmp().mEvent, before);
        break;
    }

    case kRegionStatusB: {
        if (msg.size != kStatusBSize) return;
        uint8_t* cur = status_b_flags(info);
        for (int i = 0; i < kStatusBSize; ++i) {
            cur[i] = static_cast<uint8_t>(cur[i] | msg.data[i]);
            if (s_statusB.have) s_statusB.bytes[i] = cur[i];
        }
        break;
    }
    case kRegionCollect: {
        if (msg.size != kCollectSize) return;
        auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getCollect());
        for (int i = 0; i < kCollectSize; ++i) {
            if (i == kCollectPohIndex) {
                if (msg.data[i] > cur[i]) cur[i] = msg.data[i];
            } else {
                cur[i] = static_cast<uint8_t>(cur[i] | msg.data[i]);
            }
        }
        if (s_collect.have) std::memcpy(s_collect.bytes, cur, kCollectSize);
        break;
    }
    case kRegionLightDrop: {
        if (msg.size != kLightDropSize) return;
        auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getLightDrop());
        for (int i = 0; i < kLightDropCounts; ++i) {
            if (msg.data[i] > cur[i]) cur[i] = msg.data[i];
        }
        cur[4] = static_cast<uint8_t>(cur[4] | msg.data[4]);
        if (s_lightDrop.have) std::memcpy(s_lightDrop.bytes, cur, kLightDropSize);
        break;
    }
    case kRegionEvent: {

        if (!coop_session(kSessStory, cfg_bool(s_storyVar, false))) return;
        const int off = static_cast<int>(msg.room) * 32;
        if (msg.size != 32 || off < 0 || off + 32 > kEventSize) return;
        uint8_t before[kEventSize];
        std::memcpy(before, info->getSavedata().getEvent().mEvent, kEventSize);
        merge_full(info->getSavedata().getEvent().mEvent + off,
            (baselineValid && s_base.haveEvent) ? s_base.event + off : nullptr, msg.data, 32,
            false);
        keep_private_event(info->getSavedata().getEvent().mEvent, before);
        break;
    }
    case kRegionZone:
    case kRegionZoneActor: {
        const bool actors = msg.region == kRegionZoneActor;
        const int expect = actors ? kZoneActorSize : kZoneBitSize;
        if (msg.size != expect || msg.room < 0 || msg.room >= kRooms) return;
        for (int i = 0; i < kZones; ++i) {
            dSv_zone_c& zone = info->getZone(i);
            if (zone.getRoomNo() != msg.room) continue;
            uint8_t* target = actors ? reinterpret_cast<uint8_t*>(&zone.getActor())
                                     : reinterpret_cast<uint8_t*>(&zone.getBit());
            uint8_t* base = nullptr;
            if (baselineValid && s_base.haveZone[msg.room]) {
                base = actors ? s_base.zoneActor[msg.room] : s_base.zone[msg.room];
            }
            merge_full(target, base, msg.data, expect, false);
            break;
        }
        break;
    }
    default:
        return;
    }
    coop_log::info("coop_mod: [WORLD] merged full region={} stage={:.8s} room={} ({} bytes)",
        static_cast<int>(msg.region), msg.stage, static_cast<int>(msg.room),
        static_cast<int>(msg.size));
}

void run_self_test() {
    int64_t after = 0;
    if (s_selfTestVar != 0) svc_config->get_int(mod_ctx, s_selfTestVar, &after);

    if (after == 0) return;
    const bool reportOnly = after < 0;
    dSv_info_c* info = dComIfGs_getSaveInfo();
    char stage[8];
    int saveNo = -1;
    if (info == nullptr || !current_stage(stage, saveNo)) return;

    if (!reportOnly && !s_selfTestDone && ++s_selfTestTicks >= static_cast<uint32_t>(after)) {
        s_selfTestDone = true;
        info->getMemory().getBit().onSwitch(kSelfTestSwitch);
        const int room = dComIfGp_roomControl_getStayNo();
        for (int i = 0; i < kZones; ++i) {
            if (info->getZone(i).getRoomNo() != room) continue;
            info->getZone(i).getBit().onSwitch(kSelfTestZoneSwitch);
            break;
        }
        coop_log::warn(
            "coop_mod: [WORLD-SELFTEST] *** DEBUG *** set memory switch {} and room {} zone switch {}",
            kSelfTestSwitch, room, kSelfTestZoneSwitch);
    }

    if (s_tick % 300 != 0) return;
    const int room = dComIfGp_roomControl_getStayNo();
    int zoneBit = -1;
    for (int i = 0; i < kZones; ++i) {
        if (info->getZone(i).getRoomNo() != room) continue;
        zoneBit = info->getZone(i).getBit().isSwitch(kSelfTestZoneSwitch) ? 1 : 0;
        break;
    }
    coop_log::info("coop_mod: [WORLD-CHECK] stage={:.8s} room={} memSwitch={} zoneSwitch={}", stage,
        room, info->getMemory().getBit().isSwitch(kSelfTestSwitch) ? 1 : 0, zoneBit);
}

const int16_t kProcTbox2 = 0x0FC;
const int kMaxTbox2 = 32;

uint32_t tbox2_key(fopAc_ac_c* actor) {
    uint32_t h = 2166136261u;
    const int16_t name = fopAcM_GetName(actor);
    const uint16_t setID = actor->setID;
    auto mix = [&h](const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 16777619u;
        }
    };
    mix(&name, sizeof(name));
    mix(&setID, sizeof(setID));
    if (setID == 0xFFFF) {

        const int32_t x = static_cast<int32_t>(actor->home.pos.x);
        const int32_t y = static_cast<int32_t>(actor->home.pos.y);
        const int32_t z = static_cast<int32_t>(actor->home.pos.z);
        const uint32_t param = fopAcM_GetParam(actor);
        mix(&param, sizeof(param));
        mix(&x, sizeof(x));
        mix(&y, sizeof(y));
        mix(&z, sizeof(z));
    }
    return h != 0 ? h : 1u;
}

uint32_t s_tbox2Sent[kMaxTbox2] = {};
char s_tbox2Stage[8] = {};

struct Tbox2Find {
    uint32_t key;
    cXyz home;
    daTbox2_c* found;
    f32 bestDistSq;
};

void* collect_open_tbox2(void* proc, void* data) {
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || fopAcM_GetName(actor) != kProcTbox2) return nullptr;
    auto* chest = static_cast<daTbox2_c*>(actor);

    if (chest->mAction == daTbox2_c::ACTION_OPEN_WAIT_e) return nullptr;
    const uint32_t key = tbox2_key(actor);
    for (uint32_t& sent : s_tbox2Sent) {
        if (sent == key) return nullptr;
    }
    for (uint32_t& sent : s_tbox2Sent) {
        if (sent != 0) continue;
        sent = key;
        MsgTbox2 msg{};
        msg.key = key;
        msg.room = static_cast<int8_t>(fopAcM_GetRoomNo(actor));
        msg.procName = fopAcM_GetName(actor);
        msg.home[0] = actor->home.pos.x;
        msg.home[1] = actor->home.pos.y;
        msg.home[2] = actor->home.pos.z;
        coop_net_send(kMsgTbox2, &msg, sizeof(msg));
        coop_log::info("coop_mod: [CHEST] told them about no-save chest {:#x}", key);
        break;
    }
    return nullptr;
}

void* match_open_tbox2(void* proc, void* data) {
    auto* find = static_cast<Tbox2Find*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || fopAcM_GetName(actor) != kProcTbox2) return nullptr;
    auto* chest = static_cast<daTbox2_c*>(actor);
    if (tbox2_key(actor) == find->key) {
        find->found = chest;
        find->bestDistSq = 0.0f;
        return nullptr;
    }

    const f32 dx = actor->home.pos.x - find->home.x;
    const f32 dy = actor->home.pos.y - find->home.y;
    const f32 dz = actor->home.pos.z - find->home.z;
    const f32 d = dx * dx + dy * dy + dz * dz;
    if (find->found == nullptr || d < find->bestDistSq) {
        if (d < 100.0f * 100.0f) {
            find->found = chest;
            find->bestDistSq = d;
        }
    }
    return nullptr;
}

void open_tbox2_from_message(const MsgTbox2& msg) {
    if (!coop_session(kSessDungeon, cfg_bool(s_dungeonVar, true))) return;
    Tbox2Find find{msg.key, cXyz(msg.home[0], msg.home[1], msg.home[2]), nullptr, 0.0f};
    fopAcM_Search(match_open_tbox2, &find);
    if (find.found == nullptr) return;
    daTbox2_c* chest = find.found;
    if (chest->mAction != daTbox2_c::ACTION_OPEN_WAIT_e) return;

    chest->mAction = daTbox2_c::ACTION_WAIT_e;
    chest->openInit();
    if (chest->mpBck != nullptr) {
        chest->mpBck->setFrame(chest->mpBck->getEndFrame());
        chest->mpBck->setPlaySpeed(0.0f);
    }
    coop_log::info("coop_mod: [CHEST] no-save chest {:#x} emptied in their game - closing ours",
        msg.key);
}

void tbox2_update() {
    if (!coop_net_connected() || !coop_session(kSessDungeon, cfg_bool(s_dungeonVar, true))) return;
    const char* here = dComIfGp_getStartStageName();
    if (here == nullptr) return;
    if (std::strncmp(here, s_tbox2Stage, sizeof(s_tbox2Stage) - 1) != 0) {
        std::strncpy(s_tbox2Stage, here, sizeof(s_tbox2Stage) - 1);
        s_tbox2Stage[sizeof(s_tbox2Stage) - 1] = 0;
        for (uint32_t& sent : s_tbox2Sent) sent = 0;
    }
    fopAcM_Search(collect_open_tbox2, nullptr);
}

}

void world_register_vars() {
    ConfigVarDesc dungeon = CONFIG_VAR_DESC_INIT;
    dungeon.name = "share_dungeon_progress";
    dungeon.type = CONFIG_VAR_BOOL;
    dungeon.default_bool = true;
    if (svc_config->register_var(mod_ctx, &dungeon, &s_dungeonVar) != MOD_OK) s_dungeonVar = 0;

    ConfigVarDesc story = CONFIG_VAR_DESC_INIT;
    story.name = "share_story_progress";
    story.type = CONFIG_VAR_BOOL;
    story.default_bool = false;
    if (svc_config->register_var(mod_ctx, &story, &s_storyVar) != MOD_OK) s_storyVar = 0;

    ConfigVarDesc selfTest = CONFIG_VAR_DESC_INIT;
    selfTest.name = "debug_world_selftest_ticks";
    selfTest.type = CONFIG_VAR_INT;
    selfTest.default_int = 0;
    if (svc_config->register_var(mod_ctx, &selfTest, &s_selfTestVar) != MOD_OK) s_selfTestVar = 0;

    const ModResult r = mods::hook::add_pre<TboxOpenWaitHook>(
        [](ModContext*, void* args, void* retval, void*) -> HookAction {
            auto* chest = mods::arg<daTbox_c*>(args, 0);
            if (chest == nullptr) return HOOK_CONTINUE;
            const int no = chest->getTboxNo();
            if (no < 0 || no >= 64) return HOOK_CONTINUE;

            if (!dComIfGs_isTbox(no) && !s_scrubbedByRemote[no]) return HOOK_CONTINUE;

            static bool said[64] = {};
            if (!said[no]) {
                said[no] = true;
                coop_log::info("coop_mod: [CHEST] {} is already emptied - refusing to open it", no);
            }
            if (retval != nullptr) *static_cast<int*>(retval) = 1;
            return HOOK_SKIP_ORIGINAL;
        });
    coop_log::info("coop_mod: [CHEST] open guard installed: {}", static_cast<int>(r));
}

ConfigVarHandle world_dungeon_var() {
    return s_dungeonVar;
}

ConfigVarHandle world_story_var() {
    return s_storyVar;
}

void world_update() {
    ++s_tick;
    if (!coop_net_connected()) {
        s_base.have = false;
        s_heldWorldCount = 0;
        return;
    }

    if (s_heldWorldCount > 0 && !local_mid_sequence()) {
        coop_log::info("coop_mod: [WORLD] sequence over - applying {} held update(s)",
            s_heldWorldCount);
        s_replayingWorld = true;
        for (int i = 0; i < s_heldWorldCount; ++i) {
            const HeldWorldMsg& h = s_heldWorld[i];
            world_on_message(h.type, h.bytes, h.size, h.from);
        }
        s_replayingWorld = false;
        s_heldWorldCount = 0;
    }
    if (s_tick % 10 == 0) scan();
    if (s_tick % 30 == 0 && daAlink_getAlinkActorClass() != nullptr) retry_pending_chests();

    drive_remote_chest_lids();
    tbox2_update();
    if (s_tick % kDigestEveryTicks == 0) {
        dSv_info_c* info = dComIfGs_getSaveInfo();
        char stage[8];
        int saveNo = -1;

        if (info != nullptr && current_stage(stage, saveNo)) {
            send_digest(info, stage, saveNo);
        }
    }
    run_self_test();
}

void world_on_connected() {
    s_base.have = false;
    s_lightDrop.have = false;
    s_collect.have = false;
    s_statusB.have = false;
    s_visited.have = false;
    s_visitedShareDue = true;
    s_selfTestTicks = 0;
    s_selfTestDone = false;
}

void world_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from) {
    if (type == kMsgWorldSyncRequest) {
        if (size < sizeof(MsgWorldSyncRequest) || !coop_session(kSessDungeon, cfg_bool(s_dungeonVar, true))) return;
        MsgWorldSyncRequest req;
        std::memcpy(&req, payload, sizeof(req));
        answer_sync_request(req);
        return;
    }
    if (type == kMsgTbox2) {
        if (size < sizeof(MsgTbox2)) return;
        MsgTbox2 msg;
        std::memcpy(&msg, payload, sizeof(msg));
        open_tbox2_from_message(msg);
        return;
    }
    if (type == kMsgWorldDigest) {
        if (size < sizeof(MsgWorldDigest)) return;
        MsgWorldDigest msg;
        std::memcpy(&msg, payload, sizeof(msg));
        handle_digest(msg, from);
        return;
    }

    if ((type == kMsgWorldFull || type == kMsgWorldDelta) && !s_replayingWorld &&
        local_mid_sequence() && daAlink_getAlinkActorClass() != nullptr) {
        if (s_heldWorld == nullptr) s_heldWorld = new HeldWorldMsg[kHeldWorldMax];
        if (s_heldWorldCount < kHeldWorldMax && size <= sizeof(HeldWorldMsg::bytes)) {
            HeldWorldMsg& h = s_heldWorld[s_heldWorldCount++];
            h.type = type;
            h.from = from;
            h.size = static_cast<uint16_t>(size);
            std::memcpy(h.bytes, payload, size);
            if (s_heldWorldCount == 1) {
                coop_log::info("coop_mod: [WORLD] holding their world state until our sequence "
                               "ends");
            }
            return;
        }

    }
    if (type == kMsgWorldFull) {
        if (size < sizeof(MsgWorldFull)) return;
        MsgWorldFull full;
        std::memcpy(&full, payload, sizeof(full));
        handle_full(full);
        return;
    }
    if (size < sizeof(MsgWorldDelta)) return;
    MsgWorldDelta msg;
    std::memcpy(&msg, payload, sizeof(msg));
    if (msg.size > 32) return;
    if ((msg.region == kRegionZone || msg.region == kRegionTmp) && !dungeon_stage(msg.stage)) {
        return;
    }
    const bool storyRegion = msg.region == kRegionEvent;
    if (storyRegion ? !coop_session(kSessStory, cfg_bool(s_storyVar, false)) : !coop_session(kSessDungeon, cfg_bool(s_dungeonVar, true))) return;
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (info == nullptr) return;

    char stage[8];
    int saveNo = -1;
    const bool here = current_stage(stage, saveNo) && std::memcmp(stage, msg.stage, 8) == 0 &&
                      saveNo == msg.saveNo;
    const bool baselineValid = here && s_base.have;

    const bool sameSlot = current_stage(stage, saveNo) && saveNo == msg.saveNo;
    const bool slotBaselineValid = sameSlot && s_base.have;

    switch (msg.region) {
    case kRegionMemory: {
        if (msg.saveNo < 0 || msg.saveNo >= dSv_save_c::STAGE_MAX) return;
        if (msg.offset + msg.size > kMemSize) return;
        uint8_t* target = sameSlot ? mem_bytes(info->getMemory())
                                   : mem_bytes(info->getSavedata().getSave(msg.saveNo));

        uint8_t newlySet[8] = {};
        if (sameSlot) {
            for (int i = 0; i < msg.size; ++i) {
                const int at = msg.offset + i;
                if (at >= 8) break;
                newlySet[at] = static_cast<uint8_t>(msg.set[i] & ~target[at]);
            }
        }
        apply_bytes(target, slotBaselineValid ? s_base.mem : nullptr, msg, kKeyOffset);
        if (sameSlot) {

            open_chests_from_bits(newlySet);
            remove_collected_from_bits(newlySet);
        }
        break;
    }
    case kRegionKeys: {
        if (msg.saveNo < 0 || msg.saveNo >= dSv_save_c::STAGE_MAX) return;
        uint8_t* target = sameSlot ? mem_bytes(info->getMemory())
                                   : mem_bytes(info->getSavedata().getSave(msg.saveNo));
        target[kKeyOffset] = msg.set[0];
        if (slotBaselineValid) s_base.mem[kKeyOffset] = msg.set[0];
        break;
    }
    case kRegionDan: {
        dSv_danBit_c& dan = info->getDan();
        if (!here || dan.mStageNo != msg.saveNo || msg.offset + msg.size > kDanSize) return;
        apply_bytes(reinterpret_cast<uint8_t*>(&dan) + kDanOffset,
            (baselineValid && s_base.haveDan) ? s_base.dan : nullptr, msg);
        break;
    }
    case kRegionZone: {
        if (!here || msg.room < 0 || msg.room >= kRooms || msg.offset + msg.size > kZoneBitSize) return;
        for (int i = 0; i < kZones; ++i) {
            dSv_zone_c& zone = info->getZone(i);
            if (zone.getRoomNo() != msg.room) continue;
            apply_bytes(reinterpret_cast<uint8_t*>(&zone.getBit()),
                (baselineValid && s_base.haveZone[msg.room]) ? s_base.zone[msg.room] : nullptr, msg);
            break;
        }
        break;
    }
    case kRegionZoneActor: {

        if (!here || msg.room < 0 || msg.room >= kRooms ||
            msg.offset + msg.size > kZoneActorSize) return;
        for (int i = 0; i < kZones; ++i) {
            dSv_zone_c& zone = info->getZone(i);
            if (zone.getRoomNo() != msg.room) continue;
            MsgWorldDelta onlySet = msg;
            std::memset(onlySet.clr, 0, sizeof(onlySet.clr));
            apply_bytes(reinterpret_cast<uint8_t*>(&zone.getActor()),
                (baselineValid && s_base.haveZone[msg.room]) ? s_base.zoneActor[msg.room] : nullptr,
                onlySet);
            break;
        }
        break;
    }
    case kRegionLightDrop: {

        if (msg.size != kLightDropSize) return;
        auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getLightDrop());

        for (int i = 0; i < kLightDropCounts; ++i) {
            const int sum = cur[i] + msg.set[i];
            cur[i] = static_cast<uint8_t>(sum > 255 ? 255 : sum);
        }
        cur[4] = static_cast<uint8_t>(cur[4] | msg.set[4]);

        if (s_lightDrop.have) std::memcpy(s_lightDrop.bytes, cur, kLightDropSize);
        break;
    }
    case kRegionStatusB: {

        if (msg.size != kStatusBSize) return;
        uint8_t* cur = status_b_flags(info);
        for (int i = 0; i < kStatusBSize; ++i) {
            cur[i] = static_cast<uint8_t>(cur[i] | msg.set[i]);
            if (s_statusB.have) s_statusB.bytes[i] = cur[i];
        }
        break;
    }
    case kRegionCollect: {

        if (msg.size != kCollectSize) return;
        auto* cur = reinterpret_cast<uint8_t*>(&info->getSavedata().getPlayer().getCollect());
        for (int i = 0; i < kCollectSize; ++i) {
            if (i == kCollectPohIndex) {
                if (msg.set[i] > cur[i]) cur[i] = msg.set[i];
            } else {
                cur[i] = static_cast<uint8_t>(cur[i] | msg.set[i]);
            }
        }
        if (s_collect.have) std::memcpy(s_collect.bytes, cur, kCollectSize);
        break;
    }
    case kRegionVisited: {

        if (msg.room < 0 || msg.room >= kMaps || msg.size != kVisitedSize) return;
        auto* cur = reinterpret_cast<uint8_t*>(info->getSavedata().getSave2(msg.room));
        for (int i = 0; i < kVisitedSize; ++i) {
            cur[i] = static_cast<uint8_t>(cur[i] | msg.set[i]);
            if (s_visited.have) s_visited.maps[msg.room][i] = cur[i];
        }
        break;
    }
    case kRegionTmp: {

        if (!here || msg.offset + msg.size > kEventSize) return;
        uint8_t before[kEventSize];
        std::memcpy(before, info->getTmp().mEvent, kEventSize);
        apply_bytes(info->getTmp().mEvent,
            (baselineValid && s_base.haveTmp) ? s_base.tmp : nullptr, msg);
        keep_private_tmp(info->getTmp().mEvent, before);
        break;
    }
    case kRegionEvent: {
        if (msg.offset + msg.size > kEventSize) return;
        uint8_t before[kEventSize];
        std::memcpy(before, info->getSavedata().getEvent().mEvent, kEventSize);
        apply_bytes(info->getSavedata().getEvent().mEvent,
            (s_base.have && s_base.haveEvent) ? s_base.event : nullptr, msg);
        keep_private_event(info->getSavedata().getEvent().mEvent, before);
        break;
    }
    default:
        return;
    }
    coop_log::info("coop_mod: [WORLD] applied region={} stage={:.8s} save={} room={} {}+{} ({})",
        static_cast<int>(msg.region), msg.stage, static_cast<int>(msg.saveNo),
        static_cast<int>(msg.room), static_cast<int>(msg.offset), static_cast<int>(msg.size),
        here ? "live" : "saved");
}

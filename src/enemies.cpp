

#include "mod.hpp"
#include "enemy_layout.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "SSystem/SComponent/c_lib.h"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_obj_iceblock.h"
#include "d/actor/d_a_cstatue.h"
#include "d/actor/d_a_cstaF.h"
#include "d/actor/d_a_crod.h"
#include "d/actor/d_a_obj_carry.h"
#include "d/actor/d_a_obj_lv6FurikoTrap.h"
#include "d/actor/d_a_obj_lv6TogeRoll.h"
#include "d/actor/d_a_obj_lv6TogeTrap.h"
#include "d/actor/d_a_obj_rotTrap.h"
#include "d/actor/d_a_obj_togeTrap.h"

#include "dungeon_blob_includes.inc"
#include "d/actor/d_a_obj_firepillar.h"
#include "d/actor/d_a_obj_firepillar2.h"
#include "d/actor/d_a_obj_geyser.h"
#include "d/actor/d_a_obj_waterPillar.h"
#include "d/actor/d_a_obj_lv1Candle00.h"
#include "d/actor/d_a_obj_lv1Candle01.h"
#include "d/actor/d_a_obj_lv2Candle.h"
#include "d/actor/d_a_obj_lv3Candle.h"
#include "d/actor/d_a_obj_fireWood.h"
#include "d/actor/d_a_obj_fireWood2.h"
#include "d/actor/d_a_cow.h"
#include "d/actor/d_a_ni.h"
#include "d/actor/d_a_obj_lv6swturn.h"
#include "d/actor/d_a_obj_swturn.h"
#include "d/actor/d_a_player.h"
#include "m_Do/m_Do_ext.h"
#include "d/d_particle.h"
#include "d/d_kankyo.h"

#include "d/actor/d_a_e_ai.h"
#include "d/actor/d_a_e_arrow.h"
#include "d/actor/d_a_e_ba.h"
#include "d/actor/d_a_e_bg.h"
#include "d/actor/d_a_e_bi.h"
#include "d/actor/d_a_e_bs.h"
#include "d/actor/d_a_e_bu.h"
#include "d/actor/d_a_e_cr.h"
#include "d/actor/d_a_e_cr_egg.h"
#include "d/actor/d_a_e_db.h"
#include "d/actor/d_a_e_dd.h"
#include "d/actor/d_a_e_df.h"
#include "d/actor/d_a_e_dk.h"
#include "d/actor/d_a_e_dn.h"
#include "d/actor/d_a_e_dt.h"
#include "d/actor/d_a_e_fb.h"
#include "d/actor/d_a_e_fk.h"
#include "d/actor/d_a_e_fm.h"
#include "d/actor/d_a_e_fs.h"
#include "d/actor/d_a_e_fz.h"
#include "d/actor/d_a_e_gb.h"
#include "d/actor/d_a_e_ge.h"
#include "d/actor/d_a_e_gi.h"
#include "d/actor/d_a_e_gob.h"
#include "d/actor/d_a_e_hm.h"
#include "d/actor/d_a_e_sb.h"
#include "d/actor/d_a_e_gm.h"
#include "d/actor/d_a_e_gs.h"
#include "d/actor/d_a_e_hb.h"
#include "d/actor/d_a_e_hp.h"
#include "d/actor/d_a_e_hz.h"
#include "d/actor/d_a_e_hzelda.h"
#include "d/actor/d_a_e_is.h"
#include "d/actor/d_a_e_kg.h"
#include "d/actor/d_a_e_kk.h"
#include "d/actor/d_a_e_kr.h"
#include "d/actor/d_a_e_mb.h"
#include "d/actor/d_a_e_md.h"
#include "d/actor/d_a_e_mf.h"
#include "d/actor/d_a_e_mk.h"
#include "d/actor/d_a_e_mk_bo.h"
#include "d/actor/d_a_e_mm.h"
#include "d/actor/d_a_e_mm_mt.h"
#include "d/actor/d_a_e_ms.h"
#include "d/actor/d_a_e_nest.h"
#include "d/actor/d_a_e_nz.h"
#include "d/actor/d_a_e_oc.h"
#include "d/actor/d_a_e_ot.h"
#include "d/actor/d_a_e_ph.h"
#include "d/actor/d_a_e_pm.h"
#include "d/actor/d_a_e_pz.h"
#include "d/actor/d_a_e_rb.h"
#include "d/actor/d_a_e_rd.h"
#include "d/actor/d_a_e_rdb.h"
#include "d/actor/d_a_e_rdy.h"
#include "d/actor/d_a_e_s1.h"
#include "d/actor/d_a_e_sf.h"
#include "d/actor/d_a_e_sg.h"
#include "d/actor/d_a_e_sh.h"
#include "d/actor/d_a_e_sm.h"
#include "d/actor/d_a_e_sm2.h"
#include "d/actor/d_a_e_st.h"
#include "d/actor/d_a_e_sw.h"
#include "d/actor/d_a_e_th.h"
#include "d/actor/d_a_e_th_ball.h"
#include "d/actor/d_a_e_tk.h"
#include "d/actor/d_a_e_tk2.h"
#include "d/actor/d_a_e_tk_ball.h"
#include "d/actor/d_a_e_tt.h"
#include "d/actor/d_a_e_ws.h"
#include "d/actor/d_a_e_ww.h"
#include "d/actor/d_a_e_yc.h"
#include "d/actor/d_a_e_yd.h"
#include "d/actor/d_a_e_yg.h"
#include "d/actor/d_a_e_yh.h"
#include "d/actor/d_a_e_ym.h"
#include "d/actor/d_a_e_ymb.h"
#include "d/actor/d_a_e_yr.h"
#include "d/actor/d_a_e_zh.h"
#include "d/actor/d_a_e_zm.h"
#include "d/actor/d_a_e_zs.h"
#include "d/d_com_inf_game.h"

#include "d/d_save.h"
#include "f_op/f_op_actor_iter.h"
#include "f_op/f_op_actor_mng.h"
#include "d/d_cc_d.h"
#include "d/d_cc_s.h"
#include "f_pc/f_pc_executor.h"

#include "mods/svc/hook.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstddef>

DEFINE_HOOK(&fpcEx_Execute, EnemyExecuteHook);

DEFINE_HOOK_SYMBOL("daCstatue_c::setAnime", void(daCstatue_c*), CoopStatueSetAnimeHook);

DEFINE_HOOK_SYMBOL("daCstaF_c::setAnime", void(daCstaF_c*), CoopSmallStatueSetAnimeHook);

DEFINE_HOOK(&dCcS::Move, EnemyCollisionHook);

namespace {

const int kMaxTracked = 96;
const int kKeyCacheMax = 192;
const int kSendEveryTicks = 2;
const int kDiagEveryTicks = 300;
const int kDumpMax = 60;
const f32 kSnapDistance = 400.0f;

const int kUnpauseSnapTicks = 10;
int s_unpauseSnapTicks = 0;
const f32 kPosLerp = 0.5f;
const int kStaleTicks = 30;
const int kForceDeleteTicks = 120;
const int kMaxRelayedDamage = 200;
const int kRooms = 64;

ConfigVarHandle s_enableVar = 0;
ConfigVarHandle s_positionsVar = 0;
ConfigVarHandle s_selfTestVar = 0;

ConfigVarHandle s_realHitsVar = 0;
ConfigVarHandle s_breakablesVar = 0;
ConfigVarHandle s_moversVar = 0;
const int kHitRelayQuietTicks = 20;
const int kCaptureQuietTicks = 8;
const int kMaxPendingHits = 8;
const int16_t kProcNbomb = 0x221;

struct PendingHit {
    bool used = false;
    MsgEnemyHit msg{};
};

const int kMaxPendingObjectHits = 24;
PendingHit s_pendingObjectHits[kMaxPendingObjectHits];
PendingHit s_pendingHits[kMaxPendingHits];
int s_hitsSent = 0;
int s_hitsApplied = 0;

ConfigVarHandle s_roomOwnerVar = 0;

struct RoomOwnership {
    char stage[8] = {};
    int8_t saveNo = -1;
    uint8_t owner[kRooms] = {};
    bool claimed[kRooms] = {};
    uint32_t claimTick[kRooms] = {};
};
const uint32_t kClaimRetryTicks = 60;
RoomOwnership s_rooms;

bool s_bossRoom[kRooms] = {};

bool boss_room(int room) {
    return room >= 0 && room < kRooms && s_bossRoom[room];
}

ConfigVarHandle s_targetVar = 0;

ConfigVarHandle s_decisionsVar = 0;

struct RetargetSet {
    fpc_ProcID ids[kMaxTracked];
    f32 blend[kMaxTracked];

    uint8_t target[kMaxTracked];
    int count = 0;
    fpc_ProcID lo = 0;
    fpc_ProcID hi = 0;
};
RetargetSet s_retarget;

struct RemotePose {
    bool valid = false;
    f32 x = 0.0f, y = 0.0f, z = 0.0f;
    s16 angleY = 0;
    f32 sx = 0.0f, sz = 0.0f;
};

RemotePose s_remotePoses[kCoopMaxPlayers];

const f32 kTargetMargin = 150.0f;

const int kTargetBlendTicks = 20;

const f32 kContactRange = 220.0f;

struct PlayerLie {
    fopAc_ac_c* player = nullptr;
    bool active = false;
    cXyz savedCur, savedOld, savedEye, savedSpeed;
    csXyz savedShape, savedCurAngle;
    f32 savedSpeedF = 0.0f;
    cXyz wroteCur, wroteOld, wroteEye, wroteSpeed;
    csXyz wroteShape, wroteCurAngle;
    f32 wroteSpeedF = 0.0f;
};
PlayerLie s_lie;
int s_lieDepth = 0;
int s_retargetedThisTick = 0;

uint32_t s_tick = 0;

uint32_t s_worldFrames = 0;

int s_eventSettleTicks = 0;
const int kEventSettleTicks = 120;
uint32_t s_selfTestTicks = 0;
bool s_selfTestDone = false;
uint32_t s_selfTestStuckKey = 0;
int s_selfTestStuck = 0;
int s_dumped = 0;
int s_diagMatched = 0;
int s_diagUnmatched = 0;
int s_diagOwned = 0;

int s_diagConflicts = 0;

int s_diagLonely = 0;

int s_diagInstructed = 0;

uint32_t fnv(uint32_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

int32_t round_unit(f32 v) {
    return static_cast<int32_t>(std::floor(v + 0.5f));
}

bool is_ice_block(fopAc_ac_c* actor) {
    return actor != nullptr && fopAcM_GetName(actor) == fpcNm_Obj_IceBlock_e;
}

bool keyed_by_param(fopAc_ac_c* actor) {
    return is_ice_block(actor);
}

uint32_t compute_placement_key(fopAc_ac_c* actor) {
    uint32_t h = 2166136261u;
    const int16_t name = fopAcM_GetName(actor);
    const uint16_t setID = actor->setID;
    if (setID != 0xFFFF) {
        h = fnv(h, &name, sizeof(name));
        h = fnv(h, &setID, sizeof(setID));
        return h != 0 ? h : 1u;
    }
    const uint32_t param = fopAcM_GetParam(actor);
    if (keyed_by_param(actor)) {
        h = fnv(h, &name, sizeof(name));
        h = fnv(h, &param, sizeof(param));
        return h != 0 ? h : 1u;
    }
    const int32_t x = round_unit(actor->home.pos.x);
    const int32_t y = round_unit(actor->home.pos.y);
    const int32_t z = round_unit(actor->home.pos.z);
    const int16_t angleY = actor->home.angle.y;
    h = fnv(h, &name, sizeof(name));
    h = fnv(h, &param, sizeof(param));
    h = fnv(h, &setID, sizeof(setID));
    h = fnv(h, &x, sizeof(x));
    h = fnv(h, &y, sizeof(y));
    h = fnv(h, &z, sizeof(z));
    h = fnv(h, &angleY, sizeof(angleY));
    return h != 0 ? h : 1u;
}

const int kSeqSlots = 48;

const int kSettleTicks = 90;
int s_settleTicks = kSettleTicks;

bool s_settledRoom[kRooms] = {};

struct SeqCounter {
    bool used = false;
    int8_t room = -1;
    int16_t procName = 0;

    uint16_t parentSetID = 0xFFFF;
    uint16_t next = 0;
};
SeqCounter s_seq[kSeqSlots];

void reset_sequences() {
    for (int i = 0; i < kSeqSlots; ++i) s_seq[i] = SeqCounter{};
    for (int i = 0; i < kRooms; ++i) s_settledRoom[i] = false;
    s_settleTicks = kSettleTicks;
}

bool s_roomLoaded[kRooms] = {};

bool room_is_loaded(int8_t room) {
    return room >= 0 && room < kRooms && s_roomLoaded[room];
}

void note_loaded_rooms(dSv_info_c* info) {
    if (info == nullptr) return;
    for (int i = 0; i < kRooms; ++i) s_roomLoaded[i] = false;
    for (int i = 0; i < 32; ++i) {
        const int r = info->getZone(i).getRoomNo();
        if (r >= 0 && r < kRooms) s_roomLoaded[r] = true;
    }
    for (int i = 0; i < 32; ++i) {
        const int room = info->getZone(i).getRoomNo();
        if (room < 0 || room >= kRooms || s_settledRoom[room]) continue;
        s_settledRoom[room] = true;
        s_settleTicks = kSettleTicks;
        coop_log::info("coop_mod: [ENEMY] room {} just loaded - anything appearing after this is "
                        "a runtime spawn", room);
    }
}

const uint16_t kSeqExhausted = 0xFFFF;

uint16_t spawn_parent_set_id(fopAc_ac_c* actor) {
    if (actor == nullptr) return 0xFFFF;
    const fpc_ProcID parent = actor->parentActorID;
    if (parent == fpcM_ERROR_PROCESS_ID_e) return 0xFFFF;
    auto* p = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(parent));
    if (p == nullptr) return 0xFFFF;
    return p->setID;
}

uint16_t next_sequence(int8_t room, int16_t procName, uint16_t parentSetID) {
    for (int i = 0; i < kSeqSlots; ++i) {
        if (s_seq[i].used && s_seq[i].room == room && s_seq[i].procName == procName &&
            s_seq[i].parentSetID == parentSetID) {
            return s_seq[i].next++;
        }
    }
    for (int i = 0; i < kSeqSlots; ++i) {
        if (s_seq[i].used) continue;
        s_seq[i].used = true;
        s_seq[i].room = room;
        s_seq[i].procName = procName;
        s_seq[i].parentSetID = parentSetID;
        s_seq[i].next = 1;
        return 0;
    }

    return kSeqExhausted;
}

uint32_t compute_sequence_key(fopAc_ac_c* actor) {
    const int16_t name = fopAcM_GetName(actor);
    const int8_t room = fopAcM_GetRoomNo(actor);
    const uint16_t parentSetID = spawn_parent_set_id(actor);
    const uint16_t seq = next_sequence(room, name, parentSetID);
    if (seq == kSeqExhausted) return 0;
    uint32_t h = 2166136261u;
    const uint32_t tag = 0x53455121u;
    h = fnv(h, &tag, sizeof(tag));
    h = fnv(h, &name, sizeof(name));
    h = fnv(h, &room, sizeof(room));
    h = fnv(h, &parentSetID, sizeof(parentSetID));
    h = fnv(h, &seq, sizeof(seq));
    return h != 0 ? h : 2u;
}

struct KeyCacheEntry {
    fpc_ProcID id = 0;
    uint32_t key = 0;
    bool unnamed = false;
    bool runtime = false;
    int16_t procName = 0;
    uint32_t used = 0;
};
KeyCacheEntry s_keyCache[kKeyCacheMax];
int s_keyCacheEvictions = 0;
int s_unnamedLogged = 0;

uint32_t placement_key(fopAc_ac_c* actor) {
    const fpc_ProcID id = fopAcM_GetID(actor);
    int slot = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < kKeyCacheMax; ++i) {
        if (s_keyCache[i].key != 0 && s_keyCache[i].id == id) {
            s_keyCache[i].used = s_tick;
            return s_keyCache[i].unnamed ? 0u : s_keyCache[i].key;
        }

        const uint32_t age = s_keyCache[i].key == 0 ? 0 : s_keyCache[i].used + 1;
        if (age < oldest) {
            oldest = age;
            slot = i;
        }
    }
    if (slot < 0) slot = 0;
    if (s_keyCache[slot].key != 0) {
        ++s_keyCacheEvictions;
        if (s_keyCacheEvictions <= 4) {
            coop_log::warn("coop_mod: [ENEMY] key cache full ({} entries) - evicting a key last "
                            "used {} ticks ago. If this repeats, enemies are being renamed.",
                kKeyCacheMax, static_cast<int>(s_tick - s_keyCache[slot].used));
        }
    }

    const bool runtime = s_settleTicks <= 0;
    const uint32_t key = runtime ? compute_sequence_key(actor) : compute_placement_key(actor);

    s_keyCache[slot].id = id;
    s_keyCache[slot].key = key != 0 ? key : 1u;
    s_keyCache[slot].unnamed = key == 0;
    s_keyCache[slot].runtime = runtime;
    s_keyCache[slot].used = s_tick;
    if (key == 0 && s_unnamedLogged < 4) {
        ++s_unnamedLogged;
        coop_log::warn("coop_mod: [ENEMY-DYN] ran out of sequence slots naming name={} in room "
                        "{} - leaving it local on both sides rather than risk a shared identity",
            static_cast<int>(fopAcM_GetName(actor)), static_cast<int>(fopAcM_GetRoomNo(actor)));
    }
    if (s_dumped < kDumpMax) {
        ++s_dumped;
        coop_log::info("coop_mod: [ENEMY-DUMP] {} key={:#010x} room={} name={} param={:#x} "
                        "set={:#x} home=({},{},{}) angY={} hp={}",
            runtime ? "runtime" : "placed",
            key, static_cast<int>(fopAcM_GetRoomNo(actor)), static_cast<int>(fopAcM_GetName(actor)),
            fopAcM_GetParam(actor), static_cast<int>(actor->setID), round_unit(actor->home.pos.x),
            round_unit(actor->home.pos.y), round_unit(actor->home.pos.z),
            static_cast<int>(actor->home.angle.y), static_cast<int>(actor->health));
    }
    return key;
}

struct EnemyList {
    fopAc_ac_c* actors[kMaxTracked];
    uint32_t keys[kMaxTracked];
    int8_t rooms[kMaxTracked];
    int count;
    int enemyActors;
};

bool procname_is_boss(s16 name) {
    switch (name) {

    case 0x07D:
    case 0x0D2:
    case 0x0F3:
    case 0x0F6:
    case 0x0F7:
    case 0x0F8:
    case 0x0F9:
    case 0x0FA:
    case 0x20B:
    case 0x20C:
    case 0x20D:
    case 0x20E:
    case 0x20F:
    case 0x210:
    case 0x211:
    case 0x212:
    case 0x213:
    case 0x214:
    case 0x215:
    case 0x216:
    case 0x2F1:

    case 0x1DB:
    case 0x1DC:
    case 0x2ED:
        return true;
    default:
        return false;
    }
}

bool syncable(fopAc_ac_c* actor) {

    if (procname_is_boss(fopAcM_GetName(actor))) return false;

    if (boss_room(fopAcM_GetRoomNo(actor))) return false;
    return (actor->actor_status & fopAcStts_BOSS_e) == 0;
}

void* mark_boss_room(void* proc, void* data) {
    (void)data;
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr) return nullptr;
    const s16 name = fopAcM_GetName(actor);
    if (!procname_is_boss(name) && (actor->actor_status & fopAcStts_BOSS_e) == 0) return nullptr;

    if (boss_is_supported_procname(name)) return nullptr;
    const int room = fopAcM_GetRoomNo(actor);
    if (room < 0 || room >= kRooms || s_bossRoom[room]) return nullptr;
    s_bossRoom[room] = true;
    coop_log::info("coop_mod: [ENEMY] room {} holds a boss ({}) - everything in it runs locally "
                    "from now on", room, static_cast<int>(fopAcM_GetName(actor)));
    return nullptr;
}

void* collect_enemy(void* proc, void* data) {
    auto* list = static_cast<EnemyList*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr) return nullptr;

    const bool trackedObject = fopAcM_GetName(actor) == kProcNbomb;

    if (fopAcM_GetGroup(actor) != fopAc_ENEMY_e && !trackedObject) return nullptr;

    if (fopAcM_GetName(actor) == fpcNm_NI_e) return nullptr;
    ++list->enemyActors;

    if (!syncable(actor)) return nullptr;
    if (list->count >= kMaxTracked) return nullptr;

    const uint32_t key = placement_key(actor);
    if (key == 0) return nullptr;
    const int idx = list->count++;
    list->actors[idx] = actor;
    list->keys[idx] = key;
    list->rooms[idx] = fopAcM_GetRoomNo(actor);
    return nullptr;
}

void collect_enemies(EnemyList& list) {
    list.count = 0;
    list.enemyActors = 0;
    fopAcM_Search(mark_boss_room, nullptr);
    fopAcM_Search(collect_enemy, &list);
}

void apply_hidden(fopAc_ac_c* actor, bool hidden) {
    if (hidden) {
        fopAcM_OnStatus(actor, fopAcStts_NODRAW_e);
    } else {
        fopAcM_OffStatus(actor, fopAcStts_NODRAW_e);
    }
}

bool in_gameplay() {
    return daAlink_getAlinkActorClass() != nullptr && !dComIfGp_event_runCheck() &&
           !dComIfGp_isEnableNextStage() && dComIfGp_getStageStagInfo() != nullptr;
}

bool enemies_setting_on() {
    return coop_session(kSessEnemies, cfg_bool(s_enableVar, false));
}

bool bosses_setting_on() {
    return coop_session(kSessBosses, cfg_bool(boss_enabled_var(), false));
}

bool session_live() {
    return coop_net_connected() && in_gameplay() && peer_on_our_stage();
}

bool enemies_enabled_now() {
    return session_live() && enemies_setting_on();
}

bool breakables_enabled();
bool movers_enabled();

bool objects_live() {
    return session_live() && (breakables_enabled() || movers_enabled());
}

bool claims_live() {
    return session_live() && (enemies_setting_on() || bosses_setting_on());
}

fopAc_ac_c* find_local(EnemyList& list, int8_t room, uint32_t key) {
    for (int i = 0; i < list.count; ++i) {
        if (list.rooms[i] == room && list.keys[i] == key) return list.actors[i];
    }
    return nullptr;
}

struct Tracked {
    bool used = false;
    int8_t room = -1;
    uint32_t key = 0;
    fpc_ProcID id = 0;
    int16_t health = 0;
    bool seen = false;
    bool killed = false;
    int killedTicks = 0;

    bool runtime = false;
    int16_t procName = 0;

    uint8_t targetPlayer = kCoopHostId;

    uint8_t blendPlayer = kCoopNoPlayer;

    bool decisionKnown = false;
    int16_t lastAction = 0;
    int16_t lastMode = 0;

    bool targetKnown = false;

    uint32_t targetStamp = 0;

    bool carriedByUs = false;
    f32 targetBlend = 0.0f;

    int lonelyTicks = 0;
    bool lonelyReported = false;
    int hitQuietTicks = 0;

    int captureQuietTicks = 0;
    bool goneSent = false;
    bool deleteAsked = false;
};
Tracked s_tracked[kMaxTracked];

struct Remote {
    bool used = false;
    int8_t room = -1;
    uint32_t key = 0;
    int16_t procName = 0;
    f32 x = 0.0f, y = 0.0f, z = 0.0f;
    int16_t angle[3] = {0, 0, 0};
    int16_t health = 0;
    uint8_t flags = 0;
    int16_t anmId = -1;
    f32 anmFrame = 0.0f;
    f32 anmRate = 0.0f;
    uint8_t anmMode = 0;
    bool gone = false;
    uint32_t stamp = 0;

    bool haveVel = false;
    f32 vx = 0.0f, vy = 0.0f, vz = 0.0f;
    uint8_t owner = kCoopHostId;

    uint8_t anmMismatch = 0;

    int16_t action = kEnemyNoAction;
    int16_t mode = 0;
    int16_t timers[5] = {0, 0, 0, 0, 0};
    uint8_t timerCount = 0;
    int8_t extraState = kEnemyNoState;
};
Remote s_remote[kMaxTracked];

Tracked* find_tracked(int8_t room, uint32_t key) {
    for (int i = 0; i < kMaxTracked; ++i) {
        if (s_tracked[i].used && s_tracked[i].room == room && s_tracked[i].key == key) {
            return &s_tracked[i];
        }
    }
    return nullptr;
}

bool key_is_runtime(fopAc_ac_c* actor) {
    const fpc_ProcID id = fopAcM_GetID(actor);
    for (int i = 0; i < kKeyCacheMax; ++i) {
        if (s_keyCache[i].key != 0 && s_keyCache[i].id == id) return s_keyCache[i].runtime;
    }
    return false;
}

Tracked* add_tracked(int8_t room, uint32_t key) {
    for (int i = 0; i < kMaxTracked; ++i) {
        if (!s_tracked[i].used) {
            s_tracked[i] = Tracked{};
            s_tracked[i].used = true;
            s_tracked[i].room = room;
            s_tracked[i].key = key;
            return &s_tracked[i];
        }
    }
    return nullptr;
}

Remote* find_or_add_remote(int8_t room, uint32_t key) {
    for (int i = 0; i < kMaxTracked; ++i) {
        if (s_remote[i].used && s_remote[i].room == room && s_remote[i].key == key) {
            return &s_remote[i];
        }
    }
    for (int i = 0; i < kMaxTracked; ++i) {
        if (!s_remote[i].used) {
            s_remote[i] = Remote{};
            s_remote[i].used = true;
            s_remote[i].room = room;
            s_remote[i].key = key;
            return &s_remote[i];
        }
    }
    return nullptr;
}

void reset_movers();

void reset_tables() {
    for (int i = 0; i < kMaxTracked; ++i) {
        s_tracked[i] = Tracked{};
        s_remote[i] = Remote{};
    }
    s_retarget.count = 0;
    s_retargetedThisTick = 0;
    for (int i = 0; i < kMaxPendingHits; ++i) s_pendingHits[i] = PendingHit{};

    for (int i = 0; i < kMaxPendingObjectHits; ++i) s_pendingObjectHits[i] = PendingHit{};
    reset_movers();
    s_selfTestTicks = 0;
    s_selfTestDone = false;
}

bool tables_busy() {
    for (int i = 0; i < kMaxTracked; ++i) {
        if (s_tracked[i].used || s_remote[i].used) return true;
    }
    return false;
}

uint8_t owner_of(uint8_t playerId) {
    return static_cast<uint8_t>(playerId + 1);
}

uint8_t player_of_owner(uint8_t owner) {
    return owner == kRoomOwnerNone ? kCoopNoPlayer : static_cast<uint8_t>(owner - 1);
}

uint8_t our_owner_id() {
    return owner_of(coop_net_local_id());
}

bool room_ownership_enabled() {
    return cfg_bool(s_roomOwnerVar, true);
}

void reset_rooms() {
    reset_sequences();
    s_rooms = RoomOwnership{};
    for (int i = 0; i < kRooms; ++i) s_bossRoom[i] = false;
}

bool room_stage(char out[8], int& saveNo) {
    if (daAlink_getAlinkActorClass() == nullptr) return false;
    const char* name = dComIfGp_getStartStageName();
    stage_stag_info_class* info = dComIfGp_getStageStagInfo();
    if (name == nullptr || info == nullptr) return false;
    saveNo = dStage_stagInfo_GetSaveTbl(info);
    std::memset(out, 0, 8);
    std::strncpy(out, name, 8);
    return true;
}

void set_room_owner(int room, uint8_t owner) {
    if (room < 0 || room >= kRooms) return;
    if (s_rooms.owner[room] == owner) return;
    s_rooms.owner[room] = owner;
    coop_log::info("coop_mod: [ROOM] room {} is now owned by {}", room,
        owner == kRoomOwnerNone ? -1 : static_cast<int>(player_of_owner(owner)));
}

void update_room_claims(int myRoom) {
    char stage[8];
    int saveNo = -1;
    if (!room_stage(stage, saveNo)) return;
    if (std::memcmp(stage, s_rooms.stage, 8) != 0 || saveNo != s_rooms.saveNo) {
        reset_rooms();
        std::memcpy(s_rooms.stage, stage, 8);
        s_rooms.saveNo = static_cast<int8_t>(saveNo);
    }

    const uint8_t us = our_owner_id();
    const bool wePaused = coop_player_paused(coop_net_local_id());
    for (int r = 0; r < kRooms; ++r) {
        const uint8_t owner = s_rooms.owner[r];
        if (owner == kRoomOwnerNone) continue;
        if (owner == us) {

            if (r != myRoom || wePaused) {
                set_room_owner(r, kRoomOwnerNone);
                s_rooms.claimed[r] = false;
            }
            continue;
        }

        const uint8_t who = player_of_owner(owner);
        if (who == kCoopNoPlayer || !coop_net_player_present(who) || coop_player_paused(who) ||
            static_cast<int>(features_peer_of(who).curRoom) != r ||
            std::strncmp(stage, features_peer_of(who).stage, 8) != 0) {
            set_room_owner(r, kRoomOwnerNone);
        }

        s_rooms.claimed[r] = false;
    }

    if (myRoom < 0 || myRoom >= kRooms) return;

    if (wePaused) return;

    if (s_rooms.claimed[myRoom] && s_rooms.owner[myRoom] == kRoomOwnerNone &&
        s_tick - s_rooms.claimTick[myRoom] > kClaimRetryTicks)
    {
        s_rooms.claimed[myRoom] = false;
    }
    if (s_rooms.owner[myRoom] != kRoomOwnerNone || s_rooms.claimed[myRoom]) return;

    s_rooms.claimed[myRoom] = true;
    s_rooms.claimTick[myRoom] = s_tick;
    if (coop_net_is_host()) {
        set_room_owner(myRoom, us);
        MsgRoomOwner msg{};
        std::memcpy(msg.stage, stage, 8);
        msg.saveNo = static_cast<int8_t>(saveNo);
        msg.room = static_cast<int8_t>(myRoom);
        msg.owner = us;
        coop_net_send(kMsgRoomOwner, &msg, sizeof(msg));
    } else {
        MsgRoomClaim msg{};
        std::memcpy(msg.stage, stage, 8);
        msg.saveNo = static_cast<int8_t>(saveNo);
        msg.room = static_cast<int8_t>(myRoom);
        coop_net_send(kMsgRoomClaim, &msg, sizeof(msg));
    }
}

const uint32_t kOwnerStaleTicks = 20;

const uint32_t kTargetStaleTicks = 120;

bool peer_owner_is_live() {
    return coop_net_ticks_since_rx() < kOwnerStaleTicks;
}

bool player_is_live(uint8_t playerId) {
    if (playerId >= kCoopMaxPlayers) return false;

    if (playerId == coop_net_local_id()) return !coop_player_paused(playerId);
    if (!coop_net_player_present(playerId)) return false;
    if (coop_net_ticks_since_player(playerId) >= kOwnerStaleTicks) return false;

    return !coop_player_paused(playerId);
}

uint8_t intended_owner(int8_t room, uint32_t key) {
    Tracked* t = find_tracked(room, key);

    if (t != nullptr && t->carriedByUs) return coop_net_local_id();
    if (t != nullptr && t->targetKnown && s_tick - t->targetStamp <= kTargetStaleTicks) {
        return t->targetPlayer;
    }
    if (!room_ownership_enabled() || room < 0 || room >= kRooms) return kCoopHostId;
    const uint8_t owner = s_rooms.owner[room];
    if (owner == kRoomOwnerNone) return kCoopHostId;
    return player_of_owner(owner);
}

bool we_own(int8_t room, uint32_t key) {
    const uint8_t me = coop_net_local_id();
    if (me >= kCoopMaxPlayers) return true;
    const uint8_t owner = intended_owner(room, key);
    if (owner == me) return true;
    if (player_is_live(owner)) return false;

    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (player_is_live(static_cast<uint8_t>(i))) return static_cast<uint8_t>(i) == me;
    }
    return true;
}

#define ANM(proc, cls, morfMember, anmMember, arc)                              \
    { (int16_t)(proc), (uint16_t)offsetof(cls, morfMember),                     \
      (uint16_t)offsetof(cls, anmMember),                                       \
      sizeof(((cls*)nullptr)->anmMember) == 4, false, kEnemyArcIsLiteral, (arc), nullptr }

#define ANM_MCA(proc, cls, morfMember, anmMember, arc)                          \
    { (int16_t)(proc), (uint16_t)offsetof(cls, morfMember),                     \
      (uint16_t)offsetof(cls, anmMember),                                       \
      sizeof(((cls*)nullptr)->anmMember) == 4, true, kEnemyArcIsLiteral, (arc), nullptr }

#define ANM_NOID(proc, cls, morfMember, arc)                                        { (int16_t)(proc), (uint16_t)offsetof(cls, morfMember), kEnemyNoAnmIdOffset,       false, false, kEnemyArcIsLiteral, (arc), nullptr }

#define ANM_NOID2(proc, cls, morfMember, arcA, arcB)                                { (int16_t)(proc), (uint16_t)offsetof(cls, morfMember), kEnemyNoAnmIdOffset,       false, false, kEnemyArcIsLiteral, (arcA), (arcB) }

#define ANM_NOID_ARCF(proc, cls, morfMember, arcMember)                             { (int16_t)(proc), (uint16_t)offsetof(cls, morfMember), kEnemyNoAnmIdOffset,       false, false, (uint16_t)offsetof(cls, arcMember), nullptr, nullptr }
#define ANM_NOID_MCA(proc, cls, morfMember, arc)                                    { (int16_t)(proc), (uint16_t)offsetof(cls, morfMember), kEnemyNoAnmIdOffset,       false, true, kEnemyArcIsLiteral, (arc), nullptr }

const uint16_t kEnemyNoAnmIdOffset = 0xFFFF;

const uint16_t kEnemyArcIsLiteral = 0xFFFF;

struct EnemyAnmLayout {
    int16_t procName;
    uint16_t morfOffset;
    uint16_t anmIdOffset;
    bool anmIdIs32;

    bool morfIsMca;

    uint16_t arcOffset;
    const char* arc;

    const char* arc2;
};

const EnemyAnmLayout kEnemyAnm[] = {
    ANM_NOID2(0x1F4, daE_YM_c,    mpMorf,       "E_TM", "E_YM"),

    ANM_NOID(0x1BB, daE_GM_c,     mpModelMorf,  "E_gm"),
    ANM_NOID(0x1BC, daE_MD_c,     mpModelMorf,  "E_MD"),
    ANM_NOID(0x1BE, e_sm2_class,  modelMorf,    "E_sm2"),
    ANM_NOID(0x1C1, daE_SB_c,     mpMorf,       "E_SB"),
    ANM_NOID(0x1CF, daE_HM_c,     mAnm_p,       "E_HM"),
    ANM_NOID(0x1D6, e_rdy_class,  mpMorf,       "J_Tobi"),
    ANM_NOID(0x200, daE_DT_c,     mpMorf,       "E_DT"),
    ANM_NOID(0x201, daE_BG_c,     mpMorfSO,     "E_BG"),
    ANM_NOID(0x207, daE_DK_c,     mpMorfSO,     "E_DK"),

    ANM_NOID_ARCF(0x1EA, e_ba_class, mpMorf, mArcName),

    ANM_NOID(0x00F5, e_yc_class,  mpMorf,       "E_yc"),
    ANM_NOID(0x1E7, e_ms_class,   mpModelMorf,  "E_MS"),
    ANM_NOID(0x1F1, daE_SW_c,     mpModelMorf,  "E_SW"),
    ANM_NOID(0x1F2, daE_GE_c,     mpMorfSO,     "E_GE"),
    ANM_NOID(0x1F6, daE_YMB_c,    mpModelMorf,  "E_YB"),
    ANM_NOID(0x1FC, daE_HZ_c,     mpMorfSO,     "E_HZ"),
    ANM_NOID(0x1FD, daE_WS_c,     mAnm_p,       "E_WS"),
    ANM_NOID(0x1FE, daE_OC_c,     mpMorf,       "E_ocb"),
    ANM_NOID(0x1FF, daE_OT_c,     mpMorf,       "E_OT"),
    ANM_NOID(0x206, daE_TT_c,     mpMorfSO,     "E_TT"),
    ANM_NOID(0x209, daE_WW_c,     mpModelMorf,  "E_WW"),
    ANM_NOID(0x20A, daE_GI_c,     mpModelMorf,  "E_GI"),

    ANM(0x00E4, daE_PH_c,     mpMorf,          mAnmID,      "E_PH"),
    ANM(0x1B4, e_s1_class,    mpMorf,          mAnm,        "E_S2"),
    ANM(0x1BA, daE_DF_c,      mpMorfSO,        mAnim,       "E_DF"),
    ANM_MCA(0x1C8, e_gb_class, anmP,           headAnmNo,   "E_gb"),
    ANM(0x1CC, e_yd_class,    mpMorf,          field_0x664, "E_yd"),
    ANM(0x1D4, e_rd_class,    anm_p,           anm,         "E_rdb"),
    ANM_MCA(0x1D7, e_fm_class, mpFmModelMorf,  mAnm,        "E_fm"),
    ANM(0x1DF, daE_ZS_c,      mpMorf,          mResIndex,   "E_ZS"),
    ANM(0x1E0, daE_KK_c,      mpMorfSO,        field_0x764, "E_KK"),
    ANM(0x1E1, daE_HP_c,      mpMorfSO,        field_0x780, "E_HP"),
    ANM(0x1E4, daE_PZ_c,      mpModelMorf,     mAnm,        "E_PZ"),
    ANM(0x1E5, daE_FB_c,      mpMorf,          field_0x670, "E_FL"),
    ANM(0x1E6, daE_FK_c,      mpModelMorf,     mAnm,        "e_fk"),
    ANM(0x1EF, e_kg_class,    mpMorf,          mResIndex,   "E_kg"),
    ANM(0x1FA, e_yr_class,    mpMorfSO,        field_0x5b8, "E_Yr"),

    ANM(0x1B2, e_dd_class,    mpModelMorf,   mAnm,        "E_DD"),
    ANM(0x1CD, e_yh_class,    mpMorf,        field_0x664, "E_yd"),
    ANM(0x1D0, e_tk_class,    mpMorf,        mAnim,       "E_tk"),
    ANM(0x1D1, e_tk2_class,   mpMorf,        mAnim,       "E_tk2"),
    ANM(0x1E9, e_nz_class,    mpMorf,        field_0x5e4, "E_NZ"),
    ANM(0x1F0, e_kr_class,    mpMorf,        field_0x5b8, "E_kr"),
ANM(0x1AF, e_ai_class,    m_modelMorf,   m_anm,     "E_AI"),
    ANM(0x1B0, e_gs_class,    model_morf,    anm,       "E_gs"),
    ANM(0x1B1, e_gob_class,   mpModelMorf,   mAnm,      "E_gob"),
    ANM(0x1B3, e_dn_class,    anm_p,         anm_no,    "E_dn"),
    ANM(0x1B5, e_mf_class,    mpModelMorf,   mAnmID,    "E_mf"),
    ANM(0x1B7, e_bs_class,    modelMorf,     anm,       "E_BS"),
    ANM(0x1B8, e_sf_class,    mpModelMorf,   mAnm,      "E_sf"),
    ANM(0x1B9, e_sh_class,    mAnm_p,        mCurAnm,   "E_sh"),
    ANM(0x1BD, daE_SM_c,      mpModelMorf,   mAnm,      "E_SM"),
    ANM(0x1BF, e_st_class,    mpModelMorf,   mAnm,      "E_st"),
    ANM(0x1C2, e_th_class,    mpModelMorf,   mAnm,      "E_th"),
    ANM(0x1C3, e_cr_class,    modelMorf,     anm,       "E_CR"),
    ANM(0x1C5, e_db_class,    modelMorf,     anm,       "E_db"),
    ANM(0x1C9, e_hb_class,    modelMorf,     anm,       "E_hb"),
    ANM(0x1CB, e_hzelda_class, mpModelMorf,   mAnm,      "Hzelda"),
    ANM(0x1D3, e_rb_class,    modelMorf,     anm,       "E_rb"),
    ANM(0x1D5, e_rdb_class,   mpModelMorf,   mAnm,      "E_rdb"),
    ANM(0x1D8, e_fs_class,    mpMorf,        mAnm,      "E_FS"),
    ANM(0x1D9, daE_PM_c,      mpMorf,        mAnm,      "E_PM"),
    ANM(0x1DB, e_mb_class,    mpModelMorf,   mAnm,      "E_mb"),
    ANM(0x1DC, e_mk_class,    anmP,          anmNo,     "E_mk"),
    ANM(0x1DD, e_mm_class,    modelMorf,     anm,       "E_MM"),
    ANM(0x1E2, daE_ZH_c,      mpModelMorf,   mAnm,      "E_ZH"),
    ANM(0x1E3, daE_ZM_c,      mpModelMorf,   mAnm,      "E_ZM"),
    ANM(0x1EB, e_bu_class,    modelMorf,     anm,       "E_BU"),
    ANM(0x1EE, e_is_class,    model_morf,    anm,       "E_IS"),
    ANM(0x1FB, e_yg_class,    mpMorf,        mAnm,      "E_YG"),
    ANM(0x304, e_bi_class,    anm_p,         anm_no,    "E_BI"),
};

#undef ANM

void log_anm_table() {
    coop_log::info("coop_mod: [ENEMY-ANM] {} classes covered", (int)(sizeof(kEnemyAnm) / sizeof(kEnemyAnm[0])));
    for (const EnemyAnmLayout& l : kEnemyAnm) {
        coop_log::info("coop_mod: [ENEMY-ANM]   proc={:#05x} morf=+{:#x} anmId=+{:#x} wide={} arc={}",
            l.procName, l.morfOffset, l.anmIdOffset, l.anmIdIs32 ? 1 : 0, l.arc != nullptr ? l.arc : "(per-actor)");
    }
}

const EnemyAnmLayout* anm_layout_ext(int16_t procName) {
    static EnemyAnmLayout scratch;
    int n = 0;
    const EnemyAnmLayoutExt* rows = coop_enemy_anm_wb(n);
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < n; ++i) {
            if (rows[i].procName != procName) continue;
            scratch.procName = rows[i].procName;
            scratch.morfOffset = rows[i].morfOffset;
            scratch.anmIdOffset = kEnemyNoAnmIdOffset;
            scratch.anmIdIs32 = false;
            scratch.morfIsMca = rows[i].morfIsMca;
            scratch.arcOffset = rows[i].arcOffset;
            scratch.arc = rows[i].arc;
            return &scratch;
        }
        rows = (pass == 0) ? coop_enemy_anm_yk(n) : coop_enemy_anm_po(n);
    }
    return nullptr;
}

const EnemyAnmLayout* anm_layout(s16 procName) {
    for (const EnemyAnmLayout& l : kEnemyAnm) {
        if (l.procName == procName) return &l;
    }
    return anm_layout_ext(procName);
}

mDoExt_morf_c* enemy_morf(fopAc_ac_c* actor, const EnemyAnmLayout* l) {
    if (actor == nullptr || l == nullptr) return nullptr;
    uintptr_t raw = 0;
    std::memcpy(&raw, reinterpret_cast<const uint8_t*>(actor) + l->morfOffset, sizeof(raw));
    if (raw < 0x10000 || (raw & (sizeof(void*) - 1)) != 0) return nullptr;
    return reinterpret_cast<mDoExt_morf_c*>(raw);
}

void enemy_set_anm(mDoExt_morf_c* morf, const EnemyAnmLayout* l, J3DAnmTransform* bck, int mode,
                   f32 morfFrames, f32 rate) {
    if (morf == nullptr || l == nullptr || bck == nullptr) return;
    if (l->morfIsMca) {
        static_cast<mDoExt_McaMorf*>(morf)->setAnm(bck, mode, morfFrames, rate, 0.0f, -1.0f,
                                                   nullptr);
    } else {
        static_cast<mDoExt_McaMorfSO*>(morf)->setAnm(bck, mode, morfFrames, rate, 0.0f, -1.0f);
    }
}

const int kAnmRevSlots = 96;

struct AnmRev {
    const void* arc = nullptr;
    const void* anm = nullptr;
    int16_t index = -1;
};
AnmRev s_anmRev[kAnmRevSlots];
int s_anmRevNext = 0;

int anm_index_of(const char* arc, const void* anm) {
    if (arc == nullptr || anm == nullptr) return -1;
    for (const AnmRev& r : s_anmRev) {
        if (r.anm == anm && r.arc == arc) return r.index;
    }
    dRes_info_c* info = dComIfG_getObjectResInfo(arc);
    if (info == nullptr || info->getArchive() == nullptr) return -1;
    const s32 count = info->getResNum();
    if (count <= 0 || count > 0x4000) return -1;
    for (s32 i = 0; i < count; ++i) {
        if (info->getRes(i) != anm) continue;
        AnmRev& slot = s_anmRev[s_anmRevNext];
        s_anmRevNext = (s_anmRevNext + 1) % kAnmRevSlots;
        slot.arc = arc;
        slot.anm = anm;
        slot.index = static_cast<int16_t>(i);
        return i;
    }
    return -1;
}

const char* enemy_arc(fopAc_ac_c* actor, const EnemyAnmLayout* l) {
    if (l == nullptr) return nullptr;
    if (l->arcOffset == kEnemyArcIsLiteral) return l->arc;
    if (actor == nullptr) return nullptr;
    const char* name = nullptr;
    std::memcpy(&name, reinterpret_cast<const uint8_t*>(actor) + l->arcOffset, sizeof(name));

    if (reinterpret_cast<uintptr_t>(name) < 0x10000) return nullptr;
    return name;
}

int enemy_anm_id(fopAc_ac_c* actor, const EnemyAnmLayout* l) {

    if (l->anmIdOffset == kEnemyNoAnmIdOffset) {
        mDoExt_morf_c* morf = enemy_morf(actor, l);
        if (morf == nullptr) return -1;
        const int hit = anm_index_of(enemy_arc(actor, l), morf->getAnm());
        if (hit >= 0 || l->arc2 == nullptr) return hit;
        return anm_index_of(l->arc2, morf->getAnm());
    }
    const uint8_t* at = reinterpret_cast<const uint8_t*>(actor) + l->anmIdOffset;
    if (l->anmIdIs32) {
        int32_t v = 0;
        std::memcpy(&v, at, sizeof(v));
        return v;
    }
    int16_t v = 0;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

void set_enemy_anm_id(fopAc_ac_c* actor, const EnemyAnmLayout* l, int id) {
    uint8_t* at = reinterpret_cast<uint8_t*>(actor) + l->anmIdOffset;
    if (l->anmIdIs32) {
        int32_t v = id;
        std::memcpy(at, &v, sizeof(v));
    } else {
        int16_t v = static_cast<int16_t>(id);
        std::memcpy(at, &v, sizeof(v));
    }
}

void read_enemy_anm(fopAc_ac_c* actor, MsgEnemyEntry& entry) {
    entry.anmId = -1;
    entry.anmFrame = 0.0f;
    entry.anmRate = 0.0f;
    entry.anmMode = 0;
    const EnemyAnmLayout* l = anm_layout(fopAcM_GetName(actor));
    if (l == nullptr) return;
    mDoExt_morf_c* morf = enemy_morf(actor, l);
    if (morf == nullptr) return;
    const int id = enemy_anm_id(actor, l);
    if (id < 0 || id > 0x3FFF) return;
    entry.anmId = static_cast<int16_t>(id);
    entry.anmFrame = morf->getFrame();
    entry.anmRate = morf->getPlaySpeed();
    entry.anmMode = static_cast<uint8_t>(morf->getPlayMode());
}

void read_enemy_decision(fopAc_ac_c* actor, int16_t& action, int16_t& mode);

void read_enemy_timers(fopAc_ac_c* actor, int16_t* out, uint8_t& count);
int8_t read_enemy_state(fopAc_ac_c* actor);

void read_enemy_decision_into(fopAc_ac_c* actor, MsgEnemyEntry& entry) {
    read_enemy_decision(actor, entry.action, entry.mode);
    for (int i = 0; i < 5; ++i) entry.timers[i] = 0;
    read_enemy_timers(actor, entry.timers, entry.timerCount);
    entry.extraState = read_enemy_state(actor);
}

const f32 kAnmFrameSlack = 3.0f;

const uint8_t kAnmSwitchAfterFrames = 3;

void apply_enemy_anm(fopAc_ac_c* actor, int16_t anmId, f32 frame, f32 rate, uint8_t mode,
    uint8_t& mismatchFrames) {
    if (anmId < 0) return;
    const EnemyAnmLayout* l = anm_layout(fopAcM_GetName(actor));
    if (l == nullptr) return;
    mDoExt_morf_c* morf = enemy_morf(actor, l);
    if (morf == nullptr) return;
    if (enemy_anm_id(actor, l) != anmId) {
        if (mismatchFrames < 0xFF) ++mismatchFrames;
        if (mismatchFrames < kAnmSwitchAfterFrames) return;
        mismatchFrames = 0;
        auto* bck = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(l->arc, anmId));
        if (bck == nullptr) return;

        enemy_set_anm(morf, l, bck, mode, 3.0f, rate);
        set_enemy_anm_id(actor, l, anmId);
        morf->setFrameF(frame);
        return;
    }
    mismatchFrames = 0;
    morf->setPlaySpeed(rate);
    const f32 drift = morf->getFrame() - frame;
    if (drift > kAnmFrameSlack || drift < -kAnmFrameSlack) morf->setFrameF(frame);
}

#define ACT(proc, cls, actionMember, modeMember)                                     { (int16_t)(proc), (uint16_t)offsetof(cls, actionMember),                          (uint16_t)offsetof(cls, modeMember),                                             (uint8_t)sizeof(((cls*)nullptr)->actionMember),                                  (uint8_t)sizeof(((cls*)nullptr)->modeMember) }

#define ACT_NOMODE(proc, cls, actionMember)                                           { (int16_t)(proc), (uint16_t)offsetof(cls, actionMember), kEnemyNoModeOffset,        (uint8_t)sizeof(((cls*)nullptr)->actionMember), (uint8_t)0 }

struct EnemyActLayout {
    int16_t procName;
    uint16_t actionOffset;
    uint16_t modeOffset;
    uint8_t actionSize;
    uint8_t modeSize;
};

const EnemyActLayout kEnemyAct[] = {

    ACT(0x00e4, daE_PH_c,                mAction,          mCAction),
    ACT(0x00f5, e_yc_class,              mAction,          mMode),
    ACT(0x01af, e_ai_class,              m_action,         m_mode),
    ACT(0x01b0, e_gs_class,              action,           mode),
    ACT(0x01b1, e_gob_class,             mAction,          mMode),
    ACT(0x01b2, e_dd_class,              mAction,          field_0x68c),
    ACT(0x01b3, e_dn_class,              action,           mode),
    ACT(0x01b4, e_s1_class,              mAction,          mMode),
    ACT(0x01b5, e_mf_class,              mAction,          field_0x5b4),
    ACT(0x01b6, e_sg_class,              mAction,          mMode),
    ACT(0x01b7, e_bs_class,              action,           mode),
    ACT(0x01b8, e_sf_class,              mAction,          mActionPhase),
    ACT(0x01b9, e_sh_class,              field_0x676,      field_0x678),
    ACT(0x01ba, daE_DF_c,                mAction,          mEatStep),
    ACT(0x01bc, daE_MD_c,                mHalfBreakMode,   mAction),
    ACT(0x01bd, daE_SM_c,                mAction,          mMode),
    ACT(0x01be, e_sm2_class,             action,           mode),
    ACT(0x01c2, e_th_class,              mAction,          mMode),
    ACT(0x01c3, e_cr_class,              action,           mode),
    ACT(0x01c4, e_cr_egg_class,          action,           mode),
    ACT(0x01c5, e_db_class,              action,           mode),
    ACT(0x01c8, e_gb_class,              headAction,       mode),
    ACT(0x01c9, e_hb_class,              action,           mode),
    ACT(0x01cb, e_hzelda_class,          mAction,          mMode),
    ACT(0x01cc, e_yd_class,              field_0x66e,      field_0x670),
    ACT(0x01cd, e_yh_class,              field_0x66e,      field_0x670),
    ACT(0x01d0, e_tk_class,              mAction,          mMode),
    ACT(0x01d1, e_tk2_class,             mAction,          mMode),
    ACT(0x01d2, e_tk_ball_class,         mAction,          mMode),
    ACT(0x01d3, e_rb_class,              action,           mode),
    ACT(0x01d4, e_rd_class,              action,           mode),
    ACT(0x01d5, e_rdb_class,             mAction,          mMode),
    ACT(0x01d6, e_rdy_class,             mAction,          mMode),
    ACT(0x01d7, e_fm_class,              mAction,          mMode),
    ACT(0x01d8, e_fs_class,              field_0x5b5,      mMode),
    ACT(0x01d9, daE_PM_c,                mAction,          mMode),
    ACT(0x01db, e_mb_class,              mAction,          mMode),
    ACT(0x01dc, e_mk_class,              action,           mode),
    ACT(0x01dd, e_mm_class,              action,           mode),
    ACT(0x01de, daE_FZ_c,                mActionMode,      mActionPhase),
    ACT(0x01df, daE_ZS_c,                mAction,          mMode),
    ACT(0x01e0, daE_KK_c,                mActionMode,      mMoveMode),
    ACT(0x01e1, daE_HP_c,                mAction,          movemode),
    ACT(0x01e2, daE_ZH_c,                mActionMode,      mMoveMode),
    ACT(0x01e3, daE_ZM_c,                mAction,          mMode),
    ACT(0x01e4, daE_PZ_c,                mActionMode,      mMoveMode),
    ACT(0x01e5, daE_FB_c,                mActionMode,      mMoveMode),
    ACT(0x01e6, daE_FK_c,                mAction,          mMode),
    ACT(0x01e7, e_ms_class,              mAction,          mMode),
    ACT(0x01e8, e_nest_class,            mAction,          mMode),
    ACT(0x01e9, e_nz_class,              mAction,          mSubAction),
    ACT(0x01ea, e_ba_class,              mAction,          mMode),
    ACT(0x01eb, e_bu_class,              action,           mode),
    ACT(0x01ee, e_is_class,              action,           mode),
    ACT(0x01ef, e_kg_class,              mAction,          field_0x678),
    ACT(0x01f0, e_kr_class,              mCurAction,       field_0x672),
    ACT(0x01f1, daE_SW_c,                mActionMode,      mMoveMode),
    ACT(0x01f2, daE_GE_c,                mSubMode,         mMode),
    ACT(0x01f4, daE_YM_c,                mAction,          mMode),
    ACT(0x01f6, daE_YMB_c,               mAction,          mMode),
    ACT(0x01fa, e_yr_class,              field_0x66b,      field_0x67d),
    ACT(0x01fc, daE_HZ_c,                mAction,          mMode),
    ACT(0x01fd, daE_WS_c,                mAction,          mMode),
    ACT(0x01fe, daE_OC_c,                mActionMode,      mOcState),
    ACT(0x01ff, daE_OT_c,                mAction,          mMode),
    ACT(0x0200, daE_DT_c,                mChestMode,       mMode),
    ACT(0x0201, daE_BG_c,                mActionMode,      mMoveMode),
    ACT(0x0206, daE_TT_c,                mAction,          mMode),
    ACT(0x0207, daE_DK_c,                mActionMode,      mMoveMode),
    ACT(0x0209, daE_WW_c,                mAction,          mActionMode),
    ACT(0x020a, daE_GI_c,                mActionMode,      mMoveMode),
    ACT(0x02e5, e_arrow_class,           mAction,          mMode),
    ACT(0x02e8, e_th_ball_class,         mAction,          mMode),
    ACT(0x02ed, e_mk_bo_class,           action,           mode),
    ACT(0x02ee, e_mm_mt_class,           m_action,         m_mode),
    ACT(0x0304, e_bi_class,              action,           mode),

    ACT(0x1BF, e_st_class,            mAction,          mActionPhase),
    ACT(0x1FB, e_yg_class,            mAction,          mActionMode),
};
#undef ACT

const EnemyActLayout* act_layout_ext(int16_t procName) {
    static EnemyActLayout scratch;
    int n = 0;
    const EnemyActLayoutExt* rows = coop_enemy_act_wb(n);
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < n; ++i) {
            if (rows[i].procName != procName) continue;
            scratch.procName = rows[i].procName;
            scratch.actionOffset = rows[i].actionOffset;
            scratch.modeOffset = rows[i].modeOffset;
            scratch.actionSize = rows[i].actionSize;
            scratch.modeSize = rows[i].modeSize;
            return &scratch;
        }
        rows = (pass == 0) ? coop_enemy_act_yk(n) : coop_enemy_act_po(n);
    }
    return nullptr;
}

const EnemyActLayout* act_layout_for(int16_t procName) {
    for (const EnemyActLayout& l : kEnemyAct) {
        if (l.procName == procName) return &l;
    }
    return act_layout_ext(procName);
}

int32_t read_sized(const void* base, uint16_t off, uint8_t size) {
    const auto* p = static_cast<const uint8_t*>(base) + off;
    if (size == 1) return *reinterpret_cast<const int8_t*>(p);
    if (size == 2) return *reinterpret_cast<const int16_t*>(p);
    return *reinterpret_cast<const int32_t*>(p);
}

void write_sized(void* base, uint16_t off, uint8_t size, int32_t value) {
    auto* p = static_cast<uint8_t*>(base) + off;
    if (size == 1) *reinterpret_cast<int8_t*>(p) = static_cast<int8_t>(value);
    else if (size == 2) *reinterpret_cast<int16_t*>(p) = static_cast<int16_t>(value);
    else *reinterpret_cast<int32_t*>(p) = value;
}

void read_enemy_decision(fopAc_ac_c* actor, int16_t& action, int16_t& mode) {
    action = kEnemyNoAction;
    mode = 0;
    if (actor == nullptr) return;
    const EnemyActLayout* l = act_layout_for(fopAcM_GetName(actor));
    if (l == nullptr) return;
    action = static_cast<int16_t>(read_sized(actor, l->actionOffset, l->actionSize));
    mode = l->modeSize != 0
               ? static_cast<int16_t>(read_sized(actor, l->modeOffset, l->modeSize))
               : 0;
}

void write_enemy_timers(fopAc_ac_c* actor, const int16_t* in, uint8_t count, int age);

bool apply_enemy_decision(fopAc_ac_c* actor, int16_t action, int16_t mode, Tracked* t,
                          const int16_t* timers, uint8_t timerCount, int age) {
    if (actor == nullptr || t == nullptr || action == kEnemyNoAction) return false;
    const EnemyActLayout* l = act_layout_for(fopAcM_GetName(actor));
    if (l == nullptr) return false;
    if (t->decisionKnown && t->lastAction == action && t->lastMode == mode) return false;
    t->decisionKnown = true;
    t->lastAction = action;
    t->lastMode = mode;
    write_sized(actor, l->actionOffset, l->actionSize, action);
    if (l->modeSize != 0) write_sized(actor, l->modeOffset, l->modeSize, mode);

    write_enemy_timers(actor, timers, timerCount, age);
    return true;
}

enum EnemyStateMode : uint8_t { kStateLatch, kStateMirror };

struct EnemyStateLayout {
    int16_t procName;
    uint16_t offset;
    uint8_t size;
    EnemyStateMode mode;
    int8_t latchTo;
};

const EnemyStateLayout kEnemyState[] = {

    { (int16_t)0x1C5, (uint16_t)offsetof(e_db_class, field_0x850),
      (uint8_t)sizeof(((e_db_class*)nullptr)->field_0x850), kStateLatch, (int8_t)0 },

    { (int16_t)0x1BD, (uint16_t)offsetof(daE_SM_c, mCoreAction),
      (uint8_t)sizeof(((daE_SM_c*)nullptr)->mCoreAction), kStateMirror, (int8_t)0 },
};

const EnemyStateLayout* state_layout_for(int16_t procName) {
    for (const EnemyStateLayout& l : kEnemyState) {
        if (l.procName == procName) return &l;
    }
    return nullptr;
}

int8_t read_enemy_state(fopAc_ac_c* actor) {
    if (actor == nullptr) return kEnemyNoState;
    const EnemyStateLayout* l = state_layout_for(fopAcM_GetName(actor));
    if (l == nullptr) return kEnemyNoState;
    const int32_t v = read_sized(actor, l->offset, l->size);

    if (v < -127 || v > 127) return kEnemyNoState;
    return static_cast<int8_t>(v);
}

void write_enemy_state(fopAc_ac_c* actor, int8_t value) {
    if (actor == nullptr || value == kEnemyNoState) return;
    const EnemyStateLayout* l = state_layout_for(fopAcM_GetName(actor));
    if (l == nullptr) return;
    const int32_t ours = read_sized(actor, l->offset, l->size);
    if (l->mode == kStateLatch) {

        if (value != l->latchTo || ours == l->latchTo) return;
        write_sized(actor, l->offset, l->size, l->latchTo);
        return;
    }

    if (ours == value) return;
    write_sized(actor, l->offset, l->size, value);
}

#define TMR(proc, cls, member)                                                       { (int16_t)(proc), (uint16_t)offsetof(cls, member),                                (uint8_t)(sizeof(((cls*)nullptr)->member) / sizeof(((cls*)nullptr)->member[0])) }

#define TMR_ONE(proc, cls, member)                                                   { (int16_t)(proc), (uint16_t)offsetof(cls, member), (uint8_t)1 }

struct EnemyTmrLayout {
    int16_t procName;
    uint16_t offset;
    uint8_t count;
};

const int kEnemyTimerMax = 5;

const EnemyTmrLayout kEnemyTmr[] = {

    TMR_ONE(0x1FD, daE_WS_c,  mInvulnerabilityTimer),
    TMR_ONE(0x206, daE_TT_c,  mDamageCooldownTimer),
    TMR_ONE(0x207, daE_DK_c,  field_0x694),
    TMR_ONE(0x209, daE_WW_c,  field_0x724),

    TMR(0x1B2, e_dd_class,    field_0x6aa),
    TMR(0x1B5, e_mf_class,    field_0x6c0),
    TMR(0x1B9, e_sh_class,    field_0x698),
    TMR(0x1CC, e_yd_class,    field_0x69c),
    TMR(0x1CD, e_yh_class,    field_0x698),
    TMR(0x1D5, e_rdb_class,   field_0x6b8),
    TMR(0x1E9, e_nz_class,    field_0x6a2),
    TMR(0x1F2, daE_GE_c,      field_0xb8e),

    TMR(0x00e4, daE_PH_c,                mTimers),
    TMR(0x00f5, e_yc_class,              mTimer),
    TMR(0x01af, e_ai_class,              m_timers),
    TMR(0x01b0, e_gs_class,              timers),
    TMR(0x01b1, e_gob_class,             mTimers),
    TMR(0x01b3, e_dn_class,              timer),
    TMR(0x01b4, e_s1_class,              mTimers),
    TMR(0x01b6, e_sg_class,              mTimers),
    TMR(0x01b7, e_bs_class,              timers),
    TMR(0x01b8, e_sf_class,              mTimers),
    TMR(0x01be, e_sm2_class,             timers),
    TMR(0x01c2, e_th_class,              mTimers),
    TMR(0x01c3, e_cr_class,              timers),
    TMR(0x01c4, e_cr_egg_class,          timers),
    TMR(0x01c5, e_db_class,              timers),
    TMR(0x01c8, e_gb_class,              timer),
    TMR(0x01c9, e_hb_class,              timers),
    TMR(0x01cb, e_hzelda_class,          mTimers),
    TMR(0x01d0, e_tk_class,              mActionTimer),
    TMR(0x01d1, e_tk2_class,             mActionTimer),
    TMR(0x01d2, e_tk_ball_class,         mActionTimer),
    TMR(0x01d3, e_rb_class,              timers),
    TMR(0x01d4, e_rd_class,              timer),
    TMR(0x01d6, e_rdy_class,             mTimer),
    TMR(0x01d7, e_fm_class,              mTimers),
    TMR(0x01d8, e_fs_class,              mTimer),
    TMR(0x01d9, daE_PM_c,                mTimer),
    TMR(0x01db, e_mb_class,              mTimers),
    TMR(0x01dc, e_mk_class,              timer),
    TMR(0x01dd, e_mm_class,              timers),
    TMR(0x01e7, e_ms_class,              mActionTimer),
    TMR(0x01e8, e_nest_class,            mTimers),
    TMR(0x01ea, e_ba_class,              mTimer),
    TMR(0x01eb, e_bu_class,              timers),
    TMR(0x01ee, e_is_class,              timers),
    TMR(0x02e5, e_arrow_class,           mTimers),
    TMR(0x02e8, e_th_ball_class,         mTimers),
    TMR(0x02ed, e_mk_bo_class,           timers),
    TMR(0x02ee, e_mm_mt_class,           m_timer),
    TMR(0x0304, e_bi_class,              timer),
    TMR(0x1BF, e_st_class,            mTimers),
};
#undef TMR

const EnemyTmrLayout* tmr_layout_ext(int16_t procName) {
    static EnemyTmrLayout scratch;
    int n = 0;
    const EnemyTmrLayoutExt* rows = coop_enemy_tmr_wb(n);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < n; ++i) {
            if (rows[i].procName != procName) continue;
            scratch.procName = rows[i].procName;
            scratch.offset = rows[i].offset;
            scratch.count = rows[i].count;
            return &scratch;
        }
        rows = coop_enemy_tmr_yk(n);
    }
    return nullptr;
}

const EnemyTmrLayout* tmr_layout_for(int16_t procName) {
    for (const EnemyTmrLayout& l : kEnemyTmr) {
        if (l.procName == procName) return &l;
    }
    return tmr_layout_ext(procName);
}

void read_enemy_timers(fopAc_ac_c* actor, int16_t* out, uint8_t& count) {
    count = 0;
    if (actor == nullptr) return;
    const EnemyTmrLayout* l = tmr_layout_for(fopAcM_GetName(actor));
    if (l == nullptr) return;
    const auto* base = reinterpret_cast<const int16_t*>(
        reinterpret_cast<const uint8_t*>(actor) + l->offset);
    count = l->count > kEnemyTimerMax ? kEnemyTimerMax : l->count;
    for (uint8_t i = 0; i < count; ++i) out[i] = base[i];
}

void write_enemy_timers(fopAc_ac_c* actor, const int16_t* in, uint8_t count, int age) {
    if (actor == nullptr || count == 0) return;
    const EnemyTmrLayout* l = tmr_layout_for(fopAcM_GetName(actor));
    if (l == nullptr) return;
    auto* base = reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(actor) + l->offset);
    const uint8_t n = count < l->count ? count : l->count;
    for (uint8_t i = 0; i < n; ++i) {
        int v = in[i];
        if (v > 0) {
            v -= age;
            if (v < 0) v = 0;
        }
        base[i] = static_cast<int16_t>(v);
    }
}

void send_state(EnemyList& list) {

    static_assert(1 + kCoopMaxEnemiesPerMessage * sizeof(MsgEnemyEntry) <= kCoopMaxMessagePayload,
                  "MsgEnemyEntry grew: the per-message enemy cap no longer fits the payload");
    uint8_t buffer[1 + kCoopMaxEnemiesPerMessage * sizeof(MsgEnemyEntry)];
    int count = 0;
    for (int i = 0; i < list.count && count < kCoopMaxEnemiesPerMessage; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        if (!syncable(actor)) continue;
        if (!we_own(list.rooms[i], list.keys[i])) continue;
        MsgEnemyEntry entry{};
        entry.key = list.keys[i];
        entry.room = list.rooms[i];
        entry.procName = fopAcM_GetName(actor);
        entry.pos[0] = actor->current.pos.x;
        entry.pos[1] = actor->current.pos.y;
        entry.pos[2] = actor->current.pos.z;
        entry.angle[0] = actor->shape_angle.x;
        entry.angle[1] = actor->shape_angle.y;
        entry.angle[2] = actor->shape_angle.z;
        entry.health = actor->health;
        entry.flags = 0;
        if ((actor->actor_status & fopAcStts_NODRAW_e) != 0) entry.flags |= kEnemyFlagHidden;
        if (fopAcM_checkCarryNow(actor) != 0) entry.flags |= kEnemyFlagCarried;
        read_enemy_anm(actor, entry);
        read_enemy_decision_into(actor, entry);
        std::memcpy(buffer + 1 + count * sizeof(MsgEnemyEntry), &entry, sizeof(entry));
        ++count;
    }
    s_diagOwned = count;
    if (count == 0) return;
    buffer[0] = static_cast<uint8_t>(count);
    coop_net_send(kMsgEnemyState, buffer, 1 + count * sizeof(MsgEnemyEntry));
}

void send_gone(int8_t room, uint32_t key) {
    MsgEnemyGone msg{};
    msg.key = key;
    msg.room = room;
    coop_net_send(kMsgEnemyGone, &msg, sizeof(msg));
    coop_log::info("coop_mod: [ENEMY] killed room={} key={:#010x} - telling the other player",
        static_cast<int>(room), key);
}

void sweep(EnemyList& list, bool host) {
    for (int i = 0; i < kMaxTracked; ++i) s_tracked[i].seen = false;

    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        const int8_t room = list.rooms[i];
        const uint32_t key = list.keys[i];
        Tracked* t = find_tracked(room, key);
        const bool fresh = t == nullptr;
        if (fresh) {
            t = add_tracked(room, key);
            if (t == nullptr) continue;
            t->runtime = key_is_runtime(actor);
            t->procName = fopAcM_GetName(actor);
            t->id = fopAcM_GetID(actor);
            t->health = actor->health;

            t->seen = true;
            continue;
        }
        t->seen = true;

        const fpc_ProcID id = fopAcM_GetID(actor);
        if (id != t->id) {

            t->id = id;
            t->health = actor->health;
            t->killed = false;
            t->killedTicks = 0;
            continue;
        }

        t->carriedByUs = fopAcM_checkCarryNow(actor) != 0;

        if (!we_own(room, key)) {
            bool heardOfIt = false;
            for (int r = 0; r < kMaxTracked; ++r) {
                if (s_remote[r].used && s_remote[r].room == room && s_remote[r].key == key) {
                    heardOfIt = true;
                    break;
                }
            }
            if (heardOfIt) {
                t->lonelyTicks = 0;
            } else if (++t->lonelyTicks > 180 && !t->lonelyReported) {
                t->lonelyReported = true;
                ++s_diagLonely;
                coop_log::info(
                    "coop_mod: [ENEMY-DYN] room={} key={:#010x} name={} has existed here for 3s and "
                    "the other game has never described it - almost certainly a runtime spawn, "
                    "running independently on each screen",
                    static_cast<int>(room), key, static_cast<int>(fopAcM_GetName(actor)));

                coop_log::info(
                    "coop_mod: [ENEMY-DYN]   key inputs: name={} param={:#010x} setID={} "
                    "home=({},{},{}) homeAngleY={}",
                    static_cast<int>(fopAcM_GetName(actor)),
                    static_cast<uint32_t>(fopAcM_GetParam(actor)),
                    static_cast<int>(actor->setID),
                    round_unit(actor->home.pos.x), round_unit(actor->home.pos.y),
                    round_unit(actor->home.pos.z), static_cast<int>(actor->home.angle.y));
            }
        }
        if (t->hitQuietTicks > 0) --t->hitQuietTicks;
        if (t->captureQuietTicks > 0) --t->captureQuietTicks;
        const int16_t health = actor->health;

        (void)fresh;

        if (t->health > 0 && health <= 0) t->killed = true;
        t->health = health;
        if (t->killed) ++t->killedTicks;
    }

    const bool quiet = s_eventSettleTicks > 0;
    for (int i = 0; i < kMaxTracked; ++i) {
        Tracked& t = s_tracked[i];
        if (!t.used || t.seen) continue;

        const bool catchable = t.procName == fpcNm_E_BI_e;
        if (!t.killed && t.runtime && !t.goneSent && !catchable && room_is_loaded(t.room)) {
            if (quiet) continue;
            send_gone(t.room, t.key);
            coop_log::info("coop_mod: [ENEMY-DYN] a spawned actor was removed here (room {} "
                            "key={:#010x}) - telling the other game", static_cast<int>(t.room),
                t.key);
            t = Tracked{};
            continue;
        }
        if (t.killed && !t.goneSent) {

            if (quiet) continue;
            send_gone(t.room, t.key);
        }
        t = Tracked{};
    }
    (void)host;
}

void apply_remote(EnemyList& list) {
    const bool positions = cfg_bool(s_positionsVar, true);
    s_diagMatched = 0;
    s_diagUnmatched = 0;

    fopAc_ac_c* toDelete[kMaxTracked];
    int deleteCount = 0;

    for (int i = 0; i < kMaxTracked; ++i) {
        Remote& r = s_remote[i];
        if (!r.used) continue;
        if (!r.gone && s_tick - r.stamp > static_cast<uint32_t>(kStaleTicks)) {
            r = Remote{};
            continue;
        }
        fopAc_ac_c* actor = find_local(list, r.room, r.key);
        if (actor == nullptr) {

            if (r.gone) r = Remote{};
            else ++s_diagUnmatched;
            continue;
        }
        ++s_diagMatched;

        if (r.gone) {
            if (actor->health > 0) actor->health = 0;
            Tracked* t = find_tracked(r.room, r.key);
            if (t != nullptr) {
                t->health = 0;
                t->killed = true;
                t->goneSent = true;

                if (t->killedTicks > kForceDeleteTicks && !t->deleteAsked && actor->health <= 0 &&
                    deleteCount < kMaxTracked) {
                    t->deleteAsked = true;
                    toDelete[deleteCount++] = actor;
                }
            }
            continue;
        }

        if (!syncable(actor)) continue;

        if (we_own(r.room, r.key)) {
            r = Remote{};
            continue;
        }

        if (r.health < actor->health) {
            actor->health = r.health;
            Tracked* t = find_tracked(r.room, r.key);
            if (t != nullptr) t->health = r.health;
        }

        const bool wantCarried = (r.flags & kEnemyFlagCarried) != 0 &&
                                 fopAcM_GetName(actor) != fpcNm_E_BI_e;
        const bool isCarried = fopAcM_checkCarryNow(actor) != 0;
        if (wantCarried != isCarried && fopAcM_GetName(actor) != fpcNm_E_BI_e) {
            if (wantCarried) {
                fopAcM_setCarryNow(actor, 1);
            } else {
                fopAcM_cancelCarryNow(actor);
            }
        }

        if (!positions) continue;

        apply_hidden(actor, (r.flags & kEnemyFlagHidden) != 0);

        apply_enemy_anm(actor, r.anmId, r.anmFrame, r.anmRate, r.anmMode, r.anmMismatch);

        if (cfg_bool(s_decisionsVar, true)) {

            write_enemy_state(actor, r.extraState);

            if (Tracked* dt = find_tracked(r.room, r.key)) {

                int age = static_cast<int>(s_tick - r.stamp) +
                          static_cast<int>(coop_net_rtt_ticks(r.owner)) / 2;
                if (age < 0) age = 0;
                if (age > 60) age = 60;
                if (apply_enemy_decision(actor, r.action, r.mode, dt, r.timers, r.timerCount,
                                         age)) {
                    ++s_diagInstructed;
                }
            }
        }

        f32 wantX = r.x;
        f32 wantY = r.y;
        f32 wantZ = r.z;
        if (r.haveVel) {
            const f32 transit = static_cast<f32>(coop_net_rtt_ticks(r.owner)) * 0.5f;
            f32 ahead = static_cast<f32>(s_tick - r.stamp) + transit;
            const f32 kMaxAheadTicks = 6.0f;
            if (ahead > kMaxAheadTicks) ahead = kMaxAheadTicks;
            wantX += r.vx * ahead;
            wantY += r.vy * ahead;
            wantZ += r.vz * ahead;
        }
        const f32 dx = wantX - actor->current.pos.x;
        const f32 dy = wantY - actor->current.pos.y;
        const f32 dz = wantZ - actor->current.pos.z;
        const f32 dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist > kSnapDistance || wantCarried || s_unpauseSnapTicks > 0) {
            actor->current.pos.x = wantX;
            actor->current.pos.y = wantY;
            actor->current.pos.z = wantZ;
        } else {
            actor->current.pos.x += dx * kPosLerp;
            actor->current.pos.y += dy * kPosLerp;
            actor->current.pos.z += dz * kPosLerp;
        }

        const int16_t* want = r.angle;
        if (dist > kSnapDistance || s_unpauseSnapTicks > 0) {
            actor->shape_angle.x = want[0];
            actor->shape_angle.y = want[1];
            actor->shape_angle.z = want[2];
            actor->current.angle.y = want[1];
        } else {
            cLib_addCalcAngleS(&actor->shape_angle.x, want[0], 2, 0x4000, 0x100);
            cLib_addCalcAngleS(&actor->shape_angle.y, want[1], 2, 0x4000, 0x100);
            cLib_addCalcAngleS(&actor->shape_angle.z, want[2], 2, 0x4000, 0x100);
            cLib_addCalcAngleS(&actor->current.angle.y, want[1], 2, 0x4000, 0x100);
        }
    }

    for (int i = 0; i < deleteCount; ++i) {
        coop_log::info("coop_mod: [ENEMY] force-removing a body the other player already killed");
        fopAcM_delete(toDelete[i]);
    }
}

void owner_finish_kills(EnemyList& list) {
    fopAc_ac_c* toDelete[kMaxTracked];
    int deleteCount = 0;
    for (int i = 0; i < kMaxTracked; ++i) {
        Tracked& t = s_tracked[i];
        if (!t.used || !t.killed || t.deleteAsked || t.killedTicks <= kForceDeleteTicks) continue;
        if (!we_own(t.room, t.key)) continue;
        fopAc_ac_c* actor = find_local(list, t.room, t.key);
        if (actor != nullptr && actor->health > 0) continue;
        if (actor != nullptr && deleteCount < kMaxTracked) {
            t.deleteAsked = true;
            toDelete[deleteCount++] = actor;
        }
    }
    for (int i = 0; i < deleteCount; ++i) fopAcM_delete(toDelete[i]);
}

f32 retarget_blend(fpc_ProcID id, uint8_t* target = nullptr) {
    if (s_retarget.count == 0 || id < s_retarget.lo || id > s_retarget.hi) return 0.0f;
    for (int i = 0; i < s_retarget.count; ++i) {
        if (s_retarget.ids[i] == id) {
            if (target != nullptr) *target = s_retarget.target[i];
            return s_retarget.blend[i];
        }
    }
    return 0.0f;
}

bool player_body(uint8_t playerId, cXyz& out) {
    if (playerId == coop_net_local_id()) {
        fopAc_ac_c* me = dComIfGp_getPlayer(0);
        if (me == nullptr) return false;
        out = me->current.pos;
        return true;
    }
    f32 x = 0.0f, y = 0.0f, z = 0.0f;
    if (!puppet_hook_get_pose_of(playerId, &x, &y, &z, nullptr, nullptr, nullptr)) return false;
    out.set(x, y, z);
    return true;
}

bool player_reachable(uint8_t playerId) {
    cXyz p;

    return playerId < kCoopMaxPlayers && coop_net_player_present(playerId) &&
           !coop_player_paused(playerId) && player_body(playerId, p);
}

f32 player_distance(uint8_t playerId, const cXyz& at) {
    cXyz p;
    if (!player_body(playerId, p)) return 1.0e9f;
    const f32 dx = p.x - at.x, dy = p.y - at.y, dz = p.z - at.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

int reachable_player_count() {
    int n = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (player_reachable(static_cast<uint8_t>(i))) ++n;
    }
    return n;
}

void decide_targets(EnemyList& list) {
    if (!coop_net_is_host() || !cfg_bool(s_targetVar, true)) return;

    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) return;
    if (reachable_player_count() < 2) {

        for (int i = 0; i < kMaxTracked; ++i) {
            if (s_tracked[i].used) s_tracked[i].targetKnown = false;
        }
        return;
    }

    uint8_t buffer[1 + kCoopMaxEnemiesPerMessage * sizeof(MsgEnemyTarget)];
    int count = 0;

    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        if (!syncable(actor)) continue;
        Tracked* t = find_tracked(list.rooms[i], list.keys[i]);
        if (t == nullptr) continue;
        const cXyz& at = actor->current.pos;

        const f32 dCur = player_distance(t->targetPlayer, at);
        uint8_t bestId = t->targetPlayer;
        f32 bestD = dCur;
        bool tookByContact = false;
        for (int pi = 0; pi < kCoopMaxPlayers; ++pi) {
            const uint8_t cand = static_cast<uint8_t>(pi);
            if (cand == t->targetPlayer || !player_reachable(cand)) continue;
            const f32 d = player_distance(cand, at);
            if (d >= kContactRange) continue;
            if (dCur < kContactRange) continue;
            if (!tookByContact || d < bestD) {
                bestId = cand;
                bestD = d;
                tookByContact = true;
            }
        }
        if (!tookByContact) {
            for (int pi = 0; pi < kCoopMaxPlayers; ++pi) {
                const uint8_t cand = static_cast<uint8_t>(pi);
                if (cand == t->targetPlayer || !player_reachable(cand)) continue;
                const f32 d = player_distance(cand, at);
                if (d + kTargetMargin < bestD) {
                    bestId = cand;
                    bestD = d;
                }
            }
        }

        if (!player_reachable(bestId)) {
            for (int pi = 0; pi < kCoopMaxPlayers; ++pi) {
                if (player_reachable(static_cast<uint8_t>(pi))) {
                    bestId = static_cast<uint8_t>(pi);
                    break;
                }
            }
        }
        t->targetPlayer = bestId;
        t->targetKnown = true;
        t->targetStamp = s_tick;

        if (count < kCoopMaxEnemiesPerMessage) {
            MsgEnemyTarget entry{};
            entry.key = list.keys[i];
            entry.room = list.rooms[i];
            entry.targetPlayer = t->targetPlayer;
            std::memcpy(buffer + 1 + count * sizeof(entry), &entry, sizeof(entry));
            ++count;
        }
    }
    if (count == 0) return;
    buffer[0] = static_cast<uint8_t>(count);
    coop_net_send(kMsgEnemyTargets, buffer, 1 + count * sizeof(MsgEnemyTarget));
}

void advance_blends(EnemyList& list) {
    s_retarget.count = 0;
    s_retargetedThisTick = 0;
    if (!cfg_bool(s_targetVar, true)) return;

    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) return;

    bool havePuppet[kCoopMaxPlayers] = {};
    bool anyPose = false;
    for (int p = 0; p < kCoopMaxPlayers; ++p) {
        if (static_cast<uint8_t>(p) == coop_net_local_id()) continue;
        f32 px = 0.0f, py = 0.0f, pz = 0.0f, psx = 0.0f, psz = 0.0f;
        s16 pangle = 0;
        if (puppet_hook_get_pose_of(static_cast<uint8_t>(p), &px, &py, &pz, &pangle, &psx, &psz)) {
            havePuppet[p] = true;
            RemotePose& pose = s_remotePoses[p];
            pose.valid = true;
            pose.x = px;
            pose.y = py;
            pose.z = pz;
            pose.angleY = pangle;
            pose.sx = psx;
            pose.sz = psz;
        }
        if (s_remotePoses[p].valid) anyPose = true;
    }

    if (!anyPose) return;
    bool stillUsed[kCoopMaxPlayers] = {};
    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        if (!syncable(actor)) continue;
        Tracked* t = find_tracked(list.rooms[i], list.keys[i]);
        if (t == nullptr) continue;

        const uint8_t target = t->targetPlayer;
        const bool wantsOther = t->targetKnown && target != coop_net_local_id() &&
                                target < kCoopMaxPlayers && havePuppet[target] &&
                                player_is_live(target);
        const uint8_t desired = wantsOther ? target : kCoopNoPlayer;
        f32 want = 0.0f;
        if (t->targetBlend > 0.0f && t->blendPlayer != desired) {

            want = 0.0f;
        } else if (desired != kCoopNoPlayer) {
            t->blendPlayer = desired;
            want = 1.0f;
        }
        const f32 step = 1.0f / static_cast<f32>(kTargetBlendTicks);
        if (t->targetBlend < want) {
            t->targetBlend = t->targetBlend + step > want ? want : t->targetBlend + step;
        } else if (t->targetBlend > want) {
            t->targetBlend = t->targetBlend - step < want ? want : t->targetBlend - step;
        }
        if (t->targetBlend <= 0.0f) continue;
        const uint8_t toward = t->blendPlayer;
        if (toward >= kCoopMaxPlayers || !s_remotePoses[toward].valid) {
            t->targetBlend = 0.0f;
            continue;
        }
        stillUsed[toward] = true;

        if (s_retarget.count < kMaxTracked) {
            s_retarget.blend[s_retarget.count] = t->targetBlend;
            s_retarget.target[s_retarget.count] = toward;
            s_retarget.ids[s_retarget.count++] = fopAcM_GetID(actor);
        }
    }

    s_retargetedThisTick = s_retarget.count;
    if (s_retarget.count > 0) {
        s_retarget.lo = s_retarget.ids[0];
        s_retarget.hi = s_retarget.ids[0];
        for (int i = 1; i < s_retarget.count; ++i) {
            if (s_retarget.ids[i] < s_retarget.lo) s_retarget.lo = s_retarget.ids[i];
            if (s_retarget.ids[i] > s_retarget.hi) s_retarget.hi = s_retarget.ids[i];
        }
    }

    for (int p = 0; p < kCoopMaxPlayers; ++p) {
        if (!havePuppet[p] && !stillUsed[p]) s_remotePoses[p].valid = false;
    }
}

void lie_begin(fopAc_ac_c* player, f32 blend, uint8_t target) {
    if (target >= kCoopMaxPlayers || blend <= 0.0f) return;
    const RemotePose& pose = s_remotePoses[target];
    if (!pose.valid) return;
    if (blend > 1.0f) blend = 1.0f;

    s_lie.player = player;
    s_lie.savedCur = player->current.pos;
    s_lie.savedOld = player->old.pos;
    s_lie.savedEye = player->eyePos;
    s_lie.savedSpeed = player->speed;
    s_lie.savedShape = player->shape_angle;
    s_lie.savedCurAngle = player->current.angle;
    s_lie.savedSpeedF = player->speedF;

    const f32 px = s_lie.savedCur.x + (pose.x - s_lie.savedCur.x) * blend;
    const f32 py = s_lie.savedCur.y + (pose.y - s_lie.savedCur.y) * blend;
    const f32 pz = s_lie.savedCur.z + (pose.z - s_lie.savedCur.z) * blend;
    const f32 sx = s_lie.savedSpeed.x + (pose.sx - s_lie.savedSpeed.x) * blend;
    const f32 sz = s_lie.savedSpeed.z + (pose.sz - s_lie.savedSpeed.z) * blend;

    player->current.pos.set(px, py, pz);
    player->old.pos.set(px - sx, py, pz - sz);
    player->eyePos.set(px + (s_lie.savedEye.x - s_lie.savedCur.x),
        py + (s_lie.savedEye.y - s_lie.savedCur.y), pz + (s_lie.savedEye.z - s_lie.savedCur.z));
    player->speed.set(sx, 0.0f, sz);
    player->speedF = std::sqrt(sx * sx + sz * sz);

    if (blend >= 0.5f) {
        player->shape_angle.y = pose.angleY;
        player->current.angle.y = pose.angleY;
    }

    s_lie.wroteCur = player->current.pos;
    s_lie.wroteOld = player->old.pos;
    s_lie.wroteEye = player->eyePos;
    s_lie.wroteSpeed = player->speed;
    s_lie.wroteShape = player->shape_angle;
    s_lie.wroteCurAngle = player->current.angle;
    s_lie.wroteSpeedF = player->speedF;
    s_lie.active = true;
}

void lie_end() {
    if (!s_lie.active || s_lie.player == nullptr) {
        s_lie.active = false;
        s_lie.player = nullptr;
        return;
    }
    fopAc_ac_c* p = s_lie.player;
    p->current.pos = s_lie.savedCur + (p->current.pos - s_lie.wroteCur);
    p->old.pos = s_lie.savedOld + (p->old.pos - s_lie.wroteOld);
    p->eyePos = s_lie.savedEye + (p->eyePos - s_lie.wroteEye);
    p->speed = s_lie.savedSpeed + (p->speed - s_lie.wroteSpeed);
    p->speedF = s_lie.savedSpeedF + (p->speedF - s_lie.wroteSpeedF);
    p->shape_angle.y =
        static_cast<s16>(s_lie.savedShape.y + (p->shape_angle.y - s_lie.wroteShape.y));
    p->current.angle.y =
        static_cast<s16>(s_lie.savedCurAngle.y + (p->current.angle.y - s_lie.wroteCurAngle.y));
    s_lie.active = false;
    s_lie.player = nullptr;
}

HookAction on_proc_execute_pre(ModContext*, void* args, void*, void*) {

    {
        auto* proc = mods::arg<base_process_class*>(args, 0);
        daAlink_c* alink = daAlink_getAlinkActorClass();

        if (proc != nullptr && alink != nullptr && proc->id == fopAcM_GetID(alink)) {
            ++s_worldFrames;
        }
    }

    if (s_lieDepth++ != 0) return HOOK_CONTINUE;
    if (s_retarget.count == 0) return HOOK_CONTINUE;
    auto* proc = mods::arg<base_process_class*>(args, 0);
    if (proc == nullptr) return HOOK_CONTINUE;
    uint8_t target = kCoopNoPlayer;
    const f32 blend = retarget_blend(proc->id, &target);
    if (blend <= 0.0f) return HOOK_CONTINUE;
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) return HOOK_CONTINUE;
    lie_begin(player, blend, target);
    return HOOK_CONTINUE;
}

void on_proc_execute_post(ModContext*, void*, void*, void*) {
    if (--s_lieDepth != 0) return;
    lie_end();
}

template <class Fn>
void for_each_tg_collider(fopAc_ac_c* actor, int count, Fn visit) {
    dCcS* cc = dComIfG_Ccsp();
    if (cc == nullptr || actor == nullptr) return;
    if (count < 0) count = 0;
    if (count > static_cast<int>(ARRAY_SIZEU(cc->mpObjTg))) {
        count = static_cast<int>(ARRAY_SIZEU(cc->mpObjTg));
    }
    for (int i = 0; i < count; ++i) {
        cCcD_Obj* obj = cc->mpObjTg[i];
        if (obj == nullptr) continue;
        dCcD_GObjInf* inf = dCcD_GetGObjInf(obj);
        if (inf == nullptr || inf->GetAc() != actor) continue;
        if (visit(obj, inf)) return;
    }
}

bool blow_is_ours(fopAc_ac_c* attacker) {
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (attacker == nullptr || player == nullptr) return false;

    if (attacker == player) return true;

    const int16_t name = fopAcM_GetName(attacker);
    if (name == kProcNbomb && !spawns_replicates_procname(name)) return true;
    return false;
}

void apply_damage_amount(fopAc_ac_c* actor, int8_t room, uint32_t key, int amount) {
    if (actor == nullptr || amount <= 0) return;
    int health = actor->health - amount;
    if (health < 0) health = 0;
    actor->health = static_cast<s16>(health);
    Tracked* t = find_tracked(room, key);
    if (t != nullptr) {
        t->health = actor->health;
        if (health == 0) t->killed = true;
    }
}

const f32 kDisarmBlend = 0.5f;
int s_diagDisarmed = 0;

const int kMaxDisarmed = 64;
cCcD_Obj* s_disarmed[kMaxDisarmed];
int s_disarmedCount = 0;

void rearm_disarmed_attacks();

void disarm_retargeted_attacks() {

    rearm_disarmed_attacks();
    s_diagDisarmed = 0;
    dCcS* cc = dComIfG_Ccsp();
    if (cc == nullptr) return;
    const int atCount = static_cast<int>(cc->mObjAtCount);
    const int n = atCount > static_cast<int>(ARRAY_SIZEU(cc->mpObjAt))
                      ? static_cast<int>(ARRAY_SIZEU(cc->mpObjAt))
                      : atCount;
    for (int i = 0; i < n && s_disarmedCount < kMaxDisarmed; ++i) {
        cCcD_Obj* obj = cc->mpObjAt[i];
        if (obj == nullptr || !obj->ChkAtSet()) continue;
        fopAc_ac_c* owner = obj->GetAc();
        if (owner == nullptr) continue;
        if (retarget_blend(fopAcM_GetID(owner)) < kDisarmBlend) continue;
        obj->OffAtSetBit();
        s_disarmed[s_disarmedCount++] = obj;
    }
    s_diagDisarmed = s_disarmedCount;
}

void rearm_disarmed_attacks() {
    for (int i = 0; i < s_disarmedCount; ++i) {
        if (s_disarmed[i] != nullptr) s_disarmed[i]->OnAtSetBit();
    }
    s_disarmedCount = 0;
}

void capture_landed_hits(EnemyList& list) {
    dCcS* cc = dComIfG_Ccsp();
    if (cc == nullptr) return;
    const int tgCount = static_cast<int>(cc->field_0x280e);
    if (tgCount <= 0) return;

    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        if (!syncable(actor)) continue;
        Tracked* t = find_tracked(list.rooms[i], list.keys[i]);
        if (t == nullptr) continue;
        const int8_t room = list.rooms[i];
        const uint32_t key = list.keys[i];
        if (t->captureQuietTicks > 0) continue;
        for_each_tg_collider(actor, tgCount, [&](cCcD_Obj* obj, dCcD_GObjInf* inf) {
            (void)obj;
            if (!inf->ChkTgHit()) return false;
            cCcD_Obj* atObj = inf->GetTgHitObj();
            if (atObj == nullptr) return false;
            dCcD_GObjInf* atInf = dCcD_GetGObjInf(atObj);
            if (atInf == nullptr) return false;

            fopAc_ac_c* attacker = inf->GetTgHitAc();
            if (!blow_is_ours(attacker)) return false;

            MsgEnemyHit msg{};
            msg.key = key;
            msg.room = room;
            msg.atType = atInf->GetAtType();
            msg.atp = static_cast<uint8_t>(atInf->GetAtAtp());
            msg.spl = static_cast<uint8_t>(atInf->GetAtSpl());
            msg.mtrl = atInf->GetAtMtrl();
            const cXyz from = attacker->current.pos;
            msg.from[0] = from.x;
            msg.from[1] = from.y;
            msg.from[2] = from.z;
            const cXyz* at = inf->GetTgHitPosP();
            const cXyz where = at != nullptr ? *at : actor->current.pos;
            msg.at[0] = where.x;
            msg.at[1] = where.y;
            msg.at[2] = where.z;
            coop_net_send(kMsgEnemyHit, &msg, sizeof(msg));
            t->hitQuietTicks = kHitRelayQuietTicks;
            t->captureQuietTicks = kCaptureQuietTicks;
            ++s_hitsSent;
            coop_log::info(
                "coop_mod: [ENEMY] relaying our blow on room={} key={:#010x} type={:#x} atp={} spl={}",
                static_cast<int>(room), key, msg.atType, static_cast<int>(msg.atp),
                static_cast<int>(msg.spl));
            return true;
        });
    }
}

void inject_pending_hits(EnemyList& list) {
    bool any = false;
    for (int i = 0; i < kMaxPendingHits; ++i) {
        if (s_pendingHits[i].used) { any = true; break; }
    }
    if (!any) return;

    dCcS* cc = dComIfG_Ccsp();
    if (cc == nullptr) return;
    const int tgCount = static_cast<int>(cc->field_0x280e);

    for (int i = 0; i < kMaxPendingHits; ++i) {
        if (!s_pendingHits[i].used) continue;
        const MsgEnemyHit msg = s_pendingHits[i].msg;
        s_pendingHits[i] = PendingHit{};

        fopAc_ac_c* actor = find_local(list, msg.room, msg.key);
        if (actor == nullptr) continue;

        static dCcD_Stts s_blowStts[kMaxPendingHits];
        static dCcD_Sph s_blow[kMaxPendingHits];
        dCcD_Stts& blowStts = s_blowStts[i];
        dCcD_Sph& blow = s_blow[i];
        fopAc_ac_c* stand_in = dComIfGp_getPlayer(0);
        if (stand_in == nullptr) continue;
        blowStts.Init(0xFF, 0xFF, stand_in);
        blow.SetStts(&blowStts);
        blow.SetAtType(msg.atType);
        blow.SetAtAtp(msg.atp);
        blow.SetAtSpl(static_cast<dCcG_At_Spl>(msg.spl));
        blow.SetAtMtrl(msg.mtrl);

        blow.SetC(cXyz(msg.at[0], msg.at[1], msg.at[2]));
        blow.SetR(10.0f);

        int landed = 0;
        for_each_tg_collider(actor, tgCount, [&](cCcD_Obj* obj, dCcD_GObjInf* inf) {
            inf->SetTgHit(&blow);

            inf->OnTgHitNoActor();
            cXyz where(msg.at[0], msg.at[1], msg.at[2]);
            inf->SetTgHitPos(where);
            cXyz away(actor->current.pos.x - msg.from[0], 0.0f, actor->current.pos.z - msg.from[2]);
            const f32 len = std::sqrt(away.x * away.x + away.z * away.z);
            if (len > 0.01f) {
                away.x *= 10.0f / len;
                away.z *= 10.0f / len;
            } else {
                away.set(cM_ssin(actor->shape_angle.y) * -10.0f, 0.0f,
                    cM_scos(actor->shape_angle.y) * -10.0f);
            }
            inf->SetTgRVec(away);
            cCcD_Stts* stts = obj->GetStts();
            if (stts != nullptr) stts->PlusDmg(msg.atp);
            ++landed;
            return false;
        });

        if (landed > 0) {
            ++s_hitsApplied;
            Tracked* t = find_tracked(msg.room, msg.key);

            if (t != nullptr) t->hitQuietTicks = kHitRelayQuietTicks;
            coop_log::info(
                "coop_mod: [ENEMY] replaying their blow on room={} key={:#010x} atp={} hurtboxes={} hp={}",
                static_cast<int>(msg.room), msg.key, static_cast<int>(msg.atp), landed,
                static_cast<int>(actor->health));
        } else {

            apply_damage_amount(actor, msg.room, msg.key, power_class_to_damage(msg.atp));
        }
    }
}

bool real_hits_enabled() {
    return cfg_bool(s_realHitsVar, true);
}

bool breakables_enabled() {
    return coop_session(kSessWorldObjects, cfg_bool(s_breakablesVar, true));
}

bool movers_enabled() {

    return cfg_bool(s_moversVar, true) &&
           coop_session(kSessWorldObjects, cfg_bool(s_breakablesVar, true));
}

const int kMaxBreakables = 256;
bool s_breakableOverflowLogged = false;

int s_diagBreakables = 0;
uint32_t s_objHitsSent = 0;
uint32_t s_objHitsApplied = 0;
uint32_t s_objHitsLost = 0;

struct BreakableList {
    fopAc_ac_c* actors[kMaxBreakables];
    uint32_t keys[kMaxBreakables];
    int8_t rooms[kMaxBreakables];
    int count = 0;
};

struct BreakableQuiet {
    uint32_t key = 0;
    int8_t room = -1;
    int ticks = 0;
};

const int kMaxBreakableQuiet = 48;
const int kBreakableQuietTicks = 8;
BreakableQuiet s_breakQuiet[kMaxBreakableQuiet];

void age_breakable_quiet() {
    for (int i = 0; i < kMaxBreakableQuiet; ++i) {
        if (s_breakQuiet[i].ticks > 0 && --s_breakQuiet[i].ticks == 0) s_breakQuiet[i].key = 0;
    }
}

bool breakable_is_quiet(int8_t room, uint32_t key) {
    for (int i = 0; i < kMaxBreakableQuiet; ++i) {
        if (s_breakQuiet[i].ticks > 0 && s_breakQuiet[i].key == key && s_breakQuiet[i].room == room) {
            return true;
        }
    }
    return false;
}

void breakable_go_quiet(int8_t room, uint32_t key) {
    int slot = -1;
    for (int i = 0; i < kMaxBreakableQuiet; ++i) {
        if (s_breakQuiet[i].ticks == 0) { slot = i; break; }

        if (slot < 0 || s_breakQuiet[i].ticks < s_breakQuiet[slot].ticks) slot = i;
    }
    s_breakQuiet[slot].key = key;
    s_breakQuiet[slot].room = room;
    s_breakQuiet[slot].ticks = kBreakableQuietTicks;
}

bool has_blob(fopAc_ac_c* actor);

bool is_timed_hazard(fopAc_ac_c* actor) {
    switch (fopAcM_GetName(actor)) {
    case fpcNm_Obj_FirePillar_e:
    case fpcNm_Obj_FirePillar2_e:
    case fpcNm_Obj_Geyser_e:
    case fpcNm_Obj_WaterPillar_e:
        return true;
    default:
        return false;
    }
}

void* collect_breakable(void* proc, void* data) {
    auto* list = static_cast<BreakableList*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr) return nullptr;
    if (list->count >= kMaxBreakables) {
        if (!s_breakableOverflowLogged) {
            s_breakableOverflowLogged = true;
            coop_log::info("coop_mod: [OBJ] more than {} placed objects loaded - the rest will "
                            "not relay blows", kMaxBreakables);
        }
        return nullptr;
    }

    if (fopAcM_GetGroup(actor) != fopAc_ACTOR_e) return nullptr;

    if (fopAcM_checkCarryNow(actor) != 0) return nullptr;

    if (actor->setID == 0xFFFF && !keyed_by_param(actor) && !has_blob(actor)) return nullptr;
    if (boss_room(fopAcM_GetRoomNo(actor))) return nullptr;
    const uint32_t key = compute_placement_key(actor);
    if (key == 0) return nullptr;
    const int idx = list->count++;
    list->actors[idx] = actor;
    list->keys[idx] = key;
    list->rooms[idx] = fopAcM_GetRoomNo(actor);
    return nullptr;
}

void collect_breakables(BreakableList& list) {
    list.count = 0;
    fopAcM_Search(collect_breakable, &list);
    s_diagBreakables = list.count;
}

struct CollectBoth {
    EnemyList* enemies;
    BreakableList* breakables;
};

void* collect_either(void* proc, void* data) {
    auto* both = static_cast<CollectBoth*>(data);
    collect_enemy(proc, both->enemies);
    collect_breakable(proc, both->breakables);
    return nullptr;
}

void collect_enemies_and_breakables(EnemyList& enemies, BreakableList& breakables) {
    enemies.count = 0;
    enemies.enemyActors = 0;
    breakables.count = 0;
    fopAcM_Search(mark_boss_room, nullptr);
    CollectBoth both{&enemies, &breakables};
    fopAcM_Search(collect_either, &both);
    s_diagBreakables = breakables.count;
}

fopAc_ac_c* find_local_breakable(BreakableList& list, int8_t room, uint32_t key) {
    for (int i = 0; i < list.count; ++i) {
        if (list.rooms[i] == room && list.keys[i] == key) return list.actors[i];
    }
    return nullptr;
}

const int kMaxObjPos = kMaxBreakables;
struct ObjPos {
    uint32_t key = 0;
    int8_t room = 0;
    bool used = false;
    cXyz pos;
    csXyz angle;
    uint32_t stateHash = 0;
};
ObjPos s_objPos[kMaxObjPos];

ObjPos* remember_position(int8_t room, uint32_t key, const cXyz& now, const csXyz& angle) {
    int free = -1;
    for (int i = 0; i < kMaxObjPos; ++i) {
        if (s_objPos[i].used && s_objPos[i].key == key && s_objPos[i].room == room) {
            return &s_objPos[i];
        }
        if (!s_objPos[i].used && free < 0) free = i;
    }
    if (free < 0) return nullptr;
    s_objPos[free].used = true;
    s_objPos[free].key = key;
    s_objPos[free].room = room;
    s_objPos[free].pos = now;
    s_objPos[free].angle = angle;
    return &s_objPos[free];
}

const int kMoverAngleEpsilon = 64;

int angle_step(const csXyz& a, const csXyz& b) {
    const int dx = std::abs(static_cast<int>(static_cast<s16>(a.x - b.x)));
    const int dy = std::abs(static_cast<int>(static_cast<s16>(a.y - b.y)));
    const int dz = std::abs(static_cast<int>(static_cast<s16>(a.z - b.z)));
    return std::max(dx, std::max(dy, dz));
}

int object_phase(fopAc_ac_c* actor, int16_t* out) {
    const s16 name = fopAcM_GetName(actor);
    if (name == fpcNm_Obj_Lv6FuriTrap_e) {
        out[0] = static_cast<daLv6FurikoTrap_c*>(actor)->mAngle;
        return 1;
    }
    if (name == fpcNm_Obj_Lv6SwTurn_e) {
        auto* sw = static_cast<daObjLv6SwTurn_c*>(actor);
        out[0] = sw->mMode;
        out[1] = sw->unk5B0;
        out[2] = static_cast<int16_t>(sw->unk5B8);
        out[3] = sw->unk5BC;
        out[4] = sw->unk5B6;
        out[5] = sw->unk5B2;
        return 6;
    }
    if (name == fpcNm_Obj_SwTurn_e) {
        auto* sw = static_cast<daObjSwTurn_c*>(actor);
        out[0] = sw->mMode;
        out[1] = sw->field_0x5c4;
        out[2] = sw->field_0x5b8;
        out[3] = static_cast<int16_t>(sw->field_0x5c0);
        out[4] = static_cast<int16_t>(sw->field_0x5cc);
        out[5] = sw->field_0x5ba;
        out[6] = static_cast<int16_t>(sw->mRevCount);
        return 7;
    }
    return 0;
}

struct BlobRun {
    size_t begin;
    size_t end;
};

const int kMaxBlobRuns = 8;

int blob_runs(fopAc_ac_c* actor, BlobRun* out) {
    switch (fopAcM_GetName(actor)) {
    case fpcNm_Obj_Lv6TogeTrap_e:
        out[0] = {offsetof(daLv6TogeTrap_c, mPathNo), offsetof(daLv6TogeTrap_c, mLine)};
        out[1] = {offsetof(daLv6TogeTrap_c, mIsPathClosed), offsetof(daLv6TogeTrap_c, mCcStts)};
        return 2;
    case fpcNm_Obj_Lv6TogeRoll_e:
        out[0] = {offsetof(daTogeRoll_c, mPathID), offsetof(daTogeRoll_c, mStts)};
        return 1;
    case fpcNm_Obj_RotTrap_e:
        out[0] = {offsetof(daRotTrap_c, mMode), offsetof(daRotTrap_c, mCcStts)};
        return 1;
    case fpcNm_Obj_TogeTrap_e:
        out[0] = {offsetof(daTogeTrap_c, mMode),
            offsetof(daTogeTrap_c, mIsPlayerInArea) + sizeof(BOOL)};
        return 1;

    case fpcNm_Obj_FirePillar_e:
        out[0] = {offsetof(daObjFPillar_c, mAction),
            offsetof(daObjFPillar_c, mActionTimer) + sizeof(u16)};
        return 1;
    case fpcNm_Obj_FirePillar2_e:
        out[0] = {offsetof(daObjFPillar2_c, mActionTimer),
            offsetof(daObjFPillar2_c, mInitAngles)};
        out[1] = {offsetof(daObjFPillar2_c, mAction), offsetof(daObjFPillar2_c, mAction) + 1};
        out[2] = {offsetof(daObjFPillar2_c, mFirePipeTimer),
            offsetof(daObjFPillar2_c, mFirePipeTimer) + 1};
        return 3;
    case fpcNm_Obj_Geyser_e:
        out[0] = {offsetof(daObjGeyser_c, field_0x760),
            offsetof(daObjGeyser_c, field_0x768) + sizeof(u16)};
        return 1;
    case fpcNm_Obj_WaterPillar_e:
        out[0] = {offsetof(daWtPillar_c, mCurrentHeight),
            offsetof(daWtPillar_c, mCurrentHeight) + sizeof(f32)};
        out[1] = {offsetof(daWtPillar_c, mAction), offsetof(daWtPillar_c, mTargetSpeed) + sizeof(f32)};
        return 2;
#include "dungeon_blob_cases.inc"
    default:
        return 0;
    }
}

int clipped_runs(fopAc_ac_c* actor, BlobRun* runs, size_t cap) {
    const int count = blob_runs(actor, runs);
    size_t used = 0;
    for (int i = 0; i < count; ++i) {
        if (runs[i].end <= runs[i].begin) return i;
        const size_t len = runs[i].end - runs[i].begin;
        if (used + len > cap) {
            runs[i].end = runs[i].begin + (cap - used);
            return runs[i].end > runs[i].begin ? i + 1 : i;
        }
        used += len;
    }
    return count;
}

const size_t kBlobCap = sizeof(MsgObjectMove::blob);

bool has_blob(fopAc_ac_c* actor) {
    BlobRun runs[kMaxBlobRuns];
    return blob_runs(actor, runs) > 0;
}

int object_blob(fopAc_ac_c* actor, uint8_t* out, size_t cap) {
    BlobRun runs[kMaxBlobRuns];
    const int count = clipped_runs(actor, runs, cap);
    size_t at = 0;
    for (int i = 0; i < count; ++i) {
        const size_t len = runs[i].end - runs[i].begin;
        std::memcpy(out + at, reinterpret_cast<const uint8_t*>(actor) + runs[i].begin, len);
        at += len;
    }
    return static_cast<int>(at);
}

uint32_t blob_hash(fopAc_ac_c* actor) {
    uint8_t buf[kBlobCap];
    const int n = object_blob(actor, buf, sizeof(buf));
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) h = (h ^ buf[i]) * 16777619u;
    return h;
}

bool apply_object_blob(fopAc_ac_c* actor, const uint8_t* in, size_t len) {
    BlobRun runs[kMaxBlobRuns];
    const int count = clipped_runs(actor, runs, kBlobCap);
    size_t total = 0;
    for (int i = 0; i < count; ++i) total += runs[i].end - runs[i].begin;

    if (count == 0 || total != len) return false;
    size_t at = 0;
    for (int i = 0; i < count; ++i) {
        const size_t n = runs[i].end - runs[i].begin;
        std::memcpy(reinterpret_cast<uint8_t*>(actor) + runs[i].begin, in + at, n);
        at += n;
    }
    return true;
}

void apply_object_phase(fopAc_ac_c* actor, const int16_t* in, int count) {
    const s16 name = fopAcM_GetName(actor);
    if (name == fpcNm_Obj_Lv6FuriTrap_e && count >= 1) {
        static_cast<daLv6FurikoTrap_c*>(actor)->mAngle = in[0];
        return;
    }
    if (name == fpcNm_Obj_Lv6SwTurn_e && count >= 6) {
        auto* sw = static_cast<daObjLv6SwTurn_c*>(actor);
        sw->mMode = static_cast<u8>(in[0]);
        sw->unk5B0 = in[1];
        sw->unk5B8 = in[2];
        sw->unk5BC = static_cast<s8>(in[3]);
        sw->unk5B6 = in[4];
        sw->unk5B2 = in[5];
        return;
    }
    if (name == fpcNm_Obj_SwTurn_e && count >= 7) {
        auto* sw = static_cast<daObjSwTurn_c*>(actor);

        if (in[0] == daObjSwTurn_c::MODE_ROTATE && sw->mMode != daObjSwTurn_c::MODE_ROTATE) {
            sw->field_0x5c0 = in[3];
            sw->init_modeRotate();
        }
        sw->mMode = static_cast<u8>(in[0]);
        sw->field_0x5c4 = in[1];
        sw->field_0x5b8 = in[2];
        sw->field_0x5c0 = in[3];
        sw->field_0x5cc = static_cast<u16>(in[4]);
        sw->field_0x5ba = in[5];
        sw->mRevCount = static_cast<u16>(in[6]);
    }
}

void forget_tracked_positions() {
    for (int i = 0; i < kMaxObjPos; ++i) s_objPos[i] = ObjPos{};
}

const int kMaxMovers = 32;

const f32 kMoverEpsilon = 0.6f;

const int kMoverTailTicks = 20;

const f32 kMoverBlend = 0.5f;

const f32 kMoverSnapDist = 200.0f;

const f32 kMoverRelayRange = 1200.0f;

struct Mover {
    bool used = false;
    int8_t room = -1;
    uint32_t key = 0;
    cXyz lastPos;
    int tail = 0;

    cXyz sentPos;
    csXyz sentAngle;
    bool everSent = false;
};
Mover s_movers[kMaxMovers];

struct PendingMove {
    bool used = false;
    MsgObjectMove msg{};
};
PendingMove s_pendingMoves[kMaxMovers];

uint32_t s_movesSent = 0;
uint32_t s_movesApplied = 0;
uint32_t s_movesReceived = 0;
int s_diagMovers = 0;

void reset_animals_and_torches();

void reset_movers() {
    for (int i = 0; i < kMaxMovers; ++i) {
        s_movers[i] = Mover{};
        s_pendingMoves[i] = PendingMove{};
    }
    reset_animals_and_torches();

    forget_tracked_positions();
}

Mover* find_mover(int8_t room, uint32_t key) {
    for (int i = 0; i < kMaxMovers; ++i) {
        if (s_movers[i].used && s_movers[i].room == room && s_movers[i].key == key) {
            return &s_movers[i];
        }
    }
    return nullptr;
}

Mover* add_mover(int8_t room, uint32_t key, const cXyz& pos) {
    int slot = -1;
    for (int i = 0; i < kMaxMovers; ++i) {
        if (!s_movers[i].used) { slot = i; break; }

        if (slot < 0 || s_movers[i].tail < s_movers[slot].tail) slot = i;
    }
    s_movers[slot] = Mover{};
    s_movers[slot].used = true;
    s_movers[slot].room = room;
    s_movers[slot].key = key;
    s_movers[slot].lastPos = pos;
    s_movers[slot].tail = kMoverTailTicks;
    return &s_movers[slot];
}

bool nearest_to(const cXyz& pos) {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return false;

    if (coop_player_paused(coop_net_local_id())) return false;
    const f32 ours = (pos - alink->current.pos).abs();
    const uint8_t us = coop_net_local_id();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (static_cast<uint8_t>(i) == us) continue;
        const CoopPeer& peer = features_peer_of(static_cast<uint8_t>(i));
        if (!peer.present || !peer.inGame || coop_player_paused(static_cast<uint8_t>(i))) continue;
        const cXyz theirs(peer.x, peer.y, peer.z);
        const f32 d = (pos - theirs).abs();
        if (d < ours) return false;
        if (d == ours && static_cast<uint8_t>(i) < us) return false;
    }
    return true;
}

bool runs_by_itself(fopAc_ac_c* actor) {
    if (is_timed_hazard(actor)) return true;
    switch (fopAcM_GetName(actor)) {
    case fpcNm_Obj_Lv6TogeTrap_e:
    case fpcNm_Obj_Lv6TogeRoll_e:
    case fpcNm_Obj_RotTrap_e:
    case fpcNm_Obj_TogeTrap_e:
    case fpcNm_Obj_Lv6FuriTrap_e:
    case fpcNm_Obj_WoodPendulum_e:
    case fpcNm_Obj_Lv8KekkaiTrap_e:
        return true;
    default:
        return false;
    }
}

bool mechanism_is_ours(fopAc_ac_c* actor, int8_t room) {
    if (coop_player_paused(coop_net_local_id())) return false;

    if (!runs_by_itself(actor)) return nearest_to(actor->home.pos);
    if (room >= 0 && room < kRooms && s_rooms.owner[room] != kRoomOwnerNone) {
        return s_rooms.owner[room] == our_owner_id();
    }
    return nearest_to(actor->home.pos);
}

const int kPushQuietTicks = 24;
struct PushQuiet {
    uint32_t key = 0;
    int8_t room = 0;
    int ticks = 0;
};
PushQuiet s_pushQuiet[16];
uint32_t s_pushesSent = 0;
uint32_t s_pushesApplied = 0;

void age_push_quiet() {
    for (PushQuiet& q : s_pushQuiet) {
        if (q.ticks > 0 && --q.ticks == 0) q.key = 0;
    }
}

bool push_is_quiet(int8_t room, uint32_t key) {
    for (const PushQuiet& q : s_pushQuiet) {
        if (q.ticks > 0 && q.key == key && q.room == room) return true;
    }
    return false;
}

void push_go_quiet(int8_t room, uint32_t key) {
    PushQuiet* slot = &s_pushQuiet[0];
    for (PushQuiet& q : s_pushQuiet) {
        if (q.ticks == 0) { slot = &q; break; }
        if (q.ticks < slot->ticks) slot = &q;
    }
    slot->key = key;
    slot->room = room;
    slot->ticks = kPushQuietTicks;
}

bool is_pushable_block(fopAc_ac_c* actor) {
    return is_ice_block(actor);
}

struct PendingPush {
    bool used = false;
    MsgObjectPush msg{};
};
PendingPush s_pendingPushes[8];

void apply_pending_pushes() {
    bool any = false;
    for (const PendingPush& pending : s_pendingPushes) {
        if (pending.used) { any = true; break; }
    }
    if (!any) return;

    BreakableList list;
    collect_breakables(list);
    for (PendingPush& pending : s_pendingPushes) {
        if (!pending.used) continue;
        const MsgObjectPush msg = pending.msg;
        pending = PendingPush{};
        if (msg.dir > 3) continue;

        fopAc_ac_c* actor = find_local_breakable(list, msg.room, msg.key);
        if (!is_pushable_block(actor)) continue;
        auto* block = static_cast<daObjIceBlk_c*>(actor);
        for (int d = 0; d < 4; ++d) {
            block->mCounter[d] = (d == msg.dir) ? 1 : 0;
        }

        push_go_quiet(msg.room, msg.key);
        ++s_pushesApplied;
    }
}

void capture_block_pushes(BreakableList& list) {
    age_push_quiet();
    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        if (!is_pushable_block(actor)) continue;
        const int8_t room = list.rooms[i];
        const uint32_t key = list.keys[i];
        if (push_is_quiet(room, key)) continue;

        auto* block = static_cast<daObjIceBlk_c*>(actor);
        int dir = -1;
        for (int d = 0; d < 4; ++d) {
            if (block->mCounter[d] != 0) dir = d;
        }
        if (dir < 0) continue;

        if (!nearest_to(actor->current.pos)) continue;

        MsgObjectPush msg{};
        msg.key = key;
        msg.room = room;
        msg.dir = static_cast<uint8_t>(dir);
        coop_net_send(kMsgObjectPush, &msg, sizeof(msg));
        push_go_quiet(room, key);
        ++s_pushesSent;
    }
}

bool carry_driven_elsewhere(int8_t room, uint32_t key);
bool carry_driven_here(int8_t room, uint32_t key);

void capture_moved_objects(BreakableList& list) {
    int live = 0;
    for (int i = 0; i < kMaxMovers; ++i) {
        if (s_movers[i].used && --s_movers[i].tail <= 0) s_movers[i] = Mover{};
        if (s_movers[i].used) ++live;
    }
    s_diagMovers = live;

    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        const int8_t room = list.rooms[i];
        const uint32_t key = list.keys[i];

        if (is_pushable_block(actor)) continue;

        if (carry_driven_elsewhere(room, key)) continue;
        ObjPos* last = remember_position(room, key, actor->current.pos, actor->shape_angle);
        if (last == nullptr) continue;
        const cXyz step = actor->current.pos - last->pos;
        const int turn = angle_step(actor->shape_angle, last->angle);
        last->pos = actor->current.pos;
        last->angle = actor->shape_angle;

        bool stateChanged = false;
        if (s_tick % 3 == 0 && has_blob(actor)) {
            const uint32_t h = blob_hash(actor);
            if (h != last->stateHash) {
                last->stateHash = h;
                stateChanged = true;
            }
        }
        if (step.abs() < kMoverEpsilon && turn < kMoverAngleEpsilon && !stateChanged) continue;

        if (is_timed_hazard(actor) && s_tick % 30 != 0) continue;

        Mover* m = find_mover(room, key);
        if (m == nullptr) {
            add_mover(room, key, actor->current.pos);
            m = find_mover(room, key);
            if (m == nullptr) continue;
        }
        m->lastPos = actor->current.pos;
        m->tail = kMoverTailTicks;
        const bool mechanism = has_blob(actor);
        if (mechanism ? !mechanism_is_ours(actor, room) : !nearest_to(actor->current.pos)) continue;
        daAlink_c* alink = daAlink_getAlinkActorClass();

        if (alink == nullptr ||
            (!mechanism && (actor->current.pos - alink->current.pos).abs() > kMoverRelayRange))
        {
            continue;
        }

        if (!stateChanged && m->everSent &&
            (actor->current.pos - m->sentPos).abs() < kMoverEpsilon &&
            angle_step(actor->shape_angle, m->sentAngle) < kMoverAngleEpsilon)
        {
            continue;
        }
        m->sentPos = actor->current.pos;
        m->sentAngle = actor->shape_angle;
        m->everSent = true;

        MsgObjectMove msg{};
        msg.key = key;
        msg.room = room;
        msg.pos[0] = actor->current.pos.x;
        msg.pos[1] = actor->current.pos.y;
        msg.pos[2] = actor->current.pos.z;
        msg.angle[0] = actor->shape_angle.x;
        msg.angle[1] = actor->shape_angle.y;
        msg.angle[2] = actor->shape_angle.z;
        msg.phaseCount = static_cast<uint8_t>(object_phase(actor, msg.phase));
        msg.blobLen = static_cast<uint8_t>(object_blob(actor, msg.blob, sizeof(msg.blob)));
        msg.procName = fopAcM_GetName(actor);
        msg.homeAngleY = actor->home.angle.y;
        msg.setID = actor->setID;
        msg.param = fopAcM_GetParam(actor);
        msg.home[0] = actor->home.pos.x;
        msg.home[1] = actor->home.pos.y;
        msg.home[2] = actor->home.pos.z;
        coop_net_send(kMsgObjectMove, &msg, sizeof(msg));
        ++s_movesSent;
    }
}

fopAc_ac_c* find_by_placement(BreakableList& list, const MsgObjectMove& msg) {
    const cXyz home(msg.home[0], msg.home[1], msg.home[2]);
    fopAc_ac_c* best = nullptr;
    int bestIdx = -1;
    f32 bestDist = 30.0f;
    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* a = list.actors[i];
        if (list.rooms[i] != msg.room || fopAcM_GetName(a) != msg.procName) continue;
        const f32 d = (a->home.pos - home).abs();
        if (d < bestDist) {
            bestDist = d;
            best = a;
            bestIdx = i;
        }
    }
    static uint32_t s_logged[32] = {};
    static int s_loggedNext = 0;
    bool seen = false;
    for (uint32_t k : s_logged) seen = seen || k == msg.key;
    if (!seen) {
        s_logged[s_loggedNext] = msg.key;
        s_loggedNext = (s_loggedNext + 1) % 32;
        if (best == nullptr) {
            coop_log::warn("coop_mod: [OBJ] no match for key {:#010x} room={} proc={} param={:#x} "
                           "setID={} home=({:.1f},{:.1f},{:.1f}) angY={}",
                msg.key, static_cast<int>(msg.room), static_cast<int>(msg.procName), msg.param,
                static_cast<int>(msg.setID), msg.home[0], msg.home[1], msg.home[2],
                static_cast<int>(msg.homeAngleY));
        } else {
            coop_log::warn("coop_mod: [OBJ] key differs for proc={} room={}: theirs {:#010x} "
                           "(param={:#x} setID={} home=({:.1f},{:.1f},{:.1f}) angY={}) ours {:#010x} "
                           "(param={:#x} setID={} home=({:.1f},{:.1f},{:.1f}) angY={}) - matched by "
                           "placement",
                static_cast<int>(msg.procName), static_cast<int>(msg.room), msg.key, msg.param,
                static_cast<int>(msg.setID), msg.home[0], msg.home[1], msg.home[2],
                static_cast<int>(msg.homeAngleY), list.keys[bestIdx], fopAcM_GetParam(best),
                static_cast<int>(best->setID), best->home.pos.x, best->home.pos.y,
                best->home.pos.z, static_cast<int>(best->home.angle.y));
        }
    }
    return best;
}

void apply_pending_moves() {
    bool any = false;
    for (int i = 0; i < kMaxMovers; ++i) {
        if (s_pendingMoves[i].used) { any = true; break; }
    }
    if (!any) return;

    static const void* s_seenLink = nullptr;
    static uint32_t s_seenLinkTick = 0;
    daAlink_c* me = daAlink_getAlinkActorClass();
    if (me != s_seenLink) {
        s_seenLink = me;
        s_seenLinkTick = s_tick;
    }
    if (me == nullptr || dComIfGp_isEnableNextStage() || s_tick - s_seenLinkTick < 90) {
        for (int i = 0; i < kMaxMovers; ++i) s_pendingMoves[i] = PendingMove{};
        return;
    }

    BreakableList list;
    collect_breakables(list);

    for (int i = 0; i < kMaxMovers; ++i) {
        if (!s_pendingMoves[i].used) continue;
        const MsgObjectMove msg = s_pendingMoves[i].msg;
        s_pendingMoves[i] = PendingMove{};

        fopAc_ac_c* actor = find_local_breakable(list, msg.room, msg.key);
        if (actor == nullptr) actor = find_by_placement(list, msg);
        if (actor == nullptr) continue;

        if (fpcM_IsCreating(fopAcM_GetID(actor))) continue;

        if (carry_driven_here(msg.room, msg.key)) continue;

        if (has_blob(actor) ? mechanism_is_ours(actor, msg.room) : nearest_to(actor->current.pos)) {
            continue;
        }

        const cXyz want(msg.pos[0], msg.pos[1], msg.pos[2]);
        const cXyz gap = want - actor->current.pos;

        const bool stateCopied = msg.blobLen > 0 && msg.blobLen <= sizeof(msg.blob) &&
                                 apply_object_blob(actor, msg.blob, msg.blobLen);
        if (stateCopied || gap.abs() > kMoverSnapDist) {
            actor->current.pos = want;
            actor->old.pos = want;
        } else {
            actor->current.pos.x += gap.x * kMoverBlend;
            actor->current.pos.y += gap.y * kMoverBlend;
            actor->current.pos.z += gap.z * kMoverBlend;
        }
        actor->shape_angle.x = msg.angle[0];
        actor->shape_angle.y = msg.angle[1];
        actor->shape_angle.z = msg.angle[2];
        if (msg.phaseCount > 0 && msg.phaseCount <= 8) {
            apply_object_phase(actor, msg.phase, msg.phaseCount);
        }

        Mover* m = find_mover(msg.room, msg.key);
        if (m == nullptr) m = add_mover(msg.room, msg.key, actor->current.pos);
        m->lastPos = actor->current.pos;
        m->tail = kMoverTailTicks;
        ++s_movesApplied;
    }
}

bool carry_live() {
    return session_live() && movers_enabled();
}

const int kMaxCarried = 8;
const int kCarrySendEvery = 2;
const int kCarryRestTicks = 15;
const int kCarryFlightMaxTicks = 300;
const int kCarryStaleTicks = 30;
const int kCarryNudgeTicks = 30;

enum CarriedPhase : uint8_t { kPhaseHeld, kPhaseFlying };

struct Carried {
    bool used = false;
    fpc_ProcID id = fpcM_ERROR_PROCESS_ID_e;
    uint32_t key = 0;
    int8_t room = 0;
    uint8_t phase = kPhaseHeld;
    bool rodStatue = false;
    cXyz lastPos;
    int still = 0;
    int flight = 0;
};
Carried s_carried[kMaxCarried];

struct RemoteCarry {
    bool used = false;
    uint8_t from = kCoopNoPlayer;
    MsgCarry msg{};
    uint32_t heardTick = 0;
    bool applied = false;

    bool simulating = false;
    cXyz simSpeed;
    f32 simGravity = -3.0f;
    int age = 0;
    bool spin = false;

    bool nudging = false;
    cXyz nudgeTo;
};
RemoteCarry s_remoteCarry[kMaxCarried];

void to_local(const cXyz& d, s16 yaw, f32* out) {
    const f32 c = cM_scos(yaw);
    const f32 sn = cM_ssin(yaw);
    out[0] = d.x * c - d.z * sn;
    out[1] = d.y;
    out[2] = d.x * sn + d.z * c;
}

cXyz from_local(const f32* l, s16 yaw) {
    const f32 c = cM_scos(yaw);
    const f32 sn = cM_ssin(yaw);
    return cXyz(l[0] * c + l[2] * sn, l[1], -l[0] * sn + l[2] * c);
}

void fill_carry(MsgCarry& msg, const Carried& c, fopAc_ac_c* actor, uint8_t state) {
    msg.key = c.key;
    msg.room = c.room;
    msg.state = state;
    if (actor == nullptr) return;
    msg.pos[0] = actor->current.pos.x;
    msg.pos[1] = actor->current.pos.y;
    msg.pos[2] = actor->current.pos.z;
    msg.angle[0] = actor->shape_angle.x;
    msg.angle[1] = actor->shape_angle.y;
    msg.angle[2] = actor->shape_angle.z;
}

void send_held(const Carried& c, fopAc_ac_c* actor, daAlink_c* alink) {
    MsgCarry msg{};
    fill_carry(msg, c, actor, kCarryHeld);
    if (c.rodStatue) {

        msg.flags |= kCarryFlagStatue;
        if (fopAcM_GetName(actor) == fpcNm_CSTATUE_e) {
            auto* statue = static_cast<daCstatue_c*>(actor);
            msg.statueAnim = statue->mCurrentAnim;
            msg.statueFrame = statue->mpMorf != nullptr ? statue->mpMorf->getFrame() : 0.0f;
        } else if (fopAcM_GetName(actor) == fpcNm_CSTAF_e) {
            auto* statue = static_cast<daCstaF_c*>(actor);
            msg.statueAnim = statue->m_action;
            msg.statueFrame =
                statue->mp_modelMorf != nullptr ? statue->mp_modelMorf->getFrame() : 0.0f;
        }
    } else if (alink != nullptr) {
        msg.flags |= kCarryFlagRelative;
        to_local(actor->current.pos - alink->current.pos, alink->shape_angle.y, msg.rel);
        msg.relYaw = static_cast<int16_t>(actor->shape_angle.y - alink->shape_angle.y);
    }
    coop_net_send(kMsgCarry, &msg, sizeof(msg));
}

void send_simple(const Carried& c, fopAc_ac_c* actor, uint8_t state) {
    MsgCarry msg{};
    fill_carry(msg, c, actor, state);
    if (state == kCarryThrown && actor != nullptr) {
        msg.angle[1] = actor->current.angle.y;
        msg.speedF = actor->speedF;
        msg.speedY = actor->speed.y;
        msg.gravity = actor->gravity;
    }
    coop_net_send(kMsgCarry, &msg, sizeof(msg));
}

struct CarriedNow {
    fopAc_ac_c* actors[kMaxCarried];
    int count = 0;
};

void* collect_carried_now(void* proc, void* data) {
    auto* out = static_cast<CarriedNow*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || out->count >= kMaxCarried) return nullptr;
    if (fopAcM_GetGroup(actor) == fopAc_PLAYER_e) return nullptr;

    if (fopAcM_GetGroup(actor) == fopAc_ENEMY_e && enemies_setting_on() &&
        fopAcM_GetName(actor) != fpcNm_NI_e) {
        return nullptr;
    }
    if (fopAcM_checkCarryNow(actor) == 0) return nullptr;

    if (fopAcM_GetName(actor) == kProcNbomb || fopAcM_GetName(actor) == fpcNm_BOOMERANG_e) {
        return nullptr;
    }
    out->actors[out->count++] = actor;
    return nullptr;
}

void capture_carried() {
    CarriedNow now;
    fopAcM_Search(collect_carried_now, &now);

    daAlink_c* alink = daAlink_getAlinkActorClass();
    fopAc_ac_c* statue = alink != nullptr ? alink->getCopyRodControllActor() : nullptr;

    if (statue != nullptr && carry_driven_elsewhere(fopAcM_GetRoomNo(statue),
                                 compute_placement_key(statue))) {
        fopAc_ac_c* rod = alink->getCopyRodActor();
        if (rod != nullptr) static_cast<daCrod_c*>(rod)->offControll();
        coop_toast("Taken", "Someone else is controlling that statue.");
        statue = nullptr;
    }
    if (statue != nullptr && now.count < kMaxCarried) {
        bool listed = false;
        for (int i = 0; i < now.count; ++i) listed = listed || now.actors[i] == statue;
        if (!listed) now.actors[now.count++] = statue;
    }

    for (int i = 0; i < now.count; ++i) {
        fopAc_ac_c* actor = now.actors[i];
        const fpc_ProcID id = fopAcM_GetID(actor);
        Carried* c = nullptr;
        for (Carried& e : s_carried) {
            if (e.used && e.id == id) c = &e;
        }
        if (c == nullptr) {
            const uint32_t key = compute_placement_key(actor);
            if (key == 0) continue;
            for (Carried& e : s_carried) {
                if (!e.used) { c = &e; break; }
            }
            if (c == nullptr) continue;
            *c = Carried{};
            c->used = true;
            c->id = id;
            c->key = key;
            c->room = fopAcM_GetRoomNo(actor);
            coop_log::info("coop_mod: [CARRY] picked up {:#x} (proc {})", key,
                static_cast<int>(fopAcM_GetName(actor)));
        }
        c->phase = kPhaseHeld;
        c->rodStatue = actor == statue;
        c->still = 0;
        c->flight = 0;
        c->lastPos = actor->current.pos;
    }

    for (Carried& c : s_carried) {
        if (!c.used) continue;
        fopAc_ac_c* actor = fopAcM_SearchByID(c.id);
        if (actor == nullptr) {

            send_simple(c, nullptr, kCarryGone);
            coop_log::info("coop_mod: [CARRY] {:#x} is gone", c.key);
            c = Carried{};
            continue;
        }
        const bool heldNow = c.rodStatue ? actor == statue : fopAcM_checkCarryNow(actor) != 0;
        if (c.phase == kPhaseHeld && !heldNow) {
            if (c.rodStatue) {

                send_simple(c, actor, kCarryRest);
                c = Carried{};
                continue;
            }

            send_simple(c, actor, kCarryThrown);
            c.phase = kPhaseFlying;
            c.flight = 0;
            c.still = 0;
            c.lastPos = actor->current.pos;
            continue;
        }
        if (c.phase == kPhaseFlying) {
            ++c.flight;

            if (s_tick % kCarrySendEvery == 0) {
                MsgCarry spin{};
                fill_carry(spin, c, actor, kCarryThrown);
                spin.flags |= kCarryFlagSpin;
                coop_net_send(kMsgCarry, &spin, sizeof(spin));
            }
            const f32 moved = (actor->current.pos - c.lastPos).abs();
            c.still = moved < 1.0f ? c.still + 1 : 0;
            c.lastPos = actor->current.pos;
            if (c.still >= kCarryRestTicks || c.flight >= kCarryFlightMaxTicks) {
                send_simple(c, actor, kCarryRest);
                c = Carried{};
            }
            continue;
        }
        c.lastPos = actor->current.pos;
        if (s_tick % kCarrySendEvery == 0) send_held(c, actor, alink);
    }
}

bool carry_gone_live() {
    return session_live() && breakables_enabled();
}

void carry_on_message(const MsgCarry& msg, uint8_t from) {
    if (msg.state == kCarryGone ? !carry_gone_live() : !carry_live()) return;
    if ((msg.flags & kCarryFlagSpin) != 0) {

        for (RemoteCarry& r : s_remoteCarry) {
            if (!r.used || r.msg.key != msg.key || r.msg.room != msg.room) continue;
            if (r.msg.state != kCarryThrown || !r.applied) return;
            r.msg.angle[0] = msg.angle[0];
            r.msg.angle[1] = msg.angle[1];
            r.msg.angle[2] = msg.angle[2];
            r.spin = true;
            r.heardTick = s_tick;
        }
        return;
    }
    RemoteCarry* slot = nullptr;
    for (RemoteCarry& r : s_remoteCarry) {
        if (r.used && r.msg.key == msg.key && r.msg.room == msg.room) slot = &r;
    }
    if (slot == nullptr) {
        for (RemoteCarry& r : s_remoteCarry) {
            if (!r.used) { slot = &r; break; }
        }
    }
    if (slot == nullptr) {

        slot = &s_remoteCarry[0];
        for (RemoteCarry& r : s_remoteCarry) {
            if (r.heardTick < slot->heardTick) slot = &r;
        }
    }
    const bool keepFlight = slot->used && slot->simulating && msg.state == kCarryRest;
    const bool wasSimulating = slot->simulating;
    const cXyz simSpeed = slot->simSpeed;
    const f32 simGravity = slot->simGravity;
    *slot = RemoteCarry{};
    slot->used = true;
    slot->from = from;
    slot->msg = msg;
    slot->heardTick = s_tick;
    if (keepFlight) {
        slot->simulating = wasSimulating;
        slot->simSpeed = simSpeed;
        slot->simGravity = simGravity;
    }
}

struct CarryFind {
    uint32_t key;
    int8_t room;
    fopAc_ac_c* found;
};

void* find_by_placement_key(void* proc, void* data) {
    auto* find = static_cast<CarryFind*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || find->found != nullptr) return nullptr;
    if (fopAcM_GetGroup(actor) == fopAc_PLAYER_e) return nullptr;
    if (fopAcM_GetRoomNo(actor) != find->room) return nullptr;
    if (compute_placement_key(actor) != find->key) return nullptr;
    find->found = actor;
    return nullptr;
}

void place(fopAc_ac_c* actor, const cXyz& at) {
    actor->current.pos = at;
    actor->old.pos = at;
    actor->speedF = 0.0f;
    actor->speed.set(0.0f, 0.0f, 0.0f);
}

void apply_remote_carry() {
    const bool full = carry_live();
    daAlink_c* me = daAlink_getAlinkActorClass();
    for (RemoteCarry& r : s_remoteCarry) {
        if (!r.used) continue;
        const MsgCarry& msg = r.msg;
        if (!full && msg.state != kCarryGone) {
            r = RemoteCarry{};
            continue;
        }
        if (msg.state == kCarryHeld && s_tick - r.heardTick > kCarryStaleTicks) {
            r = RemoteCarry{};
            continue;
        }
        CarryFind find{msg.key, msg.room, nullptr};
        fopAcM_Search(find_by_placement_key, &find);
        fopAc_ac_c* actor = find.found;
        if (actor == nullptr) {

            if (msg.state != kCarryHeld) r = RemoteCarry{};
            continue;
        }

        if (fopAcM_checkCarryNow(actor) != 0 ||
            (me != nullptr && me->getCopyRodControllActor() == actor)) {
            if (msg.state != kCarryHeld) r = RemoteCarry{};
            continue;
        }
        const bool isPot = fopAcM_GetName(actor) == fpcNm_Obj_Carry_e;

        switch (msg.state) {
        case kCarryGone:
            coop_log::info("coop_mod: [CARRY] {:#x} broke in their game - breaking ours", msg.key);

            if (isPot) static_cast<daObjCarry_c*>(actor)->obj_break(true, true, true);
            fopAcM_delete(actor);
            r = RemoteCarry{};
            break;

        case kCarryHeld: {
            if ((msg.flags & kCarryFlagRelative) != 0) {

                f32 px = 0.0f, py = 0.0f, pz = 0.0f;
                s16 yaw = 0;
                if (puppet_hook_get_pose_of(r.from, &px, &py, &pz, &yaw, nullptr, nullptr)) {
                    place(actor, cXyz(px, py, pz) + from_local(msg.rel, yaw));
                    actor->shape_angle.x = msg.angle[0];
                    actor->shape_angle.y = static_cast<s16>(yaw + msg.relYaw);
                    actor->shape_angle.z = msg.angle[2];
                    actor->current.angle.y = actor->shape_angle.y;
                    break;
                }
            }
            place(actor, cXyz(msg.pos[0], msg.pos[1], msg.pos[2]));
            actor->shape_angle.x = msg.angle[0];
            actor->shape_angle.y = msg.angle[1];
            actor->shape_angle.z = msg.angle[2];
            actor->current.angle.y = msg.angle[1];
            break;
        }

        case kCarryThrown:
            if (r.spin) {

                r.spin = false;
                actor->shape_angle.x = msg.angle[0];
                actor->shape_angle.y = msg.angle[1];
                actor->shape_angle.z = msg.angle[2];
            }
            if (r.applied) {

                if (r.simulating && ++r.age < kCarryFlightMaxTicks) {
                    actor->current.pos += r.simSpeed;
                    r.simSpeed.y = std::max(r.simSpeed.y + r.simGravity, -100.0f);
                }
                break;
            }
            r.applied = true;
            actor->current.pos.set(msg.pos[0], msg.pos[1], msg.pos[2]);
            actor->old.pos = actor->current.pos;
            actor->current.angle.y = msg.angle[1];
            actor->speedF = msg.speedF;
            actor->speed.y = msg.speedY;
            if (isPot) {

                static_cast<daObjCarry_c*>(actor)->mode_init_drop(0);
            } else {
                r.simulating = true;
                r.simSpeed.set(msg.speedF * cM_ssin(msg.angle[1]), msg.speedY,
                    msg.speedF * cM_scos(msg.angle[1]));
                r.simGravity = msg.gravity < 0.0f ? msg.gravity : -3.0f;
            }
            break;

        case kCarryRest: {

            const cXyz to(msg.pos[0], msg.pos[1], msg.pos[2]);
            if (!r.nudging) {
                r.nudging = true;
                r.age = 0;
                r.simulating = false;
                r.nudgeTo = to;
            }
            const cXyz gap = r.nudgeTo - actor->current.pos;
            if (gap.abs() < 2.0f || ++r.age >= kCarryNudgeTicks) {
                actor->current.pos = r.nudgeTo;
                actor->old.pos = r.nudgeTo;
                r = RemoteCarry{};
                break;
            }
            actor->current.pos += gap * 0.25f;
            break;
        }

        default:
            r = RemoteCarry{};
            break;
        }
    }
}

bool carry_driven_elsewhere(int8_t room, uint32_t key) {
    for (const RemoteCarry& r : s_remoteCarry) {
        if (r.used && r.msg.room == room && r.msg.key == key) return true;
    }
    return false;
}

bool is_torch(fopAc_ac_c* actor) {
    switch (fopAcM_GetName(actor)) {
    case fpcNm_Obj_Lv1Cdl00_e:
    case fpcNm_Obj_Lv1Cdl01_e:
    case fpcNm_Obj_Lv2Candle_e:
    case fpcNm_Obj_Lv3Candle_e:
    case fpcNm_Obj_FireWood_e:
    case fpcNm_Obj_FireWood2_e:
        return true;
    default:
        return false;
    }
}

uint8_t* torch_lit_flag(fopAc_ac_c* actor) {
    switch (fopAcM_GetName(actor)) {
    case fpcNm_Obj_Lv1Cdl00_e:
        return reinterpret_cast<uint8_t*>(&static_cast<daLv1Cdl00_c*>(actor)->mIsLit);
    case fpcNm_Obj_Lv1Cdl01_e:
        return reinterpret_cast<uint8_t*>(&static_cast<daLv1Cdl01_c*>(actor)->mIsLit);
    case fpcNm_Obj_Lv2Candle_e:
        return reinterpret_cast<uint8_t*>(&static_cast<daLv2Candle_c*>(actor)->mIsLit);
    case fpcNm_Obj_Lv3Candle_e:
        return &static_cast<daLv3Candle_c*>(actor)->mIsLit;
    case fpcNm_Obj_FireWood_e:
        return &static_cast<daFireWood_c*>(actor)->mIsLit;
    case fpcNm_Obj_FireWood2_e:
        return &static_cast<daFireWood2_c*>(actor)->mIsLit;
    default:
        return nullptr;
    }
}

struct TorchSeen {
    bool used = false;
    fpc_ProcID id = fpcM_ERROR_PROCESS_ID_e;
    bool lit = false;
};
const int kMaxTorches = 48;
TorchSeen s_torches[kMaxTorches];

struct TorchScan {
    fopAc_ac_c* actors[kMaxTorches];
    int count = 0;
};

void* scan_torches(void* proc, void* data) {
    auto* out = static_cast<TorchScan*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || out->count >= kMaxTorches || !is_torch(actor)) return nullptr;
    if (fpcM_IsCreating(fopAcM_GetID(actor))) return nullptr;
    out->actors[out->count++] = actor;
    return nullptr;
}

TorchSeen* torch_slot(fpc_ProcID id, bool add) {
    TorchSeen* free = nullptr;
    for (TorchSeen& t : s_torches) {
        if (t.used && t.id == id) return &t;
        if (!t.used && free == nullptr) free = &t;
    }
    if (!add || free == nullptr) return nullptr;
    *free = TorchSeen{};
    free->used = true;
    free->id = id;
    return free;
}

void capture_torches() {
    TorchScan scan;
    fopAcM_Search(scan_torches, &scan);

    for (TorchSeen& t : s_torches) {
        if (t.used && fopAcM_SearchByID(t.id) == nullptr) t = TorchSeen{};
    }
    for (int i = 0; i < scan.count; ++i) {
        fopAc_ac_c* torch = scan.actors[i];
        const uint8_t* flag = torch_lit_flag(torch);
        if (flag == nullptr) continue;
        const bool lit = *flag != 0;
        TorchSeen* seen = torch_slot(fopAcM_GetID(torch), false);
        if (seen == nullptr) {
            seen = torch_slot(fopAcM_GetID(torch), true);
            if (seen != nullptr) seen->lit = lit;
            continue;
        }
        if (seen->lit == lit) continue;
        seen->lit = lit;
        const uint32_t key = compute_placement_key(torch);
        if (key == 0) continue;
        MsgTorch msg{};
        msg.key = key;
        msg.room = static_cast<int8_t>(fopAcM_GetRoomNo(torch));
        msg.lit = lit ? 1 : 0;
        msg.procName = fopAcM_GetName(torch);
        msg.home[0] = torch->home.pos.x;
        msg.home[1] = torch->home.pos.y;
        msg.home[2] = torch->home.pos.z;
        coop_net_send(kMsgTorch, &msg, sizeof(msg));
        coop_log::info("coop_mod: [TORCH] {:#010x} {} here", key, lit ? "lit" : "put out");
    }
}

void torch_on_message(const MsgTorch& msg) {
    TorchScan scan;
    fopAcM_Search(scan_torches, &scan);
    const cXyz home(msg.home[0], msg.home[1], msg.home[2]);
    for (int i = 0; i < scan.count; ++i) {
        fopAc_ac_c* torch = scan.actors[i];
        if (fopAcM_GetRoomNo(torch) != msg.room || fopAcM_GetName(torch) != msg.procName) continue;
        if (compute_placement_key(torch) != msg.key && (torch->home.pos - home).abs() > 30.0f) {
            continue;
        }
        uint8_t* flag = torch_lit_flag(torch);
        if (flag == nullptr) return;
        *flag = msg.lit;

        TorchSeen* seen = torch_slot(fopAcM_GetID(torch), true);
        if (seen != nullptr) seen->lit = msg.lit != 0;
        mDoAud_seStart(msg.lit ? Z2SE_OBJ_FIRE_IGNITION : Z2SE_OBJ_FIRE_OFF, &torch->current.pos, 0,
            dComIfGp_getReverb(fopAcM_GetRoomNo(torch)));
        return;
    }
}

bool is_animal(fopAc_ac_c* actor) {
    const s16 name = fopAcM_GetName(actor);
    return name == fpcNm_NI_e || name == fpcNm_COW_e;
}

struct AnimalSteer {
    bool used = false;
    uint32_t key = 0;
    int8_t room = 0;
    int16_t procName = 0;
    cXyz home;
    cXyz pos;
    int16_t angleY = 0;
    f32 speedF = 0.0f;
    f32 theirDist = 0.0f;
    uint32_t heardTick = 0;
};
const int kMaxAnimals = 48;
AnimalSteer s_animals[kMaxAnimals];
const uint32_t kAnimalFreshTicks = 30;
const f32 kAnimalSnapDist = 400.0f;
const f32 kAnimalPull = 0.15f;

struct AnimalScan {
    fopAc_ac_c* actors[kMaxAnimals];
    int count = 0;
};

void* scan_animals(void* proc, void* data) {
    auto* out = static_cast<AnimalScan*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || out->count >= kMaxAnimals || !is_animal(actor)) return nullptr;
    if (fpcM_IsCreating(fopAcM_GetID(actor))) return nullptr;
    out->actors[out->count++] = actor;
    return nullptr;
}

AnimalSteer* animal_steer_for(fopAc_ac_c* actor, uint32_t key) {
    const int8_t room = static_cast<int8_t>(fopAcM_GetRoomNo(actor));
    for (AnimalSteer& a : s_animals) {
        if (!a.used || a.room != room || a.procName != fopAcM_GetName(actor)) continue;
        if (a.key == key || (a.home - actor->home.pos).abs() < 30.0f) return &a;
    }
    return nullptr;
}

void tick_animals() {
    AnimalScan scan;
    fopAcM_Search(scan_animals, &scan);
    daAlink_c* me = daAlink_getAlinkActorClass();
    if (me == nullptr) return;
    for (int i = 0; i < scan.count; ++i) {
        fopAc_ac_c* animal = scan.actors[i];
        if (fopAcM_checkCarryNow(animal) != 0) continue;
        const uint32_t key = compute_placement_key(animal);
        if (key == 0) continue;

        if (carry_driven_elsewhere(static_cast<int8_t>(fopAcM_GetRoomNo(animal)), key)) continue;
        AnimalSteer* steer = animal_steer_for(animal, key);
        const bool fresh = steer != nullptr && s_tick - steer->heardTick <= kAnimalFreshTicks;

        const cXyz shared = fresh ? steer->pos : animal->current.pos;
        const f32 mine = (shared - me->current.pos).abs();
        bool ours = nearest_to(shared);

        if (fresh && steer->theirDist < mine) ours = false;

        if (ours) {
            if (s_tick % 6 != 0) continue;
            MsgAnimal msg{};
            msg.key = key;
            msg.room = static_cast<int8_t>(fopAcM_GetRoomNo(animal));
            msg.procName = fopAcM_GetName(animal);
            msg.home[0] = animal->home.pos.x;
            msg.home[1] = animal->home.pos.y;
            msg.home[2] = animal->home.pos.z;
            msg.pos[0] = animal->current.pos.x;
            msg.pos[1] = animal->current.pos.y;
            msg.pos[2] = animal->current.pos.z;
            msg.angleY = animal->shape_angle.y;
            msg.speedF = animal->speedF;
            msg.dist = (animal->current.pos - me->current.pos).abs();
            coop_net_send(kMsgAnimal, &msg, sizeof(msg));
            continue;
        }
        if (!fresh) continue;

        const cXyz gap = steer->pos - animal->current.pos;
        if (gap.abs() > kAnimalSnapDist) {
            animal->current.pos = steer->pos;
            animal->old.pos = steer->pos;
        } else {
            animal->current.pos += gap * kAnimalPull;
        }
        const s16 turn = static_cast<s16>(steer->angleY - animal->shape_angle.y);
        animal->shape_angle.y = static_cast<s16>(animal->shape_angle.y + turn / 4);
        animal->current.angle.y = animal->shape_angle.y;
    }
}

void animal_on_message(const MsgAnimal& msg) {
    AnimalSteer* slot = nullptr;
    AnimalSteer* free = nullptr;
    AnimalSteer* oldest = &s_animals[0];
    const cXyz home(msg.home[0], msg.home[1], msg.home[2]);
    for (AnimalSteer& a : s_animals) {
        if (a.used && a.room == msg.room && a.procName == msg.procName &&
            (a.key == msg.key || (a.home - home).abs() < 30.0f)) {
            slot = &a;
            break;
        }
        if (!a.used && free == nullptr) free = &a;
        if (a.heardTick < oldest->heardTick) oldest = &a;
    }
    if (slot == nullptr) slot = free != nullptr ? free : oldest;
    slot->used = true;
    slot->key = msg.key;
    slot->room = msg.room;
    slot->procName = msg.procName;
    slot->home = home;
    slot->pos.set(msg.pos[0], msg.pos[1], msg.pos[2]);
    slot->angleY = msg.angleY;
    slot->speedF = msg.speedF;
    slot->theirDist = msg.dist;
    slot->heardTick = s_tick;
}

void reset_animals_and_torches() {
    for (AnimalSteer& a : s_animals) a = AnimalSteer{};
    for (TorchSeen& t : s_torches) t = TorchSeen{};
}

bool carry_driven_here(int8_t room, uint32_t key) {
    for (const Carried& c : s_carried) {
        if (c.used && c.room == room && c.key == key) return true;
    }
    return false;
}

const RemoteCarry* remote_statue(fopAc_ac_c* statue) {
    const int8_t room = fopAcM_GetRoomNo(statue);
    uint32_t key = 0;
    for (const RemoteCarry& r : s_remoteCarry) {
        if (!r.used || r.msg.state != kCarryHeld || (r.msg.flags & kCarryFlagStatue) == 0) continue;
        if (r.msg.room != room) continue;
        if (key == 0) key = compute_placement_key(statue);
        if (r.msg.key == key) return &r;
    }
    return nullptr;
}

HookAction on_statue_set_anime(ModContext*, void* args, void*, void*) {
    auto* statue = mods::arg<daCstatue_c*>(args, 0);
    if (statue == nullptr || !carry_live()) return HOOK_CONTINUE;
    const RemoteCarry* r = remote_statue(statue);
    if (r == nullptr) return HOOK_CONTINUE;

    const uint8_t want = r->msg.statueAnim;
    if (want < 7 && want != statue->mCurrentAnim && statue->mpMorf != nullptr) {
        auto* anm = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(
            statue->mResName, daCstatue_c::m_bckIdxTable[statue->mType][want]));
        if (anm != nullptr) {

            const f32 start = want == 0 ? anm->getFrameMax() - 0.001f : 0.0f;
            f32 speed = 1.0f;
            if (statue->mSph != nullptr) {
                if (want == 2) speed = 5.0f;
                else if (want == 6 || want == 1) speed = 3.0f;
            }
            statue->mpMorf->setAnm(anm, -1, 3.0f, speed, start, -1.0f);
            statue->mpMorf->setFrameF(start);
            statue->mCurrentAnim = want;
        }
    }

    if (want == statue->mCurrentAnim && statue->mpMorf != nullptr && want != 0) {
        const f32 ours = statue->mpMorf->getFrame();
        const f32 theirs = r->msg.statueFrame;
        if (theirs < ours - 1.0f || theirs > ours + 6.0f) statue->mpMorf->setFrameF(theirs);
    }

    if ((statue->mStateFlg0 & 0x4) == 0) {
        statue->initStartBrkBtk();
    } else if (statue->mType != daCstatueType_Small) {
        statue->mAnim1.play();
    }
    if (statue->mType == daCstatueType_Normal2) statue->mAnim1.play();
    statue->mAnim2.play();
    return HOOK_SKIP_ORIGINAL;
}

const int kMaxSeenCarryables = 64;
struct SeenCarryable {
    bool used = false;
    fpc_ProcID id = fpcM_ERROR_PROCESS_ID_e;
    uint32_t key = 0;
    int8_t room = 0;
    cXyz pos;
    uint32_t seenTick = 0;
};
SeenCarryable s_seenCarryables[kMaxSeenCarryables];
uint32_t s_brokenSent = 0;

void capture_broken_carryables(BreakableList& list) {

    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        if (fopAcM_GetName(actor) != fpcNm_Obj_Carry_e) continue;
        const fpc_ProcID id = fopAcM_GetID(actor);
        SeenCarryable* slot = nullptr;
        SeenCarryable* free = nullptr;
        for (SeenCarryable& e : s_seenCarryables) {
            if (e.used && e.id == id) { slot = &e; break; }
            if (!e.used && free == nullptr) free = &e;
        }
        if (slot == nullptr) {
            if (free == nullptr) continue;
            slot = free;
            *slot = SeenCarryable{};
            slot->used = true;
            slot->id = id;
            slot->key = list.keys[i];
            slot->room = list.rooms[i];
        }
        slot->pos = actor->current.pos;
        slot->seenTick = s_tick;
    }

    for (SeenCarryable& e : s_seenCarryables) {
        if (!e.used || e.seenTick == s_tick) continue;
        const bool justGone = e.seenTick + 1 == s_tick;
        const SeenCarryable was = e;
        e = SeenCarryable{};
        if (!justGone) continue;

        if (fopAcM_SearchByID(was.id) != nullptr) continue;

        if (was.room < 0 || !dComIfGp_roomControl_checkRoomDisp(was.room)) continue;
        if (dComIfGp_isEnableNextStage()) continue;

        if (carry_driven_here(was.room, was.key)) continue;

        if (carry_driven_elsewhere(was.room, was.key)) continue;
        if (!nearest_to(was.pos)) continue;
        MsgCarry msg{};
        msg.key = was.key;
        msg.room = was.room;
        msg.state = kCarryGone;
        msg.pos[0] = was.pos.x;
        msg.pos[1] = was.pos.y;
        msg.pos[2] = was.pos.z;
        coop_net_send(kMsgCarry, &msg, sizeof(msg));
        ++s_brokenSent;
        coop_log::info("coop_mod: [CARRY] pot {:#x} broke here - telling the others", was.key);
    }
}

HookAction on_small_statue_set_anime(ModContext*, void* args, void*, void*) {
    auto* statue = mods::arg<daCstaF_c*>(args, 0);
    if (statue == nullptr || !carry_live()) return HOOK_CONTINUE;
    const RemoteCarry* r = remote_statue(statue);
    if (r == nullptr) return HOOK_CONTINUE;

    const uint8_t want = r->msg.statueAnim;
    if (want < 4 && want != statue->m_action && statue->mp_modelMorf != nullptr) {
        auto* anm = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(
            statue->m_arcName, daCstaF_c::m_bckIdxTable[statue->m_type].idx[want]));
        if (anm != nullptr) {

            statue->mp_modelMorf->setAnm(anm, -1, 3.0f, want == 0 ? 0.0f : 1.0f, 0.0f, -1.0f);
            statue->m_action = want;
        }
    }
    if (want == statue->m_action && statue->mp_modelMorf != nullptr && want != 0) {
        const f32 ours = statue->mp_modelMorf->getFrame();
        const f32 theirs = r->msg.statueFrame;
        if (theirs < ours - 1.0f || theirs > ours + 6.0f) statue->mp_modelMorf->setFrameF(theirs);
    }
    if (!statue->m_isStartBrkBtkInit) {
        statue->initStartBrkBtk();
    } else {
        statue->m_btk.play();
    }
    statue->m_brk.play();
    return HOOK_SKIP_ORIGINAL;
}

void reset_carry() {
    for (Carried& c : s_carried) c = Carried{};
    for (RemoteCarry& r : s_remoteCarry) r = RemoteCarry{};
    for (SeenCarryable& e : s_seenCarryables) e = SeenCarryable{};
}

void capture_landed_object_hits(BreakableList& list) {
    dCcS* cc = dComIfG_Ccsp();
    if (cc == nullptr) return;
    const int tgCount = static_cast<int>(cc->field_0x280e);
    if (tgCount <= 0) return;

    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        const int8_t room = list.rooms[i];
        const uint32_t key = list.keys[i];
        if (breakable_is_quiet(room, key)) continue;
        for_each_tg_collider(actor, tgCount, [&](cCcD_Obj* obj, dCcD_GObjInf* inf) {
            (void)obj;
            if (!inf->ChkTgHit()) return false;
            cCcD_Obj* atObj = inf->GetTgHitObj();
            if (atObj == nullptr) return false;
            dCcD_GObjInf* atInf = dCcD_GetGObjInf(atObj);
            if (atInf == nullptr) return false;
            fopAc_ac_c* attacker = inf->GetTgHitAc();
            if (!blow_is_ours(attacker)) return false;

            MsgEnemyHit msg{};
            msg.key = key;
            msg.room = room;
            msg.atType = atInf->GetAtType();
            msg.atp = static_cast<uint8_t>(atInf->GetAtAtp());
            msg.spl = static_cast<uint8_t>(atInf->GetAtSpl());
            msg.mtrl = atInf->GetAtMtrl();
            const cXyz from = attacker->current.pos;
            msg.from[0] = from.x;
            msg.from[1] = from.y;
            msg.from[2] = from.z;
            const cXyz* at = inf->GetTgHitPosP();
            const cXyz where = at != nullptr ? *at : actor->current.pos;
            msg.at[0] = where.x;
            msg.at[1] = where.y;
            msg.at[2] = where.z;
            coop_net_send(kMsgObjectHit, &msg, sizeof(msg));
            breakable_go_quiet(room, key);
            ++s_objHitsSent;
            coop_log::info(
                "coop_mod: [OBJ] relaying our blow on room={} key={:#010x} name={} type={:#x}",
                static_cast<int>(room), key, static_cast<int>(fopAcM_GetName(actor)), msg.atType);
            return true;
        });
    }
}

void inject_pending_object_hits() {
    bool any = false;
    for (int i = 0; i < kMaxPendingObjectHits; ++i) {
        if (s_pendingObjectHits[i].used) { any = true; break; }
    }
    if (!any) return;

    dCcS* cc = dComIfG_Ccsp();
    if (cc == nullptr) return;
    const int tgCount = static_cast<int>(cc->field_0x280e);

    BreakableList list;
    collect_breakables(list);

    for (int i = 0; i < kMaxPendingObjectHits; ++i) {
        if (!s_pendingObjectHits[i].used) continue;
        const MsgEnemyHit msg = s_pendingObjectHits[i].msg;
        s_pendingObjectHits[i] = PendingHit{};

        fopAc_ac_c* actor = find_local_breakable(list, msg.room, msg.key);
        if (actor == nullptr) {

            ++s_objHitsLost;
            continue;
        }

        static dCcD_Stts s_objStts[kMaxPendingObjectHits];
        static dCcD_Sph s_objBlow[kMaxPendingObjectHits];
        dCcD_Stts& blowStts = s_objStts[i];
        dCcD_Sph& blow = s_objBlow[i];
        fopAc_ac_c* stand_in = dComIfGp_getPlayer(0);
        if (stand_in == nullptr) continue;
        blowStts.Init(0xFF, 0xFF, stand_in);
        blow.SetStts(&blowStts);
        blow.SetAtType(msg.atType);
        blow.SetAtAtp(msg.atp);
        blow.SetAtSpl(static_cast<dCcG_At_Spl>(msg.spl));
        blow.SetAtMtrl(msg.mtrl);
        blow.SetC(cXyz(msg.at[0], msg.at[1], msg.at[2]));
        blow.SetR(10.0f);

        const cXyz at(msg.at[0], msg.at[1], msg.at[2]);
        f32 nearest = -1.0f;
        for_each_tg_collider(actor, tgCount, [&](cCcD_Obj* obj, dCcD_GObjInf* inf) {
            (void)inf;
            cCcD_ShapeAttr* shape = obj->GetShapeAttr();
            if (shape == nullptr) return false;
            cXyz centre;
            shape->mAab.CalcCenter(&centre);
            const f32 d = (centre - at).abs();
            if (nearest < 0.0f || d < nearest) nearest = d;
            return false;
        });

        const f32 kSameObjectMargin = 60.0f;

        int landed = 0;
        for_each_tg_collider(actor, tgCount, [&](cCcD_Obj* obj, dCcD_GObjInf* inf) {
            if (nearest >= 0.0f) {
                cCcD_ShapeAttr* shape = obj->GetShapeAttr();
                if (shape != nullptr) {
                    cXyz centre;
                    shape->mAab.CalcCenter(&centre);
                    if ((centre - at).abs() > nearest + kSameObjectMargin) return false;
                }
            }
            inf->SetTgHit(&blow);
            inf->OnTgHitNoActor();
            cXyz where(msg.at[0], msg.at[1], msg.at[2]);
            inf->SetTgHitPos(where);
            cXyz away(actor->current.pos.x - msg.from[0], 0.0f, actor->current.pos.z - msg.from[2]);
            const f32 len = std::sqrt(away.x * away.x + away.z * away.z);
            if (len > 0.01f) {
                away.x *= 10.0f / len;
                away.z *= 10.0f / len;
            } else {
                away.set(cM_ssin(actor->shape_angle.y) * -10.0f, 0.0f,
                    cM_scos(actor->shape_angle.y) * -10.0f);
            }
            inf->SetTgRVec(away);
            cCcD_Stts* stts = obj->GetStts();
            if (stts != nullptr) stts->PlusDmg(msg.atp);
            ++landed;
            return false;
        });

        breakable_go_quiet(msg.room, msg.key);
        if (landed > 0) {
            ++s_objHitsApplied;
            coop_log::info(
                "coop_mod: [OBJ] replaying their blow on room={} key={:#010x} name={} hurtboxes={}",
                static_cast<int>(msg.room), msg.key, static_cast<int>(fopAcM_GetName(actor)),
                landed);
        }
    }
}

HookAction on_collision_move_pre(ModContext*, void*, void*, void*) {
    if (!enemies_enabled_now()) return HOOK_CONTINUE;
    disarm_retargeted_attacks();
    return HOOK_CONTINUE;
}

void on_collision_move_post(ModContext*, void*, void*, void*) {

    rearm_disarmed_attacks();
    const bool enemiesOn = enemies_enabled_now();
    const bool objectsOn = objects_live();
    if (carry_live()) {
        capture_carried();
        capture_torches();
        tick_animals();
    }
    if (!enemiesOn && !objectsOn) return;
    const bool hits = real_hits_enabled();
    EnemyList list;
    if (objectsOn) {
        BreakableList breakables;
        collect_enemies_and_breakables(list, breakables);
        if (enemiesOn && hits) capture_landed_hits(list);
        if (hits && breakables_enabled()) capture_landed_object_hits(breakables);
        if (breakables_enabled()) capture_broken_carryables(breakables);

        if (movers_enabled()) {
            capture_block_pushes(breakables);
            capture_moved_objects(breakables);
        }
    } else if (hits) {
        collect_enemies(list);
        capture_landed_hits(list);
    }
}

void run_self_test(EnemyList& list, bool host) {
    int64_t after = 0;
    if (s_selfTestVar != 0) svc_config->get_int(mod_ctx, s_selfTestVar, &after);
    if (after <= 0 || s_selfTestDone) return;
    if (++s_selfTestTicks < static_cast<uint32_t>(after)) return;
    s_selfTestTicks = 0;

    for (int i = 0; i < list.count; ++i) {
        fopAc_ac_c* actor = list.actors[i];
        if (!syncable(actor) || actor->health <= 0) continue;

        if (s_selfTestStuckKey == list.keys[i] && ++s_selfTestStuck > 2) continue;
        if (s_selfTestStuckKey != list.keys[i]) {
            s_selfTestStuckKey = list.keys[i];
            s_selfTestStuck = 0;
        }

        MsgEnemyHit msg{};
        msg.key = list.keys[i];
        msg.room = list.rooms[i];

        msg.atType = AT_TYPE_NORMAL_SWORD;
        msg.atp = 2;
        msg.spl = 0;
        msg.mtrl = 0;
        fopAc_ac_c* player = dComIfGp_getPlayer(0);
        const cXyz from = player != nullptr ? player->current.pos : actor->current.pos;
        msg.from[0] = from.x;
        msg.from[1] = from.y;
        msg.from[2] = from.z;
        msg.at[0] = actor->current.pos.x;
        msg.at[1] = actor->current.pos.y + 30.0f;
        msg.at[2] = actor->current.pos.z;

        coop_net_send(kMsgEnemyHit, &msg, sizeof(msg));
        for (int q = 0; q < kMaxPendingHits; ++q) {
            if (s_pendingHits[q].used) continue;
            s_pendingHits[q].used = true;
            s_pendingHits[q].msg = msg;
            break;
        }
        coop_log::warn(
            "coop_mod: [ENEMY-SELFTEST] *** DEBUG *** {} swinging at room={} key={:#010x} name={} hp={}",
            host ? "host" : "joiner", static_cast<int>(list.rooms[i]), list.keys[i],
            static_cast<int>(fopAcM_GetName(actor)), static_cast<int>(actor->health));
        return;
    }
    coop_log::info("coop_mod: [ENEMY-SELFTEST] nothing left to swing at (tracking {})", list.count);
}

template <class Row, int N>
void audit_one_table(const char* what, const Row (&rows)[N]) {
    int dupes = 0;
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            if (rows[i].procName != rows[j].procName) continue;
            ++dupes;
            coop_log::info("coop_mod: [ENEMY-AUDIT] {} has TWO rows for proc={:#05x} (rows {} and "
                            "{}) - the second is dead and the first may be the wrong class",
                what, rows[i].procName, i, j);
        }
    }
    coop_log::info("coop_mod: [ENEMY-AUDIT] {}: {} rows, {} duplicate procNames", what, N, dupes);
}

void audit_layout_tables() {
    audit_one_table("decisions", kEnemyAct);
    audit_one_table("animation", kEnemyAnm);
    audit_one_table("timers", kEnemyTmr);
    audit_one_table("states", kEnemyState);
}

void log_status(const EnemyList* list) {
    if (s_tick % kDiagEveryTicks != 0) return;
    const CoopPeer& peer = features_peer();
    const char* stage = dComIfGp_getStartStageName();
    daAlink_c* alink = daAlink_getAlinkActorClass();
    coop_log::info(
        "coop_mod: [ENEMY] {} stage='{}' room={} conn={} peer(present={} inGame={} stage='{:.8s}') "
        "gameplay={} here={} matched={} unmatched={} huntingOther={} blows(sent={} replayed={}) "
        "own={} myRoomOwner={} conflicts={} disarmed={} peerLive={} lonely={} instructed={}",
        coop_net_is_host() ? "host" : "joiner", stage != nullptr ? stage : "?",
        alink != nullptr ? static_cast<int>(fopAcM_GetRoomNo(alink)) : -1,
        coop_net_connected() ? 1 : 0, peer.present ? 1 : 0, peer.inGame ? 1 : 0, peer.stage,
        in_gameplay() ? 1 : 0, list != nullptr ? list->count : -1, s_diagMatched, s_diagUnmatched,
        s_retargetedThisTick, s_hitsSent, s_hitsApplied, s_diagOwned,
        (alink != nullptr && fopAcM_GetRoomNo(alink) >= 0 && fopAcM_GetRoomNo(alink) < kRooms)
            ? static_cast<int>(s_rooms.owner[fopAcM_GetRoomNo(alink)])
            : -1,
        s_diagConflicts, s_diagDisarmed, peer_owner_is_live() ? 1 : 0, s_diagLonely,
        s_diagInstructed);

    coop_log::info("coop_mod: [OBJ] on={} objects={} blows(sent={} replayed={} lost={}) "
                    "moving={} moves(sent={} received={} applied={}) pushes(sent={} applied={})",
        breakables_enabled() ? 1 : 0, s_diagBreakables, s_objHitsSent, s_objHitsApplied,
        s_objHitsLost, s_diagMovers, s_movesSent, s_movesReceived, s_movesApplied, s_pushesSent,
        s_pushesApplied);

    char worlds[128];
    int at = 0;
    for (int i = 0; i < kCoopMaxPlayers && at < static_cast<int>(sizeof(worlds)) - 24; ++i) {
        if (i != coop_net_local_id() && !coop_net_player_present(static_cast<uint8_t>(i))) continue;
        at += std::snprintf(worlds + at, sizeof(worlds) - at, "%s p%d:still=%u%s",
                            at ? " " : "", i,
                            static_cast<unsigned>(coop_ticks_since_world(static_cast<uint8_t>(i))),
                            coop_world_stalled(static_cast<uint8_t>(i)) ? "(PAUSED)" : "");
    }
    worlds[sizeof(worlds) - 1] = 0;
    coop_log::info("coop_mod: [ENEMY-WORLD] ourFrames={} {}", coop_local_world_frames(), worlds);
}

}

void enemies_register_vars() {
    ConfigVarDesc enable = CONFIG_VAR_DESC_INIT;
    enable.name = "sync_enemies";
    enable.type = CONFIG_VAR_BOOL;
    enable.default_bool = false;
    if (svc_config->register_var(mod_ctx, &enable, &s_enableVar) != MOD_OK) s_enableVar = 0;

    ConfigVarDesc positions = CONFIG_VAR_DESC_INIT;
    positions.name = "sync_enemy_positions";
    positions.type = CONFIG_VAR_BOOL;
    positions.default_bool = true;
    if (svc_config->register_var(mod_ctx, &positions, &s_positionsVar) != MOD_OK) s_positionsVar = 0;

    ConfigVarDesc decisions = CONFIG_VAR_DESC_INIT;
    decisions.name = "sync_enemy_decisions";
    decisions.type = CONFIG_VAR_BOOL;
    decisions.default_bool = true;
    if (svc_config->register_var(mod_ctx, &decisions, &s_decisionsVar) != MOD_OK) {
        s_decisionsVar = 0;
    }

    ConfigVarDesc target = CONFIG_VAR_DESC_INIT;
    target.name = "enemies_target_both";
    target.type = CONFIG_VAR_BOOL;
    target.default_bool = true;
    if (svc_config->register_var(mod_ctx, &target, &s_targetVar) != MOD_OK) s_targetVar = 0;

    ConfigVarDesc roomOwner = CONFIG_VAR_DESC_INIT;
    roomOwner.name = "enemies_room_ownership";
    roomOwner.type = CONFIG_VAR_BOOL;
    roomOwner.default_bool = true;
    if (svc_config->register_var(mod_ctx, &roomOwner, &s_roomOwnerVar) != MOD_OK) s_roomOwnerVar = 0;

    ConfigVarDesc realHits = CONFIG_VAR_DESC_INIT;
    realHits.name = "enemies_share_hits";
    realHits.type = CONFIG_VAR_BOOL;
    realHits.default_bool = true;
    if (svc_config->register_var(mod_ctx, &realHits, &s_realHitsVar) != MOD_OK) s_realHitsVar = 0;

    ConfigVarDesc breakables = CONFIG_VAR_DESC_INIT;
    breakables.name = "sync_breakables";
    breakables.type = CONFIG_VAR_BOOL;
    breakables.default_bool = true;
    if (svc_config->register_var(mod_ctx, &breakables, &s_breakablesVar) != MOD_OK) {
        s_breakablesVar = 0;
    }

    ConfigVarDesc movers = CONFIG_VAR_DESC_INIT;
    movers.name = "sync_pushed_objects";
    movers.type = CONFIG_VAR_BOOL;
    movers.default_bool = true;
    if (svc_config->register_var(mod_ctx, &movers, &s_moversVar) != MOD_OK) s_moversVar = 0;

    ConfigVarDesc selfTest = CONFIG_VAR_DESC_INIT;
    selfTest.name = "debug_enemy_selftest_ticks";
    selfTest.type = CONFIG_VAR_INT;
    selfTest.default_int = 0;
    if (svc_config->register_var(mod_ctx, &selfTest, &s_selfTestVar) != MOD_OK) s_selfTestVar = 0;
}

ConfigVarHandle enemies_decisions_var() {
    return s_decisionsVar;
}

ConfigVarHandle enemies_enabled_var() {
    return s_enableVar;
}

uint8_t enemies_room_owner_player(int room) {
    if (room < 0 || room >= kRooms) return kCoopNoPlayer;
    if (!cfg_bool(s_roomOwnerVar, true)) return kCoopNoPlayer;
    char stage[8];
    int saveNo = -1;

    if (!room_stage(stage, saveNo) || std::memcmp(stage, s_rooms.stage, 8) != 0) {
        return kCoopNoPlayer;
    }
    return player_of_owner(s_rooms.owner[room]);
}

ConfigVarHandle enemies_breakables_var() {
    return s_breakablesVar;
}

ConfigVarHandle enemies_movers_var() {
    return s_moversVar;
}

void warn_if_debug_armed() {
    int64_t enemyTicks = 0;
    if (s_selfTestVar != 0) svc_config->get_int(mod_ctx, s_selfTestVar, &enemyTicks);
    if (enemyTicks == 0) return;
    coop_log::warn(
        "coop_mod: *** DEBUG SELF-TEST IS ARMED *** debug_enemy_selftest_ticks={} - this game will "
        "attack its own enemies every {} ticks and tell the other player to do the same. Set it to "
        "0 (or launch with play.ps1) unless you are running the automated test.",
        enemyTicks, enemyTicks);
}

uint32_t coop_local_world_frames() {
    return s_worldFrames;
}

void enemies_init() {
    const ModResult pre = mods::hook::add_pre<EnemyExecuteHook>(on_proc_execute_pre);
    const ModResult post = mods::hook::add_post<EnemyExecuteHook>(on_proc_execute_post);
    const ModResult ccPre = mods::hook::add_pre<EnemyCollisionHook>(on_collision_move_pre);
    const ModResult ccPost = mods::hook::add_post<EnemyCollisionHook>(on_collision_move_post);
    const ModResult statue = mods::hook::add_pre<CoopStatueSetAnimeHook>(on_statue_set_anime);
    const ModResult small =
        mods::hook::add_pre<CoopSmallStatueSetAnimeHook>(on_small_statue_set_anime);
    coop_log::info("coop_mod: [CARRY] statue look hooks: big={} small={}", static_cast<int>(statue),
        static_cast<int>(small));
    coop_log::info("coop_mod: [ENEMY] hooks: retargetPre={} retargetPost={} ccPre={} ccPost={}",
        static_cast<int>(pre), static_cast<int>(post), static_cast<int>(ccPre),
        static_cast<int>(ccPost));
    warn_if_debug_armed();
    log_anm_table();
    audit_layout_tables();
}

void enemies_on_connected() {
    reset_tables();
    reset_rooms();
}

void enemies_on_disconnected() {
    reset_tables();
    reset_rooms();
}

void enemies_on_local_unpause() {
    s_unpauseSnapTicks = kUnpauseSnapTicks;
}

void enemies_update() {
    ++s_tick;
    if (s_unpauseSnapTicks > 0) --s_unpauseSnapTicks;
    note_loaded_rooms(dComIfGs_getSaveInfo());
    if (s_settleTicks > 0) --s_settleTicks;
    if (dComIfGp_event_runCheck()) s_eventSettleTicks = kEventSettleTicks;
    else if (s_eventSettleTicks > 0) --s_eventSettleTicks;

    if (!enemies_enabled_now()) {
        if (tables_busy()) reset_tables();

        s_retarget.count = 0;
        s_retargetedThisTick = 0;
        s_diagDisarmed = 0;

        if (claims_live()) {
            daAlink_c* me = daAlink_getAlinkActorClass();
            update_room_claims(me != nullptr ? static_cast<int>(fopAcM_GetRoomNo(me)) : -1);
        }
        if (objects_live()) {
            age_breakable_quiet();
            if (real_hits_enabled() && breakables_enabled()) inject_pending_object_hits();
            if (movers_enabled()) {
                apply_pending_pushes();
                apply_pending_moves();
            }
        }
        if (carry_live() || carry_gone_live()) {
            apply_remote_carry();
        } else {
            reset_carry();
        }
        log_status(nullptr);
        return;
    }

    EnemyList list;
    collect_enemies(list);

    const bool host = coop_net_is_host();
    daAlink_c* alink = daAlink_getAlinkActorClass();
    update_room_claims(alink != nullptr ? static_cast<int>(fopAcM_GetRoomNo(alink)) : -1);
    sweep(list, host);

    apply_remote(list);
    if (real_hits_enabled()) inject_pending_hits(list);

    age_breakable_quiet();
    if (real_hits_enabled() && breakables_enabled()) inject_pending_object_hits();
    if (movers_enabled()) {
        apply_pending_pushes();
        apply_pending_moves();
    }

    if (carry_gone_live()) apply_remote_carry();
    run_self_test(list, host);

    decide_targets(list);
    advance_blends(list);

    owner_finish_kills(list);
    if (s_tick % kSendEveryTicks == 0) send_state(list);
    (void)host;

    log_status(&list);
}

void enemies_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from) {

    if (type == kMsgRoomClaim || type == kMsgRoomOwner) {
        if (!enemies_setting_on() && !bosses_setting_on()) return;
    } else if (type == kMsgObjectHit || type == kMsgObjectMove || type == kMsgCarry ||
               type == kMsgTorch || type == kMsgAnimal) {
        if (!breakables_enabled() && !movers_enabled()) return;
    } else if (!enemies_setting_on()) {
        return;
    }
    switch (type) {
    case kMsgEnemyState: {
        if (size < 1) return;
        const int count = payload[0];
        if (size < 1 + count * sizeof(MsgEnemyEntry)) return;
        for (int i = 0; i < count; ++i) {
            MsgEnemyEntry entry;
            std::memcpy(&entry, payload + 1 + i * sizeof(MsgEnemyEntry), sizeof(entry));
            if (we_own(entry.room, entry.key)) {
                ++s_diagConflicts;
                continue;
            }
            Remote* r = find_or_add_remote(entry.room, entry.key);
            if (r == nullptr) continue;
            if (r->gone) continue;
            r->procName = entry.procName;
            r->x = entry.pos[0];
            r->y = entry.pos[1];
            r->z = entry.pos[2];
            r->angle[0] = entry.angle[0];
            r->angle[1] = entry.angle[1];
            r->angle[2] = entry.angle[2];

            const uint32_t gap = s_tick - r->stamp;
            if (r->stamp != 0 && gap > 0 && gap <= 8) {
                const f32 inv = 1.0f / static_cast<f32>(gap);
                r->vx = (entry.pos[0] - r->x) * inv;
                r->vy = (entry.pos[1] - r->y) * inv;
                r->vz = (entry.pos[2] - r->z) * inv;
                r->haveVel = true;
            } else {
                r->haveVel = false;
            }
            r->owner = from;
            r->health = entry.health;
            r->flags = entry.flags;
            r->anmId = entry.anmId;
            r->anmFrame = entry.anmFrame;
            r->anmRate = entry.anmRate;
            r->anmMode = entry.anmMode;
            r->action = entry.action;
            r->mode = entry.mode;
            for (int k = 0; k < 5; ++k) r->timers[k] = entry.timers[k];
            r->timerCount = entry.timerCount;
            r->extraState = entry.extraState;
            r->stamp = s_tick;
        }
        break;
    }
    case kMsgEnemyGone: {
        if (size < sizeof(MsgEnemyGone)) return;
        MsgEnemyGone msg;
        std::memcpy(&msg, payload, sizeof(msg));
        Remote* r = find_or_add_remote(msg.room, msg.key);
        if (r == nullptr) return;
        r->gone = true;
        r->stamp = s_tick;
        coop_log::info("coop_mod: [ENEMY] the other player killed room={} key={:#010x}",
            static_cast<int>(msg.room), msg.key);
        break;
    }
    case kMsgEnemyTargets: {

        if (from != kCoopHostId) return;

        if (coop_net_is_host() || size < 1) return;
        const int count = payload[0];
        if (size < 1 + count * sizeof(MsgEnemyTarget)) return;
        for (int i = 0; i < count; ++i) {
            MsgEnemyTarget entry;
            std::memcpy(&entry, payload + 1 + i * sizeof(entry), sizeof(entry));
            Tracked* t = find_tracked(entry.room, entry.key);
            if (t == nullptr) continue;
            t->targetPlayer = entry.targetPlayer;
            t->targetStamp = s_tick;
            t->targetKnown = true;
        }
        break;
    }
    case kMsgRoomClaim: {

        if (!coop_net_is_host() || size < sizeof(MsgRoomClaim)) return;
        MsgRoomClaim msg;
        std::memcpy(&msg, payload, sizeof(msg));
        if (std::memcmp(msg.stage, s_rooms.stage, 8) != 0 || msg.saveNo != s_rooms.saveNo) return;
        if (msg.room < 0 || msg.room >= kRooms) return;

        if (s_rooms.owner[msg.room] == kRoomOwnerNone && !coop_player_paused(from)) {
            set_room_owner(msg.room, owner_of(from));
        }
        MsgRoomOwner reply{};
        std::memcpy(reply.stage, msg.stage, 8);
        reply.saveNo = msg.saveNo;
        reply.room = msg.room;
        reply.owner = s_rooms.owner[msg.room];
        coop_net_send(kMsgRoomOwner, &reply, sizeof(reply));
        break;
    }
    case kMsgRoomOwner: {

        if (coop_net_is_host() || from != kCoopHostId || size < sizeof(MsgRoomOwner)) return;
        MsgRoomOwner msg;
        std::memcpy(&msg, payload, sizeof(msg));
        if (std::memcmp(msg.stage, s_rooms.stage, 8) != 0 || msg.saveNo != s_rooms.saveNo) return;
        if (msg.room < 0 || msg.room >= kRooms) return;
        set_room_owner(msg.room, msg.owner);
        break;
    }
    case kMsgEnemyHit: {
        if (size < sizeof(MsgEnemyHit) || !real_hits_enabled()) return;
        MsgEnemyHit msg;
        std::memcpy(&msg, payload, sizeof(msg));
        for (int i = 0; i < kMaxPendingHits; ++i) {
            if (s_pendingHits[i].used) continue;
            s_pendingHits[i].used = true;
            s_pendingHits[i].msg = msg;
            return;
        }
        coop_log::info("coop_mod: [ENEMY] dropped a relayed blow - {} already queued",
            kMaxPendingHits);
        break;
    }
    case kMsgObjectHit: {
        if (size < sizeof(MsgEnemyHit) || !real_hits_enabled() || !breakables_enabled()) return;
        MsgEnemyHit msg;
        std::memcpy(&msg, payload, sizeof(msg));
        for (int i = 0; i < kMaxPendingObjectHits; ++i) {
            if (s_pendingObjectHits[i].used) continue;
            s_pendingObjectHits[i].used = true;
            s_pendingObjectHits[i].msg = msg;
            return;
        }
        coop_log::info("coop_mod: [OBJ] dropped a relayed blow - {} already queued",
            kMaxPendingObjectHits);
        break;
    }
    case kMsgObjectPush: {
        if (size < sizeof(MsgObjectPush) || !movers_enabled()) return;
        MsgObjectPush msg;
        std::memcpy(&msg, payload, sizeof(msg));
        for (PendingPush& pending : s_pendingPushes) {
            if (pending.used && pending.msg.key == msg.key && pending.msg.room == msg.room) {
                pending.msg = msg;
                return;
            }
        }
        for (PendingPush& pending : s_pendingPushes) {
            if (!pending.used) {
                pending.used = true;
                pending.msg = msg;
                return;
            }
        }
        return;
    }
    case kMsgCarry: {
        if (size < sizeof(MsgCarry)) return;
        MsgCarry msg;
        std::memcpy(&msg, payload, sizeof(msg));
        carry_on_message(msg, from);
        return;
    }
    case kMsgTorch: {
        if (size < sizeof(MsgTorch) || !carry_live()) return;
        MsgTorch msg;
        std::memcpy(&msg, payload, sizeof(msg));
        torch_on_message(msg);
        return;
    }
    case kMsgAnimal: {
        if (size < sizeof(MsgAnimal) || !carry_live()) return;
        MsgAnimal msg;
        std::memcpy(&msg, payload, sizeof(msg));
        animal_on_message(msg);
        return;
    }
    case kMsgObjectMove: {
        if (size < sizeof(MsgObjectMove) || !movers_enabled()) return;
        MsgObjectMove msg;
        std::memcpy(&msg, payload, sizeof(msg));
        ++s_movesReceived;

        for (int i = 0; i < kMaxMovers; ++i) {
            if (s_pendingMoves[i].used && s_pendingMoves[i].msg.key == msg.key &&
                s_pendingMoves[i].msg.room == msg.room)
            {
                s_pendingMoves[i].msg = msg;
                return;
            }
        }
        for (int i = 0; i < kMaxMovers; ++i) {
            if (s_pendingMoves[i].used) continue;
            s_pendingMoves[i].used = true;
            s_pendingMoves[i].msg = msg;
            return;
        }
        break;
    }
    case kMsgEnemyDamage: {
        if (size < sizeof(MsgEnemyDamage)) return;
        MsgEnemyDamage msg;
        std::memcpy(&msg, payload, sizeof(msg));
        if (msg.amount <= 0 || msg.amount > kMaxRelayedDamage) return;
        EnemyList list;
        collect_enemies(list);
        fopAc_ac_c* actor = find_local(list, msg.room, msg.key);
        if (actor == nullptr) return;
        apply_damage_amount(actor, msg.room, msg.key, msg.amount);
        coop_log::info("coop_mod: [ENEMY] the other player hit room={} key={:#010x} for {} (now {})",
            static_cast<int>(msg.room), msg.key, static_cast<int>(msg.amount),
            static_cast<int>(actor->health));
        break;
    }
    default:
        break;
    }
}

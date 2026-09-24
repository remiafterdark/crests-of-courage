

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "SSystem/SComponent/c_lib.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_boomerang.h"
#include "d/actor/d_a_nbomb.h"
#include "d/d_bomb.h"
#include "d/d_resorce.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"

#include <cmath>
#include <cstring>

DEFINE_HOOK((static_cast<fopAc_ac_c* (*)(s16, u32, const cXyz*, int, const csXyz*, const cXyz*, s8,
                createFunc, void*, u32, u8)>(&fopAcM_fastCreate)),
    FastCreateHook);

DEFINE_HOOK_SYMBOL("src/d/actor/d_a_nbomb.cpp#daNbomb_createHeap", int(fopAc_ac_c*),
    BombCreateHeapHook);

namespace {

const int kMaxSpawns = kCoopMaxPlayers * 6;
const int kSendEveryTicks = 2;
const int kNoId = -1;

ConfigVarHandle s_enableVar = 0;
ConfigVarHandle s_selfTestVar = 0;

ConfigVarHandle s_noHookVar = 0;
uint32_t s_tick = 0;
uint32_t s_nextNetId = 1;
int s_diagOwned = 0;
int s_diagReplicas = 0;

int s_creatingReplica = 0;

uint8_t s_replicaBombKind = 0xFF;

const int kAlinkBombResIdx = 0x1E;

void write_roll(const Mtx m, int16_t* q) {
    f32 x, y, z, w;
    const f32 tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        const f32 s = std::sqrt(tr + 1.0f) * 2.0f;
        w = 0.25f * s;
        x = (m[2][1] - m[1][2]) / s;
        y = (m[0][2] - m[2][0]) / s;
        z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const f32 s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        w = (m[2][1] - m[1][2]) / s;
        x = 0.25f * s;
        y = (m[0][1] + m[1][0]) / s;
        z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        const f32 s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        w = (m[0][2] - m[2][0]) / s;
        x = (m[0][1] + m[1][0]) / s;
        y = 0.25f * s;
        z = (m[1][2] + m[2][1]) / s;
    } else {
        const f32 s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        w = (m[1][0] - m[0][1]) / s;
        x = (m[0][2] + m[2][0]) / s;
        y = (m[1][2] + m[2][1]) / s;
        z = 0.25f * s;
    }
    const f32 v[4] = {x, y, z, w};
    for (int k = 0; k < 4; ++k) {
        const f32 c = v[k] > 1.0f ? 1.0f : (v[k] < -1.0f ? -1.0f : v[k]);
        q[k] = static_cast<int16_t>(c * 32767.0f);
    }
}

void read_roll(const int16_t* q, Mtx m) {
    if (q[0] == 0 && q[1] == 0 && q[2] == 0 && q[3] == 0) return;
    f32 x = q[0] / 32767.0f, y = q[1] / 32767.0f, z = q[2] / 32767.0f, w = q[3] / 32767.0f;
    const f32 len = std::sqrt(x * x + y * y + z * z + w * w);
    if (len < 0.0001f) return;
    x /= len;
    y /= len;
    z /= len;
    w /= len;
    m[0][0] = 1.0f - 2.0f * (y * y + z * z);
    m[0][1] = 2.0f * (x * y - z * w);
    m[0][2] = 2.0f * (x * z + y * w);
    m[1][0] = 2.0f * (x * y + z * w);
    m[1][1] = 1.0f - 2.0f * (x * x + z * z);
    m[1][2] = 2.0f * (y * z - x * w);
    m[2][0] = 2.0f * (x * z - y * w);
    m[2][1] = 2.0f * (y * z + x * w);
    m[2][2] = 1.0f - 2.0f * (x * x + y * y);
    m[0][3] = m[1][3] = m[2][3] = 0.0f;
}

bool bomb_model_available() {
    return dComIfG_getObjectRes(daAlink_c::getAlinkArcName(),
               kAlinkBombResIdx) != nullptr;
}

bool alink_arc_resident() {
    dRes_info_c* info = dComIfG_getObjectResInfo(daAlink_c::getAlinkArcName());
    if (info == nullptr) return false;
    JKRArchive* archive = info->getArchive();
    if (archive == nullptr) return false;

    return JKRGetNameResource("zelda_v_cursor_new_yellow.blo", archive) != nullptr &&
           JKRGetNameResource("zelda_v_cursor_new_yellow.bpk", archive) != nullptr;
}

void log_bomb_res_once() {
    static bool logged = false;
    if (logged) return;
    logged = true;
    void* res = dComIfG_getObjectRes(daAlink_c::getAlinkArcName(), kAlinkBombResIdx);
    coop_log::info("coop_mod: [SPAWN] bomb model res (arc='{}' idx={:#x}) = {}",
        daAlink_c::getAlinkArcName(), static_cast<int>(kAlinkBombResIdx),
        res != nullptr ? "present" : "NULL");
}

bool procname_is_replicated(s16 name) {
    return name == fpcNm_NBOMB_e || name == fpcNm_BOOMERANG_e;
}

bool should_replicate(s16 name, u32 bornParam, u32 liveParam) {
    if (!procname_is_replicated(name)) return false;
    if (name == fpcNm_BOOMERANG_e) return liveParam != 0;
    const u32 param = bornParam;
    if (name == fpcNm_NBOMB_e && param == static_cast<u32>(dBomb_c::PRM_ENEMY_BOMB_BOOMERANG)) {
        return false;
    }

    if (name == fpcNm_NBOMB_e && (param == static_cast<u32>(dBomb_c::PRM_NORMAL_BOMB_EXPLODE) ||
                                  param == static_cast<u32>(dBomb_c::PRM_WATER_BOMB_EXPLODE))) {
        return false;
    }

    if (name == fpcNm_BOOMERANG_e && param == 0) return false;
    return true;
}

uint8_t read_actor_kind(fopAc_ac_c* actor, s16 name) {
    if (actor == nullptr || name != fpcNm_NBOMB_e) return 0;
    return static_cast<daNbomb_c*>(actor)->mType;
}

void write_actor_kind(fopAc_ac_c* actor, s16 name, uint8_t kind) {
    if (actor == nullptr || name != fpcNm_NBOMB_e) return;
    auto* bomb = static_cast<daNbomb_c*>(actor);
    if (kind == daNbomb_c::TYPE_WATER_PLAYER && bomb->mType == daNbomb_c::TYPE_WATER_PLAYER) {
        bomb->onStateFlg0(daNbomb_c::FLG0_WATER_BOMB);
    }
}

u32 create_param_for_kind(s16 name, u32 param, uint8_t kind) {
    if (name == fpcNm_NBOMB_e && kind == daNbomb_c::TYPE_INSECT_PLAYER) {
        return static_cast<u32>(dBomb_c::PRM_INSECT_BOMB_PLAYER);
    }
    return param;
}

HookAction on_bomb_create_heap_pre(ModContext*, void* args, void*, void*) {
    if (s_replicaBombKind != daNbomb_c::TYPE_WATER_PLAYER) return HOOK_CONTINUE;
    auto* bomb = static_cast<daNbomb_c*>(mods::arg<fopAc_ac_c*>(args, 0));
    if (bomb != nullptr && bomb->mType == daNbomb_c::TYPE_NORMAL_PLAYER) {
        bomb->mType = daNbomb_c::TYPE_WATER_PLAYER;
    }
    return HOOK_CONTINUE;
}

int16_t read_actor_state(fopAc_ac_c* actor, s16 name) {
    if (actor == nullptr) return 0;
    if (name == fpcNm_NBOMB_e) return static_cast<daNbomb_c*>(actor)->mExTime;
    return 0;
}

void write_actor_state(fopAc_ac_c* actor, s16 name, int16_t state) {
    if (actor == nullptr) return;
    if (name == fpcNm_NBOMB_e) {

        static_cast<daNbomb_c*>(actor)->mExTime = state > 0 ? state : 1;
    }
}

bool param_assumes_a_grabber(s16 name, u32 param) {
    if (name != fpcNm_NBOMB_e) return false;
    switch (param) {
    case dBomb_c::PRM_BOMB_CARRY:
    case dBomb_c::PRM_NORMAL_BOMB_PLAYER:
    case dBomb_c::PRM_WATER_BOMB_PLAYER:
    case dBomb_c::PRM_INSECT_BOMB_PLAYER:
    case dBomb_c::PRM_BOMB_CARGO_CARRY:
        return true;
    default:
        return false;
    }
}

bool gone_removes_replica(s16 name) {
    switch (name) {
    case fpcNm_NBOMB_e:
        return false;
    default:
        return true;
    }
}

struct Owned {
    bool used = false;
    uint32_t netId = 0;
    fpc_ProcID id = 0;
    s16 procName = 0;
};
Owned s_owned[kMaxSpawns];

struct Candidate {
    bool used = false;
    fpc_ProcID id = 0;
    s16 procName = 0;
    u32 bornParam = 0;
    int age = 0;
};
Candidate s_candidate[kMaxSpawns];

const uint32_t kReplicaStaleTicks = 60 * 20;
const int kCandidateMaxAge = 60 * 60;

struct Replica {
    bool used = false;
    uint32_t netId = 0;
    fpc_ProcID id = 0;
    s16 procName = 0;
    bool havePos = false;
    cXyz pos;
    csXyz angle;
    uint32_t stamp = 0;

    bool haveVel = false;
    cXyz vel;
    uint32_t velTick = 0;

    uint8_t settling = 0;

    bool ownerInCutscene = false;
};
Replica s_replica[kMaxSpawns];

bool spawns_enabled() {
    return coop_net_connected() && cfg_bool(s_enableVar, true);
}

bool in_gameplay() {
    return daAlink_getAlinkActorClass() != nullptr && !dComIfGp_event_runCheck() &&
           !dComIfGp_isEnableNextStage();
}

bool is_replica(fpc_ProcID id) {
    for (int i = 0; i < kMaxSpawns; ++i) {
        if (s_replica[i].used && s_replica[i].id == id) return true;
    }
    return false;
}

Replica* find_replica(uint32_t netId) {
    for (int i = 0; i < kMaxSpawns; ++i) {
        if (s_replica[i].used && s_replica[i].netId == netId) return &s_replica[i];
    }
    return nullptr;
}

Owned* find_owned(uint32_t netId) {
    for (int i = 0; i < kMaxSpawns; ++i) {
        if (s_owned[i].used && s_owned[i].netId == netId) return &s_owned[i];
    }
    return nullptr;
}

void reset_tables() {
    for (int i = 0; i < kMaxSpawns; ++i) {
        s_owned[i] = Owned{};
        s_replica[i] = Replica{};
        s_candidate[i] = Candidate{};
    }
    s_diagOwned = 0;
    s_diagReplicas = 0;
}

void on_local_spawn(fopAc_ac_c* actor, s16 procName, u32 param) {
    if (actor == nullptr) return;
    for (int i = 0; i < kMaxSpawns; ++i) {
        if (s_candidate[i].used) continue;
        s_candidate[i].used = true;
        s_candidate[i].id = fopAcM_GetID(actor);
        s_candidate[i].procName = procName;
        s_candidate[i].bornParam = param;
        s_candidate[i].age = 0;
        return;
    }
}

bool still_held(fopAc_ac_c* actor) {

    if (fopAcM_GetName(actor) == fpcNm_BOOMERANG_e && fopAcM_GetParam(actor) == 0) return true;
    if (fopAcM_checkCarryNow(actor) != 0) return true;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    return alink != nullptr && alink->getGrabActorID() == fopAcM_GetID(actor);
}

void announce(fpc_ProcID id, fopAc_ac_c* actor, s16 procName, u32 bornParam) {
    int slot = -1;
    for (int i = 0; i < kMaxSpawns; ++i) {
        if (!s_owned[i].used) { slot = i; break; }
    }
    if (slot < 0) return;

    const uint8_t mintBy = coop_net_local_id();
    if (mintBy >= kCoopMaxPlayers) return;
    const uint32_t netId =
        (static_cast<uint32_t>(mintBy) << 28) | (s_nextNetId++ & 0x0FFFFFFFu);
    s_owned[slot].used = true;
    s_owned[slot].netId = netId;
    s_owned[slot].id = id;
    s_owned[slot].procName = procName;

    MsgActorSpawn msg{};
    msg.netId = netId;
    msg.procName = procName;

    const u32 livePrm = fopAcM_GetParam(actor);
    if (!should_replicate(procName, bornParam, livePrm)) return;
    msg.param = param_assumes_a_grabber(procName, livePrm)
                    ? static_cast<uint32_t>(dBomb_c::PRM_BOMB_WAIT)
                    : livePrm;
    msg.pos[0] = actor->current.pos.x;
    msg.pos[1] = actor->current.pos.y;
    msg.pos[2] = actor->current.pos.z;
    msg.angle[0] = actor->current.angle.x;
    msg.angle[1] = actor->current.angle.y;
    msg.angle[2] = actor->current.angle.z;
    msg.room = fopAcM_GetRoomNo(actor);
    msg.state = read_actor_state(actor, procName);
    msg.kind = read_actor_kind(actor, procName);
    coop_net_send(kMsgActorSpawn, &msg, sizeof(msg));
    coop_log::info("coop_mod: [SPAWN] ours netId={} name={:#x} let go of - telling the other player",
        netId, static_cast<int>(procName));
}

void promote_candidates() {
    for (int i = 0; i < kMaxSpawns; ++i) {
        Candidate& c = s_candidate[i];
        if (!c.used) continue;
        auto* actor = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(c.id));
        if (actor == nullptr || ++c.age > kCandidateMaxAge) {
            c = Candidate{};
            continue;
        }
        if (still_held(actor)) continue;
        announce(c.id, actor, c.procName, c.bornParam);
        c = Candidate{};
    }
}

HookAction on_fast_create_pre(ModContext*, void*, void*, void*) {
    return HOOK_CONTINUE;
}

void on_fast_create_post(ModContext*, void* args, void* retval, void*) {
    if (s_creatingReplica > 0) return;

    if (!spawns_enabled() || !in_gameplay() || !peer_on_our_stage()) return;
    const s16 procName = mods::arg<s16>(args, 0);
    if (!procname_is_replicated(procName)) return;
    auto* actor = *static_cast<fopAc_ac_c**>(retval);
    if (actor == nullptr) return;
    on_local_spawn(actor, procName, mods::arg<u32>(args, 1));
}

bool we_own_a_boomerang() {
    for (int i = 0; i < kMaxSpawns; ++i) {
        if (s_owned[i].used && s_owned[i].procName == fpcNm_BOOMERANG_e) return true;
    }
    return false;
}

void send_transforms() {
    daAlink_c* alinkTx = daAlink_getAlinkActorClass();

    const bool localInCutsceneTx =
        alinkTx != nullptr && (alinkTx->checkEventRun() != FALSE || boss_local_demo_running());
    uint8_t buffer[1 + kMaxSpawns * sizeof(MsgActorState)];
    int count = 0;
    for (int i = 0; i < kMaxSpawns; ++i) {
        if (!s_owned[i].used) continue;
        auto* actor = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(s_owned[i].id));

        bool returned = false;
        if (actor != nullptr && s_owned[i].procName == fpcNm_BOOMERANG_e &&
            fopAcM_GetParam(actor) == 0) {
            daAlink_c* me = daAlink_getAlinkActorClass();
            if (me == nullptr) {
                returned = true;
            } else {
                const f32 dx = actor->current.pos.x - me->current.pos.x;
                const f32 dy = actor->current.pos.y - me->current.pos.y;
                const f32 dz = actor->current.pos.z - me->current.pos.z;
                returned = (dx * dx + dy * dy + dz * dz) < (220.0f * 220.0f);
            }
        }
        if (actor == nullptr || returned) {

            MsgActorGone gone{};
            gone.netId = s_owned[i].netId;
            coop_net_send(kMsgActorGone, &gone, sizeof(gone));
            coop_log::info("coop_mod: [SPAWN] ours netId={} is gone", s_owned[i].netId);
            s_owned[i] = Owned{};
            continue;
        }
        MsgActorState st{};
        st.netId = s_owned[i].netId;
        st.pos[0] = actor->current.pos.x;
        st.pos[1] = actor->current.pos.y;
        st.pos[2] = actor->current.pos.z;
        st.angle[0] = actor->shape_angle.x;
        st.angle[1] = actor->shape_angle.y;
        st.angle[2] = actor->shape_angle.z;
        st.flags = 0;
        if (fopAcM_checkCarryNow(actor) != 0) st.flags |= kActorFlagCarried;
        if ((actor->actor_status & fopAcStts_NODRAW_e) != 0) st.flags |= kActorFlagHidden;

        if (localInCutsceneTx) st.flags |= kActorFlagOwnerInCutscene;
        st.state = read_actor_state(actor, s_owned[i].procName);
        st.param = fopAcM_GetParam(actor);
        if (s_owned[i].procName == fpcNm_NBOMB_e) {
            write_roll(static_cast<daNbomb_c*>(actor)->field_0xa40, st.roll);
        }
        std::memcpy(buffer + 1 + count * sizeof(st), &st, sizeof(st));
        ++count;
    }
    s_diagOwned = count;
    if (count == 0) return;
    buffer[0] = static_cast<uint8_t>(count);
    coop_net_send(kMsgActorState, buffer, 1 + count * sizeof(MsgActorState));
}

const uint8_t kReplicaSettleTicks = 2;

void hide_for_first_frame(fopAc_ac_c* actor) {
    fopAcM_OnStatus(actor, fopAcStts_NODRAW_e);
}

void update_replica_visibility(fopAc_ac_c* actor, Replica& r, bool localInCutscene) {
    if (r.settling > 0) --r.settling;
    const bool hidden = r.settling > 0 || localInCutscene || r.ownerInCutscene;
    const bool isHidden = (actor->actor_status & fopAcStts_NODRAW_e) != 0;
    if (hidden == isHidden) return;
    if (hidden) {
        fopAcM_OnStatus(actor, fopAcStts_NODRAW_e);
    } else {
        fopAcM_OffStatus(actor, fopAcStts_NODRAW_e);
    }
}

void neutralise_boomerang(daBoomerang_c* boom, const csXyz& angle) {
    if (boom == nullptr) return;

    boom->field_0x988 = 0.0f;
    boom->speedF = 0.0f;
    boom->offStateFlg0(daBoomerang_c::FLG0_80);

    boom->field_0x957 = 0;

    boom->old.pos = boom->current.pos;

    boom->current.angle.x = angle.x;
    boom->current.angle.y = angle.y;

    boom->offStateFlg0(daBoomerang_c::FLG0_40);
}

const int kMaxRiddenBombs = kCoopMaxPlayers;

struct RiddenBomb {
    bool used = false;
    fpc_ProcID id = 0;
    uint32_t boomNetId = 0;
    s16 angle = 0;
    f32 offsetXZ = 0.0f;
    f32 offsetY = 0.0f;
};
RiddenBomb s_ridden[kMaxRiddenBombs];

const s16 kRiddenRotStep = 0x1C00;

void daNbomb_ridden_coHitCallback(fopAc_ac_c* i_coActorA, dCcD_GObjInf*, fopAc_ac_c* i_coActorB,
                                  dCcD_GObjInf*) {

    if (i_coActorA == nullptr || i_coActorB == nullptr) return;
    static_cast<daNbomb_c*>(i_coActorA)->coHitCallback(i_coActorB);
}

void rearm_ridden_bomb(fopAc_ac_c* bomb) {
    auto* nb = static_cast<daNbomb_c*>(bomb);

    if (fopAcM_GetParam(bomb) != static_cast<u32>(dBomb_c::PRM_ENEMY_BOMB_BOOMERANG_MOVE)) {
        fopAcM_SetParam(bomb, dBomb_c::PRM_ENEMY_BOMB_BOOMERANG_MOVE);
    }

    nb->mCcSph.OnCoSetBit();
    nb->mCcSph.SetCoHitCallback(daNbomb_ridden_coHitCallback);

    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink != nullptr) {
        const s16 base = alink->getBombExplodeTime();
        if (nb->mExTime < base) nb->mExTime = static_cast<s16>(base * 1.5f);
    }
}

bool param_is_boomerang_carry(u32 param) {
    return param == static_cast<u32>(dBomb_c::PRM_ENEMY_BOMB_BOOMERANG) ||
           param == static_cast<u32>(dBomb_c::PRM_ENEMY_BOMB_BOOMERANG_MOVE) ||
           param == static_cast<u32>(dBomb_c::PRM_BOMB_BOOMERANG_MOVE);
}

struct CatchScan {
    cXyz boomPos;
    uint32_t boomNetId;
    int attached;
};

bool already_ridden(fpc_ProcID id) {
    for (const RiddenBomb& rb : s_ridden) {
        if (rb.used && rb.id == id) return true;
    }
    return false;
}

void* attach_caught_bomb(void* proc, void* data) {
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    auto* scan = static_cast<CatchScan*>(data);
    if (fopAcM_GetName(actor) != fpcNm_NBOMB_e) return nullptr;
    if (!param_is_boomerang_carry(fopAcM_GetParam(actor))) return nullptr;
    const fpc_ProcID id = fopAcM_GetID(actor);
    if (already_ridden(id)) return nullptr;

    const f32 dx = actor->current.pos.x - scan->boomPos.x;
    const f32 dy = actor->current.pos.y - scan->boomPos.y;
    const f32 dz = actor->current.pos.z - scan->boomPos.z;
    if (dx * dx + dy * dy + dz * dz > 300.0f * 300.0f) return nullptr;
    for (RiddenBomb& rb : s_ridden) {
        if (rb.used) continue;
        rb.used = true;
        rb.id = id;
        rb.boomNetId = scan->boomNetId;

        rb.offsetY = dy;
        rb.offsetXZ = std::sqrt(dx * dx + dz * dz);
        if (rb.offsetXZ < 30.0f) rb.offsetXZ = 60.0f;
        if (rb.offsetXZ > 300.0f) rb.offsetXZ = 300.0f;
        if (rb.offsetY < -700.0f) rb.offsetY = -700.0f;
        if (rb.offsetY > 50.0f) rb.offsetY = 50.0f;
        rb.angle = cM_atan2s(dx, dz);
        ++scan->attached;
        coop_log::info("coop_mod: [SPAWN-BOMB] their boomerang caught a Bombling - carrying the "
                        "bomb ourselves (offsetXZ={:.0f} offsetY={:.0f})", rb.offsetXZ, rb.offsetY);
        return nullptr;
    }
    return nullptr;
}

void carry_bombs_on_replica(const Replica& r, fopAc_ac_c* boom) {
    CatchScan scan{};
    scan.boomPos = boom->current.pos;
    scan.boomNetId = r.netId;
    scan.attached = 0;
    fopAcM_Search(attach_caught_bomb, &scan);

    for (RiddenBomb& rb : s_ridden) {
        if (!rb.used || rb.boomNetId != r.netId) continue;
        auto* bomb = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(rb.id));
        if (bomb == nullptr) {
            rb = RiddenBomb{};
            continue;
        }
        rb.angle -= kRiddenRotStep;
        bomb->current.pos.set(boom->current.pos.x + rb.offsetXZ * cM_ssin(rb.angle),
                              boom->current.pos.y + rb.offsetY,
                              boom->current.pos.z + rb.offsetXZ * cM_scos(rb.angle));
        bomb->shape_angle.y -= kRiddenRotStep;

        rearm_ridden_bomb(bomb);

        bomb->speed.set(0.0f, 0.0f, 0.0f);
        bomb->speedF = 0.0f;
        bomb->gravity = 0.0f;
        bomb->old.pos = bomb->current.pos;
    }
}

void release_bombs_of(uint32_t boomNetId) {
    for (RiddenBomb& rb : s_ridden) {
        if (rb.used && rb.boomNetId == boomNetId) rb = RiddenBomb{};
    }
}

void apply_replicas() {
    daAlink_c* alinkForCutscene = daAlink_getAlinkActorClass();

    const bool localInCutscene =
        alinkForCutscene != nullptr &&
        (alinkForCutscene->checkEventRun() != FALSE || boss_local_demo_running());
    int live = 0;
    for (int i = 0; i < kMaxSpawns; ++i) {
        Replica& r = s_replica[i];
        if (!r.used) continue;
        auto* actor = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(r.id));
        if (actor == nullptr) {
            r = Replica{};
            continue;
        }

        if (r.procName == fpcNm_NBOMB_e && s_tick % 30 == 0) {

            const int fuse = static_cast<int>(static_cast<daNbomb_c*>(actor)->mExTime);

            const u32 prm = fopAcM_GetParam(actor);
            const bool exploding = prm == static_cast<u32>(dBomb_c::PRM_NORMAL_BOMB_EXPLODE) ||
                                   prm == static_cast<u32>(dBomb_c::PRM_WATER_BOMB_EXPLODE);
            if (fuse <= 0 && !exploding) {
                coop_log::warn("coop_mod: [SPAWN-BOMB] netId={} has no fuse (param={}) - it will "
                                "never explode and never free its slot", r.netId,
                    static_cast<int>(fopAcM_GetParam(actor)));
            }
        }
        if (s_tick - r.stamp > kReplicaStaleTicks) {
            coop_log::warn("coop_mod: [SPAWN] replica netId={} name={:#x} outlived its owner's "
                            "updates - deleting it rather than leaking the slot",
                r.netId, static_cast<int>(r.procName));
            fopAcM_delete(actor);
            r = Replica{};
            continue;
        }
        ++live;
        if (!r.havePos) continue;

        if (r.procName == fpcNm_BOOMERANG_e) {

            cXyz want = r.pos;
            if (r.haveVel) {

                const uint8_t owner = static_cast<uint8_t>(r.netId >> 28);
                const f32 transit = static_cast<f32>(coop_net_rtt_ticks(owner)) * 0.5f;
                const f32 coast = static_cast<f32>(s_tick - r.velTick) + transit;

                const f32 kMaxCoastTicks = 8.0f;
                const f32 n = coast < kMaxCoastTicks ? coast : kMaxCoastTicks;
                want.x += r.vel.x * n;
                want.y += r.vel.y * n;
                want.z += r.vel.z * n;
            }
            const f32 dx = want.x - actor->current.pos.x;
            const f32 dy = want.y - actor->current.pos.y;
            const f32 dz = want.z - actor->current.pos.z;
            const f32 far2 = 300.0f * 300.0f;
            if (dx * dx + dy * dy + dz * dz > far2) {
                actor->current.pos = want;
            } else {

                const f32 k = 0.8f;
                actor->current.pos.x += dx * k;
                actor->current.pos.y += dy * k;
                actor->current.pos.z += dz * k;
            }
        } else {
            actor->current.pos = r.pos;
        }
        actor->shape_angle = r.angle;
        if (r.procName == fpcNm_BOOMERANG_e) {
            neutralise_boomerang(static_cast<daBoomerang_c*>(actor), r.angle);

            if (!localInCutscene && !r.ownerInCutscene && r.settling == 0) {
                carry_bombs_on_replica(r, actor);
            } else {
                release_bombs_of(r.netId);
            }
        }
        update_replica_visibility(actor, r, localInCutscene);
    }
    s_diagReplicas = live;

    if (s_tick % 60 == 0) {
        for (int i = 0; i < kMaxSpawns; ++i) {
            const Replica& r = s_replica[i];
            if (!r.used || r.procName != fpcNm_BOOMERANG_e) continue;
            auto* actor = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(r.id));
            if (actor == nullptr) continue;
            auto* boom = static_cast<daBoomerang_c*>(actor);
            const f32 dx = actor->current.pos.x - r.pos.x;
            const f32 dy = actor->current.pos.y - r.pos.y;
            const f32 dz = actor->current.pos.z - r.pos.z;

            const bool moving = actor->speedF != 0.0f || boom->field_0x957 != 0;
            const bool noGale = fopAcM_GetParam(actor) == 0;
            if (moving || noGale) {
                coop_log::warn("coop_mod: [SPAWN-BOOM] netId={} INVARIANT BROKEN param={} "
                                "speedF={:.1f} burst={} storedSpeed={:.1f} drift={:.0f}",
                    r.netId, static_cast<int>(fopAcM_GetParam(actor)), actor->speedF,
                    static_cast<int>(boom->field_0x957), boom->field_0x988,
                    std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
    }
}

void run_self_test() {
    int64_t after = 0;
    if (s_selfTestVar != 0) svc_config->get_int(mod_ctx, s_selfTestVar, &after);
    if (after <= 0) return;
    static uint32_t ticks = 0;
    static int dropped = 0;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) { ticks = 0; return; }
    if (++ticks < static_cast<uint32_t>(after)) return;
    ticks = 0;

    if (dropped > 0) return;
    ++dropped;

    log_bomb_res_once();
    cXyz at(alink->current.pos.x + cM_ssin(alink->shape_angle.y) * 80.0f, alink->current.pos.y,
        alink->current.pos.z + cM_scos(alink->shape_angle.y) * 80.0f);
    fopAc_ac_c* bomb = fopAcM_fastCreate(fpcNm_NBOMB_e,
        static_cast<u32>(dBomb_c::PRM_BOMB_WAIT), &at, fopAcM_GetRoomNo(alink), nullptr, nullptr,
        -1, nullptr, nullptr);
    coop_log::warn("coop_mod: [SPAWN-SELFTEST] *** DEBUG *** dropped ONE bomb at "
                    "({:.0f},{:.0f},{:.0f}) -> {}",
        at.x, at.y, at.z, bomb != nullptr ? "created" : "FAILED");
}

}

bool spawns_is_replica(fopAc_ac_c* actor) {
    return actor != nullptr && is_replica(fopAcM_GetID(actor));
}

void spawns_register_vars() {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = "sync_spawned_objects";
    desc.type = CONFIG_VAR_BOOL;
    desc.default_bool = true;
    if (svc_config->register_var(mod_ctx, &desc, &s_enableVar) != MOD_OK) s_enableVar = 0;

    ConfigVarDesc noHook = CONFIG_VAR_DESC_INIT;
    noHook.name = "debug_spawn_no_hook";
    noHook.type = CONFIG_VAR_BOOL;
    noHook.default_bool = false;
    if (svc_config->register_var(mod_ctx, &noHook, &s_noHookVar) != MOD_OK) s_noHookVar = 0;

    ConfigVarDesc selfTest = CONFIG_VAR_DESC_INIT;
    selfTest.name = "debug_spawn_bomb_ticks";
    selfTest.type = CONFIG_VAR_INT;
    selfTest.default_int = 0;
    if (svc_config->register_var(mod_ctx, &selfTest, &s_selfTestVar) != MOD_OK) s_selfTestVar = 0;
}

void spawns_init() {
    if (cfg_bool(s_noHookVar, false)) {
        coop_log::warn("coop_mod: [SPAWN] *** DEBUG *** fastCreate hook NOT installed");
        return;
    }
    const ModResult pre = mods::hook::add_pre<FastCreateHook>(on_fast_create_pre);
    const ModResult post = mods::hook::add_post<FastCreateHook>(on_fast_create_post);
    coop_log::info("coop_mod: [SPAWN] fastCreate hook: pre={} post={}", static_cast<int>(pre),
        static_cast<int>(post));
    const ModResult heap = mods::hook::add_pre<BombCreateHeapHook>(on_bomb_create_heap_pre);
    coop_log::info("coop_mod: [SPAWN] bomb type hook: {}",
        heap == MOD_OK ? "attached" : "FAILED - water bomb replicas will look like plain ones");
    int64_t bombTicks = 0;
    if (s_selfTestVar != 0) svc_config->get_int(mod_ctx, s_selfTestVar, &bombTicks);
    if (bombTicks != 0) {
        coop_log::warn(
            "coop_mod: *** DEBUG SELF-TEST IS ARMED *** debug_spawn_bomb_ticks={} - this game will "
            "drop a live bomb every {} ticks. Set it to 0 (or launch with play.ps1) unless you are "
            "running the automated test.",
            bombTicks, bombTicks);
    }
}

void spawns_on_connected() {
    reset_tables();

    s_nextNetId = 1;
}

void remove_boomerang_replicas();

void spawns_on_disconnected() {

    if (daAlink_getAlinkActorClass() != nullptr) remove_boomerang_replicas();
    reset_tables();
}

bool spawns_replicates_procname(int16_t procName) {
    return procname_is_replicated(static_cast<s16>(procName));
}

void remove_boomerang_replicas() {
    for (int i = 0; i < kMaxSpawns; ++i) {
        Replica& r = s_replica[i];
        if (!r.used || r.procName != fpcNm_BOOMERANG_e) continue;
        auto* actor = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(r.id));
        if (actor != nullptr) fopAcM_delete(actor);
        release_bombs_of(r.netId);
        coop_log::info("coop_mod: [SPAWN] cutscene - removed their boomerang netId={}", r.netId);
        r = Replica{};
    }
}

struct FreshBoomBomb {
    fpc_ProcID id = 0;
    uint32_t born = 0;
    cXyz pos;
};
const int kMaxFreshBoomBombs = kCoopMaxPlayers * 2;
FreshBoomBomb s_freshBoomBombs[kMaxFreshBoomBombs];
const uint32_t kCatchDupTicks = 10;
const f32 kCatchDupDist = 40.0f;

struct BoomBombScan {
    fopAc_ac_c* found[kMaxFreshBoomBombs];
    int count;
};

void* collect_boom_bombs(void* proc, void* data) {
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    auto* scan = static_cast<BoomBombScan*>(data);
    if (fopAcM_GetName(actor) != fpcNm_NBOMB_e) return nullptr;
    if (fopAcM_GetParam(actor) != static_cast<u32>(dBomb_c::PRM_ENEMY_BOMB_BOOMERANG)) {
        return nullptr;
    }
    if (is_replica(fopAcM_GetID(actor))) return nullptr;
    if (scan->count < kMaxFreshBoomBombs) scan->found[scan->count++] = actor;
    return nullptr;
}

void dedupe_boomerang_catches() {
    for (FreshBoomBomb& f : s_freshBoomBombs) {
        if (f.id != 0 && s_tick - f.born > kCatchDupTicks * 4) f = FreshBoomBomb{};
    }
    BoomBombScan scan{};
    fopAcM_Search(collect_boom_bombs, &scan);
    for (int i = 0; i < scan.count; ++i) {
        fopAc_ac_c* bomb = scan.found[i];
        const fpc_ProcID id = fopAcM_GetID(bomb);
        bool known = false;
        for (const FreshBoomBomb& f : s_freshBoomBombs) {
            if (f.id == id) known = true;
        }
        if (known) continue;
        bool duplicate = false;
        for (const FreshBoomBomb& f : s_freshBoomBombs) {
            if (f.id == 0 || s_tick - f.born > kCatchDupTicks) continue;
            if ((f.pos - bomb->current.pos).abs() < kCatchDupDist) duplicate = true;
        }
        if (duplicate) {
            coop_log::info("coop_mod: [SPAWN-BOMB] second bomb from the same boomerang catch - "
                            "removing it");
            fopAcM_delete(bomb);
            continue;
        }
        for (FreshBoomBomb& f : s_freshBoomBombs) {
            if (f.id != 0) continue;
            f.id = id;
            f.born = s_tick;
            f.pos = bomb->current.pos;
            break;
        }
    }
}

void spawns_update() {
    ++s_tick;
    if (s_tick % 300 == 0) {
        coop_log::info("coop_mod: [SPAWN] {} conn={} peerHere={} ours={} theirs={}",
            coop_net_is_host() ? "host" : "joiner", coop_net_connected() ? 1 : 0,
            peer_on_our_stage() ? 1 : 0, s_diagOwned, s_diagReplicas);
    }

    if (spawns_enabled() && dComIfGp_event_runCheck()) remove_boomerang_replicas();
    if (!spawns_enabled() || !in_gameplay()) {
        return;
    }
    if (peer_on_our_stage()) dedupe_boomerang_catches();
    apply_replicas();
    promote_candidates();
    run_self_test();

    if (s_tick % kSendEveryTicks == 0 || we_own_a_boomerang()) send_transforms();
}

void spawns_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from) {
    if (!cfg_bool(s_enableVar, true)) return;
    switch (type) {
    case kMsgActorSpawn: {
        if (size < sizeof(MsgActorSpawn) || !in_gameplay()) return;
        MsgActorSpawn msg;
        std::memcpy(&msg, payload, sizeof(msg));
        if (find_replica(msg.netId) != nullptr) return;
        if (!should_replicate(msg.procName, msg.param, msg.param)) return;
        log_bomb_res_once();
        if (msg.procName == fpcNm_BOOMERANG_e && !alink_arc_resident()) {
            coop_log::info("coop_mod: [SPAWN] refusing netId={} - a boomerang needs Link's archive "
                           "and it is not resident here", msg.netId);
            return;
        }
        if (msg.procName == fpcNm_NBOMB_e && !bomb_model_available()) {

            coop_log::info("coop_mod: [SPAWN] refusing netId={} - the bomb model is not resident "
                            "here", msg.netId);
            return;
        }
        int slot = -1;
        for (int i = 0; i < kMaxSpawns; ++i) {
            if (!s_replica[i].used) { slot = i; break; }
        }
        if (slot < 0) return;

        const cXyz pos(msg.pos[0], msg.pos[1], msg.pos[2]);
        const csXyz angle(msg.angle[0], msg.angle[1], msg.angle[2]);
        ++s_creatingReplica;
        s_replicaBombKind = msg.procName == fpcNm_NBOMB_e ? msg.kind : 0xFF;
        fopAc_ac_c* actor = fopAcM_fastCreate(msg.procName,
            create_param_for_kind(msg.procName, msg.param, msg.kind), &pos,
            static_cast<int>(msg.room), &angle, nullptr, -1, nullptr, nullptr);
        s_replicaBombKind = 0xFF;
        --s_creatingReplica;
        if (actor == nullptr) {
            coop_log::info("coop_mod: [SPAWN] could not create netId={} name={:#x}", msg.netId,
                static_cast<int>(msg.procName));
            return;
        }

        write_actor_state(actor, msg.procName, msg.state);
        write_actor_kind(actor, msg.procName, msg.kind);

        hide_for_first_frame(actor);

        s_replica[slot].used = true;
        s_replica[slot].netId = msg.netId;
        s_replica[slot].id = fopAcM_GetID(actor);
        s_replica[slot].procName = msg.procName;
        s_replica[slot].havePos = true;
        s_replica[slot].pos = pos;
        s_replica[slot].angle = angle;
        s_replica[slot].stamp = s_tick;
        s_replica[slot].settling = kReplicaSettleTicks;
        coop_log::info("coop_mod: [SPAWN] theirs netId={} name={:#x} created", msg.netId,
            static_cast<int>(msg.procName));
        break;
    }
    case kMsgActorState: {
        if (size < 1) return;
        const int count = payload[0];
        if (size < 1 + count * sizeof(MsgActorState)) return;
        for (int i = 0; i < count; ++i) {
            MsgActorState st;
            std::memcpy(&st, payload + 1 + i * sizeof(st), sizeof(st));
            Replica* r = find_replica(st.netId);
            if (r == nullptr) continue;

            const cXyz fresh(st.pos[0], st.pos[1], st.pos[2]);
            const uint32_t gap = s_tick - r->stamp;
            if (r->havePos && gap > 0 && gap <= 8) {
                r->vel.set((fresh.x - r->pos.x) / static_cast<f32>(gap),
                           (fresh.y - r->pos.y) / static_cast<f32>(gap),
                           (fresh.z - r->pos.z) / static_cast<f32>(gap));
                r->haveVel = true;
            } else {
                r->haveVel = false;
            }
            r->velTick = s_tick;
            r->pos = fresh;
            r->angle.x = st.angle[0];
            r->angle.y = st.angle[1];
            r->angle.z = st.angle[2];
            r->havePos = true;
            r->stamp = s_tick;
            auto* actor = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(r->id));
            if (actor == nullptr) continue;

            write_actor_state(actor, r->procName, st.state);

            if (fopAcM_GetParam(actor) != st.param) fopAcM_SetParam(actor, st.param);
            if (r->procName == fpcNm_NBOMB_e) {
                read_roll(st.roll, static_cast<daNbomb_c*>(actor)->field_0xa40);
            }
            r->ownerInCutscene = (st.flags & kActorFlagOwnerInCutscene) != 0;
            const bool wantCarried = (st.flags & kActorFlagCarried) != 0;
            if (wantCarried != (fopAcM_checkCarryNow(actor) != 0)) {
                if (wantCarried) {
                    fopAcM_setCarryNow(actor, 1);
                } else {
                    fopAcM_cancelCarryNow(actor);
                }
            }
        }
        break;
    }
    case kMsgActorGone: {
        if (size < sizeof(MsgActorGone)) return;
        MsgActorGone msg;
        std::memcpy(&msg, payload, sizeof(msg));
        Replica* r = find_replica(msg.netId);
        if (r == nullptr) return;
        auto* actor = static_cast<fopAc_ac_c*>(fopAcM_SearchByID(r->id));
        if (actor != nullptr && gone_removes_replica(r->procName)) {
            fopAcM_delete(actor);
            coop_log::info("coop_mod: [SPAWN] theirs netId={} removed", msg.netId);
        } else {

            coop_log::info("coop_mod: [SPAWN] theirs netId={} released to run itself out",
                msg.netId);
        }
        *r = Replica{};
        break;
    }
    default:
        break;
    }

    (void)&find_owned;
}

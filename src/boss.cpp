

#include "boss_adapter.hpp"
#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_b_bh.h"
#include "d/actor/d_a_b_bq.h"
#include "d/actor/d_a_e_mb.h"
#include "d/actor/d_a_e_mk_bo.h"
#include "d/actor/d_a_obj_pillar.h"
#include "d/actor/d_a_e_db.h"
#include "d/actor/d_a_e_mk.h"
#include "f_pc/f_pc_manager.h"
#include "d/actor/d_a_player.h"
#include "d/d_bomb.h"
#include "d/d_cc_d.h"
#include "d/d_cc_s.h"
#include "d/d_com_inf_game.h"
#include "d/d_drawlist.h"
#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/TColor.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_controller_pad.h"
#include "m_Do/m_Do_graphic.h"
#include "m_Do/m_Do_ext.h"
#include "f_op/f_op_actor_iter.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_executor.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

const int kSendEveryTicks = 1;
const int kMaxBossActors = 8;

ConfigVarHandle s_enableVar = 0;
uint32_t s_tick = 0;
int s_diagSent = 0;
int s_diagApplied = 0;

bool in_gameplay() {
    return daAlink_getAlinkActorClass() != nullptr && !dComIfGp_isEnableNextStage() &&
           dComIfGp_getStageStagInfo() != nullptr;
}

bool boss_sync_on() {
    return coop_net_connected() && coop_session(kSessBosses, cfg_bool(s_enableVar, false)) &&
           in_gameplay() && peer_on_our_stage();
}

bool boss_wait_possible() {
    return coop_net_connected() && in_gameplay();
}

struct BossList {
    fopAc_ac_c* actors[kMaxBossActors];
    uint8_t kinds[kMaxBossActors];
    uint8_t indices[kMaxBossActors];
    int count;
};

void note_boss_beaten(const char* stage, int room);
bool boss_already_beaten(const char* stage, int room);
bool kind_holds_a_fight(uint8_t kind);

uint8_t kind_of(s16 procName) {
    switch (procName) {
    case kBossProcDiababa: return kBossKindDiababa;
    case kBossProcTentacle: return kBossKindTentacle;
    case kBossProcOok: return kBossKindOok;
    case kBossProcHelper: return kBossKindHelper;
    case kBossProcOokBoomerang: return kBossKindOokBoomerang;
    default: return kBossKindNone;
    }
}

void* collect_boss(void* proc, void* data) {
    auto* list = static_cast<BossList*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || list->count >= kMaxBossActors) return nullptr;
    const uint8_t kind = kind_of(fopAcM_GetName(actor));
    if (kind == kBossKindNone) return nullptr;
    const int i = list->count++;
    list->actors[i] = actor;
    list->kinds[i] = kind;

    if (kind == kBossKindTentacle) {
        list->indices[i] = reinterpret_cast<b_bh_class*>(actor)->mID;
    } else {
        list->indices[i] = static_cast<uint8_t>(fopAcM_GetParam(actor) & 0xFF);
    }
    return nullptr;
}

void collect(BossList& list) {
    list.count = 0;
    fopAcM_Search(collect_boss, &list);
}

fopAc_ac_c* find(BossList& list, uint8_t kind, uint8_t index) {
    for (int i = 0; i < list.count; ++i) {
        if (list.kinds[i] == kind && list.indices[i] == index) return list.actors[i];
    }
    return nullptr;
}

using namespace coop_boss;

u8 helper_anm_attr(int anmID);
u8 helper_anm_attr_fn(int anmId) { return helper_anm_attr(anmId); }

const BossField kFieldsDiababa[] = {
    BOSS_FIELD(b_bq_class, mAction, kBSlotAction, kBPolicyMirror),
    BOSS_FIELD(b_bq_class, mMode, kBSlotMode, kBPolicyMirror),
    BOSS_FIELD(b_bq_class, mHeadRot, bslot_extra(0), kBPolicyMirror),
    BOSS_FIELD(b_bq_class, mDisableDraw, bslot_extra(3), kBPolicyMirror),
    BOSS_FIELD(b_bq_class, mColpatType, bslot_extra(4), kBPolicyMirror),
    BOSS_FIELD(b_bq_class, mSetBossExplode, bslot_extra(5), kBPolicyMirror),
    BOSS_FIELD(b_bq_class, mSetDeadColor, bslot_extra(6), kBPolicyMirror),

    BOSS_FIELD(b_bq_class, field_0x1394, bslot_extra(7), kBPolicyMirror),

    BOSS_FIELD(b_bq_class, field_0x6fa, bslot_extra(8), kBPolicyMirror),
    BOSS_FIELD(b_bq_class, field_0x6fe, bslot_extra(9), kBPolicyMirror),

    BOSS_FIELD(b_bq_class, field_0x6ec, bslot_extra(10), kBPolicyMirror),

    BOSS_FIELD(b_bq_class, mDamageBackCount, bslot_extra(11), kBPolicyMirror),
    BOSS_FIELD(b_bq_class, mColpatBlend, kBSlotExtraF, kBPolicyMirror),
};

const BossField kFieldsTentacle[] = {
    BOSS_FIELD(b_bh_class, mAction, kBSlotAction, kBPolicyMirror),
    BOSS_FIELD(b_bh_class, mMode, kBSlotMode, kBPolicyMirror),
};

const BossField kFieldsOok[] = {
    BOSS_FIELD(e_mk_class, action, kBSlotAction, kBPolicyMirror),
    BOSS_FIELD(e_mk_class, mode, kBSlotMode, kBPolicyMirror),
    BOSS_FIELD(e_mk_class, unkCounter1, bslot_timer(4), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, crownStatus, bslot_extra(0), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, boomerangStatus, bslot_extra(1), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, field_0x6b4, bslot_extra(2), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, invulnerabilityTimer, bslot_extra(3), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, tubaTimer, bslot_extra(4), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, btpFrameFlag, bslot_extra(5), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, unkFlag2, bslot_extra(6), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, unkCounter2, bslot_extra(7), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, unkTimer1, bslot_extra(8), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, unkCounter3, bslot_extra(9), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, setSmokeFlag, bslot_extra(10), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, demoHasiraFlag, bslot_extra(11), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, firstHasiraFlag, bslot_extra(12), kBPolicyMirror),
    BOSS_FIELD(e_mk_class, btpFrame, kBSlotExtraF, kBPolicyMirror),
};

const BossField kFieldsHelper[] = {
    BOSS_FIELD(e_mb_class, mAction, kBSlotAction, kBPolicyMirror),
    BOSS_FIELD(e_mb_class, mMode, kBSlotMode, kBPolicyMirror),
    BOSS_FIELD(e_mb_class, mCounter, bslot_timer(3), kBPolicyMirror),
    BOSS_FIELD(e_mb_class, field_0x6a2, bslot_timer(4), kBPolicyMirror),
    BOSS_FIELD(e_mb_class, field_0x5d4, bslot_extra(0), kBPolicyMirror),
    BOSS_FIELD(e_mb_class, field_0x68c, bslot_extra(1), kBPolicyMirror),
    BOSS_FIELD(e_mb_class, field_0x6b0, bslot_extra(2), kBPolicyMirror),

    BOSS_FIELD(e_mb_class, field_0x8c8, bslot_extra(3), kBPolicySticky),
    BOSS_FIELD(e_mb_class, field_0x6f0, kBSlotExtraF, kBPolicyMirror),
};

const BossField kFieldsOokBoomerang[] = {
    BOSS_FIELD(e_mk_bo_class, action, kBSlotAction, kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, mode, kBSlotMode, kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, counter, bslot_timer(2), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x5ec, bslot_timer(3), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x5ee, bslot_timer(4), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x5f8, bslot_extra(0), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x5fa, bslot_extra(1), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x600, bslot_extra(2), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x602, bslot_extra(3), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x998, bslot_extra(4), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x9b4, bslot_extra(5), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x9b5, bslot_extra(6), kBPolicyMirror),
    BOSS_FIELD(e_mk_bo_class, field_0x5f0, kBSlotExtraF, kBPolicyMirror),
};

#define BOSS_FIELDS(arr) arr, (uint8_t)(sizeof(arr) / sizeof((arr)[0]))

const BossAdapter kBossAdapters[] = {
    { kBossProcDiababa, kBossKindDiababa, "Diababa",
      kBossHoldsDoor,
      "B_bq", (uint16_t)offsetof(b_bq_class, mpMorf), (uint16_t)offsetof(b_bq_class, mAnmID),
      kBTypeS32, nullptr,
      BOSS_FIELDS(kFieldsDiababa),
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },

    { kBossProcTentacle, kBossKindTentacle, "tentacle",
      0,
      "B_BH", (uint16_t)offsetof(b_bh_class, mpModelMorf), (uint16_t)offsetof(b_bh_class, mAnm),
      kBTypeS32, nullptr,
      BOSS_FIELDS(kFieldsTentacle),
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },

    { kBossProcOok, kBossKindOok, "Ook (mini-boss)",
      kBossSelfMoving | kBossNoRetarget | kBossIgnoreDemoGate | kBossHoldsDoor,
      "E_mk", (uint16_t)offsetof(e_mk_class, anmP), (uint16_t)offsetof(e_mk_class, anmNo),
      kBTypeS32, nullptr,
      BOSS_FIELDS(kFieldsOok),
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },

    { kBossProcHelper, kBossKindHelper, "Ook (Diababa helper)",
      kBossSelfMoving | kBossNoRetarget | kBossIgnoreDemoGate,
      "E_mb", (uint16_t)offsetof(e_mb_class, mpModelMorf), (uint16_t)offsetof(e_mb_class, mAnm),
      kBTypeS32, helper_anm_attr_fn,
      BOSS_FIELDS(kFieldsHelper),
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },

    { kBossProcOokBoomerang, kBossKindOokBoomerang, "Ook's boomerang",
      kBossSelfMoving | kBossNoRetarget,
      nullptr, 0xFFFF, 0xFFFF, kBTypeS32, nullptr,
      BOSS_FIELDS(kFieldsOokBoomerang),
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
};

const BossAdapter* adapter_for_kind(uint8_t kind) {
    for (const BossAdapter& a : kBossAdapters) {
        if (a.kind == kind) return &a;
    }
    return nullptr;
}

bool kind_has_flag(uint8_t kind, uint16_t flag) {
    const BossAdapter* a = adapter_for_kind(kind);
    return a != nullptr && (a->flags & flag) != 0;
}

void read_morf(mDoExt_morf_c* morf, MsgBossActor& m) {
    if (morf == nullptr) {
        m.anmFrame = 0.0f;
        m.anmRate = 0.0f;
        return;
    }
    m.anmFrame = morf->getFrame();
    m.anmRate = morf->getPlaySpeed();
}

void switch_morf_anm(mDoExt_McaMorfSO* morf, const char* arc, int anmID, f32 morfFrames,
    f32 speed, u8 attr = J3DFrameCtrl::EMode_NONE) {
    if (morf == nullptr || arc == nullptr) return;
    auto* bck = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(arc, anmID));
    if (bck == nullptr) return;
    morf->setAnm(bck, attr, morfFrames, speed, 0.0f, -1.0f);
}

u8 helper_anm_attr(int anmID) {
    switch (anmID) {
    case 8:
    case 18:
    case 19:
        return 2;
    default:
        return J3DFrameCtrl::EMode_NONE;
    }
}

f32 s_morfAhead = 0.0f;

void write_morf(mDoExt_morf_c* morf, const MsgBossActor& m) {
    if (morf == nullptr) return;

    f32 frame = m.anmFrame + m.anmRate * s_morfAhead;
    const f32 end = morf->getEndFrame();
    if (end > 0.0f && frame > end) {

        const f32 loop = morf->getLoopFrame();
        if (morf->getPlayMode() == J3DFrameCtrl::EMode_LOOP && end > loop) {
            frame = loop + std::fmod(frame - loop, end - loop);
        } else {
            frame = end;
        }
    }
    if (frame < 0.0f) frame = 0.0f;
    morf->setFrameF(frame);
    morf->setPlaySpeed(m.anmRate);
}

void read_actor(fopAc_ac_c* actor, MsgBossActor& m) {
    m.pos[0] = actor->current.pos.x;
    m.pos[1] = actor->current.pos.y;
    m.pos[2] = actor->current.pos.z;
    m.angle[0] = actor->current.angle.x;
    m.angle[1] = actor->current.angle.y;
    m.angle[2] = actor->current.angle.z;
    m.shapeAngle[0] = actor->shape_angle.x;
    m.shapeAngle[1] = actor->shape_angle.y;
    m.shapeAngle[2] = actor->shape_angle.z;
    m.health = actor->health;
}

void write_actor(fopAc_ac_c* actor, const MsgBossActor& m) {
    actor->current.pos.set(m.pos[0], m.pos[1], m.pos[2]);
    actor->current.angle.x = m.angle[0];
    actor->current.angle.y = m.angle[1];
    actor->current.angle.z = m.angle[2];
    actor->shape_angle.x = m.shapeAngle[0];
    actor->shape_angle.y = m.shapeAngle[1];
    actor->shape_angle.z = m.shapeAngle[2];
    actor->health = m.health;
}

const s16 kBqActionEnd = 4;

const s16 kBqDemoDeath = 50;

bool s_hostInDemo = false;

uint8_t s_fightOwner = kCoopNoPlayer;

bool i_run_the_fight() {
    return s_fightOwner == coop_net_local_id();
}

bool s_hostFightOver = false;
bool s_bossSeen = false;
int s_fightOverSends = 0;
const int kFightOverSends = 180;

void read_diababa(fopAc_ac_c* actor, MsgBossActor& m) {
    auto* b = reinterpret_cast<b_bq_class*>(actor);
    read_actor(actor, m);
    s_hostInDemo = b->mDemoMode != 0;
    s_bossSeen = true;

    if (b->mAction == kBqActionEnd) {
        if (!s_hostFightOver) s_fightOverSends = kFightOverSends;
        s_hostFightOver = true;
        note_boss_beaten(dComIfGp_getStartStageName(), fopAcM_GetRoomNo(actor));
    }
    m.inDemo = s_hostInDemo ? 1 : 0;
    m.fightOver = s_hostFightOver ? 1 : 0;
    m.anmId = b->mAnmID;
    read_morf(b->mpMorf, m);
    adapter_read_fields(actor, adapter_for_kind(kBossKindDiababa), m);
    for (int i = 0; i < 5; ++i) m.timers[i] = b->mTimers[i];

    m.extra[1] = 0;
    m.extra[2] = 0;

}

bool in_hit_grace(uint8_t kind, uint8_t index);

bool s_ookHandedOver = false;

bool s_fightOver = false;
void boss_reset_fight_state();
void boss_reset_eat_watch();

void write_diababa(fopAc_ac_c* actor, const MsgBossActor& m) {
    auto* b = reinterpret_cast<b_bq_class*>(actor);
    if (m.fightOver && !s_fightOver) {
        s_fightOver = true;

        note_boss_beaten(dComIfGp_getStartStageName(), fopAcM_GetRoomNo(actor));
        if (b->mAction != kBqActionEnd) {
            b->mAction = kBqActionEnd;
            b->mMode = 0;
            b->mDemoMode = kBqDemoDeath;
            b->mDemoModeTimer = 0;
            coop_log::info("coop_mod: [BOSS] the fight is over - starting the death demo "
                            "(action={} demoMode={}), the rest is local",
                            static_cast<int>(b->mAction), static_cast<int>(b->mDemoMode));
        } else {
            coop_log::info("coop_mod: [BOSS] the fight is over - we were already dying "
                            "(demoMode={}), the rest is local", static_cast<int>(b->mDemoMode));
        }
        return;
    }
    if (s_fightOver) return;

    if (actor->health > m.health) actor->health = m.health;

    if (m.inDemo || b->mDemoMode != 0) return;
    write_actor(actor, m);

    if (b->mAnmID != m.anmId) {
        switch_morf_anm(b->mpMorf, "B_bq", m.anmId, 3.0f, m.anmRate);
        b->mAnmID = m.anmId;
    }
    write_morf(b->mpMorf, m);
    adapter_write_fields(actor, adapter_for_kind(kBossKindDiababa), m,
        in_hit_grace(kBossKindDiababa, m.index) ? boss_skip_action_mode() : 0);
    for (int i = 0; i < 5; ++i) b->mTimers[i] = m.timers[i];

}

void read_tentacle(fopAc_ac_c* actor, MsgBossActor& m) {
    auto* t = reinterpret_cast<b_bh_class*>(actor);
    read_actor(actor, m);
    m.inDemo = s_hostInDemo ? 1 : 0;
    m.fightOver = s_hostFightOver ? 1 : 0;

    m.anmId = t->mAnm;
    read_morf(t->mpModelMorf, m);
    adapter_read_fields(actor, adapter_for_kind(kBossKindTentacle), m);
    for (int i = 0; i < 5; ++i) m.timers[i] = t->mTimers[i];

    for (int i = 0; i < 17; ++i) m.extra[i] = t->field_0x8d4[i];
}

const s16 kBhActionBombEat = 10;
const s16 kBhActionBBombEat = 22;

bool bh_action_is_eating(s16 action) {
    return action == kBhActionBombEat || action == kBhActionBBombEat;
}

void write_tentacle(fopAc_ac_c* actor, const MsgBossActor& m) {
    const bool ourHit = in_hit_grace(kBossKindTentacle, m.index);
    auto* t = reinterpret_cast<b_bh_class*>(actor);
    if (s_fightOver) return;

    if (actor->health > m.health) actor->health = m.health;

    if (bh_action_is_eating(t->mAction)) return;
    if (m.inDemo) return;
    write_actor(actor, m);
    if (t->mAnm != m.anmId) {
        switch_morf_anm(t->mpModelMorf, "B_BH", m.anmId, 3.0f, m.anmRate);
        t->mAnm = m.anmId;
    }
    write_morf(t->mpModelMorf, m);

    adapter_write_fields(actor, adapter_for_kind(kBossKindTentacle), m,
        ourHit ? boss_skip_action_mode() : 0);
    for (int i = 0; i < 5; ++i) t->mTimers[i] = m.timers[i];
    for (int i = 0; i < 17; ++i) t->field_0x8d4[i] = m.extra[i];
}

void read_ook_boomerang(fopAc_ac_c* actor, MsgBossActor& m) {
    auto* bo = reinterpret_cast<e_mk_bo_class*>(actor);
    read_actor(actor, m);
    m.inDemo = s_hostInDemo ? 1 : 0;
    m.fightOver = s_hostFightOver ? 1 : 0;
    m.anmId = -1;
    adapter_read_fields(actor, adapter_for_kind(kBossKindOokBoomerang), m);
    m.timers[0] = bo->timers[0];
    m.timers[1] = bo->timers[1];

    m.targetPos[0] = bo->field_0x5e0.x;
    m.targetPos[1] = bo->field_0x5e0.y;
    m.targetPos[2] = bo->field_0x5e0.z;
}

void write_ook_boomerang(fopAc_ac_c* actor, const MsgBossActor& m) {

    if (s_ookHandedOver) return;
    auto* bo = reinterpret_cast<e_mk_bo_class*>(actor);
    if (s_fightOver) return;
    write_actor(actor, m);
    adapter_write_fields(actor, adapter_for_kind(kBossKindOokBoomerang), m);
    bo->timers[0] = m.timers[0];
    bo->timers[1] = m.timers[1];
    bo->field_0x5e0.set(m.targetPos[0], m.targetPos[1], m.targetPos[2]);
}

void read_helper(fopAc_ac_c* actor, MsgBossActor& m) {
    auto* o = reinterpret_cast<e_mb_class*>(actor);
    read_actor(actor, m);
    m.inDemo = s_hostInDemo ? 1 : 0;
    m.fightOver = s_hostFightOver ? 1 : 0;
    m.anmId = o->mAnm;
    read_morf(o->mpModelMorf, m);
    adapter_read_fields(actor, adapter_for_kind(kBossKindHelper), m);

    for (int i = 0; i < 3; ++i) m.timers[i] = o->mTimers[i];

    m.extra[4] = static_cast<int16_t>(o->field_0x6a4.x);
    m.extra[5] = static_cast<int16_t>(o->field_0x6a4.y);
    m.extra[6] = static_cast<int16_t>(o->field_0x6a4.z);

    m.targetPos[0] = o->field_0x5b8.x;
    m.targetPos[1] = o->field_0x5b8.y;
    m.targetPos[2] = o->field_0x5b8.z;
}

void write_helper(fopAc_ac_c* actor, const MsgBossActor& m) {
    auto* o = reinterpret_cast<e_mb_class*>(actor);

    if (s_fightOver) return;
    write_actor(actor, m);
    if (o->mAnm != m.anmId) {
        switch_morf_anm(o->mpModelMorf, "E_mb", m.anmId, 3.0f, m.anmRate,
                        helper_anm_attr(m.anmId));
        o->mAnm = m.anmId;
    }
    write_morf(o->mpModelMorf, m);
    adapter_write_fields(actor, adapter_for_kind(kBossKindHelper), m);
    for (int i = 0; i < 3; ++i) o->mTimers[i] = m.timers[i];

    o->field_0x6a4.set(static_cast<f32>(m.extra[4]), static_cast<f32>(m.extra[5]),
                       static_cast<f32>(m.extra[6]));
    o->field_0x5b8.set(m.targetPos[0], m.targetPos[1], m.targetPos[2]);
}

void read_ook(fopAc_ac_c* actor, MsgBossActor& m) {
    auto* o = reinterpret_cast<e_mk_class*>(actor);
    read_actor(actor, m);

    static fpc_ProcID s_ookSeenAlive = fpcM_ERROR_PROCESS_ID_e;
    const fpc_ProcID ookId = fopAcM_GetID(actor);
    if (actor->health > 0) {
        s_ookSeenAlive = ookId;
    } else if (s_ookSeenAlive == ookId) {
        note_boss_beaten(dComIfGp_getStartStageName(), fopAcM_GetRoomNo(actor));
    }
    m.inDemo = s_hostInDemo ? 1 : 0;
    m.fightOver = s_hostFightOver ? 1 : 0;
    m.anmId = o->anmNo;
    read_morf(o->anmP, m);
    adapter_read_fields(actor, adapter_for_kind(kBossKindOok), m);

    for (int i = 0; i < 4; ++i) m.timers[i] = o->timer[i];

    m.targetPos[0] = o->posTarget.x;
    m.targetPos[1] = o->posTarget.y;
    m.targetPos[2] = o->posTarget.z;
}

struct OokDbScan {
    cXyz from;
    e_db_class* best;
    f32 bestDist;
};

void* ook_db_search(void* proc, void* data) {
    auto* scan = static_cast<OokDbScan*>(data);
    if (!fopAcM_IsActor(proc) || fopAcM_GetName(proc) != fpcNm_E_DB_e) return nullptr;
    auto* db = static_cast<e_db_class*>(proc);
    if (db->action != 10 || db->mode < 1) return nullptr;
    const f32 d = (db->enemy.current.pos - scan->from).abs();
    if (d < scan->bestDist) {
        scan->bestDist = d;
        scan->best = db;
    }
    return nullptr;
}

void ook_fix_db_target(e_mk_class* o) {
    if (o->mode != 10) return;
    if (o->db != nullptr && fopAcM_IsActor(o->db) &&
        fopAcM_GetName(o->db) == fpcNm_E_DB_e) {
        return;
    }
    OokDbScan scan{};
    scan.from = o->actor.current.pos;
    scan.best = nullptr;
    scan.bestDist = 2000.0f;
    fpcM_Search(ook_db_search, &scan);
    o->db = scan.best;
    if (o->db == nullptr) o->mode = 2;
}

void write_ook(fopAc_ac_c* actor, const MsgBossActor& m) {
    auto* o = reinterpret_cast<e_mk_class*>(actor);

    if (s_ookHandedOver) return;
    if (o->action == e_mk_class::ACT_E_DEMO) {
        s_ookHandedOver = true;
        return;
    }

    if (m.action == e_mk_class::ACT_E_DEMO) {
        s_ookHandedOver = true;
        o->action = e_mk_class::ACT_E_DEMO;
        o->mode = 0;
        if (actor->health > 0) actor->health = 0;

        mDoAud_subBgmStop();
        coop_log::info("coop_mod: [BOSS] Ook is down - starting our own death demo, "
                        "and he is local from here");
        return;
    }

    const bool ourHit = in_hit_grace(kBossKindOok, m.index);
    if (actor->health > m.health) actor->health = m.health;
    const s16 keepHealth = actor->health;

    if (s_fightOver) return;
    write_actor(actor, m);
    actor->health = keepHealth;
    if (o->anmNo != m.anmId) {
        switch_morf_anm(o->anmP, "E_mk", m.anmId, 3.0f, m.anmRate);
        o->anmNo = m.anmId;
    }
    write_morf(o->anmP, m);
    adapter_write_fields(actor, adapter_for_kind(kBossKindOok), m,
        ourHit ? boss_skip_action_mode() : 0);
    for (int i = 0; i < 4; ++i) o->timer[i] = m.timers[i];
    o->posTarget.set(m.targetPos[0], m.targetPos[1], m.targetPos[2]);
    ook_fix_db_target(o);
}

void apply_cached_stems(BossList& list);

struct CachedState {
    bool used = false;
    uint8_t kind = 0;
    uint8_t index = 0;
    MsgBossActor m{};
    uint32_t recvTick = 0;
};
CachedState s_cached[kMaxBossActors];

void cache_state(const MsgBossActor& m) {
    for (int i = 0; i < kMaxBossActors; ++i) {
        if (s_cached[i].used && s_cached[i].kind == m.kind && s_cached[i].index == m.index) {
            s_cached[i].m = m;
            s_cached[i].recvTick = s_tick;
            return;
        }
    }
    for (int i = 0; i < kMaxBossActors; ++i) {
        if (s_cached[i].used) continue;
        s_cached[i].used = true;
        s_cached[i].kind = m.kind;
        s_cached[i].index = m.index;
        s_cached[i].m = m;
        s_cached[i].recvTick = s_tick;
        return;
    }
}

void still_the_body(fopAc_ac_c* actor) {
    actor->speed.set(0.0f, 0.0f, 0.0f);
    actor->speedF = 0.0f;
    actor->gravity = 0.0f;
    actor->maxFallSpeed = 0.0f;
    actor->old.pos = actor->current.pos;
}

bool host_stalled() {
    const uint8_t owner = s_fightOwner;
    if (owner == kCoopNoPlayer || owner == coop_net_local_id()) return false;

    return coop_net_ticks_since_player(owner) > 15 || coop_player_paused(owner);
}

void reapply_cached(BossList& list, bool afterExecute) {

    if (s_fightOver) return;
    for (int i = 0; i < kMaxBossActors; ++i) {
        if (!s_cached[i].used) continue;
        fopAc_ac_c* actor = find(list, s_cached[i].kind, s_cached[i].index);
        if (actor == nullptr) continue;
        const MsgBossActor& m = s_cached[i].m;
        f32 ahead = static_cast<f32>(s_tick - s_cached[i].recvTick) + (afterExecute ? 1.0f : 0.0f);
        if (ahead > 6.0f) ahead = 6.0f;
        s_morfAhead = ahead;
        switch (m.kind) {
        case kBossKindDiababa: write_diababa(actor, m); break;
        case kBossKindTentacle: write_tentacle(actor, m); break;
        case kBossKindOok: write_ook(actor, m); break;
        case kBossKindHelper: write_helper(actor, m); break;
        case kBossKindOokBoomerang: write_ook_boomerang(actor, m); break;
        default: s_morfAhead = 0.0f; continue;
        }
        s_morfAhead = 0.0f;

        if (!kind_has_flag(m.kind, kBossSelfMoving) || host_stalled()) still_the_body(actor);
    }
}

const s16 kBqActionAttack = 2;

const s16 kBhActionAttack1 = 5;
const s16 kBhActionBAttack1 = 21;

bool action_is_attack(uint8_t kind, s16 action) {
    if (kind == kBossKindDiababa) return action == kBqActionAttack;
    if (kind == kBossKindTentacle) {
        return action == kBhActionAttack1 || action == kBhActionBAttack1;
    }
    return false;
}

bool read_action(fopAc_ac_c* actor, uint8_t kind, s16& out) {
    if (actor == nullptr) return false;
    if (kind == kBossKindDiababa) {
        out = reinterpret_cast<b_bq_class*>(actor)->mAction;
        return true;
    }
    if (kind == kBossKindTentacle) {
        out = reinterpret_cast<b_bh_class*>(actor)->mAction;
        return true;
    }
    return false;
}

struct AttackerTarget {
    bool used = false;
    uint8_t slot = 0;
    s16 lastAction = -1;
    bool attacking = false;

    uint8_t targetPlayer = kCoopHostId;
};
AttackerTarget s_attacker[8];

AttackerTarget* attacker_slot(uint8_t kind, uint8_t index) {
    const uint8_t slot = static_cast<uint8_t>((kind << 4) | (index & 0xF));
    for (int i = 0; i < 8; ++i) {
        if (s_attacker[i].used && s_attacker[i].slot == slot) return &s_attacker[i];
    }
    for (int i = 0; i < 8; ++i) {
        if (s_attacker[i].used) continue;
        s_attacker[i].used = true;
        s_attacker[i].slot = slot;
        s_attacker[i].lastAction = -1;
        s_attacker[i].attacking = false;
        s_attacker[i].targetPlayer = kCoopHostId;
        return &s_attacker[i];
    }
    return nullptr;
}

void reset_attackers() {
    for (int i = 0; i < 8; ++i) s_attacker[i] = AttackerTarget{};
}

bool boss_player_body(uint8_t playerId, cXyz& out) {
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

uint8_t random_player_in_fight() {
    uint8_t ids[kCoopMaxPlayers];
    int n = 0;
    cXyz p;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (coop_net_player_present(id) && boss_player_body(id, p)) ids[n++] = id;
    }
    if (n == 0) return coop_net_local_id();
    const int pick = static_cast<int>(cM_rndF(static_cast<f32>(n)));
    return ids[pick < n ? pick : n - 1];
}

uint8_t nearest_player_to(const cXyz& from) {
    uint8_t best = coop_net_local_id();
    f32 bestD = 0.0f;
    bool any = false;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        cXyz p;
        if (!coop_net_player_present(id) || !boss_player_body(id, p)) continue;
        const cXyz d(p.x - from.x, p.y - from.y, p.z - from.z);
        const f32 len = d.abs();
        if (!any || len < bestD) {
            best = id;
            bestD = len;
            any = true;
        }
    }
    return best;
}

DEFINE_HOOK(&fpcEx_Execute, BossExecuteHook);

struct BossLie {
    fopAc_ac_c* player = nullptr;
    bool active = false;
    cXyz savedCur, savedEye, savedOld;
    csXyz savedShape, savedCurAngle;
};
BossLie s_lie;
int s_lieDepth = 0;
fpc_ProcID s_bossProcs[kMaxBossActors];

uint8_t s_bossProcShow[kMaxBossActors] = {};
int s_bossProcCount = 0;

uint8_t boss_proc_shows(fpc_ProcID id) {
    for (int i = 0; i < s_bossProcCount; ++i) {
        if (s_bossProcs[i] == id) return s_bossProcShow[i];
    }
    return kCoopNoPlayer;
}

void refresh_boss_procs(BossList& list) {
    s_bossProcCount = 0;
    for (int i = 0; i < list.count && s_bossProcCount < kMaxBossActors; ++i) {

        if (kind_has_flag(list.kinds[i], kBossNoRetarget)) continue;
        s16 action = -1;
        if (!read_action(list.actors[i], list.kinds[i], action)) continue;
        AttackerTarget* at = attacker_slot(list.kinds[i], list.indices[i]);
        if (at == nullptr) continue;

        const bool nowAttacking = action_is_attack(list.kinds[i], action);
        if (nowAttacking && !at->attacking) {
            at->targetPlayer = random_player_in_fight();
            coop_log::info("coop_mod: [BOSS] {} {} winding up at {}",
                list.kinds[i] == kBossKindDiababa ? "the head" : "a tentacle",
                static_cast<int>(list.indices[i]),
                static_cast<int>(at->targetPlayer));
        }
        at->attacking = nowAttacking;
        at->lastAction = action;

        const uint8_t show = nowAttacking
                                 ? at->targetPlayer
                                 : nearest_player_to(list.actors[i]->current.pos);

        if (show != coop_net_local_id()) {
            s_bossProcs[s_bossProcCount] = fopAcM_GetID(list.actors[i]);
            s_bossProcShow[s_bossProcCount] = show;
            ++s_bossProcCount;
        }
    }
}

HookAction on_boss_execute_pre(ModContext*, void* args, void*, void*) {
    if (s_lieDepth++ != 0) return HOOK_CONTINUE;
    if (s_bossProcCount == 0 || !i_run_the_fight()) return HOOK_CONTINUE;
    auto* proc = mods::arg<base_process_class*>(args, 0);
    if (proc == nullptr) return HOOK_CONTINUE;
    const uint8_t show = boss_proc_shows(proc->id);
    if (show == kCoopNoPlayer) return HOOK_CONTINUE;
    f32 px = 0.0f, py = 0.0f, pz = 0.0f;
    s16 angleY = 0;
    if (!puppet_hook_get_pose_of(show, &px, &py, &pz, &angleY, nullptr, nullptr)) {
        return HOOK_CONTINUE;
    }
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) return HOOK_CONTINUE;

    s_lie.player = player;
    s_lie.savedCur = player->current.pos;
    s_lie.savedOld = player->old.pos;
    s_lie.savedEye = player->eyePos;
    s_lie.savedShape = player->shape_angle;
    s_lie.savedCurAngle = player->current.angle;
    const cXyz eyeOff(player->eyePos.x - player->current.pos.x,
        player->eyePos.y - player->current.pos.y, player->eyePos.z - player->current.pos.z);
    player->current.pos.set(px, py, pz);
    player->old.pos.set(px, py, pz);
    player->eyePos.set(px + eyeOff.x, py + eyeOff.y, pz + eyeOff.z);
    player->shape_angle.y = angleY;
    player->current.angle.y = angleY;
    s_lie.active = true;
    return HOOK_CONTINUE;
}

void on_boss_execute_post(ModContext*, void*, void*, void*) {
    if (--s_lieDepth != 0) return;
    if (!s_lie.active || s_lie.player == nullptr) {
        s_lie.active = false;
        return;
    }
    fopAc_ac_c* p = s_lie.player;
    p->current.pos = s_lie.savedCur;
    p->old.pos = s_lie.savedOld;
    p->eyePos = s_lie.savedEye;
    p->shape_angle = s_lie.savedShape;
    p->current.angle = s_lie.savedCurAngle;
    s_lie.active = false;
    s_lie.player = nullptr;
}

DEFINE_HOOK(&dCcS::Move, BossCollisionHook);

const int16_t kProcNbomb = 0x221;

void report_hit(uint8_t kind, uint8_t index, uint8_t collider, dCcD_GObjInf* tg) {
    cCcD_Obj* atObj = tg->GetTgHitObj();
    if (atObj == nullptr) return;
    dCcD_GObjInf* atInf = dCcD_GetGObjInf(atObj);
    if (atInf == nullptr) return;
    fopAc_ac_c* attacker = tg->GetTgHitAc();

    MsgBossHit msg{};
    msg.kind = kind;
    msg.index = index;
    msg.collider = collider;
    msg.attackerName = attacker != nullptr ? fopAcM_GetName(attacker) : -1;
    msg.atType = atInf->GetAtType();
    msg.atp = static_cast<uint8_t>(atInf->GetAtAtp());
    msg.spl = static_cast<uint8_t>(atInf->GetAtSpl());
    msg.mtrl = atInf->GetAtMtrl();
    daAlink_c* alink = daAlink_getAlinkActorClass();
    msg.cutJump = (alink != nullptr &&
                   daPy_getPlayerActorClass()->getCutType() == daPy_py_c::CUT_TYPE_JUMP)
                      ? 1
                      : 0;
    msg.cutCount = alink != nullptr
                       ? static_cast<uint8_t>(daPy_getPlayerActorClass()->getCutCount())
                       : 0;
    msg.fastCut = (alink != nullptr && daPy_getPlayerActorClass()->checkFastSwordCut()) ? 1 : 0;
    coop_net_send(kMsgBossHit, &msg, sizeof(msg));
    coop_log::info("coop_mod: [BOSS] we hit kind={} collider={} attacker={} atp={} - telling the host",
        static_cast<int>(kind), static_cast<int>(collider), static_cast<int>(msg.attackerName),
        static_cast<int>(msg.atp));
}

const uint32_t kHitGraceTicks = 30;

struct HitGrace {
    bool used = false;
    uint8_t kind = 0;
    uint8_t index = 0;
    uint32_t until = 0;
};
HitGrace s_hitGrace[kMaxBossActors];

void note_our_hit(uint8_t kind, uint8_t index) {
    for (int i = 0; i < kMaxBossActors; ++i) {
        if (s_hitGrace[i].used && s_hitGrace[i].kind == kind && s_hitGrace[i].index == index) {
            s_hitGrace[i].until = s_tick + kHitGraceTicks;
            return;
        }
    }
    for (int i = 0; i < kMaxBossActors; ++i) {
        if (s_hitGrace[i].used) continue;
        s_hitGrace[i] = HitGrace{true, kind, index, s_tick + kHitGraceTicks};
        return;
    }
}

bool in_hit_grace(uint8_t kind, uint8_t index) {
    for (int i = 0; i < kMaxBossActors; ++i) {
        if (!s_hitGrace[i].used || s_hitGrace[i].kind != kind || s_hitGrace[i].index != index) {
            continue;
        }
        if (s_tick <= s_hitGrace[i].until) return true;
        s_hitGrace[i] = HitGrace{};
        return false;
    }
    return false;
}

void capture_our_hits() {
    BossList list;
    collect(list);
    for (int i = 0; i < list.count; ++i) {
        if (list.kinds[i] == kBossKindDiababa) {
            auto* bq = reinterpret_cast<b_bq_class*>(list.actors[i]);
            if (bq->mCcSph.ChkTgHit()) {
                report_hit(kBossKindDiababa, list.indices[i], kBossColliderBody, &bq->mCcSph);
                note_our_hit(kBossKindDiababa, list.indices[i]);
            }
            if (bq->mCcCoreSph.ChkTgHit()) {
                report_hit(kBossKindDiababa, list.indices[i], kBossColliderCore, &bq->mCcCoreSph);
                note_our_hit(kBossKindDiababa, list.indices[i]);
            }
        } else if (list.kinds[i] == kBossKindTentacle) {
            auto* bh = reinterpret_cast<b_bh_class*>(list.actors[i]);
            if (bh->mTgSph.ChkTgHit()) {
                report_hit(kBossKindTentacle, list.indices[i], kBossColliderTentacle, &bh->mTgSph);
                note_our_hit(kBossKindTentacle, list.indices[i]);
            }
        } else if (list.kinds[i] == kBossKindOok) {

            auto* ook = reinterpret_cast<e_mk_class*>(list.actors[i]);
            if (ook->tgSph.ChkTgHit()) {
                report_hit(kBossKindOok, list.indices[i], kBossColliderOok, &ook->tgSph);
                note_our_hit(kBossKindOok, list.indices[i]);
            }
        }
    }
}

const int kBckBqNoDamage = 0x11;

void play_boss_anm(b_bq_class* bq, int anmID, f32 morf, f32 speed) {
    if (bq == nullptr || bq->mpMorf == nullptr) return;
    auto* bck = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes("B_bq", anmID));
    if (bck == nullptr) return;
    bq->mpMorf->setAnm(bck, J3DFrameCtrl::EMode_NONE, morf, speed, 0.0f, -1.0f);
    bq->mAnmID = anmID;
}

void apply_remote_hit(const MsgBossHit& msg) {
    BossList list;
    collect(list);
    fopAc_ac_c* actor = find(list, msg.kind, msg.index);
    if (actor == nullptr) return;

    if (msg.kind == kBossKindDiababa) {
        auto* bq = reinterpret_cast<b_bq_class*>(actor);
        if (msg.collider == kBossColliderBody) {

            if (bq->mAction == 3  ) return;
            if (msg.attackerName != kProcNbomb) {

                play_boss_anm(bq, kBckBqNoDamage, 3.0f, 1.0f);
                bq->field_0x6de = 10;
                return;
            }
            bq->mAction = 3;
            bq->mMode = 0;
            bq->field_0x6de = 30;
            bq->field_0x11fc++;
            dComIfGs_onOneZoneSwitch(8, -1);
            coop_log::info("coop_mod: [BOSS] their bomb stunned Diababa");
            return;
        }
        if (msg.collider == kBossColliderCore) {

            if (bq->mAction != 3 || bq->field_0x6de != 0) return;
            bq->field_0x6de = msg.cutJump ? 3 : 6;
            int hp = actor->health - power_class_to_damage(msg.atp);
            if (hp < 0) hp = 0;
            actor->health = static_cast<s16>(hp);

            const bool finished = hp <= 0 || msg.cutCount >= 4;
            bq->mMode = finished ? 20 : 10;
            if (finished && msg.fastCut != 0 && bq->mDamageBackCount >= 2) {

                bq->mDamageBackCount++;
                bq->mAction = kBqActionEnd;
                bq->mMode = 0;
                bq->mDemoMode = kBqDemoDeath;
                coop_log::info("coop_mod: [BOSS] their finishing spin on the core - Diababa is done");
                return;
            }
            if (finished) {
                bq->field_0x6de = 100;
            } else if (msg.cutCount != 0 && bq->mTimers[0] < 30) {
                bq->mTimers[0] = 30;
            }
            coop_log::info("coop_mod: [BOSS] their hit on the core: hp now {}", hp);
            return;
        }
    } else if (msg.kind == kBossKindTentacle) {
        int hp = actor->health - power_class_to_damage(msg.atp);
        if (hp < 0) hp = 0;
        actor->health = static_cast<s16>(hp);
        coop_log::info("coop_mod: [BOSS] their hit on a tentacle: hp now {}", hp);
    } else if (msg.kind == kBossKindOok) {

        auto* ook = reinterpret_cast<e_mk_class*>(actor);
        if (ook->invulnerabilityTimer != 0) return;
        if (ook->action < 9) return;
        int hp = actor->health - power_class_to_damage(msg.atp);
        if (hp < 0) hp = 0;
        actor->health = static_cast<s16>(hp);
        if (hp <= 0) {

            ook->action = e_mk_class::ACT_E_DEMO;
            ook->mode = 0;
            ook->invulnerabilityTimer = 20000;

            mDoAud_subBgmStop();
            coop_log::info("coop_mod: [BOSS] their blow killed Ook - starting his death demo");
        } else {
            ook->action = e_mk_class::ACT_DAMAGE;
            ook->mode = 0;
            ook->invulnerabilityTimer = 10;
            coop_log::info("coop_mod: [BOSS] their hit on Ook: hp now {}", hp);
        }
    }
}

void on_boss_collision_post(ModContext*, void*, void*, void*) {
    if (!boss_sync_on() || i_run_the_fight()) return;
    capture_our_hits();
}

HookAction on_boss_collision_pre(ModContext*, void*, void*, void*) {
    if (!boss_sync_on() || i_run_the_fight()) return HOOK_CONTINUE;
    BossList list;
    collect(list);
    reapply_cached(list, true);
    if (!s_fightOver && !s_hostInDemo) apply_cached_stems(list);
    return HOOK_CONTINUE;
}

struct CachedStem {
    bool used = false;
    uint8_t index = 0;
    MsgBossStem m{};
};
CachedStem s_cachedStem[4];

void send_stems(BossList& list) {
    for (int i = 0; i < list.count; ++i) {
        if (list.kinds[i] != kBossKindTentacle) continue;
        auto* t = reinterpret_cast<b_bh_class*>(list.actors[i]);
        MsgBossStem m{};
        m.index = list.indices[i];
        for (int seg = 0; seg < 18; ++seg) {
            m.pos[seg][0] = t->field_0x6bc[seg].x;
            m.pos[seg][1] = t->field_0x6bc[seg].y;
            m.pos[seg][2] = t->field_0x6bc[seg].z;
            m.angle[seg][0] = t->field_0x794[seg].x;
            m.angle[seg][1] = t->field_0x794[seg].y;
            m.angle[seg][2] = t->field_0x794[seg].z;
        }
        coop_net_send(kMsgBossStem, &m, sizeof(m));
    }
}

void apply_cached_stems(BossList& list) {
    for (int i = 0; i < 4; ++i) {
        if (!s_cachedStem[i].used) continue;
        fopAc_ac_c* actor = find(list, kBossKindTentacle, s_cachedStem[i].index);
        if (actor == nullptr) continue;
        auto* t = reinterpret_cast<b_bh_class*>(actor);
        const MsgBossStem& m = s_cachedStem[i].m;
        for (int seg = 0; seg < 18; ++seg) {
            t->field_0x6bc[seg].set(m.pos[seg][0], m.pos[seg][1], m.pos[seg][2]);
            t->field_0x794[seg].x = m.angle[seg][0];
            t->field_0x794[seg].y = m.angle[seg][1];
            t->field_0x794[seg].z = m.angle[seg][2];
        }
    }
}

void cache_stem(const MsgBossStem& m) {
    for (int i = 0; i < 4; ++i) {
        if (s_cachedStem[i].used && s_cachedStem[i].index == m.index) {
            s_cachedStem[i].m = m;
            return;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (s_cachedStem[i].used) continue;
        s_cachedStem[i].used = true;
        s_cachedStem[i].index = m.index;
        s_cachedStem[i].m = m;
        return;
    }
}

void send_state(BossList& list) {
    uint8_t buffer[1 + kMaxBossActors * sizeof(MsgBossActor)];
    int count = 0;

    for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < list.count && count < kMaxBossActors; ++i) {
        const bool isBoss = list.kinds[i] == kBossKindDiababa;
        if ((pass == 0) != isBoss) continue;
        MsgBossActor m{};
        m.kind = list.kinds[i];
        m.index = list.indices[i];
        switch (list.kinds[i]) {
        case kBossKindDiababa: read_diababa(list.actors[i], m); break;
        case kBossKindTentacle: read_tentacle(list.actors[i], m); break;
        case kBossKindOok: read_ook(list.actors[i], m); break;
        case kBossKindHelper: read_helper(list.actors[i], m); break;
        case kBossKindOokBoomerang: read_ook_boomerang(list.actors[i], m); break;
        default: continue;
        }
        std::memcpy(buffer + 1 + count * sizeof(m), &m, sizeof(m));
        ++count;
    }

    if (count == 0 && s_hostFightOver && s_fightOverSends > 0) {
        --s_fightOverSends;
        MsgBossActor m{};
        m.kind = kBossKindDiababa;
        m.index = 0;
        m.fightOver = 1;
        std::memcpy(buffer + 1, &m, sizeof(m));
        buffer[0] = 1;
        coop_net_send(kMsgBossState, buffer, 1 + sizeof(m));
        return;
    }
    s_diagSent = count;
    if (count == 0) return;
    buffer[0] = static_cast<uint8_t>(count);
    coop_net_send(kMsgBossState, buffer, 1 + count * sizeof(MsgBossActor));
}

class BossWaitDlst : public dDlst_base_c {
public:
    virtual void draw() {
        if (!mVisible) return;
        JUTFont* font = mDoExt_getMesgFont();
        if (font == nullptr) return;
        J2DOrthoGraph ortho(0.0f, 0.0f, static_cast<f32>(FB_WIDTH), static_cast<f32>(FB_HEIGHT),
            -1.0f, 1.0f);
        ortho.setOrtho(mDoGph_gInf_c::getMinXF(), mDoGph_gInf_c::getMinYF(),
            mDoGph_gInf_c::getWidthF(), mDoGph_gInf_c::getHeightF(), -1.0f, 1.0f);
        ortho.setPort();
        font->setGX();

        const f32 midX = mDoGph_gInf_c::getMinXF() + mDoGph_gInf_c::getWidthF() * 0.5f;
        const f32 midY = mDoGph_gInf_c::getMinYF() + mDoGph_gInf_c::getHeightF() * 0.5f;
        draw_line(font, mTop, 26.0f, midX, midY - 30.0f, 255);
        draw_line(font, mBottom, 18.0f, midX, midY + 24.0f, 210);
    }

    static void draw_line(JUTFont* font, const char* text, f32 cell, f32 midX, f32 y, u8 alpha) {
        if (text[0] == '\0') return;
        f32 width = 0.0f;
        const f32 cellWidth = static_cast<f32>(font->getCellWidth());
        for (const char* c = text; *c != '\0'; ++c) {
            const f32 adv = font->isFixed() ? static_cast<f32>(font->getFixedWidth())
                                            : static_cast<f32>(font->getWidth(static_cast<u8>(*c)));
            width += cellWidth > 0.0f ? adv * (cell / cellWidth) : cell * 0.6f;
        }
        const f32 x = midX - width * 0.5f;
        const f32 shadow = cell * 0.09f;
        font->setCharColor(JUtility::TColor(0, 0, 0, static_cast<u8>(alpha * 0.75f)));
        font->drawString_scale(x + shadow, y + shadow, cell, cell, text, true);
        font->setCharColor(JUtility::TColor(255, 255, 255, alpha));
        font->drawString_scale(x, y, cell, cell, text, true);
    }

    char mTop[64] = {};
    char mBottom[64] = {};
    bool mVisible = false;
};
BossWaitDlst s_waitDlst;

void queue_wait_popup(bool visible, const char* top, const char* bottom) {
    s_waitDlst.mVisible = visible;
    if (!visible) return;
    std::strncpy(s_waitDlst.mTop, top, sizeof(s_waitDlst.mTop) - 1);
    s_waitDlst.mTop[sizeof(s_waitDlst.mTop) - 1] = '\0';
    std::strncpy(s_waitDlst.mBottom, bottom, sizeof(s_waitDlst.mBottom) - 1);
    s_waitDlst.mBottom[sizeof(s_waitDlst.mBottom) - 1] = '\0';
    dComIfGd_set2DXlu(&s_waitDlst);
}

void boss_go_anyway();

ConfigVarHandle s_waitVar = 0;

uint16_t s_remoteReady = 0;
uint32_t s_readyStamp[kCoopMaxPlayers] = {};

char s_readyStage[kCoopMaxPlayers][8] = {};
int8_t s_readyRoom[kCoopMaxPlayers] = {};
const uint32_t kReadyStaleTicks = 200;

bool ready_for_our_fight(uint8_t id);

bool local_ready_now();

bool fight_already_underway(BossList& list) {
    for (int i = 0; i < list.count; ++i) {
        if (list.kinds[i] != kBossKindDiababa) continue;
        if (reinterpret_cast<b_bq_class*>(list.actors[i])->mAction != 0) return true;
    }
    for (int i = 0; i < kMaxBossActors; ++i) {
        if (!s_cached[i].used || s_cached[i].kind != kBossKindDiababa) continue;
        if (s_cached[i].m.action != 0) return true;
    }
    return false;
}

int others_ready_count();

bool player_ready(uint8_t id) {
    if (id == coop_net_local_id()) return local_ready_now();
    if ((s_remoteReady & (1u << id)) == 0) return false;

    if (!ready_for_our_fight(id)) return false;
    return s_tick - s_readyStamp[id] <= kReadyStaleTicks;
}

int players_still_coming() {
    const uint16_t roster = coop_net_roster();
    int missing = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if ((roster & (1u << i)) == 0) continue;
        if (!player_ready(id)) ++missing;
    }
    return missing;
}

int others_ready_count() {
    const uint16_t roster = coop_net_roster();
    int n = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || (roster & (1u << i)) == 0) continue;
        if (player_ready(id)) ++n;
    }
    return n;
}

int others_still_coming() {
    const uint16_t roster = coop_net_roster();
    int missing = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || (roster & (1u << i)) == 0) continue;
        if (!player_ready(id)) ++missing;
    }
    return missing;
}
int s_waitTicks = 0;
const int kMaxWaitTicks = 30 * 60;
bool s_goAnyway = false;
bool s_wasWaiting = false;
int s_bossRoom = -1;
char s_bossStage[8] = {};

bool ready_for_our_fight(uint8_t id) {
    if (id >= kCoopMaxPlayers) return false;
    if (s_bossRoom < 0) return false;
    if (s_readyRoom[id] != static_cast<int8_t>(s_bossRoom)) return false;
    return std::memcmp(s_readyStage[id], s_bossStage, 8) == 0;
}
uint32_t s_lastReadySent = 0;
bool s_sentInRoom = false;
bool s_sentReady = false;
char s_waitText[96] = {};

extern s16 s_heldDemoMode;

int s_roomTicks = 0;
int s_eventTicks = 0;
bool s_sawEvent = false;
bool s_readyLatched = false;
const int kEntryGraceTicks = 90;
const int kReadyLatchCeiling = 60 * 10;
const int kEventWedgedTicks = 60 * 3;

void update_ready_latch() {
    if (s_readyLatched) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();

    const bool atDoor = alink != nullptr && (alink->mProcID == daAlink_c::PROC_DOOR_OPEN ||
                                             alink->mProcID == daAlink_c::PROC_BOSS_ATN_WAIT);
    const bool noEvent = alink != nullptr && alink->checkEventRun() == FALSE && !atDoor;
    if (noEvent) {
        s_eventTicks = 0;
    } else {
        ++s_eventTicks;
    }

    const bool wedged = s_heldDemoMode >= 0 && s_eventTicks > kEventWedgedTicks;

    if (!noEvent) s_sawEvent = true;
    const bool settled = noEvent && (s_sawEvent || s_roomTicks > kEntryGraceTicks);

    if (settled || wedged || s_roomTicks > kReadyLatchCeiling) {
        s_readyLatched = true;
        coop_log::info("coop_mod: [BOSS] through the entry cutscene ({}) - ready",
            settled ? (s_sawEvent ? "the entry cutscene ended" : "there was no entry cutscene")
                    : (wedged ? "the intro is wedged behind our own hold" : "waited long enough"));
    }
}

bool local_ready_now() {
    return s_sentInRoom && s_readyLatched;
}

bool wait_enabled() {
    return coop_session(kSessBossWait, cfg_bool(s_waitVar, false));
}

struct BeatenBoss {
    char stage[8];
    int8_t room;
};
const int kMaxBeatenBosses = 16;
BeatenBoss s_beaten[kMaxBeatenBosses];
int s_beatenCount = 0;

bool boss_already_beaten(const char* stage, int room) {
    if (stage == nullptr || room < 0) return false;
    for (int i = 0; i < s_beatenCount; ++i) {
        if (s_beaten[i].room == static_cast<int8_t>(room) &&
            std::memcmp(s_beaten[i].stage, stage, 8) == 0) {
            return true;
        }
    }
    return false;
}

void note_boss_beaten(const char* stage, int room) {
    if (stage == nullptr || room < 0 || boss_already_beaten(stage, room)) return;
    if (s_beatenCount >= kMaxBeatenBosses) return;
    std::memcpy(s_beaten[s_beatenCount].stage, stage, 8);
    s_beaten[s_beatenCount].room = static_cast<int8_t>(room);
    ++s_beatenCount;
    coop_log::info("coop_mod: [BOSS] {}:{} is beaten - the door wait will not run here again",
        stage, room);
}

bool local_in_boss_room(BossList& list) {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return false;
    const char* stage = dComIfGp_getStartStageName();
    if (stage == nullptr) return false;

    for (int i = 0; i < list.count; ++i) {
        if (!kind_holds_a_fight(list.kinds[i])) continue;
        s_bossRoom = fopAcM_GetRoomNo(list.actors[i]);
        std::memcpy(s_bossStage, stage, 8);
        break;
    }
    if (s_bossRoom < 0 || std::memcmp(s_bossStage, stage, 8) != 0) return false;
    return fopAcM_GetRoomNo(alink) == s_bossRoom;
}

void send_ready(bool inRoom, bool goAnyway) {
    MsgBossReady msg{};
    msg.room = static_cast<int8_t>(s_bossRoom);
    msg.inRoom = inRoom ? 1 : 0;
    msg.goAnyway = goAnyway ? 1 : 0;

    const char* stage = dComIfGp_getStartStageName();
    if (stage != nullptr) std::strncpy(msg.stage, stage, sizeof(msg.stage));
    coop_net_send(kMsgBossReady, &msg, sizeof(msg));
}

s16 s_heldAction = -1;
s16 s_heldDemoMode = -1;
s16 s_heldDemoTimer = -1;

bool kind_holds_a_fight(uint8_t kind) {
    return kind_has_flag(kind, kBossHoldsDoor);
}

bool s_holdingOok = false;

s16 s_heldOokDemo = -1;
s16 s_heldOokSubDemo = -1;

const s16 kOokHoldTimer = 60;

void hold_fight(BossList& list) {
    for (int i = 0; i < list.count; ++i) {
        if (list.kinds[i] == kBossKindOok) {
            auto* ook = reinterpret_cast<e_mk_class*>(list.actors[i]);

            if (ook->action == e_mk_class::ACT_S_DEMO) {
                if (ook->demoMode != e_mk_class::DEMO_MODE_NONE && s_heldOokDemo < 0) {
                    s_heldOokDemo = ook->demoMode;
                    s_heldOokSubDemo = ook->demoSubMode;
                    coop_log::info("coop_mod: [BOSS] caught Ook's intro trying to start "
                                    "(demoMode={} sub={}) - holding it until everyone is here",
                        s_heldOokDemo, s_heldOokSubDemo);
                }
                if (s_heldOokDemo >= 0) {
                    ook->demoMode = e_mk_class::DEMO_MODE_NONE;
                    ook->demoSubMode = 0;
                }
                if (ook->mode == 1 && ook->timer[0] < kOokHoldTimer) {
                    ook->timer[0] = kOokHoldTimer;
                }
                s_holdingOok = true;
            }
            continue;
        }
        if (list.kinds[i] != kBossKindDiababa) continue;
        auto* bq = reinterpret_cast<b_bq_class*>(list.actors[i]);

        if (bq->mDemoMode != 0 && s_heldDemoMode < 0) {
            s_heldDemoMode = bq->mDemoMode;
            s_heldDemoTimer = bq->mDemoModeTimer;
            coop_log::info("coop_mod: [BOSS] caught the intro trying to start (demoMode={}) - "
                            "holding it until everyone is here", s_heldDemoMode);
        }
        if (bq->mAction != 0 && s_heldAction < 0) s_heldAction = bq->mAction;
        bq->mAction = 0;
        bq->mDemoMode = 0;
        bq->mDemoModeTimer = 0;
    }
}

bool release_fight(BossList& list) {
    const bool nothingHeld = s_heldAction < 0 && s_heldDemoMode < 0 && !s_holdingOok &&
                             s_heldOokDemo < 0;
    bool handedBack = false;
    for (int i = 0; i < list.count; ++i) {
        if (list.kinds[i] == kBossKindOok) {
            if (s_holdingOok) {
                auto* ook = reinterpret_cast<e_mk_class*>(list.actors[i]);

                if (s_heldOokDemo >= 0) {
                    ook->demoMode = static_cast<s16>(s_heldOokDemo);
                    ook->demoSubMode = static_cast<s16>(s_heldOokSubDemo);
                }
                if (ook->action == e_mk_class::ACT_S_DEMO && ook->mode == 1) ook->timer[0] = 1;
                coop_log::info("coop_mod: [BOSS] released Ook - handing back demoMode={} sub={}",
                    s_heldOokDemo, s_heldOokSubDemo);
                s_heldOokDemo = -1;
                s_heldOokSubDemo = -1;
                s_holdingOok = false;
            }
            handedBack = true;
            continue;
        }
        if (list.kinds[i] != kBossKindDiababa) continue;
        auto* bq = reinterpret_cast<b_bq_class*>(list.actors[i]);
        if (s_heldDemoMode >= 0) {
            bq->mDemoMode = s_heldDemoMode;
            bq->mDemoModeTimer = s_heldDemoTimer;
        }
        if (s_heldAction >= 0) bq->mAction = s_heldAction;
        coop_log::info("coop_mod: [BOSS] released - handing back action={} demoMode={}",
            s_heldAction, s_heldDemoMode);
        handedBack = true;
    }
    if (!handedBack && !nothingHeld) return false;
    s_heldAction = -1;
    s_heldDemoMode = -1;
    s_heldDemoTimer = -1;
    s_holdingOok = false;
    s_heldOokDemo = -1;
    s_heldOokSubDemo = -1;
    s_ookHandedOver = false;
    return true;
}

void discard_held_fight() {
    s_heldAction = -1;
    s_heldDemoMode = -1;
    s_heldDemoTimer = -1;
    s_holdingOok = false;
    s_heldOokDemo = -1;
    s_heldOokSubDemo = -1;
    s_ookHandedOver = false;
}

void update_wait(BossList& list) {
    const bool here = local_in_boss_room(list);
    static bool announced = false;
    if (here && !announced) {
        announced = true;
        coop_log::info("coop_mod: [BOSS] walked into the boss room (room {}) - waitEnabled={} "
                        "peerInRoom={} goAnyway={}",
            s_bossRoom, wait_enabled() ? 1 : 0, players_still_coming(), s_goAnyway ? 1 : 0);
    }
    if (!here) announced = false;

    if (!here) {

        const bool wasInRoom = s_sentInRoom;
        if (wasInRoom) {
            s_sentInRoom = false;
            s_sentReady = false;
            send_ready(false, false);
        }
        if (s_wasWaiting) release_fight(list);
        s_wasWaiting = false;
        s_waitTicks = 0;
        s_roomTicks = 0;
        s_eventTicks = 0;
        s_sawEvent = false;
        s_readyLatched = false;

        if (wasInRoom) {
            boss_reset_fight_state();
            discard_held_fight();
        }

        if (s_goAnyway) {
            s_goAnyway = false;
            if (wasInRoom) {
                coop_log::info("coop_mod: [BOSS] left the boss room - the wait is armed again");
            }
        }
        s_waitText[0] = '\0';
        queue_wait_popup(false, "", "");
        return;
    }

    ++s_roomTicks;
    update_ready_latch();

    const bool ready = s_readyLatched;
    if (ready != s_sentReady || !s_sentInRoom || s_tick - s_lastReadySent > 60) {
        s_sentInRoom = true;
        s_sentReady = ready;
        s_lastReadySent = s_tick;

        send_ready(ready, s_goAnyway);
    }

    if (s_wasWaiting && ++s_waitTicks > kMaxWaitTicks && !s_goAnyway) {
        s_goAnyway = true;
        coop_log::info("coop_mod: [BOSS] wait timed out after {} ticks - releasing rather than "
                        "leaving the room stuck", s_waitTicks);
    }

    const bool underway = fight_already_underway(list) && others_ready_count() >= 1;

    const int missing = players_still_coming();
    const int missingShown = others_still_coming();

    const bool retired = s_fightOver ||
                         boss_already_beaten(dComIfGp_getStartStageName(), s_bossRoom);
    const bool waiting =
        wait_enabled() && !s_goAnyway && missing > 0 && !underway && !retired;
    if (waiting) {
        hold_fight(list);

        if (missingShown == 0) {
            s_waitText[0] = 0;
            queue_wait_popup(false, "", "");
        } else {

            std::snprintf(s_waitText, sizeof(s_waitText), "Waiting for %d %s", missingShown,
                missingShown == 1 ? "Player" : "Players");
            queue_wait_popup(true, s_waitText, "Press  D-Pad Down  to continue anyway");
        }

        interface_of_controller_pad& cpad = mDoCPd_c::getCpadInfo(PAD_1);
        const bool pressed = (cpad.mPressedButtonFlags & PAD_BUTTON_DOWN) != 0;
        cpad.mPressedButtonFlags &= ~PAD_BUTTON_DOWN;
        cpad.mButtonFlags &= ~PAD_BUTTON_DOWN;
        if (pressed) boss_go_anyway();
        if (!s_wasWaiting) {
            s_wasWaiting = true;
            coop_log::info("coop_mod: [BOSS] holding the fight - {}", s_waitText);
        }
    } else if (s_wasWaiting) {
        queue_wait_popup(false, "", "");
        release_fight(list);
        s_waitTicks = 0;
        s_wasWaiting = false;
        s_waitText[0] = '\0';
        coop_log::info("coop_mod: [BOSS] wait over ({}) - starting the fight",
            s_goAnyway ? "continue anyway" : "everyone is here");
    } else {
        queue_wait_popup(false, "", "");
    }
}

void boss_go_anyway() {
    if (!s_wasWaiting) return;
    s_goAnyway = true;
    send_ready(s_sentInRoom, true);
    coop_log::info("coop_mod: [BOSS] go anyway pressed");
}

void boss_reset_fight_state() {
    s_fightOver = false;
    s_hostInDemo = false;
    s_hostFightOver = false;
    s_bossSeen = false;
    s_fightOverSends = 0;
    for (int i = 0; i < kMaxBossActors; ++i) s_cached[i] = CachedState{};
    for (int i = 0; i < 4; ++i) s_cachedStem[i] = CachedStem{};
    reset_attackers();

    boss_reset_eat_watch();
}

}

bool boss_is_supported_procname(s16 procName) {
    return kind_of(procName) != kBossKindNone || procName == kBossProcOokBoomerang;
}

void boss_init() {
    const ModResult pre = mods::hook::add_pre<BossCollisionHook>(on_boss_collision_pre);
    const ModResult post = mods::hook::add_post<BossCollisionHook>(on_boss_collision_post);
    const ModResult exPre = mods::hook::add_pre<BossExecuteHook>(on_boss_execute_pre);
    const ModResult exPost = mods::hook::add_post<BossExecuteHook>(on_boss_execute_post);
    coop_log::info("coop_mod: [BOSS] hooks: place={} damage={} targetPre={} targetPost={}",
        static_cast<int>(pre), static_cast<int>(post), static_cast<int>(exPre),
        static_cast<int>(exPost));
}

void boss_register_vars() {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = "sync_boss_diababa";
    desc.type = CONFIG_VAR_BOOL;
    desc.default_bool = false;
    if (svc_config->register_var(mod_ctx, &desc, &s_enableVar) != MOD_OK) s_enableVar = 0;

    ConfigVarDesc wait = CONFIG_VAR_DESC_INIT;
    wait.name = "boss_room_wait";
    wait.type = CONFIG_VAR_BOOL;
    wait.default_bool = false;
    if (svc_config->register_var(mod_ctx, &wait, &s_waitVar) != MOD_OK) s_waitVar = 0;
}

void boss_on_connected() {
    boss_reset_fight_state();
    s_remoteReady = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        s_readyStage[i][0] = 0;
        s_readyRoom[i] = -1;
    }
    s_goAnyway = false;
    s_wasWaiting = false;
    s_sentInRoom = false;
    s_waitText[0] = '\0';
}

bool boss_waiting_for_peer() {
    return s_wasWaiting;
}

ConfigVarHandle boss_wait_var() {
    return s_waitVar;
}

ConfigVarHandle boss_enabled_var() {
    return s_enableVar;
}

struct PillarRollScan {
    daAlink_c* alink;
    bool wolf;
    int sent;
};

void* scan_pillar_rolls(void* proc, void* data) {
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    auto* scan = static_cast<PillarRollScan*>(data);
    if (fopAcM_GetName(actor) != kBossProcPillar) return nullptr;
    const f32 range = scan->wolf ? 220.0f : 150.0f;
    if (scan->alink->current.pos.abs(actor->current.pos) >= range) return nullptr;

    auto* pillar = static_cast<daPillar_c*>(actor);
    const char* stage = dComIfGp_getStartStageName();
    const bool weakRoom = stage != nullptr && std::strcmp(stage, "D_MN05") == 0 &&
                          fopAcM_GetRoomNo(actor) == 2;
    MsgPillarShake msg{};
    msg.room = static_cast<int8_t>(fopAcM_GetRoomNo(actor));
    msg.swBit = pillar->getSwbit();

    msg.homeX = actor->home.pos.x;
    msg.homeY = actor->home.pos.y;
    msg.homeZ = actor->home.pos.z;
    msg.shake = static_cast<uint8_t>(weakRoom ? daPillar_c::SHAKE_CRASH_LV1
                                              : daPillar_c::SHAKE_CRASH);
    coop_net_send(kMsgPillarShake, &msg, sizeof(msg));
    ++scan->sent;
    coop_log::info("coop_mod: [PILLAR] rolled into pillar sw={} room={} - telling the others",
        static_cast<int>(msg.swBit), static_cast<int>(msg.room));
    return nullptr;
}

bool s_wasRollCrashing = false;

void announce_pillar_rolls() {
    if (!coop_net_connected()) {
        s_wasRollCrashing = false;
        return;
    }
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) {
        s_wasRollCrashing = false;
        return;
    }

    daPy_py_c* py = daPy_getPlayerActorClass();
    if (py == nullptr) {
        s_wasRollCrashing = false;
        return;
    }
    const bool crashing = py->checkFrontRollCrash() != FALSE ||
                          py->checkWolfAttackReverse() != FALSE;
    const bool edge = crashing && !s_wasRollCrashing;
    s_wasRollCrashing = crashing;
    if (!edge) return;

    PillarRollScan scan{};
    scan.alink = alink;
    scan.wolf = daPy_py_c::checkNowWolf() != FALSE;
    scan.sent = 0;
    fopAcM_Search(scan_pillar_rolls, &scan);
}

struct PillarFind {
    int8_t room;
    uint8_t swBit;
    uint8_t shake;
    f32 homeX;
    f32 homeY;
    f32 homeZ;
    int hit;
};

const f32 kPillarMatchDist = 50.0f;

void* apply_pillar_shake_to(void* proc, void* data) {
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    auto* want = static_cast<PillarFind*>(data);
    if (fopAcM_GetName(actor) != kBossProcPillar) return nullptr;
    if (fopAcM_GetRoomNo(actor) != want->room) return nullptr;
    auto* pillar = static_cast<daPillar_c*>(actor);
    if (pillar->getSwbit() != want->swBit) return nullptr;

    const f32 dx = actor->home.pos.x - want->homeX;
    const f32 dy = actor->home.pos.y - want->homeY;
    const f32 dz = actor->home.pos.z - want->homeZ;
    if (dx * dx + dy * dy + dz * dz > kPillarMatchDist * kPillarMatchDist) return nullptr;

    pillar->setShake(want->shake);
    ++want->hit;
    return nullptr;
}

void apply_pillar_shake(const MsgPillarShake& msg) {
    PillarFind want{};
    want.room = msg.room;
    want.swBit = msg.swBit;
    want.shake = msg.shake;
    want.homeX = msg.homeX;
    want.homeY = msg.homeY;
    want.homeZ = msg.homeZ;
    want.hit = 0;
    fopAcM_Search(apply_pillar_shake_to, &want);
    coop_log::info("coop_mod: [PILLAR] the other player rolled into pillar sw={} - shook {}",
        static_cast<int>(msg.swBit), want.hit);
}

namespace {

struct EatWatch {
    bool used = false;
    uint8_t index = 0;
    bool eating = false;
};
EatWatch s_eatWatch[kMaxBossActors];

EatWatch* eat_slot(uint8_t index) {
    for (EatWatch& w : s_eatWatch) {
        if (w.used && w.index == index) return &w;
    }
    for (EatWatch& w : s_eatWatch) {
        if (w.used) continue;
        w = EatWatch{true, index, false};
        return &w;
    }
    return nullptr;
}

void boss_reset_eat_watch() {
    for (EatWatch& w : s_eatWatch) w = EatWatch{};
}

struct EatenBombScan {
    cXyz eye;
    int deleted;
};

void* delete_bomb_near_mouth(void* proc, void* data) {
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    auto* scan = static_cast<EatenBombScan*>(data);
    if (!dBomb_c::checkBombActor(actor)) return nullptr;
    if (static_cast<dBomb_c*>(actor)->checkStateExplode()) return nullptr;
    const f32 dx = actor->current.pos.x - scan->eye.x;
    const f32 dy = actor->current.pos.y - scan->eye.y;
    const f32 dz = actor->current.pos.z - scan->eye.z;

    if (dx * dx + dy * dy + dz * dz > 200.0f * 200.0f) return nullptr;
    fopAcM_delete(actor);
    ++scan->deleted;
    return actor;
}

void announce_bomb_eats(BossList& list) {
    if (!coop_net_connected()) return;
    for (int i = 0; i < list.count; ++i) {
        if (list.kinds[i] != kBossKindTentacle) continue;
        auto* t = reinterpret_cast<b_bh_class*>(list.actors[i]);
        EatWatch* w = eat_slot(list.indices[i]);
        if (w == nullptr) continue;
        const bool eating = bh_action_is_eating(t->mAction);
        const bool edge = eating && !w->eating;
        w->eating = eating;
        if (!edge) continue;
        MsgBossBombEat msg{};
        msg.index = list.indices[i];
        msg.phase2 = t->mAction == kBhActionBBombEat ? 1 : 0;
        coop_net_send(kMsgBossBombEat, &msg, sizeof(msg));
        coop_log::info("coop_mod: [BOSS] head {} took a Bombling (phase2={}) - telling the others",
            static_cast<int>(msg.index), static_cast<int>(msg.phase2));
    }
}

void apply_bomb_eat(const MsgBossBombEat& msg) {
    BossList list;
    collect(list);
    fopAc_ac_c* actor = find(list, kBossKindTentacle, msg.index);
    if (actor == nullptr) return;
    auto* t = reinterpret_cast<b_bh_class*>(actor);

    if (bh_action_is_eating(t->mAction)) {
        EatWatch* w = eat_slot(msg.index);
        if (w != nullptr) w->eating = true;
        return;
    }

    EatenBombScan scan{};
    scan.eye = actor->eyePos;
    scan.deleted = 0;
    fopAcM_Search(delete_bomb_near_mouth, &scan);

    if (msg.phase2) {
        t->mAction = kBhActionBBombEat;
        dComIfGs_onOneZoneSwitch(14, -1);
    } else {
        t->mAction = kBhActionBombEat;
        auto* bq = reinterpret_cast<b_bq_class*>(fopAcM_SearchByID(actor->parentActorID));

        if (bq != nullptr && bq->field_0x6fd == 0) {
            bq->mDemoMode = 20;
            bq->field_0x123c = t->mID;
            bq->field_0x6fd = 1;
        }
    }
    t->mMode = 0;
    t->field_0x69e = 10;

    EatWatch* w = eat_slot(msg.index);
    if (w != nullptr) w->eating = true;
    coop_log::info("coop_mod: [BOSS] their Bombling went into head {} (phase2={}, dropped {} of "
                    "our own bombs)",
        static_cast<int>(msg.index), static_cast<int>(msg.phase2), scan.deleted);
}

}

static bool s_localDemoRunning = false;

static void refresh_local_demo(BossList& list) {
    bool running = false;
    for (int i = 0; i < list.count; ++i) {
        if (list.kinds[i] != kBossKindDiababa) continue;
        if (reinterpret_cast<b_bq_class*>(list.actors[i])->mDemoMode != 0) {
            running = true;
            break;
        }
    }
    s_localDemoRunning = running;
}

bool boss_local_demo_running() { return s_localDemoRunning; }

bool fight_player_here(uint8_t id, const char* stage) {
    if (id == coop_net_local_id()) return true;
    if (id >= kCoopMaxPlayers || !coop_net_player_present(id) || stage == nullptr) return false;
    const CoopPeer& p = features_peer_of(id);
    return p.present && p.inGame && std::strncmp(stage, p.stage, 8) == 0;
}

uint8_t decide_fight_owner(const BossList& list) {
    const char* stage = dComIfGp_getStartStageName();
    int room = -1;
    for (int i = 0; i < list.count; ++i) {

        if (kind_holds_a_fight(list.kinds[i])) {
            room = fopAcM_GetRoomNo(list.actors[i]);
            break;
        }
    }
    if (room < 0 && list.count > 0) room = fopAcM_GetRoomNo(list.actors[0]);

    const bool ownerStillHere = s_fightOwner != kCoopNoPlayer &&
        (s_fightOwner == coop_net_local_id() || coop_net_player_present(s_fightOwner));
    if (ownerStillHere && (s_fightOver || s_hostFightOver || boss_already_beaten(stage, room))) {
        return s_fightOwner;
    }

    const auto playing = [&](uint8_t id) {
        return fight_player_here(id, stage) && !coop_player_paused(id);
    };
    if (room >= 0) {
        const uint8_t owner = enemies_room_owner_player(room);
        if (owner != kCoopNoPlayer && playing(owner)) return owner;
    }
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (playing(static_cast<uint8_t>(i))) return static_cast<uint8_t>(i);
    }

    return s_fightOwner != kCoopNoPlayer ? s_fightOwner : coop_net_local_id();
}

void boss_update() {
    ++s_tick;

    announce_pillar_rolls();

    {
        BossList demoList;
        collect(demoList);
        refresh_local_demo(demoList);

        const uint8_t owner = decide_fight_owner(demoList);
        if (owner != s_fightOwner) {
            if (demoList.count > 0) {
                coop_log::info("coop_mod: [BOSS] player {} is running the fight now (was {})",
                    static_cast<int>(owner),
                    s_fightOwner == kCoopNoPlayer ? -1 : static_cast<int>(s_fightOwner));
            }

            for (int i = 0; i < kMaxBossActors; ++i) s_cached[i] = CachedState{};
            for (int i = 0; i < 4; ++i) s_cachedStem[i] = CachedStem{};
            s_fightOwner = owner;
        }
    }

    if (!boss_wait_possible()) {

        if (s_wasWaiting && in_gameplay()) {
            BossList held;
            collect(held);
            release_fight(held);
            queue_wait_popup(false, "", "");
            s_wasWaiting = false;
            s_waitTicks = 0;
            s_waitText[0] = '\0';
            coop_log::info("coop_mod: [BOSS] disconnected during the door wait - releasing the fight");
        }
        return;
    }

    BossList list;
    collect(list);

    announce_bomb_eats(list);

    update_wait(list);

    if (s_tick % 180 == 0) {
        int cached = 0;
        for (int i = 0; i < kMaxBossActors; ++i) {
            if (s_cached[i].used) ++cached;
        }

        int locAct = -1, locDemo = -1, remAct = -1, remDemo = -1;
        for (int i = 0; i < list.count; ++i) {
            if (list.kinds[i] != kBossKindDiababa) continue;
            auto* bq = reinterpret_cast<b_bq_class*>(list.actors[i]);
            locAct = bq->mAction;
            locDemo = bq->mDemoMode;
            break;
        }
        for (int i = 0; i < kMaxBossActors; ++i) {
            if (!s_cached[i].used || s_cached[i].kind != kBossKindDiababa) continue;
            remAct = s_cached[i].m.action;
            remDemo = s_cached[i].m.inDemo;
            break;
        }

        int helpAct = -1, helpMode = -1, helpCarry = -1, helpSide = -1, helperHere = 0;
        for (int i = 0; i < list.count; ++i) {
            if (list.kinds[i] != kBossKindHelper) continue;
            auto* h = reinterpret_cast<e_mb_class*>(list.actors[i]);
            helperHere = 1;
            helpAct = h->mAction;
            helpMode = h->mMode;
            helpCarry = h->field_0x68c;
            helpSide = h->field_0x6b0;
            break;
        }
        int phaseLeft = -1;
        for (int i = 0; i < list.count; ++i) {
            if (list.kinds[i] != kBossKindDiababa) continue;
            phaseLeft = reinterpret_cast<b_bq_class*>(list.actors[i])->field_0x6ec;
            break;
        }
        coop_log::trace("coop_mod: [BOSS-DIAG2] helperHere={} helperAction={} helperMode={} "
                        "helperCarry={} helperSide={} headsLeftBeforePhase2={}",
            helperHere, helpAct, helpMode, helpCarry, helpSide, phaseLeft);
        coop_log::trace("coop_mod: [BOSS-DIAG] id={} host={} collected={} cached={} syncOn={} "
                        "peerHere={} inGameplay={} enabled={} fightOver={} "
                        "localAction={} localDemo={} remoteAction={} remoteInDemo={} "
                        "proc={:#x} event={} ready={} latched={} roomTicks={}",
            coop_net_local_id(), coop_net_is_host() ? 1 : 0, list.count, cached,
            boss_sync_on() ? 1 : 0, peer_on_our_stage() ? 1 : 0, in_gameplay() ? 1 : 0,
            coop_session(kSessBosses, cfg_bool(s_enableVar, false)) ? 1 : 0, s_fightOver ? 1 : 0,
            locAct, locDemo, remAct, remDemo,

            daAlink_getAlinkActorClass() != nullptr
                ? static_cast<int>(daAlink_getAlinkActorClass()->mProcID) : -1,
            (daAlink_getAlinkActorClass() != nullptr &&
             daAlink_getAlinkActorClass()->checkEventRun() != FALSE) ? 1 : 0,
            local_ready_now() ? 1 : 0, s_readyLatched ? 1 : 0, s_roomTicks);
    }

    if (!boss_sync_on()) return;
    if (!i_run_the_fight()) {

        reapply_cached(list, false);
        return;
    }

    refresh_boss_procs(list);
    if (s_tick % kSendEveryTicks == 0) {
        send_state(list);
        send_stems(list);
    }

    if (s_tick % 300 == 0) {
        coop_log::info("coop_mod: [BOSS] player {} (us) describing {} actors",
            static_cast<int>(coop_net_local_id()), list.count);
    }
}

void boss_queue_overlay() {
    if (!s_waitDlst.mVisible) return;
    dDlst_list_c& lists = g_dComIfG_gameInfo.drawlist;
    for (dDlst_base_c** it = lists.mp2DXluDrawLists; it < lists.mp2DXluStart; ++it) {
        if (*it == &s_waitDlst) return;
    }
    dComIfGd_set2DXlu(&s_waitDlst);
}

void boss_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from) {
    if (type == kMsgPillarShake) {
        if (size < sizeof(MsgPillarShake)) return;
        MsgPillarShake msg;
        std::memcpy(&msg, payload, sizeof(msg));
        apply_pillar_shake(msg);
        return;
    }
    if (type == kMsgBossBombEat) {

        if (size < sizeof(MsgBossBombEat)) return;
        if (!boss_sync_on() || !in_gameplay()) return;
        MsgBossBombEat msg;
        std::memcpy(&msg, payload, sizeof(msg));
        apply_bomb_eat(msg);
        return;
    }
    if (type == kMsgBossReady) {
        if (size < sizeof(MsgBossReady)) return;
        MsgBossReady msg;
        std::memcpy(&msg, payload, sizeof(msg));
        if (from < kCoopMaxPlayers) {
            const uint16_t bit = static_cast<uint16_t>(1u << from);
            s_remoteReady = msg.inRoom != 0 ? static_cast<uint16_t>(s_remoteReady | bit)
                                            : static_cast<uint16_t>(s_remoteReady & ~bit);
            s_readyStamp[from] = s_tick;

            s_readyRoom[from] = msg.room;
            std::memcpy(s_readyStage[from], msg.stage, sizeof(s_readyStage[from]));
        }

        if (msg.goAnyway != 0 && !s_goAnyway && s_sentInRoom && ready_for_our_fight(from)) {
            s_goAnyway = true;
            coop_log::info("coop_mod: [BOSS] the other player chose to go anyway");
        }
        return;
    }
    if (type == kMsgBossStem) {

        if (i_run_the_fight() || from != s_fightOwner || size < sizeof(MsgBossStem)) return;
        if (!coop_session(kSessBosses, cfg_bool(s_enableVar, false)) || !in_gameplay()) return;
        MsgBossStem m;
        std::memcpy(&m, payload, sizeof(m));
        cache_stem(m);
        return;
    }
    if (type == kMsgBossHit) {

        if (!i_run_the_fight() || size < sizeof(MsgBossHit)) return;
        if (!coop_session(kSessBosses, cfg_bool(s_enableVar, false)) || !in_gameplay()) return;
        MsgBossHit msg;
        std::memcpy(&msg, payload, sizeof(msg));
        apply_remote_hit(msg);
        return;
    }
    if (type != kMsgBossState) return;
    if (i_run_the_fight() || from != s_fightOwner) return;
    if (!coop_session(kSessBosses, cfg_bool(s_enableVar, false)) || !in_gameplay()) return;
    if (size < 1) return;
    const int count = payload[0];
    if (size < 1 + count * sizeof(MsgBossActor)) return;

    BossList list;
    collect(list);
    int applied = 0;
    for (int i = 0; i < count; ++i) {
        MsgBossActor m;
        std::memcpy(&m, payload + 1 + i * sizeof(m), sizeof(m));

        cache_state(m);
        if (find(list, m.kind, m.index) != nullptr) ++applied;
    }
    s_diagApplied = applied;
    if (s_tick % 300 == 0 && applied > 0) {
        coop_log::info("coop_mod: [BOSS] joiner following {} actors", applied);
    }
}

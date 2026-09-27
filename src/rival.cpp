

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "mods/svc/actor.h"
#include "mods/svc/hook.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_cc_d.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"

#include <cmath>
#include <cstring>

IMPORT_OPTIONAL_SERVICE(ActorService, svc_actor);

DEFINE_HOOK_SYMBOL("daAlink_c::checkCutFastReady", bool(daAlink_c*), RivalCutFastReadyHook);

bool rival_can_mortal_draw(fopAc_ac_c* target);

namespace {

const char kRivalName[] = "cpRival";

class daCoopRival_c : public fopEn_enemy_c {
public:
    u8 mPlayer;
    dCcD_Stts mStts;
    dCcD_Cyl mBody;
    s16 mShieldAngle;
    u32 mNextHit;
    u32 mHeadLockUntil;
    u32 mNextPin;
    bool mPinning;
    u32 mInvulnUntil;
    u32 mNoFinishUntil;
    cXyz mLastPos;
    u32 mStillSince;

    cPhs_Step create();
    int Execute();
    int Delete();
};

s16 s_procName = -1;
ActorHandle s_handle = 0;
bool s_registered = false;
u32 s_frame = 0;

fpc_ProcID s_rivalOf[kCoopMaxPlayers];

const f32 kHumanRadius = 35.0f;
const f32 kHumanHeight = 150.0f;
const f32 kWolfRadius = 45.0f;
const f32 kWolfHeight = 90.0f;
const u32 kHitGap = 10;

const u32 kTheirInvincibleFrames = 10;
const u32 kEndingBlowCooldown = 180;
const u8 kPinReleaseFrames = 5;
const u32 kHeadLockFrames = 60;
const u32 kDrawStillFrames = 30;
const f32 kStillEpsilon2 = 1.5f * 1.5f;
const u8 kCombatGuard = 1;
const u8 kCombatDown = 2;
const u8 kCombatHurt = 4;

const u32 kTakesEverything = 0xFFFFFFFEu;

const dCcD_SrcCyl kBodySrc = {
    {
        {0x0, {{0x0, 0x0, 0x0}, {kTakesEverything, 0x3}, 0x75}},
        {dCcD_SE_NONE, 0x0, 0x0, 0x0, 0x0},
        {dCcD_SE_NONE, 0x0, 0x0, 0x0, 0x2},
        {0x0},
    },
    {{{0.0f, 0.0f, 0.0f}, kHumanRadius, kHumanHeight}},
};

bool ours(fopAc_ac_c* hitter) {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (hitter == nullptr) return false;
    if (hitter == alink) return true;
    if (projectiles_is_remote(hitter) || spawns_is_replica(hitter)) return false;
    if (fopAcM_GetGroup(hitter) == fopAc_ENEMY_e) return false;

    if (enemies_carried_by_other(hitter)) return false;

    if (fopAcM_GetName(hitter) == fpcNm_NBOMB_e && fopAcM_GetParam(hitter) == 0 &&
        !projectiles_blast_is_ours(hitter)) {
        return false;
    }
    return true;
}

void send_to_them(u8 player, dCcD_GObjInf& at, u8 kind, const cXyz& from, bool blocked, u8 cut) {
    MsgPvpHit msg{};
    msg.to = player;
    msg.kind = kind;
    msg.atType = at.GetAtType();
    msg.atp = at.GetAtAtp();
    msg.spl = static_cast<uint8_t>(at.GetAtSpl());
    msg.mtrl = at.GetAtMtrl();
    msg.se = at.GetAtSe();
    msg.blocked = blocked ? 1 : 0;
    msg.cut = cut;
    features_guess_damage(player, pvp_predict_damage(msg, puppet_hook_combat_of(player)));
    msg.from[0] = from.x;
    msg.from[1] = from.y;
    msg.from[2] = from.z;
    coop_net_send(kMsgPvpHit, &msg, sizeof(msg));
}

cPhs_Step daCoopRival_c::create() {
    fopAcM_ct(this, daCoopRival_c);
    mPlayer = static_cast<u8>(fopAcM_GetParam(this) & 0xFF);
    mNextHit = 0;
    mHeadLockUntil = 0;
    mLastPos = current.pos;
    mStillSince = s_frame;
    mNextPin = 0;
    mPinning = false;
    mInvulnUntil = 0;
    mNoFinishUntil = 0;
    mShieldAngle = 0;
    mStts.Init(150, 0, this);
    mBody.Set(kBodySrc);
    mBody.SetStts(&mStts);
    fopAcM_SetMin(this, -60.0f, 0.0f, -60.0f);
    fopAcM_SetMax(this, 60.0f, 180.0f, 60.0f);
    health = 100;
    return cPhs_COMPLEATE_e;
}

int daCoopRival_c::Delete() {
    this->~daCoopRival_c();
    return 1;
}

int daCoopRival_c::Execute() {

    if (mPlayer >= kCoopMaxPlayers || s_rivalOf[mPlayer] != fopAcM_GetID(this)) {
        fopAcM_delete(this);
        return 1;
    }
    daAlink_c* alink = daAlink_getAlinkActorClass();
    f32 x, y, z;
    short angle = 0;
    if (!pvp_active() || alink == nullptr ||
        !puppet_hook_get_pose_of(mPlayer, &x, &y, &z, &angle, nullptr, nullptr)) {
        attention_info.flags = 0;
        return 1;
    }
    const u8 combat = puppet_hook_combat_of(mPlayer);
    const bool wolf = puppet_hook_is_wolf_of(mPlayer);

    if (mBody.ChkTgHit()) {
        dCcD_GObjInf* at = mBody.GetTgHitGObj();
        fopAc_ac_c* hitter = mBody.GetTgHitAc();
        if (at != nullptr && ours(hitter) && s_frame >= mNextHit) {
            const bool clawshot = (at->GetAtType() & AT_TYPE_HOOKSHOT) != 0;

            const cXyz from = (clawshot || hitter == nullptr) ? alink->current.pos
                                                              : hitter->current.pos;
            const bool blocked = mBody.ChkTgShieldHit();

            const bool sword = hitter == alink &&
                               (at->GetAtType() & (AT_TYPE_NORMAL_SWORD | AT_TYPE_MASTER_SWORD)) != 0;
            send_to_them(mPlayer, *at, clawshot ? kPvpPull : kPvpHit, from, blocked,
                sword ? alink->getCutType() : 0);
            mNextHit = s_frame + kHitGap;
            if (!clawshot && !blocked && (at->GetAtType() & AT_TYPE_SHIELD_ATTACK) == 0) {
                mInvulnUntil = s_frame + kTheirInvincibleFrames;
            }

            if ((at->GetAtType() & AT_TYPE_SHIELD_ATTACK) != 0 && !blocked) {
                mHeadLockUntil = s_frame + kHeadLockFrames;
            }
            coop_log::info("coop_mod: [PVP] we hit {}: type={:#x} atp={} spl={}{}{}", mPlayer,
                at->GetAtType(), at->GetAtAtp(), static_cast<int>(at->GetAtSpl()),
                clawshot ? " (clawshot)" : "", blocked ? " (on their shield)" : "");
        }
        mBody.ClrTgHit();
    }

    const bool finishing =
        (alink->mProcID == daAlink_c::PROC_CUT_DOWN || alink->mProcID == daAlink_c::PROC_CUT_DOWN_LAND) &&
        alink->field_0x280c.getActor() == this;
    if (finishing && s_frame >= mNextPin) {
        MsgPvpHit pin{};
        pin.to = mPlayer;
        pin.kind = kPvpPin;
        pin.atp = 0;
        coop_net_send(kMsgPvpHit, &pin, sizeof(pin));
        mNextPin = s_frame + 4;
        mPinning = true;
    } else if (!finishing && mPinning) {
        MsgPvpHit release{};
        release.to = mPlayer;
        release.kind = kPvpPin;
        release.atp = kPinReleaseFrames;
        coop_net_send(kMsgPvpHit, &release, sizeof(release));
        mPinning = false;
    }

    if (checkCutDownHitFlg()) {
        offCutDownHitFlg();
        MsgPvpHit msg{};
        msg.to = mPlayer;
        msg.kind = kPvpHit;
        msg.atType = AT_TYPE_NORMAL_SWORD;
        msg.atp = 8;
        msg.spl = dCcG_At_Spl_UNK_1;
        msg.se = dCcD_SE_SWORD_STAB;
        msg.cut = daPy_py_c::CUT_TYPE_DOWN;
        features_guess_damage(mPlayer, pvp_predict_damage(msg, puppet_hook_combat_of(mPlayer)));
        msg.from[0] = alink->current.pos.x;
        msg.from[1] = alink->current.pos.y;
        msg.from[2] = alink->current.pos.z;
        coop_net_send(kMsgPvpHit, &msg, sizeof(msg));
        mNoFinishUntil = s_frame + kEndingBlowCooldown;
        coop_log::info("coop_mod: [PVP] ending blow on {}", mPlayer);
    }

    const bool hooked = fopAcM_checkHookCarryNow(this) != 0;
    if (!hooked) current.pos.set(x, y, z);
    old.pos = current.pos;

    {
        const f32 dx = current.pos.x - mLastPos.x, dy = current.pos.y - mLastPos.y,
                  dz = current.pos.z - mLastPos.z;
        if (hooked || dx * dx + dy * dy + dz * dz > kStillEpsilon2) mStillSince = s_frame;
        mLastPos = current.pos;
    }
    shape_angle.y = current.angle.y = angle;
    const f32 height = wolf ? kWolfHeight : kHumanHeight;
    const f32 radius = wolf ? kWolfRadius : kHumanRadius;
    eyePos.set(current.pos.x, current.pos.y + height * 0.85f, current.pos.z);
    attention_info.position.set(current.pos.x, current.pos.y + height + 20.0f, current.pos.z);

    if (pvp_lock_on()) {
        attention_info.flags = fopAc_AttnFlag_BATTLE_e;
        attention_info.distances[fopAc_attn_BATTLE_e] = 3;
    } else {
        attention_info.flags = 0;
    }

    if ((combat & kCombatDown) != 0 && s_frame >= mNoFinishUntil) {
        onDownFlg();
        cXyz torso;
        if (puppet_hook_torso_of(mPlayer, &torso)) {
            setDownPos(&torso);
        } else {
            setDownPos(&current.pos);
        }
    } else {
        offDownFlg();
    }
    if (s_frame < mHeadLockUntil) {
        onHeadLockFlg();
        setHeadLockPos(&eyePos);
    } else {
        offHeadLockFlg();
    }

    const bool guard = (combat & kCombatGuard) != 0 && !wolf;
    if (guard) {
        mShieldAngle = angle;
        mBody.OnTgShield();
        mBody.OnTgShieldFrontRange();
        mBody.SetTgShieldFrontRangeYAngle(&mShieldAngle);
    } else {
        mBody.OffTgShield();
    }

    mBody.SetTgHitMark(CcG_Tg_UNK_MARK_6);

    if (!guard) {
        fopAcM_OnStatus(this, fopAcStts_UNK_0x80000_e);
    } else if (!hooked) {
        fopAcM_OffStatus(this, fopAcStts_UNK_0x80000_e);
    }

    if (s_frame < mInvulnUntil && (combat & kCombatDown) == 0) {
        mBody.OffTgSetBit();
        mBody.ClrTgHit();
    } else {
        mBody.OnTgSetBit();
    }

    mBody.SetC(current.pos);
    mBody.SetR(radius);
    mBody.SetH(height);
    dComIfG_Ccsp()->Set(&mBody);
    return 1;
}

cPhs_Step rival_create(void* self) { return static_cast<daCoopRival_c*>(self)->create(); }
int rival_delete(void* self) { return static_cast<daCoopRival_c*>(self)->Delete(); }
int rival_execute(void* self) { return static_cast<daCoopRival_c*>(self)->Execute(); }
int rival_is_delete(void*) { return 1; }
int rival_draw(void*) { return 1; }

const ActorProfileDesc kProfile = {
    .name = "cpRival",
    .priority_group = 7,
    .process_size = sizeof(daCoopRival_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e,
    .group = fopAc_ENEMY_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = rival_create,
    .delete_function = rival_delete,
    .execute_function = rival_execute,
    .is_delete_function = rival_is_delete,
    .draw_function = rival_draw,
};

daCoopRival_c* rival_actor(u8 player) {
    if (s_rivalOf[player] == fpcM_ERROR_PROCESS_ID_e) return nullptr;
    return static_cast<daCoopRival_c*>(fopAcM_SearchByID(s_rivalOf[player]));
}

void remove_rival(u8 player) {
    if (daCoopRival_c* rival = rival_actor(player)) fopAcM_delete(rival);
    s_rivalOf[player] = fpcM_ERROR_PROCESS_ID_e;
}

}

void rival_init() {
    for (int i = 0; i < kCoopMaxPlayers; ++i) s_rivalOf[i] = fpcM_ERROR_PROCESS_ID_e;
    if (svc_actor == nullptr) {
        coop_log::warn("coop_mod: [PVP] no actor service - players cannot be targeted or hit");
        return;
    }
    s_registered = svc_actor->register_actor(mod_ctx, &kProfile, &s_procName, &s_handle) == MOD_OK;
    const ModResult draw = mods::hook::add_post<RivalCutFastReadyHook>(
        [](ModContext*, void* args, void* retval, void*) {

            auto* self = mods::arg<daAlink_c*>(args, 0);
            auto* ready = static_cast<bool*>(retval);
            if (ready == nullptr || !*ready || self == nullptr) return;
            if (self->mProcID == daAlink_c::PROC_CUT_FAST_READY) return;
            if (self->mAttention == nullptr) return;
            if (!rival_can_mortal_draw(self->mAttention->LockonTarget(0))) *ready = false;
        });
    coop_log::info("coop_mod: [PVP] mortal draw gate hook: {}", static_cast<int>(draw));
    coop_log::info("coop_mod: [PVP] rival actor {}", s_registered ? "registered" : "refused");
}

void rival_update() {
    ++s_frame;
    if (!s_registered) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();

    const bool on = pvp_active() && alink != nullptr && !dComIfGp_isEnableNextStage() &&
                    !sumo_hides_equipment(coop_net_local_id());
    for (u8 id = 0; id < kCoopMaxPlayers; ++id) {
        const bool want = on && id != coop_net_local_id() &&
                          puppet_hook_get_pose_of(id, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        if (!want) {
            if (s_rivalOf[id] != fpcM_ERROR_PROCESS_ID_e) remove_rival(id);
            continue;
        }
        if (s_rivalOf[id] != fpcM_ERROR_PROCESS_ID_e) {
            if (rival_actor(id) != nullptr || fpcM_IsCreating(s_rivalOf[id])) continue;
            s_rivalOf[id] = fpcM_ERROR_PROCESS_ID_e;
        }
        ActorSpawnParams params{};
        params.parameters = id;
        params.argument = -1;
        params.room_num = static_cast<int8_t>(fopAcM_GetRoomNo(alink));
        params.position = {alink->current.pos.x, alink->current.pos.y, alink->current.pos.z};
        params.scale = {1.0f, 1.0f, 1.0f};
        ActorId made = 0;
        CoopActorLayer layer;
        if (svc_actor->create_actor(mod_ctx, s_procName, &params, &made) == MOD_OK) {
            s_rivalOf[id] = static_cast<fpc_ProcID>(made);
            coop_log::info("coop_mod: [PVP] a rival for player {}", id);
        }
    }
}

bool rival_can_mortal_draw(fopAc_ac_c* target) {
    if (!rival_is(target)) return true;
    const auto* rival = static_cast<daCoopRival_c*>(target);
    return s_frame - rival->mStillSince >= kDrawStillFrames;
}

bool rival_hooked_pos(uint8_t player, cXyz* pos) {
    if (player >= kCoopMaxPlayers || !pvp_active()) return false;
    daCoopRival_c* rival = rival_actor(player);
    if (rival == nullptr || fopAcM_checkHookCarryNow(rival) == 0) return false;
    if (pos != nullptr) *pos = rival->current.pos;
    return true;
}

bool rival_is(fopAc_ac_c* actor) {
    return actor != nullptr && s_procName >= 0 && fopAcM_GetName(actor) == s_procName;
}

void rival_forget_all() {
    for (int i = 0; i < kCoopMaxPlayers; ++i) s_rivalOf[i] = fpcM_ERROR_PROCESS_ID_e;
}

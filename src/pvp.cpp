

#include "mod.hpp"
#include "net/messages.hpp"
#include "net/protocol.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_arrow.h"
#include "d/actor/d_a_boomerang.h"
#include "d/actor/d_a_nbomb.h"
#include "d/d_cc_d.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"

#include <cmath>
#include <cstring>

namespace {

ConfigVarHandle s_enabledVar = 0;
ConfigVarHandle s_damageVar = 0;
ConfigVarHandle s_lockOnVar = 0;

bool s_hostEnabled = false;
uint16_t s_hostDamagePercent = 100;

bool s_sentEnabled = false;
uint16_t s_sentDamagePercent = 0;
bool s_needSend = false;

uint32_t s_tick = 0;
bool s_shieldHitSet = false;

uint16_t local_damage_percent() {
    int64_t v = 100;
    if (s_damageVar != 0) svc_config->get_int(mod_ctx, s_damageVar, &v);
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    return static_cast<uint16_t>(v);
}

uint16_t effective_damage_percent() {
    if (!coop_net_connected()) return 100;
    return coop_net_is_host() ? local_damage_percent() : s_hostDamagePercent;
}

struct Pull {
    bool active = false;
    cXyz to;
    u32 frames = 0;
};
Pull s_pull;
const f32 kPullSpeed = 60.0f;
const f32 kPullStopShort = 90.0f;
const u32 kPullMaxFrames = 45;

const s16 kPvpInvincibleFrames = 10;
const u32 kTrimWindowFrames = 120;
u32 s_trimFrames = 0;

void trim_invincibility(daAlink_c* alink) {
    if (s_trimFrames == 0) return;
    --s_trimFrames;
    if (alink == nullptr || alink->mDamageTimer == 0) return;
    if (alink->mDamageTimer > kPvpInvincibleFrames) alink->mDamageTimer = kPvpInvincibleFrames;
    if (!alink->checkModeFlg(8)) s_trimFrames = 0;
}

void start_pull(const MsgPvpHit& hit) {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;
    if (dComIfGp_event_runCheck() || alink->checkHorseRide() ||
        sumo_hides_equipment(coop_net_local_id())) {
        coop_log::info("coop_mod: [PVP] clawshot hit, pull blocked (event/horse/sumo)");
        return;
    }
    const cXyz from(hit.from[0], hit.from[1], hit.from[2]);
    cXyz toward(alink->current.pos.x - from.x, 0.0f, alink->current.pos.z - from.z);
    const f32 len = std::sqrt(toward.x * toward.x + toward.z * toward.z);
    if (len < kPullStopShort) {
        coop_log::info("coop_mod: [PVP] clawshot hit at {:.0f}, already close", len);
        return;
    }
    s_pull.active = true;
    s_pull.to.set(from.x + toward.x / len * kPullStopShort, from.y, from.z + toward.z / len * kPullStopShort);
    s_pull.frames = 0;
    coop_log::info("coop_mod: [PVP] clawshot pull {:.0f}", len - kPullStopShort);
}

u8 hit_se_for(const MsgPvpHit& hit) {
    if (hit.se != 0) return hit.se;
    const u32 t = hit.atType;
    if (t & AT_TYPE_SLINGSHOT) return dCcD_SE_PACHINKO;
    if (t & AT_TYPE_HOOKSHOT) return dCcD_SE_HOOKSHOT_STICK;
    if (t & AT_TYPE_ARROW) return dCcD_SE_ARROW_STICK;
    if (t & AT_TYPE_SHIELD_ATTACK) return dCcD_SE_SHIELD_ATTACK;
    return dCcD_SE_SWORD;
}

struct SkillPower {
    u8 cut;
    u8 atp;
    u8 spl;
    const char* name;
};
const SkillPower kSkillPower[] = {
    {daPy_py_c::CUT_TYPE_MORTAL_DRAW_A, 12, 1, "Mortal Draw"},
    {daPy_py_c::CUT_TYPE_MORTAL_DRAW_B, 12, 1, "Mortal Draw"},
    {daPy_py_c::CUT_TYPE_HEAD_JUMP, 8, 1, "Helm Splitter"},

    {daPy_py_c::CUT_TYPE_DOWN, 16, 1, "Ending Blow"},
    {daPy_py_c::CUT_TYPE_FINISH_STAB, 16, 1, "Ending Blow"},
    {daPy_py_c::CUT_TYPE_LARGE_JUMP_FINISH, 8, 1, "Jump Strike"},
    {daPy_py_c::CUT_TYPE_LARGE_JUMP, 8, 1, "Jump Strike"},
    {daPy_py_c::CUT_TYPE_TWIRL, 6, 0, "Back Slice"},
    {daPy_py_c::CUT_TYPE_LARGE_TURN_LEFT, 6, 1, "Great Spin"},
    {daPy_py_c::CUT_TYPE_LARGE_TURN_RIGHT, 6, 1, "Great Spin"},
};

const SkillPower* skill_power(u8 cut) {
    if (cut == 0) return nullptr;
    for (const SkillPower& s : kSkillPower) {
        if (s.cut == cut) return &s;
    }
    return nullptr;
}

struct HitPower {
    int atp;
    u8 spl;
    int dmg;
    const SkillPower* skill;
};

HitPower hit_power(const MsgPvpHit& hit) {
    HitPower h{};
    h.atp = hit.atp;

    if (h.atp == 0 && (hit.atType & AT_TYPE_SLINGSHOT) != 0) h.atp = 1;
    h.spl = hit.spl;
    h.skill = skill_power(hit.cut);
    if (h.skill != nullptr) {
        if (h.skill->atp > h.atp) h.atp = h.skill->atp;
        if (h.skill->spl > h.spl) h.spl = h.skill->spl;
    }
    h.dmg = (h.atp * effective_damage_percent() + 50) / 100;
    if (h.atp > 0 && h.dmg < 1 && effective_damage_percent() > 0) h.dmg = 1;
    if (h.dmg > 255) h.dmg = 255;
    return h;
}

bool guarding(daAlink_c* alink) {
    return !alink->checkWolf() && (alink->checkUpperGuardAnime() || alink->checkPlayerGuard());
}

void hit_spark(daAlink_c* alink, const cXyz& from, u16 mark, u32 atType) {
    const s16 toAttacker = cLib_targetAngleY(&alink->current.pos, &from);
    csXyz sparkAngle(0, toAttacker, 0);
    cXyz sparkPos(alink->current.pos.x + cM_ssin(toAttacker) * 30.0f, alink->current.pos.y + 90.0f,
                  alink->current.pos.z + cM_scos(toAttacker) * 30.0f);
    dComIfGp_setHitMark(mark, alink, &sparkPos, &sparkAngle, nullptr, atType);
}

u32 s_pinFrames = 0;
u32 s_pinTotal = 0;

const u32 kPinRefresh = 20;
const u32 kPinMax = 150;

void apply_hit(const MsgPvpHit& hit) {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr || !pvp_live()) return;
    if (dComIfGp_event_runCheck()) return;

    if (sumo_hides_equipment(coop_net_local_id())) return;

    if ((hit.atType & AT_TYPE_800) != 0) return;

    const HitPower power = hit_power(hit);
    const int atp = power.atp;
    const u8 spl = power.spl;
    const int dmg = power.dmg;
    const SkillPower* skill = power.skill;
    const cXyz from(hit.from[0], hit.from[1], hit.from[2]);

    const bool endingBlow = hit.cut == daPy_py_c::CUT_TYPE_DOWN || hit.cut == daPy_py_c::CUT_TYPE_FINISH_STAB;
    if (alink->checkCameraLargeDamage() || endingBlow) {
        if (skill == nullptr) return;
        alink->setDamagePoint(dmg, FALSE, FALSE, 0);
        hit_spark(alink, from, 1, hit.atType);

        coop_log::info("coop_mod: [PVP] got hit while down: {} dmg={}", skill->name, dmg);
        return;
    }
    if (alink->mDamageTimer != 0) return;

    bool shield = false;
    if (guarding(alink)) {
        const s16 toAttacker = cLib_targetAngleY(&alink->current.pos, &from);
        const s16 diff = static_cast<s16>(toAttacker - alink->shape_angle.y);
        shield = hit.blocked != 0 || (diff > -0x3000 && diff < 0x3000);
    }
    const bool bash = (hit.atType & AT_TYPE_SHIELD_ATTACK) != 0 && !alink->checkWolf() &&
                      !alink->checkHorseRide();

    if (shield && !bash && spl == 0) {
        const bool wood = alink->checkWoodShieldEquipNotIronBall() && !alink->checkMagicArmorNoDamage();
        const u32 se = dCcD_GObjInf::getHitSeID(hit_se_for(hit), wood ? 0 : 1);
        alink->mZ2Link.startCollisionSE(se, wood ? 0x29 : 0x28);
        dComIfGp_getVibration().StartShock(VIBMODE_S_POWER3, 1, cXyz(0.0f, 1.0f, 0.0f));
        hit_spark(alink, from, 6, hit.atType);
        coop_log::info("coop_mod: [PVP] blocked: type={:#x} cut={}", hit.atType, hit.cut);
        return;
    }

    static dCcD_Sph s_attacker;
    s_attacker.SetAtType(hit.atType);
    s_attacker.SetAtAtp(static_cast<u8>(atp));
    s_attacker.SetAtSpl(static_cast<dCcG_At_Spl>(spl));
    s_attacker.SetAtMtrl(hit.mtrl);
    s_attacker.SetAtSe(hit_se_for(hit));

    dCcD_Cyl& tg = alink->mTgCyls[0];
    tg.SetTgHit(&s_attacker);

    tg.OnTgHitNoActor();
    cXyz hitPos(alink->current.pos.x, alink->current.pos.y + 80.0f, alink->current.pos.z);
    tg.SetTgHitPos(hitPos);

    cXyz away(alink->current.pos.x - from.x, 0.0f, alink->current.pos.z - from.z);
    const f32 len = std::sqrt(away.x * away.x + away.z * away.z);
    if (len > 0.01f) {
        away.x *= 10.0f / len;
        away.z *= 10.0f / len;
    } else {
        away.set(cM_ssin(alink->shape_angle.y) * -10.0f, 0.0f, cM_scos(alink->shape_angle.y) * -10.0f);
    }
    tg.SetTgRVec(away);

    const bool stun = bash;
    if (stun) {
        tg.OnTgShieldHit();
        s_shieldHitSet = true;
        s_attacker.SetAtSpl(static_cast<dCcG_At_Spl>(10));
    } else if (shield) {

        tg.OnTgShieldHit();
        s_shieldHitSet = true;
    } else {
        alink->mCcStts.PlusDmg(dmg);
        s_trimFrames = kTrimWindowFrames;
    }
    if (shield) {
        hit_spark(alink, from, 6, hit.atType);
    } else if (atp > 0 || stun) {
        hit_spark(alink, from, 1, hit.atType);
    }
    coop_log::info("coop_mod: [PVP] hit type={:#x} atp={} spl={} se={} cut={} dmg={}{}{}{}",
        hit.atType, atp, spl, hit_se_for(hit), hit.cut, stun || shield ? 0 : dmg,
        skill != nullptr ? " - " : "", skill != nullptr ? skill->name : "",
        stun ? " (stunned)" : shield ? " (on our shield)" : "");
}

void send_state() {
    MsgPvpState msg{};

    msg.enabled = pvp_active() ? 1 : 0;
    msg.damagePercent = local_damage_percent();
    coop_net_send(kMsgPvpState, &msg, sizeof(msg));
    s_sentEnabled = msg.enabled != 0;
    s_sentDamagePercent = msg.damagePercent;
    s_needSend = false;
}

}

int pvp_predict_damage(const MsgPvpHit& hit, uint8_t combat) {
    if (!pvp_live() || hit.kind != kPvpHit) return 0;
    if ((combat & kSnapNoHits) != 0) return 0;
    const HitPower power = hit_power(hit);
    if ((combat & 2) != 0) return power.skill != nullptr ? power.dmg : 0;
    if ((combat & 4) != 0) return 0;
    if (hit.blocked != 0 || (hit.atType & AT_TYPE_SHIELD_ATTACK) != 0) return 0;
    return power.dmg;
}

void pvp_register_vars() {
    ConfigVarDesc enabled = CONFIG_VAR_DESC_INIT;
    enabled.name = "pvp_enabled";
    enabled.type = CONFIG_VAR_BOOL;
    enabled.default_bool = false;
    if (svc_config->register_var(mod_ctx, &enabled, &s_enabledVar) != MOD_OK) s_enabledVar = 0;

    ConfigVarDesc damage = CONFIG_VAR_DESC_INIT;
    damage.name = "pvp_damage_percent";
    damage.type = CONFIG_VAR_INT;
    damage.default_int = 100;
    if (svc_config->register_var(mod_ctx, &damage, &s_damageVar) != MOD_OK) s_damageVar = 0;

    ConfigVarDesc lockOn = CONFIG_VAR_DESC_INIT;
    lockOn.name = "pvp_lock_on";
    lockOn.type = CONFIG_VAR_BOOL;
    lockOn.default_bool = true;
    if (svc_config->register_var(mod_ctx, &lockOn, &s_lockOnVar) != MOD_OK) s_lockOnVar = 0;
}

ConfigVarHandle pvp_enabled_var() { return s_enabledVar; }
ConfigVarHandle pvp_damage_var() { return s_damageVar; }
ConfigVarHandle pvp_lock_on_var() { return s_lockOnVar; }

bool pvp_lock_on() {
    return pvp_live() && cfg_bool(s_lockOnVar, true);
}

void pvp_after_player(daAlink_c* alink) {
    trim_invincibility(alink);
    if (!s_pull.active) return;
    if (alink == nullptr || !pvp_live() || dComIfGp_event_runCheck() || ++s_pull.frames > kPullMaxFrames) {
        s_pull.active = false;
        return;
    }
    cXyz at = alink->current.pos;
    cXyz left(s_pull.to.x - at.x, s_pull.to.y - at.y, s_pull.to.z - at.z);
    const f32 dist = std::sqrt(left.x * left.x + left.y * left.y + left.z * left.z);
    if (dist <= kPullSpeed) {
        at = s_pull.to;
        s_pull.active = false;
    } else {
        at.x += left.x / dist * kPullSpeed;
        at.y += left.y / dist * kPullSpeed;
        at.z += left.z / dist * kPullSpeed;
    }

    const s16 facing = cLib_targetAngleY(&alink->current.pos, &s_pull.to);
    alink->setPlayerPosAndAngle(&at, facing, TRUE);
}

bool pvp_active() {
    if (!coop_net_connected()) return false;
    return coop_net_is_host() ? cfg_bool(s_enabledVar, false) : s_hostEnabled;
}

bool pvp_with(uint8_t player) {
    if (coop_net_connected()) return pvp_active();
    return global_pvp_on() && global_slot_pvp(player);
}

bool pvp_live() {
    return pvp_active() || global_pvp_on();
}

void pvp_update() {
    ++s_tick;
    daAlink_c* alink = daAlink_getAlinkActorClass();

    if (s_shieldHitSet && alink != nullptr) {
        alink->mTgCyls[0].OffTgShieldHit();
        s_shieldHitSet = false;
    }

    if (s_pinFrames > 0) {
        --s_pinFrames;
        ++s_pinTotal;
        if (alink == nullptr || !alink->checkCameraLargeDamage() || s_pinTotal > kPinMax) {
            s_pinFrames = 0;
        } else {
            alink->onLargeDamageUpStop();
        }
    }

    rival_update();
    if (!coop_net_connected()) return;

    if (coop_net_is_host()) {
        const bool enabled = cfg_bool(s_enabledVar, false);
        const uint16_t percent = local_damage_percent();
        if (s_needSend || enabled != s_sentEnabled || percent != s_sentDamagePercent) {
            send_state();
        }
    }
}

void pvp_on_connected() {
    s_needSend = true;
    s_hostEnabled = false;
    s_hostDamagePercent = 100;
}

void pvp_on_disconnected() {
    s_hostEnabled = false;
    s_trimFrames = 0;
}

void pvp_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from) {
    if (type == kMsgPvpState) {
        if (size < sizeof(MsgPvpState) || coop_net_is_host()) return;
        MsgPvpState msg;
        std::memcpy(&msg, payload, sizeof(msg));
        const bool was = s_hostEnabled;
        s_hostEnabled = msg.enabled != 0;
        s_hostDamagePercent = msg.damagePercent;
        if (was != s_hostEnabled) {
            coop_log::info("coop_mod: [PVP] host turned PvP {}", s_hostEnabled ? "on" : "off");
        }
    } else if (type == kMsgPvpHit) {
        if (size < sizeof(MsgPvpHit)) return;
        MsgPvpHit msg;
        std::memcpy(&msg, payload, sizeof(msg));
        if (msg.to != coop_net_local_id() || !pvp_with(from)) return;
        if (msg.kind == kPvpPin) {
            daAlink_c* me = daAlink_getAlinkActorClass();
            if (me != nullptr && me->checkCameraLargeDamage()) {
                if (s_pinFrames == 0) {
                    s_pinTotal = 0;
                    coop_log::info("coop_mod: [PVP] pinned for an Ending Blow");
                }
                s_pinFrames = msg.atp != 0 ? msg.atp : kPinRefresh;
            }
            return;
        }

        daAlink_c* alink = daAlink_getAlinkActorClass();
        bool pullBlocked = false;
        if (msg.kind == kPvpPull && alink != nullptr && guarding(alink)) {
            const cXyz from(msg.from[0], msg.from[1], msg.from[2]);
            const s16 diff = static_cast<s16>(cLib_targetAngleY(&alink->current.pos, &from) -
                                              alink->shape_angle.y);
            pullBlocked = msg.blocked != 0 || (diff > -0x3000 && diff < 0x3000);
        }
        if (msg.kind == kPvpPull && !pullBlocked) {
            start_pull(msg);
        } else {
            apply_hit(msg);
        }
    }
}

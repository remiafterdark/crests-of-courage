

#include "mod.hpp"
#include "net/messages.hpp"

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
    return coop_net_is_host() ? local_damage_percent() : s_hostDamagePercent;
}

const int kSourceCooldownTicks = 20;

enum PvpSource : uint8_t {
    kSrcAtSph,
    kSrcAtCyl,
    kSrcAtCps0,
    kSrcAtCps1,
    kSrcAtCps2,
    kSrcGuardCps,
    kSrcIronBall,
    kSrcBoomerang,
    kSrcArrow,
    kSrcBomb,
    kSrcCount,
};
uint32_t s_cooldownUntil[kSrcCount] = {};

struct Hurtbox {
    cXyz feet;
    f32 radius;
    f32 height;
};

bool sphere_hits(const Hurtbox& hb, const cXyz& c, f32 r) {
    const f32 dx = c.x - hb.feet.x;
    const f32 dz = c.z - hb.feet.z;
    const f32 reach = r + hb.radius;
    if (dx * dx + dz * dz > reach * reach) return false;
    return c.y + r >= hb.feet.y && c.y - r <= hb.feet.y + hb.height;
}

bool cylinder_hits(const Hurtbox& hb, const cXyz& bottom, f32 r, f32 h) {
    const f32 dx = bottom.x - hb.feet.x;
    const f32 dz = bottom.z - hb.feet.z;
    const f32 reach = r + hb.radius;
    if (dx * dx + dz * dz > reach * reach) return false;
    return bottom.y <= hb.feet.y + hb.height && bottom.y + h >= hb.feet.y;
}

bool capsule_hits(const Hurtbox& hb, const cXyz& a, const cXyz& b, f32 r) {
    const int kSamples = 8;
    for (int i = 0; i <= kSamples; ++i) {
        const f32 t = static_cast<f32>(i) / kSamples;
        const cXyz p(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
        if (sphere_hits(hb, p, r)) return true;
    }
    return false;
}

void send_hit(dCcD_GObjInf& at, PvpSource source, const cXyz& from) {
    MsgPvpHit msg{};
    msg.atType = at.GetAtType();
    msg.atp = at.GetAtAtp();
    msg.spl = static_cast<uint8_t>(at.GetAtSpl());
    msg.mtrl = at.GetAtMtrl();
    msg.source = source;
    msg.from[0] = from.x;
    msg.from[1] = from.y;
    msg.from[2] = from.z;
    coop_net_send(kMsgPvpHit, &msg, sizeof(msg));
    s_cooldownUntil[source] = s_tick + kSourceCooldownTicks;
    coop_log::info("coop_mod: [PVP] hit them: source={} type={:#x} atp={} spl={}", static_cast<int>(source),
        msg.atType, msg.atp, msg.spl);
}

bool ready(PvpSource source) {
    return s_tick >= s_cooldownUntil[source];
}

struct ActorList {
    fopAc_ac_c* actors[16];
    int count;
    s16 name;
};

void* collect_by_name(void* proc, void* data) {
    auto* list = static_cast<ActorList*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor != nullptr && list->count < 16 && fopAcM_GetName(actor) == list->name) {
        list->actors[list->count++] = actor;
    }
    return nullptr;
}

void check_attacks(daAlink_c* alink, const Hurtbox& hb) {
    const cXyz& me = alink->current.pos;

    auto try_sph = [&](dCcD_Sph& sph, PvpSource src, const cXyz& from) {
        if (!ready(src) || !sph.ChkAtSet()) return false;
        if (!sphere_hits(hb, *sph.GetCP(), sph.GetR())) return false;
        send_hit(sph, src, from);
        return true;
    };
    auto try_cps = [&](dCcD_Cps& cps, PvpSource src, const cXyz& from) {
        if (!ready(src) || !cps.ChkAtSet()) return false;
        if (!capsule_hits(hb, *cps.GetStartP(), *cps.GetEndP(), cps.GetR())) return false;
        send_hit(cps, src, from);
        return true;
    };

    try_sph(alink->mAtSph, kSrcAtSph, me);
    if (ready(kSrcAtCyl) && alink->mAtCyl.ChkAtSet() &&
        cylinder_hits(hb, *alink->mAtCyl.GetCP(), alink->mAtCyl.GetR(), alink->mAtCyl.GetH()))
    {
        send_hit(alink->mAtCyl, kSrcAtCyl, me);
    }
    try_cps(alink->mAtCps[0], kSrcAtCps0, me);
    try_cps(alink->mAtCps[1], kSrcAtCps1, me);
    try_cps(alink->mAtCps[2], kSrcAtCps2, me);
    try_cps(alink->mGuardAtCps, kSrcGuardCps, me);
    try_sph(alink->field_0x1778, kSrcIronBall, *alink->field_0x1778.GetCP());

    if (fopAc_ac_c* boomActor = alink->getBoomerangActor()) {
        daBoomerang_c* boom = static_cast<daBoomerang_c*>(boomActor);
        try_cps(boom->m_atCps, kSrcBoomerang, boom->current.pos);
    }

    if (ready(kSrcArrow)) {
        ActorList arrows{};
        arrows.name = fpcNm_ARROW_e;
        fopAcM_Search(collect_by_name, &arrows);
        for (int i = 0; i < arrows.count; ++i) {
            daArrow_c* arrow = static_cast<daArrow_c*>(arrows.actors[i]);
            if (projectiles_is_remote(arrow) || !arrow->field_0x688.ChkAtSet()) continue;
            if (fopAcM_GetParam(arrow) != 1 && fopAcM_GetParam(arrow) != 2) continue;
            if (!capsule_hits(hb, *arrow->field_0x688.GetStartP(), *arrow->field_0x688.GetEndP(),
                    arrow->field_0x688.GetR()))
            {
                continue;
            }
            send_hit(arrow->field_0x688, kSrcArrow, arrow->current.pos);
            s_cooldownUntil[kSrcArrow] = s_tick + 2;
            arrow->deleteArrow();
            break;
        }
    }

    if (ready(kSrcBomb)) {
        ActorList bombs{};
        bombs.name = fpcNm_NBOMB_e;
        fopAcM_Search(collect_by_name, &bombs);
        for (int i = 0; i < bombs.count; ++i) {
            daNbomb_c* bomb = static_cast<daNbomb_c*>(bombs.actors[i]);
            if (!bomb->mCcSph.ChkAtSet()) continue;
            if (!sphere_hits(hb, *bomb->mCcSph.GetCP(), bomb->mCcSph.GetR())) continue;
            send_hit(bomb->mCcSph, kSrcBomb, bomb->current.pos);
            break;
        }
    }
}

void apply_hit(const MsgPvpHit& hit) {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr || !pvp_active()) return;
    if (alink->mDamageTimer != 0 || dComIfGp_event_runCheck()) return;

    int dmg = (static_cast<int>(hit.atp) * effective_damage_percent() + 50) / 100;
    if (hit.atp > 0 && dmg < 1 && effective_damage_percent() > 0) dmg = 1;
    if (dmg > 255) dmg = 255;

    static dCcD_Sph s_attacker;
    s_attacker.SetAtType(hit.atType);
    s_attacker.SetAtAtp(hit.atp);
    s_attacker.SetAtSpl(static_cast<dCcG_At_Spl>(hit.spl));
    s_attacker.SetAtMtrl(hit.mtrl);

    const cXyz from(hit.from[0], hit.from[1], hit.from[2]);
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
    alink->mCcStts.PlusDmg(dmg);

    if (alink->checkUpperGuardAnime()) {
        const s16 toAttacker = cLib_targetAngleY(&alink->current.pos, &from);
        const s16 diff = static_cast<s16>(toAttacker - alink->shape_angle.y);
        if (diff > -0x3000 && diff < 0x3000) {
            tg.OnTgShieldHit();
            s_shieldHitSet = true;
        }
    }
    coop_log::info("coop_mod: [PVP] got hit: type={:#x} atp={} spl={} dmg={}", hit.atType, hit.atp,
        hit.spl, dmg);
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
}

bool pvp_active() {
    return false;
#if 0
    if (!coop_net_connected()) return false;
    return coop_net_is_host() ? cfg_bool(s_enabledVar, false) : s_hostEnabled;
#endif
}

void pvp_update() {
    ++s_tick;
    daAlink_c* alink = daAlink_getAlinkActorClass();

    if (s_shieldHitSet && alink != nullptr) {
        alink->mTgCyls[0].OffTgShieldHit();
        s_shieldHitSet = false;
    }

    if (!coop_net_connected()) return;

    if (coop_net_is_host()) {
        const bool enabled = cfg_bool(s_enabledVar, false);
        const uint16_t percent = local_damage_percent();
        if (s_needSend || enabled != s_sentEnabled || percent != s_sentDamagePercent) {
            send_state();
        }
    }

    if (!pvp_active() || alink == nullptr || dComIfGp_event_runCheck()) return;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!puppet_hook_get_position(&x, &y, &z)) return;
    const bool wolf = puppet_hook_is_wolf();
    Hurtbox hb{cXyz(x, y, z), wolf ? 45.0f : 35.0f, wolf ? 90.0f : 150.0f};
    check_attacks(alink, hb);
}

void pvp_on_connected() {
    s_needSend = true;
    s_hostEnabled = false;
    s_hostDamagePercent = 100;
}

void pvp_on_disconnected() {
    s_hostEnabled = false;
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
        apply_hit(msg);
    }
}

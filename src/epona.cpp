

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "SSystem/SComponent/c_lib.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_horse.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"

#include <cstring>

namespace {

const int kHorseStaleTicks = 12;

const int kIdleSendTicks = 15;

const int kIdleStaleTicks = 180;

const f32 kHorsePosBlend = 0.35f;

const f32 kHorseLeadFrames = (1.0f - kHorsePosBlend) / kHorsePosBlend;

const f32 kHorseMaxStep = 120.0f;

const f32 kHorseSnapDist = 400.0f;

struct RemoteHorse {
    bool active = false;
    uint8_t rider = kCoopNoPlayer;
    bool ridden = false;
    int age = 0;
    MsgHorse msg{};

    bool procKnown = false;
    uint8_t lastProc = 0xFF;

    bool anmKnown = false;
    uint16_t lastAnm[2] = {0xFFFF, 0xFFFF};

    bool haveVel = false;
    cXyz vel;
};
RemoteHorse s_remote[kCoopMaxPlayers];

bool s_wasRiding = false;
int s_idleTick = 0;
ConfigVarHandle s_enableVar = 0;

uint32_t s_sent = 0;
uint32_t s_applied = 0;
int s_diagTick = 0;

bool enabled() {

    if (s_enableVar == 0) return false;
    bool value = true;
    if (svc_config != nullptr) svc_config->get_bool(mod_ctx, s_enableVar, &value);
    return value;
}

bool player_on_our_stage(uint8_t playerId) {
    if (playerId >= kCoopMaxPlayers) return false;
    const CoopPeer& peer = features_peer_of(playerId);
    const char* stage = dComIfGp_getStartStageName();
    return peer.present && peer.inGame && stage != nullptr &&
           std::strncmp(stage, peer.stage, 8) == 0;
}

bool any_peer_on_our_stage() {
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (i != coop_net_local_id() && player_on_our_stage(static_cast<uint8_t>(i))) return true;
    }
    return false;
}

bool local_is_riding(daAlink_c* alink) {
    return alink != nullptr && alink->checkHorseRide() != 0;
}

uint8_t owner_of(fopAc_ac_c* horse) {
    if (horse == nullptr) return kCoopNoPlayer;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (horses_actor_for(static_cast<uint8_t>(i)) == horse) return static_cast<uint8_t>(i);
    }
    return coop_net_local_id();
}

void fill(MsgHorse& msg, daHorse_c* horse) {
    msg.pos[0] = horse->current.pos.x;
    msg.pos[1] = horse->current.pos.y;
    msg.pos[2] = horse->current.pos.z;
    msg.shapeAngleY = horse->shape_angle.y;
    msg.shapeAngleX = horse->shape_angle.x;
    msg.procID = horse->m_procID;
}

void send_state(daAlink_c* alink) {
    daHorse_c* ridden = local_is_riding(alink)
        ? static_cast<daHorse_c*>(dComIfGp_getHorseActor()) : nullptr;

    if (ridden == nullptr) {
        if (s_wasRiding) {

            MsgHorse msg{};
            msg.riding = 0;
            msg.owner = coop_net_local_id();
            daHorse_c* mine = static_cast<daHorse_c*>(horses_local());
            if (mine != nullptr) {
                fill(msg, mine);
                msg.owner = owner_of(mine);
            }
            msg.anmIdx[0] = 0xFFFF;
            msg.anmIdx[1] = 0xFFFF;
            coop_net_send(kMsgHorse, &msg, sizeof(msg));
            s_wasRiding = false;
        }

        if (++s_idleTick >= kIdleSendTicks) {
            s_idleTick = 0;
            daHorse_c* mine = static_cast<daHorse_c*>(horses_local());
            if (mine != nullptr) {
                MsgHorse msg{};
                fill(msg, mine);
                msg.riding = 0;
                msg.standing = 1;
                msg.owner = coop_net_local_id();
                msg.anmIdx[0] = 0xFFFF;
                msg.anmIdx[1] = 0xFFFF;
                coop_net_send(kMsgHorse, &msg, sizeof(msg));
            }
        }
        return;
    }

    MsgHorse msg{};
    fill(msg, ridden);
    msg.speedF = ridden->speedF;
    msg.riding = 1;
    msg.owner = owner_of(ridden);
    msg.anmIdx[0] = ridden->m_anmIdx[0];
    msg.anmIdx[1] = ridden->m_anmIdx[1];
    msg.anmRatio = ridden->m_anmRatio[1].getRatio();
    msg.anmFrame[0] = ridden->m_frameCtrl[0].getFrame();
    msg.anmFrame[1] = ridden->m_frameCtrl[1].getFrame();
    coop_net_send(kMsgHorse, &msg, sizeof(msg));
    ++s_sent;
    s_wasRiding = true;
}

void apply_proc(daHorse_c* horse, uint8_t procID) {
    switch (procID) {
    case daHorse_c::PROC_WAIT_e: horse->procWaitInit(); break;
    case daHorse_c::PROC_MOVE_e: horse->procMoveInit(); break;
    case daHorse_c::PROC_STOP_e: horse->procStopInit(); break;

    case daHorse_c::PROC_TURN_e: horse->procTurnInit(0); break;
    case daHorse_c::PROC_JUMP_e: horse->procJumpInit(0); break;
    case daHorse_c::PROC_LAND_e: horse->procLandInit(0.0f, 0); break;
    default: break;
    }
}

void apply_one(uint8_t owner, daAlink_c* alink) {
    RemoteHorse& state = s_remote[owner];
    if (!state.active) return;
    daHorse_c* horse = static_cast<daHorse_c*>(horses_actor_for(owner));
    if (horse == nullptr) {

        cXyz at(state.msg.pos[0], state.msg.pos[1], state.msg.pos[2]);
        horses_spawn_for(owner, at, state.msg.shapeAngleY);
        return;
    }

    if (local_is_riding(alink) && dComIfGp_getHorseActor() == horse) return;

    const MsgHorse& m = state.msg;

    if (state.ridden) {
        horse->attention_info.flags &= ~(fopAc_AttnFlag_ETC_e | fopAc_AttnFlag_SPEAK_e);
    }

    horse->offNoDrawWait();

    cXyz want(m.pos[0], m.pos[1], m.pos[2]);
    if (state.haveVel) {
        want.x += state.vel.x * kHorseLeadFrames;
        want.y += state.vel.y * kHorseLeadFrames;
        want.z += state.vel.z * kHorseLeadFrames;
    }
    const cXyz gap = want - horse->current.pos;
    if (gap.abs() > kHorseSnapDist) {
        horse->current.pos = want;
        horse->old.pos = want;
    } else {
        horse->current.pos.x += gap.x * kHorsePosBlend;
        horse->current.pos.y += gap.y * kHorsePosBlend;
        horse->current.pos.z += gap.z * kHorsePosBlend;
    }

    horse->shape_angle.y = m.shapeAngleY;
    horse->shape_angle.x = m.shapeAngleX;
    horse->current.angle.y = m.shapeAngleY;
    horse->speedF = m.speedF;

    if (!state.ridden) return;

    if (!state.procKnown || state.lastProc != m.procID) {
        apply_proc(horse, m.procID);
        state.procKnown = true;
        state.lastProc = m.procID;
        ++s_applied;
    }

    if (m.anmIdx[0] != 0xFFFF && m.anmIdx[1] != 0xFFFF &&
        (!state.anmKnown || state.lastAnm[0] != m.anmIdx[0] || state.lastAnm[1] != m.anmIdx[1]))
    {
        horse->setDoubleAnime(m.anmRatio, 1.0f, 1.0f, m.anmIdx[0], m.anmIdx[1], -1.0f);
        state.anmKnown = true;
        state.lastAnm[0] = m.anmIdx[0];
        state.lastAnm[1] = m.anmIdx[1];
    }
}

}

void horse_register_vars() {
    if (svc_config == nullptr) return;
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = "sync_horse";
    desc.type = CONFIG_VAR_BOOL;

    desc.default_bool = false;
    if (svc_config->register_var(mod_ctx, &desc, &s_enableVar) != MOD_OK) s_enableVar = 0;
}

ConfigVarHandle horse_enabled_var() {
    return s_enableVar;
}

bool horse_sync_enabled() {
    return enabled();
}

bool horse_peer_riding(uint8_t owner) {
    if (owner >= kCoopMaxPlayers) return false;
    return s_remote[owner].active && s_remote[owner].ridden;
}

void horse_reset() {
    for (int i = 0; i < kCoopMaxPlayers; ++i) s_remote[i] = RemoteHorse{};
    s_wasRiding = false;
    s_idleTick = 0;
}

void horse_update() {
    if (!coop_net_connected() || !enabled()) {
        horse_reset();
        return;
    }
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;

    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        RemoteHorse& state = s_remote[i];
        if (!state.active) continue;

        const int limit = state.ridden ? kHorseStaleTicks : kIdleStaleTicks;

        if (++state.age > limit || !player_on_our_stage(static_cast<uint8_t>(i))) {
            state = RemoteHorse{};
            horses_release(static_cast<uint8_t>(i));
        }
    }

    if (any_peer_on_our_stage()) send_state(alink);
    for (int i = 0; i < kCoopMaxPlayers; ++i) apply_one(static_cast<uint8_t>(i), alink);
    horses_update();

    if (++s_diagTick >= 300) {
        s_diagTick = 0;
        if (s_sent != 0 || s_applied != 0) {
            int known = 0;
            for (int i = 0; i < kCoopMaxPlayers; ++i) known += s_remote[i].active ? 1 : 0;
            coop_log::info("coop_mod: [HORSE] localRiding={} otherHorses={} sent={} applied={}",
                local_is_riding(alink) ? 1 : 0, known, s_sent, s_applied);
        }
    }
}

void horse_on_message(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < sizeof(MsgHorse) || !enabled()) return;
    MsgHorse msg;
    std::memcpy(&msg, payload, sizeof(msg));

    const uint8_t owner = msg.owner;
    if (owner >= kCoopMaxPlayers) return;
    RemoteHorse& state = s_remote[owner];

    if (msg.riding == 0 && msg.standing == 0) {

        daAlink_c* alink = daAlink_getAlinkActorClass();
        daHorse_c* horse = static_cast<daHorse_c*>(horses_actor_for(owner));
        const bool oursToTouch =
            horse != nullptr && !(local_is_riding(alink) && dComIfGp_getHorseActor() == horse);
        if (oursToTouch && state.active && state.rider == from) {
            horse->current.pos.set(msg.pos[0], msg.pos[1], msg.pos[2]);
            horse->shape_angle.y = msg.shapeAngleY;
            horse->current.angle.y = msg.shapeAngleY;
            horse->speedF = 0.0f;
            horse->procWaitInit();
        }
        if (state.rider == from) {

            state.ridden = false;
            state.rider = kCoopNoPlayer;
            state.msg = msg;
            state.haveVel = false;
            state.age = 0;
        }
        return;
    }

    const bool fresh = !state.active || (msg.riding != 0 && state.rider != from);
    if (fresh) {
        const MsgHorse keep = msg;
        state = RemoteHorse{};
        state.active = true;
        state.msg = keep;
    }
    state.rider = msg.riding != 0 ? from : kCoopNoPlayer;
    state.ridden = msg.riding != 0;

    if (!fresh && state.msg.riding != 0 && msg.riding != 0) {
        const cXyz prev(state.msg.pos[0], state.msg.pos[1], state.msg.pos[2]);
        cXyz step(msg.pos[0] - prev.x, msg.pos[1] - prev.y, msg.pos[2] - prev.z);
        if (step.abs() < kHorseMaxStep) {
            state.vel = step;
            state.haveVel = true;
        } else {
            state.haveVel = false;
        }
    }
    if (!state.ridden) state.haveVel = false;
    state.msg = msg;
    state.age = 0;
}

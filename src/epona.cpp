

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

const f32 kHorsePosBlend = 0.35f;

const f32 kHorseLeadFrames = (1.0f - kHorsePosBlend) / kHorsePosBlend;

const f32 kHorseMaxStep = 120.0f;

const f32 kHorseSnapDist = 400.0f;

struct RemoteHorse {
    bool active = false;
    uint8_t player = kCoopNoPlayer;
    int age = 0;
    MsgHorse msg{};

    bool procKnown = false;
    uint8_t lastProc = 0xFF;

    bool anmKnown = false;
    uint16_t lastAnm[2] = {0xFFFF, 0xFFFF};

    bool haveVel = false;
    cXyz vel;
};
RemoteHorse s_remote;

bool s_wasRiding = false;
ConfigVarHandle s_enableVar = 0;

uint32_t s_sent = 0;
uint32_t s_applied = 0;
int s_diagTick = 0;

bool enabled() {

    if (!features_debug_menu()) return false;
    if (s_enableVar == 0) return true;
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

daHorse_c* our_horse() {
    return dComIfGp_getHorseActor();
}

bool local_is_riding(daAlink_c* alink) {
    return alink != nullptr && alink->checkHorseRide() != 0;
}

void send_state(daAlink_c* alink) {
    daHorse_c* horse = our_horse();
    const bool riding = local_is_riding(alink) && horse != nullptr;

    if (!riding) {
        if (s_wasRiding) {

            MsgHorse msg{};
            msg.riding = 0;
            if (horse != nullptr) {
                msg.pos[0] = horse->current.pos.x;
                msg.pos[1] = horse->current.pos.y;
                msg.pos[2] = horse->current.pos.z;
                msg.shapeAngleY = horse->shape_angle.y;
                msg.shapeAngleX = horse->shape_angle.x;
                msg.procID = horse->m_procID;
            }
            msg.anmIdx[0] = 0xFFFF;
            msg.anmIdx[1] = 0xFFFF;
            coop_net_send(kMsgHorse, &msg, sizeof(msg));
            s_wasRiding = false;
        }
        return;
    }

    MsgHorse msg{};
    msg.pos[0] = horse->current.pos.x;
    msg.pos[1] = horse->current.pos.y;
    msg.pos[2] = horse->current.pos.z;
    msg.shapeAngleY = horse->shape_angle.y;
    msg.shapeAngleX = horse->shape_angle.x;
    msg.speedF = horse->speedF;
    msg.procID = horse->m_procID;
    msg.riding = 1;
    msg.anmIdx[0] = horse->m_anmIdx[0];
    msg.anmIdx[1] = horse->m_anmIdx[1];
    msg.anmRatio = horse->m_anmRatio[1].getRatio();
    msg.anmFrame[0] = horse->m_frameCtrl[0].getFrame();
    msg.anmFrame[1] = horse->m_frameCtrl[1].getFrame();
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

void apply_remote(daAlink_c* alink) {
    if (!s_remote.active) return;
    daHorse_c* horse = our_horse();
    if (horse == nullptr) return;

    if (local_is_riding(alink)) return;

    const MsgHorse& m = s_remote.msg;

    horse->attention_info.flags &= ~(fopAc_AttnFlag_ETC_e | fopAc_AttnFlag_SPEAK_e);

    horse->offNoDrawWait();

    cXyz want(m.pos[0], m.pos[1], m.pos[2]);
    if (s_remote.haveVel) {
        want.x += s_remote.vel.x * kHorseLeadFrames;
        want.y += s_remote.vel.y * kHorseLeadFrames;
        want.z += s_remote.vel.z * kHorseLeadFrames;
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

    if (!s_remote.procKnown || s_remote.lastProc != m.procID) {
        apply_proc(horse, m.procID);
        s_remote.procKnown = true;
        s_remote.lastProc = m.procID;
        ++s_applied;
    }

    if (m.anmIdx[0] != 0xFFFF && m.anmIdx[1] != 0xFFFF &&
        (!s_remote.anmKnown || s_remote.lastAnm[0] != m.anmIdx[0] ||
            s_remote.lastAnm[1] != m.anmIdx[1]))
    {
        horse->setDoubleAnime(m.anmRatio, 1.0f, 1.0f, m.anmIdx[0], m.anmIdx[1], -1.0f);
        s_remote.anmKnown = true;
        s_remote.lastAnm[0] = m.anmIdx[0];
        s_remote.lastAnm[1] = m.anmIdx[1];
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

void horse_reset() {
    s_remote = RemoteHorse{};
    s_wasRiding = false;
}

void horse_update() {
    if (!coop_net_connected() || !enabled()) {
        if (s_remote.active) horse_reset();
        return;
    }
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;

    if (s_remote.active && ++s_remote.age > kHorseStaleTicks) {

        s_remote = RemoteHorse{};
    }

    if (s_remote.active && !player_on_our_stage(s_remote.player)) s_remote = RemoteHorse{};

    if (any_peer_on_our_stage()) send_state(alink);
    apply_remote(alink);

    if (++s_diagTick >= 300) {
        s_diagTick = 0;
        if (s_sent != 0 || s_remote.active) {
            daHorse_c* horse = our_horse();
            coop_log::info("coop_mod: [HORSE] ours={} localRiding={} theirs(active={} from={} "
                            "proc={}) sent={} applied={}",
                horse != nullptr ? 1 : 0, local_is_riding(alink) ? 1 : 0,
                s_remote.active ? 1 : 0, static_cast<int>(s_remote.player),
                static_cast<int>(s_remote.msg.procID), s_sent, s_applied);
        }
    }
}

void horse_on_message(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < sizeof(MsgHorse) || !enabled()) return;
    MsgHorse msg;
    std::memcpy(&msg, payload, sizeof(msg));

    if (msg.riding == 0) {

        daAlink_c* alink = daAlink_getAlinkActorClass();
        daHorse_c* horse = our_horse();
        if (horse != nullptr && alink != nullptr && !local_is_riding(alink) && s_remote.active &&
            s_remote.player == from)
        {
            horse->current.pos.set(msg.pos[0], msg.pos[1], msg.pos[2]);
            horse->shape_angle.y = msg.shapeAngleY;
            horse->current.angle.y = msg.shapeAngleY;
            horse->speedF = 0.0f;
            horse->procWaitInit();
        }
        if (s_remote.player == from) s_remote = RemoteHorse{};
        return;
    }

    if (!s_remote.active || s_remote.player != from) {
        s_remote = RemoteHorse{};
        s_remote.active = true;
        s_remote.player = from;
    }

    if (s_remote.msg.riding != 0) {
        const cXyz prev(s_remote.msg.pos[0], s_remote.msg.pos[1], s_remote.msg.pos[2]);
        cXyz step(msg.pos[0] - prev.x, msg.pos[1] - prev.y, msg.pos[2] - prev.z);
        if (step.abs() < kHorseMaxStep) {
            s_remote.vel = step;
            s_remote.haveVel = true;
        } else {
            s_remote.haveVel = false;
        }
    }
    s_remote.msg = msg;
    s_remote.age = 0;
}

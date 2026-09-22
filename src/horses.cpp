

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "SSystem/SComponent/c_lib.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_horse.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include <cstdint>

namespace {

const f32 kClaimDist = 500.0f;

const f32 kClaimMargin = 60.0f;

struct PlayerHorse {
    fopAc_ac_c* actor = nullptr;
    uint32_t actorId = fpcM_ERROR_PROCESS_ID_e;
    bool spawned = false;
};
PlayerHorse s_horses[kCoopMaxPlayers];

fopAc_ac_c* s_localHorse = nullptr;

fopAc_ac_c* s_claimed = nullptr;
fopAc_ac_c* s_lastRidden = nullptr;

bool s_setting = false;

bool enabled() {

    return coop_net_connected() && horse_sync_enabled();
}

fopAc_ac_c* horse_of(uint8_t player) {
    if (player >= kCoopMaxPlayers) return nullptr;
    PlayerHorse& h = s_horses[player];
    if (h.actor == nullptr) return nullptr;
    if (fopAcM_SearchByID(h.actorId) != h.actor) {
        h.actor = nullptr;
        h.actorId = fpcM_ERROR_PROCESS_ID_e;
        h.spawned = false;
        return nullptr;
    }
    return h.actor;
}

void set_pointer(fopAc_ac_c* horse) {
    if (horse == nullptr || horse == s_claimed) return;
    s_setting = true;
    dComIfGp_setHorseActor(horse);
    s_setting = false;
    s_claimed = horse;
}

bool local_is_riding() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    return alink != nullptr && alink->checkHorseRide() != 0;
}

void choose_pointer() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;

    if (local_is_riding()) {

        fopAc_ac_c* riding = dComIfGp_getHorseActor();
        if (riding != nullptr) {
            s_lastRidden = riding;
            s_claimed = riding;
        }
        return;
    }

    const cXyz& me = alink->current.pos;
    fopAc_ac_c* best = nullptr;
    f32 bestDist = kClaimDist;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        fopAc_ac_c* horse = horse_of(static_cast<uint8_t>(i));
        if (horse == nullptr) continue;

        if (i != coop_net_local_id() && horse_peer_riding(static_cast<uint8_t>(i))) continue;
        cXyz gap = horse->current.pos - me;
        f32 dist = gap.abs();

        if (horse == s_claimed) dist -= kClaimMargin;
        if (dist < bestDist) {
            bestDist = dist;
            best = horse;
        }
    }

    if (best != nullptr) {
        set_pointer(best);
        return;
    }

    set_pointer(s_lastRidden != nullptr ? s_lastRidden : s_localHorse);
}

}

DEFINE_HOOK_SYMBOL("dComIfGp_setHorseActor", void(fopAc_ac_c*),
    HorsesSetHorseActorHook);

void horses_init() {

    if (!horse_sync_enabled()) return;
    mods::hook::add_pre<HorsesSetHorseActorHook>(
        [](ModContext*, void* args, void*, void*) -> HookAction {
            if (s_setting) return HOOK_CONTINUE;
            fopAc_ac_c*& horse = mods::arg_ref<fopAc_ac_c*>(args, 0);
            if (horse == nullptr) {

                if (s_claimed == s_localHorse) s_claimed = nullptr;
                if (s_lastRidden == s_localHorse) s_lastRidden = nullptr;
                s_horses[coop_net_local_id()] = PlayerHorse{};
                s_localHorse = nullptr;
                return HOOK_CONTINUE;
            }
            if (s_localHorse == nullptr) {

                s_localHorse = horse;
                PlayerHorse& mine = s_horses[coop_net_local_id()];
                mine.actor = horse;
                mine.actorId = fopAcM_GetID(horse);
                mine.spawned = false;
                s_claimed = horse;
                return HOOK_CONTINUE;
            }
            if (horse == s_localHorse) return HOOK_CONTINUE;

            return HOOK_SKIP_ORIGINAL;
        });
    coop_log::info("coop_mod: [HORSE] one Epona per player is armed (off unless switched on)");
}

bool horses_spawn_for(uint8_t player, const cXyz& pos, int16_t angleY) {
    if (player >= kCoopMaxPlayers || player == coop_net_local_id()) return false;
    if (horse_of(player) != nullptr) return true;
    if (!enabled()) return false;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return false;

    csXyz angle(0, angleY, 0);
    fopAc_ac_c* horse = fopAcM_fastCreate(fpcNm_HORSE_e, 0xFFFFFFFF, &pos,
        fopAcM_GetRoomNo(alink), &angle, nullptr, -1, nullptr, nullptr, 0, 0);
    if (horse == nullptr) return false;

    s_horses[player].actor = horse;
    s_horses[player].actorId = fopAcM_GetID(horse);
    s_horses[player].spawned = true;
    coop_log::info("coop_mod: [HORSE] spawned an Epona for player {}", static_cast<int>(player));
    return true;
}

void horses_release(uint8_t player) {
    if (player >= kCoopMaxPlayers) return;
    fopAc_ac_c* horse = horse_of(player);
    if (horse != nullptr && s_horses[player].spawned) {
        if (s_claimed == horse) s_claimed = nullptr;
        if (s_lastRidden == horse) s_lastRidden = nullptr;
        fopAcM_delete(horse);
        coop_log::info("coop_mod: [HORSE] took away player {}'s Epona", static_cast<int>(player));
    }
    s_horses[player] = PlayerHorse{};
}

fopAc_ac_c* horses_actor_for(uint8_t player) {
    return horse_of(player);
}

void horses_update() {
    if (!enabled()) return;
    choose_pointer();
}

void horses_on_disconnected() {
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (i != coop_net_local_id()) horses_release(static_cast<uint8_t>(i));
    }
    s_claimed = nullptr;
    s_lastRidden = nullptr;

    set_pointer(s_localHorse);
}

fopAc_ac_c* horses_local() {
    return s_localHorse;
}

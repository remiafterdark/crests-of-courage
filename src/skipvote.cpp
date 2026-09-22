

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "mods/svc/hook.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_event.h"
#include "m_Do/m_Do_controller_pad.h"
#include "m_Do/m_Do_graphic.h"

#include <cstring>

DEFINE_HOOK_SYMBOL("dEvt_control_c::skipper", bool(dEvt_control_c*), CoopSkipperHook);

namespace {

struct Watcher {
    bool inCutscene = false;
    bool voted = false;
    char stage[8] = {};
    uint32_t heardTick = 0;
};
uint32_t s_tick = 0;

const uint32_t kStaleTicks = 180;
Watcher s_remote[kCoopMaxPlayers];

bool s_voted = false;
bool s_skipping = false;
bool s_wasInCutscene = false;
uint32_t s_resendTicks = 0;
bool s_hooked = false;

bool local_in_cutscene() {
    return daAlink_getAlinkActorClass() != nullptr && boss_local_demo_running();
}

bool same_stage(const char* stage) {
    const char* mine = dComIfGp_getStartStageName();
    return mine != nullptr && std::strncmp(mine, stage, 8) == 0;
}

void count_votes(int& voted, int& needed) {
    voted = s_voted ? 1 : 0;
    needed = 1;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || !coop_net_player_present(id)) continue;
        const Watcher& w = s_remote[i];
        if (!w.inCutscene || !same_stage(w.stage) || s_tick - w.heardTick > kStaleTicks) continue;
        ++needed;
        if (w.voted) ++voted;
    }
}

bool vote_needed() {
    if (!coop_net_connected()) return false;
    int voted = 0;
    int needed = 0;
    count_votes(voted, needed);
    return needed > 1;
}

void announce() {
    MsgSkipVote msg{};
    msg.inCutscene = local_in_cutscene() ? 1 : 0;
    msg.voted = s_voted ? 1 : 0;
    const char* stage = dComIfGp_getStartStageName();
    if (stage != nullptr) std::strncpy(msg.stage, stage, sizeof(msg.stage));
    coop_net_send(kMsgSkipVote, &msg, sizeof(msg));
}

void say_count() {
    int voted = 0;
    int needed = 0;
    count_votes(voted, needed);
    const std::string line = std::to_string(voted) + "/" + std::to_string(needed) + " want to skip";
    coop_toast("Skip", line.c_str());
}

void begin_skip(dEvt_control_c* evt) {
    s_skipping = true;
    evt->mSkipTimer = -1;
    if (evt->mSkipFunc != nullptr && evt->mIsSkipFade) mDoGph_gInf_c::fadeOut(0.1f);
    coop_log::info("coop_mod: [SKIP] everybody voted - skipping");
}

HookAction on_skipper(ModContext*, void* args, void* retval, void*) {
    dEvt_control_c* evt = mods::arg<dEvt_control_c*>(args, 0);
    if (evt == nullptr || s_skipping || !local_in_cutscene() || !vote_needed()) {
        return HOOK_CONTINUE;
    }
    if (evt->mEventStatus != 1) return HOOK_CONTINUE;

    int voted = 0;
    int needed = 0;
    count_votes(voted, needed);
    if (s_voted && voted >= needed) {
        begin_skip(evt);
        return HOOK_CONTINUE;
    }

    const bool commit = mDoCPd_c::getTrigStart(PAD_1) && evt->mSkipTimer > 0;
    if (commit && !s_voted && evt->mSkipFunc != nullptr) {
        s_voted = true;
        evt->mSkipTimer = 0;
        announce();
        count_votes(voted, needed);
        coop_log::info("coop_mod: [SKIP] we voted to skip ({}/{})", voted, needed);
        if (voted >= needed) {
            begin_skip(evt);
            return HOOK_CONTINUE;
        }
        say_count();

        evt->offFlag2(8);
        if (retval != nullptr) *static_cast<bool*>(retval) = false;
        return HOOK_SKIP_ORIGINAL;
    }

    if (s_voted) {
        evt->offFlag2(8);
        if (retval != nullptr) *static_cast<bool*>(retval) = false;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

}

void skipvote_init() {
    s_hooked = mods::hook::add_pre<CoopSkipperHook>(on_skipper) == MOD_OK;
    coop_log::info("coop_mod: [SKIP] skip vote hook={}", s_hooked ? 1 : 0);
}

void skipvote_on_message(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < sizeof(MsgSkipVote) || from >= kCoopMaxPlayers) return;
    MsgSkipVote msg;
    std::memcpy(&msg, payload, sizeof(msg));
    Watcher& w = s_remote[from];
    const bool newVote = msg.voted != 0 && !w.voted;
    w.inCutscene = msg.inCutscene != 0;
    w.voted = msg.voted != 0 && w.inCutscene;
    std::memcpy(w.stage, msg.stage, sizeof(w.stage));
    w.heardTick = s_tick;
    if (newVote && local_in_cutscene() && same_stage(w.stage)) say_count();
}

void skipvote_update() {
    ++s_tick;
    if (!coop_net_connected()) {
        for (Watcher& w : s_remote) w = Watcher{};
        s_voted = false;
        s_skipping = false;
        s_wasInCutscene = false;
        return;
    }
    const bool now = local_in_cutscene();
    if (now != s_wasInCutscene) {
        s_wasInCutscene = now;
        if (!now) {

            s_voted = false;
            s_skipping = false;
        }
        announce();
        s_resendTicks = 0;
        return;
    }

    if (now && ++s_resendTicks >= 60) {
        s_resendTicks = 0;
        announce();
    }
}


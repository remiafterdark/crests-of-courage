

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_e_ym.h"
#include "d/actor/d_a_obj_drop.h"
#include "d/d_com_inf_game.h"
#include "d/d_stage.h"
#include "f_op/f_op_actor_mng.h"

#include <cstring>

namespace {

struct TrackedBug {
    bool used = false;
    fpc_ProcID id = fpcM_ERROR_PROCESS_ID_e;
    int8_t room = -1;
    uint8_t swBit = 0xFF;
    cXyz pos;
    uint32_t seenTick = 0;
};
const int kMaxBugs = 32;
TrackedBug s_bugs[kMaxBugs];
uint32_t s_tick = 0;
char s_stage[9] = {};

int s_lastCount = -1;
int s_lastArea = -1;
bool s_tboxWas[64] = {};

bool s_applying = false;

bool s_resync = true;

bool live() {
    return coop_net_connected() && daAlink_getAlinkActorClass() != nullptr &&
           dComIfGp_getStartStageName() != nullptr;
}

void reset_stage() {
    for (TrackedBug& b : s_bugs) b = TrackedBug{};
    for (bool& t : s_tboxWas) t = false;
    s_lastCount = -1;
    s_lastArea = -1;
    s_resync = true;
}

struct BugScan {
    daE_YM_c* bugs[kMaxBugs];
    int count = 0;
};

void* scan_bugs(void* proc, void* data) {
    auto* out = static_cast<BugScan*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || out->count >= kMaxBugs) return nullptr;
    if (fopAcM_GetName(actor) != fpcNm_E_YM_e) return nullptr;
    out->bugs[out->count++] = static_cast<daE_YM_c*>(actor);
    return nullptr;
}

struct DropFind {
    int save = -1;
    uint8_t swBit = 0xFF;
    daObjDrop_c* found = nullptr;
};

void* find_drop(void* proc, void* data) {
    auto* f = static_cast<DropFind*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || f->found != nullptr || fopAcM_GetName(actor) != fpcNm_Obj_Drop_e) {
        return nullptr;
    }
    auto* drop = static_cast<daObjDrop_c*>(actor);
    if ((f->save >= 0 && drop->getSave() == f->save) ||
        (f->swBit != 0xFF && drop->getYmSwbit() == f->swBit)) {
        f->found = drop;
    }
    return nullptr;
}

void send_kill(const TrackedBug& b) {
    MsgTwilightBug msg{};
    std::memcpy(msg.stage, s_stage, 8);
    msg.room = b.room;
    msg.swBit = b.swBit;
    msg.pos[0] = b.pos.x;
    msg.pos[1] = b.pos.y;
    msg.pos[2] = b.pos.z;
    coop_net_send(kMsgTwilightBug, &msg, sizeof(msg));
    coop_log::info("coop_mod: [TWILIGHT] bug {} in room {} killed here - telling the others",
        static_cast<int>(b.swBit), static_cast<int>(b.room));
}

void watch_bugs() {
    BugScan scan;
    fopAcM_Search(scan_bugs, &scan);
    for (int i = 0; i < scan.count; ++i) {
        daE_YM_c* bug = scan.bugs[i];
        const fpc_ProcID id = fopAcM_GetID(bug);
        TrackedBug* slot = nullptr;
        TrackedBug* free = nullptr;
        for (TrackedBug& b : s_bugs) {
            if (b.used && b.id == id) slot = &b;
            if (!b.used && free == nullptr) free = &b;
        }
        if (slot == nullptr) {
            if (free == nullptr || bug->getSwitchBit() == 0xFF) continue;
            slot = free;
            *slot = TrackedBug{};
            slot->used = true;
            slot->id = id;
            slot->room = static_cast<int8_t>(fopAcM_GetRoomNo(bug));
            slot->swBit = bug->getSwitchBit();
        }
        slot->pos = bug->current.pos;
        slot->seenTick = s_tick;
    }

    for (TrackedBug& b : s_bugs) {
        if (!b.used || b.seenTick == s_tick) continue;
        const TrackedBug was = b;
        b = TrackedBug{};
        if (was.seenTick + 1 != s_tick) continue;
        if (fopAcM_SearchByID(was.id) != nullptr) continue;
        if (!dComIfGs_isSwitch(was.swBit, was.room)) continue;
        send_kill(was);
    }
}

void watch_tears() {
    const int area = dComIfGp_getStartStageDarkArea();
    if (area < 0 || area > 3) {

        s_lastArea = area;
        s_resync = true;
        return;
    }
    const int count = dComIfGs_getLightDropNum(static_cast<u8>(area));
    bool flipped[64] = {};
    for (int i = 0; i < 64; ++i) {
        const bool now = dComIfGs_isTbox(i) != 0;
        flipped[i] = now && !s_tboxWas[i] && !s_resync;
        s_tboxWas[i] = now;
    }
    const bool rose = !s_resync && area == s_lastArea && s_lastCount >= 0 && count > s_lastCount;
    s_lastArea = area;
    s_lastCount = count;
    s_resync = false;
    if (!rose) return;

    for (int i = 0; i < 64; ++i) {
        if (!flipped[i]) continue;
        MsgTearGot msg{};
        std::memcpy(msg.stage, s_stage, 8);
        msg.saveNo = static_cast<int8_t>(dStage_stagInfo_GetSaveTbl(dComIfGp_getStageStagInfo()));
        msg.area = static_cast<uint8_t>(area);
        msg.save = static_cast<uint8_t>(i);
        msg.count = static_cast<uint8_t>(count);
        coop_net_send(kMsgTearGot, &msg, sizeof(msg));
        coop_log::info("coop_mod: [TWILIGHT] tear {} picked up here ({} in area {})", i, count,
            area);
        return;
    }
}

bool same_stage(const char* stage) {
    return std::strncmp(stage, s_stage, 8) == 0;
}

void on_bug_killed(const MsgTwilightBug& msg) {
    if (msg.swBit == 0xFF || !same_stage(msg.stage)) return;
    s_applying = true;
    const cXyz at(msg.pos[0], msg.pos[1], msg.pos[2]);
    BugScan scan;
    fopAcM_Search(scan_bugs, &scan);
    for (int i = 0; i < scan.count; ++i) {
        daE_YM_c* bug = scan.bugs[i];
        if (bug->getSwitchBit() != msg.swBit || fopAcM_GetRoomNo(bug) != msg.room) continue;

        bug->current.pos = at;
        bug->old.pos = at;
        DropFind find;
        find.swBit = msg.swBit;
        fopAcM_Search(find_drop, &find);
        if (find.found != nullptr) {
            find.found->current.pos = at;
            find.found->old.pos = at;
        }
        fopAcM_createDisappear(bug, &at, 10, 1, 0xFF);

        for (TrackedBug& b : s_bugs) {
            if (b.used && b.id == fopAcM_GetID(bug)) b = TrackedBug{};
        }
        fopAcM_delete(bug);
    }
    if (!dComIfGs_isSwitch(msg.swBit, msg.room)) dComIfGs_onSwitch(msg.swBit, msg.room);
    s_applying = false;
    coop_log::info("coop_mod: [TWILIGHT] bug {} in room {} was killed by somebody else",
        static_cast<int>(msg.swBit), static_cast<int>(msg.room));
}

void on_tear_got(const MsgTearGot& msg) {
    if (msg.area > 3 || msg.save >= 64) return;
    s_applying = true;
    int count = dComIfGs_getLightDropNum(msg.area);
    if (same_stage(msg.stage)) {
        if (!dComIfGs_isTbox(msg.save)) {
            dComIfGs_onTbox(msg.save);
            ++count;
        }
        s_tboxWas[msg.save] = true;
        DropFind find;
        find.save = msg.save;
        fopAcM_Search(find_drop, &find);
        if (find.found != nullptr) fopAcM_delete(find.found);
    } else if (msg.saveNo >= 0 && msg.saveNo < dSv_save_c::STAGE_MAX &&
               dComIfGs_getSaveInfo() != nullptr) {

        dSv_memBit_c& bits = dComIfGs_getSaveInfo()->getSavedata().getSave(msg.saveNo).getBit();
        if (!bits.isTbox(msg.save)) {
            bits.onTbox(msg.save);
            ++count;
        }
    }
    if (msg.count > count) count = msg.count;
    if (count > 16) count = 16;
    dComIfGs_setLightDropNum(msg.area, static_cast<u8>(count));

    if (msg.area == 2 && count == 15) dComIfGs_onEventBit(0x0180);

    s_tboxWas[msg.save] = true;
    s_lastArea = dComIfGp_getStartStageDarkArea();
    if (s_lastArea >= 0 && s_lastArea <= 3) {
        s_lastCount = dComIfGs_getLightDropNum(static_cast<u8>(s_lastArea));
    }
    s_applying = false;
    coop_log::info("coop_mod: [TWILIGHT] somebody picked up tear {} ({} in area {})",
        static_cast<int>(msg.save), count, static_cast<int>(msg.area));
}

}

void twilight_update() {
    ++s_tick;
    if (!live()) {
        reset_stage();
        s_stage[0] = '\0';
        return;
    }
    const char* stage = dComIfGp_getStartStageName();
    if (std::strncmp(stage, s_stage, 8) != 0) {
        reset_stage();
        std::strncpy(s_stage, stage, 8);
        s_stage[8] = '\0';
    }
    if (s_applying) return;
    watch_bugs();
    watch_tears();
}

void twilight_on_message(uint8_t type, const uint8_t* payload, size_t size) {
    if (!live()) return;
    if (type == kMsgTwilightBug && size >= sizeof(MsgTwilightBug)) {
        MsgTwilightBug msg;
        std::memcpy(&msg, payload, sizeof(msg));
        on_bug_killed(msg);
    } else if (type == kMsgTearGot && size >= sizeof(MsgTearGot)) {
        MsgTearGot msg;
        std::memcpy(&msg, payload, sizeof(msg));
        on_tear_got(msg);
    }
}

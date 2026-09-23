

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_arrow.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"

#include <cstring>

namespace {

const int kTrackMax = kCoopMaxPlayers * 16;
const fpc_ProcID kNoId = static_cast<fpc_ProcID>(-1);

fpc_ProcID s_reported[kTrackMax];
int s_reportedNext = 0;
fpc_ProcID s_remote[kTrackMax];
int s_remoteNext = 0;
bool s_inited = false;

struct PendingShot {
    bool active = false;
    fpc_ProcID id = kNoId;
    int age = 0;
    MsgArrowShot shot{};
};
PendingShot s_pending[kCoopMaxPlayers * 2];

void init_once() {
    if (s_inited) return;
    for (int i = 0; i < kTrackMax; ++i) {
        s_reported[i] = kNoId;
        s_remote[i] = kNoId;
    }
    s_inited = true;
}

bool in_list(const fpc_ProcID* list, fpc_ProcID id) {
    for (int i = 0; i < kTrackMax; ++i) {
        if (list[i] == id) return true;
    }
    return false;
}

void push_id(fpc_ProcID* list, int& next, fpc_ProcID id) {
    list[next] = id;
    next = (next + 1) % kTrackMax;
}

const int kArrowListMax = kCoopMaxPlayers * 4;

struct ArrowList {
    daArrow_c* arrows[kArrowListMax];
    int count;
};

void* collect_arrows(void* proc, void* data) {
    auto* list = static_cast<ArrowList*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor != nullptr && list->count < kArrowListMax && fopAcM_GetName(actor) == fpcNm_ARROW_e) {
        list->arrows[list->count++] = static_cast<daArrow_c*>(actor);
    }
    return nullptr;
}

void apply_launch(daArrow_c* arrow, const MsgArrowShot& shot) {
    const cXyz start(shot.startPos[0], shot.startPos[1], shot.startPos[2]);
    arrow->current.pos = start;
    arrow->old.pos = start;
    arrow->mStartPos = start;
    arrow->current.angle.x = shot.angleX;
    arrow->current.angle.y = shot.angleY;
    arrow->shape_angle.x = shot.shapeX;
    arrow->shape_angle.y = shot.shapeY;
    arrow->speed.set(shot.speed[0], shot.speed[1], shot.speed[2]);
    arrow->mFlyMax = shot.flyMax;
    arrow->field_0x99c = shot.flySpeed;
    if (arrow->mArrowType != daArrow_c::ARROW_TYPE_SLING && shot.flySpeed > 0.0f) {
        arrow->mOutLengthRate = 95.0f / shot.flySpeed;
    }
}

}

bool projectiles_is_remote(fopAc_ac_c* actor) {
    init_once();
    return actor != nullptr && in_list(s_remote, fopAcM_GetID(actor));
}

void projectiles_update() {
    init_once();
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) {
        for (PendingShot& p : s_pending) p.active = false;
        return;
    }

    for (PendingShot& p : s_pending) {
        if (!p.active) continue;
        fopAc_ac_c* actor = fopAcM_SearchByID(p.id);
        if (actor == nullptr) {
            p.active = false;
            continue;
        }
        daArrow_c* arrow = static_cast<daArrow_c*>(actor);
        if (arrow->speedF == 100.0f) {
            apply_launch(arrow, p.shot);
            p.active = false;
        } else if (++p.age > 10) {
            p.active = false;
        }
    }

    if (!coop_net_connected()) return;

    ArrowList list{};
    fopAcM_Search(collect_arrows, &list);
    for (int i = 0; i < list.count; ++i) {
        daArrow_c* arrow = list.arrows[i];
        const fpc_ProcID id = fopAcM_GetID(arrow);
        if (in_list(s_remote, id) || in_list(s_reported, id)) continue;
        const u32 param = fopAcM_GetParam(arrow);
        if (param != 1 && param != 2) continue;
        if (arrow->speedF != 100.0f) continue;
        push_id(s_reported, s_reportedNext, id);
        if (arrow->mArrowType == daArrow_c::ARROW_TYPE_LIGHT) continue;

        MsgArrowShot shot{};
        shot.type = arrow->mArrowType;
        shot.param = static_cast<uint8_t>(param);
        shot.startPos[0] = arrow->mStartPos.x;
        shot.startPos[1] = arrow->mStartPos.y;
        shot.startPos[2] = arrow->mStartPos.z;
        shot.angleX = arrow->current.angle.x;
        shot.angleY = arrow->current.angle.y;
        shot.shapeX = arrow->shape_angle.x;
        shot.shapeY = arrow->shape_angle.y;
        shot.speed[0] = arrow->speed.x;
        shot.speed[1] = arrow->speed.y;
        shot.speed[2] = arrow->speed.z;
        shot.flyMax = arrow->mFlyMax;
        shot.flySpeed = arrow->field_0x99c;
        coop_net_send(kMsgArrowShot, &shot, sizeof(shot));
    }
}

void projectiles_on_message(const uint8_t* payload, size_t size) {
    init_once();
    if (size < sizeof(MsgArrowShot)) return;
    MsgArrowShot shot;
    std::memcpy(&shot, payload, sizeof(shot));
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr || !peer_on_our_stage()) return;
    if (shot.type != daArrow_c::ARROW_TYPE_NORMAL && shot.type != daArrow_c::ARROW_TYPE_BOMB &&
        shot.type != daArrow_c::ARROW_TYPE_SLING)
    {
        return;
    }
    const u8 param = (shot.type == daArrow_c::ARROW_TYPE_SLING) ? 1 : (shot.param == 2 ? 2 : 1);
    cXyz pos(shot.startPos[0], shot.startPos[1], shot.startPos[2]);
    csXyz angle(shot.angleX, shot.angleY, 0);
    fopAc_ac_c* actor = fopAcM_fastCreate(fpcNm_ARROW_e, (static_cast<u32>(shot.type) << 8) | param,
        &pos, fopAcM_GetRoomNo(alink), &angle, nullptr, -1, nullptr, nullptr);
    if (actor == nullptr) {
        coop_log::warn("coop_mod: [ARROW] could not spawn their arrow (type {})", shot.type);
        return;
    }
    push_id(s_remote, s_remoteNext, fopAcM_GetID(actor));
    daArrow_c* arrow = static_cast<daArrow_c*>(actor);
    if (arrow->speedF == 100.0f) {
        apply_launch(arrow, shot);
        return;
    }
    for (PendingShot& p : s_pending) {
        if (p.active) continue;
        p.active = true;
        p.id = fopAcM_GetID(actor);
        p.age = 0;
        p.shot = shot;
        return;
    }
}

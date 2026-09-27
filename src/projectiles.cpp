

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_arrow.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_layer.h"

#include <cmath>
#include <cstring>

namespace {

const int kTrackMax = kCoopMaxPlayers * 16;
const fpc_ProcID kNoId = static_cast<fpc_ProcID>(-1);

fpc_ProcID s_reported[kTrackMax];
int s_reportedNext = 0;
fpc_ProcID s_remote[kTrackMax];
int s_remoteNext = 0;

fpc_ProcID s_stopped[kTrackMax];
int s_stoppedNext = 0;
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
        s_stopped[i] = kNoId;
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

bool segment_meets_body(const cXyz& a, const cXyz& b, const cXyz& feet, f32 radius, f32 height) {

    const f32 dx = b.x - a.x, dz = b.z - a.z;
    const f32 len2 = dx * dx + dz * dz;
    f32 t = 0.0f;
    if (len2 > 0.0001f) {
        t = ((feet.x - a.x) * dx + (feet.z - a.z) * dz) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    const f32 px = a.x + dx * t - feet.x, pz = a.z + dz * t - feet.z;
    if (px * px + pz * pz > radius * radius) return false;
    const f32 y = a.y + (b.y - a.y) * t;
    return y >= feet.y - 10.0f && y <= feet.y + height;
}

void stop_remote_arrows_on_us(daAlink_c* alink) {
    if (!pvp_active()) return;
    const bool wolf = alink->checkWolf() != 0;
    const f32 radius = wolf ? 45.0f : 35.0f;
    const f32 height = wolf ? 90.0f : 150.0f;
    ArrowList list{};
    fopAcM_Search(collect_arrows, &list);
    for (int i = 0; i < list.count; ++i) {
        daArrow_c* arrow = list.arrows[i];
        const fpc_ProcID id = fopAcM_GetID(arrow);
        if (!in_list(s_remote, id) || in_list(s_stopped, id)) continue;
        const u32 param = fopAcM_GetParam(arrow);
        if ((param != 1 && param != 2) || arrow->speedF <= 0.0f || arrow->field_0x93f != 0) continue;
        const cXyz from = arrow->current.pos;
        const cXyz to(from.x + arrow->speed.x, from.y + arrow->speed.y, from.z + arrow->speed.z);
        if (!segment_meets_body(from, to, alink->current.pos, radius, height)) continue;
        push_id(s_stopped, s_stoppedNext, id);

        cXyz at = from;
        cXyz toUs(alink->current.pos.x - from.x, 0.0f, alink->current.pos.z - from.z);
        const f32 d = std::sqrt(toUs.x * toUs.x + toUs.z * toUs.z);
        if (d > radius) {
            const f32 k = (d - radius) / d;
            at.set(from.x + toUs.x * k, from.y + (to.y - from.y) * k, from.z + toUs.z * k);
        }
        if (arrow->mArrowType == daArrow_c::ARROW_TYPE_SLING) {
            arrow->current.pos = at;
            arrow->procSlingHitInit(&at, nullptr);
        } else {
            arrow->current.pos = at;
            arrow->speedF = 0.0f;
            arrow->speed.set(0.0f, 0.0f, 0.0f);
            arrow->field_0x93f = 1;
        }
        coop_log::info("coop_mod: [PVP] their {} stopped on us",
            arrow->mArrowType == daArrow_c::ARROW_TYPE_SLING ? "pellet" : "arrow");
    }
}

struct BombArrowSeen {
    fpc_ProcID id = kNoId;
    cXyz pos;
    bool ours = false;
    bool seen = false;
};
BombArrowSeen s_bombArrows[kArrowListMax];

struct BlastSite {
    cXyz pos;
    u32 frame = 0;
    bool used = false;
};
const int kBlastSites = 16;
const u32 kBlastMemoryFrames = 90;
const f32 kBlastReach = 300.0f;
BlastSite s_ourBlasts[kBlastSites];
int s_ourBlastNext = 0;
u32 s_frame = 0;

void follow_bomb_arrows(const ArrowList& list) {
    ++s_frame;
    for (BombArrowSeen& b : s_bombArrows) b.seen = false;
    for (int i = 0; i < list.count; ++i) {
        daArrow_c* arrow = list.arrows[i];
        if (arrow->mArrowType != daArrow_c::ARROW_TYPE_BOMB) continue;
        const fpc_ProcID id = fopAcM_GetID(arrow);
        BombArrowSeen* slot = nullptr;
        BombArrowSeen* free = nullptr;
        for (BombArrowSeen& b : s_bombArrows) {
            if (b.id == id) slot = &b;
            if (b.id == kNoId && free == nullptr) free = &b;
        }
        if (slot == nullptr) slot = free;
        if (slot == nullptr) continue;
        slot->id = id;
        slot->pos = arrow->current.pos;
        slot->ours = !in_list(s_remote, id);
        slot->seen = true;
    }
    for (BombArrowSeen& b : s_bombArrows) {
        if (b.id == kNoId || b.seen) continue;
        if (b.ours) {
            BlastSite& site = s_ourBlasts[s_ourBlastNext];
            s_ourBlastNext = (s_ourBlastNext + 1) % kBlastSites;
            site.pos = b.pos;
            site.frame = s_frame;
            site.used = true;
        }
        b = BombArrowSeen{};
    }
}

}

bool projectiles_blast_is_ours(fopAc_ac_c* blast) {
    if (blast == nullptr) return false;
    for (const BlastSite& site : s_ourBlasts) {
        if (!site.used || s_frame - site.frame > kBlastMemoryFrames) continue;
        const f32 dx = blast->current.pos.x - site.pos.x, dy = blast->current.pos.y - site.pos.y,
                  dz = blast->current.pos.z - site.pos.z;
        if (dx * dx + dy * dy + dz * dz <= kBlastReach * kBlastReach) return true;
    }
    return false;
}

layer_class* coop_enter_actor_layer() {
    layer_class* prev = fpcLy_CurrentLayer();
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink != nullptr) {
        layer_class* linkLayer = static_cast<base_process_class*>(alink)->layer_tag.layer;
        if (linkLayer != nullptr) fpcLy_SetCurrentLayer(linkLayer);
    }
    return prev;
}

void coop_leave_actor_layer(layer_class* prev) {
    if (prev != nullptr) fpcLy_SetCurrentLayer(prev);
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
    stop_remote_arrows_on_us(alink);

    ArrowList list{};
    fopAcM_Search(collect_arrows, &list);
    follow_bomb_arrows(list);
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
    fopAc_ac_c* actor = nullptr;
    {
        CoopActorLayer layer;
        actor = fopAcM_fastCreate(fpcNm_ARROW_e, (static_cast<u32>(shot.type) << 8) | param, &pos,
            fopAcM_GetRoomNo(alink), &angle, nullptr, -1, nullptr, nullptr);
    }
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

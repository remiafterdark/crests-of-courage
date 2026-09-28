

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_spinner.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "SSystem/SComponent/c_math.h"

#include <cmath>

namespace {

const f32 kTheirRadius = 40.0f;
const f32 kMaxHeightGap = 60.0f;
const int kCooldownTicks = 20;
const int kHitsToFall = 3;

int s_cooldown[kCoopMaxPlayers] = {};
int s_hitsTaken = 0;
fpc_ProcID s_ride = fpcM_ERROR_PROCESS_ID_e;

}

void spinner_after_player(daAlink_c* alink) {
    for (int& c : s_cooldown) {
        if (c > 0) --c;
    }
    if (alink == nullptr || !alink->checkSpinnerRide() || !coop_net_connected()) return;
    daSpinner_c* mine = alink->getSpinnerActor();
    if (mine == nullptr || mine->getDeleteFlg()) return;

    if (fopAcM_GetID(mine) != s_ride) {
        s_ride = fopAcM_GetID(mine);
        s_hitsTaken = 0;
    }
    const f32 myRadius = mine->mCyl.GetR();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || s_cooldown[i] > 0 || !puppet_hook_rides_spinner(id)) continue;
        f32 x = 0.0f, y = 0.0f, z = 0.0f, vx = 0.0f, vz = 0.0f;
        if (!puppet_hook_get_pose_of(id, &x, &y, &z, nullptr, &vx, &vz)) continue;
        if (std::fabs(mine->current.pos.y - y) > kMaxHeightGap) continue;
        const f32 dx = mine->current.pos.x - x;
        const f32 dz = mine->current.pos.z - z;
        const f32 dist = std::sqrt(dx * dx + dz * dz);
        const f32 reach = myRadius + kTheirRadius;
        if (dist >= reach || dist < 0.01f) continue;
        s_cooldown[i] = kCooldownTicks;

        const f32 nx = dx / dist;
        const f32 nz = dz / dist;
        const f32 myClosing = -(mine->speedF * (cM_ssin(mine->current.angle.y) * nx +
                                                cM_scos(mine->current.angle.y) * nz));
        const f32 theirClosing = vx * nx + vz * nz;

        const bool bladesOut = mine->reflectAccept();
        mine->setWallHit(cM_atan2s(nx, nz), 0);
        const f32 push = reach - dist;
        mine->current.pos.x += nx * push;
        mine->current.pos.z += nz * push;

        if (!pvp_active() || bladesOut || theirClosing + 1.0f < myClosing) continue;
        if (++s_hitsTaken < kHitsToFall) continue;
        coop_log::info("coop_mod: [SPINNER] knocked off by player {}", static_cast<int>(id));
        mine->forceDelete();
        s_hitsTaken = 0;
    }
}

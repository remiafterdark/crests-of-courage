

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "d/actor/d_a_grass.h"
#include "d/actor/d_flower.h"
#include "d/actor/d_grass.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "m_Do/m_Do_audio.h"

#include <cmath>
#include <cstring>

namespace {

const int kGrassSlots = 1500;
const int kGrassRooms = 64;

const f32 kGrassMatchDist = 8.0f;

bool s_wasCut[kGrassSlots];
bool s_wasLive[kGrassSlots];
bool s_primed = false;

const int kFlowerSlots = 1000;
const u8 kFlowerCutBits = 0x08 | 0x10;
u8 s_flowerBits[kFlowerSlots];
bool s_flowerLive[kFlowerSlots];
bool s_flowerPrimed = false;

MsgGrassCut s_out{};
uint32_t s_sent = 0;
uint32_t s_applied = 0;
uint32_t s_unmatched = 0;

bool enabled() {
    ConfigVarHandle h = enemies_breakables_var();
    bool on = true;
    if (h != 0 && svc_config != nullptr) svc_config->get_bool(mod_ctx, h, &on);
    return coop_session(kSessWorldObjects, on);
}

void flush() {
    if (s_out.count == 0) return;
    coop_net_send(kMsgGrassCut, &s_out, sizeof(s_out));
    s_sent += s_out.count;
    s_out.count = 0;
}

void queue_cut(int room, const cXyz& pos, uint8_t kind = kGrassKindGrass, uint8_t bits = 0) {
    if (s_out.count != 0 && (s_out.room != room || s_out.kind != kind)) flush();
    s_out.room = static_cast<int8_t>(room);
    s_out.kind = kind;
    s_out.bits[s_out.count] = bits;
    s_out.pos[s_out.count][0] = pos.x;
    s_out.pos[s_out.count][1] = pos.y;
    s_out.pos[s_out.count][2] = pos.z;
    if (++s_out.count >= kGrassCutsPerMsg) flush();
}

int slot_of(dGrass_packet_c* grass, dGrass_data_c* data) {
    const ptrdiff_t idx = data - grass->getData();
    return (idx >= 0 && idx < kGrassSlots) ? static_cast<int>(idx) : -1;
}

void scan(dGrass_packet_c* grass, bool report) {
    bool seen[kGrassSlots] = {};
    for (int r = 0; r < kGrassRooms; ++r) {
        for (dGrass_data_c* d = grass->m_room[r].getData(); d != nullptr; d = d->mp_next) {
            const int i = slot_of(grass, d);
            if (i < 0) break;
            seen[i] = true;
            const bool cut = d->m_state != 0 && d->field_0x02 < 0;
            if (report && s_primed && s_wasLive[i] && !s_wasCut[i] && cut) queue_cut(r, d->m_pos);
            s_wasLive[i] = d->m_state != 0;
            s_wasCut[i] = cut;
        }
    }
    for (int i = 0; i < kGrassSlots; ++i) {
        if (!seen[i]) {
            s_wasLive[i] = false;
            s_wasCut[i] = false;
        }
    }
    s_primed = true;
}

int flower_slot_of(dFlower_packet_c* flower, dFlower_data_c* data) {
    const ptrdiff_t idx = data - flower->getData();
    return (idx >= 0 && idx < kFlowerSlots) ? static_cast<int>(idx) : -1;
}

void scan_flowers(dFlower_packet_c* flower, bool report) {
    bool seen[kFlowerSlots] = {};
    for (int r = 0; r < kGrassRooms; ++r) {
        for (dFlower_data_c* d = flower->m_room[r].getData(); d != nullptr; d = d->mp_next) {
            const int i = flower_slot_of(flower, d);
            if (i < 0) break;
            seen[i] = true;
            const u8 bits = d->m_state & kFlowerCutBits;
            if (report && s_flowerPrimed && s_flowerLive[i] && (bits & ~s_flowerBits[i]) != 0) {
                queue_cut(r, d->m_pos, kGrassKindFlower, bits);
            }
            s_flowerLive[i] = true;
            s_flowerBits[i] = bits;
        }
    }
    for (int i = 0; i < kFlowerSlots; ++i) {
        if (!seen[i]) {
            s_flowerLive[i] = false;
            s_flowerBits[i] = 0;
        }
    }
    s_flowerPrimed = true;
}

void cut_flower(dFlower_data_c* d, u8 bits, int room) {
    const u8 before = d->m_state & kFlowerCutBits;
    d->m_state |= bits;
    if ((bits & ~before) == 0) return;
    d->deleteAnm();
    cXyz at(d->m_pos.x, d->m_pos.y, d->m_pos.z);
    dKy_tevstr_c* tev = dComIfGp_roomControl_getTevStr(room);
    if (d->m_state & 0x40) {
        dComIfGp_particle_set(0x8297, &at, tev, nullptr, nullptr);
        dComIfGp_particle_set(0x8298, &at, tev, nullptr, nullptr);
    } else {
        dComIfGp_particle_set(0x8299, &at, tev, nullptr, nullptr);
        dComIfGp_particle_set(0x829A, &at, tev, nullptr, nullptr);
    }
    mDoAud_seStart(JA_SE_LK_CUT_GRASS, &d->m_pos, 0, dComIfGp_getReverb(room));
}

void cut_clump(dGrass_packet_c* grass, dGrass_data_c* d, int room) {
    if (d->field_0x02 >= 16) grass->deleteAnm(d->field_0x02);
    d->field_0x02 = -1;

    u16 particle = 0x89D7;
    if (d->field_0x05 >= 7 && d->field_0x05 <= 9) {
        particle = 0x89D6;
    } else if (d->field_0x05 >= 4 && d->field_0x05 <= 6) {
        particle = 0x89D8;
    }
    cXyz at(d->m_pos.x, d->m_pos.y + 25.0f, d->m_pos.z);
    csXyz rot(0, 0, 0);
    GXColor env;
    env.r = static_cast<u8>(d->m_addCol >> 8);
    env.g = static_cast<u8>(d->m_addCol & 0xFF);
    env.b = 0;
    env.a = 0;
    dComIfGp_particle_set(particle, &at, dComIfGp_roomControl_getTevStr(room), &rot, nullptr, 255,
        dPa_control_c::getLight8EcallBack(), -1, &env, nullptr, nullptr);
    mDoAud_seStart(JA_SE_LK_CUT_GRASS, &d->m_pos, 0, dComIfGp_getReverb(room));
}

}

void grass_reset() {
    s_primed = false;
    s_flowerPrimed = false;
    std::memset(s_flowerBits, 0, sizeof(s_flowerBits));
    std::memset(s_flowerLive, 0, sizeof(s_flowerLive));
    s_out.count = 0;
    std::memset(s_wasCut, 0, sizeof(s_wasCut));
    std::memset(s_wasLive, 0, sizeof(s_wasLive));
}

void grass_update() {
    dGrass_packet_c* grass = daGrass_c::getGrass();
    dFlower_packet_c* flower = daGrass_c::getFlower();
    if (!coop_net_connected() || !enabled()) {
        s_primed = false;
        s_flowerPrimed = false;
        return;
    }
    if (grass == nullptr) s_primed = false;
    if (flower == nullptr) s_flowerPrimed = false;
    if (grass == nullptr && flower == nullptr) return;

    const bool report = peer_on_our_stage();
    if (grass != nullptr) scan(grass, report);
    if (flower != nullptr) scan_flowers(flower, report);
    if (report) flush();
    else s_out.count = 0;

    static uint32_t s_logged = 0;
    const uint32_t total = s_sent + s_applied + s_unmatched;
    if (total != s_logged && total - s_logged >= 20) {
        s_logged = total;
        coop_log::info("coop_mod: [GRASS] sent={} applied={} unmatched={}", s_sent, s_applied,
            s_unmatched);
    }
}

void grass_on_message(const uint8_t* payload, size_t size, uint8_t from) {
    (void)from;
    if (size < sizeof(MsgGrassCut) || !enabled()) return;
    MsgGrassCut msg;
    std::memcpy(&msg, payload, sizeof(msg));
    if (msg.room < 0 || msg.room >= kGrassRooms) return;
    const int n = msg.count < kGrassCutsPerMsg ? msg.count : kGrassCutsPerMsg;

    if (msg.kind == kGrassKindFlower) {
        dFlower_packet_c* flower = daGrass_c::getFlower();
        if (flower == nullptr) return;
        for (int c = 0; c < n; ++c) {
            dFlower_data_c* best = nullptr;
            f32 bestDist = kGrassMatchDist;
            for (dFlower_data_c* d = flower->m_room[msg.room].getData(); d != nullptr;
                 d = d->mp_next) {
                if (flower_slot_of(flower, d) < 0) break;
                const f32 dx = d->m_pos.x - msg.pos[c][0];
                const f32 dz = d->m_pos.z - msg.pos[c][2];
                const f32 dist = std::sqrt(dx * dx + dz * dz);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = d;
                }
            }
            if (best == nullptr) {
                ++s_unmatched;
                continue;
            }
            cut_flower(best, msg.bits[c] & kFlowerCutBits, msg.room);
            ++s_applied;
            const int slot = flower_slot_of(flower, best);
            s_flowerBits[slot] = best->m_state & kFlowerCutBits;
            s_flowerLive[slot] = true;
        }
        return;
    }

    dGrass_packet_c* grass = daGrass_c::getGrass();
    if (grass == nullptr) return;

    for (int c = 0; c < n; ++c) {
        const cXyz want(msg.pos[c][0], msg.pos[c][1], msg.pos[c][2]);
        dGrass_data_c* best = nullptr;
        f32 bestDist = kGrassMatchDist;
        for (dGrass_data_c* d = grass->m_room[msg.room].getData(); d != nullptr; d = d->mp_next) {
            if (slot_of(grass, d) < 0) break;
            if (d->m_state == 0) continue;

            const f32 dx = d->m_pos.x - want.x;
            const f32 dz = d->m_pos.z - want.z;
            const f32 dist = std::sqrt(dx * dx + dz * dz);
            if (dist < bestDist) {
                bestDist = dist;
                best = d;
            }
        }
        if (best == nullptr) {
            ++s_unmatched;
            continue;
        }
        if (best->field_0x02 < 0) continue;
        cut_clump(grass, best, msg.room);
        ++s_applied;

        const int slot = slot_of(grass, best);
        s_wasCut[slot] = true;
        s_wasLive[slot] = true;
    }
}



#include "mod.hpp"
#include "net/messages.hpp"
#include "util.hpp"
#include "print.hpp"

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"

#include <cmath>
#include <cstring>

namespace {

struct ItemTableList {
    char mListName[11];
    u8 mTableNum;
    u8 padding[4];
    u8 mTables[255][16];
};

bool s_inDrop = false;
cXyz s_dropPos;

bool s_inPotBreak = false;
cXyz s_potHome;

bool drops_live() {
    return coop_net_connected() &&
           coop_session(kSessWorldObjects, cfg_bool(enemies_breakables_var(), true));
}

uint32_t fnv(uint32_t h, const void* data, size_t n) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

int party_hp_percent() {
    const u16 myMax = dComIfGs_getMaxLife();
    const int myHp = myMax >= 5 ? (dComIfGs_getLife() * 100) / (myMax / 5 * 4) : 100;
    int lowest = myHp;
    const char* stage = dComIfGp_getStartStageName();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id()) continue;
        const CoopPeer& p = features_peer_of(id);
        if (!p.present || !p.inGame || !p.lifeKnown || p.maxLife < 5) continue;
        if (stage == nullptr || std::strncmp(stage, p.stage, 8) != 0) continue;
        const int hp = (p.life * 100) / (p.maxLife / 5 * 4);
        if (hp < lowest) lowest = hp;
    }
    return lowest;
}

}

DEFINE_HOOK_SYMBOL("fopAcM_createItemFromTable",
    fpc_ProcID(const cXyz*, int, int, int, const csXyz*, int, const cXyz*, f32*, f32*, bool),
    DropsCreateFromTableHook);
DEFINE_HOOK_SYMBOL("fopAcM_getItemNoFromTableNo", u8(u8), DropsPickHook);
DEFINE_HOOK_SYMBOL("daObjCarry_c::obj_break", void(void*, bool, bool, bool),
    DropsPotBreakHook);

void drops_init() {
    const ModResult a = mods::hook::add_pre<DropsCreateFromTableHook>(
        [](ModContext*, void* args, void*, void*) -> HookAction {
            const cXyz* pos = mods::arg<const cXyz*>(args, 0);
            s_inDrop = pos != nullptr;
            if (pos != nullptr) s_dropPos = *pos;
            return HOOK_CONTINUE;
        });
    const ModResult b = mods::hook::add_post<DropsCreateFromTableHook>(
        [](ModContext*, void*, void*, void*) { s_inDrop = false; });
    mods::hook::add_pre<DropsPotBreakHook>(
        [](ModContext*, void* args, void*, void*) -> HookAction {
            auto* pot = mods::arg<fopAc_ac_c*>(args, 0);
            s_inPotBreak = pot != nullptr;
            if (pot != nullptr) s_potHome = pot->home.pos;
            return HOOK_CONTINUE;
        });
    mods::hook::add_post<DropsPotBreakHook>(
        [](ModContext*, void*, void*, void*) { s_inPotBreak = false; });
    const ModResult c = mods::hook::add_pre<DropsPickHook>(
        [](ModContext*, void* args, void* retval, void*) -> HookAction {
            if (!s_inDrop || retval == nullptr || !drops_live()) return HOOK_CONTINUE;
            u8 table = mods::arg<u8>(args, 0);
            if (table == 255) return HOOK_CONTINUE;
            const auto* list = static_cast<const ItemTableList*>(dComIfGp_getItemTable());
            if (list == nullptr || table >= list->mTableNum) return HOOK_CONTINUE;
            switch (table) {
            case 150: case 160: case 170: case 180: case 190: {
                const int hp = party_hp_percent();
                if (hp < 80) {
                    if (hp >= 60) table += 1;
                    else if (hp >= 40) table += 2;
                    else if (hp >= 20) table += 3;
                    else table += 4;
                }
                break;
            }
            default:
                break;
            }
            if (table >= list->mTableNum) return HOOK_CONTINUE;
            const cXyz where = s_inPotBreak ? s_potHome : s_dropPos;

            const int32_t cell[3] = {static_cast<int32_t>(std::floor(where.x / 50.0f)),
                static_cast<int32_t>(std::floor(where.y / 50.0f)),
                static_cast<int32_t>(std::floor(where.z / 50.0f))};
            uint32_t h = 2166136261u;
            h = fnv(h, cell, sizeof(cell));
            h = fnv(h, &table, sizeof(table));
            const u16 day = dComIfGs_getDate();
            h = fnv(h, &day, sizeof(day));
            const char* stage = dComIfGp_getStartStageName();
            if (stage != nullptr) h = fnv(h, stage, std::strlen(stage));
            *static_cast<u8*>(retval) = list->mTables[table][(h >> 8) & 15];
            return HOOK_SKIP_ORIGINAL;
        });
    coop_log::info("coop_mod: [DROPS] hooks: create={}/{} pick={}", static_cast<int>(a),
        static_cast<int>(b), static_cast<int>(c));
}

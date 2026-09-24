

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/item.h"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_item.h"
#include "d/d_item_data.h"
#include "f_op/f_op_actor_mng.h"

#include <cstring>
#include <string>

extern const ConfigService* svc_config;
extern const ItemService* svc_item;
extern const HookService* svc_hook;

namespace {

ConfigVarHandle s_var = 0;
uint32_t s_t = 0;
bool s_done[8] = {};
std::string s_ledgerName;
fpc_ProcID s_testItem = fpcM_ERROR_PROCESS_ID_e;
const int kTestBit = 0x70;

struct CommitResult {
    uint32_t tag;
    uint8_t itemNo;
    uint8_t displayItemNo;
    bool wasResolved;
};
using CommitFn = CommitResult (*)(const char*, uint8_t, fopAc_ac_c*);
CommitFn s_commit = nullptr;

bool on() { return s_var != 0 && cfg_int(s_var, 0) != 0; }

void result(int n, bool pass, const std::string& detail) {
    coop_log::info("coop_mod: [SELFTEST] T{} {} - {}", n, pass ? "PASS" : "FAIL", detail);
}

bool test_resolver(ModContext*, const ItemCheckInfo* info, ItemCheckResolution* out, void*) {
    if (info == nullptr || info->name == nullptr ||
        std::strncmp(info->name, "coop_selftest", 13) != 0) {
        return false;
    }
    out->item = dItemNo_BLUE_RUPEE_e;
    return true;
}

bool once(int n) {
    if (s_done[n]) return false;
    s_done[n] = true;
    return true;
}

void host_timeline(daAlink_c* alink) {

    if (s_t >= 600 && once(1)) {
        s_ledgerName = "coop_selftest:ledger:" + std::to_string(fopAcM_GetID(alink) & 0xFFFF);
        if (s_commit == nullptr) {
            result(1, false, "item_check_commit did not resolve");
        } else {
            const CommitResult c = s_commit(s_ledgerName.c_str(), dItemNo_GREEN_RUPEE_e, nullptr);
            execItemGet(c.itemNo, c.tag, nullptr);
            coop_log::info("coop_mod: [SELFTEST] T1 host paid '{}' as {:#x} (resolved={})",
                s_ledgerName, c.itemNo, c.wasResolved);
            result(1, c.itemNo == dItemNo_BLUE_RUPEE_e, "host's own first payout is the real item");
        }
    }

    if (s_t >= 900 && once(3)) {
        cXyz at = alink->current.pos;
        at.x += 150.0f;
        s_testItem = fopAcM_createItem(&at, dItemNo_GREEN_RUPEE_e, kTestBit,
            fopAcM_GetRoomNo(alink), nullptr, nullptr, 0);
        coop_log::info("coop_mod: [SELFTEST] T3 host placed a pickup with bit {:#x}", kTestBit);
    }
    if (s_t >= 1300 && once(4)) {
        dComIfGs_onItem(kTestBit, fopAcM_GetRoomNo(alink));
        coop_log::info("coop_mod: [SELFTEST] T3 host marked bit {:#x} taken", kTestBit);
    }

    if (s_t >= 1800 && once(5)) {
        const std::string seed = rando_debug_any_seed();
        if (seed.empty()) {
            result(4, false, "the host has no randomizer seed to hand off");
        } else {
            rando_debug_enter_randomizer();
            rando_debug_set_local_seed(seed.c_str());
            coop_log::info("coop_mod: [SELFTEST] T4 host is on '{}'", seed);
        }
    }
}

void joiner_timeline(daAlink_c* alink) {

    if (!s_done[1]) {
        const std::vector<std::string> got = checks_debug_names_with("coop_selftest:ledger:");
        if (!got.empty()) {
            s_done[1] = true;
            ItemCheckResolution r{};
            const ModResult rc = svc_item != nullptr
                ? svc_item->resolve_check_full(mod_ctx, got[0].c_str(), dItemNo_GREEN_RUPEE_e, &r)
                : MOD_UNAVAILABLE;
            result(1, rc == MOD_OK && r.item == dItemNo_GREEN_RUPEE_e,
                "'" + got[0] + "' already collected by the host resolves to item " +
                    std::to_string(r.item) + " (want " + std::to_string(dItemNo_GREEN_RUPEE_e) +
                    ", the rupee instead of the blue one)");
        } else if (s_t >= 1500) {
            s_done[1] = true;
            result(1, false, "the host's check never reached our ledger");
        }
    }

    if (s_t >= 900 && once(3)) {
        cXyz at = alink->current.pos;
        at.x += 150.0f;
        s_testItem = fopAcM_createItem(&at, dItemNo_GREEN_RUPEE_e, kTestBit,
            fopAcM_GetRoomNo(alink), nullptr, nullptr, 0);
        coop_log::info("coop_mod: [SELFTEST] T3 joiner placed its copy");
    }
    if (s_t >= 1700 && once(4)) {
        const bool bit = dComIfGs_isItem(kTestBit, fopAcM_GetRoomNo(alink));
        const bool gone = fopAcM_SearchByID(s_testItem) == nullptr;
        result(3, bit && gone, std::string("bit arrived: ") + (bit ? "yes" : "no") +
                                   ", our copy removed: " + (gone ? "yes" : "no"));
    }

    if (s_t >= 1900 && once(5)) {
        rando_debug_enter_randomizer();
        rando_debug_set_local_seed("coop-selftest-other-seed");
        coop_log::info("coop_mod: [SELFTEST] T4 joiner is on a different seed");
    }
    if (s_t >= 3000 && once(6)) {
        const std::string now = rando_debug_local_seed();
        result(4, rando_debug_host_seed_ready() && now != "coop-selftest-other-seed" && !now.empty(),
            "host seed here: " + std::string(rando_debug_host_seed_ready() ? "yes" : "no") +
                ", randomizer now on '" + now + "'");
    }

    if (s_t >= 3100 && once(7)) {
        features_debug_receive_item(0x3F, kCoopHostId);
        features_debug_receive_item(0x3F, kCoopHostId);
        const bool wood = checkItemGet(0x3F, 1) != 0;
        const bool ordon = checkItemGet(0x28, 1) != 0;
        result(2, wood && ordon, std::string("wooden sword: ") + (wood ? "yes" : "no") +
                                     ", ordon sword from the second: " + (ordon ? "yes" : "no"));
    }
}

}

void selftest_init() {
    if (svc_config == nullptr) return;
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = "debug_selftest";
    desc.type = CONFIG_VAR_INT;
    desc.default_int = 0;
    if (svc_config->register_var(mod_ctx, &desc, &s_var) != MOD_OK) s_var = 0;
    if (!on()) return;
    if (svc_item != nullptr) svc_item->set_check_resolver(mod_ctx, nullptr, test_resolver, nullptr, nullptr);
    if (svc_hook != nullptr) {
        void* addr = nullptr;
        svc_hook->resolve(mod_ctx,
            "?item_check_commit@mods@dusk@@YA?AUItemCheckResult@12@PEBDEPEAVfopAc_ac_c@@@Z", &addr,
            nullptr);
        s_commit = reinterpret_cast<CommitFn>(addr);
    }
    coop_log::warn("coop_mod: *** SELFTEST ARMED *** (debug_selftest) - this instance will run "
                   "the automated co-op checks");
}

void selftest_update() {
    if (!on()) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    const bool ready = coop_net_connected() && alink != nullptr && coop_net_player_present(0) &&
                       coop_net_player_present(1);
    if (!ready) return;
    ++s_t;
    if (coop_net_is_host()) {
        host_timeline(alink);
    } else {
        joiner_timeline(alink);
    }
}

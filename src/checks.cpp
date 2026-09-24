

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "mods/svc/hook.hpp"
#include "mods/svc/item.h"
#include "mods/svc/save.h"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "f_op/f_op_actor_iter.h"
#include "f_op/f_op_actor_mng.h"

#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

extern const ItemService* svc_item;
extern const SaveService* svc_save;
extern const HookService* svc_hook;

#if defined(_WIN32)
DEFINE_HOOK_SYMBOL("?item_check_resolve@mods@dusk@@YA?AUItemCheckResolution@@PEBDEPEAVfopAc_ac_c@@@Z",
    ItemCheckResolution(const char*, uint8_t, fopAc_ac_c*), ChecksResolveHook);
#else
DEFINE_HOOK_SYMBOL("_ZN4dusk4mods18item_check_resolveEPKchP10fopAc_ac_c",
    ItemCheckResolution(const char*, uint8_t, fopAc_ac_c*), ChecksResolveHook);
#endif

namespace {

std::unordered_set<std::string> s_ledger;

std::unordered_set<std::string> s_fromPeers;
bool s_loaded = false;
bool s_dirty = false;
uint32_t s_tick = 0;
uint32_t s_lastRoster = 0;
int s_blocked = 0;

using GiveTagFn = uint32_t (*)(const char*);
using CancelFn = void (*)(uint32_t);
GiveTagFn s_giveTag = nullptr;
CancelFn s_cancel = nullptr;

const uint8_t kConsolation = dItemNo_GREEN_RUPEE_e;
const size_t kBlobMax = 48u * 1024u;

void save_ledger() {
    if (svc_save == nullptr || !s_dirty) return;
    std::string blob;
    for (const std::string& name : s_ledger) {
        if (name.rfind("coop_selftest", 0) == 0) continue;
        if (blob.size() + name.size() + 1 > kBlobMax) break;
        blob += name;
        blob += '\n';
    }
    svc_save->set_blob(mod_ctx, "checks", blob.data(), blob.size());
    s_dirty = false;
}

void load_ledger() {
    s_ledger.clear();
    s_loaded = true;
    s_dirty = false;
    if (svc_save == nullptr) return;
    size_t size = 0;
    if (svc_save->get_blob(mod_ctx, "checks", nullptr, &size) != MOD_OK || size == 0) return;
    std::string blob(size, '\0');
    if (svc_save->get_blob(mod_ctx, "checks", blob.data(), &size) != MOD_OK) return;
    blob.resize(size);
    size_t at = 0;
    while (at < blob.size()) {
        size_t nl = blob.find('\n', at);
        if (nl == std::string::npos) nl = blob.size();
        if (nl > at) s_ledger.insert(blob.substr(at, nl - at));
        at = nl + 1;
    }
    coop_log::info("coop_mod: [CHECKS] {} collected check(s) on this file", s_ledger.size());
}

void on_save_loaded(ModContext*, uint32_t, void*) { load_ledger(); }

void on_new_save(ModContext*, uint32_t, void*) {
    s_ledger.clear();
    s_loaded = true;
    s_dirty = true;
    save_ledger();
}

void forget_commit(const std::string& name) {
    if (s_giveTag == nullptr || s_cancel == nullptr) return;
    const uint32_t tag = s_giveTag(name.c_str());
    if (tag != 0) s_cancel(tag);
}

void send_one(const std::string& name) {
    MsgCheckTaken msg{};
    if (name.size() >= sizeof(msg.name)) return;
    std::memcpy(msg.name, name.data(), name.size());
    coop_net_send(kMsgCheckTaken, &msg, sizeof(msg));
}

void send_all() {
    uint8_t buf[kCoopMaxMessagePayload];
    size_t used = 1;
    uint8_t count = 0;
    const auto flush = [&] {
        if (count == 0) return;
        buf[0] = count;
        coop_net_send(kMsgCheckList, buf, used);
        used = 1;
        count = 0;
    };
    for (const std::string& name : s_ledger) {
        if (name.empty() || name.size() > 255) continue;
        if (used + 1 + name.size() > sizeof(buf) || count == 255) flush();
        buf[used++] = static_cast<uint8_t>(name.size());
        std::memcpy(buf + used, name.data(), name.size());
        used += name.size();
        ++count;
    }
    flush();
}

bool note(const std::string& name) {
    if (name.empty() || !s_ledger.insert(name).second) return false;
    s_dirty = true;
    return true;
}

void on_give(ModContext*, const ItemGiveInfo* info, void*) {
    if (info == nullptr || info->check_name == nullptr || info->check_name[0] == '\0') return;
    if (info->origin != ITEM_GIVE_ORIGIN_GAME) return;
    const std::string name(info->check_name);
    if (!note(name)) return;
    save_ledger();
    if (coop_net_connected()) send_one(name);
    coop_log::info("coop_mod: [CHECKS] collected '{}'", name);
}

void on_resolve_post(ModContext*, void* args, void* retval, void*) {
    if (retval == nullptr || s_ledger.empty()) return;
    const char* name = mods::arg<const char*>(args, 0);
    if (name == nullptr) return;
    auto* result = static_cast<ItemCheckResolution*>(retval);
    if (!result->was_resolved || result->item == kConsolation) return;
    if (s_ledger.find(name) == s_ledger.end()) return;
    result->item = kConsolation;
    result->display_item = kConsolation;
    if (++s_blocked <= 20) {
        coop_log::info("coop_mod: [CHECKS] '{}' was already collected by another player - "
                       "paying out a rupee instead", name);
    }
}

fpc_ProcID s_removed[16] = {};
int s_removedNext = 0;

bool already_removed(fpc_ProcID id) {
    for (fpc_ProcID r : s_removed) {
        if (r == id) return true;
    }
    return false;
}

struct Sweep {
    fopAc_ac_c* mine;
    fopAc_ac_c* found[16];
    int count;
};

void* find_taken(void* proc, void* data) {
    auto* sweep = static_cast<Sweep*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || sweep->count >= 16 || actor == sweep->mine) return nullptr;
    if (already_removed(fopAcM_GetID(actor))) return nullptr;
    const s16 name = fopAcM_GetName(actor);
    if (name != fpcNm_ITEM_e && name != fpcNm_Obj_LifeContainer_e && name != fpcNm_Obj_SmallKey_e) {
        return nullptr;
    }

    const int bit = static_cast<int>((fopAcM_GetParam(actor) >> 8) & 0xFF);
    if (bit == 0xFF) return nullptr;

    const bool taken = name == fpcNm_Obj_SmallKey_e ? dComIfGs_isTbox(bit) != 0
                                                    : fopAcM_isItem(actor, bit);
    if (taken) sweep->found[sweep->count++] = actor;
    return nullptr;
}

void sweep_taken_pickups() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;
    Sweep sweep{};

    sweep.mine = fopAcM_getItemEventPartner(alink);
    fopAcM_Search(find_taken, &sweep);
    for (int i = 0; i < sweep.count; ++i) {
        fopAc_ac_c* a = sweep.found[i];
        s_removed[s_removedNext] = fopAcM_GetID(a);
        s_removedNext = (s_removedNext + 1) % 16;
        const s32 ok = fopAcM_delete(a);
        coop_log::info("coop_mod: [CHECKS] removing a {:#x} somebody else already took (id {} "
                       "param {:#x} at {:.0f},{:.0f},{:.0f} -> {})",
            static_cast<int>(fopAcM_GetName(a)), fopAcM_GetID(a), fopAcM_GetParam(a),
            a->current.pos.x, a->current.pos.y, a->current.pos.z, ok);
    }
}

}

void checks_init() {
    if (svc_item != nullptr) svc_item->observe_gives(mod_ctx, on_give, nullptr, nullptr);
    if (svc_save != nullptr) {
        svc_save->observe_saves(mod_ctx, on_new_save, on_save_loaded, nullptr, nullptr, nullptr);
    }
    const bool hooked = mods::hook::add_post<ChecksResolveHook>(on_resolve_post) == MOD_OK;
    if (svc_hook != nullptr) {
        void* addr = nullptr;
        if (svc_hook->resolve(mod_ctx, "dusk::mods::item_give_tag", &addr, nullptr) == MOD_OK) {
            s_giveTag = reinterpret_cast<GiveTagFn>(addr);
        }
        addr = nullptr;
        if (svc_hook->resolve(mod_ctx, "dusk::mods::item_check_cancel", &addr, nullptr) == MOD_OK) {
            s_cancel = reinterpret_cast<CancelFn>(addr);
        }
    }
    coop_log::info("coop_mod: [CHECKS] ledger {}, commit reset {}",
        hooked ? "attached" : "FAILED - a check can pay out twice",
        s_giveTag != nullptr && s_cancel != nullptr ? "found" : "missing");
}

void checks_update() {
    ++s_tick;
    const bool inGame = daAlink_getAlinkActorClass() != nullptr;
    if (inGame && !s_loaded) load_ledger();
    if (!coop_net_connected()) {
        s_lastRoster = 0;
        s_fromPeers.clear();
        return;
    }

    const uint32_t roster = coop_net_roster();
    if (roster != s_lastRoster) {
        s_lastRoster = roster;
        if (coop_net_is_host()) send_all();
    }
    if (inGame && s_tick % 15 == 0) sweep_taken_pickups();
    if (s_tick % 300 == 0) save_ledger();
}

void checks_on_message(uint8_t type, const uint8_t* payload, size_t size) {
    int added = 0;
    std::string last;
    if (type == kMsgCheckTaken && size >= sizeof(MsgCheckTaken)) {
        MsgCheckTaken msg;
        std::memcpy(&msg, payload, sizeof(msg));
        const std::string name(msg.name, strnlen(msg.name, sizeof(msg.name)));
        s_fromPeers.insert(name);
        if (note(name)) {
            ++added;
            last = name;
            forget_commit(name);
        }
    } else if (type == kMsgCheckList && size >= 1) {
        const int count = payload[0];
        size_t at = 1;
        for (int i = 0; i < count && at < size; ++i) {
            const size_t len = payload[at++];
            if (at + len > size) break;
            const std::string name(reinterpret_cast<const char*>(payload + at), len);
            at += len;
            s_fromPeers.insert(name);
            if (note(name)) {
                ++added;
                last = name;
                forget_commit(name);
            }
        }
    }
    if (added == 0) return;
    save_ledger();
    if (added == 1) {
        coop_log::info("coop_mod: [CHECKS] another player collected '{}'", last);
    } else {
        coop_log::info("coop_mod: [CHECKS] {} check(s) another player already collected", added);
    }
}

void checks_on_join_synced() {
    s_ledger = s_fromPeers;
    s_loaded = true;
    s_dirty = true;
    save_ledger();
    coop_log::info("coop_mod: [CHECKS] took the host's world - {} collected check(s)", s_ledger.size());
}

bool checks_collected(const char* name) {
    return name != nullptr && s_ledger.find(name) != s_ledger.end();
}

std::vector<std::string> checks_debug_names_with(const char* prefix) {
    std::vector<std::string> out;
    for (const std::string& name : s_ledger) {
        if (name.rfind(prefix, 0) == 0) out.push_back(name);
    }
    return out;
}



#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "mods/svc/hook.hpp"
#include "mods/svc/item.h"
#include "mods/svc/save.h"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_e_po.h"
#include "d/actor/d_a_obj_life_container.h"
#include "d/actor/d_a_obj_smallkey.h"
#include "d/d_a_item_static.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "f_op/f_op_actor_iter.h"
#include "f_op/f_op_actor_mng.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern const ItemService* svc_item;
extern const SaveService* svc_save;
extern const HookService* svc_hook;

DEFINE_HOOK_SYMBOL("execItemGet", void(u8, u32, fopAc_ac_c*), ChecksGrantHook);
DEFINE_HOOK_SYMBOL("getItemFunc", void(u8), ChecksItemFuncHook);

namespace {

std::unordered_set<std::string> s_ledger;

std::unordered_set<std::string> s_fromPeers;
bool s_loaded = false;
bool s_dirty = false;
uint32_t s_tick = 0;
uint32_t s_lastRoster = 0;
int s_blocked = 0;

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

void send_one(const std::string& name, uint8_t item) {
    MsgCheckTaken msg{};
    if (name.size() >= sizeof(msg.name)) return;
    std::memcpy(msg.name, name.data(), name.size());
    msg.item = item;
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

struct GrantPhoto {
    uint8_t save[sizeof(dSv_save_c)];
    uint8_t memory[sizeof(dSv_memory_c)];
    uint8_t dan[sizeof(dSv_danBit_c)];
    uint8_t counters[64];
};
GrantPhoto s_before;
GrantPhoto s_after;
bool s_inGrant = false;
bool s_haveAfter = false;
bool s_grantIsDuplicate = false;
std::string s_grantName;

std::unordered_map<std::string, uint8_t> s_collectedBy;

uint8_t* counter_bytes(size_t* size) {
    auto& info = g_dComIfG_gameInfo.play.mItemInfo;
    auto* first = reinterpret_cast<uint8_t*>(&info.mItemLifeCount);
    auto* last = reinterpret_cast<uint8_t*>(&info.mItemMaxBombNumCount2) + sizeof(s16);
    *size = static_cast<size_t>(last - first);
    return first;
}

void take_photo(GrantPhoto& photo) {
    dSv_info_c* info = dComIfGs_getSaveInfo();
    std::memcpy(photo.save, &info->getSavedata(), sizeof(photo.save));
    std::memcpy(photo.memory, &info->getMemory(), sizeof(photo.memory));
    std::memcpy(photo.dan, &info->getDan(), sizeof(photo.dan));
    size_t n = 0;
    const uint8_t* counters = counter_bytes(&n);
    std::memcpy(photo.counters, counters, std::min(n, sizeof(photo.counters)));
}

void xor_restore(uint8_t* now, const uint8_t* before, const uint8_t* after, size_t size) {
    for (size_t i = 0; i < size; ++i) now[i] = static_cast<uint8_t>(now[i] ^ (before[i] ^ after[i]));
}

void undo_grant() {
    dSv_info_c* info = dComIfGs_getSaveInfo();
    xor_restore(reinterpret_cast<uint8_t*>(&info->getSavedata()), s_before.save, s_after.save,
        sizeof(s_before.save));
    xor_restore(reinterpret_cast<uint8_t*>(&info->getMemory()), s_before.memory, s_after.memory,
        sizeof(s_before.memory));
    xor_restore(reinterpret_cast<uint8_t*>(&info->getDan()), s_before.dan, s_after.dan,
        sizeof(s_before.dan));
    size_t n = 0;
    uint8_t* counters = counter_bytes(&n);
    xor_restore(counters, s_before.counters, s_after.counters, std::min(n, sizeof(s_before.counters)));
}

HookAction on_grant_pre(ModContext*, void*, void*, void*) {
    s_inGrant = dComIfGs_getSaveInfo() != nullptr;
    s_haveAfter = false;
    s_grantIsDuplicate = false;
    s_grantName.clear();
    if (s_inGrant) take_photo(s_before);
    return HOOK_CONTINUE;
}

void on_item_func_post(ModContext*, void*, void*, void*) {
    if (!s_inGrant || s_haveAfter) return;
    take_photo(s_after);
    s_haveAfter = true;
}

void on_grant_post(ModContext*, void* args, void*, void*) {
    if (!s_inGrant) return;
    s_inGrant = false;
    if (!s_grantIsDuplicate || !s_haveAfter) return;
    undo_grant();
    const uint8_t item = mods::arg<u8>(args, 0);
    if (++s_blocked <= 20) {
        coop_log::info("coop_mod: [CHECKS] '{}' already collected by peer, item {:#x} revoked", s_grantName, item);
    }

    const auto by = s_collectedBy.find(s_grantName);
    const std::string who = by != s_collectedBy.end() ? features_peer_name(by->second) : "";
    features_toast("Already collected",
        ((who.empty() ? std::string("Someone") : who) + " already got this one")
            .c_str());
}

std::unordered_set<std::string> s_ownFreestanding;

void on_give(ModContext*, const ItemGiveInfo* info, void*) {
    if (info == nullptr || info->check_name == nullptr || info->check_name[0] == '\0') return;
    if (info->origin != ITEM_GIVE_ORIGIN_GAME) return;
    const std::string name(info->check_name);

    if (s_inGrant) {
        s_grantName = name;

        ItemCheckResolution r{};
        const bool resolved = svc_item != nullptr && svc_item->resolve_check_full != nullptr &&
                              svc_item->resolve_check_full(mod_ctx, name.c_str(), info->item, &r) ==
                                  MOD_OK &&
                              r.was_resolved;
        s_grantIsDuplicate = resolved && s_ledger.count(name) != 0;

        if (!s_haveAfter) {
            take_photo(s_after);
            s_haveAfter = true;
        }
        if (s_grantIsDuplicate) return;
    }
    if (name.rfind("freestanding:", 0) == 0) s_ownFreestanding.insert(name);
    if (!note(name)) return;
    save_ledger();
    world_note_item_taken();
    if (coop_net_connected()) send_one(name, info->item);
    coop_log::info("coop_mod: [CHECKS] collected '{}'", name);
}

fpc_ProcID s_removed[16] = {};
int s_removedNext = 0;

bool was_removed(fpc_ProcID id) {
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
    if (was_removed(fopAcM_GetID(actor))) return nullptr;
    const s16 name = fopAcM_GetName(actor);
    if (name != fpcNm_ITEM_e && name != fpcNm_Obj_LifeContainer_e && name != fpcNm_Obj_SmallKey_e) {
        return nullptr;
    }

    const int bit = static_cast<int>((fopAcM_GetParam(actor) >> 8) & 0xFF);
    if (bit == 0xFF) return nullptr;

    if (name == fpcNm_ITEM_e) {
        if (static_cast<daItem_c*>(actor)->checkFlag(daItem_c::FLAG_INIT_GET_ITEM_e)) return nullptr;
    } else if (name == fpcNm_Obj_LifeContainer_e) {
        auto* life = static_cast<daObjLife_c*>(actor);
        if (life->chkStatus(daObjLife_c::STATUS_ORDER_GET_DEMO_e) ||
            life->chkStatus(daObjLife_c::STATUS_GET_DEMO_e)) {
            return nullptr;
        }
    } else {
        auto* key = static_cast<daKey_c*>(actor);
        if (key->chkStatus(daKey_c::STATUS_ORDER_GET_DEMO_e) ||
            key->chkStatus(daKey_c::STATUS_GET_DEMO_e)) {
            return nullptr;
        }
    }

    if (name == fpcNm_ITEM_e) {
        const char* stage = dComIfGp_getStartStageName();
        if (stage != nullptr && s_ownFreestanding.count("freestanding:" + std::string(stage) + ":" +
                                                        std::to_string(bit)) != 0) {
            return nullptr;
        }
    }
    const bool taken = name == fpcNm_Obj_SmallKey_e ? dComIfGs_isTbox(bit) != 0
                                                    : fopAcM_isItem(actor, bit);
    if (taken) sweep->found[sweep->count++] = actor;
    return nullptr;
}

int poe_switch(fopAc_ac_c* actor) {
    const s16 name = fopAcM_GetName(actor);
    if (name == fpcNm_E_HP_e) return static_cast<int>((fopAcM_GetParam(actor) & 0xFF00) >> 8);
    if (name == fpcNm_E_PO_e) return reinterpret_cast<e_po_class*>(actor)->BitSW;
    return 0xFF;
}

void* find_taken_poe(void* proc, void* data) {
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr) return nullptr;
    const int sw = poe_switch(actor);
    const char* stage = dComIfGp_getStartStageName();
    if (sw == 0xFF || stage == nullptr) return nullptr;
    const std::string name = "poe:" + std::string(stage) + ":" + std::to_string(sw);
    if (s_fromPeers.count(name) == 0) return nullptr;
    auto* sweep = static_cast<Sweep*>(data);
    if (sweep->count < static_cast<int>(sizeof(sweep->found) / sizeof(sweep->found[0]))) {
        sweep->found[sweep->count++] = actor;
    }
    return nullptr;
}

void sweep_taken_poes() {
    if (daAlink_getAlinkActorClass() == nullptr || dComIfGp_event_runCheck()) return;
    Sweep sweep{};
    fopAcM_Search(find_taken_poe, &sweep);
    for (int i = 0; i < sweep.count; ++i) {
        fopAc_ac_c* a = sweep.found[i];
        const int sw = poe_switch(a);
        fopAcM_onSwitch(a, sw);
        fopAcM_createDisappear(a, &a->current.pos, 8, 3, 0xFF);
        fopAcM_delete(a);
        coop_log::info("coop_mod: [CHECKS] poe sw={} soul taken by peer, removed", sw);
    }
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
        coop_log::info("coop_mod: [CHECKS] removing {:#x} taken by peer (id={} param={:#x} pos={:.0f},{:.0f},{:.0f} -> {})",
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
    const bool pre = mods::hook::add_pre<ChecksGrantHook>(on_grant_pre) == MOD_OK;
    const bool post = mods::hook::add_post<ChecksGrantHook>(on_grant_post) == MOD_OK;
    const bool func = mods::hook::add_post<ChecksItemFuncHook>(on_item_func_post) == MOD_OK;
    coop_log::info("coop_mod: [CHECKS] ledger {}, item function {}",
        pre && post ? "attached" : "FAILED - a check can pay out twice",
        func ? "attached" : "not hookable - using the grant observer");
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
    if (inGame && s_tick % 15 == 7) sweep_taken_poes();
    if (s_tick % 300 == 0) save_ledger();
}

void checks_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from) {
    int added = 0;
    std::string last;
    if (type == kMsgCheckTaken && size >= sizeof(MsgCheckTaken::name)) {
        MsgCheckTaken msg{};
        std::memcpy(&msg, payload, std::min(size, sizeof(msg)));
        const std::string name(msg.name, strnlen(msg.name, sizeof(msg.name)));
        s_fromPeers.insert(name);
        s_collectedBy.emplace(name, from);
        if (note(name)) {
            ++added;
            last = name;

            if (size >= sizeof(MsgCheckTaken)) features_check_found(from, name.c_str(), msg.item);
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
            s_collectedBy.emplace(name, from);
            if (note(name)) {
                ++added;
                last = name;
                }
        }
    }
    if (added == 0) return;
    save_ledger();
    if (added == 1) {
        coop_log::info("coop_mod: [CHECKS] another player collected '{}'", last);
    } else {
        coop_log::info("coop_mod: [CHECKS] {} checks collected by peers", added);
    }
}

void checks_on_join_synced() {
    s_ledger = s_fromPeers;
    s_loaded = true;
    s_dirty = true;
    save_ledger();
    coop_log::info("coop_mod: [CHECKS] host world taken, {} collected checks", s_ledger.size());
}

bool checks_collected(const char* name) {
    return name != nullptr && s_ledger.find(name) != s_ledger.end();
}

std::vector<std::string> checks_debug_find(const char* prefix) {
    std::vector<std::string> out;
    for (const std::string& name : s_ledger) {
        if (name.rfind(prefix, 0) == 0) out.push_back(name);
    }
    return out;
}

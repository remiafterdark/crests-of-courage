

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "mods/svc/hook.hpp"
#include "mods/svc/save.h"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_save.h"
#include "f_op/f_op_actor_mng.h"

#include <cstring>

extern const SaveService* svc_save;

DEFINE_HOOK_SYMBOL("dComIfGs_isEventBit", BOOL(u16), SkillsIsEventBitHook);
DEFINE_HOOK_SYMBOL("dComIfGs_onEventBit", void(u16), SkillsOnEventBitHook);
DEFINE_HOOK_SYMBOL("dSv_event_c::onEventBit", void(dSv_event_c*, u16), SkillsSvOnEventBitHook);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_obj_wind_stone.cpp#daWindStone_create", int(void*),
    SkillsStoneCreateHook);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_obj_wind_stone.cpp#daWindStone_execute", int(void*),
    SkillsStoneExecuteHook);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_npc_gwolf.cpp#daNpc_GWolf_Create", int(void*),
    SkillsWolfCreateHook);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_npc_kn.cpp#daNpc_Kn_Create", int(void*),
    SkillsShadeCreateHook);

namespace {

const int kSkillFirst = 0x152;
const int kSkillCount = 7;
const int kHowlFirst = 0x1D8;
const int kHowlCount = 6;
const int kTracked = kSkillCount + kHowlCount;
const uint16_t kAllSkills = (1u << kSkillCount) - 1;
const uint16_t kAllHowls = ((1u << kHowlCount) - 1) << kSkillCount;

const int kWolfTmpFirst = 0x5B;
const int kWolfTmpCount = 7;

const int kWolfGoneFirst = 0x1EC;
const int kWolfGoneCount = 6;

enum Context { kCtxNone, kCtxStone, kCtxWolf, kCtxShade };
Context s_context = kCtxNone;
int s_contextDepth = 0;

int s_shadeDelFlag = -1;

uint16_t s_earned = 0;
bool s_haveEarned = false;
bool s_applying = false;
uint16_t s_sent = 0;
uint32_t s_tick = 0;
const uint32_t kResendTicks = 300;

struct SkillsBlob {
    uint32_t version;
    uint16_t earned;
    uint16_t pad;
};
const uint32_t kBlobVersion = 1;

uint16_t label(int k) {
    const int idx = k < kSkillCount ? kSkillFirst + k : kHowlFirst + (k - kSkillCount);
    return static_cast<uint16_t>(dSv_event_flag_c::saveBitLabels[idx]);
}

int tracked_index(uint16_t flag) {
    for (int k = 0; k < kTracked; ++k) {
        if (label(k) == flag) return k;
    }
    return -1;
}

bool in_game() {
    return daAlink_getAlinkActorClass() != nullptr && dComIfGp_getStartStageName() != nullptr;
}

bool flag_on(int k) {
    return g_dComIfG_gameInfo.info.getSavedata().getEvent().isEventBit(label(k)) != 0;
}

uint16_t have_mask() {
    uint16_t mask = 0;
    for (int k = 0; k < kTracked; ++k) {
        if (flag_on(k)) mask |= static_cast<uint16_t>(1u << k);
    }
    return mask;
}

void save_earned() {
    if (svc_save == nullptr) return;
    SkillsBlob blob{kBlobVersion, s_earned, 0};
    svc_save->set_blob(mod_ctx, "skills", &blob, sizeof(blob));
}

void load_earned() {
    s_haveEarned = true;
    SkillsBlob blob{};
    size_t size = sizeof(blob);
    if (svc_save != nullptr && svc_save->get_blob(mod_ctx, "skills", &blob, &size) == MOD_OK &&
        size == sizeof(blob) && blob.version == kBlobVersion) {
        s_earned = blob.earned;
    } else {

        s_earned = have_mask();
        save_earned();
    }
    s_sent = 0;
    coop_log::info("coop_mod: [SKILLS] earned {:#06x}, have {:#06x}", s_earned, have_mask());
}

void on_save_loaded(ModContext*, uint32_t, void*) { load_earned(); }

void on_new_save(ModContext*, uint32_t, void*) {
    s_haveEarned = true;
    s_earned = 0;
    s_sent = 0;
    save_earned();
}

void mark_earned(u16 flag) {
    if (s_applying) return;
    const int k = tracked_index(flag);
    if (k < 0) return;
    const uint16_t bit = static_cast<uint16_t>(1u << k);
    if (s_earned & bit) return;
    s_earned |= bit;
    save_earned();
    coop_log::info("coop_mod: [SKILLS] earned {} {} here", k < kSkillCount ? "skill" : "howl",
        k < kSkillCount ? k + 1 : k - kSkillCount + 2);
    s_sent = 0;
}

HookAction on_event_bit_pre(ModContext*, void* args, void*, void*) {
    mark_earned(mods::arg<u16>(args, 0));
    return HOOK_CONTINUE;
}

HookAction on_sv_event_bit_pre(ModContext*, void* args, void*, void*) {

    if (mods::arg<dSv_event_c*>(args, 0) != &g_dComIfG_gameInfo.info.getSavedata().getEvent()) {
        return HOOK_CONTINUE;
    }
    mark_earned(mods::arg<u16>(args, 1));
    return HOOK_CONTINUE;
}

void on_is_event_bit_post(ModContext*, void* args, void* retval, void*) {
    if (s_context == kCtxNone || retval == nullptr) return;
    BOOL* result = static_cast<BOOL*>(retval);
    if (!*result) return;
    const int k = tracked_index(mods::arg<u16>(args, 0));
    if (k < 0) return;
    const uint16_t bit = static_cast<uint16_t>(1u << k);
    const uint16_t about = s_context == kCtxStone ? kAllHowls : kAllSkills;
    if (!(about & bit) || (s_earned & bit)) return;

    if (about == kAllSkills && rando_active()) return;
    if (s_context == kCtxShade && label(k) != s_shadeDelFlag) return;
    *result = FALSE;
}

template <Context C>
HookAction enter(ModContext*, void* args, void*, void*) {
    if (s_contextDepth++ == 0) {
        s_context = C;
        s_shadeDelFlag = -1;
        if (C == kCtxShade) {

            static const s16 kDelFlag[7] = {0x153, 0x152, 0x154, 0x155, 0x156, 0x157, 0x158};
            auto* actor = mods::arg<fopAc_ac_c*>(args, 0);
            const u32 prm = actor != nullptr ? (fopAcM_GetParam(actor) & 0xFF) : 0;
            if (prm >= 1 && prm <= 7) {
                s_shadeDelFlag = static_cast<uint16_t>(dSv_event_flag_c::saveBitLabels[kDelFlag[prm - 1]]);
            }
        }
    }
    return HOOK_CONTINUE;
}

void leave(ModContext*, void*, void*, void*) {
    if (s_contextDepth > 0 && --s_contextDepth == 0) s_context = kCtxNone;
}

void apply(uint16_t mask) {
    if (!in_game() || !s_haveEarned) return;
    for (int k = 0; k < kTracked; ++k) {
        if (!(mask & (1u << k)) || flag_on(k)) continue;
        s_applying = true;
        dComIfGs_onEventBit(label(k));
        s_applying = false;
        coop_log::info("coop_mod: [SKILLS] another player finished {} {} - yours too",
            k < kSkillCount ? "skill" : "howl", k < kSkillCount ? k + 1 : k - kSkillCount + 2);
    }
}

}

void skills_init() {
    bool ok = mods::hook::add_post<SkillsIsEventBitHook>(on_is_event_bit_post) == MOD_OK;
    ok = mods::hook::add_pre<SkillsOnEventBitHook>(on_event_bit_pre) == MOD_OK && ok;

    mods::hook::add_pre<SkillsSvOnEventBitHook>(on_sv_event_bit_pre);
    ok = mods::hook::add_pre<SkillsStoneCreateHook>(enter<kCtxStone>) == MOD_OK && ok;
    mods::hook::add_post<SkillsStoneCreateHook>(leave);
    ok = mods::hook::add_pre<SkillsStoneExecuteHook>(enter<kCtxStone>) == MOD_OK && ok;
    mods::hook::add_post<SkillsStoneExecuteHook>(leave);
    ok = mods::hook::add_pre<SkillsWolfCreateHook>(enter<kCtxWolf>) == MOD_OK && ok;
    mods::hook::add_post<SkillsWolfCreateHook>(leave);
    ok = mods::hook::add_pre<SkillsShadeCreateHook>(enter<kCtxShade>) == MOD_OK && ok;
    mods::hook::add_post<SkillsShadeCreateHook>(leave);
    if (svc_save != nullptr) {
        svc_save->observe_saves(mod_ctx, on_new_save, on_save_loaded, nullptr, nullptr, nullptr);
    }
    coop_log::info("coop_mod: [SKILLS] hooks {}", ok ? "attached" : "PARTLY FAILED - stones and "
                                                      "shades may retire when another player "
                                                      "finishes them");
}

void skills_update() {
    ++s_tick;
    if (!in_game()) return;

    if (!s_haveEarned) load_earned();
    if (!coop_net_connected()) return;
    const uint16_t have = have_mask();
    if (have == 0) return;
    if (have == s_sent && s_tick % kResendTicks != 0) return;
    MsgSkills msg{};
    msg.have = have;
    coop_net_send(kMsgSkills, &msg, sizeof(msg));
    s_sent = have;
}

void skills_on_message(const uint8_t* payload, size_t size) {
    if (size < sizeof(MsgSkills)) return;
    MsgSkills msg;
    std::memcpy(&msg, payload, sizeof(msg));
    apply(msg.have);
}

uint8_t skills_tmp_private(int byte) {
    uint8_t mask = 0;
    for (int i = 0; i < kWolfTmpCount; ++i) {
        const uint16_t flag = dSv_event_tmp_flag_c::tempBitLabels[kWolfTmpFirst + i];
        if ((flag >> 8) == byte) mask |= static_cast<uint8_t>(flag & 0xFF);
    }
    return mask;
}

uint8_t skills_event_private(int byte) {
    uint8_t mask = 0;
    for (int i = 0; i < kWolfGoneCount; ++i) {
        const uint16_t flag =
            static_cast<uint16_t>(dSv_event_flag_c::saveBitLabels[kWolfGoneFirst + i]);
        if ((flag >> 8) == byte) mask |= static_cast<uint8_t>(flag & 0xFF);
    }
    return mask;
}



#include "mod.hpp"
#include "print.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_save.h"
#include "d/d_stage.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct RawSoftlock {
    const char* report;
    const char* what;
    const char* when;
    const char* fix;
    const char* note;
};

const RawSoftlock kRaw[] = {
#define SOFTLOCK(report, what, when, fix, note) {report, what, when, fix, note},
#include "softlocks.inc"
#undef SOFTLOCK
};

enum class Op {
    FlagOn, FlagOff, SwOn, SwOff, TboxOn, Stage, NotStage, Room, NotPoint, Rando, NoRando, NotTransform, NotDarkClear,
    Wolf, NoTwilight, Item, NoItem,
    Set, Clear, SwSet, SwClear, Human,
};

struct Term {
    Op op;
    int a = 0;
    int b = 0;
    std::vector<std::string> stages;
};

struct Softlock {
    const RawSoftlock* raw;
    std::vector<Term> when;
    std::vector<Term> fix;
    bool bad = false;
};

std::vector<Softlock> s_table;

std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : text) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    for (std::string& s : out) {
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        while (!s.empty() && s.back() == ' ') s.pop_back();
    }
    return out;
}

bool parse_term(const std::string& text, Term& t) {
    const size_t space = text.find(' ');
    const std::string word = text.substr(0, space);
    const std::string arg = space == std::string::npos ? "" : text.substr(space + 1);
    const auto slot_sw = [&]() {
        const size_t colon = arg.find(':');
        if (colon == std::string::npos) return false;
        t.a = std::atoi(arg.substr(0, colon).c_str());
        t.b = std::atoi(arg.substr(colon + 1).c_str());
        return t.a >= 0 && t.a < dSv_save_c::STAGE_MAX && t.b >= 0 && t.b < 0x80;
    };
    const auto flag = [&]() {
        t.a = static_cast<int>(std::strtol(arg.c_str(), nullptr, 16));
        return t.a > 0 && t.a < 0x10000 && (t.a & 0xFF) != 0;
    };
    if (word == "on") { t.op = Op::FlagOn; return flag(); }
    if (word == "off") { t.op = Op::FlagOff; return flag(); }
    if (word == "set") { t.op = Op::Set; return flag(); }
    if (word == "clear") { t.op = Op::Clear; return flag(); }
    if (word == "swon") { t.op = Op::SwOn; return slot_sw(); }
    if (word == "swoff") { t.op = Op::SwOff; return slot_sw(); }
    if (word == "tbon") { t.op = Op::TboxOn; return slot_sw() && t.b < 64; }
    if (word == "swset") { t.op = Op::SwSet; return slot_sw(); }
    if (word == "swclear") { t.op = Op::SwClear; return slot_sw(); }
    if (word == "stage" || word == "notstage") {
        t.op = word == "stage" ? Op::Stage : Op::NotStage;
        t.stages = split(arg, ',');
        return !t.stages.empty();
    }
    if (word == "room") { t.op = Op::Room; t.a = std::atoi(arg.c_str()); return true; }
    if (word == "notpoint") { t.op = Op::NotPoint; t.a = std::atoi(arg.c_str()); return true; }
    if (word == "nottransform") { t.op = Op::NotTransform; t.a = std::atoi(arg.c_str()); return t.a >= 0 && t.a < 8; }
    if (word == "notdarkclear") { t.op = Op::NotDarkClear; t.a = std::atoi(arg.c_str()); return t.a >= 0 && t.a < 8; }
    if (word == "item" || word == "noitem") {
        t.op = word == "item" ? Op::Item : Op::NoItem;
        t.a = static_cast<int>(std::strtol(arg.c_str(), nullptr, 16));
        return t.a > 0 && t.a < 0x100;
    }
    if (word == "wolf") { t.op = Op::Wolf; return true; }
    if (word == "notwilight") { t.op = Op::NoTwilight; return true; }
    if (word == "human") { t.op = Op::Human; return true; }
    if (word == "rando") { t.op = Op::Rando; return true; }
    if (word == "norando") { t.op = Op::NoRando; return true; }
    return false;
}

void build() {
    for (const RawSoftlock& raw : kRaw) {
        Softlock s;
        s.raw = &raw;
        for (const std::string& part : split(raw.when, ';')) {
            Term t{};
            if (part.empty()) continue;
            if (!parse_term(part, t)) s.bad = true;
            s.when.push_back(t);
        }
        for (const std::string& part : split(raw.fix, ';')) {
            Term t{};
            if (part.empty()) continue;
            if (!parse_term(part, t)) s.bad = true;
            s.fix.push_back(t);
        }
        if (s.bad) coop_log::warn("coop_mod: [SOFTLOCK] entry '{}' does not parse, left out", raw.what);
        s_table.push_back(std::move(s));
    }
}

int current_slot() {
    stage_stag_info_class* info = dComIfGp_getStageStagInfo();
    if (info == nullptr) return -1;
    const int slot = dStage_stagInfo_GetSaveTbl(info);
    return (slot >= 0 && slot < dSv_save_c::STAGE_MAX) ? slot : -1;
}

dSv_memBit_c& slot_bits(int slot) {
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (slot == current_slot()) return info->getMemory().getBit();
    return info->getSavedata().getSave(slot).getBit();
}

bool holds(const Term& t, const char* stage) {
    switch (t.op) {
    case Op::FlagOn: return dComIfGs_isEventBit(static_cast<u16>(t.a)) != 0;
    case Op::FlagOff: return dComIfGs_isEventBit(static_cast<u16>(t.a)) == 0;
    case Op::SwOn: return slot_bits(t.a).isSwitch(t.b) != 0;
    case Op::SwOff: return slot_bits(t.a).isSwitch(t.b) == 0;
    case Op::TboxOn: return slot_bits(t.a).isTbox(t.b) != 0;
    case Op::Stage:
    case Op::NotStage: {
        bool in = false;
        for (const std::string& s : t.stages) in = in || std::strncmp(stage, s.c_str(), 8) == 0;
        return t.op == Op::Stage ? in : !in;
    }
    case Op::Room: return dComIfGp_roomControl_getStayNo() == t.a;
    case Op::NotPoint: return dComIfGp_getStartStagePoint() != t.a;
    case Op::Rando: return rando_active();
    case Op::NoRando: return !rando_active();
    case Op::NotTransform: return dComIfGs_isTransformLV(t.a) == 0;
    case Op::NotDarkClear: return dComIfGs_isDarkClearLV(t.a) == 0;
    case Op::Wolf: {
        daAlink_c* alink = daAlink_getAlinkActorClass();
        return alink != nullptr && alink->checkWolf();
    }
    case Op::NoTwilight: return !dKy_darkworld_check();
    case Op::Item: return dComIfGs_isItemFirstBit(static_cast<u8>(t.a)) != 0;
    case Op::NoItem: return dComIfGs_isItemFirstBit(static_cast<u8>(t.a)) == 0;
    default: return false;
    }
}

void apply(const Term& t) {
    switch (t.op) {
    case Op::Set: dComIfGs_onEventBit(static_cast<u16>(t.a)); break;
    case Op::Clear: dComIfGs_offEventBit(static_cast<u16>(t.a)); break;
    case Op::SwSet: slot_bits(t.a).onSwitch(t.b); break;
    case Op::SwClear: slot_bits(t.a).offSwitch(t.b); break;
    case Op::Human: {

        daAlink_c* alink = daAlink_getAlinkActorClass();
        if (alink != nullptr && alink->checkWolf() && alink->mEquipItem != dItemNo_IRONBALL_e &&
            alink->mProcID != daAlink_c::PROC_METAMORPHOSE && alink->mProcID != daAlink_c::PROC_METAMORPHOSE_ONLY) {
            alink->procCoMetamorphoseInit();
        }
        break;
    }
    default: break;
    }
}

}

void softlocks_update() {
    if (s_table.empty()) build();
    if (daAlink_getAlinkActorClass() == nullptr || dComIfGp_event_runCheck() || dComIfGp_isEnableNextStage()) return;
    if (dComIfGs_getSaveInfo() == nullptr) return;
    const char* stage = dComIfGp_getStartStageName();
    if (stage == nullptr || stage[0] == '\0' || coop_on_title_screen()) return;
    for (const Softlock& s : s_table) {
        if (s.bad) continue;
        bool all = true;
        for (const Term& t : s.when) {
            if (!holds(t, stage)) {
                all = false;
                break;
            }
        }
        if (!all) continue;
        for (const Term& t : s.fix) apply(t);
        coop_log::info("coop_mod: [SOFTLOCK] {} in {:.8}: {} - fixed ({})", s.raw->report, stage, s.raw->what,
            s.raw->fix);
        if (s.raw->note[0] != '\0') coop_notify_c(kNotifyOther, "Fixed a stuck save", s.raw->note);
    }
}

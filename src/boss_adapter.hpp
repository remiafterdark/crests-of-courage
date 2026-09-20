#pragma once

#include "mod.hpp"
#include "net/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "f_op/f_op_actor_mng.h"

namespace coop_boss {

enum BossSlot : uint8_t {
    kBSlotAction,
    kBSlotMode,
    kBSlotTimer0,
    kBSlotTimer1,
    kBSlotTimer2,
    kBSlotTimer3,
    kBSlotTimer4,
    kBSlotExtra0,
    kBSlotExtraF = kBSlotExtra0 + 17,
    kBSlotCount,
};

constexpr uint8_t bslot_extra(int n) { return static_cast<uint8_t>(kBSlotExtra0 + n); }
constexpr uint8_t bslot_timer(int n) { return static_cast<uint8_t>(kBSlotTimer0 + n); }

enum BossFieldType : uint8_t { kBTypeS8, kBTypeU8, kBTypeS16, kBTypeU16, kBTypeS32, kBTypeF32 };

enum BossFieldPolicy : uint8_t {

    kBPolicyMirror = 0,

    kBPolicySticky,

    kBPolicyNeverRaise,

    kBPolicyReadOnly,
};

struct BossField {
    uint16_t offset;
    uint8_t type;
    uint8_t slot;
    uint8_t policy;
    const char* name;
};

template <typename T> struct BossTypeCode;
template <> struct BossTypeCode<s8>  { static const uint8_t value = kBTypeS8; };
template <> struct BossTypeCode<u8>  { static const uint8_t value = kBTypeU8; };
template <> struct BossTypeCode<s16> { static const uint8_t value = kBTypeS16; };
template <> struct BossTypeCode<u16> { static const uint8_t value = kBTypeU16; };
template <> struct BossTypeCode<s32> { static const uint8_t value = kBTypeS32; };
template <> struct BossTypeCode<u32> { static const uint8_t value = kBTypeS32; };
template <> struct BossTypeCode<f32> { static const uint8_t value = kBTypeF32; };

#define BOSS_FIELD(cls, member, slot_, policy_)                                                      coop_boss::BossField {                                                                               (uint16_t)offsetof(cls, member),                                                                 coop_boss::BossTypeCode<decltype(cls::member)>::value,                                           (uint8_t)(slot_), (uint8_t)(policy_), #member                                                }

enum BossAdapterFlags : uint16_t {

    kBossSelfMoving = 1 << 0,

    kBossHoldsDoor = 1 << 1,

    kBossIgnoreDemoGate = 1 << 2,

    kBossNoRetarget = 1 << 3,
};

inline uint32_t boss_skip_action_mode() {
    return (1u << kBSlotAction) | (1u << kBSlotMode);
}

struct BossHold {
    bool held = false;
    s16 a = -1;
    s16 b = -1;
    s16 c = -1;
};

struct BossAdapter {
    int16_t procName;
    uint8_t kind;
    const char* label;
    uint16_t flags;

    const char* arc;
    uint16_t morfOffset;
    uint16_t anmIdOffset;
    uint8_t anmIdType;

    uint8_t (*anmAttr)(int anmId);

    const BossField* fields;
    uint8_t fieldCount;

    bool (*fightIsOver)(fopAc_ac_c* actor);

    void (*handOverDeath)(fopAc_ac_c* actor);

    void (*holdAtDoor)(fopAc_ac_c* actor, BossHold& hold);
    void (*releaseAtDoor)(fopAc_ac_c* actor, const BossHold& hold);

    void (*extraRead)(fopAc_ac_c* actor, MsgBossActor& m);
    void (*extraWrite)(fopAc_ac_c* actor, const MsgBossActor& m);
};

inline int16_t* boss_slot_ptr(MsgBossActor& m, uint8_t slot) {
    if (slot == kBSlotAction) return &m.action;
    if (slot == kBSlotMode) return &m.mode;
    if (slot >= kBSlotTimer0 && slot <= kBSlotTimer4) return &m.timers[slot - kBSlotTimer0];
    if (slot >= kBSlotExtra0 && slot < kBSlotExtraF) return &m.extra[slot - kBSlotExtra0];
    return nullptr;
}

inline const int16_t* boss_slot_ptr(const MsgBossActor& m, uint8_t slot) {
    return boss_slot_ptr(const_cast<MsgBossActor&>(m), slot);
}

inline int32_t boss_field_load(const fopAc_ac_c* actor, const BossField& f) {
    const uint8_t* at = reinterpret_cast<const uint8_t*>(actor) + f.offset;
    switch (f.type) {
    case kBTypeS8:  { s8 v;  std::memcpy(&v, at, 1); return v; }
    case kBTypeU8:  { u8 v;  std::memcpy(&v, at, 1); return v; }
    case kBTypeS16: { s16 v; std::memcpy(&v, at, 2); return v; }
    case kBTypeU16: { u16 v; std::memcpy(&v, at, 2); return v; }
    default:        { s32 v; std::memcpy(&v, at, 4); return v; }
    }
}

inline void boss_field_store(fopAc_ac_c* actor, const BossField& f, int32_t value) {
    uint8_t* at = reinterpret_cast<uint8_t*>(actor) + f.offset;
    switch (f.type) {
    case kBTypeS8:  { s8 v = (s8)value;   std::memcpy(at, &v, 1); break; }
    case kBTypeU8:  { u8 v = (u8)value;   std::memcpy(at, &v, 1); break; }
    case kBTypeS16: { s16 v = (s16)value; std::memcpy(at, &v, 2); break; }
    case kBTypeU16: { u16 v = (u16)value; std::memcpy(at, &v, 2); break; }
    default:        { s32 v = value;      std::memcpy(at, &v, 4); break; }
    }
}

inline void adapter_read_fields(fopAc_ac_c* actor, const BossAdapter* ap, MsgBossActor& m) {
    if (actor == nullptr || ap == nullptr) return;
    const BossAdapter& a = *ap;
    for (uint8_t i = 0; i < a.fieldCount; ++i) {
        const BossField& f = a.fields[i];
        if (f.type == kBTypeF32) {
            std::memcpy(&m.extraF, reinterpret_cast<const uint8_t*>(actor) + f.offset, 4);
            continue;
        }
        int16_t* slot = boss_slot_ptr(m, f.slot);
        if (slot != nullptr) *slot = (int16_t)boss_field_load(actor, f);
    }
}

inline void adapter_write_fields(fopAc_ac_c* actor, const BossAdapter* ap, const MsgBossActor& m,
    uint32_t skipSlots = 0) {
    if (actor == nullptr || ap == nullptr) return;
    const BossAdapter& a = *ap;
    for (uint8_t i = 0; i < a.fieldCount; ++i) {
        const BossField& f = a.fields[i];
        if (f.policy == kBPolicyReadOnly) continue;
        if (f.slot < 32 && (skipSlots & (1u << f.slot)) != 0) continue;
        if (f.type == kBTypeF32) {
            std::memcpy(reinterpret_cast<uint8_t*>(actor) + f.offset, &m.extraF, 4);
            continue;
        }
        const int16_t* slot = boss_slot_ptr(m, f.slot);
        if (slot == nullptr) continue;
        const int32_t want = *slot;
        switch (f.policy) {
        case kBPolicySticky:

            if (want != 0 && boss_field_load(actor, f) == 0) boss_field_store(actor, f, want);
            break;
        case kBPolicyNeverRaise:
            if (want < boss_field_load(actor, f)) boss_field_store(actor, f, want);
            break;
        default:
            boss_field_store(actor, f, want);
            break;
        }
    }
}

}

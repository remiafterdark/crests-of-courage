

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "Z2AudioLib/Z2AudioMgr.h"
#include "Z2AudioLib/Z2SeMgr.h"
#include "Z2AudioLib/Z2SoundObject.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_boomerang.h"
#include "d/actor/d_a_spinner.h"
#include "d/d_com_inf_game.h"
#include "d/d_particle.h"
#include "d/d_particle_copoly.h"
#include "d/d_particle_name.h"

#include <cstring>

DEFINE_HOOK_SYMBOL("?startSound@Z2SoundObjBase@@UEAAPEAVZ2SoundHandlePool@@VJAISoundID@@IC@Z",
    Z2SoundHandlePool*(Z2SoundObjBase*, JAISoundID, u32, s8), CoopFxObjSound);
DEFINE_HOOK_SYMBOL("?startLevelSound@Z2SoundObjBase@@UEAAPEAVZ2SoundHandlePool@@VJAISoundID@@IC@Z",
    Z2SoundHandlePool*(Z2SoundObjBase*, JAISoundID, u32, s8), CoopFxObjLevelSound);
DEFINE_HOOK_SYMBOL("?seStart@Z2SeMgr@@QEAA_NVJAISoundID@@PEBUVec@@ICMMMME@Z",
    bool(Z2SeMgr*, JAISoundID, const Vec*, u32, s8, f32, f32, f32, f32, u8), CoopFxSeStart);
DEFINE_HOOK_SYMBOL("?seStartLevel@Z2SeMgr@@QEAA_NVJAISoundID@@PEBUVec@@ICMMMME@Z",
    bool(Z2SeMgr*, JAISoundID, const Vec*, u32, s8, f32, f32, f32, f32, u8), CoopFxSeStartLevel);
DEFINE_HOOK_SYMBOL(
    "?set@dPa_control_c@@QEAAIIEGPEBUcXyz@@PEBVdKy_tevstr_c@@PEBVcsXyz@@0EPEAVdPa_levelEcallBack@@CPEBUGXColor@@40M@Z",
    u32(dPa_control_c*, u32, u8, u16, const cXyz*, const dKy_tevstr_c*, const csXyz*, const cXyz*,
        u8, dPa_levelEcallBack*, s8, const GXColor*, const GXColor*, const cXyz*, f32),
    CoopFxParticleKeyed);
DEFINE_HOOK_SYMBOL(
    "?set@dPa_control_c@@QEAAPEAVJPABaseEmitter@@EGPEBUcXyz@@PEBVdKy_tevstr_c@@PEBVcsXyz@@0EPEAVdPa_levelEcallBack@@CPEBUGXColor@@40M@Z",
    JPABaseEmitter*(dPa_control_c*, u8, u16, const cXyz*, const dKy_tevstr_c*, const csXyz*,
        const cXyz*, u8, dPa_levelEcallBack*, s8, const GXColor*, const GXColor*, const cXyz*, f32),
    CoopFxParticleOnce);
DEFINE_HOOK(&daBoomerang_c::execute, CoopFxBoomerangExecute);

DEFINE_HOOK_SYMBOL(
    "?setEffectFour@dPaPoF_c@@QEAAHPEBVdKy_tevstr_c@@PEBUcXyz@@II11111PEBVcsXyz@@1CMM@Z",
    int(dPaPoF_c*, const dKy_tevstr_c*, const cXyz*, u32, u32, const cXyz*, const cXyz*,
        const cXyz*, const cXyz*, const cXyz*, const csXyz*, const cXyz*, s8, f32, f32),
    CoopFxWalkDustFour);
DEFINE_HOOK_SYMBOL(
    "?setEffectTwo@dPaPoT_c@@QEAAHPEBVdKy_tevstr_c@@PEBUcXyz@@II111PEBVcsXyz@@1CMM@Z",
    int(dPaPoT_c*, const dKy_tevstr_c*, const cXyz*, u32, u32, const cXyz*, const cXyz*,
        const cXyz*, const csXyz*, const cXyz*, s8, f32, f32),
    CoopFxWalkDustTwo);
DEFINE_HOOK(&daSpinner_c::execute, CoopFxSpinnerExecute);

namespace {

struct OwnedRange {
    const char* lo = nullptr;
    const char* hi = nullptr;
};
OwnedRange s_ranges[3];
const void* s_hookSound = nullptr;
bool s_windowAlink = false;
bool s_windowBoomerang = false;
bool s_windowSpinner = false;
int s_suppress = 0;
int s_inWalkDust = 0;
bool s_captureSounds = false;
bool s_captureVfx = false;
uint32_t s_tick = 0;

bool in_owned_memory(const void* p) {
    if (p == nullptr) return false;
    const char* c = static_cast<const char*>(p);
    for (const OwnedRange& r : s_ranges) {
        if (r.lo != nullptr && c >= r.lo && c < r.hi) return true;
    }
    return false;
}

bool in_owner_window() {
    return s_windowAlink || s_windowBoomerang || s_windowSpinner;
}

void refresh_owned_ranges() {
    for (OwnedRange& r : s_ranges) r = OwnedRange{};
    s_hookSound = nullptr;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;
    s_ranges[0].lo = reinterpret_cast<const char*>(alink);
    s_ranges[0].hi = s_ranges[0].lo + sizeof(daAlink_c);
    if (fopAc_ac_c* boom = alink->getBoomerangActor()) {
        s_ranges[1].lo = reinterpret_cast<const char*>(boom);
        s_ranges[1].hi = s_ranges[1].lo + sizeof(daBoomerang_c);
    }
    if (daSpinner_c* spinner = alink->getSpinnerActor()) {
        s_ranges[2].lo = reinterpret_cast<const char*>(spinner);
        s_ranges[2].hi = s_ranges[2].lo + sizeof(daSpinner_c);
    }
    s_hookSound = alink->mpHookSound;
}

bool relative_to_link(const Vec* pos, float out[3]) {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr || pos == nullptr) return false;
    out[0] = pos->x - alink->current.pos.x;
    out[1] = pos->y - alink->current.pos.y;
    out[2] = pos->z - alink->current.pos.z;
    return true;
}

MsgSoundEntry s_sounds[kCoopMaxSoundsPerMessage];
int s_soundCount = 0;

const int kMaxParticlesPerFrame = kCoopMaxParticlesPerMessage * 4;
MsgParticleEntry s_particles[kMaxParticlesPerFrame];

JPABaseEmitter* s_particleEmitters[kMaxParticlesPerFrame] = {};
int s_particleCount = 0;

void queue_sound(JAISoundID soundID, uint8_t kind, const Vec* pos) {
    const uint32_t id = static_cast<uint32_t>(soundID);
    for (int i = 0; i < s_soundCount; ++i) {

        if (s_sounds[i].id == id && s_sounds[i].kind == kind) return;
    }
    if (s_soundCount >= kCoopMaxSoundsPerMessage) return;
    MsgSoundEntry& e = s_sounds[s_soundCount];
    if (!relative_to_link(pos, e.rel)) {
        e.rel[0] = e.rel[1] = e.rel[2] = 0.0f;
    }
    e.id = id;
    e.kind = kind;
    ++s_soundCount;
}

bool is_rebuilt_by_puppet(u16 id) {
    switch (id) {
    case ID_ZI_J_KAITENGIRI_A: case ID_ZI_J_KAITENGIRI_B:
    case ID_ZI_J_KAITENGIRIL_A: case ID_ZI_J_KAITENGIRIL_B:
    case ID_ZI_J_KAITENGIRIL_C: case ID_ZI_J_KAITENGIRIL_D:
    case ID_ZI_J_KAITENGIRID_A: case ID_ZI_J_KAITENGIRID_B: case ID_ZI_J_KAITENGIRID_C:
    case ID_ZI_J_KAITENGIRID_D: case ID_ZI_J_KAITENGIRID_E: case ID_ZI_J_KAITENGIRID_F:
    case ID_ZI_J_KAITENGIRI_INWTR_A: case ID_ZI_J_KAITENGIRI_INWTR_B:
    case ID_ZI_J_WL_KAITENAT_A: case ID_ZI_J_WL_KAITENAT_B:
    case ID_ZI_J_LK_DJGIRI_A: case ID_ZI_J_LK_DJGIRI_B: case ID_ZI_J_LK_DJGIRI_C:
    case ID_ZI_J_LK_DJGIRI_D: case ID_ZI_J_LK_DJGIRI_E: case ID_ZI_J_LK_DJGIRI_F:
        return true;
    default:
        return false;
    }
}

void queue_particle(uint8_t flags, uint8_t type, u16 id, u32 key, const cXyz* pos, const csXyz* rot,
    const cXyz* scale, u8 alpha, const GXColor* prm, const GXColor* env, f32 blend,
    JPABaseEmitter* emitter) {
    if (is_rebuilt_by_puppet(id) || s_particleCount >= kMaxParticlesPerFrame) return;
    MsgParticleEntry& e = s_particles[s_particleCount];
    if (!relative_to_link(pos, e.rel)) return;
    e.id = id;
    e.flags = flags;
    e.type = type;
    e.key = key;
    e.rot[0] = rot ? rot->x : 0;
    e.rot[1] = rot ? rot->y : 0;
    e.rot[2] = rot ? rot->z : 0;
    if (rot != nullptr) e.flags |= kParticleHasRot;
    e.scale[0] = scale ? scale->x : 1.0f;
    e.scale[1] = scale ? scale->y : 1.0f;
    e.scale[2] = scale ? scale->z : 1.0f;
    if (scale != nullptr) e.flags |= kParticleHasScale;
    e.alpha = alpha;
    e.prm[0] = prm ? prm->r : 0xFF;
    e.prm[1] = prm ? prm->g : 0xFF;
    e.prm[2] = prm ? prm->b : 0xFF;
    e.prm[3] = prm ? prm->a : 0xFF;
    if (prm != nullptr) e.flags |= kParticleHasPrm;
    e.env[0] = env ? env->r : 0xFF;
    e.env[1] = env ? env->g : 0xFF;
    e.env[2] = env ? env->b : 0xFF;
    e.env[3] = env ? env->a : 0xFF;
    if (env != nullptr) e.flags |= kParticleHasEnv;
    e.blend = blend;
    s_particleEmitters[s_particleCount] = emitter;
    ++s_particleCount;
}

void on_local_sound_pre(ModContext*, void*, void*, void*) {
    if (s_suppress > 0) return;
    voices_begin_local();
}

void on_obj_sound(void* args, uint8_t kind) {
    if (!s_captureSounds || s_suppress > 0) return;
    Z2SoundObjBase* self = mods::arg<Z2SoundObjBase*>(args, 0);
    if (self == nullptr || !self->alive_) return;
    const Vec* pos = reinterpret_cast<const Vec*>(self->pos_);
    if (!(in_owner_window() || in_owned_memory(self) || self == s_hookSound || in_owned_memory(pos))) {
        return;
    }
    queue_sound(mods::arg<JAISoundID>(args, 1), kind, pos);
}

void on_obj_sound_post(ModContext*, void* args, void*, void*) {
    on_obj_sound(args, kSoundOneShot);
}

void on_obj_level_sound_post(ModContext*, void* args, void*, void*) {
    on_obj_sound(args, kSoundLevel);
}

void on_se(void* args, uint8_t kind) {
    if (!s_captureSounds || s_suppress > 0) return;
    const Vec* pos = mods::arg<const Vec*>(args, 2);
    if (pos == nullptr) return;
    if (!(in_owner_window() || in_owned_memory(pos))) return;
    queue_sound(mods::arg<JAISoundID>(args, 1), kind, pos);
}

void on_se_start_post(ModContext*, void* args, void*, void*) {
    on_se(args, kSoundOneShot);
}

void on_se_start_level_post(ModContext*, void* args, void*, void*) {
    on_se(args, kSoundLevel);
}

int s_inKeyedSet = 0;

HookAction on_particle_keyed_pre(ModContext*, void*, void*, void*) {
    ++s_inKeyedSet;
    return HOOK_CONTINUE;
}

void on_particle_keyed_post(ModContext*, void* args, void* retval, void*) {
    if (s_inKeyedSet > 0) --s_inKeyedSet;
    if (s_inWalkDust > 0) return;
    if (!s_captureVfx || s_suppress > 0 || retval == nullptr) return;
    const cXyz* pos = mods::arg<const cXyz*>(args, 4);
    if (pos == nullptr || !(in_owner_window() || in_owned_memory(pos))) return;
    const u32 key = *static_cast<u32*>(retval);
    if (key == 0) return;

    queue_particle(kParticleKeyed, mods::arg<u8>(args, 2), mods::arg<u16>(args, 3), key, pos,
        mods::arg<const csXyz*>(args, 6), mods::arg<const cXyz*>(args, 7), mods::arg<u8>(args, 8),
        mods::arg<const GXColor*>(args, 11), mods::arg<const GXColor*>(args, 12),
        mods::arg<f32>(args, 14), nullptr);
}

void on_particle_once_post(ModContext*, void* args, void* retval, void*) {
    if (s_inKeyedSet > 0) return;
    if (s_inWalkDust > 0) return;
    if (!s_captureVfx || s_suppress > 0 || retval == nullptr) return;
    if (*static_cast<JPABaseEmitter**>(retval) == nullptr) return;
    const cXyz* pos = mods::arg<const cXyz*>(args, 3);
    if (pos == nullptr || !(in_owner_window() || in_owned_memory(pos))) return;

    queue_particle(0, mods::arg<u8>(args, 1), mods::arg<u16>(args, 2), 0, pos,
        mods::arg<const csXyz*>(args, 5), mods::arg<const cXyz*>(args, 6), mods::arg<u8>(args, 7),
        mods::arg<const GXColor*>(args, 10), mods::arg<const GXColor*>(args, 11),
        mods::arg<f32>(args, 13), *static_cast<JPABaseEmitter**>(retval));
}

HookAction on_walk_dust_pre(ModContext*, void*, void*, void*) {
    ++s_inWalkDust;
    return HOOK_CONTINUE;
}
void on_walk_dust_post(ModContext*, void*, void*, void*) {
    if (s_inWalkDust > 0) --s_inWalkDust;
}

HookAction on_boomerang_execute_pre(ModContext*, void*, void*, void*) {
    s_windowBoomerang = true;
    return HOOK_CONTINUE;
}
void on_boomerang_execute_post(ModContext*, void*, void*, void*) {
    s_windowBoomerang = false;
}
HookAction on_spinner_execute_pre(ModContext*, void*, void*, void*) {
    s_windowSpinner = true;
    return HOOK_CONTINUE;
}
void on_spinner_execute_post(ModContext*, void*, void*, void*) {
    s_windowSpinner = false;
}

f32 sound_volume() {
    int64_t percent = 35;
    const ConfigVarHandle h = features_vars().soundVolume;
    if (h != 0) svc_config->get_int(mod_ctx, h, &percent);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return static_cast<f32>(percent) / 100.0f;
}

Vec s_oneShotPos[64];
int s_oneShotNext = 0;

struct LevelSlot {
    uint32_t id = 0;
    uint32_t lastTick = 0;
    Vec pos = {0.0f, 0.0f, 0.0f};
};
LevelSlot s_levelSlots[24];

Vec* level_slot_pos(uint32_t id) {
    LevelSlot* freeSlot = nullptr;
    for (LevelSlot& s : s_levelSlots) {
        if (s.id == id) {
            s.lastTick = s_tick;
            return &s.pos;
        }
        if (freeSlot == nullptr && (s.id == 0 || s_tick - s.lastTick > 30)) freeSlot = &s;
    }
    if (freeSlot == nullptr) return nullptr;
    freeSlot->id = id;
    freeSlot->lastTick = s_tick;
    return &freeSlot->pos;
}

struct OneShotSeen {
    uint32_t id = 0;
    uint32_t lastTick = 0;
};
OneShotSeen s_oneShotSeen[64];
const uint32_t kOneShotQuietTicks = 8;

bool one_shot_should_play(uint32_t id) {
    OneShotSeen* freeSlot = nullptr;
    OneShotSeen* oldest = &s_oneShotSeen[0];
    for (OneShotSeen& s : s_oneShotSeen) {
        if (s.id == id) {
            const bool quiet = s_tick - s.lastTick > kOneShotQuietTicks;
            s.lastTick = s_tick;
            return quiet;
        }
        if (freeSlot == nullptr && (s.id == 0 || s_tick - s.lastTick > kOneShotQuietTicks)) {
            freeSlot = &s;
        }
        if (s.lastTick < oldest->lastTick) oldest = &s;
    }
    OneShotSeen* slot = freeSlot != nullptr ? freeSlot : oldest;
    slot->id = id;
    slot->lastTick = s_tick;
    return true;
}

struct KeySlot {

    uint8_t sender = kCoopNoPlayer;
    u32 senderKey = 0;
    u32 localKey = 0;
    uint32_t lastTick = 0;
    MsgParticleEntry last{};
};
KeySlot s_keySlots[64];

KeySlot* key_slot(uint8_t sender, u32 senderKey) {
    KeySlot* freeSlot = nullptr;
    for (KeySlot& s : s_keySlots) {
        if (s.sender == sender && s.senderKey == senderKey && s_tick - s.lastTick <= 5) return &s;
        if (freeSlot == nullptr && (s.senderKey == 0 || s_tick - s.lastTick > 5)) freeSlot = &s;
    }
    if (freeSlot != nullptr) {
        freeSlot->sender = sender;
        freeSlot->senderKey = senderKey;
        freeSlot->localKey = 0;
    }
    return freeSlot;
}

bool sender_position(uint8_t sender, float* x, float* y, float* z) {
    return puppet_hook_get_pose_of(sender, x, y, z, nullptr, nullptr, nullptr);
}

const s32 kOnceMaxFrames = 30;

const uint32_t kKeyedBridgeTicks = 3;

void capture_emitter_state(MsgParticleEntry& e, const JPABaseEmitter* em) {
    e.flags |= kParticleHasState;
    e.rate = em->mRate;
    e.livePrm[0] = em->mGlobalPrmClr.r;
    e.livePrm[1] = em->mGlobalPrmClr.g;
    e.livePrm[2] = em->mGlobalPrmClr.b;
    e.livePrm[3] = em->mGlobalPrmClr.a;
    e.liveEnv[0] = em->mGlobalEnvClr.r;
    e.liveEnv[1] = em->mGlobalEnvClr.g;
    e.liveEnv[2] = em->mGlobalEnvClr.b;
    e.liveEnv[3] = em->mGlobalEnvClr.a;
    e.gscl[0] = em->mGlobalScl.x;
    e.gscl[1] = em->mGlobalScl.y;
    e.gscl[2] = em->mGlobalScl.z;
    e.pscl[0] = em->mGlobalPScl.x;
    e.pscl[1] = em->mGlobalPScl.y;
    e.life = em->mLifeTime;
    e.vol = em->mVolumeSize;
    e.awayCenter = em->mAwayFromCenterSpeed;
    e.awayAxis = em->mAwayFromAxisSpeed;
    e.dirSpeed = em->mDirSpeed;
    e.dir[0] = em->mLocalDir.x;
    e.dir[1] = em->mLocalDir.y;
    e.dir[2] = em->mLocalDir.z;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) e.grot[r * 3 + c] = em->mGlobalRot[r][c];
    }
}

void apply_emitter_state(const MsgParticleEntry& e, JPABaseEmitter* em) {
    if (em == nullptr || (e.flags & kParticleHasState) == 0) return;
    em->mRate = e.rate;
    em->mGlobalPrmClr.r = e.livePrm[0];
    em->mGlobalPrmClr.g = e.livePrm[1];
    em->mGlobalPrmClr.b = e.livePrm[2];
    em->mGlobalPrmClr.a = e.livePrm[3];
    em->mGlobalEnvClr.r = e.liveEnv[0];
    em->mGlobalEnvClr.g = e.liveEnv[1];
    em->mGlobalEnvClr.b = e.liveEnv[2];
    em->mGlobalEnvClr.a = e.liveEnv[3];
    em->mGlobalScl.set(e.gscl[0], e.gscl[1], e.gscl[2]);
    em->mGlobalPScl.set(e.pscl[0], e.pscl[1]);
    em->mLifeTime = e.life;
    em->mVolumeSize = e.vol;
    em->mAwayFromCenterSpeed = e.awayCenter;
    em->mAwayFromAxisSpeed = e.awayAxis;
    em->mDirSpeed = e.dirSpeed;
    em->mLocalDir.set(e.dir[0], e.dir[1], e.dir[2]);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) em->mGlobalRot[r][c] = e.grot[r * 3 + c];
    }
}

void emit_particle(dPa_control_c* particles, daAlink_c* alink, const MsgParticleEntry& e, f32 x,
    f32 y, f32 z, KeySlot* slot) {
    const cXyz pos(x + e.rel[0], y + e.rel[1], z + e.rel[2]);
    const csXyz rot(e.rot[0], e.rot[1], e.rot[2]);
    const cXyz scale(e.scale[0], e.scale[1], e.scale[2]);
    const GXColor prm = {e.prm[0], e.prm[1], e.prm[2], e.prm[3]};
    const GXColor env = {e.env[0], e.env[1], e.env[2], e.env[3]};
    const csXyz* rotP = (e.flags & kParticleHasRot) ? &rot : nullptr;
    const cXyz* scaleP = (e.flags & kParticleHasScale) ? &scale : nullptr;
    const GXColor* prmP = (e.flags & kParticleHasPrm) ? &prm : nullptr;
    const GXColor* envP = (e.flags & kParticleHasEnv) ? &env : nullptr;
    if (slot != nullptr) {
        slot->localKey = particles->set(slot->localKey, e.type, e.id, &pos, &alink->tevStr, rotP,
            scaleP, e.alpha, nullptr, -1, prmP, envP, nullptr, e.blend);
        dComIfGp_particle_levelEmitterOnEventMove(slot->localKey);
        apply_emitter_state(e, dComIfGp_particle_getEmitter(slot->localKey));
        return;
    }
    JPABaseEmitter* emitter = particles->set(e.type, e.id, &pos, &alink->tevStr, rotP, scaleP,
        e.alpha, nullptr, -1, prmP, envP, nullptr, e.blend);
    if (emitter != nullptr) {
        apply_emitter_state(e, emitter);
        emitter->quitImmortalEmitter();
        if (emitter->mMaxFrame == 0) emitter->mMaxFrame = kOnceMaxFrames;
    }
}

void flush_queues() {
    if (s_soundCount > 0) {
        uint8_t buffer[1 + kCoopMaxSoundsPerMessage * sizeof(MsgSoundEntry)];
        buffer[0] = static_cast<uint8_t>(s_soundCount);
        std::memcpy(buffer + 1, s_sounds, s_soundCount * sizeof(MsgSoundEntry));
        coop_net_send(kMsgSounds, buffer, 1 + s_soundCount * sizeof(MsgSoundEntry));
        s_soundCount = 0;
    }

    for (int i = 0; i < s_particleCount; ++i) {
        MsgParticleEntry& e = s_particles[i];
        const JPABaseEmitter* em = (e.flags & kParticleKeyed)
                                       ? dComIfGp_particle_getEmitter(e.key)
                                       : s_particleEmitters[i];
        if (em != nullptr) capture_emitter_state(e, em);
    }
    int sent = 0;
    while (sent < s_particleCount) {
        int n = s_particleCount - sent;
        if (n > kCoopMaxParticlesPerMessage) n = kCoopMaxParticlesPerMessage;
        uint8_t buffer[1 + kCoopMaxParticlesPerMessage * sizeof(MsgParticleEntry)];
        buffer[0] = static_cast<uint8_t>(n);
        std::memcpy(buffer + 1, s_particles + sent, n * sizeof(MsgParticleEntry));
        coop_net_send(kMsgParticles, buffer, 1 + n * sizeof(MsgParticleEntry));
        sent += n;
    }
    s_particleCount = 0;
}

}

void fx_init() {
    const ModResult a = mods::hook::add_post<CoopFxObjSound>(on_obj_sound_post);
    const ModResult b = mods::hook::add_post<CoopFxObjLevelSound>(on_obj_level_sound_post);
    mods::hook::add_pre<CoopFxSeStart>(
        [](ModContext* ctx, void* a, void* r, void* u) -> HookAction {
            on_local_sound_pre(ctx, a, r, u);
            return HOOK_CONTINUE;
        });
    mods::hook::add_pre<CoopFxSeStartLevel>(
        [](ModContext* ctx, void* a, void* r, void* u) -> HookAction {
            on_local_sound_pre(ctx, a, r, u);
            return HOOK_CONTINUE;
        });
    mods::hook::add_pre<CoopFxObjSound>(
        [](ModContext* ctx, void* a, void* r, void* u) -> HookAction {
            on_local_sound_pre(ctx, a, r, u);
            return HOOK_CONTINUE;
        });
    const ModResult c = mods::hook::add_post<CoopFxSeStart>(on_se_start_post);
    const ModResult d = mods::hook::add_post<CoopFxSeStartLevel>(on_se_start_level_post);
    mods::hook::add_pre<CoopFxParticleKeyed>(on_particle_keyed_pre);
    mods::hook::add_pre<CoopFxWalkDustFour>(on_walk_dust_pre);
    mods::hook::add_post<CoopFxWalkDustFour>(on_walk_dust_post);
    mods::hook::add_pre<CoopFxWalkDustTwo>(on_walk_dust_pre);
    mods::hook::add_post<CoopFxWalkDustTwo>(on_walk_dust_post);
    const ModResult e = mods::hook::add_post<CoopFxParticleKeyed>(on_particle_keyed_post);
    const ModResult f = mods::hook::add_post<CoopFxParticleOnce>(on_particle_once_post);
    const ModResult g = mods::hook::add_pre<CoopFxBoomerangExecute>(on_boomerang_execute_pre);
    const ModResult h = mods::hook::add_post<CoopFxBoomerangExecute>(on_boomerang_execute_post);
    const ModResult i = mods::hook::add_pre<CoopFxSpinnerExecute>(on_spinner_execute_pre);
    const ModResult j = mods::hook::add_post<CoopFxSpinnerExecute>(on_spinner_execute_post);
    coop_log::info(
        "coop_mod: [FX] hooks objSound={} objLevel={} se={} seLevel={} pKeyed={} pOnce={} "
        "boom={}/{} spin={}/{}",
        static_cast<int>(a), static_cast<int>(b), static_cast<int>(c), static_cast<int>(d),
        static_cast<int>(e), static_cast<int>(f), static_cast<int>(g), static_cast<int>(h),
        static_cast<int>(i), static_cast<int>(j));
}

void fx_owner_window(bool active) {
    s_windowAlink = active;
}

bool local_in_cutscene() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return false;

    return alink->checkEventRun() != FALSE || boss_local_demo_running();
}

void fx_update() {
    ++s_tick;
    const bool connected = coop_net_connected();
    const CoopFeatureVars& vars = features_vars();

    const bool inCutscene = local_in_cutscene();
    if (connected && !inCutscene) {
        flush_queues();
    } else {
        s_soundCount = 0;
        s_particleCount = 0;
    }
    s_captureSounds = connected && !inCutscene && cfg_bool(vars.syncSounds, true);

    s_captureVfx = connected && !inCutscene;
    refresh_owned_ranges();

    if (inCutscene) return;

    daAlink_c* alink = daAlink_getAlinkActorClass();
    dPa_control_c* particles = g_dComIfG_gameInfo.play.getParticle();
    if (alink != nullptr && particles != nullptr) {
        ++s_suppress;
        for (KeySlot& slot : s_keySlots) {
            if (slot.senderKey == 0 || slot.localKey == 0) continue;
            const uint32_t age = s_tick - slot.lastTick;
            if (age < 1 || age > kKeyedBridgeTicks) continue;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!sender_position(slot.sender, &x, &y, &z)) continue;
            emit_particle(particles, alink, slot.last, x, y, z, &slot);
        }
        --s_suppress;
    }
}

void fx_on_sounds(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < 1 || !cfg_bool(features_vars().syncSounds, true)) return;

    if (local_in_cutscene()) return;
    const size_t count = payload[0];
    if (count > static_cast<size_t>(kCoopMaxSoundsPerMessage) ||
        size < 1 + count * sizeof(MsgSoundEntry)) {
        return;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!sender_position(from, &x, &y, &z)) return;

    Z2AudioMgr* se = Z2AudioMgr::getInterface();
    if (se == nullptr) return;
    const f32 volume = sound_volume();
    if (volume <= 0.0f) return;

    const CoopPeer& peer = features_peer_of(from);
    const s8 reverb = peer.present && peer.inGame
                          ? dComIfGp_getReverb(static_cast<int>(peer.curRoom))
                          : 0;

    if (peer.present) {

        const char* voiceSkin = peer.skins.name[kSkinChoiceVoice];
        if (voiceSkin[0] != '\0' &&
            skins_have(voiceSkin, peer.skins.hash[kSkinChoiceVoice])) {
            voices_begin(voiceSkin);
        }
    }

    ++s_suppress;
    for (size_t i = 0; i < count; ++i) {
        MsgSoundEntry entry;
        std::memcpy(&entry, payload + 1 + i * sizeof(MsgSoundEntry), sizeof(entry));
        const f32 px = x + entry.rel[0];
        const f32 py = y + entry.rel[1];
        const f32 pz = z + entry.rel[2];
        if (entry.kind == kSoundLevel) {
            Vec* pos = level_slot_pos(entry.id);
            if (pos == nullptr) continue;
            pos->x = px;
            pos->y = py;
            pos->z = pz;
            se->seStartLevel(JAISoundID(entry.id), pos, 0, reverb, 1.0f, volume, -1.0f, -1.0f, 0);
            continue;
        }
        if (!one_shot_should_play(entry.id)) continue;
        Vec& pos = s_oneShotPos[s_oneShotNext];
        s_oneShotNext = (s_oneShotNext + 1) % 64;
        pos.x = px;
        pos.y = py;
        pos.z = pz;
        se->seStart(JAISoundID(entry.id), &pos, 0, reverb, 1.0f, volume, -1.0f, -1.0f, 0);
    }
    --s_suppress;
}

void fx_on_particles(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < 1) return;

    if (local_in_cutscene()) return;
    const size_t count = payload[0];
    if (count > static_cast<size_t>(kCoopMaxParticlesPerMessage) ||
        size < 1 + count * sizeof(MsgParticleEntry)) {
        return;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!sender_position(from, &x, &y, &z)) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    dPa_control_c* particles = g_dComIfG_gameInfo.play.getParticle();
    if (alink == nullptr || particles == nullptr) return;

    ++s_suppress;
    for (size_t i = 0; i < count; ++i) {
        MsgParticleEntry e;
        std::memcpy(&e, payload + 1 + i * sizeof(MsgParticleEntry), sizeof(e));
        if (e.flags & kParticleKeyed) {

            KeySlot* slot = key_slot(from, e.key);
            if (slot == nullptr) continue;
            slot->last = e;
            slot->lastTick = s_tick;
            emit_particle(particles, alink, e, x, y, z, slot);
        } else {
            emit_particle(particles, alink, e, x, y, z, nullptr);
        }
    }
    --s_suppress;
}

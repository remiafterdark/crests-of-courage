

#include "models.hpp"

#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphLoader/J3DAnmLoader.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphBase/J3DTransform.h"
#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/JParticle/JPAEmitter.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/TColor.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "SSystem/SComponent/c_lib.h"
#include "SSystem/SComponent/c_math.h"
#include "SSystem/SComponent/c_m3d.h"
#include "SSystem/SComponent/c_m3d_g_pla.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_midna.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "d/d_drawlist.h"
#include "d/d_camera.h"
#include "d/d_kankyo.h"
#include "d/d_resorce.h"
#include "d/d_particle_name.h"
#include "d/d_item_data.h"
#include "d/d_resorce.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"
#include "f_op/f_op_view.h"
#include "m_Do/m_Do_lib.h"
#include "m_Do/m_Do_mtx.h"
#include "net/messages.hpp"
#include "net/protocol.hpp"
#include "mod.hpp"
#include "res/Object/AlAnm.h"
#include "res/Object/Alink.h"
#include "res/Object/Wmdl.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <cstring>
#include <fstream>

extern const LogService* svc_log;
extern const HookService* svc_hook;

DEFINE_HOOK(&daAlink_c::execute, PuppetAlinkExecuteHook);
DEFINE_HOOK(&daAlink_c::draw, PuppetAlinkDrawHook);

DEFINE_HOOK_SYMBOL("dScnKy_env_light_c::exeKankyo", void(dScnKy_env_light_c*),
    PuppetKankyoExeHook);

#if defined(_MSC_VER)
DEFINE_HOOK_SYMBOL("??1daAlink_c@@UEAA@XZ", void(daAlink_c*), PuppetAlinkDtorHook);
#else
DEFINE_HOOK_SYMBOL("_ZN9daAlink_cD1Ev", void(daAlink_c*), PuppetAlinkDtorHook);
#endif

DEFINE_HOOK((static_cast<void* (*)(const char*, const char*, dRes_info_c*, int)>(
                &dRes_control_c::getRes)),
    LocalSkinResByNameHook);

DEFINE_HOOK_SYMBOL("daAlink_c::loadAramBmd",
    J3DModelData*(daAlink_c*, u16, u32), LocalAramBmdHook);
DEFINE_HOOK((static_cast<void* (*)(const char*, s32, dRes_info_c*, int)>(&dRes_control_c::getRes)),
    LocalSkinResByIndexHook);

namespace {

const f32 kPuppetSpawnDist = 150.0f;
const int kPuppetMaxJoints = 128;

const int kUnderRootJoint = 0;
const int kUpperBodyRootJoint = 1;
const int kBackbone2Joint = 2;
const u32 kPuppetBckBufferSize = 0x2C00;

const u32 kGuardSize = 0x40u;
const u8 kGuardByte = 0xA5u;

struct OutfitFiles {
    const char* arc;
    const char* body;
    const char* face;
    const char* hat;
    const char* hands;
    bool hasKmdlHatTail;

    u16 bodyResIdx;
    bool isWolf;
};

const OutfitFiles kOutfitFiles[] = {
      {"Kmdl", "al.bmd", "al_face.bmd", "al_head.bmd", "al_hands.bmd", true, 0xFFFF, false},
      {"Bmdl", "bl.bmd", "al_face.bmd", "bl_head.bmd", "bl_hands.bmd", false, 0xFFFF, false},
      {"Zmdl", "zl.bmd", "zl_face.bmd", "zl_head.bmd", "al_hands.bmd", false, 0xFFFF, false},
      {"Mmdl", "ml.bmd", "al_face.bmd", "ml_head.bmd", "al_hands.bmd", false, 0xFFFF, false},
      {"Wmdl", nullptr, nullptr, nullptr, nullptr, false, dRes_INDEX_WMDL_BMD_WL_e, true},
};

const OutfitFiles& outfit_files(u8 outfit) {
    if (outfit >= sizeof(kOutfitFiles) / sizeof(kOutfitFiles[0])) outfit = kPuppetOutfitDefault;
    return kOutfitFiles[outfit];
}

u8 detect_local_outfit_for_a_press() {
    const u8 clothes = dComIfGs_getSelectEquipClothes();
    if (clothes == dItemNo_WEAR_CASUAL_e) return kPuppetOutfitCasual;
    if (clothes == dItemNo_WEAR_ZORA_e) return kPuppetOutfitZora;
    if (clothes == dItemNo_ARMOR_e) return kPuppetOutfitMagicArmor;
    return kPuppetOutfitDefault;
}

const int kAnimCacheSize = kCoopMaxPlayers * 8;
struct PuppetAnimCacheEntry {
    u16 resIdx = 0xFFFF;
    mDoExt_bckAnm* bck = nullptr;

    u8* buf = nullptr;
    u32 lastUsed = 0;

    u32 usedFrame = 0;
};
PuppetAnimCacheEntry s_puppetAnimCache[kAnimCacheSize];
u32 s_puppetAnimCacheClock = 0;

u32 s_puppetFrame = 0;

uintptr_t s_liveAnmVtbl = 0;
int s_deadAnmLogCount = 0;

bool vtbl_looks_like_code(uintptr_t v) {
    const daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return false;
    const uintptr_t ref = *reinterpret_cast<const uintptr_t*>(alink);
    const uintptr_t d = v > ref ? v - ref : ref - v;
    return v != 0 && d < 0x10000000ull;
}

bool anim_is_live(mDoExt_bckAnm* bck) {
    if (bck == nullptr) return false;
    const J3DAnmTransform* anm = bck->getBckAnm();
    if (anm == nullptr) return false;
    const uintptr_t vtbl = *reinterpret_cast<const uintptr_t*>(anm);
    if (!vtbl_looks_like_code(vtbl)) return false;

    return s_liveAnmVtbl == 0 || vtbl == s_liveAnmVtbl;
}

const int kAnmSlots = 3;

const f32 kFrameCorrectRate = 0.25f;
const f32 kFrameResyncThreshold = 4.0f;

struct PuppetAnimHalf {
    u16 resIdx[kAnmSlots] = {0xFFFF, 0xFFFF, 0xFFFF};
    f32 frame[kAnmSlots] = {0.0f, 0.0f, 0.0f};
    f32 targetFrame[kAnmSlots] = {0.0f, 0.0f, 0.0f};
    f32 rate[kAnmSlots] = {1.0f, 1.0f, 1.0f};
    f32 ratio[kAnmSlots] = {0.0f, 0.0f, 0.0f};
    mDoExt_AnmRatioPack packs[kAnmSlots];
    mDoExt_MtxCalcAnmBlendTblOld* tbl = nullptr;
};

struct PuppetItemData {
    J3DModelData* data = nullptr;
    u8* buf = nullptr;
    bool fromOutfit = false;
    bool shared = false;
};

const int kChainLinkPool = 384;
const int kRodSegments = 15;
const int kSpinEmitterMax = 6;

struct PuppetAttachSlot {
    u8 kind = kPuppetHeldNone;
    J3DModel* model = nullptr;

    u16 wireIdx = 0xFFFF;
};

struct Puppet {
    int state = 0;
    u8 outfit = kPuppetOutfitDefault;
    J3DModel* model = nullptr;
    J3DTransformInfo oldFrameTrans[kPuppetMaxJoints];
    Quaternion oldFrameQuat[kPuppetMaxJoints];
    mDoExt_MtxCalcOldFrame* oldFrame = nullptr;
    u16 oldFrameJointNum = 0;
    bool oldFrameMorfPending = false;
    PuppetAnimHalf under;
    PuppetAnimHalf upper;
    J3DModel* faceModel = nullptr;
    J3DModel* hatModel = nullptr;
    J3DModel* handsModel = nullptr;
    J3DModel* bootModels[2] = {nullptr, nullptr};
    u8 bootsVisible = 0;
    u8 handL = 1;
    u8 handR = 6;
    J3DModel* swordModel = nullptr;
    J3DModel* sheathModel = nullptr;
    J3DModel* shieldModel = nullptr;
    u8 swordId = kPuppetSwordNone;
    u8 sheathId = kPuppetSheathNone;
    char shieldArc[16] = {};
    u8 wantSword = kPuppetSwordNone;
    u8 wantSheath = kPuppetSheathNone;
    u8 wantSwordVisible = 0;
    u8 wantShield = 0;
    u8 swordInHand = 0;
    u8 shieldInHand = 0;
    char wantShieldArc[16] = {};
    u16 swordJoint = 0;
    u16 sheathJoint = 0;
    u16 shieldJoint = 0;
    cXyz pos;
    cXyz prevPos;
    csXyz angle;
    cXyz targetPos;
    csXyz targetAngle;
    char stage[16] = {};
    s32 room = -1;
    bool arcSwapHold = false;
    bool shieldSwapHold = false;
    bool respawnPending = false;
    u8 respawnOutfit = kPuppetOutfitDefault;
    s16 bodyRotX = 0;
    s16 bodyRotY = 0;
    s16 bodyRotZ = 0;
    u8 rootClearMask = 0;
    f32 rootClearX = 0.0f;
    f32 rootClearY = 0.0f;
    f32 rootClearZ = 0.0f;
    s16 footAngles[4][3] = {};
    PuppetAttachSlot attachSlots[kPuppetAttachSlots];
    char rodArc[16] = {};
    char rideArc[16] = {};

    char rideArcBuilt[2][16] = {};
    u16 rideIdxBuilt[2] = {0xFFFF, 0xFFFF};
    AttachedModelSnapshot attached[kPuppetAttachSlots];
    u8 chainKind = kPuppetChainNone;
    u8 chainCount = 0;
    s16 chainStopTime = 0;
    f32 chainPts[kPuppetChainPts][3] = {};
    u8 vfxSpin = kSpinVfxNone;
    u8 vfxSpinApplied = kSpinVfxNone;
    u8 vfxJumpLand = 0;
    u8 vfxJumpLandSeen = 0;
    u8 vfxDig = 0;
    s16 digAngleX = 0;
    cXyz digPos;
    u8 warpOn = 0;
    f32 warpScroll = 0.0f;
    f32 warpDissolve = 0.0f;
    u32 shadowKey = 0;

    HorseSnapshot horse{};
    int horseAge = 1 << 20;

    HorseSnapshot horsePrev{};
    bool horsePrevValid = false;
    int horseGap = 1;
    J3DModel* horseModel = nullptr;
    bool horseArcHeld = false;
    mDoExt_3DlineMat1_c* horseReins = nullptr;
    f32 horseIdleFrame = 0.0f;
    u32 horseShadowKey = 0;
    u16 horseIdleAnm = 0xFFFF;
    uint32_t horseIdleSeq = 0;

    MidnaSnapshot midna{};
    bool midnaActive = false;
    int midnaAge = 0;

    J3DModel* midnaSolid[4] = {};
    J3DModel* midnaShadow[5] = {};

    mDoExt_invisibleModel midnaInv[4];
    bool midnaInvReady[4] = {};

    bool midnaSolidFailed = false;
    bool midnaShadowFailed = false;

    bool warpWarmDone = false;
    int warpWarmTicks = 0;
    int warpWarmFrames = 0;

    f32 lanternGlowScale = 0.0f;

    u32 lanternPeekZ = 0;

    PuppetItemData itemData[kPuppetHeldCount];

    u8 getItemNoCached = 0xFF;

    char getItemArc[32] = "";
    char getItemArcOld[32] = "";
    int getItemArcOldTimer = 0;
    u8 pendingOutfit = kPuppetOutfitDefault;
    s16 hatPitch = 0;
    J3DModel* chainLinks[kChainLinkPool] = {};
    int chainLinksUsed = 0;
    J3DModel* rodSegModels[kRodSegments] = {};
    u8 rodSegKind = kPuppetChainNone;
    bool peerVisible = true;

    int8_t peerRoom = -1;
    bool lowHealth = false;
    bool releaseRequested = false;

    bool holdsArc = false;

    char heldArc[16] = {};

    bool skinDirty = false;

    SkinChoices builtSkins = {};

    J3DModelData* mtxCalcData = nullptr;
    int mtxCalcJoints[3] = {-1, -1, -1};

    J3DMtxCalc* mtxCalcSaved[3] = {nullptr, nullptr, nullptr};
    char nametagName[32] = "Player";
    bool haveJumpLandBaseline = false;
    u32 spinEmitterKeys[kSpinEmitterMax] = {};
    u32 digEmitterKeys[3] = {};
};

const int kMaxPuppets = kCoopMaxPlayers;
Puppet s_puppetSlots[kMaxPuppets];

Puppet* s_pup = &s_puppetSlots[0];

const int kPendingFreeMax = kCoopMaxPlayers * 48;
J3DModel* s_pendingFree[kPendingFreeMax];
int s_pendingFreeCount = 0;

J3DModel* s_pendingFreeOlder[kPendingFreeMax];
int s_pendingFreeOlderCount = 0;

void puppet_free_later(J3DModel*& model) {
    if (model == nullptr) return;
    if (s_pendingFreeCount < kPendingFreeMax) {
        s_pendingFree[s_pendingFreeCount++] = model;
    } else {

        coop_log::warn("coop_mod: [PUPPET] deferred-free queue full - leaking one model on purpose");
    }
    model = nullptr;
}

void puppet_flush_pending_frees() {

    for (int i = 0; i < s_pendingFreeOlderCount; ++i) {

        colors_detach_model(s_pendingFreeOlder[i]);
        JKR_DELETE(s_pendingFreeOlder[i]);
        s_pendingFreeOlder[i] = nullptr;
    }
    s_pendingFreeOlderCount = 0;
    for (int i = 0; i < s_pendingFreeCount; ++i) {
        s_pendingFreeOlder[i] = s_pendingFree[i];
        s_pendingFree[i] = nullptr;
    }
    s_pendingFreeOlderCount = s_pendingFreeCount;
    s_pendingFreeCount = 0;
}
inline Puppet& pup() { return *s_pup; }

uint8_t s_pupId = 0;

struct PuppetScope {
    Puppet* saved;
    uint8_t savedId;
    PuppetScope(uint8_t id) : saved(s_pup), savedId(s_pupId) {
        s_pup = &s_puppetSlots[id];
        s_pupId = id;
    }
    ~PuppetScope() {
        s_pup = saved;
        s_pupId = savedId;
    }
};

struct PuppetLanternFlame {
    cXyz pos;
    s16 angleY = 0;
    f32 power = 0.0f;

    int ttl = 0;
};
PuppetLanternFlame s_lanternFlame[kCoopMaxPlayers];

const int kLanternFlameTtl = 4;

const int kLanternHangJoint = 1;
const f32 kLanternHangLength = 17.0f;

cXyz s_lanternHangTarget;
bool s_lanternHangHaveTarget = false;

int puppet_kantera_callback(J3DJoint*, int phase) {

    if (phase != 0) return 1;

    cXyz jointPos;
    mDoMtx_multVecZero(J3DSys::mCurrentMtx, &jointPos);

    const f32 sx = J3DSys::mCurrentMtx[0][0];
    const f32 sy = J3DSys::mCurrentMtx[1][0];
    const f32 sz = J3DSys::mCurrentMtx[2][0];
    f32 scale = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (!(scale > 0.01f) || scale > 100.0f) scale = 1.0f;

    cXyz down(0.0f, -kLanternHangLength * scale, 0.0f);
    if (s_lanternHangHaveTarget) {
        const cXyz toFlame = s_lanternHangTarget - jointPos;
        const f32 len = toFlame.abs();

        if (len > kLanternHangLength * 0.25f && len < kLanternHangLength * 4.0f) down = toFlame;
    }

    const f32 len = down.abs();
    if (len < 0.01f) return 1;

    const cXyz yAxis(-down.x / len, -down.y / len, -down.z / len);

    cXyz ref(J3DSys::mCurrentMtx[0][2], J3DSys::mCurrentMtx[1][2], J3DSys::mCurrentMtx[2][2]);
    cXyz xAxis = yAxis.outprod(ref);
    if (xAxis.abs() < 0.01f) {

        ref.set(J3DSys::mCurrentMtx[0][0], J3DSys::mCurrentMtx[1][0], J3DSys::mCurrentMtx[2][0]);
        xAxis = yAxis.outprod(ref);
        if (xAxis.abs() < 0.01f) return 1;
    }
    xAxis.normalize();
    const cXyz zAxis = xAxis.outprod(yAxis);

    const f32 s = scale;
    Mtx out;
    out[0][0] = xAxis.x * s; out[0][1] = yAxis.x * s; out[0][2] = zAxis.x * s; out[0][3] = jointPos.x;
    out[1][0] = xAxis.y * s; out[1][1] = yAxis.y * s; out[1][2] = zAxis.y * s; out[1][3] = jointPos.y;
    out[2][0] = xAxis.z * s; out[2][1] = yAxis.z * s; out[2][2] = zAxis.z * s; out[2][3] = jointPos.z;

    J3DModel* model = j3dSys.getModel();
    if (model != nullptr) model->setAnmMtx(kLanternHangJoint, out);
    cMtx_copy(out, J3DSys::mCurrentMtx);
    return 1;
}

const f32 kPuppetOldFrameMorf = 4.0f;

bool puppet_ensure_old_frame(u16 jointNum) {
    if (jointNum == 0 || jointNum > kPuppetMaxJoints) return false;
    if (pup().oldFrame != nullptr) return true;
    std::memset(pup().oldFrameTrans, 0, sizeof(pup().oldFrameTrans));
    std::memset(pup().oldFrameQuat, 0, sizeof(pup().oldFrameQuat));
    pup().oldFrame = JKR_NEW mDoExt_MtxCalcOldFrame(pup().oldFrameTrans, pup().oldFrameQuat);
    if (pup().oldFrame == nullptr) return false;
    pup().oldFrameJointNum = jointNum;
    return true;
}

mDoExt_bckAnm* get_or_load_puppet_anim(daAlink_c* alink, u16 resIdx);
void sync_equipment_models();
void release_outfit_item_data();
bool local_in_hiding_event(daAlink_c* alink);
bool puppet_hold_for_clothes_swap(daAlink_c* alink);
void puppet_swap_finished();
void puppet_swap_starting(daAlink_c* alink);
void puppet_shield_swap_one(daAlink_c* alink);
void puppet_hold_for_shield_swap(daAlink_c* alink);

void puppet_advance_frames(PuppetAnimHalf& half) {
    for (int i = 0; i < kAnmSlots; ++i) {
        if (half.resIdx[i] == 0xFFFF) continue;
        half.frame[i] += half.rate[i];
        const f32 diff = half.targetFrame[i] - half.frame[i];
        if (diff > kFrameResyncThreshold || diff < -kFrameResyncThreshold) {
            half.frame[i] = half.targetFrame[i];
        } else {
            half.frame[i] += diff * kFrameCorrectRate;
        }
    }
}

int s_blendTrace = 0;

void breadcrumb(const char* step) {

    static std::ofstream file;
    if (!file.is_open()) {

        file.open("coop-crash-trail.txt", std::ios::app);
        if (!file.is_open()) return;
        file << "--- session start ---" << std::endl;
    }
    file << step << std::endl;
}

void breadcrumb2(const char* step, const char* detail) {
    static std::string line;
    line = step;
    line += ' ';
    line += (detail != nullptr) ? detail : "(null)";
    breadcrumb(line.c_str());
}

mDoExt_MtxCalcAnmBlendTblOld* puppet_build_blend(daAlink_c* alink, PuppetAnimHalf& half) {
    const bool trace = s_blendTrace > 0;
    if (trace) --s_blendTrace;
    int usable = 0;
    for (int i = 0; i < kAnmSlots; ++i) {
        mDoExt_bckAnm* bck = nullptr;
        if (half.resIdx[i] != 0xFFFF) {
            bck = get_or_load_puppet_anim(alink, half.resIdx[i]);
            if (bck != nullptr && !anim_is_live(bck)) bck = nullptr;
        }
        if (trace) {
            coop_log::trace("coop_mod: [BLEND] slot {} resIdx={} bck={:#x}", i, half.resIdx[i],
                reinterpret_cast<uintptr_t>(bck));
        }
        J3DAnmTransform* anm = (bck != nullptr) ? bck->getBckAnm() : nullptr;
        if (trace) {
            coop_log::trace("coop_mod: [BLEND] slot {} anm={:#x}", i,
                reinterpret_cast<uintptr_t>(anm));
        }
        if (anm != nullptr) {

            f32 f = half.frame[i];
            const f32 maxFrame = static_cast<f32>(anm->getFrameMax());
            if (!(f >= 0.0f)) f = 0.0f;
            if (f > maxFrame) f = maxFrame;
            if (trace) {
                coop_log::trace("coop_mod: [BLEND] slot {} frame={} max={}", i, f, maxFrame);
            }
            anm->setFrame(f);
            ++usable;
        }
        half.packs[i].setAnmTransform(anm);
        half.packs[i].setRatio(half.ratio[i]);
    }
    if (usable == 0) return nullptr;

    if (half.packs[0].getAnmTransform() == nullptr) {
        for (int i = 1; i < kAnmSlots; ++i) {
            if (half.packs[i].getAnmTransform() != nullptr) {
                half.packs[0].setAnmTransform(half.packs[i].getAnmTransform());
                half.packs[0].setRatio(half.packs[i].getRatio());
                half.packs[i].setAnmTransform(nullptr);
                half.packs[i].setRatio(0.0f);
                break;
            }
        }
    }
    if (half.tbl == nullptr) {
        if (pup().oldFrame == nullptr) {
            if (trace) coop_log::trace("coop_mod: [BLEND] no oldFrame - no table this frame");
            return nullptr;
        }
        half.tbl = JKR_NEW mDoExt_MtxCalcAnmBlendTblOld(pup().oldFrame, kAnmSlots, half.packs);
        if (trace) {
            coop_log::trace("coop_mod: [BLEND] new table {:#x} over oldFrame {:#x}",
                reinterpret_cast<uintptr_t>(half.tbl),
                reinterpret_cast<uintptr_t>(pup().oldFrame));
        }
        if (half.tbl == nullptr) return nullptr;
    }
    half.tbl->mNum = kAnmSlots;
    if (trace) coop_log::trace("coop_mod: [BLEND] table ready, usable={}", usable);
    return half.tbl;
}

const int kMidnaPartBody = 0;
const int kMidnaPartHands = 1;
const int kMidnaPartMask = 2;
const int kMidnaPartHair = 3;
const int kMidnaPartGokou = 4;

const char* const kMidnaSolidFiles[4] = {"md.bmd", "md_hands.bmd", "md_mask.bmd",
    "md_hair_hand.bmd"};

const int kMidnaShadowRes[5] = {14, 7, 8, 15, 11};
const char* const kMidnaArc = "Midna";

const int kMidnaJntBackbone1 = 0x01;
const int kMidnaJntHead = 0x04;
const int kMidnaJntHandL = 0x0F;
const int kMidnaJntHandR = 0x13;

const int kMidnaStaleTicks = 20;

void release_midna_models() {
    for (int i = 0; i < 4; ++i) {
        if (pup().midnaInvReady[i] && pup().midnaInv[i].mpPackets != nullptr) {
            JKR_DELETE_ARRAY(pup().midnaInv[i].mpPackets);
        }
        pup().midnaInv[i].mpPackets = nullptr;
        pup().midnaInv[i].mModel = nullptr;
        pup().midnaInvReady[i] = false;
        puppet_free_later(pup().midnaSolid[i]);
    }
    for (int i = 0; i < 5; ++i) {
        puppet_free_later(pup().midnaShadow[i]);
    }
    pup().midnaSolidFailed = false;
    pup().midnaShadowFailed = false;

}

bool arc_in_use_elsewhere(const char* arc) {
    if (arc == nullptr || arc[0] == '\0') return false;
    for (int i = 0; i < kMaxPuppets; ++i) {
        if (i == s_pupId) continue;
        const Puppet& q = s_puppetSlots[i];

        if (q.holdsArc && q.heldArc[0] != 0 && std::strcmp(q.heldArc, arc) == 0) return true;
        if (q.shieldArc[0] != '\0' && std::strcmp(q.shieldArc, arc) == 0) return true;
    }
    return false;
}

void release_arc_share(const char* arc) {
    if (arc_in_use_elsewhere(arc)) return;
    unloadObjectArchive(arc);
}

void clear_installed_mtx_calc() {
    J3DModelData* data = pup().mtxCalcData;
    if (data != nullptr) {
        for (int i = 0; i < 3; ++i) {
            const int joint = pup().mtxCalcJoints[i];
            if (joint < 0 || data->getJointNum() <= joint) continue;
            J3DJoint* node = data->getJointNodePointer(joint);

            if (node != nullptr) node->setMtxCalc(pup().mtxCalcSaved[i]);
        }
    }
    pup().mtxCalcData = nullptr;
    for (int i = 0; i < 3; ++i) {
        pup().mtxCalcJoints[i] = -1;
        pup().mtxCalcSaved[i] = nullptr;
    }
}

void retire_get_item_arc();

void release_puppet_models_only() {

    release_midna_models();
    pup().midnaActive = false;
    J3DModel** const models[] = {
        &pup().model, &pup().faceModel, &pup().hatModel, &pup().handsModel,
        &pup().swordModel, &pup().sheathModel, &pup().shieldModel,
    };
    for (J3DModel** m : models) puppet_free_later(*m);
    for (int i = 0; i < 2; ++i) puppet_free_later(pup().bootModels[i]);
    if (pup().shieldArc[0] != '\0') {
        release_arc_share(pup().shieldArc);
        pup().shieldArc[0] = '\0';
    }
    release_outfit_item_data();
    pup().swordId = kPuppetSwordNone;
    pup().sheathId = kPuppetSheathNone;

    for (int k = 0; k < kPuppetAttachSlots; ++k) {

        puppet_free_later(pup().attachSlots[k].model);
        pup().attachSlots[k].kind = kPuppetHeldNone;
        pup().attachSlots[k].wireIdx = 0xFFFF;
    }

    clear_installed_mtx_calc();
    if (pup().under.tbl != nullptr) JKR_DELETE(pup().under.tbl);
    if (pup().upper.tbl != nullptr) JKR_DELETE(pup().upper.tbl);
    pup().under = PuppetAnimHalf();
    pup().upper = PuppetAnimHalf();
    pup().under.resIdx[0] = dRes_INDEX_ALANM_BCK_WAITS_e;
    pup().under.ratio[0] = 1.0f;
    if (pup().oldFrame != nullptr) {
        JKR_DELETE(pup().oldFrame);
        pup().oldFrame = nullptr;
    }
    pup().oldFrameJointNum = 0;
    pup().oldFrameMorfPending = false;
}

void release_puppet() {
    if (pup().model == nullptr && pup().state == 0) return;

    release_midna_models();
    pup().midnaActive = false;

    pup().horseAge = 1 << 20;
    retire_get_item_arc();
    pup().getItemNoCached = 0xFF;
    puppet_free_later(pup().model);
    puppet_free_later(pup().faceModel);
    puppet_free_later(pup().hatModel);
    puppet_free_later(pup().handsModel);
    for (int i = 0; i < 2; ++i) puppet_free_later(pup().bootModels[i]);
    puppet_free_later(pup().swordModel);
    puppet_free_later(pup().sheathModel);
    puppet_free_later(pup().shieldModel);
    if (pup().shieldArc[0] != '\0') {
        release_arc_share(pup().shieldArc);
        pup().shieldArc[0] = '\0';
    }
    release_outfit_item_data();

    pup().rootClearMask = 0;
    pup().rootClearX = pup().rootClearY = pup().rootClearZ = 0.0f;
    for (int i = 0; i < 4; ++i) {
        for (int a = 0; a < 3; ++a) pup().footAngles[i][a] = 0;
    }
    pup().bodyRotX = pup().bodyRotY = pup().bodyRotZ = 0;
    pup().swordId = kPuppetSwordNone;
    pup().sheathId = kPuppetSheathNone;

    clear_installed_mtx_calc();
    if (pup().under.tbl != nullptr) JKR_DELETE(pup().under.tbl);
    if (pup().upper.tbl != nullptr) JKR_DELETE(pup().upper.tbl);
    pup().under = PuppetAnimHalf();
    pup().upper = PuppetAnimHalf();

    if (pup().oldFrame != nullptr) {
        JKR_DELETE(pup().oldFrame);
        pup().oldFrame = nullptr;
    }
    pup().oldFrameJointNum = 0;
    pup().oldFrameMorfPending = false;
    pup().under.resIdx[0] = dRes_INDEX_ALANM_BCK_WAITS_e;
    pup().under.ratio[0] = 1.0f;

    const u8 loadedOutfit = (pup().state == 1) ? pup().pendingOutfit : pup().outfit;
    if (pup().holdsArc) {

        release_arc_share(pup().heldArc[0] != 0 ? pup().heldArc
                                                    : outfit_files(loadedOutfit).arc);
        pup().heldArc[0] = 0;
        pup().holdsArc = false;
    }
    pup().state = 0;
    pup().skinDirty = false;

    std::memset(&pup().builtSkins, 0, sizeof(pup().builtSkins));
}

const int kSwapHoldFrames = 30;

const int kSwapTailFrames = 6;
u8 s_lastLocalOutfitSeen = 0xFF;
int s_swapHoldFrames = 0;
bool s_swapTimerSeen = false;

bool ptr_in_heap(JKRHeap* heap, const void* p) {
    if (heap == nullptr || p == nullptr) return false;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    return addr >= reinterpret_cast<uintptr_t>(heap->getStartAddr()) &&
           addr < reinterpret_cast<uintptr_t>(heap->getEndAddr());
}

bool model_data_in_heap(JKRHeap* heap, J3DModel* model) {
    return model != nullptr && ptr_in_heap(heap, model->getModelData());
}

bool arc_names_match(const char* a, const char* b) {
    return a != nullptr && b != nullptr && a[0] != '\0' && std::strcmp(a, b) == 0;
}

bool puppet_hold_for_clothes_swap(daAlink_c* alink) {

    const u8 localOutfit =
        alink->checkWolf() ? kPuppetOutfitWolf : detect_local_outfit_for_a_press();
    const bool outfitChanged = s_lastLocalOutfitSeen != 0xFF && localOutfit != s_lastLocalOutfitSeen;
    s_lastLocalOutfitSeen = localOutfit;
    if (outfitChanged || alink->mClothesChangeWaitTimer != 0) {
        if (s_swapHoldFrames == 0) {
            coop_log::info("coop_mod: [SWAP] armed by {} (clothesTimer={})",
                outfitChanged ? "clothes value change" : "mClothesChangeWaitTimer",
                alink->mClothesChangeWaitTimer);
            s_swapTimerSeen = false;
        }
        if (alink->mClothesChangeWaitTimer != 0) s_swapTimerSeen = true;
        s_swapHoldFrames = kSwapHoldFrames;
    }

    if (s_swapHoldFrames == 0) {
        for (int i = 0; i < kMaxPuppets; ++i) {
            if (i == coop_net_local_id()) continue;
            PuppetScope scope(static_cast<uint8_t>(i));
            puppet_swap_finished();
        }
        return false;
    }

    if (s_swapTimerSeen && s_swapHoldFrames > kSwapTailFrames) s_swapHoldFrames = kSwapTailFrames;
    --s_swapHoldFrames;

    for (int i = 0; i < kMaxPuppets; ++i) {
        if (i == coop_net_local_id()) continue;
        PuppetScope scope(static_cast<uint8_t>(i));
        puppet_swap_starting(alink);
    }

    puppet_flush_pending_frees();
    return true;
}

void puppet_swap_finished() {
    {
        if (pup().arcSwapHold) {
            pup().arcSwapHold = false;

            if (pup().respawnPending && pup().state == 0) {
                pup().pendingOutfit = pup().respawnOutfit;
                pup().state = 1;
                coop_log::info(
                    "coop_mod: [SWAP] clothes swap finished - respawning puppet outfit={}",
                    pup().respawnOutfit);
            }
            pup().respawnPending = false;
        }
    }
}

void puppet_swap_starting(daAlink_c* alink) {
    if (!pup().arcSwapHold) {
        pup().arcSwapHold = true;
        const u8 outfit = (pup().state == 1) ? pup().pendingOutfit : pup().outfit;
        const char* ourArc = outfit_files(outfit).arc;

        const bool sharesHeap = model_data_in_heap(alink->mpArcHeap, pup().model) ||
                                model_data_in_heap(alink->mpArcHeap, pup().faceModel) ||
                                model_data_in_heap(alink->mpArcHeap, pup().hatModel) ||
                                model_data_in_heap(alink->mpArcHeap, pup().handsModel);
        const bool sharesName = arc_names_match(ourArc, alink->mArcName);
        coop_log::info("coop_mod: [SWAP] clothes swap starting (timer={}) puppetState={} "
                        "puppetArc='{}' playerArc='{}' sharesHeap={} sharesName={}",
            alink->mClothesChangeWaitTimer, pup().state, ourArc,
            (alink->mArcName != nullptr) ? alink->mArcName : "(null)",
            static_cast<int>(sharesHeap), static_cast<int>(sharesName));

        if (pup().state != 0) {
            pup().respawnOutfit = outfit;
            pup().respawnPending = true;

            release_puppet();
            coop_log::info("coop_mod: [SWAP] released puppet {} + archive claim ahead of "
                            "mpArcHeap->freeAll()",
                static_cast<int>(s_pupId));
        }
    }
}

void puppet_hold_for_shield_swap(daAlink_c* alink) {
    for (int i = 0; i < kMaxPuppets; ++i) {
        if (i == coop_net_local_id()) continue;
        PuppetScope scope(static_cast<uint8_t>(i));
        puppet_shield_swap_one(alink);
    }
}

void puppet_shield_swap_one(daAlink_c* alink) {
    if (alink->mShieldChangeWaitTimer == 0) {
        pup().shieldSwapHold = false;
        return;
    }
    if (pup().shieldSwapHold) return;
    pup().shieldSwapHold = true;
    if (pup().shieldModel == nullptr && pup().shieldArc[0] == '\0') return;

    const bool sharesHeap = model_data_in_heap(alink->mpShieldArcHeap, pup().shieldModel);
    const bool sharesName = arc_names_match(pup().shieldArc, alink->mShieldArcName);
    coop_log::info("coop_mod: [SWAP] shield swap starting (timer={}) puppetShieldArc='{}' "
                    "playerShieldArc='{}' sharesHeap={} sharesName={}",
        alink->mShieldChangeWaitTimer, pup().shieldArc,
        (alink->mShieldArcName != nullptr) ? alink->mShieldArcName : "(null)",
        static_cast<int>(sharesHeap), static_cast<int>(sharesName));
    if (!sharesHeap && !sharesName) return;

    puppet_free_later(pup().shieldModel);
    if (pup().shieldArc[0] != '\0') {
        release_arc_share(pup().shieldArc);
        pup().shieldArc[0] = '\0';
    }
}

const int kPuppetMaxMaterials = 64;

struct MaterialGuard {
    J3DModelData* modelData = nullptr;
    u16 count = 0;
    J3DMaterialAnm* saved[kPuppetMaxMaterials];
};

bool material_guard_begin(J3DModel* model, MaterialGuard& guard) {
    J3DModelData* data = (model != nullptr) ? model->getModelData() : nullptr;
    if (data == nullptr) return false;
    const u16 num = data->getMaterialNum();
    guard.modelData = data;
    guard.count = num <= kPuppetMaxMaterials ? num : kPuppetMaxMaterials;
    for (u16 i = 0; i < guard.count; ++i) {
        J3DMaterial* material = data->getMaterialNodePointer(i);
        if (material == nullptr) {
            guard.saved[i] = nullptr;
            continue;
        }
        guard.saved[i] = material->getMaterialAnm();
        material->setMaterialAnm(nullptr);
    }
    return true;
}

void material_guard_end(MaterialGuard& guard) {
    if (guard.modelData == nullptr) return;
    for (u16 i = 0; i < guard.count; ++i) {
        J3DMaterial* material = guard.modelData->getMaterialNodePointer(i);
        if (material != nullptr) material->setMaterialAnm(guard.saved[i]);
    }
    guard.modelData = nullptr;
    guard.count = 0;
}

struct PuppetGuard {
    J3DModelData* modelData = nullptr;
    u16 jointNum = 0;
    int skipStart = -1;
    int skipEnd = -1;
    J3DJointCallBack savedCb[kPuppetMaxJoints];
    J3DMtxCalc* savedCalc[kPuppetMaxJoints];

    u16 materialNum = 0;
    J3DMaterialAnm* savedMatAnm[kPuppetMaxMaterials];
};

bool puppet_guard_begin(J3DModel* model, PuppetGuard& guard, int skipStart = -1, int skipEnd = -1) {
    J3DModelData* modelData = (model != nullptr) ? model->getModelData() : nullptr;
    if (modelData == nullptr) return false;
    const u16 jointNum = modelData->getJointNum();
    if (jointNum > kPuppetMaxJoints) return false;
    guard.modelData = modelData;
    guard.jointNum = jointNum;
    guard.skipStart = skipStart;
    guard.skipEnd = skipEnd;

    const u16 materialNum = modelData->getMaterialNum();
    guard.materialNum = materialNum <= kPuppetMaxMaterials ? materialNum : kPuppetMaxMaterials;
    for (u16 m = 0; m < guard.materialNum; ++m) {
        J3DMaterial* material = modelData->getMaterialNodePointer(m);
        if (material == nullptr) {
            guard.savedMatAnm[m] = nullptr;
            continue;
        }
        guard.savedMatAnm[m] = material->getMaterialAnm();
        material->setMaterialAnm(nullptr);
    }
    for (u16 j = 0; j < jointNum; ++j) {
        if (static_cast<int>(j) >= skipStart && static_cast<int>(j) <= skipEnd) continue;
        J3DJoint* joint = modelData->getJointNodePointer(j);
        guard.savedCb[j] = joint->getCallBack();
        guard.savedCalc[j] = joint->getMtxCalc();
        joint->setCallBack(nullptr);
        joint->setMtxCalc(nullptr);
    }
    return true;
}

void puppet_guard_end(PuppetGuard& guard) {
    if (guard.modelData == nullptr) return;
    for (u16 m = 0; m < guard.materialNum; ++m) {
        J3DMaterial* material = guard.modelData->getMaterialNodePointer(m);
        if (material != nullptr) material->setMaterialAnm(guard.savedMatAnm[m]);
    }
    guard.materialNum = 0;
    for (u16 j = 0; j < guard.jointNum; ++j) {
        if (static_cast<int>(j) >= guard.skipStart && static_cast<int>(j) <= guard.skipEnd) continue;
        J3DJoint* joint = guard.modelData->getJointNodePointer(j);
        joint->setCallBack(guard.savedCb[j]);
        joint->setMtxCalc(guard.savedCalc[j]);
    }
    guard.modelData = nullptr;
    guard.jointNum = 0;
}

void force_alpha_always_pass(J3DModelData* modelData) {
    if (modelData == nullptr) return;
    static const J3DAlphaCompInfo kAlwaysPass = {GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0};
    for (u16 i = 0; i < modelData->getMaterialNum(); ++i) {
        J3DMaterial* material = modelData->getMaterialNodePointer(i);
        J3DPEBlock* peBlock = (material != nullptr) ? material->getPEBlock() : nullptr;
        J3DAlphaComp* alphaComp = (peBlock != nullptr) ? peBlock->getAlphaComp() : nullptr;
        if (alphaComp != nullptr) {
            alphaComp->setAlphaCompInfo(kAlwaysPass);
        }
    }
}

void force_warp_off_all_materials(J3DModelData* modelData) {
    if (modelData == nullptr) return;
    for (u16 i = 0; i < modelData->getMaterialNum(); ++i) {
        J3DMaterial* material = modelData->getMaterialNodePointer(i);
        if (material == nullptr) continue;
        J3DTevBlock* tevBlock = material->getTevBlock();
        J3DTexGenBlock* texGenBlock = material->getTexGenBlock();
        if (tevBlock == nullptr || texGenBlock == nullptr) continue;
        const u8 tevStageNum = tevBlock->getTevStageNum();
        if (tevStageNum == 0) continue;
        J3DTevOrder* tevOrder = tevBlock->getTevOrder(tevStageNum - 1);
        if (tevOrder != nullptr && tevOrder->getTexMap() == 3) {

            const u32 gens = texGenBlock->getTexGenNum();
            if (gens == 0) continue;
            tevBlock->setTevStageNum(tevStageNum - 1);
            texGenBlock->setTexGenNum(gens - 1);
        }
    }
}

void force_diff_recognizes_stage_count(J3DModel* model) {
    if (model == nullptr) return;
    model->mDiffFlag |= J3DDiffFlag_KonstColor;
}

const int kHatJoint6 = 6;
const int kHatJoint7 = 7;

int puppet_hat_tail_callback(J3DJoint* joint, int param1) {
    if (param1 != 0 || joint == nullptr || pup().hatModel == nullptr || pup().model == nullptr) {
        return 1;
    }
    const int jointNo = joint->getJntNo();
    if (jointNo != kHatJoint6 && jointNo != kHatJoint7) return 1;

    if (jointNo == kHatJoint6) {

        mDoMtx_stack_c::copy(pup().model->getAnmMtx(2));
        cXyz up;
        mDoMtx_stack_c::multVecSR(&cXyz::BaseY, &up);
        const s16 hingeFloor = static_cast<s16>(up.atan2sY_XZ() - 0x3800);

        s16 targetPitch = cM_atan2s(-2.0f, 0.0f);
        if (targetPitch < hingeFloor) targetPitch = hingeFloor;

        cLib_addCalcAngleS2(&pup().hatPitch, targetPitch, 5, 0x400);
    }

    mDoMtx_stack_c::copy(J3DSys::mCurrentMtx);
    mDoMtx_stack_c::XYZrotM(0, 0, static_cast<s16>(pup().hatPitch >> 1));
    pup().hatModel->setAnmMtx(jointNo, mDoMtx_stack_c::get());
    cMtx_copy(mDoMtx_stack_c::get(), J3DSys::mCurrentMtx);
    return 1;
}

void puppet_world_axis_rot(MtxP mtx, s16 rotX, s16 rotY, s16 rotZ, s16 shapeAngleY,
    bool toCurrent = true, const cXyz* pivot = nullptr) {
    cXyz tmp;
    mDoMtx_multVecZero(mtx, &tmp);
    if (pivot != nullptr) {
        mDoMtx_stack_c::transS(*pivot);
    } else {
        mDoMtx_stack_c::transS(tmp);
    }
    mDoMtx_stack_c::YrotM(shapeAngleY);
    mDoMtx_stack_c::ZXYrotM(rotX, rotY, rotZ);
    mDoMtx_stack_c::YrotM(-shapeAngleY);
    mDoMtx_stack_c::transM(-tmp.x, -tmp.y, -tmp.z);
    mDoMtx_stack_c::concat(mtx);
    mDoMtx_copy(mDoMtx_stack_c::get(), mtx);

    if (toCurrent) {
        cMtx_copy(mDoMtx_stack_c::get(), J3DSys::mCurrentMtx);
    }
}

void puppet_apply_human_feet(J3DModel* model, s16 shapeAngleY) {
    static const u16 footJoint[2] = {0x12, 0x17};
    static const cXyz leg1Vec(30.0f, 0.0f, 0.0f);
    static const cXyz leg2Vec(39.363499f, 0.0f, 0.0f);
    static const cXyz footVec(14.18f, 0.0f, 0.0f);
    J3DModelData* data = model->getModelData();
    if (data == nullptr) return;

    for (int i = 0; i < 2; ++i) {
        u16 j = footJoint[i];
        if (j + 3 >= data->getJointNum()) continue;
        cXyz next;
        puppet_world_axis_rot(model->getAnmMtx(j), pup().footAngles[i][0], 0, 0, shapeAngleY,
            false, nullptr);
        mDoMtx_stack_c::multVec(&leg1Vec, &next);
        ++j;
        puppet_world_axis_rot(model->getAnmMtx(j), pup().footAngles[i][1], 0, 0, shapeAngleY,
            false, &next);
        mDoMtx_stack_c::multVec(&leg2Vec, &next);
        ++j;
        puppet_world_axis_rot(model->getAnmMtx(j), pup().footAngles[i][2], 0, 0, shapeAngleY,
            false, &next);
        mDoMtx_stack_c::multVec(&footVec, &next);
        ++j;
        puppet_world_axis_rot(model->getAnmMtx(j), pup().footAngles[i][2], 0, 0, shapeAngleY,
            false, &next);
    }
}

void puppet_apply_wolf_leg(J3DModel* model, u16 firstJoint, const s16 angles[3], s16 shapeAngleY,
    bool backLeg) {
    J3DModelData* data = model->getModelData();
    if (data == nullptr || firstJoint + 3 >= data->getJointNum()) return;

    cXyz jointPos[4];
    for (int j = 0; j < 4; ++j) {
        mDoMtx_multVecZero(model->getAnmMtx(firstJoint + j), &jointPos[j]);
    }

    const s16 seq[4] = {angles[0], backLeg ? angles[0] : angles[1], angles[1], angles[2]};

    u16 j = firstJoint;
    cXyz bone(0.0f, 0.0f, 0.0f);
    cXyz next;
    puppet_world_axis_rot(model->getAnmMtx(j), seq[0], 0, 0, shapeAngleY, false, nullptr);
    for (int k = 1; k < 4; ++k) {
        bone.x = jointPos[k - 1].abs(jointPos[k]);
        mDoMtx_stack_c::multVec(&bone, &next);
        ++j;
        puppet_world_axis_rot(model->getAnmMtx(j), seq[k], 0, 0, shapeAngleY, false, &next);
    }
}

void puppet_apply_wolf_feet(J3DModel* model, s16 shapeAngleY) {
    static const u16 frontJoint[2] = {16, 21};
    static const u16 backJoint[2] = {28, 33};
    for (int i = 0; i < 2; ++i) {
        puppet_apply_wolf_leg(model, frontJoint[i], pup().footAngles[i], shapeAngleY, false);
        puppet_apply_wolf_leg(model, backJoint[i], pup().footAngles[i + 2], shapeAngleY, true);
    }
}

const int kHumanFootCallbackJoint = 26;
const int kWolfFootCallbackJoint = 36;

u32 s_poseCbSeen = 0;
int s_poseDiagLogs = 0;

int puppet_body_aim_callback(J3DJoint* joint, int param1) {
    if (joint != nullptr) {
        const int seen = joint->getJntNo();
        if (seen >= 0 && seen < 32) s_poseCbSeen |= (1u << seen);
    }
    if (param1 != 0 || joint == nullptr || pup().model == nullptr) return 1;
    const int jointNo = joint->getJntNo();
    const bool wolf = outfit_files(pup().outfit).isWolf;

    if (jointNo == kUnderRootJoint) {

        const bool clearMode = (pup().rootClearMask & 7) != 0;
        const bool canoeMode = pup().rootClearMask == 0x60;
        if ((clearMode || canoeMode) && pup().oldFrame != nullptr) {
            J3DTransformInfo* info = pup().oldFrame->getOldFrameTransInfo(0);
            Quaternion* quat = pup().oldFrame->getOldFrameQuaternion(0);
            if (info != nullptr && quat != nullptr) {
                const J3DTransformInfo rootTrans = *info;
                Quaternion rootQuat = *quat;
                if (clearMode) {
                    if (pup().rootClearMask & 4) info->mTranslate.z = pup().rootClearZ;
                    if (pup().rootClearMask & 1) info->mTranslate.x = pup().rootClearX;
                    if (pup().rootClearMask & 2) info->mTranslate.y = pup().rootClearY;
                }

                J3DTransformInfo applied = *info;
                if (canoeMode) {
                    applied.mTranslate.x -= pup().rootClearX;
                    applied.mTranslate.y -= pup().rootClearY;
                    applied.mTranslate.z -= pup().rootClearZ;
                }

                MtxP anm = pup().model->getAnmMtx(0);
                mDoMtx_stack_c::transS(
                    rootTrans.mTranslate.x, rootTrans.mTranslate.y, rootTrans.mTranslate.z);
                mDoMtx_stack_c::quatM(&rootQuat);
                mDoMtx_stack_c::inverse();
                cMtx_concat(anm, mDoMtx_stack_c::get(), J3DSys::mCurrentMtx);

                MTXQuat(anm, &rootQuat);
                anm[0][3] = applied.mTranslate.x;
                anm[1][3] = applied.mTranslate.y;
                anm[2][3] = applied.mTranslate.z;

                cMtx_concat(J3DSys::mCurrentMtx, anm, J3DSys::mCurrentMtx);
                cMtx_copy(J3DSys::mCurrentMtx, anm);
            }
        }
        return 1;
    }

    if (jointNo == (wolf ? kWolfFootCallbackJoint : kHumanFootCallbackJoint)) {
        if (wolf) {
            puppet_apply_wolf_feet(pup().model, pup().angle.y);
        } else {
            puppet_apply_human_feet(pup().model, pup().angle.y);
        }
        return 1;
    }

    if (wolf) return 1;

    if (jointNo == kUpperBodyRootJoint) {
        if (pup().bodyRotX != 0 || pup().bodyRotY != 0 || pup().bodyRotZ != 0) {
            puppet_world_axis_rot(pup().model->getAnmMtx(kUpperBodyRootJoint), pup().bodyRotX,
                pup().bodyRotY, pup().bodyRotZ, pup().angle.y);
        }
    } else if (jointNo == kBackbone2Joint) {
        if (pup().bodyRotY != 0) {
            puppet_world_axis_rot(pup().model->getAnmMtx(kBackbone2Joint), 0, pup().bodyRotY, 0,
                pup().angle.y);
        }
    }
    return 1;
}

void install_hat_tail_sway(J3DModel* hatModel) {
    if (hatModel == nullptr) return;
    J3DModelData* modelData = hatModel->getModelData();
    if (modelData == nullptr) return;

    pup().hatPitch = 0;
}

void set_hat_tail_callbacks(J3DModel* hatModel) {
    J3DModelData* modelData = hatModel != nullptr ? hatModel->getModelData() : nullptr;
    if (modelData == nullptr) return;
    const u16 jointNum = modelData->getJointNum();
    for (int j = kHatJoint6; j <= kHatJoint7 && j < jointNum; ++j) {
        J3DJoint* joint = modelData->getJointNodePointer(j);
        if (joint != nullptr) joint->setCallBack(puppet_hat_tail_callback);
    }
}

void log_warp_state(const char* label, J3DModelData* modelData) {
    if (modelData == nullptr) {
        coop_log::trace("coop_mod: [DIAG-WARP] {}: modelData is null", label);
        return;
    }
    const u16 matNum = modelData->getMaterialNum();
    coop_log::trace("coop_mod: [DIAG-WARP] {}: {} materials, isLocked={} hasSharedDL={}", label,
        matNum, static_cast<int>(modelData->isLocked()),
        static_cast<int>(modelData->getMaterialNodePointer(0) != nullptr &&
                          modelData->getMaterialNodePointer(0)->getSharedDisplayListObj() != nullptr));
    for (u16 i = 0; i < matNum; ++i) {
        J3DMaterial* material = modelData->getMaterialNodePointer(i);
        if (material == nullptr) {
            coop_log::trace("coop_mod: [DIAG-WARP]   mat[{}]: null", i);
            continue;
        }
        J3DTevBlock* tevBlock = material->getTevBlock();
        J3DTexGenBlock* texGenBlock = material->getTexGenBlock();
        if (tevBlock == nullptr || texGenBlock == nullptr) {
            coop_log::trace("coop_mod: [DIAG-WARP]   mat[{}]: null tev/texgen block", i);
            continue;
        }
        const u8 tevStageNum = tevBlock->getTevStageNum();
        const u32 texGenNum = texGenBlock->getTexGenNum();
        int lastTexMap = -1;
        if (tevStageNum > 0) {
            J3DTevOrder* tevOrder = tevBlock->getTevOrder(tevStageNum - 1);
            lastTexMap = (tevOrder != nullptr) ? tevOrder->getTexMap() : -2;
        }
        J3DPEBlock* peBlock = material->getPEBlock();
        J3DAlphaComp* alphaComp = (peBlock != nullptr) ? peBlock->getAlphaComp() : nullptr;
        coop_log::info(
            "coop_mod: [DIAG-WARP]   mat[{}]: tevStageNum={} texGenNum={} lastTexMap={} "
            "hasAlphaComp={}",
            i, tevStageNum, texGenNum, lastTexMap, static_cast<int>(alphaComp != nullptr));
    }
}

u8* read_alanm_resource(u16 resIdx, u32 minSize, u32* o_bufSize) {

    const u32 rawSize = [&]() -> u32 {
        JKRArchive* archive = dComIfGp_getAnmArchive();
        if (archive == nullptr) return minSize;
        const u32 size = archive->getFileSize(archive->findIdxResource(resIdx));
        return size > minSize ? size : minSize;
    }();
    const u32 kMaxAnmBufSize = 0x200000u;

    ensure_system_heap_capacity();
    u32 bufSize = ((rawSize + 0x1Fu) & ~0x1Fu) + 0x20u;
    u8* buf = nullptr;
    u32 readSize = 0;
    bool guardOkAfterRead = false;
    while (true) {
        buf = static_cast<u8*>(JKRAllocFromSysHeap(bufSize + kGuardSize, 0x20));
        if (buf == nullptr) {
            coop_log::info("coop_mod: [DIAG] anim buffer alloc failed resIdx={} size={}", resIdx,
                bufSize);
            return nullptr;
        }
        memset(buf + bufSize, kGuardByte, kGuardSize);
        readSize = JKRReadIdxResource(buf, bufSize, resIdx, dComIfGp_getAnmArchive());
        guardOkAfterRead = true;
        for (u32 i = 0; i < kGuardSize; ++i) {
            if (buf[bufSize + i] != kGuardByte) { guardOkAfterRead = false; break; }
        }
        if (readSize < bufSize && guardOkAfterRead) break;

        JKRFreeToSysHeap(buf);
        buf = nullptr;
        if (bufSize >= kMaxAnmBufSize) {
            coop_log::warn(
                "coop_mod: [DIAG-OVF] resIdx={} still not fitting at bufSize={} (readSize={}, "
                "guardOk={}) - giving up on this resource",
                resIdx, bufSize, readSize, guardOkAfterRead ? 1 : 0);
            return nullptr;
        }
        const u32 grown = bufSize * 2u;
        coop_log::info("coop_mod: [DIAG-OVF] resIdx={} truncated at bufSize={} (readSize={}), "
            "retrying at {}", resIdx, bufSize, readSize, grown);
        bufSize = grown;
    }
    *o_bufSize = bufSize;
    return buf;
}

bool alanm_guard_intact(const u8* buf, u32 bufSize) {
    for (u32 i = 0; i < kGuardSize; ++i) {
        if (buf[bufSize + i] != kGuardByte) return false;
    }
    return true;
}

mDoExt_bckAnm* get_or_load_puppet_anim(daAlink_c* alink, u16 resIdx) {
    int lruIdx = -1;
    for (int i = 0; i < kAnimCacheSize; ++i) {
        if (s_puppetAnimCache[i].resIdx == resIdx && s_puppetAnimCache[i].bck != nullptr) {
            s_puppetAnimCache[i].lastUsed = ++s_puppetAnimCacheClock;
            s_puppetAnimCache[i].usedFrame = s_puppetFrame;
            return s_puppetAnimCache[i].bck;
        }

        if (s_puppetAnimCache[i].bck != nullptr && s_puppetAnimCache[i].usedFrame == s_puppetFrame) {
            continue;
        }

        if (lruIdx < 0) {
            lruIdx = i;
        } else if (s_puppetAnimCache[lruIdx].bck != nullptr &&
                   (s_puppetAnimCache[i].bck == nullptr ||
                       s_puppetAnimCache[i].lastUsed < s_puppetAnimCache[lruIdx].lastUsed)) {
            lruIdx = i;
        }
    }
    if (alink == nullptr) return nullptr;
    if (lruIdx < 0) {

        coop_log::warn("coop_mod: [ANIM] cache full this frame - resIdx={} not loaded", resIdx);
        return nullptr;
    }

    u32 bufSize = 0;
    u8* buf = read_alanm_resource(resIdx, kPuppetBckBufferSize, &bufSize);
    if (buf == nullptr) return nullptr;
    auto guard_intact = [&]() { return alanm_guard_intact(buf, bufSize); };

    J3DAnmTransform* anm = static_cast<J3DAnmTransform*>(J3DAnmLoaderDataBase::load(buf));

    if (!guard_intact()) {
        coop_log::warn("coop_mod: [DIAG-OVF] resIdx={} load() overran a complete buffer "
            "(bufSize={}) - DROPPING", resIdx, bufSize);
        JKRFreeToSysHeap(buf);
        return nullptr;
    }

    mDoExt_bckAnm* bck = (anm != nullptr) ? JKR_NEW mDoExt_bckAnm() : nullptr;
    const bool initOk = bck != nullptr && bck->init(anm, TRUE, 2, 1.0f, 0, -1, false);
    if (!initOk) {
        if (bck != nullptr) {
            JKR_DELETE(bck);
        }
        JKRFreeToSysHeap(buf);
        coop_log::info("coop_mod: [DIAG] failed to load/init anim resIdx={}", resIdx);
        return nullptr;
    }

    const uintptr_t vtbl = *reinterpret_cast<const uintptr_t*>(anm);
    coop_log::trace("coop_mod: [DIAG-ANM] resIdx={} anm={:#x} vtbl0={:#x}", resIdx,
        reinterpret_cast<uintptr_t>(anm), vtbl);
    if (!vtbl_looks_like_code(vtbl)) {

        coop_log::warn("coop_mod: [DIAG-DEAD] resIdx={} loaded a bad anim (vtbl0={:#x}) - dropping",
            resIdx, vtbl);
        JKR_DELETE(bck);
        JKRFreeToSysHeap(buf);
        return nullptr;
    }
    if (s_liveAnmVtbl == 0) {
        s_liveAnmVtbl = vtbl;
    }

    if (s_puppetAnimCache[lruIdx].bck != nullptr) {
        JKR_DELETE(s_puppetAnimCache[lruIdx].bck);
    }
    if (s_puppetAnimCache[lruIdx].buf != nullptr) {
        JKRFreeToSysHeap(s_puppetAnimCache[lruIdx].buf);
    }
    s_puppetAnimCache[lruIdx].buf = buf;
    s_puppetAnimCache[lruIdx].resIdx = resIdx;
    s_puppetAnimCache[lruIdx].bck = bck;
    s_puppetAnimCache[lruIdx].lastUsed = ++s_puppetAnimCacheClock;
    s_puppetAnimCache[lruIdx].usedFrame = s_puppetFrame;
    return bck;
}

const int kMaxEquipShapes = 32;

struct ShapeVisGuard {
    J3DModelData* modelData = nullptr;
    u16 num = 0;

    bool wasHidden[kMaxEquipShapes];
};

bool shape_vis_force_show(J3DModel* model, ShapeVisGuard& guard);
void shape_vis_restore(ShapeVisGuard& guard);
void prep_equipment_model(J3DModel* model);

void drop_slot_models_of_kind(u8 kind);

enum ItemSource {
    kItemSrcAlAnm = 0,
    kItemSrcOutfit = 1,
    kItemSrcAlink = 2,
    kItemSrcOutfitIdx = 3,
    kItemSrcWireArc = 4,

    kItemSrcFieldItem = 5,

    kItemSrcRideArc = 6,
};

struct HeldItemRes {
    u16 bmdResIdx;
    u32 bmdBufSize;
    const char* outfitFile;
    u8 source;
};

const u16 kBoomerangAlinkResIdx = 0x1F;

const HeldItemRes kHeldItemRes[kPuppetHeldCount] = {
      {0xFFFF, 0, nullptr, kItemSrcAlAnm},
      {dRes_INDEX_ALANM_BMD_AL_BOW_e, 0x4C00, nullptr, kItemSrcAlAnm},
      {dRes_INDEX_ALANM_BMD_AL_PACHI_e, 0x2C00, nullptr, kItemSrcAlAnm},
      {dRes_INDEX_ALANM_BMD_AL_HS_e, 0x5C00, nullptr, kItemSrcAlAnm},
      {dRes_INDEX_ALANM_BMD_AL_IB_e, 0x2800, nullptr, kItemSrcAlAnm},
      {dRes_INDEX_ALANM_BMD_AL_CROD_e, 0x5400, nullptr, kItemSrcAlAnm},
      {dRes_INDEX_ALANM_BMD_AL_BOTTLE_e, 0x5C00, nullptr, kItemSrcAlAnm},
      {dRes_INDEX_ALANM_BMD_AL_HS_TIP_e, 0x3800, nullptr, kItemSrcAlAnm},
      {0xFFFF, 0, "al_kantera.bmd", kItemSrcOutfit},
      {kBoomerangAlinkResIdx, 0, nullptr, kItemSrcAlink},
      {dRes_INDEX_WMDL_BMD_WL_KUSARI_e, 0, nullptr, kItemSrcOutfitIdx},
      {dRes_INDEX_ALANM_BMD_AL_WF_e, 0x6000, nullptr, kItemSrcAlAnm},
      {0x21, 0, nullptr, kItemSrcAlink},
      {4, 0, nullptr, kItemSrcWireArc},
      {0xFFFF, 0, "ef_ktGlow.bmd", kItemSrcOutfit},
      {0x2D, 0, nullptr, kItemSrcAlink},
      {0x2E, 0, nullptr, kItemSrcAlink},
      {44, 0, nullptr, kItemSrcAlink},
      {43, 0, nullptr, kItemSrcAlink},
      {0x1C, 0, nullptr, kItemSrcAlink},
      {0x1D, 0, nullptr, kItemSrcAlink},
      {0x20, 0, nullptr, kItemSrcAlink},
      {0x34, 0, nullptr, kItemSrcAlink},
      {0x19, 0, nullptr, kItemSrcAlink},
      {0xFFFF, 0, nullptr, kItemSrcFieldItem},

      {0x1E, 0, nullptr, kItemSrcAlink},
      {0xFFFF, 0, nullptr, kItemSrcRideArc},
      {0xFFFF, 0, nullptr, kItemSrcRideArc},
};

void retire_get_item_arc() {
    if (pup().getItemArc[0] == '\0') return;
    if (pup().getItemArcOld[0] != '\0' &&
        std::strcmp(pup().getItemArcOld, pup().getItemArc) != 0) {
        unloadObjectArchive(pup().getItemArcOld);
    }
    std::memcpy(pup().getItemArcOld, pup().getItemArc, sizeof(pup().getItemArcOld));
    pup().getItemArcOldTimer = 60;
    pup().getItemArc[0] = '\0';
}

J3DModelData* get_field_item_data(u8 itemNo) {
    PuppetItemData& slot = pup().itemData[kPuppetHeldGetItem];
    if (slot.data != nullptr && pup().getItemNoCached == itemNo) return slot.data;
    if (slot.data != nullptr) {
        slot.data = nullptr;
        drop_slot_models_of_kind(kPuppetHeldGetItem);
    }
    pup().getItemNoCached = itemNo;

    const char* arc = dItem_data::getArcName(itemNo);
    const s16 bmd = dItem_data::getBmdName(itemNo);
    if (arc == nullptr || arc[0] == '\0' || bmd < 0) return nullptr;

    if (std::strcmp(pup().getItemArc, arc) != 0) {
        if (std::strcmp(pup().getItemArcOld, arc) == 0) {
            pup().getItemArcOld[0] = '\0';
        } else {
            retire_get_item_arc();
        }
        std::strncpy(pup().getItemArc, arc, sizeof(pup().getItemArc) - 1);
        pup().getItemArc[sizeof(pup().getItemArc) - 1] = '\0';
    }
    if (loadObjectArchive(arc) != 0) return nullptr;
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(arc, bmd));
    if (data == nullptr) return nullptr;
    slot.data = data;
    slot.fromOutfit = false;
    slot.shared = true;
    coop_log::trace("coop_mod: [DIAG-ITEM] get-item model for item {:#x} from '{}' idx={}",
        static_cast<int>(itemNo), arc, static_cast<int>(bmd));
    return slot.data;
}

J3DModel* puppet_skin_equipment(const char* file, const cXyz& scale);

J3DModelData* get_or_load_item_data(u8 kind, u16 wireIdx) {
    if (kind == kPuppetHeldNone || kind >= kPuppetHeldCount) return nullptr;
    if (kind == kPuppetHeldGetItem) return get_field_item_data(static_cast<u8>(wireIdx));
    PuppetItemData& slot = pup().itemData[kind];
    if (slot.data != nullptr && (kind == kPuppetHeldRide || kind == kPuppetHeldRideExtra)) {
        const int which = kind == kPuppetHeldRide ? 0 : 1;
        if (pup().rideIdxBuilt[which] != wireIdx ||
            std::strcmp(pup().rideArcBuilt[which], pup().rideArc) != 0) {
            slot.data = nullptr;
            drop_slot_models_of_kind(kind);
        }
    }
    if (slot.data != nullptr) return slot.data;

    const HeldItemRes& res = kHeldItemRes[kind];

    if (res.source == kItemSrcRideArc) {

        const int which = kind == kPuppetHeldRide ? 0 : 1;
        if (pup().rideArc[0] == '\0' || wireIdx == 0xFFFF) return nullptr;
        if (loadObjectArchive(pup().rideArc) != 0) return nullptr;
        auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(pup().rideArc, wireIdx));
        if (data == nullptr) return nullptr;
        slot.data = data;
        slot.shared = true;
        std::memcpy(pup().rideArcBuilt[which], pup().rideArc, sizeof(pup().rideArc));
        pup().rideIdxBuilt[which] = wireIdx;
        coop_log::trace("coop_mod: [DIAG-ITEM] loaded kind={} from ride archive '{}' idx={}", kind,
            pup().rideArc, wireIdx);
        return slot.data;
    }

    if (res.source == kItemSrcWireArc) {
        if (pup().rodArc[0] == '\0') return nullptr;
        if (loadObjectArchive(pup().rodArc) != 0) return nullptr;
        J3DModel* model = loadBmdFromArcIdx(pup().rodArc, res.bmdResIdx, cXyz(1.0f, 1.0f, 1.0f));
        if (model == nullptr) return nullptr;
        slot.data = model->getModelData();
        slot.shared = true;
        prep_equipment_model(model);
        JKR_DELETE(model);
        coop_log::trace("coop_mod: [DIAG-ITEM] loaded kind={} from wire archive '{}' idx={}", kind,
            pup().rodArc, res.bmdResIdx);
        return slot.data;
    }

    if (res.source == kItemSrcOutfitIdx) {
        if (pup().state != 2) return nullptr;
        J3DModel* model =
            loadBmdFromArcIdx(outfit_files(pup().outfit).arc, res.bmdResIdx, cXyz(1.0f, 1.0f, 1.0f));
        if (model == nullptr) return nullptr;
        slot.data = model->getModelData();
        slot.fromOutfit = true;
        slot.shared = true;
        prep_equipment_model(model);
        JKR_DELETE(model);
        coop_log::trace("coop_mod: [DIAG-ITEM] loaded kind={} from outfit archive idx={}", kind,
            res.bmdResIdx);
        return slot.data;
    }

    if (res.source == kItemSrcAlink) {

        J3DModel* model = loadBmdFromArcIdx("Alink", res.bmdResIdx, cXyz(1.0f, 1.0f, 1.0f));
        if (model == nullptr) return nullptr;
        slot.data = model->getModelData();
        slot.fromOutfit = false;
        slot.shared = true;
        prep_equipment_model(model);
        JKR_DELETE(model);
        coop_log::trace("coop_mod: [DIAG-ITEM] loaded kind={} from Alink idx={}", kind,
            res.bmdResIdx);
        return slot.data;
    }

    if (res.outfitFile != nullptr) {

        if (pup().state != 2) return nullptr;
        J3DModel* model = loadBmdFromArc(outfit_files(pup().outfit).arc, res.outfitFile,
            cXyz(1.0f, 1.0f, 1.0f));
        if (model == nullptr) return nullptr;
        slot.data = model->getModelData();
        slot.fromOutfit = true;
        slot.shared = true;
        prep_equipment_model(model);
        JKR_DELETE(model);
        coop_log::trace("coop_mod: [DIAG-ITEM] loaded kind={} from outfit archive '{}'", kind,
            res.outfitFile);
        return slot.data;
    }

    const char* skinFile = skins_aram_file_for_index(res.bmdResIdx);
    if (skinFile != nullptr) {
        J3DModel* skinModel = puppet_skin_equipment(skinFile, cXyz(1.0f, 1.0f, 1.0f));
        if (skinModel != nullptr) {
            slot.data = skinModel->getModelData();
            slot.fromOutfit = false;
            slot.shared = true;
            prep_equipment_model(skinModel);
            JKR_DELETE(skinModel);
            coop_log::info("coop_mod: [SKIN] held item kind={} is their model's '{}'", kind,
                skinFile);
            return slot.data;
        }
    }

    JKRArchive* anmArchive = dComIfGp_getAnmArchive();
    if (anmArchive == nullptr) return nullptr;

    u32 bufSize = 0;
    u8* buf = read_alanm_resource(res.bmdResIdx, res.bmdBufSize, &bufSize);
    if (buf == nullptr) return nullptr;

    u32 type = 'BMWR';
    JKRArchive::SDIDirEntry* dir = anmArchive->mNodes;
    for (int i = 0; i < anmArchive->countDirectory(); ++i, ++dir) {
        if (res.bmdResIdx >= dir->first_file_index &&
            res.bmdResIdx < dir->first_file_index + dir->num_entries) {
            type = dir->type;
            break;
        }
    }

    J3DModelData* modelData = static_cast<J3DModelData*>(dRes_info_c::loaderBasicBmd(type, buf));

    if (!alanm_guard_intact(buf, bufSize)) {
        coop_log::warn("coop_mod: [DIAG-OVF] item kind={} bmdResIdx={} loaderBasicBmd overran a "
                        "complete buffer (bufSize={}) - DROPPING", kind, res.bmdResIdx, bufSize);
        JKRFreeToSysHeap(buf);
        return nullptr;
    }
    if (modelData == nullptr || modelData->getMaterialNum() == 0) {
        coop_log::trace("coop_mod: [DIAG-ITEM] kind={} bmdResIdx={} produced no usable model data",
            kind, res.bmdResIdx);
        JKRFreeToSysHeap(buf);
        return nullptr;
    }

    dRes_info_c::offWarpMaterial(modelData);

    slot.data = modelData;
    slot.buf = buf;
    coop_log::trace("coop_mod: [DIAG-ITEM] loaded kind={} bmdResIdx={} data={:p} mats={}", kind,
        res.bmdResIdx, static_cast<void*>(modelData), modelData->getMaterialNum());
    return modelData;
}

void release_outfit_item_data() {

    if (pup().swordId == kPuppetSwordWood) {
        puppet_free_later(pup().swordModel);
        pup().swordId = kPuppetSwordNone;
    }
    for (int i = 0; i < kPuppetHeldCount; ++i) {
        if (!pup().itemData[i].fromOutfit) continue;
        pup().itemData[i].data = nullptr;
        pup().itemData[i].fromOutfit = false;
        drop_slot_models_of_kind(static_cast<u8>(i));
    }
}

void drop_slot_models_of_kind(u8 kind) {
    for (int k = 0; k < kPuppetAttachSlots; ++k) {
        if (pup().attachSlots[k].kind != kind) continue;
        puppet_free_later(pup().attachSlots[k].model);
        pup().attachSlots[k].kind = kPuppetHeldNone;
        pup().attachSlots[k].wireIdx = 0xFFFF;
    }
}

J3DModel* get_slot_model(int slotIdx, u8 kind, u16 wireIdx) {
    PuppetAttachSlot& slot = pup().attachSlots[slotIdx];

    const bool itemChanged = (kind == kPuppetHeldGetItem || kind == kPuppetHeldRide ||
                                 kind == kPuppetHeldRideExtra) &&
                             slot.wireIdx != wireIdx;
    if (slot.kind == kind && slot.model != nullptr && !itemChanged) return slot.model;
    slot.wireIdx = wireIdx;
    if (slot.model != nullptr) {

        puppet_free_later(slot.model);
        slot.model = nullptr;
    }
    slot.kind = kPuppetHeldNone;

    J3DModelData* data = get_or_load_item_data(kind, wireIdx);
    if (data == nullptr) return nullptr;
    J3DModel* model = mDoExt_J3DModel__create(data, 0x80000, 0x11000284);
    if (model == nullptr) return nullptr;
    force_diff_recognizes_stage_count(model);
    slot.kind = kind;
    slot.model = model;
    return model;
}

const f32 kChainStep = 5.0f;

const f32 kIronBallLinkScale = 2.0f;

J3DModelData* s_chainLinkData = nullptr;
u8* s_chainLinkBuf = nullptr;

bool ensure_chain_link_data() {
    if (s_chainLinkData != nullptr) return true;
    JKRArchive* anmArchive = dComIfGp_getAnmArchive();
    if (anmArchive == nullptr) return false;

    u32 bufSize = 0;

    u8* buf = read_alanm_resource(dRes_INDEX_ALANM_BMD_AL_HS_KUSARI_e, 0x1000, &bufSize);
    if (buf == nullptr) return false;

    u32 type = 'BMWR';
    JKRArchive::SDIDirEntry* dir = anmArchive->mNodes;
    for (int i = 0; i < anmArchive->countDirectory(); ++i, ++dir) {
        if (dRes_INDEX_ALANM_BMD_AL_HS_KUSARI_e >= dir->first_file_index &&
            dRes_INDEX_ALANM_BMD_AL_HS_KUSARI_e < dir->first_file_index + dir->num_entries) {
            type = dir->type;
            break;
        }
    }

    J3DModelData* data = static_cast<J3DModelData*>(dRes_info_c::loaderBasicBmd(type, buf));
    if (!alanm_guard_intact(buf, bufSize) || data == nullptr || data->getMaterialNum() == 0) {
        coop_log::warn("coop_mod: [DIAG-CHAIN] chain link resource unusable - chains disabled");
        JKRFreeToSysHeap(buf);
        return false;
    }
    dRes_info_c::offWarpMaterial(data);
    s_chainLinkData = data;
    s_chainLinkBuf = buf;
    coop_log::info("coop_mod: [DIAG-CHAIN] chain link loaded data={:p} mats={}",
        static_cast<void*>(data), data->getMaterialNum());
    return true;
}

bool draw_chain_link(MtxP mtx, f32 scale) {
    if (pup().chainLinksUsed >= kChainLinkPool) return false;
    J3DModel*& model = pup().chainLinks[pup().chainLinksUsed];
    if (model == nullptr) {
        model = mDoExt_J3DModel__create(s_chainLinkData, 0x80000, 0x11000284);
        if (model == nullptr) return false;
        force_diff_recognizes_stage_count(model);
    }
    ++pup().chainLinksUsed;
    model->setBaseScale(cXyz(scale, scale, scale));
    renderModelAtMtx(model, mtx, nullptr);
    return true;
}

void chain_sample(const f32 pts[][3], int count, f32 t, cXyz* out) {
    if (count <= 1) {
        out->set(pts[0][0], pts[0][1], pts[0][2]);
        return;
    }
    f32 scaled = t * (f32)(count - 1);
    int i = (int)scaled;
    if (i >= count - 1) i = count - 2;
    const f32 u = scaled - (f32)i;

    const int i0 = (i > 0) ? i - 1 : 0;
    const int i1 = i;
    const int i2 = i + 1;
    const int i3 = (i + 2 < count) ? i + 2 : count - 1;

    const f32 u2 = u * u;
    const f32 u3 = u2 * u;
    for (int c = 0; c < 3; ++c) {
        const f32 p0 = pts[i0][c], p1 = pts[i1][c], p2 = pts[i2][c], p3 = pts[i3][c];
        const f32 v = 0.5f * ((2.0f * p1) + (-p0 + p2) * u +
                              (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2 +
                              (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3);
        ((f32*)&out->x)[c] = v;
    }
}

u16 rod_segment_res(bool uki, int i) {
    if (uki) {
        if (i == 0) return 40;
        return (i == 3 || i == 6 || i == 9 || i >= 12) ? 42 : 41;
    }
    return ((i & 1) || i == 2) ? 41 : 42;
}

void release_rod_segments() {
    for (int i = 0; i < kRodSegments; ++i) puppet_free_later(pup().rodSegModels[i]);
    pup().rodSegKind = kPuppetChainNone;
}

void draw_puppet_rod(const cXyz* pts, int count, bool uki) {
    if (count < kRodSegments + 1) return;
    const u8 kind = uki ? kPuppetChainRodUki : kPuppetChainRodLure;
    if (pup().rodSegKind != kind) {
        release_rod_segments();
        for (int i = 0; i < kRodSegments; ++i) {
            J3DModel* model =
                loadBmdFromArcIdx("Alink", rod_segment_res(uki, i), cXyz(1.0f, 1.0f, 1.0f));
            if (model == nullptr) {
                coop_log::warn("coop_mod: [DIAG-ROD] segment {} (Alink idx {}) failed to load", i,
                    rod_segment_res(uki, i));
                release_rod_segments();
                return;
            }
            prep_equipment_model(model);
            pup().rodSegModels[i] = model;
        }
        pup().rodSegKind = kind;
        coop_log::info("coop_mod: [DIAG-ROD] built {} shaft segments for the {} rod", kRodSegments,
            uki ? "uki" : "lure");
    }

    static const u8 kUkiWidth[kRodSegments] = {15, 15, 15, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 2};
    static const u8 kLureWidth[kRodSegments] = {10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3};
    for (int i = 0; i < kRodSegments; ++i) {
        const cXyz seg = pts[i + 1] - pts[i];
        const f32 horiz = std::sqrt(seg.x * seg.x + seg.z * seg.z);
        mDoMtx_stack_c::transS(pts[i].x, pts[i].y, pts[i].z);
        mDoMtx_stack_c::YrotM(cM_atan2s(seg.x, seg.z));
        mDoMtx_stack_c::XrotM(static_cast<s16>(-cM_atan2s(seg.y, horiz)));
        const f32 width = (uki ? kUkiWidth[i] : kLureWidth[i]) * 0.05166f;
        mDoMtx_stack_c::scaleM(width, width, 0.073f * seg.abs());
        if (uki && i == 0) {
            mDoMtx_stack_c::scaleM(1.0f, 1.0f, 0.5f);
            mDoMtx_stack_c::transM(0.0f, 0.0f, -5.5f);
        }
        Mtx segMtx;
        mDoMtx_copy(mDoMtx_stack_c::get(), segMtx);

        J3DModel* model = pup().rodSegModels[i];
        PuppetGuard guard;
        const bool guarded = puppet_guard_begin(model, guard);
        ShapeVisGuard vis;
        const bool visGuarded = shape_vis_force_show(model, vis);
        renderModelAtMtx(model, segMtx, nullptr);
        if (visGuarded) shape_vis_restore(vis);
        if (guarded) puppet_guard_end(guard);
    }
}

void draw_puppet_chain() {
    if (pup().chainKind == kPuppetChainNone || pup().model == nullptr) return;
    if (pup().chainCount == 0) return;

    Mtx root;
    mDoMtx_copy(pup().model->getBaseTRMtx(), root);
    cXyz pts[kPuppetChainPts];
    const int count = (pup().chainCount <= kPuppetChainPts) ? pup().chainCount : kPuppetChainPts;
    for (int i = 0; i < count; ++i) {
        cXyz local(pup().chainPts[i][0], pup().chainPts[i][1], pup().chainPts[i][2]);
        mDoMtx_multVec(root, &local, &pts[i]);
    }

    if (pup().chainKind == kPuppetChainRodUki || pup().chainKind == kPuppetChainRodLure) {
        draw_puppet_rod(pts, count, pup().chainKind == kPuppetChainRodUki);
        return;
    }
    if (!ensure_chain_link_data()) return;

    if (pup().chainKind == kPuppetChainHookshot) {

        const cXyz& top = pts[0];
        const cXyz& rootPos = pts[1];
        cXyz dir = rootPos - top;
        f32 len = dir.abs();
        if (len > 1.0f) {
            dir *= (1.0f / len);
            csXyz base(dir.atan2sY_XZ(), dir.atan2sX_Z(), 0);
            csXyz cur = base;
            cXyz pos = top;
            const f32 phase = M_PI / len;
            f32 travelled = 0.0f;
            f32 prev = 0.0f;
            f32 amp = 2.5f * (f32)pup().chainStopTime;
            if (pup().chainStopTime & 1) amp = -amp;

            while (travelled < len) {
                const f32 bend = amp * cM_fsin(phase * travelled);
                const s16 bendAngle = cM_atan2s(bend - prev, kChainStep);
                cur.x = static_cast<s16>(base.x + bendAngle);

                mDoMtx_stack_c::transS(pos);
                mDoMtx_stack_c::ZXYrotM(cur);
                static const Vec kStepVec = {0.0f, 0.0f, kChainStep};
                Mtx linkMtx;
                mDoMtx_copy(mDoMtx_stack_c::get(), linkMtx);
                mDoMtx_stack_c::multVec(&kStepVec, &pos);

                if (!draw_chain_link(linkMtx, 1.0f)) break;

                cur.z = static_cast<s16>(cur.z + 0x3000);
                prev = bend;
                travelled += fabsf(cM_scos(bendAngle)) * kChainStep;
            }
        }

        if (count >= 4) {
            const cXyz& subTop = pts[2];
            const cXyz& subRoot = pts[3];
            cXyz subDir = subRoot - subTop;
            f32 subLen = subDir.abs();
            if (subLen > 1.0f) {
                subDir *= (1.0f / subLen);
                csXyz ang(subDir.atan2sY_XZ(), subDir.atan2sX_Z(), 0);
                cXyz pos = subTop;
                for (f32 travelled = 0.0f; travelled < subLen; travelled += kChainStep) {
                    mDoMtx_stack_c::transS(pos);
                    mDoMtx_stack_c::ZXYrotM(ang);
                    Mtx linkMtx;
                    mDoMtx_copy(mDoMtx_stack_c::get(), linkMtx);
                    if (!draw_chain_link(linkMtx, 1.0f)) break;
                    pos += subDir * kChainStep;
                    ang.z = static_cast<s16>(ang.z + 0x3000);
                }
            }
        }
        return;
    }

    f32 length = 0.0f;
    for (int i = 1; i < count; ++i) {
        length += (pts[i] - pts[i - 1]).abs();
    }
    if (length < 1.0f) return;

    int links = (int)(length / kChainStep);
    if (links < 2) links = 2;
    if (links > kChainLinkPool) links = kChainLinkPool;

    csXyz roll(0, 0, 0);
    cXyz prevPos;
    chain_sample(pup().chainPts, count, 0.0f, &prevPos);
    mDoMtx_multVec(root, &prevPos, &prevPos);
    for (int i = 1; i <= links; ++i) {
        cXyz localPos;
        chain_sample(pup().chainPts, count, (f32)i / (f32)links, &localPos);
        cXyz pos;
        mDoMtx_multVec(root, &localPos, &pos);

        cXyz seg = pos - prevPos;
        if (seg.abs() > 0.01f) {
            csXyz ang(seg.atan2sY_XZ(), seg.atan2sX_Z(), roll.z);
            mDoMtx_stack_c::transS(prevPos);
            mDoMtx_stack_c::ZXYrotM(ang);
            Mtx linkMtx;
            mDoMtx_copy(mDoMtx_stack_c::get(), linkMtx);
            if (!draw_chain_link(linkMtx, kIronBallLinkScale)) break;
            roll.z = static_cast<s16>(roll.z + 0x3000);
        }
        prevPos = pos;
    }
}

mDoExt_bckAnm* s_windBck = nullptr;
bool s_windResTried = false;
J3DAnmTextureSRTKey* s_windBtk = nullptr;
J3DModelData* s_windBtkBoundTo = nullptr;
f32 s_windBtkFrame = 0.0f;

void render_boomerang_wind(J3DModel* model, const Mtx world, const AttachedModelSnapshot& att) {
    J3DModelData* data = model->getModelData();
    if (data == nullptr) return;
    if (!s_windResTried) {
        s_windResTried = true;
        J3DAnmTransform* anm = static_cast<J3DAnmTransform*>(
            dComIfG_getObjectRes(daAlink_c::getAlinkArcName(), 0x13));
        if (anm != nullptr) {
            s_windBck = JKR_NEW mDoExt_bckAnm();
            if (s_windBck != nullptr && !s_windBck->init(anm, 0, 2, 1.0f, 0, -1, false)) {
                JKR_DELETE(s_windBck);
                s_windBck = nullptr;
            }
        }
        s_windBtk = static_cast<J3DAnmTextureSRTKey*>(
            dComIfG_getObjectRes(daAlink_c::getAlinkArcName(), 0x48));
    }
    if (s_windBck != nullptr) s_windBck->entry(data, att.frame);

    f32 savedBtkFrame = 0.0f;
    if (s_windBtk != nullptr) {
        if (s_windBtkBoundTo != data) {
            s_windBtk->searchUpdateMaterialID(data);
            s_windBtkBoundTo = data;
        }
        data->entryTexMtxAnimator(s_windBtk);
        savedBtkFrame = s_windBtk->getFrame();
        s_windBtkFrame += 1.0f;
        if (s_windBtkFrame >= static_cast<f32>(s_windBtk->getFrameMax())) s_windBtkFrame = 0.0f;
        s_windBtk->setFrame(s_windBtkFrame);
    }
    model->setBaseScale(cXyz(att.scale, 1.0f, att.scale));
    Mtx worldCopy;
    mDoMtx_copy(world, worldCopy);
    renderModelAtMtx(model, worldCopy, nullptr);
    if (s_windBtk != nullptr) s_windBtk->setFrame(savedBtkFrame);
}

J3DAnmTextureSRTKey* s_aimWindBtk = nullptr;
bool s_aimWindResTried = false;
J3DModelData* s_aimWindBtkBoundTo = nullptr;
f32 s_aimWindBtkFrame = 0.0f;

void render_boomerang_aim_wind(J3DModel* model, const Mtx world) {
    J3DModelData* data = model->getModelData();
    if (data == nullptr) return;
    if (!s_aimWindResTried) {
        s_aimWindResTried = true;
        s_aimWindBtk = static_cast<J3DAnmTextureSRTKey*>(
            dComIfG_getObjectRes(daAlink_c::getAlinkArcName(), 0x47));
    }
    f32 savedBtkFrame = 0.0f;
    if (s_aimWindBtk != nullptr) {
        if (s_aimWindBtkBoundTo != data) {
            s_aimWindBtk->searchUpdateMaterialID(data);
            s_aimWindBtkBoundTo = data;
        }
        data->entryTexMtxAnimator(s_aimWindBtk);
        savedBtkFrame = s_aimWindBtk->getFrame();
        s_aimWindBtkFrame += 1.0f;
        if (s_aimWindBtkFrame >= static_cast<f32>(s_aimWindBtk->getFrameMax())) {
            s_aimWindBtkFrame = 0.0f;
        }
        s_aimWindBtk->setFrame(s_aimWindBtkFrame);
    }
    Mtx worldCopy;
    mDoMtx_copy(world, worldCopy);
    renderModelAtMtx(model, worldCopy, nullptr);
    if (s_aimWindBtk != nullptr) s_aimWindBtk->setFrame(savedBtkFrame);
}

void draw_puppet_attachments(daAlink_c* alink) {
    if (pup().model == nullptr) return;

    pup().chainLinksUsed = 0;
    draw_puppet_chain();
    J3DModelData* bodyData = pup().model->getModelData();
    if (bodyData == nullptr) return;

    for (int i = 0; i < kPuppetAttachSlots; ++i) {
        const AttachedModelSnapshot& att = pup().attached[i];
        if (att.kind == kPuppetHeldNone || att.kind >= kPuppetHeldCount) continue;

        Mtx parent;
        if (att.joint == kPuppetHeldJointRoot) {
            mDoMtx_copy(pup().model->getBaseTRMtx(), parent);
        } else if (att.joint < bodyData->getJointNum()) {
            mDoMtx_copy(pup().model->getAnmMtx(att.joint), parent);
        } else {
            continue;
        }

        J3DModel* model = get_slot_model(i, att.kind, att.bckResIdx);
        if (model == nullptr) continue;

        model->setBaseScale(cXyz(att.scale, att.scale, att.scale));

        Mtx local;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                local[r][c] = att.mtx[r * 4 + c];
            }
        }
        Mtx world;
        mDoMtx_concat(parent, local, world);

        PuppetGuard guard;
        const bool guarded = puppet_guard_begin(model, guard);

        if (att.kind == kPuppetHeldBoomerangWind) {
            render_boomerang_wind(model, world, att);
            if (guarded) puppet_guard_end(guard);
            continue;
        }
        if (att.kind == kPuppetHeldBoomerangAimWind) {
            render_boomerang_aim_wind(model, world);
            if (guarded) puppet_guard_end(guard);
            continue;
        }

        if (att.kind == kPuppetHeldLantern && guarded) {
            J3DModelData* lanternData = model->getModelData();
            if (lanternData != nullptr && lanternData->getJointNum() > kLanternHangJoint) {

                s_lanternHangHaveTarget =
                    s_pupId < kCoopMaxPlayers && s_lanternFlame[s_pupId].ttl > 0;
                if (s_lanternHangHaveTarget) s_lanternHangTarget = s_lanternFlame[s_pupId].pos;
                lanternData->getJointNodePointer(kLanternHangJoint)
                    ->setCallBack(puppet_kantera_callback);
            }
        }

        if (att.kind == kPuppetHeldLanternGlow) {

            J3DModelData* glowData = model->getModelData();
            if (glowData != nullptr) {
                J3DMaterial* mat = glowData->getMaterialNodePointer(0);
                if (mat != nullptr) {
                    const daAlinkHIO_kandelaar_c1& hio = daAlinkHIO_kandelaar_c0::m;
                    J3DGXColorS10 color;
                    color.r = hio.mColorReg1R;
                    color.g = hio.mColorReg1G;
                    color.b = hio.mColorReg1B;
                    color.a = 255;
                    mat->setTevColor(1, &color);
                    color.r = hio.mColorReg2R;
                    color.g = hio.mColorReg2G;
                    color.b = hio.mColorReg2B;
                    mat->setTevColor(2, &color);
                }
            }

            cXyz flame(world[0][3], world[1][3], world[2][3]);
            const bool burning = att.scale > 0.5f;

            if (s_pupId < kCoopMaxPlayers) {
                if (burning) {
                    s_lanternFlame[s_pupId].pos = flame;
                    s_lanternFlame[s_pupId].angleY = pup().angle.y;
                    s_lanternFlame[s_pupId].ttl = kLanternFlameTtl;
                } else {

                    s_lanternFlame[s_pupId].ttl = 0;
                }
            }

            f32 target = burning ? 1.0f : 0.0f;
            if (burning) {
                cXyz proj;
                mDoLib_project(&flame, &proj);
                camera_process_class* cam = dComIfGp_getCamera(0);
                const f32 trim = cam != nullptr ? cam->mCamera.TrimHeight() : 0.0f;
                if (proj.x > 0.0f && proj.x < FB_WIDTH && proj.y > trim &&
                    proj.y < FB_HEIGHT - trim)
                {
                    dComIfGd_peekZ(static_cast<s16>(proj.x), static_cast<s16>(proj.y),
                        &pup().lanternPeekZ);
                } else {
                    pup().lanternPeekZ = 0;
                }
                view_class* view = dComIfGd_getView();
                if (view != nullptr) {
                    cXyz toCam;
                    mDoLib_pos2camera(&flame, &toCam);
                    toCam.z += 30.0f;
                    if (toCam.z > -0.01f) toCam.z = -0.01f;
                    const f32 depth =
                        ((view->near_ + (view->far_ * view->near_) / toCam.z) /
                            (view->far_ - view->near_) + 1.0f) * 1.6777215E7f;
                    if (depth > static_cast<f32>(pup().lanternPeekZ)) target = 0.0f;
                }
            }
            cLib_addCalc(&pup().lanternGlowScale, target, 0.5f, 0.3f, 0.1f);
            const f32 glow = pup().lanternGlowScale;
            model->setBaseScale(cXyz(glow, glow, glow));
        }

        if (att.bckResIdx != 0xFFFF) {
            mDoExt_bckAnm* bck = get_or_load_puppet_anim(alink, att.bckResIdx);
            if (bck != nullptr && anim_is_live(bck)) {
                bck->entry(model->getModelData(), att.frame);
            }
        }

        ShapeVisGuard vis;
        const bool visGuarded =
            pup().itemData[att.kind].shared ? shape_vis_force_show(model, vis) : false;
        renderModelAtMtx(model, world, nullptr);
        if (visGuarded) shape_vis_restore(vis);
        if (guarded) puppet_guard_end(guard);
    }
}

void load_puppet_bck(daAlink_c* alink) {

    for (int i = 0; i < kAnmSlots; ++i) {
        if (pup().under.resIdx[i] != 0xFFFF) get_or_load_puppet_anim(alink, pup().under.resIdx[i]);
        if (pup().upper.resIdx[i] != 0xFFFF) get_or_load_puppet_anim(alink, pup().upper.resIdx[i]);
    }
}

const int kAnimBisectMode = 0;

void update_puppet_anim_selection(daAlink_c* alink) {
    if (kAnimBisectMode != 0) {
        if (kAnimBisectMode == 1) {

            for (int i = 0; i < kAnmSlots; ++i) {
                if (pup().under.resIdx[i] != 0xFFFF) {
                    (void)get_or_load_puppet_anim(alink, pup().under.resIdx[i]);
                }
                if (pup().upper.resIdx[i] != 0xFFFF) {
                    (void)get_or_load_puppet_anim(alink, pup().upper.resIdx[i]);
                }
            }
        }
        pup().under.tbl = nullptr;
        pup().upper.tbl = nullptr;
        return;
    }

}

int s_diagWarpLogCount = 0;
ConfigVarHandle s_warpDumpVar = 0;

bool warp_diag_on() {
    return s_diagWarpLogCount < 3 && cfg_bool(s_warpDumpVar, false);
}

const char* puppet_skin_for(int slot) {
    if (s_pupId >= kCoopMaxPlayers || slot < 0 || slot >= kSkinChoiceCount) return nullptr;

    const SkinChoices& chosen = pup().builtSkins;
    const char* name = chosen.name[slot];
    if (name[0] == '\0' || !skins_have(name, chosen.hash[slot])) return nullptr;
    return name;
}

struct LinkFile {
    const char* arc;
    const char* file;
    int outfit;
    int part;
};
const LinkFile kLinkFiles[] = {
    {"Kmdl", "al.bmd", kSkinOutfitHero, kSkinPartBody},
    {"Kmdl", "al_face.bmd", kSkinOutfitHero, kSkinPartFace},
    {"Kmdl", "al_head.bmd", kSkinOutfitHero, kSkinPartHead},
    {"Kmdl", "al_hands.bmd", kSkinOutfitHero, kSkinPartHands},
    {"Bmdl", "bl.bmd", kSkinOutfitOrdon, kSkinPartBody},
    {"Bmdl", "al_face.bmd", kSkinOutfitOrdon, kSkinPartFace},
    {"Bmdl", "bl_head.bmd", kSkinOutfitOrdon, kSkinPartHead},
    {"Bmdl", "bl_hands.bmd", kSkinOutfitOrdon, kSkinPartHands},
    {"Zmdl", "zl.bmd", kSkinOutfitZora, kSkinPartBody},
    {"Zmdl", "zl_face.bmd", kSkinOutfitZora, kSkinPartFace},
    {"Zmdl", "zl_head.bmd", kSkinOutfitZora, kSkinPartHead},
    {"Zmdl", "al_hands.bmd", kSkinOutfitZora, kSkinPartHands},
    {"Mmdl", "ml.bmd", kSkinOutfitMagic, kSkinPartBody},
    {"Mmdl", "al_face.bmd", kSkinOutfitMagic, kSkinPartFace},
    {"Mmdl", "ml_head.bmd", kSkinOutfitMagic, kSkinPartHead},
    {"Mmdl", "al_hands.bmd", kSkinOutfitMagic, kSkinPartHands},
};

bool s_loadingPuppetModels = false;

struct PuppetModelLoadScope {
    PuppetModelLoadScope() { s_loadingPuppetModels = true; }
    ~PuppetModelLoadScope() { s_loadingPuppetModels = false; }
};

bool skin_data_looks_sane(J3DModelData* data) {
    if (data == nullptr) return false;
    const u16 joints = data->getJointNum();
    if (joints == 0 || joints > kPuppetMaxJoints) return false;
    if (data->getJointNodePointer(0) == nullptr) return false;
    return true;
}

bool skin_fits_original(J3DModelData* mine, J3DModelData* theirs, const char* what) {
    if (mine == nullptr || theirs == nullptr) return false;
    const u16 myJoints = mine->getJointNum();
    const u16 theirJoints = theirs->getJointNum();
    const u16 myMats = mine->getMaterialNum();
    const u16 theirMats = theirs->getMaterialNum();
    if (myJoints == theirJoints && myMats == theirMats) return true;
    coop_log::warn("coop_mod: [SKIN] '{}' does not fit the game's own (joints {}/{}, materials"
                   " {}/{}) - using the game's",
        what, myJoints, theirJoints, myMats, theirMats);
    breadcrumb2("local: REFUSED mismatched part", what);
    return false;
}

J3DModelData* guard_skin_data(J3DModelData* data, const char* what) {
    if (data == nullptr) return nullptr;
    if (skin_data_looks_sane(data)) return data;
    coop_log::warn("coop_mod: [SKIN] '{}' did not look like a model - using the game's own", what);
    breadcrumb2("local: REFUSED bad skin data", what);
    return nullptr;
}

struct AramOriginal {
    u16 index = 0xFFFF;
    J3DModelData* data = nullptr;
};
AramOriginal s_aramOriginals[16];

void remember_aram_original(u16 index, J3DModelData* data) {
    for (AramOriginal& slot : s_aramOriginals) {
        if (slot.index == index) return;
        if (slot.data != nullptr) continue;
        slot.index = index;
        slot.data = data;
        return;
    }
}

J3DModelData* aram_original(u16 index) {
    for (const AramOriginal& slot : s_aramOriginals) {
        if (slot.index == index) return slot.data;
    }
    return nullptr;
}

J3DModelData* local_skin_for(const char* arcName, const char* resName) {
    if (s_loadingPuppetModels || arcName == nullptr || resName == nullptr) return nullptr;
    for (const LinkFile& f : kLinkFiles) {
        if (std::strcmp(f.arc, arcName) != 0 || std::strcmp(f.file, resName) != 0) continue;
        J3DModelData* data = guard_skin_data(skins_local_part_data(f.outfit, f.part), resName);
        if (data != nullptr) breadcrumb2("local: outfit part", resName);
        return data;
    }

    if (std::strncmp(arcName, "Demo", 4) == 0) {
        J3DModelData* data = guard_skin_data(skins_local_cutscene_data(resName), resName);
        if (data != nullptr) breadcrumb2("local: cutscene part", resName);
        return data;
    }

    static const char* const kEquipmentArcs[] = {"Alink", "AlAnm", "HyShd", "SWShd", "MstrSword"};
    for (const char* arc : kEquipmentArcs) {
        if (std::strcmp(arc, arcName) != 0) continue;
        J3DModelData* data = guard_skin_data(skins_local_equipment_data(resName), resName);
        if (data != nullptr) breadcrumb2("local: equipment part", resName);
        return data;
    }
    return nullptr;
}

int puppet_skin_outfit() {
    switch (pup().outfit) {
    case kPuppetOutfitCasual: return kSkinOutfitOrdon;
    case kPuppetOutfitZora: return kSkinOutfitZora;
    case kPuppetOutfitMagicArmor: return kSkinOutfitMagic;
    case kPuppetOutfitWolf: return kSkinOutfitWolf;
    default: return kSkinOutfitHero;
    }
}

const char* equipment_arc_for_file(const char* file) {
    if (file == nullptr) return "Alink";
    if (std::strcmp(file, "al_sha.bmd") == 0) return "HyShd";
    if (std::strcmp(file, "al_shc.bmd") == 0) return "SWShd";
    if (std::strcmp(file, "o_al_swm.bmd") == 0) return "MstrSword";
    return "Alink";
}

J3DModel* puppet_skin_equipment(const char* file, const cXyz& scale) {
    if (file == nullptr) return nullptr;

    const char* name = puppet_skin_for(skins_slot_for_equipment_file(file));
    if (name == nullptr) name = puppet_skin_for(kSkinChoiceEquipment);
    if (name == nullptr) return nullptr;
    J3DModelData* data = skins_equipment_data(name, file);
    if (data == nullptr) return nullptr;

    {
        PuppetModelLoadScope scope;
        J3DModelData* theirs = static_cast<J3DModelData*>(
            dComIfG_getObjectRes(equipment_arc_for_file(file), file));
        if (!skin_fits_original(data, theirs, file)) return nullptr;
    }

    J3DModel* model = modelFromData(data, scale);
    if (model != nullptr) {
        force_warp_off_all_materials(model->getModelData());
        force_diff_recognizes_stage_count(model);
    }
    return model;
}

J3DModel* puppet_skin_part(int part, const cXyz& scale) {
    const int outfit = puppet_skin_outfit();
    const char* name = puppet_skin_for(skins_slot_for_outfit(outfit));
    if (name == nullptr) return nullptr;
    J3DModel* model = skins_part_model(name, outfit, part, scale.x);
    if (model != nullptr) {
        force_warp_off_all_materials(model->getModelData());
        force_diff_recognizes_stage_count(model);
    }
    return model;
}

void load_puppet_parts(const OutfitFiles& files) {

    PuppetModelLoadScope scope;

    if (files.isWolf) return;

    const cXyz unitScale(1.0f, 1.0f, 1.0f);
    pup().faceModel = puppet_skin_part(kSkinPartFace, unitScale);
    if (pup().faceModel == nullptr) pup().faceModel = loadBmdFromArc(files.arc, files.face, unitScale);
    pup().hatModel = puppet_skin_part(kSkinPartHead, unitScale);
    if (pup().hatModel == nullptr) pup().hatModel = loadBmdFromArc(files.arc, files.hat, unitScale);
    pup().handsModel = puppet_skin_part(kSkinPartHands, unitScale);
    if (pup().handsModel == nullptr) {
        pup().handsModel = loadBmdFromArc(files.arc, files.hands, unitScale);
    }
    for (int i = 0; i < 2; ++i) {
        pup().bootModels[i] = loadBmdFromArc(files.arc, "al_bootsH.bmd", unitScale);
        if (pup().bootModels[i] != nullptr) {
            force_warp_off_all_materials(pup().bootModels[i]->getModelData());
            force_diff_recognizes_stage_count(pup().bootModels[i]);
        }
    }
    const bool doDiag = warp_diag_on();
    if (doDiag) {
        ++s_diagWarpLogCount;
        log_warp_state("face BEFORE", pup().faceModel ? pup().faceModel->getModelData() : nullptr);
        log_warp_state("hat BEFORE", pup().hatModel ? pup().hatModel->getModelData() : nullptr);
        log_warp_state("hands BEFORE", pup().handsModel ? pup().handsModel->getModelData() : nullptr);
    }

    if (pup().faceModel != nullptr) {
        force_warp_off_all_materials(pup().faceModel->getModelData());
        force_alpha_always_pass(pup().faceModel->getModelData());
        force_diff_recognizes_stage_count(pup().faceModel);
    }
    if (pup().hatModel != nullptr) {
        force_warp_off_all_materials(pup().hatModel->getModelData());
        force_alpha_always_pass(pup().hatModel->getModelData());
        force_diff_recognizes_stage_count(pup().hatModel);

        if (files.hasKmdlHatTail) {
            install_hat_tail_sway(pup().hatModel);
        }
    }
    if (pup().handsModel != nullptr) {
        force_warp_off_all_materials(pup().handsModel->getModelData());
        force_alpha_always_pass(pup().handsModel->getModelData());
        force_diff_recognizes_stage_count(pup().handsModel);
    }
    breadcrumb("build: face/hat/hands done");
    if (doDiag) {
        log_warp_state("face AFTER", pup().faceModel ? pup().faceModel->getModelData() : nullptr);
        log_warp_state("hat AFTER", pup().hatModel ? pup().hatModel->getModelData() : nullptr);
        log_warp_state("hands AFTER", pup().handsModel ? pup().handsModel->getModelData() : nullptr);
    }
}

bool s_vfxEnabled = true;
bool s_nametagEnabled = true;
bool s_nametagHideFar = false;
bool s_nametagHealth = true;

bool s_edgeTagsEnabled = true;

void spawn_puppet_jump_land(daAlink_c* alink) {
    static const u16 kEffName[6] = {
        ID_ZI_J_LK_DJGIRI_A, ID_ZI_J_LK_DJGIRI_B, ID_ZI_J_LK_DJGIRI_C,
        ID_ZI_J_LK_DJGIRI_D, ID_ZI_J_LK_DJGIRI_E, ID_ZI_J_LK_DJGIRI_F,
    };
    cXyz pos(pup().pos.x + cM_ssin(pup().angle.y) * 20.0f, pup().pos.y + 50.0f,
        pup().pos.z + cM_scos(pup().angle.y) * 20.0f);
    csXyz rot(0, 0, 0);
    dBgS_GndChk gndChk;
    gndChk.SetPos(&pos);
    const f32 groundY = dComIfG_Bgsp().GroundCross(&gndChk);
    if (groundY > -G_CM3D_F_INF && groundY >= pup().pos.y - 50.0f) {
        pos.y = groundY;
        cM3dGPla plane;
        if (dComIfG_Bgsp().GetTriPla(gndChk, &plane)) {
            rot.x = cM_atan2s(plane.mNormal.absXZ(), plane.mNormal.y);
            rot.y = plane.mNormal.atan2sX_Z();
        }
    } else {
        pos.y = pup().pos.y;
    }
    for (int i = 0; i < 6; ++i) {
        dComIfGp_particle_set(kEffName[i], &pos, &alink->tevStr, &rot, nullptr);
    }
}

void update_puppet_dig_vfx(daAlink_c* alink) {
    if (pup().vfxDig == 0) {
        for (u32& key : pup().digEmitterKeys) key = 0;
        return;
    }
    const int groundType = pup().vfxDig - 1;
    u16 dig = 0;
    u16 dash = 0;
    u16 extra = 0;
    f32 dashSpeed = 3.0f;
    if (groundType == 3) {
        dig = dPa_RM(ID_ZI_S_DIGSAND_A);
        dash = dPa_RM(ID_ZI_S_DASHSAND_A);
    } else if (groundType == 0xD) {
        dig = dPa_RM(ID_ZI_S_DIGSNOW_A);
        dash = dPa_RM(ID_ZI_S_DASHSNOW_B);
    } else if (groundType == 1) {
        dig = ID_ZI_J_DIG00_A;
        dash = ID_ZI_J_DASHSMOKE_A;
    } else if (groundType == 4) {
        dig = ID_ZI_J_DIG00_A;
        dash = ID_ZI_J_DASHSMOKE_A;
        extra = ID_ZI_J_DASHKUSA_A;
    } else if (groundType == 7) {
        dash = ID_ZI_J_DASHWTRA_C;
        dig = ID_ZI_J_DASHWTRA_A;
        extra = ID_ZI_J_DASHWTRA_B;
        dashSpeed = 0.0f;
    } else {
        dash = ID_ZI_J_DASHSMOKE_A;
    }
    csXyz rot(pup().digAngleX, pup().angle.y, 0);
    const u16 ids[3] = {dig, dash, extra};
    for (int i = 0; i < 3; ++i) {
        if (ids[i] == 0) {
            pup().digEmitterKeys[i] = 0;
            continue;
        }
        pup().digEmitterKeys[i] = dComIfGp_particle_set(pup().digEmitterKeys[i], ids[i], &pup().digPos,
            &alink->tevStr, &rot, nullptr, 0xFF, nullptr, -1, nullptr, nullptr, nullptr);
        dComIfGp_particle_levelEmitterOnEventMove(pup().digEmitterKeys[i]);
        if (i == 1 && dashSpeed > 0.0f) {
            JPABaseEmitter* emitter = dComIfGp_particle_getEmitter(pup().digEmitterKeys[i]);
            if (emitter != nullptr) emitter->setAwayFromAxisSpeed(dashSpeed);
        }
    }
}

void update_puppet_vfx(daAlink_c* alink) {
    if (pup().state != 2 || pup().model == nullptr) {
        pup().haveJumpLandBaseline = false;
        pup().vfxSpinApplied = kSpinVfxNone;
        return;
    }

    if (local_in_hiding_event(alink)) {
        pup().vfxJumpLandSeen = pup().vfxJumpLand;
        pup().haveJumpLandBaseline = true;
        pup().vfxSpinApplied = kSpinVfxNone;
        for (u32& key : pup().digEmitterKeys) key = 0;
        for (int i = 0; i < kSpinEmitterMax; ++i) pup().spinEmitterKeys[i] = 0;
        return;
    }

    if (!pup().haveJumpLandBaseline) {
        pup().vfxJumpLandSeen = pup().vfxJumpLand;
        pup().haveJumpLandBaseline = true;
    }
    const bool landed = pup().vfxJumpLand != pup().vfxJumpLandSeen;
    pup().vfxJumpLandSeen = pup().vfxJumpLand;
    if (!s_vfxEnabled) {
        pup().vfxSpinApplied = kSpinVfxNone;
        return;
    }
    if (landed) {
        spawn_puppet_jump_land(alink);
    }
    update_puppet_dig_vfx(alink);

    const u8 variant = pup().vfxSpin & kSpinVfxVariantMask;
    if (variant != (pup().vfxSpinApplied & kSpinVfxVariantMask)) {
        for (int i = 0; i < kSpinEmitterMax; ++i) pup().spinEmitterKeys[i] = 0;
    }
    pup().vfxSpinApplied = pup().vfxSpin;
    if (variant == kSpinVfxNone) return;

    if (variant == kSpinVfxWolf) {
        cXyz pos(pup().pos.x, pup().pos.y + 80.0f, pup().pos.z);
        csXyz rot(pup().angle.x, pup().angle.y, 0);
        if (pup().vfxSpin & kSpinVfxFlagWolfFlip) {
            rot.x = static_cast<s16>(rot.x + 0x8000);
        }
        pup().spinEmitterKeys[0] = dComIfGp_particle_set(pup().spinEmitterKeys[0], ID_ZI_J_WL_KAITENAT_A,
            &pos, &alink->tevStr, &rot, nullptr, 0xFF, nullptr, -1, nullptr, nullptr, nullptr);
        dComIfGp_particle_levelEmitterOnEventMove(pup().spinEmitterKeys[0]);
        if (pup().vfxSpin & kSpinVfxFlagWolfEmitB) {
            pup().spinEmitterKeys[1] = dComIfGp_particle_set(pup().spinEmitterKeys[1], ID_ZI_J_WL_KAITENAT_B,
                &pos, &alink->tevStr, &rot, nullptr, 0xFF, nullptr, -1, nullptr, nullptr, nullptr);
            dComIfGp_particle_levelEmitterOnEventMove(pup().spinEmitterKeys[1]);
        } else {
            pup().spinEmitterKeys[1] = 0;
        }
        return;
    }

    J3DModelData* modelData = pup().model->getModelData();
    if (modelData == nullptr || modelData->getJointNum() < 2) return;

    static const u16 kNormalName[] = {ID_ZI_J_KAITENGIRI_A, ID_ZI_J_KAITENGIRI_B};
    static s16 kNormalRot[] = {
        cM_deg2s(180), cM_deg2s(45), cM_deg2s(13), cM_deg2s(180), cM_deg2s(45), cM_deg2s(13),
    };
    static Vec kNormalTrans[] = {{0.0f, 0.0f, 0.0f}, {0.0f, 30.0f, 0.0f}};

    static const u16 kLightName[] = {
        ID_ZI_J_KAITENGIRIL_A, ID_ZI_J_KAITENGIRIL_B, ID_ZI_J_KAITENGIRIL_C, ID_ZI_J_KAITENGIRIL_D,
    };
    static s16 kLightRot[] = {
        cM_deg2s(180), cM_deg2s(100), cM_deg2s(13), cM_deg2s(180), cM_deg2s(80), cM_deg2s(13),
        cM_deg2s(180), cM_deg2s(80),  cM_deg2s(13), cM_deg2s(180), cM_deg2s(80), cM_deg2s(13),
    };
    static Vec kLightTrans[] = {
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 35.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
    };

    static const u16 kLargeName[] = {
        ID_ZI_J_KAITENGIRID_A, ID_ZI_J_KAITENGIRID_B, ID_ZI_J_KAITENGIRID_C,
        ID_ZI_J_KAITENGIRID_D, ID_ZI_J_KAITENGIRID_E, ID_ZI_J_KAITENGIRID_F,
    };
    static s16 kLargeRot[] = {
        cM_deg2s(180), cM_deg2s(45), cM_deg2s(13), cM_deg2s(180), cM_deg2s(45), cM_deg2s(13),
        cM_deg2s(180), cM_deg2s(60), cM_deg2s(13), cM_deg2s(180), cM_deg2s(60), cM_deg2s(13),
        cM_deg2s(180), cM_deg2s(60), cM_deg2s(13), cM_deg2s(180), cM_deg2s(60), cM_deg2s(13),
    };
    static Vec kLargeTrans[] = {
        {0.0f, 0.0f, 0.0f}, {0.0f, 35.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
        {0.0f, 45.0f, 0.0f}, {0.0f, 30.0f, 0.0f}, {0.0f, 50.0f, 0.0f},
    };

    static const u16 kWaterName[] = {ID_ZI_J_KAITENGIRI_INWTR_A, ID_ZI_J_KAITENGIRI_INWTR_B};
    static s16 kWaterRot[] = {
        cM_deg2s(-90), cM_deg2s(0), cM_deg2s(180), cM_deg2s(0), cM_deg2s(0), cM_deg2s(180),
    };
    static Vec kWaterTrans[] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    static Vec kWaterScale = {1.5f, 1.5f, 1.5f};

    const u16* names = nullptr;
    s16* rots = nullptr;
    Vec* trans = nullptr;
    int count = 0;
    switch (variant) {
    case kSpinVfxNormal:
        names = kNormalName;
        rots = kNormalRot;
        trans = kNormalTrans;
        count = 2;
        break;
    case kSpinVfxLight:
        names = kLightName;
        rots = kLightRot;
        trans = kLightTrans;
        count = 4;
        break;
    case kSpinVfxLarge:
        names = kLargeName;
        rots = kLargeRot;
        trans = kLargeTrans;
        count = 6;
        break;
    case kSpinVfxWater:
        names = kWaterName;
        rots = kWaterRot;
        trans = kWaterTrans;
        count = 2;
        break;
    default:
        return;
    }

    cXyz pos;
    mDoMtx_multVecZero(pup().model->getAnmMtx(1), &pos);
    const bool localRot = (pup().vfxSpin & kSpinVfxFlagNoLocalRot) == 0;
    const bool waterLarge = (pup().vfxSpin & kSpinVfxFlagWaterLarge) != 0;
    for (int i = 0; i < count; ++i) {
        pup().spinEmitterKeys[i] = dComIfGp_particle_set(pup().spinEmitterKeys[i], names[i], &pos,
            &alink->tevStr, &pup().angle, nullptr, 0xFF, nullptr, -1, nullptr, nullptr, nullptr);
        dComIfGp_particle_levelEmitterOnEventMove(pup().spinEmitterKeys[i]);
        JPABaseEmitter* emitter = dComIfGp_particle_getEmitter(pup().spinEmitterKeys[i]);
        if (emitter == nullptr) continue;
        if (localRot) {
            emitter->setLocalRotation(*(JGeometry::TVec3<s16>*)&rots[i * 3]);
            if (trans[i].y > 1.0f) {
                emitter->setLocalTranslation(*(JGeometry::TVec3<f32>*)&trans[i]);
            }
        }
        if (waterLarge) {
            if (i == 0) {
                emitter->setGlobalParticleScale(*(JGeometry::TVec3<f32>*)&kWaterScale);
            } else {
                emitter->setVolumeSize(225);
                emitter->setAwayFromAxisSpeed(15.0f);
            }
        }
    }
}

class PuppetNametagDlst : public dDlst_base_c {
public:

    bool layout() {

        {
            const f32 kEase = 0.5f;
            const f32 kSnapDist = 400.0f;
            const cXyz toHead = mHeadWorld - mSmoothHead;
            if (!mHaveSmooth || toHead.abs2() > kSnapDist * kSnapDist) {
                mSmoothHead = mHeadWorld;
                mSmoothFeet = mFeetWorld;
                mHaveSmooth = true;
            } else {
                mSmoothHead = mSmoothHead + (mHeadWorld - mSmoothHead) * (1.0f - kEase);
                mSmoothFeet = mSmoothFeet + (mFeetWorld - mSmoothFeet) * (1.0f - kEase);
            }
        }
        cXyz head = mSmoothHead;

        const view_class* view = dComIfGd_getView();
        if (view == nullptr) return false;
        const cXyz eye = view->lookat.eye;
        const f32 dist = (head - eye).abs();

        if (s_nametagHideFar && dist > 6000.0f) return false;

        const f32 kFullSizeDist = 500.0f;
        f32 cell = dist <= kFullSizeDist ? 18.0f : 18.0f * (kFullSizeDist / dist);

        if (cell < 7.0f) cell = 7.0f;
        mCell = cell;
        mAlpha = 255;
        Vec screen;
        mDoLib_project(&head, &screen);
        const f32 minX = mDoGph_gInf_c::getMinXF();
        const f32 minY = mDoGph_gInf_c::getMinYF();
        const f32 width = mDoGph_gInf_c::getWidthF();
        const f32 height = mDoGph_gInf_c::getHeightF();

        Vec toCam;
        mDoLib_pos2camera(&head, &toCam);
        const bool behind = toCam.z > -1.0f;
        const bool onScreen = !behind && screen.x >= minX && screen.x <= minX + width &&
                              screen.y >= minY + 16.0f && screen.y <= minY + height;

        mEdge = false;
        const char* marker = nullptr;
        f32 tx = screen.x;
        f32 ty = screen.y;
        if (!onScreen) {

            if (!s_edgeTagsEnabled) return false;
            const f32 cx = minX + width * 0.5f;
            const f32 cy = minY + height * 0.5f;
            f32 dx;
            f32 dy;
            if (behind) {
                dx = toCam.x;
                dy = -toCam.y;

                if (dy < 0.0f) dy = -dy;
                if (dx * dx + dy * dy < 1.0f) dy = 1.0f;
            } else {
                dx = screen.x - cx;
                dy = screen.y - cy;
            }
            const f32 kMargin = 28.0f;
            const f32 halfW = width * 0.5f - kMargin;
            const f32 halfH = height * 0.5f - kMargin;
            const f32 ax = dx < 0.0f ? -dx : dx;
            const f32 ay = dy < 0.0f ? -dy : dy;
            if (ax < 0.001f && ay < 0.001f) return false;
            const f32 sx = ax > 0.001f ? halfW / ax : 1.0e9f;
            const f32 sy = ay > 0.001f ? halfH / ay : 1.0e9f;
            const bool sideways = sx < sy;
            const f32 scale = sideways ? sx : sy;
            tx = cx + dx * scale;
            ty = cy + dy * scale;
            if (sideways) {
                marker = dx < 0.0f ? "<" : ">";
            } else {
                marker = dy < 0.0f ? "^" : "v";
            }
            mEdge = true;

            mCell = 14.0f;
            mAlpha = 220;
        }

        if (marker == nullptr) {
            std::strncpy(mName, mBaseName, sizeof(mName) - 1);
        } else if (marker[0] == '<' || marker[0] == '^') {
            std::snprintf(mName, sizeof(mName), "%s %s", marker, mBaseName);
        } else {
            std::snprintf(mName, sizeof(mName), "%s %s", mBaseName, marker);
        }
        mName[sizeof(mName) - 1] = '\0';
        cXyz feet = mSmoothFeet;
        Vec footScreen;
        mDoLib_project(&feet, &footScreen);
        Vec footCam;
        mDoLib_pos2camera(&feet, &footCam);

        mX = tx;
        mY = ty;
        {
            mFootX = footScreen.x;
            mFootY = footScreen.y;
            mFootU = width > 0.0f ? (footScreen.x - minX) / width : 0.0f;
            mFootV = height > 0.0f ? (footScreen.y - minY) / height : 0.0f;
            mCamDist = dist;
            mFootOnScreen = footCam.z <= -1.0f && footScreen.x >= minX &&
                                footScreen.x <= minX + width && footScreen.y >= minY &&
                                footScreen.y <= minY + height - cell;
        }
        return true;
    }

    virtual void draw() {
        if (!mVisible || mBaseName[0] == '\0') return;

        if (!layout()) return;
        JUTFont* font = mDoExt_getMesgFont();
        if (font == nullptr) return;

        J2DOrthoGraph ortho(0.0f, 0.0f, static_cast<f32>(FB_WIDTH), static_cast<f32>(FB_HEIGHT),
            -1.0f, 1.0f);
        ortho.setOrtho(mDoGph_gInf_c::getMinXF(), mDoGph_gInf_c::getMinYF(),
            mDoGph_gInf_c::getWidthF(), mDoGph_gInf_c::getHeightF(), -1.0f, 1.0f);
        ortho.setPort();
        font->setGX();
        const f32 cell = mCell;

        f32 textWidth = 0.0f;
        const f32 cellWidth = static_cast<f32>(font->getCellWidth());
        for (const char* c = mName; *c != '\0'; ++c) {
            const f32 advance = font->isFixed() ? static_cast<f32>(font->getFixedWidth())
                                                : static_cast<f32>(font->getWidth(static_cast<u8>(*c)));
            textWidth += cellWidth > 0.0f ? advance * (cell / cellWidth) : cell * 0.6f;
        }
        f32 x = mX - textWidth * 0.5f;
        f32 y = mY;
        if (mEdge) {

            const f32 minX = mDoGph_gInf_c::getMinXF() + 6.0f;
            const f32 maxX = mDoGph_gInf_c::getMinXF() + mDoGph_gInf_c::getWidthF() - 6.0f;
            const f32 minY = mDoGph_gInf_c::getMinYF() + cell + 6.0f;
            const f32 maxY = mDoGph_gInf_c::getMinYF() + mDoGph_gInf_c::getHeightF() - 6.0f;
            if (x < minX) x = minX;
            if (x + textWidth > maxX) x = maxX - textWidth;
            if (y < minY) y = minY;
            if (y > maxY) y = maxY;
        }
        const f32 shadow = cell * 0.08f;
        font->setCharColor(JUtility::TColor(0, 0, 0, static_cast<u8>(mAlpha * 0.7f)));
        font->drawString_scale(x + shadow, y + shadow, cell, cell, mName, true);
        font->setCharColor(JUtility::TColor(255, 255, 255, mAlpha));
        font->drawString_scale(x, y, cell, cell, mName, true);

        if (J2DGrafContext* port = dComIfGp_getCurrentGrafPort()) {
            port->setPort();
            port->setup2D();
        }
    }

    char mName[32] = {};

    f32 mFootX = 0.0f;
    f32 mFootY = 0.0f;
    bool mFootOnScreen = false;

    cXyz mSmoothHead;
    cXyz mSmoothFeet;
    bool mHaveSmooth = false;
    bool mWasEdge = false;

    cXyz mHeadWorld;
    cXyz mFeetWorld;

    char mBaseName[32] = {};

    f32 mFootU = 0.0f;
    f32 mFootV = 0.0f;
    f32 mCamDist = 0.0f;

    f32 mCell = 18.0f;
    u8 mAlpha = 255;
    f32 mX = 0.0f;
    f32 mY = 0.0f;
    bool mVisible = false;

    bool mEdge = false;
};

PuppetNametagDlst s_nametagDlst[kMaxPuppets];

bool local_in_hiding_event(daAlink_c* alink) {
    if (boss_local_demo_running()) return true;
    if (alink->checkEventRun() == FALSE) return false;
    return fopAcM_getTalkEventPartner(alink) == nullptr;
}

void queue_puppet_nametag(daAlink_c* alink) {
    s_nametagDlst[s_pupId].mVisible = false;
    if (!s_nametagEnabled || pup().nametagName[0] == '\0' || pup().model == nullptr) return;
    cXyz head;
    J3DModelData* modelData = pup().model->getModelData();
    if (!outfit_files(pup().outfit).isWolf && modelData != nullptr &&
        modelData->getJointNum() > 4) {
        mDoMtx_multVecZero(pup().model->getAnmMtx(4), &head);
        head.y += 45.0f;
    } else {
        head.set(pup().pos.x, pup().pos.y + 140.0f, pup().pos.z);
    }
    PuppetNametagDlst& tag = s_nametagDlst[s_pupId];
    tag.mHeadWorld = head;
    tag.mFeetWorld.set(pup().pos.x, pup().pos.y - 8.0f, pup().pos.z);
    std::strncpy(tag.mBaseName, pup().nametagName, sizeof(tag.mBaseName) - 1);
    tag.mBaseName[sizeof(tag.mBaseName) - 1] = '\0';
    tag.mVisible = true;

    dDlst_list_c& lists = g_dComIfG_gameInfo.drawlist;
    for (dDlst_base_c** it = lists.mp2DXluDrawLists; it < lists.mp2DXluStart; ++it) {
        if (*it == &s_nametagDlst[s_pupId]) return;
    }
    dComIfGd_set2DXlu(&s_nametagDlst[s_pupId]);
}

void queue_boss_overlay_from_draw() {
    boss_queue_overlay();
}

void update_one_puppet(daAlink_c* alink);
void horse_idle_tick();

daAlink_c* s_builtAgainstAlink = nullptr;
void* s_builtAgainstArcHeap = nullptr;

bool puppets_lost_their_link(daAlink_c* alink) {
    void* const arcHeap = static_cast<void*>(alink->mpArcHeap);
    if (alink == s_builtAgainstAlink && arcHeap == s_builtAgainstArcHeap) return false;
    const bool hadLink = s_builtAgainstAlink != nullptr;
    const daAlink_c* was = s_builtAgainstAlink;
    const void* wasHeap = s_builtAgainstArcHeap;
    s_builtAgainstAlink = alink;
    s_builtAgainstArcHeap = arcHeap;
    if (!hadLink) return false;
    bool released = false;
    for (int i = 0; i < kMaxPuppets; ++i) {
        PuppetScope scope(static_cast<uint8_t>(i));
        if (pup().state == 0) continue;
        coop_log::warn("coop_mod: [LINKSWAP] the player actor changed under player {}'s puppet "
                        "(alink {} -> {}, arcHeap {} -> {}) - releasing",
            i, static_cast<const void*>(was), static_cast<void*>(alink), wasHeap, arcHeap);
        release_puppet();
        released = true;
    }
    return released;
}

void on_alink_execute_puppet_post(ModContext*, void*, void*, void*) {
    ++s_puppetFrame;

    puppet_flush_pending_frees();

    fx_owner_window(false);
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) {
        for (int i = 0; i < kMaxPuppets; ++i) {
            PuppetScope scope(static_cast<uint8_t>(i));
            release_puppet();
        }
        return;
    }

    if (puppet_hold_for_clothes_swap(alink)) {
        return;
    }
    puppet_hold_for_shield_swap(alink);

    if (puppets_lost_their_link(alink)) return;

    for (int i = 0; i < kMaxPuppets; ++i) {
        if (i == coop_net_local_id()) continue;
        PuppetScope scope(static_cast<uint8_t>(i));
        update_one_puppet(alink);
    }
}

void update_one_puppet(daAlink_c* alink) {
    if (pup().midnaActive && ++pup().midnaAge > kMidnaStaleTicks) pup().midnaActive = false;
    if (pup().horseAge < (1 << 20)) ++pup().horseAge;
    horse_idle_tick();
    if (pup().getItemArcOld[0] != '\0' && --pup().getItemArcOldTimer <= 0) {
        unloadObjectArchive(pup().getItemArcOld);
        pup().getItemArcOld[0] = '\0';
    }

    if (pup().releaseRequested || (!pup().peerVisible && pup().state != 0)) {
        pup().releaseRequested = false;
        release_puppet();
    }

    const CoopPeer& healPeer = features_peer_of(s_pupId);
    const char* healStage = dComIfGp_getStartStageName();
    const bool healHere = healPeer.present && healPeer.inGame && healStage != nullptr &&
                          std::strncmp(healStage, healPeer.stage, 8) == 0;

    const bool healWarping = dComIfGp_isEnableNextStage() != 0;
    if (pup().state == 0 && pup().peerVisible && !pup().respawnPending && healHere &&
        !healWarping) {
        pup().pendingOutfit = pup().outfit;
        pup().state = 1;
        coop_log::info("coop_mod: [PUPPET] player {} had no body and is here - rebuilding",
            static_cast<int>(s_pupId));
    }

    if (pup().skinDirty && pup().state == 2) {

        pup().skinDirty = false;
        if (features_debug_menu()) s_blendTrace = 24;
        breadcrumb("rebuild: teardown begin");
        release_puppet_models_only();
        breadcrumb("rebuild: teardown done");
        pup().pendingOutfit = pup().outfit;
        pup().state = 1;

        return;
    }

    const char* stage = dComIfGp_getStartStageName();
    const char* next = dComIfGp_getNextStageName();
    const bool nextStagePending = dComIfGp_isEnableNextStage() != 0;
    const bool transitioning = nextStagePending ||
                               (next != nullptr && next[0] != '\0' &&
                                (stage == nullptr || std::strcmp(next, stage) != 0));
    const char* curStage = (stage != nullptr) ? stage : "";

    const s32 room = fopAcM_GetRoomNo(alink);
    if (pup().state != 0 &&
        (transitioning ||
         std::strncmp(pup().stage, curStage, sizeof(pup().stage) - 1) != 0 ||
         pup().room != room)) {
        release_puppet();
    }
    std::strncpy(pup().stage, curStage, sizeof(pup().stage) - 1);
    pup().stage[sizeof(pup().stage) - 1] = '\0';
    pup().room = room;

    if (transitioning) {
        return;
    }

    update_puppet_vfx(alink);

    if (pup().state != 0) {
        const f32 kPosSmoothing = 0.35f;
        pup().prevPos = pup().pos;
        pup().pos.x += (pup().targetPos.x - pup().pos.x) * kPosSmoothing;
        pup().pos.y += (pup().targetPos.y - pup().pos.y) * kPosSmoothing;
        pup().pos.z += (pup().targetPos.z - pup().pos.z) * kPosSmoothing;

        cLib_addCalcAngleS(&pup().angle.x, pup().targetAngle.x, 3, 4000, 0);
        cLib_addCalcAngleS(&pup().angle.y, pup().targetAngle.y, 3, 4000, 0);
        cLib_addCalcAngleS(&pup().angle.z, pup().targetAngle.z, 3, 4000, 0);
    }
    if (pup().state == 2) {
        puppet_advance_frames(pup().under);
        puppet_advance_frames(pup().upper);
        update_puppet_anim_selection(alink);
        sync_equipment_models();
    }

    if (pup().state == 1) {
        const OutfitFiles& files = outfit_files(pup().pendingOutfit);

        if (!pup().holdsArc) {
            const int arcStatus = loadObjectArchive(files.arc);
            coop_log::info("coop_mod: [DIAG] loadObjectArchive('{}') = {}", files.arc, arcStatus);
            if (arcStatus == 1) {
                return;
            }
            pup().holdsArc = true;
            std::strncpy(pup().heldArc, files.arc, sizeof(pup().heldArc) - 1);
            pup().heldArc[sizeof(pup().heldArc) - 1] = 0;
        }
        pup().outfit = pup().pendingOutfit;
        pup().state = 2;

        if (s_pupId < kCoopMaxPlayers) pup().builtSkins = features_peer_of(s_pupId).skins;
        breadcrumb("build: body");
        const cXyz unit(1.0f, 1.0f, 1.0f);
        pup().model = puppet_skin_part(kSkinPartBody, unit);
        if (pup().model == nullptr) {
            PuppetModelLoadScope scope;
            pup().model = files.isWolf ? loadBmdFromArcIdx(files.arc, files.bodyResIdx, unit)
                                       : loadBmdFromArc(files.arc, files.body, unit);
        }

        const bool doDiag = warp_diag_on();
        if (pup().model != nullptr) {
            if (doDiag) log_warp_state("body BEFORE", pup().model->getModelData());
            force_warp_off_all_materials(pup().model->getModelData());

            force_alpha_always_pass(pup().model->getModelData());
            force_diff_recognizes_stage_count(pup().model);
            if (doDiag) log_warp_state("body AFTER", pup().model->getModelData());
        }
        if (kAnimBisectMode != 2) {
            load_puppet_bck(alink);
        }
        load_puppet_parts(files);

        colors_attach_puppet_model(pup().model, s_pupId);
        colors_attach_puppet_model(pup().hatModel, s_pupId);
        colors_attach_puppet_model(pup().bootModels[0], s_pupId);
        colors_attach_puppet_model(pup().bootModels[1], s_pupId);
        coop_log::info("coop_mod: [DIAG] model={:p} outfit={} joints={} mats={}",
            static_cast<void*>(pup().model), pup().outfit,
            pup().model ? pup().model->getModelData()->getJointNum() : 0,
            pup().model ? pup().model->getModelData()->getMaterialNum() : 0);
    }
}

}

void coop_crash_trail(const char* step) {
    breadcrumb(step);
}

void puppet_hook_on_horse_snapshot(uint8_t playerId, const HorseSnapshot& snap) {
    if (playerId >= kMaxPuppets || playerId == coop_net_local_id()) return;
    PuppetScope scope(playerId);
    if (!pup().peerVisible) return;

    const HorseSnapshot& last = pup().horse;
    pup().horsePrevValid = pup().horseAge <= 8 && last.jointCount == snap.jointCount &&
                           (last.flags & kHorseFlagRiding) == (snap.flags & kHorseFlagRiding) &&
                           (snap.flags & kHorseFlagIdle) == 0 && snap.jointCount > 0;
    pup().horseGap = pup().horseAge < 1 ? 1 : (pup().horseAge > 8 ? 8 : pup().horseAge);
    pup().horsePrev = last;
    pup().horse = snap;
    pup().horseAge = 0;
}

void puppet_hook_on_midna_snapshot(uint8_t playerId, const MidnaSnapshot& snap) {
    if (playerId >= kMaxPuppets || playerId == coop_net_local_id()) return;
    PuppetScope scope(playerId);
    if (!pup().peerVisible) return;

    if (snap.mode != pup().midna.mode && snap.mode != kMidnaModeNone) {
        std::string cols;
        for (int j = 0; j < kMidnaJoints; ++j) {
            const MidnaJointSnapshot& jt = snap.joints[j];
            f32 len[3];
            for (int c = 0; c < 3; ++c) {
                f32 sum = 0.0f;
                for (int r = 0; r < 3; ++r) {
                    const f32 v = static_cast<f32>(jt.rot[r * 3 + c]) / kMidnaRotFixed;
                    sum += v * v;
                }
                len[c] = std::sqrt(sum);
            }
            char buf[40];
            std::snprintf(buf, sizeof(buf), " %d:%.2f/%.2f/%.2f", j, len[0], len[1], len[2]);
            cols += buf;
        }
        coop_log::info("coop_mod: [MIDNA-DIAG] player {} mode {} baseScale={:.3f} joints{}",
            static_cast<int>(playerId), static_cast<int>(snap.mode), snap.baseScale, cols);
    }
    pup().midna = snap;
    pup().midnaActive = snap.mode != kMidnaModeNone;
    pup().midnaAge = 0;
}

void puppet_hook_on_network_snapshot(uint8_t playerId, float x, float y, float z, int16_t angleX,
    int16_t angleY, int16_t angleZ, int8_t roomNo, uint8_t outfit, const AnmSlotSnapshot* under,
    const AnmSlotSnapshot* upper, uint8_t handL, uint8_t handR,
    const PlayerSnapshot& equipment) {
    if (playerId >= kMaxPuppets || playerId == coop_net_local_id()) return;

    PuppetScope scope(playerId);

    if (!pup().peerVisible) return;
    pup().vfxSpin = equipment.vfxSpin;
    pup().vfxJumpLand = equipment.vfxJumpLand;
    pup().vfxDig = equipment.vfxWolfDig;
    pup().digAngleX = equipment.vfxDigAngleX;
    pup().digPos.set(equipment.vfxDigPos[0], equipment.vfxDigPos[1], equipment.vfxDigPos[2]);
    pup().warpOn = equipment.warpOn;
    pup().warpScroll = equipment.warpScroll;
    pup().warpDissolve = equipment.warpDissolve;
    pup().peerRoom = roomNo;
    pup().handL = handL;
    pup().handR = handR;
    for (int i = 0; i < kPuppetAttachSlots; ++i) pup().attached[i] = equipment.attached[i];
    std::strncpy(pup().rodArc, equipment.rodArc, sizeof(pup().rodArc) - 1);
    pup().rodArc[sizeof(pup().rodArc) - 1] = '\0';
    std::strncpy(pup().rideArc, equipment.rideArc, sizeof(pup().rideArc) - 1);
    pup().rideArc[sizeof(pup().rideArc) - 1] = '\0';
    pup().chainKind = equipment.chainKind;
    pup().chainCount = equipment.chainCount;
    pup().chainStopTime = equipment.chainStopTime;
    for (int i = 0; i < kPuppetChainPts; ++i) {
        for (int c = 0; c < 3; ++c) pup().chainPts[i][c] = equipment.chainPts[i][c];
    }
    pup().rootClearMask = equipment.rootClearMask;
    pup().rootClearX = equipment.rootClearX;
    pup().rootClearY = equipment.rootClearY;
    pup().rootClearZ = equipment.rootClearZ;
    for (int i = 0; i < 4; ++i) {
        for (int a = 0; a < 3; ++a) pup().footAngles[i][a] = equipment.footAngles[i][a];
    }
    pup().bootsVisible = equipment.bootsVisible;
    pup().bodyRotX = equipment.bodyRotX;
    pup().bodyRotY = equipment.bodyRotY;
    pup().bodyRotZ = equipment.bodyRotZ;
    pup().wantSword = equipment.sword;
    pup().wantSheath = equipment.sheath;
    pup().wantSwordVisible = equipment.swordVisible;
    pup().wantShield = equipment.shieldVisible;
    pup().swordInHand = equipment.swordInHand;
    pup().shieldInHand = equipment.shieldInHand;
    pup().swordJoint = equipment.swordJoint;
    pup().sheathJoint = equipment.sheathJoint;
    pup().shieldJoint = equipment.shieldJoint;
    std::strncpy(pup().wantShieldArc, equipment.shieldArc,
        sizeof(pup().wantShieldArc) - 1);
    pup().wantShieldArc[sizeof(pup().wantShieldArc) - 1] = '\0';
    const cXyz newTarget(x, y, z);

    auto apply_slots = [](PuppetAnimHalf& half, const AnmSlotSnapshot* in) {
        for (int i = 0; i < kAnmSlots; ++i) {
            const bool clipChanged = half.resIdx[i] != in[i].resIdx;
            if (clipChanged) {

                pup().oldFrameMorfPending = true;
            }
            half.resIdx[i] = in[i].resIdx;
            half.targetFrame[i] = in[i].frame;
            half.rate[i] = in[i].rate;
            half.ratio[i] = in[i].ratio;

            if (clipChanged) half.frame[i] = in[i].frame;
        }
    };

    const bool needsSpawnOrOutfitChange = (pup().state == 0) ||
        (pup().state == 2 && pup().outfit != outfit) ||
        (pup().state == 1 && pup().pendingOutfit != outfit);
    if (needsSpawnOrOutfitChange) {
        const cXyz targetPos = newTarget;
        csXyz targetAngle;
        targetAngle.set(angleX, angleY, angleZ);
        release_puppet();
        pup().pendingOutfit = outfit;
        pup().state = 1;
        apply_slots(pup().under, under);
        apply_slots(pup().upper, upper);

        pup().targetPos = targetPos;
        pup().targetAngle = targetAngle;
        pup().pos = targetPos;
        pup().angle = targetAngle;
    } else {
        apply_slots(pup().under, under);
        apply_slots(pup().upper, upper);
        pup().targetPos = newTarget;
        pup().targetAngle.set(angleX, angleY, angleZ);
    }
}

uint8_t nearest_to_local() {
    fopAc_ac_c* me = dComIfGp_getPlayer(0);
    if (me == nullptr) return kCoopNoPlayer;
    return puppet_hook_nearest_player(me->current.pos.x, me->current.pos.y, me->current.pos.z);
}

bool puppet_hook_get_position(float* x, float* y, float* z) {
    const uint8_t id = nearest_to_local();
    if (id == kCoopNoPlayer) return false;
    return puppet_hook_get_pose_of(id, x, y, z, nullptr, nullptr, nullptr);
}

void puppet_hook_set_player_low_health(uint8_t playerId, bool low) {
    if (playerId >= kMaxPuppets) return;
    s_puppetSlots[playerId].lowHealth = low;
}

int local_skin_outfit() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return kSkinOutfitHero;
    if (alink->checkWolf()) return kSkinOutfitWolf;
    switch (detect_local_outfit_for_a_press()) {
    case kPuppetOutfitCasual: return kSkinOutfitOrdon;
    case kPuppetOutfitZora: return kSkinOutfitZora;
    case kPuppetOutfitMagicArmor: return kSkinOutfitMagic;
    default: return kSkinOutfitHero;
    }
}

void local_skin_colors_update() {

    if (daAlink_getAlinkActorClass() != nullptr) skins_warn_update(local_skin_outfit());
    static J3DModel* s_lastBody = nullptr;
    static J3DModel* s_lastFace = nullptr;
    static J3DModel* s_lastHat = nullptr;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr || skins_all_same("")) {
        if (s_lastBody != nullptr || s_lastFace != nullptr || s_lastHat != nullptr) {
            colors_detach_local_models();
            s_lastBody = s_lastFace = s_lastHat = nullptr;
        }
        return;
    }

    if (coop_local_models_unsafe()) {
        if (s_lastBody != nullptr || s_lastFace != nullptr || s_lastHat != nullptr) {
            colors_detach_local_models();
            s_lastBody = s_lastFace = s_lastHat = nullptr;
        }
        return;
    }

    if (alink->mpLinkModel != s_lastBody || alink->mpLinkFaceModel != s_lastFace ||
        alink->mpLinkHatModel != s_lastHat) {
        colors_detach_local_models();
        s_lastBody = alink->mpLinkModel;
        s_lastFace = alink->mpLinkFaceModel;
        s_lastHat = alink->mpLinkHatModel;
    }
    colors_attach_local_model(alink->mpLinkModel);
    colors_attach_local_model(alink->mpLinkFaceModel);
    colors_attach_local_model(alink->mpLinkHatModel);
}

void local_skin_suppress(bool suppress) {
    s_loadingPuppetModels = suppress;
}

const char* shield_file_for_arc(const char* arc) {
    if (arc == nullptr || arc[0] == '\0') return nullptr;
    if (std::strcmp(arc, "HyShd") == 0) return "al_sha.bmd";
    if (std::strcmp(arc, "SWShd") == 0) return "al_shc.bmd";
    return nullptr;
}

struct LocalEquipSlot {
    const char* file;
    J3DModelData* data = nullptr;
    J3DModel* ours = nullptr;
    bool restored = false;
};

LocalEquipSlot s_localEquip[] = {
    {"al_swa.bmd"}, {"al_poda.bmd"}, {"al_swm.bmd"}, {"al_podm.bmd"}, {"al_sha.bmd"},
};

void local_equip_install(J3DModel** slot, LocalEquipSlot& rec) {
    if (slot == nullptr) return;
    J3DModelData* want = skins_local_equipment_data(rec.file);
    if (want == nullptr) {

        if (rec.restored || rec.ours == nullptr) return;
        if (*slot != rec.ours) {
            rec.restored = true;
            return;
        }
        J3DModelData* original = nullptr;
        {

            PuppetModelLoadScope scope;
            original = static_cast<J3DModelData*>(
                dComIfG_getObjectRes(equipment_arc_for_file(rec.file), rec.file));
        }

        if (original == nullptr) return;
        J3DModel* built = modelFromData(original, cXyz(1.0f, 1.0f, 1.0f));
        if (built == nullptr) return;

        force_diff_recognizes_stage_count(built);
        *slot = built;
        J3DModel* previous = rec.ours;
        rec.ours = built;
        rec.data = nullptr;
        rec.restored = true;
        puppet_free_later(previous);
        coop_log::info("coop_mod: [EQUIP] '{}' put back to the game's own", rec.file);
        return;
    }
    rec.restored = false;
    if (want == rec.data) {

        if (rec.ours != nullptr && *slot != rec.ours) {
            J3DModelData* inSlot = (*slot != nullptr) ? (*slot)->getModelData() : nullptr;
            if (!skin_fits_original(want, inSlot, rec.file)) return;
            *slot = rec.ours;
            breadcrumb2("local: put our held item back", rec.file);
        }
        return;
    }

    J3DModelData* theirs = (*slot != nullptr) ? (*slot)->getModelData() : nullptr;
    if (!skin_fits_original(want, theirs, rec.file)) return;

    J3DModel* built = modelFromData(want, cXyz(1.0f, 1.0f, 1.0f));
    if (built == nullptr) return;
    prep_equipment_model(built);

    J3DModel* previous = rec.ours;
    rec.ours = built;
    rec.data = want;
    *slot = built;
    if (previous != nullptr) puppet_free_later(previous);
    breadcrumb2("local: installed held item", rec.file);
}

void local_skin_rebuild_equipment(daAlink_c* alink) {
    if (alink == nullptr) return;

    if (alink->mClothesChangeWaitTimer != 0) return;

    if (alink->mShieldChangeWaitTimer != 0) return;
    if (coop_local_models_unsafe()) return;
    if (alink->checkWolf()) return;
    J3DModel** const slots[] = {
        &alink->mpSwAModel, &alink->mpSwASheathModel, &alink->mpSwMModel,
        &alink->mpSwMSheathModel, &alink->mShieldModel,
    };
    const int count = static_cast<int>(sizeof(slots) / sizeof(slots[0]));

    const int kShieldSlot = 4;
    const char* shieldFile = shield_file_for_arc(alink->mShieldArcName);
    if (shieldFile != nullptr && std::strcmp(shieldFile, s_localEquip[kShieldSlot].file) != 0) {
        s_localEquip[kShieldSlot].file = shieldFile;
        s_localEquip[kShieldSlot].data = nullptr;
    }

    for (int i = 0; i < count; ++i) {

        if (i == kShieldSlot && shieldFile == nullptr) continue;
        local_equip_install(slots[i], s_localEquip[i]);
    }
}

void local_skin_equipment_update() {
    local_skin_rebuild_equipment(daAlink_getAlinkActorClass());
}

void local_skin_rebuild_link() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;

    if (alink->mClothesChangeWaitTimer != 0) return;
    breadcrumb("local: rebuilding Link for a model change");
    local_skin_rebuild_equipment(alink);
    if (alink->checkWolf()) {

        coop_log::info("coop_mod: [SKIN] model change held until you are not a wolf");
        return;
    }
    alink->setClothesChange(0);
}

void puppet_hook_peer_skin_changed(uint8_t playerId) {
    if (playerId >= kMaxPuppets || playerId >= kCoopMaxPlayers) return;
    Puppet& slot = s_puppetSlots[playerId];
    if (slot.state == 0) return;

    const SkinChoices& now = features_peer_of(playerId).skins;
    if (std::memcmp(&now, &slot.builtSkins, sizeof(SkinChoices)) == 0) return;
    slot.skinDirty = true;
}

void puppet_register_vars() {
    if (svc_config == nullptr) return;
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = "debug_warp_dump";
    desc.type = CONFIG_VAR_BOOL;
    desc.default_bool = false;
    if (svc_config->register_var(mod_ctx, &desc, &s_warpDumpVar) != MOD_OK) s_warpDumpVar = 0;
}

ConfigVarHandle puppet_warp_dump_var() {
    return s_warpDumpVar;
}

void puppet_hook_set_nametag_health(bool enabled) {
    s_nametagHealth = enabled;
}

bool puppet_hook_health_enabled() {
    return s_nametagHealth && s_nametagEnabled;
}

bool puppet_hook_health_anchor(uint8_t playerId, float* u, float* v, float* cell, float* camDist) {
    if (playerId >= kMaxPuppets || !puppet_hook_health_enabled()) return false;
    const PuppetNametagDlst& t = s_nametagDlst[playerId];
    if (!t.mVisible || t.mEdge || !t.mFootOnScreen) return false;
    *u = t.mFootU;
    *v = t.mFootV;
    *cell = t.mCell;
    *camDist = t.mCamDist;
    return true;
}

void puppet_hook_set_edge_tags(bool enabled) {
    s_edgeTagsEnabled = enabled;
}

void puppet_hook_set_player_nametag(uint8_t playerId, const char* name, bool enabled,
    bool hideFar) {

    s_nametagEnabled = enabled;
    s_nametagHideFar = hideFar;
    if (playerId >= kMaxPuppets) return;
    Puppet& q = s_puppetSlots[playerId];
    std::strncpy(q.nametagName, name != nullptr ? name : "", sizeof(q.nametagName) - 1);
    q.nametagName[sizeof(q.nametagName) - 1] = '\0';
}

void puppet_hook_set_vfx_enabled(bool enabled) {
    s_vfxEnabled = enabled;
}

void puppet_hook_set_player_visible(uint8_t playerId, bool visible) {
    if (playerId >= kMaxPuppets) return;
    s_puppetSlots[playerId].peerVisible = visible;
}

bool puppet_hook_is_wolf() {
    const uint8_t id = nearest_to_local();
    if (id == kCoopNoPlayer) return false;
    return outfit_files(s_puppetSlots[id].outfit).isWolf;
}

void puppet_hook_request_release() {

    for (int i = 0; i < kMaxPuppets; ++i) s_puppetSlots[i].releaseRequested = true;
}

void puppet_hook_release_player(uint8_t playerId) {
    if (playerId >= kMaxPuppets) return;
    s_puppetSlots[playerId].releaseRequested = true;
}

bool puppet_hook_player_active(uint8_t playerId) {
    return playerId < kMaxPuppets && s_puppetSlots[playerId].state == 2 &&
           s_puppetSlots[playerId].model != nullptr;
}

bool puppet_hook_get_pose_of(uint8_t playerId, float* x, float* y, float* z, short* angleY,
    float* speedX, float* speedZ) {
    if (!puppet_hook_player_active(playerId)) return false;
    const Puppet& q = s_puppetSlots[playerId];
    if (x != nullptr) *x = q.pos.x;
    if (y != nullptr) *y = q.pos.y;
    if (z != nullptr) *z = q.pos.z;
    if (angleY != nullptr) *angleY = q.angle.y;
    if (speedX != nullptr) *speedX = q.pos.x - q.prevPos.x;
    if (speedZ != nullptr) *speedZ = q.pos.z - q.prevPos.z;
    return true;
}

bool puppet_hook_get_anim(uint8_t playerId, uint16_t* resIdx, float* frame) {
    if (!puppet_hook_player_active(playerId)) return false;
    const Puppet& q = s_puppetSlots[playerId];
    if (resIdx != nullptr) *resIdx = q.under.resIdx[0];
    if (frame != nullptr) *frame = q.under.frame[0];
    return true;
}

uint8_t puppet_hook_nearest_player(float x, float y, float z) {
    uint8_t best = kCoopNoPlayer;
    f32 bestD = 0.0f;
    for (int i = 0; i < kMaxPuppets; ++i) {
        if (!puppet_hook_player_active(static_cast<uint8_t>(i))) continue;
        const Puppet& q = s_puppetSlots[i];
        const f32 dx = q.pos.x - x, dy = q.pos.y - y, dz = q.pos.z - z;
        const f32 d = dx * dx + dy * dy + dz * dz;
        if (best == kCoopNoPlayer || d < bestD) {
            best = static_cast<uint8_t>(i);
            bestD = d;
        }
    }
    return best;
}

void puppet_hook_trigger_spawn() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;

    const char* stage = dComIfGp_getStartStageName();
    const bool onTitle = stage != nullptr &&
                         (std::strcmp(stage, "F_SP102") == 0 || std::strcmp(stage, "title") == 0);
    const bool inEvent = dComIfGp_event_runCheck();
    if (onTitle || inEvent) {
        coop_log::info("coop_mod: [DIAG] spawn button pressed but skipped: onTitle={} inEvent={}",
            onTitle, static_cast<int>(inEvent));
        return;
    }

    release_puppet();

    cXyz offset(0.0f, 0.0f, -kPuppetSpawnDist);
    cLib_offsetPos(&pup().pos, &alink->current.pos, alink->shape_angle.y, &offset);
    pup().angle.set(0, alink->shape_angle.y, 0);

    pup().targetPos = pup().pos;
    pup().targetAngle = pup().angle;
    pup().pendingOutfit = detect_local_outfit_for_a_press();
    pup().state = 1;
    coop_log::info(
        "coop_mod: [DIAG] spawn button pressed: player=({},{},{}) angleY={} outfit={} -> "
        "puppetPos=({},{},{})",
        alink->current.pos.x, alink->current.pos.y, alink->current.pos.z, alink->shape_angle.y,
        pup().pendingOutfit, pup().pos.x, pup().pos.y, pup().pos.z);
}

namespace {

const int kUnderWaistJoint = 16;

const int kWolfUpperJoint = 3;
const int kWolfUnderSecondJoint = 15;

void prep_equipment_model(J3DModel* model) {
    if (model == nullptr) return;
    J3DModelData* modelData = model->getModelData();
    if (modelData == nullptr) return;

    dRes_info_c::offWarpMaterial(modelData);

    force_diff_recognizes_stage_count(model);
}

void sync_equipment_models() {
    if (pup().wantSword != pup().swordId) {
        puppet_free_later(pup().swordModel);

        int idx = -1;
        const char* outfitBmd = nullptr;

        const char* skinBmd = nullptr;
        switch (pup().wantSword) {
        case kPuppetSwordOrdon:  idx = dRes_INDEX_ALINK_BMD_AL_SWA_e; skinBmd = "al_swa.bmd"; break;
        case kPuppetSwordMaster: idx = dRes_INDEX_ALINK_BMD_AL_SWM_e; skinBmd = "al_swm.bmd"; break;
        case kPuppetSwordWood:   outfitBmd = "al_SWB.bmd"; skinBmd = "al_swb.bmd"; break;
        default: break;
        }
        if (skinBmd != nullptr) {
            pup().swordModel = puppet_skin_equipment(skinBmd, cXyz(1.0f, 1.0f, 1.0f));
            if (pup().swordModel != nullptr) prep_equipment_model(pup().swordModel);
        }
        if (pup().swordModel != nullptr) {
            coop_log::trace("coop_mod: [DIAG-SWORD] '{}' from the model's equipment folder", skinBmd);
        } else if (outfitBmd != nullptr && pup().state == 2) {
            pup().swordModel = loadBmdFromArc(outfit_files(pup().outfit).arc, outfitBmd,
                cXyz(1.0f, 1.0f, 1.0f));
            prep_equipment_model(pup().swordModel);
            coop_log::trace("coop_mod: [DIAG-SWORD] wooden sword from outfit archive '{}'",
                outfitBmd);
        } else if (idx >= 0) {
            pup().swordModel = loadBmdFromArcIdx("Alink", idx, cXyz(1.0f, 1.0f, 1.0f));
            prep_equipment_model(pup().swordModel);

            daAlink_c* la = daAlink_getAlinkActorClass();
            J3DModel* ref = (la != nullptr)
                ? ((pup().wantSword == kPuppetSwordMaster) ? la->mpSwMModel : la->mpSwAModel)
                : nullptr;
            if (pup().swordModel != nullptr) {
                J3DModelData* od = pup().swordModel->getModelData();
                J3DModelData* rd = (ref != nullptr) ? ref->getModelData() : nullptr;
                coop_log::trace("coop_mod: [DIAG-SWORD] ourData={:p} refData={:p} ourDiff={:#x} "
                    "refDiff={:#x} mats={}",
                    static_cast<void*>(od), static_cast<void*>(rd),
                    pup().swordModel->mDiffFlag, (ref != nullptr) ? ref->mDiffFlag : 0u,
                    (od != nullptr) ? od->getMaterialNum() : 0);
                if (od != nullptr) {
                    for (u16 mi = 0; mi < od->getMaterialNum(); ++mi) {
                        J3DMaterial* mat = od->getMaterialNodePointer(mi);
                        if (mat == nullptr) continue;
                        J3DShape* shp = mat->getShape();
                        J3DTevBlock* tb = mat->getTevBlock();
                        J3DTexGenBlock* tg = mat->getTexGenBlock();
                        coop_log::trace("coop_mod: [DIAG-SWORD]   mat[{}] tev={} texgen={} "
                            "hidden={} lastTexMap={}",
                            mi, (tb != nullptr) ? tb->getTevStageNum() : 0,
                            (tg != nullptr) ? tg->getTexGenNum() : 0,
                            (shp != nullptr && shp->checkFlag(J3DShpFlag_Visible)) ? 1 : 0,
                            (tb != nullptr && tb->getTevStageNum() > 0 &&
                                tb->getTevOrder(tb->getTevStageNum() - 1) != nullptr)
                                ? tb->getTevOrder(tb->getTevStageNum() - 1)->getTexMap() : 255);
                    }
                }
            }
        }

        if (outfitBmd == nullptr || pup().swordModel != nullptr) {
            pup().swordId = pup().wantSword;
        }
        coop_log::trace("coop_mod: [DIAG-EQUIP] sword={} model={:p}", pup().swordId,
            static_cast<void*>(pup().swordModel));
    }

    if (pup().wantSheath != pup().sheathId) {
        puppet_free_later(pup().sheathModel);
        int idx = -1;
        const char* skinBmd = nullptr;
        switch (pup().wantSheath) {
        case kPuppetSheathOrdon:
            idx = dRes_INDEX_ALINK_BMD_AL_PODA_e;
            skinBmd = "al_poda.bmd";
            break;
        case kPuppetSheathMaster:
            idx = dRes_INDEX_ALINK_BMD_AL_PODM_e;
            skinBmd = "al_podm.bmd";
            break;
        default: break;
        }
        if (skinBmd != nullptr) {
            pup().sheathModel = puppet_skin_equipment(skinBmd, cXyz(1.0f, 1.0f, 1.0f));
            if (pup().sheathModel != nullptr) prep_equipment_model(pup().sheathModel);
        }
        if (pup().sheathModel == nullptr && idx >= 0) {
            pup().sheathModel = loadBmdFromArcIdx("Alink", idx, cXyz(1.0f, 1.0f, 1.0f));
            prep_equipment_model(pup().sheathModel);
        }
        pup().sheathId = pup().wantSheath;
        coop_log::trace("coop_mod: [DIAG-EQUIP] sheath={} model={:p}", pup().sheathId,
            static_cast<void*>(pup().sheathModel));
    }

    const bool wantShield = pup().wantShield != 0 && pup().wantShieldArc[0] != '\0';
    const bool archiveChanged = std::strncmp(pup().wantShieldArc, pup().shieldArc,
                                             sizeof(pup().shieldArc)) != 0;
    if (!wantShield || archiveChanged) {

        puppet_free_later(pup().shieldModel);
        if (pup().shieldArc[0] != '\0') {
            release_arc_share(pup().shieldArc);
            pup().shieldArc[0] = '\0';
        }
    }

    daAlink_c* shieldAlink = daAlink_getAlinkActorClass();
    const bool shieldSwapInFlight =
        shieldAlink != nullptr && shieldAlink->mShieldChangeWaitTimer != 0;
    if (wantShield && pup().shieldModel == nullptr && !shieldSwapInFlight) {

        const bool arcReady = loadObjectArchive(pup().wantShieldArc) != 1;

        const char* shieldFile = shield_file_for_arc(pup().wantShieldArc);
        pup().shieldModel = (arcReady && shieldFile != nullptr)
            ? puppet_skin_equipment(shieldFile, cXyz(1.0f, 1.0f, 1.0f))
            : nullptr;
        if (pup().shieldModel != nullptr) {
            prep_equipment_model(pup().shieldModel);
            std::strncpy(pup().shieldArc, pup().wantShieldArc, sizeof(pup().shieldArc) - 1);
            coop_log::trace("coop_mod: [DIAG-EQUIP] shield '{}' from the model's equipment folder",
                shieldFile);
        }
        if (pup().shieldModel == nullptr && arcReady) {
            pup().shieldModel =
                loadBmdFromArcIdx(pup().wantShieldArc, 3, cXyz(1.0f, 1.0f, 1.0f));
            prep_equipment_model(pup().shieldModel);
            std::strncpy(pup().shieldArc, pup().wantShieldArc, sizeof(pup().shieldArc) - 1);
            coop_log::trace("coop_mod: [DIAG-EQUIP] shield arc='{}' model={:p}",
                pup().shieldArc, static_cast<void*>(pup().shieldModel));
        }
    }
}

bool shape_vis_force_show(J3DModel* model, ShapeVisGuard& guard) {
    if (model == nullptr) return false;
    J3DModelData* modelData = model->getModelData();
    if (modelData == nullptr) return false;
    const u16 num = modelData->getMaterialNum();
    if (num > kMaxEquipShapes) return false;
    guard.modelData = modelData;
    guard.num = num;
    for (u16 i = 0; i < num; ++i) {
        J3DMaterial* mat = modelData->getMaterialNodePointer(i);
        J3DShape* shp = (mat != nullptr) ? mat->getShape() : nullptr;
        guard.wasHidden[i] = (shp != nullptr) && shp->checkFlag(J3DShpFlag_Visible);
        if (shp != nullptr) shp->show();
    }
    return true;
}

bool hand_index_is_body(u8 idx) { return idx != 0xFE && (idx & 0x80) != 0; }

struct BodyHandGuard {
    J3DShape* shape[2] = {nullptr, nullptr};

    bool wasHidden[2] = {false, false};
};

bool body_hand_shapes_apply(J3DModel* body, u8 leftIdx, u8 rightIdx, BodyHandGuard& guard) {
    if (body == nullptr) return false;
    J3DModelData* data = body->getModelData();
    if (data == nullptr) return false;
    const u16 num = data->getMaterialNum();
    bool any = false;
    const u8 wanted[2] = {leftIdx, rightIdx};
    for (int h = 0; h < 2; ++h) {
        if (!hand_index_is_body(wanted[h])) continue;
        const u16 mat = static_cast<u16>(wanted[h] & 0x7F);
        if (mat >= num) continue;
        J3DMaterial* m = data->getMaterialNodePointer(mat);
        J3DShape* shp = (m != nullptr) ? m->getShape() : nullptr;
        if (shp == nullptr) continue;
        guard.shape[h] = shp;
        guard.wasHidden[h] = shp->checkFlag(J3DShpFlag_Visible);
        shp->show();
        any = true;
    }
    return any;
}

void body_hand_shapes_restore(BodyHandGuard& guard) {
    for (int h = 0; h < 2; ++h) {
        if (guard.shape[h] == nullptr) continue;
        if (guard.wasHidden[h]) guard.shape[h]->hide(); else guard.shape[h]->show();
        guard.shape[h] = nullptr;
    }
}

bool hand_shapes_apply(J3DModel* model, u8 leftIdx, u8 rightIdx, ShapeVisGuard& guard) {
    if (model == nullptr) return false;
    J3DModelData* modelData = model->getModelData();
    if (modelData == nullptr) return false;
    const u16 num = modelData->getMaterialNum();
    if (num > kMaxEquipShapes) return false;
    guard.modelData = modelData;
    guard.num = num;
    for (u16 i = 0; i < num; ++i) {
        J3DMaterial* mat = modelData->getMaterialNodePointer(i);
        J3DShape* shp = (mat != nullptr) ? mat->getShape() : nullptr;
        guard.wasHidden[i] = (shp != nullptr) && shp->checkFlag(J3DShpFlag_Visible);
        if (shp == nullptr) continue;
        const bool wanted = (!hand_index_is_body(leftIdx) && i == leftIdx) ||
                            (!hand_index_is_body(rightIdx) && i == rightIdx);
        if (wanted) shp->show(); else shp->hide();
    }
    return true;
}

void shape_vis_restore(ShapeVisGuard& guard) {
    if (guard.modelData == nullptr) return;
    for (u16 i = 0; i < guard.num; ++i) {
        J3DMaterial* mat = guard.modelData->getMaterialNodePointer(i);
        J3DShape* shp = (mat != nullptr) ? mat->getShape() : nullptr;
        if (shp == nullptr) continue;
        if (guard.wasHidden[i]) shp->hide(); else shp->show();
    }
    guard.modelData = nullptr;
}

void draw_equipment_at_joint(J3DModel* model, u16 joint) {
    if (model == nullptr || pup().model == nullptr) return;
    J3DModelData* bodyData = pup().model->getModelData();
    if (bodyData == nullptr || joint >= bodyData->getJointNum()) return;
    PuppetGuard guard;
    const bool guarded = puppet_guard_begin(model, guard);
    ShapeVisGuard vis;
    const bool visGuarded = shape_vis_force_show(model, vis);
    renderModelAtMtx(model, pup().model->getAnmMtx(joint), nullptr);
    if (visGuarded) shape_vis_restore(vis);
    if (guarded) puppet_guard_end(guard);
}

void draw_equipment_on_back(J3DModel* model, u16 sheathJoint, f32 tx, f32 ty, f32 tz,
    s16 rx, s16 ry, s16 rz) {
    if (model == nullptr || pup().model == nullptr) return;
    J3DModelData* bodyData = pup().model->getModelData();
    if (bodyData == nullptr || sheathJoint >= bodyData->getJointNum()) return;
    mDoMtx_stack_c::copy(pup().model->getAnmMtx(sheathJoint));
    mDoMtx_stack_c::transM(tx, ty, tz);
    mDoMtx_stack_c::XYZrotM(rx, ry, rz);
    PuppetGuard guard;
    const bool guarded = puppet_guard_begin(model, guard);
    ShapeVisGuard vis;
    const bool visGuarded = shape_vis_force_show(model, vis);
    renderModelAtMtx(model, mDoMtx_stack_c::get(), nullptr);
    if (visGuarded) shape_vis_restore(vis);
    if (guarded) puppet_guard_end(guard);
}

void draw_puppet_equipment() {

    if (outfit_files(pup().outfit).isWolf) return;

    if (pup().wantSwordVisible) {
        draw_equipment_at_joint(pup().sheathModel, pup().sheathJoint);
        if (pup().swordInHand) {
            draw_equipment_at_joint(pup().swordModel, pup().swordJoint);
        } else {
            draw_equipment_on_back(pup().swordModel, pup().sheathJoint,
                -18.5f, 0.14f, 12.2f, 0, cM_deg2s(33.1f), 0);
        }
    }

    daAlink_c* localAlink = daAlink_getAlinkActorClass();
    const bool shieldSwapping =
        (localAlink != nullptr) && localAlink->mShieldChangeWaitTimer != 0;
    if (pup().wantShield && !shieldSwapping) {
        if (pup().shieldInHand) {
            draw_equipment_at_joint(pup().shieldModel, pup().shieldJoint);
        } else {
            draw_equipment_on_back(pup().shieldModel, pup().sheathJoint,
                4.2f, -4.4f, -20.0f, cM_deg2s(91.0f), cM_deg2s(57.0f), cM_deg2s(180.0f));
        }
    }
}

void render_puppet_body_with_upper_split(J3DModel* model, const cXyz& pos, const csXyz& angle,
    daAlink_c* alink) {
    if (model == nullptr) {
        static int s_nullReport = 0;
        if (++s_nullReport % 120 == 1) {
            coop_log::info("coop_mod: [DRAWSKIP] puppet {} has no body model (state={})",
                static_cast<int>(s_pupId), static_cast<int>(pup().state));
        }
        return;
    }
    J3DModelData* modelData = model->getModelData();

    if (!puppet_ensure_old_frame(modelData->getJointNum())) return;
    if (pup().oldFrameMorfPending) {

        pup().oldFrame->initOldFrameMorf(kPuppetOldFrameMorf, 0, modelData->getJointNum());
        pup().oldFrameMorfPending = false;
    }

    mDoExt_MtxCalcAnmBlendTblOld* underTbl = puppet_build_blend(alink, pup().under);
    mDoExt_MtxCalcAnmBlendTblOld* upperTbl = puppet_build_blend(alink, pup().upper);

    const bool wolf = outfit_files(pup().outfit).isWolf;
    const int upperJoint = wolf ? kWolfUpperJoint : kUpperBodyRootJoint;
    const int underSecondJoint = wolf ? kWolfUnderSecondJoint : kUnderWaistJoint;

    int borrowedJoints[3] = {-1, -1, -1};
    J3DMtxCalc* borrowedSaved[3] = {nullptr, nullptr, nullptr};
    const auto remember = [&](int joint) {
        for (int i = 0; i < 3; ++i) {
            if (borrowedJoints[i] == joint) return;
            if (borrowedJoints[i] < 0) {
                borrowedJoints[i] = joint;
                J3DJoint* node = modelData->getJointNodePointer(joint);
                borrowedSaved[i] = (node != nullptr) ? node->getMtxCalc() : nullptr;
                return;
            }
        }
    };
    if (underTbl != nullptr) {
        remember(kUnderRootJoint);
        modelData->getJointNodePointer(kUnderRootJoint)->setMtxCalc(underTbl);
        if (modelData->getJointNum() > underSecondJoint) {
            remember(underSecondJoint);
            modelData->getJointNodePointer(underSecondJoint)->setMtxCalc(underTbl);
        }
    }
    if (upperTbl != nullptr && modelData->getJointNum() > upperJoint) {
        remember(upperJoint);
        modelData->getJointNodePointer(upperJoint)->setMtxCalc(upperTbl);
    } else if (underTbl != nullptr && modelData->getJointNum() > upperJoint) {

        remember(upperJoint);
        modelData->getJointNodePointer(upperJoint)->setMtxCalc(underTbl);
    }

    mDoMtx_stack_c::transS(pos.x, pos.y, pos.z);
    mDoMtx_stack_c::ZXYrotM(angle.x, angle.y, angle.z);
    model->setBaseScale(cXyz(1.0f, 1.0f, 1.0f));
    model->setBaseTRMtx(mDoMtx_stack_c::get());
    model->calc();

    if (alink != nullptr) {
        g_env_light.settingTevStruct_colget_player(&alink->tevStr);
        g_env_light.setLightTevColorType_MAJI(model, &alink->tevStr);
    }

    MaterialGuard matGuard;
    const bool matGuarded = material_guard_begin(model, matGuard);

    mDoExt_modelUpdateDL(model);

    if (matGuarded) material_guard_end(matGuard);

    for (int i = 0; i < 3; ++i) {
        if (borrowedJoints[i] < 0) continue;
        J3DJoint* node = modelData->getJointNodePointer(borrowedJoints[i]);
        if (node != nullptr) node->setMtxCalc(borrowedSaved[i]);
    }
}

int s_diagDrawCount = 0;

bool warp_texgen_is_real(J3DTexGenBlock* texGen, u32 gen) {
    if (texGen == nullptr || gen >= 8) return false;

    if (texGen->getTexMtx(gen) == nullptr) return false;
    J3DTexCoord* coord = texGen->getTexCoord(gen);
    if (coord == nullptr) return false;
    if (coord->getTexGenType() != GX_TG_MTX3x4) return false;
    if (coord->getTexGenSrc() != GX_TG_POS) return false;
    if (coord->getTexGenMtx() != gen * 3 + GX_TEXMTX0) return false;
    return true;
}

struct PuppetWarpScope {
    struct Saved {
        J3DMaterial* material;
        J3DAlphaComp alpha;
    };
    static constexpr int kMax = 128;
    Saved saved[kMax];
    int count = 0;

    void apply(J3DModel* model) {
        if (model == nullptr) return;

        if ((model->mDiffFlag & J3DDiffFlag_TexGen) == 0) return;
        J3DModelData* data = model->getModelData();
        if (data == nullptr) return;
        static const J3DAlphaCompInfo kWarpAlpha = {0x04, 0x80, 0x00, 0x03, 0xFF};

        bool firstPrepared = false;
        for (u16 i = 0; i < data->getMaterialNum() && count < kMax; ++i) {
            J3DMaterial* material = data->getMaterialNodePointer(i);
            if (material == nullptr) continue;
            J3DTevBlock* tev = material->getTevBlock();
            J3DTexGenBlock* texGen = material->getTexGenBlock();
            J3DPEBlock* pe = material->getPEBlock();
            if (tev == nullptr || texGen == nullptr || pe == nullptr) continue;
            const u8 stages = tev->getTevStageNum();
            const u32 gens = texGen->getTexGenNum();
            if (stages == 0 || stages >= 4 || gens >= 4) continue;
            J3DTevOrder* prepared = tev->getTevOrder(stages);
            if (prepared == nullptr || prepared->getTexMap() != 3) continue;
            if (!warp_texgen_is_real(texGen, gens)) continue;
            J3DAlphaComp* alphaComp = pe->getAlphaComp();
            if (alphaComp == nullptr) continue;
            saved[count].material = material;
            saved[count].alpha = *alphaComp;
            ++count;
            tev->setTevStageNum(stages + 1);
            texGen->setTexGenNum(texGen->getTexGenNum() + 1);
            alphaComp->setAlphaCompInfo(kWarpAlpha);
            if (i == 0) firstPrepared = true;
        }

        if (!firstPrepared) return;
        J3DMaterial* first = data->getMaterialNodePointer(0);
        J3DTexGenBlock* firstTexGen = first->getTexGenBlock();
        const u32 texGenNum = firstTexGen->getTexGenNum();
        if (texGenNum == 0 || firstTexGen->getTexMtx(texGenNum - 1) == nullptr) return;
        dRes_info_c::setWarpSRT(data, pup().pos, pup().warpScroll, pup().warpDissolve);
    }

    ~PuppetWarpScope() {
        for (int i = count - 1; i >= 0; --i) {
            J3DMaterial* material = saved[i].material;
            J3DTevBlock* tev = material->getTevBlock();
            J3DTexGenBlock* texGen = material->getTexGenBlock();

            const u8 stages = tev->getTevStageNum();
            const u32 gens = texGen->getTexGenNum();
            if (stages > 0) tev->setTevStageNum(stages - 1);
            if (gens > 0) texGen->setTexGenNum(gens - 1);
            *material->getPEBlock()->getAlphaComp() = saved[i].alpha;
        }
    }
};

void draw_puppet_shadow(daAlink_c* alink) {
    if (pup().model == nullptr) return;
    dBgS_GndChk gndChk;
    cXyz probe(pup().pos.x, pup().pos.y + 100.0f, pup().pos.z);
    gndChk.SetPos(&probe);
    const f32 groundY = dComIfG_Bgsp().GroundCross(&gndChk);
    if (groundY <= -G_CM3D_F_INF) return;
    const bool wolf = outfit_files(pup().outfit).isWolf;
    cXyz center(pup().pos.x, pup().pos.y + (wolf ? 60.0f : 100.0f), pup().pos.z);
    const f32 bodyY = pup().pos.y + (wolf ? 50.0f : 80.0f);
    pup().shadowKey = dComIfGd_setShadow(pup().shadowKey, 0, pup().model, &center, 800.0f,
        0.0f, bodyY, groundY, gndChk, &alink->tevStr, 0, 1.0f, dDlst_shadowControl_c::getSimpleTex());
}

void draw_one_puppet(daAlink_c* alink);
void draw_puppet_horse(daAlink_c* alink);
void horse_idle_tick();
void warm_puppet_warp(daAlink_c* alink);

void on_kankyo_exe_post(ModContext*, void*, void*, void*) {

    int best = -1;
    f32 bestDist = 0.0f;
    const daAlink_c* alink = daAlink_getAlinkActorClass();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (s_lanternFlame[i].ttl <= 0) {
            s_lanternFlame[i].power = 0.0f;
            continue;
        }
        --s_lanternFlame[i].ttl;
        if (alink == nullptr) continue;

        const f32 dist = (s_lanternFlame[i].pos - alink->current.pos).abs();
        if (best < 0 || dist < bestDist) {
            best = i;
            bestDist = dist;
        }
    }
    if (best < 0 || !features_puppet_lantern_light()) return;

    daPy_py_c* player = reinterpret_cast<daPy_py_c*>(dComIfGp_getPlayer(0));
    if (player == nullptr || player->getKandelaarFlamePos() != nullptr) return;

    g_env_light.field_0x10a0 = s_lanternFlame[best].pos;
    dKy_shadow_mode_set(2);

    {
        const daAlinkHIO_huLight_c1& lm = daAlinkHIO_huLight_c0::m;
        PuppetLanternFlame& f = s_lanternFlame[best];
        cLib_chaseF(&f.power, lm.mPower, lm.mPower * 0.2f);

        if (f.power > 0.0f && !daPy_py_c::checkNowWolfEyeUp()) {
            GXColor col = {static_cast<u8>(lm.mColorR), static_cast<u8>(lm.mColorG),
                static_cast<u8>(lm.mColorB), 0xFF};
            cXyz at = f.pos;
            dKy_WolfEyeLight_set(&at, static_cast<f32>(lm.mXAngle), cM_sht2d(-f.angleY),
                (lm.mWidth * f.power) / lm.mPower, &col, f.power, lm.mAngleAttenuationType,
                lm.mDistanceAttenuationType);
        }
    }

    static int s_lastBest = -1;
    static int s_sayIn = 0;
    if (best != s_lastBest || --s_sayIn <= 0) {
        s_lastBest = best;
        s_sayIn = 300;
        coop_log::info("coop_mod: [LANTERN] player {}'s flame is lighting the room at "
                        "({:.0f}, {:.0f}, {:.0f})",
            best, s_lanternFlame[best].pos.x, s_lanternFlame[best].pos.y,
            s_lanternFlame[best].pos.z);
    }
}

void on_alink_draw_puppet_post(ModContext*, void*, void*, void*) {

    queue_boss_overlay_from_draw();

    squad_hud_queue();
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;

    const bool localInCutscene = local_in_hiding_event(alink);

    for (int i = 0; i < kMaxPuppets; ++i) {
        if (i == coop_net_local_id()) continue;
        PuppetScope scope(static_cast<uint8_t>(i));

        s_nametagDlst[s_pupId].mVisible = false;
        if (localInCutscene) continue;
        if (pup().state != 2 || pup().model == nullptr) continue;

        const int peerRoom = pup().peerRoom;
        const bool peerShown =
            !(peerRoom >= 0 && peerRoom < 64 && !dComIfGp_roomControl_checkRoomDisp(peerRoom));
        if (peerShown) {

            warm_puppet_warp(alink);
            draw_one_puppet(alink);
        }

        const bool riding = (pup().horse.flags & kHorseFlagRiding) != 0;
        const int horseRoom = pup().horse.room;
        const bool horseShown =
            riding ? peerShown
                   : !(horseRoom >= 0 && horseRoom < 64 &&
                         !dComIfGp_roomControl_checkRoomDisp(horseRoom));
        if (horseShown) draw_puppet_horse(alink);
    }
}

const f32 kWarpWarmScale = 0.0001f;
const f32 kWarpWarmDepth = -100000.0f;

const int kWarpWarmDelay = 90;

const int kWarpWarmStagger = 45;

const int kWarpWarmFrames = 3;

void warm_puppet_warp(daAlink_c* alink) {
    if (pup().warpWarmDone || pup().model == nullptr) return;
    if (!features_puppet_warp_warm()) return;
    const int due = kWarpWarmDelay + kWarpWarmStagger * static_cast<int>(coop_net_local_id());
    if (++pup().warpWarmTicks < due) return;

    if (pup().warpWarmFrames == 0) {
        coop_log::info("coop_mod: [WARP] warming the dissolve shader on player {}'s models - if "
                        "this is the last line in the log, the FATAL is in that path",
            static_cast<int>(s_pupId));
    }

    Mtx hidden;
    mDoMtx_identity(hidden);
    hidden[0][0] = kWarpWarmScale;
    hidden[1][1] = kWarpWarmScale;
    hidden[2][2] = kWarpWarmScale;
    hidden[1][3] = kWarpWarmDepth;

    {
        PuppetWarpScope warp;
        warp.apply(pup().model);
        warp.apply(pup().faceModel);
        warp.apply(pup().hatModel);
        warp.apply(pup().handsModel);
        warp.apply(pup().bootModels[0]);
        warp.apply(pup().swordModel);
        warp.apply(pup().sheathModel);
        warp.apply(pup().shieldModel);

        J3DModel* const models[] = {pup().model, pup().faceModel, pup().hatModel,
            pup().handsModel, pup().bootModels[0], pup().swordModel, pup().sheathModel,
            pup().shieldModel};
        for (J3DModel* model : models) {
            if (model == nullptr) continue;

            PuppetGuard guard;
            const bool guarded = puppet_guard_begin(model, guard);
            Mtx at;
            mDoMtx_copy(hidden, at);
            renderModelAtMtx(model, at, nullptr);
            if (guarded) puppet_guard_end(guard);
        }
    }

    if (++pup().warpWarmFrames >= kWarpWarmFrames) {
        pup().warpWarmDone = true;
        coop_log::info("coop_mod: [WARP] player {} warmed clean over {} frames - the dissolve "
                        "path survived being exercised",
            static_cast<int>(s_pupId), kWarpWarmFrames);
    }
}

J3DModel* midna_model(int part, bool solid) {
    if (solid) {
        if (part >= 4) return nullptr;
        if (pup().midnaSolid[part] == nullptr && !pup().midnaSolidFailed) {
            const cXyz unit(1.0f, 1.0f, 1.0f);
            const char* arc = outfit_files(pup().outfit).arc;
            for (int i = 0; i < 4; ++i) {
                if (pup().midnaSolid[i] == nullptr) {
                    pup().midnaSolid[i] = loadBmdFromArc(arc, kMidnaSolidFiles[i], unit);
                }
            }
            for (int i = 0; i < 4; ++i) {
                if (pup().midnaSolid[i] == nullptr) {
                    pup().midnaSolidFailed = true;
                    coop_log::warn("coop_mod: [MIDNA] could not build her solid '{}' from '{}' - "
                                    "not drawing her solid form for player {}",
                        kMidnaSolidFiles[i], arc, static_cast<int>(s_pupId));
                    break;
                }
            }
        }
        return pup().midnaSolidFailed ? nullptr : pup().midnaSolid[part];
    }

    if (part >= 5) return nullptr;
    if (pup().midnaShadow[part] == nullptr && !pup().midnaShadowFailed) {
        const cXyz unit(1.0f, 1.0f, 1.0f);
        for (int i = 0; i < 5; ++i) {
            if (pup().midnaShadow[i] == nullptr) {
                pup().midnaShadow[i] = loadBmdFromArcIdx(kMidnaArc, kMidnaShadowRes[i], unit);
            }
        }
        for (int i = 0; i < 5; ++i) {
            if (pup().midnaShadow[i] == nullptr) {
                pup().midnaShadowFailed = true;
                coop_log::warn("coop_mod: [MIDNA] could not build her shadow part {} (res {}) - "
                                "not drawing her shadow form for player {}",
                    i, kMidnaShadowRes[i], static_cast<int>(s_pupId));
                break;
            }
        }
        if (!pup().midnaShadowFailed) {

            for (int i = 0; i < 4; ++i) {
                if (pup().midnaInvReady[i]) continue;
                pup().midnaInv[i].mpPackets = nullptr;
                pup().midnaInv[i].mModel = nullptr;
                if (pup().midnaInv[i].create(pup().midnaShadow[i], 1)) {
                    pup().midnaInvReady[i] = true;
                } else {
                    pup().midnaShadowFailed = true;
                    coop_log::warn("coop_mod: [MIDNA] invisible-model setup failed for part {}", i);
                    break;
                }
            }
        }
    }
    return pup().midnaShadowFailed ? nullptr : pup().midnaShadow[part];
}

void unpack_mtx12(const float* in, Mtx out) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) out[r][c] = in[r * 4 + c];
    }
}

void midna_world_joints(const MidnaSnapshot& snap, MtxP base, Mtx out[kMidnaJoints]) {
    for (int j = 0; j < kMidnaJoints; ++j) {
        const MidnaJointSnapshot& in = snap.joints[j];
        Mtx rel;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                rel[r][c] = static_cast<f32>(in.rot[r * 3 + c]) / kMidnaRotFixed;
            }
            rel[r][3] = in.pos[r];
        }
        mDoMtx_concat(base, rel, out[j]);
    }
}

struct MidnaShapeState {
    J3DShape* shape;
    bool hidden;
};
const int kMidnaMaxShapeEdits = 16;
struct MidnaShapeEdits {
    MidnaShapeState saved[kMidnaMaxShapeEdits];
    int count = 0;

    void set(J3DModel* model, u16 material, bool show) {
        if (model == nullptr || count >= kMidnaMaxShapeEdits) return;
        J3DModelData* data = model->getModelData();
        if (data == nullptr || material >= data->getMaterialNum()) return;
        J3DMaterial* mat = data->getMaterialNodePointer(material);
        if (mat == nullptr || mat->getShape() == nullptr) return;
        J3DShape* shape = mat->getShape();
        saved[count].shape = shape;
        saved[count].hidden = shape->checkFlag(J3DShpFlag_Visible);
        ++count;
        if (show) shape->show();
        else shape->hide();
    }

    void restore() {

        for (int i = count - 1; i >= 0; --i) {
            if (saved[i].hidden) saved[i].shape->hide();
            else saved[i].shape->show();
        }
        count = 0;
    }
};

struct MidnaEyeAnmGuard {
    J3DMaterial* mat[2] = {};
    J3DMaterialAnm* anm[2] = {};

    void begin(J3DModel* body) {
        J3DModelData* data = body->getModelData();
        for (int i = 0; i < 2; ++i) {
            const u16 idx = static_cast<u16>(2 + i);
            if (data == nullptr || idx >= data->getMaterialNum()) continue;
            mat[i] = data->getMaterialNodePointer(idx);
            if (mat[i] == nullptr) continue;
            anm[i] = mat[i]->getMaterialAnm();
            mat[i]->setMaterialAnm(nullptr);
        }
    }

    void end() {
        for (int i = 0; i < 2; ++i) {
            if (mat[i] != nullptr) mat[i]->setMaterialAnm(anm[i]);
        }
    }
};

const Mtx* s_midnaPoseOverride[kMidnaJoints] = {};

int midna_pose_callback(J3DJoint* joint, int phase) {
    if (phase != 0) return 1;
    const u16 j = joint->getJntNo();
    if (j >= kMidnaJoints || s_midnaPoseOverride[j] == nullptr) return 1;
    J3DModel* model = j3dSys.getModel();
    if (model == nullptr) return 1;
    Mtx m;
    mDoMtx_copy(*s_midnaPoseOverride[j], m);
    model->setAnmMtx(j, m);
    cMtx_copy(m, J3DSys::mCurrentMtx);
    return 1;
}

void midna_pose_ex(J3DModel* model, MtxP base, f32 scale, const Mtx* const* overrides) {
    model->setBaseScale(cXyz(scale, scale, scale));
    Mtx b;
    mDoMtx_copy(base, b);
    model->setBaseTRMtx(b);
    J3DModelData* data = model->getModelData();
    const u16 n = data->getJointNum() < kMidnaJoints ? data->getJointNum() : kMidnaJoints;
    bool any = false;
    for (u16 j = 0; j < kMidnaJoints; ++j) {
        s_midnaPoseOverride[j] = (overrides != nullptr && j < n) ? overrides[j] : nullptr;
        if (s_midnaPoseOverride[j] != nullptr) any = true;
    }
    if (any) {
        for (u16 j = 0; j < n; ++j) {
            if (s_midnaPoseOverride[j] != nullptr) {
                data->getJointNodePointer(j)->setCallBack(midna_pose_callback);
            }
        }
    }
    model->calc();
    if (any) {
        for (u16 j = 0; j < n; ++j) {
            if (s_midnaPoseOverride[j] != nullptr) data->getJointNodePointer(j)->setCallBack(nullptr);
            s_midnaPoseOverride[j] = nullptr;
        }
    }
}

void midna_pose(J3DModel* model, MtxP base, f32 scale, const Mtx* joints) {
    if (joints == nullptr) {
        midna_pose_ex(model, base, scale, nullptr);
        return;
    }
    const Mtx* ptrs[kMidnaJoints];
    for (int j = 0; j < kMidnaJoints; ++j) ptrs[j] = &joints[j];
    midna_pose_ex(model, base, scale, ptrs);
}

void midna_pose_hands(J3DModel* hands, MtxP base, const Mtx joints[kMidnaJoints]) {
    const Mtx* ptrs[kMidnaJoints] = {};
    ptrs[1] = &joints[kMidnaJntHandL];
    ptrs[2] = &joints[kMidnaJntHandR];
    midna_pose_ex(hands, base, 1.0f, ptrs);
}

void midna_set_hair_colors(J3DModel* model, const MidnaSnapshot& snap, bool isHairhand) {
    J3DModelData* data = model->getModelData();
    if (data == nullptr) return;
    J3DGXColorS10 c1;
    c1.r = snap.hairColor[0];
    c1.g = snap.hairColor[1];
    c1.b = snap.hairColor[2];
    c1.a = snap.hairColor[3];
    J3DGXColor k1;
    k1.r = snap.hairK1[0];
    k1.g = snap.hairK1[1];
    k1.b = snap.hairK1[2];
    k1.a = snap.hairK1[3];
    J3DGXColor k2;
    k2.r = snap.hairK2[0];
    k2.g = snap.hairK2[1];
    k2.b = snap.hairK2[2];
    k2.a = snap.hairK2[3];
    if (isHairhand) {

        const u16 n = data->getMaterialNum() < 3 ? data->getMaterialNum() : 3;
        for (u16 i = 0; i < n; ++i) {
            J3DMaterial* m = data->getMaterialNodePointer(i);
            if (m == nullptr) continue;
            m->setTevColor(1, &c1);
            m->setTevKColor(1, &k1);
            m->setTevKColor(2, &k2);
        }
    } else if (data->getMaterialNum() > 4) {

        J3DMaterial* m = data->getMaterialNodePointer(4);
        if (m != nullptr) {
            m->setTevColor(1, &c1);
            m->setTevKColor(1, &k1);
        }
    }
}

void draw_midna_solid(daAlink_c* alink, const MidnaSnapshot& snap, MtxP base,
    const Mtx joints[kMidnaJoints]) {
    J3DModel* body = midna_model(kMidnaPartBody, true);
    J3DModel* hands = midna_model(kMidnaPartHands, true);
    J3DModel* mask = midna_model(kMidnaPartMask, true);
    J3DModel* hair = midna_model(kMidnaPartHair, true);
    if (body == nullptr || hands == nullptr || mask == nullptr || hair == nullptr) return;

    dKy_tevstr_c* tev = &alink->tevStr;

    {
        PuppetGuard guard;
        if (puppet_guard_begin(body, guard)) {
            midna_pose(body, base, snap.baseScale, joints);
            midna_set_hair_colors(body, snap, false);
            g_env_light.setLightTevColorType_MAJI(body, tev);
            MidnaShapeEdits vis;

            vis.set(body, 6, snap.leftHand == 0xFE);
            vis.set(body, 7, snap.rightHand == 0xFE);
            MidnaEyeAnmGuard eyes;
            eyes.begin(body);
            mDoExt_modelEntryDL(body);
            eyes.end();
            vis.restore();
            puppet_guard_end(guard);
        }
    }

    {
        PuppetGuard guard;
        if (puppet_guard_begin(hands, guard)) {
            midna_pose_hands(hands, base, joints);
            g_env_light.setLightTevColorType_MAJI(hands, tev);
            MidnaShapeEdits vis;
            const u16 n = hands->getModelData()->getMaterialNum() < 4
                              ? hands->getModelData()->getMaterialNum()
                              : 4;
            for (u16 i = 0; i < n; ++i) {
                vis.set(hands, i, snap.leftHand == i || snap.rightHand == i);
            }
            mDoExt_modelEntryDL(hands);
            vis.restore();
            puppet_guard_end(guard);
        }
    }

    if (snap.flags & kMidnaFlagHairhand) {
        PuppetGuard guard;
        if (puppet_guard_begin(hair, guard)) {
            Mtx rel;
            unpack_mtx12(snap.hairMtx, rel);
            Mtx world;
            mDoMtx_concat(base, rel, world);
            midna_pose(hair, world, 1.0f, nullptr);
            midna_set_hair_colors(hair, snap, true);
            g_env_light.setLightTevColorType_MAJI(hair, tev);
            MidnaShapeEdits vis;
            for (u16 i = 0; i < 3; ++i) vis.set(hair, i, snap.hairShape == i);
            mDoExt_modelEntryDL(hair);
            vis.restore();
            puppet_guard_end(guard);
        }
    }

    if (snap.flags & kMidnaFlagMask) {
        PuppetGuard guard;
        if (puppet_guard_begin(mask, guard)) {
            Mtx head;
            mDoMtx_copy(joints[kMidnaJntHead], head);
            midna_pose(mask, head, 1.0f, nullptr);
            g_env_light.setLightTevColorType_MAJI(mask, tev);
            mDoExt_modelEntryDL(mask);
            puppet_guard_end(guard);
        }
    }
}

void draw_midna_shadow(daAlink_c* alink, const MidnaSnapshot& snap, MtxP base,
    const Mtx joints[kMidnaJoints]) {
    J3DModel* body = midna_model(kMidnaPartBody, false);
    J3DModel* hands = midna_model(kMidnaPartHands, false);
    J3DModel* mask = midna_model(kMidnaPartMask, false);
    J3DModel* hair = midna_model(kMidnaPartHair, false);
    J3DModel* gokou = midna_model(kMidnaPartGokou, false);
    if (body == nullptr || hands == nullptr || mask == nullptr || hair == nullptr ||
        gokou == nullptr)
    {
        return;
    }

    dKy_tevstr_c tev = alink->tevStr;
    if (snap.flags & kMidnaFlagTevColor) {
        tev.TevColor.r = snap.tevColor[0];
        tev.TevColor.g = snap.tevColor[1];
        tev.TevColor.b = snap.tevColor[2];
        tev.TevColor.a = snap.tevColor[3];
    }

    {
        PuppetGuard guard;
        if (puppet_guard_begin(gokou, guard)) {
            Mtx at;
            mDoMtx_trans(at, joints[kMidnaJntBackbone1][0][3],
                joints[kMidnaJntBackbone1][1][3] + 20.0f, joints[kMidnaJntBackbone1][2][3]);
            midna_pose(gokou, at, 1.0f, nullptr);
            g_env_light.setLightTevColorType_MAJI(gokou, &tev);
            mDoExt_modelEntryDL(gokou);
            puppet_guard_end(guard);
        }
    }

    cXyz key(joints[0][0][3], joints[0][1][3], joints[0][2][3]);
    MTXMultVec(dComIfGd_getViewMtx(), &key, &key);
    key.z -= 200.0f;
    MTXMultVec(dComIfGd_getInvViewMtx(), &key, &key);

    struct Part {
        J3DModel* model;
        int inv;
        bool posed;
        bool draw;
    };
    const Part parts[4] = {
        {body, kMidnaPartBody, true, true},
        {mask, kMidnaPartMask, false, (snap.flags & kMidnaFlagMask) != 0},
        {hands, kMidnaPartHands, false, true},
        {hair, kMidnaPartHair, false, (snap.flags & kMidnaFlagHairhand) != 0},
    };
    for (const Part& part : parts) {
        if (!part.draw || !pup().midnaInvReady[part.inv]) continue;
        PuppetGuard guard;
        if (!puppet_guard_begin(part.model, guard)) continue;
        if (part.posed) {
            midna_pose(part.model, base, snap.baseScale, joints);
        } else if (part.model == mask) {
            Mtx head;
            mDoMtx_copy(joints[kMidnaJntHead], head);
            midna_pose(part.model, head, 1.0f, nullptr);
        } else if (part.model == hands) {
            midna_pose_hands(part.model, base, joints);
        } else {
            Mtx rel;
            unpack_mtx12(snap.hairMtx, rel);
            Mtx world;
            mDoMtx_concat(base, rel, world);
            midna_pose(part.model, world, 1.0f, nullptr);
        }
        g_env_light.setLightTevColorType_MAJI(part.model, &tev);
        pup().midnaInv[part.inv].entryDL(&key);
        puppet_guard_end(guard);
    }
}

void draw_puppet_midna(daAlink_c* alink) {
    if (!pup().midnaActive || pup().model == nullptr) return;
    if (!features_puppet_midna()) return;
    const MidnaSnapshot& snap = pup().midna;
    const bool solid = snap.mode == kMidnaModeSolid;

    if (solid && !outfit_files(pup().outfit).isWolf) return;
    if (!solid && snap.mode != kMidnaModeShadow) return;

    Mtx rel;
    unpack_mtx12(snap.baseMtx, rel);
    Mtx base;
    if (snap.flags & kMidnaFlagWorldBase) {
        mDoMtx_copy(rel, base);

        daMidna_c* mine = daPy_py_c::getMidnaActor();
        daAlink_c* me = daAlink_getAlinkActorClass();
        const bool mineRiding = me != nullptr && mine != nullptr && me->checkWolf() &&
                                !mine->checkStateFlg0(daMidna_c::FLG0_WOLF_NO_POS);
        if (mine != nullptr && me != nullptr && !mineRiding && mine->mpShadowModel != nullptr &&
            !mine->checkNoDrawState()) {
            MtxP m = mine->mpShadowModel->getBaseTRMtx();
            const f32 dx = m[0][3] - base[0][3];
            const f32 dy = m[1][3] - base[1][3];
            const f32 dz = m[2][3] - base[2][3];
            const f32 kSameMidna = 100.0f;
            if (dx * dx + dy * dy + dz * dz < kSameMidna * kSameMidna) return;
        }
    } else {
        mDoMtx_concat(pup().model->getBaseTRMtx(), rel, base);
    }
    Mtx joints[kMidnaJoints];
    midna_world_joints(snap, base, joints);

    J3DDrawBuffer* saved0 = j3dSys.getDrawBuffer(0);
    J3DDrawBuffer* saved1 = j3dSys.getDrawBuffer(1);
    dComIfGd_setListDark();
    if (solid) {
        draw_midna_solid(alink, snap, base, joints);
    } else {
        draw_midna_shadow(alink, snap, base, joints);
    }
    j3dSys.setDrawBuffer(saved0, 0);
    j3dSys.setDrawBuffer(saved1, 1);
}

const Mtx* s_horsePose[kHorseJoints] = {};
const int kHorseStaleTicks = 20;

const int kHorseStandingStaleTicks = 45;

int horse_stale_limit() {
    return (pup().horse.flags & kHorseFlagIdle) != 0 ? kHorseStandingStaleTicks : kHorseStaleTicks;
}

int horse_pose_callback(J3DJoint* joint, int phase) {
    if (phase != 0) return 1;
    const u16 j = joint->getJntNo();
    if (j >= kHorseJoints || s_horsePose[j] == nullptr) return 1;
    J3DModel* model = j3dSys.getModel();
    if (model == nullptr) return 1;
    Mtx m;
    mDoMtx_copy(*s_horsePose[j], m);
    model->setAnmMtx(j, m);
    cMtx_copy(m, J3DSys::mCurrentMtx);
    return 1;
}

void horse_joint_mtx(const HorseJointSnapshot& in, Mtx out) {
    f32 x = in.q[0] / kHorseQuatScale;
    f32 y = in.q[1] / kHorseQuatScale;
    f32 z = in.q[2] / kHorseQuatScale;
    f32 w = in.q[3] / kHorseQuatScale;
    const f32 len = std::sqrt(x * x + y * y + z * z + w * w);
    if (len > 0.0001f) {
        x /= len;
        y /= len;
        z /= len;
        w /= len;
    } else {
        w = 1.0f;
    }
    out[0][0] = 1.0f - 2.0f * (y * y + z * z);
    out[0][1] = 2.0f * (x * y - z * w);
    out[0][2] = 2.0f * (x * z + y * w);
    out[1][0] = 2.0f * (x * y + z * w);
    out[1][1] = 1.0f - 2.0f * (x * x + z * z);
    out[1][2] = 2.0f * (y * z - x * w);
    out[2][0] = 2.0f * (x * z - y * w);
    out[2][1] = 2.0f * (y * z + x * w);
    out[2][2] = 1.0f - 2.0f * (x * x + y * y);
    for (int r = 0; r < 3; ++r) out[r][3] = in.p[r] / kHorsePosScale;
}

const u16 kHorseIdleAnm = 27;

void horse_idle_joint(J3DJoint* joint, J3DAnmTransform* anm, const Mtx parent, Mtx* out, int count) {
    for (; joint != nullptr; joint = joint->getYounger()) {
        const u16 j = joint->getJntNo();
        if (j >= count) continue;
        J3DTransformInfo info = joint->getTransformInfo();
        anm->getTransform(j, &info);
        Mtx local;
        J3DGetTranslateRotateMtx(info, local);
        for (int r = 0; r < 3; ++r) {
            local[r][0] *= info.mScale.x;
            local[r][1] *= info.mScale.y;
            local[r][2] *= info.mScale.z;
        }
        mDoMtx_concat(parent, local, out[j]);
        horse_idle_joint(joint->getChild(), anm, out[j], out, count);
    }
}

J3DAnmTransform* horse_idle_anm(u16 want, u16* which) {
    *which = want < 0x100 ? want : kHorseIdleAnm;
    auto* anm = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes("Horse", *which));
    if (anm == nullptr && *which != kHorseIdleAnm) {
        *which = kHorseIdleAnm;
        anm = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes("Horse", *which));
    }
    return anm;
}

void horse_idle_tick() {
    const HorseSnapshot& snap = pup().horse;
    if ((snap.flags & kHorseFlagIdle) == 0 || pup().horseAge > horse_stale_limit()) return;
    u16 which = 0;
    J3DAnmTransform* anm = horse_idle_anm(snap.idleAnm, &which);
    if (anm == nullptr) return;
    const f32 end = anm->getFrameMax();
    if (end <= 0.0f) return;
    const f32 rate = std::isfinite(snap.idleRate) ? snap.idleRate : 1.0f;
    f32 frame = pup().horseIdleFrame + rate;
    const bool fresh = snap.seq != pup().horseIdleSeq;
    if (which != pup().horseIdleAnm) {
        pup().horseIdleAnm = which;
        frame = snap.idleFrame;
    } else if (fresh && std::isfinite(snap.idleFrame)) {
        const f32 diff = snap.idleFrame - frame;
        frame = (diff > 4.0f || diff < -4.0f) ? snap.idleFrame : frame + diff * 0.25f;
    }
    pup().horseIdleSeq = snap.seq;
    if (!std::isfinite(frame)) frame = 0.0f;

    while (frame >= end) frame -= end;
    while (frame < 0.0f) frame += end;
    pup().horseIdleFrame = frame;
}

bool horse_idle_pose(J3DModel* model, const Mtx base, Mtx* out, int count,
    const HorseSnapshot& snap) {
    u16 which = 0;
    J3DAnmTransform* anm = horse_idle_anm(snap.idleAnm, &which);
    J3DModelData* data = model->getModelData();
    if (anm == nullptr || data == nullptr || data->getJointNum() == 0) return false;
    if (which != pup().horseIdleAnm) return false;
    anm->setFrame(pup().horseIdleFrame);
    horse_idle_joint(data->getJointNodePointer(0), anm, base, out, count);
    return true;
}

void horse_blend_joint(int j, Mtx out) {
    const HorseJointSnapshot& b = pup().horse.joints[j];
    if (!pup().horsePrevValid) {
        horse_joint_mtx(b, out);
        return;
    }
    f32 t = static_cast<f32>(pup().horseAge) / static_cast<f32>(pup().horseGap);
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    const HorseJointSnapshot& a = pup().horsePrev.joints[j];
    f32 dot = 0.0f;
    for (int k = 0; k < 4; ++k) dot += static_cast<f32>(a.q[k]) * static_cast<f32>(b.q[k]);
    const f32 sign = dot < 0.0f ? -1.0f : 1.0f;
    HorseJointSnapshot mix;
    for (int k = 0; k < 4; ++k) {
        const f32 v = a.q[k] * sign * (1.0f - t) + b.q[k] * t;
        mix.q[k] = static_cast<int16_t>(v > 32767.0f ? 32767.0f : (v < -32767.0f ? -32767.0f : v));
    }
    for (int k = 0; k < 3; ++k) {
        mix.p[k] = static_cast<int16_t>(a.p[k] * (1.0f - t) + b.p[k] * t);
    }
    horse_joint_mtx(mix, out);
}

void draw_puppet_horse(daAlink_c* alink) {
    const HorseSnapshot& snap = pup().horse;
    if (pup().horseAge > horse_stale_limit() || snap.jointCount == 0 ||
        snap.jointCount > kHorseJoints) {
        return;
    }
    if (pup().model == nullptr) return;
    if (!pup().horseArcHeld) {

        if (loadObjectArchive("Horse") != 0) return;
        pup().horseArcHeld = true;
    }
    if (pup().horseModel == nullptr) {
        pup().horseModel = loadBmdFromArcIdx("Horse", 0x26, cXyz(1.0f, 1.0f, 1.0f));
        if (pup().horseModel == nullptr) return;
    }
    J3DModel* model = pup().horseModel;

    Mtx rel;
    unpack_mtx12(snap.baseMtx, rel);
    Mtx base;
    if ((snap.flags & kHorseFlagRiding) != 0) {

        mDoMtx_concat(pup().model->getBaseTRMtx(), rel, base);
    } else {
        mDoMtx_copy(rel, base);
    }
    static Mtx joints[kHorseJoints];
    J3DModelData* horseData = model->getModelData();
    if (horseData == nullptr) return;
    const u16 modelJoints = horseData->getJointNum();
    const int idleCount = modelJoints < kHorseJoints ? modelJoints : kHorseJoints;
    const bool idle = (snap.flags & kHorseFlagIdle) != 0 &&
                      horse_idle_pose(model, base, joints, idleCount, snap);
    for (int j = 0; j < kHorseJoints; ++j) {
        if (idle) {
            s_horsePose[j] = j < idleCount ? &joints[j] : nullptr;
        } else if (j < snap.jointCount) {
            Mtx local;
            horse_blend_joint(j, local);
            mDoMtx_concat(base, local, joints[j]);
            s_horsePose[j] = &joints[j];
        } else {
            s_horsePose[j] = nullptr;
        }
    }

    PuppetGuard guard;
    if (!puppet_guard_begin(model, guard)) return;
    model->setBaseScale(cXyz(1.0f, 1.0f, 1.0f));
    model->setBaseTRMtx(base);
    J3DModelData* data = model->getModelData();
    const u16 n = data->getJointNum() < kHorseJoints ? data->getJointNum() : kHorseJoints;
    for (u16 j = 0; j < n; ++j) {
        if (s_horsePose[j] != nullptr) data->getJointNodePointer(j)->setCallBack(horse_pose_callback);
    }
    model->calc();
    for (u16 j = 0; j < n; ++j) {
        if (s_horsePose[j] != nullptr) data->getJointNodePointer(j)->setCallBack(nullptr);
        s_horsePose[j] = nullptr;
    }
    g_env_light.setLightTevColorType_MAJI(model, &alink->tevStr);
    mDoExt_modelEntryDL(model);
    puppet_guard_end(guard);

    {
        const cXyz feet(base[0][3], base[1][3], base[2][3]);
        dBgS_GndChk gndChk;
        cXyz probe(feet.x, feet.y + 100.0f, feet.z);
        gndChk.SetPos(&probe);
        const f32 groundY = dComIfG_Bgsp().GroundCross(&gndChk);
        if (groundY > -G_CM3D_F_INF) {
            cXyz center(feet.x, feet.y + 100.0f, feet.z);
            pup().horseShadowKey = dComIfGd_setShadow(pup().horseShadowKey, 0, model, &center,
                1000.0f, 0.0f, feet.y, groundY, gndChk, &alink->tevStr, 0, 1.0f,
                dDlst_shadowControl_c::getSimpleTex());
        }
    }

    if (snap.reinCount < 2 || snap.reinCount > kHorseReinPoints) return;
    if (pup().horseReins == nullptr) {
        auto* texture = static_cast<ResTIMG*>(dComIfG_getObjectRes("Horse", 0x2C));
        if (texture == nullptr) return;
        JKRHeap* heap = mDoExt_getZeldaHeap();
        JKRHeap* previous = heap != nullptr ? heap->becomeCurrentHeap() : nullptr;
        auto* line = JKR_NEW mDoExt_3DlineMat1_c();
        const bool ok = line != nullptr && line->init(1, kHorseReinPoints, texture, 0) != 0;
        if (previous != nullptr) previous->becomeCurrentHeap();
        if (!ok) return;
        pup().horseReins = line;
    }
    cXyz* points = pup().horseReins->getPos(0);
    for (int i = 0; i < snap.reinCount; ++i) {
        const cXyz local(snap.reins[i][0] / kHorsePosScale, snap.reins[i][1] / kHorsePosScale,
            snap.reins[i][2] / kHorsePosScale);
        mDoMtx_multVec(base, &local, &points[i]);
    }
    static GXColor reinColor = {0x00, 0x00, 0x00, 0xFF};
    pup().horseReins->update(snap.reinCount, 1.5f, reinColor, 0, &alink->tevStr);
    dComIfGd_set3DlineMat(pup().horseReins);
}

void draw_one_puppet(daAlink_c* alink) {

    if (alink->mClothesChangeWaitTimer != 0) return;

    PuppetWarpScope warp;
    if (pup().warpOn && features_puppet_warp_fx()) {
        warp.apply(pup().model);
        warp.apply(pup().faceModel);
        warp.apply(pup().hatModel);
        warp.apply(pup().handsModel);
        warp.apply(pup().bootModels[0]);
        warp.apply(pup().swordModel);
        warp.apply(pup().sheathModel);
        warp.apply(pup().shieldModel);
    }

    PuppetGuard bodyGuard;
    const bool guardOk = puppet_guard_begin(pup().model, bodyGuard);
    if (s_diagDrawCount < 5) {
        ++s_diagDrawCount;
        coop_log::info(
            "coop_mod: [DIAG] draw #{} guardOk={} puppetPos=({},{},{}) puppetAngle=({},{},{}) "
            "playerPos=({},{},{})",
            s_diagDrawCount, static_cast<int>(guardOk), pup().pos.x, pup().pos.y,
            pup().pos.z, pup().angle.x, pup().angle.y, pup().angle.z,
            alink->current.pos.x, alink->current.pos.y, alink->current.pos.z);
    }
    if (!guardOk) return;

    {
        J3DModelData* bodyData = pup().model->getModelData();
        const u16 jointNum = (bodyData != nullptr) ? bodyData->getJointNum() : 0;
        const bool wolf = outfit_files(pup().outfit).isWolf;
        auto hook_joint = [&](int j) {
            if (j < jointNum) bodyData->getJointNodePointer(j)->setCallBack(puppet_body_aim_callback);
        };
        hook_joint(kUnderRootJoint);
        hook_joint(wolf ? kWolfFootCallbackJoint : kHumanFootCallbackJoint);
        if (!wolf) {
            hook_joint(kUpperBodyRootJoint);
            hook_joint(kBackbone2Joint);
        }
    }

    force_warp_off_all_materials(pup().model->getModelData());
    force_alpha_always_pass(pup().model->getModelData());
    BodyHandGuard bodyHands;
    const bool bodyHandsApplied =
        body_hand_shapes_apply(pup().model, pup().handL, pup().handR, bodyHands);
    render_puppet_body_with_upper_split(pup().model, pup().pos, pup().angle, alink);
    if (bodyHandsApplied) body_hand_shapes_restore(bodyHands);
    if (s_poseDiagLogs < 30 &&
        (pup().rootClearMask != 0 || pup().footAngles[0][0] != 0 ||
         pup().footAngles[0][1] != 0 || pup().footAngles[1][0] != 0))
    {
        ++s_poseDiagLogs;
        coop_log::trace("coop_mod: [POSEDIAG-RX] cbJoints={:#x} wolf={} rootMask={:#x} "
                        "clear=({},{},{}) foot0=({},{},{}) foot1=({},{},{})",
            s_poseCbSeen, static_cast<int>(outfit_files(pup().outfit).isWolf),
            pup().rootClearMask, pup().rootClearX, pup().rootClearY, pup().rootClearZ,
            pup().footAngles[0][0], pup().footAngles[0][1], pup().footAngles[0][2],
            pup().footAngles[1][0], pup().footAngles[1][1], pup().footAngles[1][2]);
    }
    s_poseCbSeen = 0;
    puppet_guard_end(bodyGuard);

    draw_puppet_equipment();
    draw_puppet_attachments(alink);
    draw_puppet_shadow(alink);
    queue_puppet_nametag(alink);

    if (pup().faceModel != nullptr) {

        PuppetGuard faceGuard;
        if (puppet_guard_begin(pup().faceModel, faceGuard)) {
            force_warp_off_all_materials(pup().faceModel->getModelData());
            force_alpha_always_pass(pup().faceModel->getModelData());

            renderModelAtMtx(pup().faceModel, pup().model->getAnmMtx(4), nullptr);
            puppet_guard_end(faceGuard);
        }
    }
    if (pup().hatModel != nullptr) {

        const bool hasSway = outfit_files(pup().outfit).hasKmdlHatTail;
        PuppetGuard hatGuard;
        const bool guardOk = puppet_guard_begin(pup().hatModel, hatGuard);
        if (guardOk && hasSway) set_hat_tail_callbacks(pup().hatModel);
        if (guardOk) {
            force_warp_off_all_materials(pup().hatModel->getModelData());
            force_alpha_always_pass(pup().hatModel->getModelData());
            renderModelAtMtx(pup().hatModel, pup().model->getAnmMtx(4), nullptr);
            puppet_guard_end(hatGuard);
        }
    }

    if (pup().bootsVisible && pup().bootModels[0] != nullptr &&
        pup().bootModels[1] != nullptr)
    {
        static const u16 kBootJoints[2][3] = {{0x13, 0x14, 0x15}, {0x18, 0x19, 0x1A}};
        J3DModelData* legData = pup().model->getModelData();
        if (legData != nullptr && 0x1A < legData->getJointNum()) {
            for (int i = 0; i < 2; ++i) {
                J3DModel* boot = pup().bootModels[i];
                PuppetGuard bootGuard;
                if (!puppet_guard_begin(boot, bootGuard)) continue;
                ShapeVisGuard bootVis;
                const bool bootVisApplied = shape_vis_force_show(boot, bootVis);
                boot->setBaseTRMtx(pup().model->getBaseTRMtx());
                boot->calc();
                for (int j = 0; j < 3; ++j) {
                    if (i == 0) {
                        boot->setAnmMtx(j + 1, pup().model->getAnmMtx(kBootJoints[i][j]));
                    } else {
                        mDoMtx_stack_c::XrotS(-0x8000);
                        Mtx bootMtx;
                        mDoMtx_concat(pup().model->getAnmMtx(kBootJoints[i][j]),
                            mDoMtx_stack_c::get(), bootMtx);
                        boot->setAnmMtx(j + 1, bootMtx);
                    }
                }
                g_env_light.setLightTevColorType_MAJI(boot, &alink->tevStr);
                boot->calcMaterial();
                boot->diff();
                boot->entry();
                boot->viewCalc();
                if (bootVisApplied) shape_vis_restore(bootVis);
                puppet_guard_end(bootGuard);
            }
        }
    }

    if (pup().handsModel != nullptr) {

        PuppetGuard handsGuard;
        if (puppet_guard_begin(pup().handsModel, handsGuard)) {
            force_warp_off_all_materials(pup().handsModel->getModelData());
            force_alpha_always_pass(pup().handsModel->getModelData());
            pup().handsModel->setBaseTRMtx(pup().model->getBaseTRMtx());
            pup().handsModel->calc();
            pup().handsModel->setAnmMtx(1, pup().model->getAnmMtx(9));
            pup().handsModel->setAnmMtx(2, pup().model->getAnmMtx(0xE));
            g_env_light.setLightTevColorType_MAJI(pup().handsModel, &alink->tevStr);

            ShapeVisGuard handVis;
            const bool handVisApplied =
                hand_shapes_apply(pup().handsModel, pup().handL, pup().handR, handVis);
            pup().handsModel->calcMaterial();
            pup().handsModel->diff();
            pup().handsModel->entry();
            pup().handsModel->viewCalc();
            if (handVisApplied) shape_vis_restore(handVis);
            puppet_guard_end(handsGuard);
        }
    }

    draw_puppet_midna(alink);

}

}

void puppet_hook_init() {

    mods::hook::add_pre<PuppetAlinkExecuteHook>([](ModContext*, void*, void*, void*) -> HookAction {
        fx_owner_window(true);

        if (daAlink_c* alink = daAlink_getAlinkActorClass()) {
            if (alink->mShieldChangeWaitTimer != 0) puppet_hold_for_shield_swap(alink);
        }
        return HOOK_CONTINUE;
    });
    const ModResult execResult =
        mods::hook::add_post<PuppetAlinkExecuteHook>(on_alink_execute_puppet_post);
    const ModResult drawResult =
        mods::hook::add_post<PuppetAlinkDrawHook>(on_alink_draw_puppet_post);
    const ModResult kankyoResult = mods::hook::add_post<PuppetKankyoExeHook>(on_kankyo_exe_post);

    mods::hook::add_post<LocalAramBmdHook>(
        [](ModContext*, void* args, void* retval, void*) {
            J3DModelData** result = static_cast<J3DModelData**>(retval);
            if (result == nullptr || *result == nullptr || s_loadingPuppetModels) return;
            const u16 index = mods::arg<u16>(args, 1);
            remember_aram_original(index, *result);
            J3DModelData* mine = skins_local_aram_data(index);
            const char* file = skins_aram_file_for_index(index);
            if (mine == nullptr || !skin_fits_original(mine, *result, file)) return;
            *result = mine;
            breadcrumb2("local: held item (first draw)", file);
        });

    const ModResult aramResult = mods::hook::add_pre<LocalAramBmdHook>(
        [](ModContext*, void* args, void* retval, void*) -> HookAction {
            J3DModelData** result = static_cast<J3DModelData**>(retval);
            if (result == nullptr || s_loadingPuppetModels) return HOOK_CONTINUE;
            const u16 index = mods::arg<u16>(args, 1);
            J3DModelData* theirs = aram_original(index);
            if (theirs == nullptr) return HOOK_CONTINUE;
            J3DModelData* mine = skins_local_aram_data(index);
            const char* file = skins_aram_file_for_index(index);
            if (mine == nullptr || !skin_fits_original(mine, theirs, file)) return HOOK_CONTINUE;
            *result = mine;
            breadcrumb2("local: held item", file);
            return HOOK_SKIP_ORIGINAL;
        });
    coop_log::info("coop_mod: [SKIN] held-item hook {}",
        aramResult == MOD_OK ? "attached" : "FAILED - held items will stay the game's own");
    mods::hook::add_post<LocalSkinResByNameHook>(
        [](ModContext*, void* args, void* retval, void*) {
            void** result = static_cast<void**>(retval);
            if (result == nullptr || *result == nullptr) return;
            const char* resName = mods::arg<const char*>(args, 1);
            J3DModelData* mine = local_skin_for(mods::arg<const char*>(args, 0), resName);

            if (mine != nullptr &&
                skin_fits_original(mine, static_cast<J3DModelData*>(*result), resName)) {
                *result = mine;
            }
        });
    mods::hook::add_post<LocalSkinResByIndexHook>(
        [](ModContext*, void* args, void* retval, void*) {
            void** result = static_cast<void**>(retval);
            if (result == nullptr || *result == nullptr || s_loadingPuppetModels) return;

            const char* arc = mods::arg<const char*>(args, 0);
            if (arc == nullptr || std::strcmp(arc, "Wmdl") != 0) return;
            if (mods::arg<int>(args, 1) != dRes_INDEX_WMDL_BMD_WL_e) return;
            J3DModelData* mine = skins_local_part_data(kSkinOutfitWolf, kSkinPartBody);
            if (mine != nullptr) *result = mine;
        });
    const ModResult dtorResult = mods::hook::add_pre<PuppetAlinkDtorHook>(
        [](ModContext*, void*, void*, void*) -> HookAction {
            int released = 0;
            for (int i = 0; i < kMaxPuppets; ++i) {
                PuppetScope scope(static_cast<uint8_t>(i));
                if (pup().state != 0 || pup().shieldArc[0] != '\0') {
                    if (pup().state != 0) {
                        pup().respawnOutfit = pup().outfit;
                        pup().respawnPending = false;
                    }
                    release_puppet();
                    ++released;
                }
            }

            s_builtAgainstAlink = nullptr;
            s_builtAgainstArcHeap = nullptr;
            coop_log::info("coop_mod: [LINKDTOR] player actor going away - released {} puppet(s) "
                            "before its archives are deleted",
                released);
            return HOOK_CONTINUE;
        });
    coop_log::info("coop_mod: [DIAG] alink destructor hook={}", static_cast<int>(dtorResult));
    coop_log::info("coop_mod: [DIAG] puppet_hook_init: execHook={} drawHook={} kankyoHook={}",
        static_cast<int>(execResult), static_cast<int>(drawResult),
        static_cast<int>(kankyoResult));
}

bool puppet_hook_sword_mtx(uint8_t playerId, float out[3][4], bool* master) {
    if (playerId >= kMaxPuppets || playerId == coop_net_local_id()) return false;
    PuppetScope scope(playerId);
    if (pup().state != 2 || pup().swordModel == nullptr) return false;
    MtxP m = pup().swordModel->getBaseTRMtx();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) out[r][c] = m[r][c];
    }
    if (master != nullptr) *master = pup().swordId == kPuppetSwordMaster;
    return true;
}



#include "mod.hpp"
#include "models.hpp"
#include "print.hpp"
#include "util.hpp"

#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_mirror.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/J3DGraphAnimator/J3DAnimation.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphBase/J3DTransform.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JUtility/JUTNameTab.h"

#include <cstring>

extern const ConfigService* svc_config;

DEFINE_HOOK_SYMBOL("daAlink_c::setWolfItemMatrix", void(daAlink_c*), BeastWolfItemsHook);

namespace {

const char* const kArc = "B_mgn";
const char* const kModel = "mgn.bmd";
const char* const kStance = "mgn_wait.bck";
const char* const kGoldArc = "GWolf";
const char* const kGoldModel = "gw.bmd";

const int kLookBeast = 0;
const int kLookGold = 1;
const int kMaxJoints = 80;
const int kRigs = 6;
const f32 kSize = 0.6f;

const int kWolfBack = 2;
const int kWolfHead = 4;
const int kWolfMidnaSeat = 0x19;

bool s_learned = false;
int s_count = 0;
int s_parent[kMaxJoints];
int s_order[kMaxJoints];
Mtx s_refWorld[kMaxJoints];
Mtx s_refLocal[kMaxJoints];
Vec s_offset[kMaxJoints];
Vec s_hipRef = {0.0f, 1.0f, 0.0f};
int s_hips[2] = {-1, -1};
int s_nose = -1;

Vec s_seat = {0.0f, 0.0f, 0.0f};
int s_seatJoint = -1;

Vec s_back = {0.0f, 0.0f, 0.0f};
int s_backJoint = -1;

void rotation_only(const Mtx in, Mtx out) {
    cMtx_copy(in, out);
    out[0][3] = out[1][3] = out[2][3] = 0.0f;
}

int joint_named(J3DModelData* data, const char* name) {
    JUTNameTab* names = data->getJointName();
    if (names == nullptr) return -1;
    for (u16 i = 0; i < data->getJointNum(); ++i) {
        const char* n = names->getName(i);
        if (n != nullptr && std::strcmp(n, name) == 0) return i;
    }
    return -1;
}

void walk(J3DJoint* joint, int parent, int& n) {
    for (; joint != nullptr; joint = joint->getYounger()) {
        const int j = joint->getJntNo();
        if (j >= kMaxJoints) continue;
        s_parent[j] = parent;
        s_order[n++] = j;
        walk(joint->getChild(), j, n);
    }
}

void learn(J3DModelData* data, J3DAnmTransform* stance) {
    if (s_learned) return;
    s_count = data->getJointNum();
    int n = 0;
    walk(data->getJointNodePointer(0), -1, n);
    Mtx world[kMaxJoints];
    Mtx bind[kMaxJoints];
    if (stance != nullptr) stance->setFrame(0.0f);
    for (int k = 0; k < n; ++k) {
        const int j = s_order[k];
        const int p = s_parent[j];
        const J3DTransformInfo& own = data->getJointNodePointer(static_cast<u16>(j))->getTransformInfo();
        J3DTransformInfo info = own;
        if (stance != nullptr) stance->getTransform(static_cast<u16>(j), &info);
        Mtx local, bindLocal;
        J3DGetTranslateRotateMtx(info, local);
        J3DGetTranslateRotateMtx(own, bindLocal);
        rotation_only(local, s_refLocal[j]);
        s_offset[j] = info.mTranslate;
        if (p < 0) {
            cMtx_copy(local, world[j]);
            cMtx_copy(bindLocal, bind[j]);
        } else {
            MTXConcat(world[p], local, world[j]);
            MTXConcat(bind[p], bindLocal, bind[j]);
        }
    }
    for (int j = 0; j < s_count; ++j) rotation_only(world[j], s_refWorld[j]);
    s_hips[0] = joint_named(data, "backbone1");
    s_hips[1] = joint_named(data, "waist");
    s_nose = joint_named(data, "nose");
    if (s_hips[0] >= 0) {
        s_hipRef.x = world[s_hips[0]][0][3];
        s_hipRef.y = world[s_hips[0]][1][3] > 1.0f ? world[s_hips[0]][1][3] : 1.0f;
        s_hipRef.z = world[s_hips[0]][2][3];
        Mtx inv;
        MTXInverse(bind[s_hips[0]], inv);
        const Vec seat = {0.0f, 361.0f, 106.0f};
        MTXMultVec(inv, &seat, &s_seat);
        s_seatJoint = s_hips[0];
    }
    s_backJoint = joint_named(data, "backbone2");
    if (s_backJoint >= 0) {
        Mtx inv;
        MTXInverse(bind[s_backJoint], inv);
        const Vec back = {0.0f, 400.0f, 234.0f};
        MTXMultVec(inv, &back, &s_back);
    }
    s_learned = true;
}

struct Pair {
    const char* his;
    const char* wolf;
};
const Pair kPairs[] = {
    {"backbone1", "backbone1"}, {"backbone2", "backbone2"}, {"neck", "neck"}, {"head", "head"},
    {"chin", "chin"}, {"tange", "tongue1"}, {"earL", "earL"}, {"earR", "earR"},
    {"shouldeL", "shoulderL"}, {"armL1", "FlegL1"}, {"armL2", "FlegL2"}, {"handL", "FlegL4"},
    {"shouldeR", "shoulderR"}, {"armR1", "FlegR1"}, {"armR2", "FlegR2"}, {"handR", "FlegR4"},
    {"waist", "waist"},
    {"legL1", "BlegL1"}, {"legL2", "BlegL2"}, {"legL3", "BlegL3"}, {"footL", "BlegL4"},
    {"legR1", "BlegR1"}, {"legR2", "BlegR2"}, {"legR3", "BlegR3"}, {"footR", "BlegR4"},
    {"tail1", "tail1"}, {"tail2", "tail2"}, {"tail3", "tail3"},
};

const int kWolfMax = 64;
struct WolfRest {
    J3DModelData* data = nullptr;
    u16 joints = 0;
    f32 rootY = 0.0f;
    Mtx rest[kWolfMax];
    Vec centre = {0.0f, 1.0f, 0.0f};
    int follow[kMaxJoints];
    f32 scale = 1.0f;
    Vec back = {0.0f, 0.0f, 0.0f};
};
WolfRest s_wolfRests[4];
int s_wolfRestNext = 0;

void walk_world(J3DJoint* joint, const Mtx parentWorld, Mtx* world) {
    for (; joint != nullptr; joint = joint->getYounger()) {
        const int j = joint->getJntNo();
        if (j >= kWolfMax) continue;
        Mtx local;
        J3DGetTranslateRotateMtx(joint->getTransformInfo(), local);
        MTXConcat(parentWorld, local, world[j]);
        walk_world(joint->getChild(), world[j], world);
    }
}

WolfRest* wolf_rest(J3DModelData* data, J3DModelData* his) {
    if (data == nullptr || data->getJointNum() <= kWolfMidnaSeat || data->getJointNum() > kWolfMax) {
        return nullptr;
    }
    const f32 rootY = data->getJointNodePointer(0)->getTransformInfo().mTranslate.y;
    for (WolfRest& r : s_wolfRests) {
        if (r.data == data && r.joints == data->getJointNum() && r.rootY == rootY) return &r;
    }
    WolfRest& r = s_wolfRests[s_wolfRestNext];
    s_wolfRestNext = (s_wolfRestNext + 1) % 4;
    Mtx identity;
    MTXIdentity(identity);
    Mtx world[kWolfMax];
    walk_world(data->getJointNodePointer(0), identity, world);
    for (int j = 0; j < data->getJointNum(); ++j) rotation_only(world[j], r.rest[j]);
    r.data = data;
    r.joints = data->getJointNum();
    r.rootY = rootY;
    r.centre.x = world[0][0][3];
    r.centre.y = world[0][1][3] > 1.0f ? world[0][1][3] : 1.0f;
    r.centre.z = world[0][2][3];
    r.scale = kSize * r.centre.y / s_hipRef.y;

    {
        Mtx inv;
        MTXInverse(world[kWolfBack], inv);
        const Vec back = {0.0f, 112.0f, world[kWolfBack][2][3]};
        MTXMultVec(inv, &back, &r.back);
    }
    for (int j = 0; j < kMaxJoints; ++j) r.follow[j] = -1;
    for (const Pair& pair : kPairs) {
        const int j = joint_named(his, pair.his);
        if (j >= 0 && j < kMaxJoints) r.follow[j] = joint_named(data, pair.wolf);
    }
    return &r;
}

J3DAnmTransform* s_stanceAnm = nullptr;
J3DAnmTevRegKey* s_coreAnm = nullptr;
J3DAnmTextureSRTKey* s_skinAnm = nullptr;
bool s_animsLoaded = false;
J3DAnmTevRegKey* s_goldGlow = nullptr;
J3DAnmTextureSRTKey* s_goldShimmer = nullptr;
bool s_goldAnimsLoaded = false;

struct Rig {
    bool used = false;
    int look = kLookBeast;
    J3DModel* model = nullptr;
    dKy_tevstr_c tev;
    int tevRoom = -1000;
    mDoExt_brkAnm* core = nullptr;
    mDoExt_btkAnm* skin = nullptr;
    Mtx pose[kMaxJoints];
    Mtx rigid[kMaxJoints];
    int owner = -1;
    u32 lastUsed = 0;
};
Rig s_rigs[kRigs];
Rig& s_local = s_rigs[0];

const Mtx* s_posing = nullptr;
int s_posingCount = 0;

int pose_callback(J3DJoint* joint, int phase) {
    if (phase != 0 || s_posing == nullptr) return 1;
    const u16 j = joint->getJntNo();
    J3DModel* model = j3dSys.getModel();
    if (j >= s_posingCount || model == nullptr) return 1;
    Mtx m;
    cMtx_copy(s_posing[j], m);
    model->setAnmMtx(j, m);
    cMtx_copy(m, J3DSys::mCurrentMtx);
    return 1;
}

enum ArcState { kArcNone, kArcMounting, kArcReady, kArcFailed };
ArcState s_arcs[2] = {kArcNone, kArcNone};

bool arc_ready(int look) {
    ArcState& state = s_arcs[look];
    const char* arc = look == kLookGold ? kGoldArc : kArc;
    if (state == kArcReady) return true;
    if (state == kArcFailed) return false;
    if (state == kArcNone) {
        state = private_arc_request(arc) ? kArcMounting : kArcFailed;
        if (state == kArcFailed) {
            coop_log::warn("coop_mod: [BEAST] could not start loading {}", arc);
            return false;
        }
    }
    const int ready = private_arc_poll(arc);
    if (ready == 0) return false;
    state = ready > 0 ? kArcReady : kArcFailed;
    if (state == kArcFailed) coop_log::warn("coop_mod: [BEAST] {} did not load", arc);
    return state == kArcReady;
}

bool gold_build(Rig& rig) {
    J3DModelData* data = private_arc_load(kGoldArc, kGoldModel);
    if (data == nullptr) return false;
    if (data->getJointNum() > kMaxJoints) {
        private_arc_free_data(data);
        return false;
    }
    J3DModel* model = coop_create_model(data, 0x80000, 0x11020284);
    if (model == nullptr) {
        private_arc_free_data(data);
        return false;
    }
    if (!s_goldAnimsLoaded) {
        s_goldAnimsLoaded = true;
        s_goldGlow = static_cast<J3DAnmTevRegKey*>(private_arc_load_anm(kGoldArc, "gw.brk"));
        s_goldShimmer = static_cast<J3DAnmTextureSRTKey*>(private_arc_load_anm(kGoldArc, "gw.btk"));
    }
    for (u16 j = 0; j < data->getJointNum(); ++j) data->getJointNodePointer(j)->setCallBack(pose_callback);
    JKRHeap* heap = private_arc_heap_if_any();
    JKRHeap* previous = heap != nullptr ? heap->becomeCurrentHeap() : nullptr;
    if (s_goldGlow != nullptr) {
        rig.core = JKR_NEW mDoExt_brkAnm();
        if (rig.core != nullptr && !rig.core->init(data, s_goldGlow, 1, 2, 1.0f, 0, -1)) rig.core = nullptr;
    }
    if (s_goldShimmer != nullptr) {
        rig.skin = JKR_NEW mDoExt_btkAnm();
        if (rig.skin != nullptr && !rig.skin->init(data, s_goldShimmer, 1, 2, 1.0f, 0, -1)) rig.skin = nullptr;
    }
    if (previous != nullptr) previous->becomeCurrentHeap();
    rig.model = model;
    rig.look = kLookGold;
    rig.used = true;
    dKy_tevstr_init(&rig.tev, 0, 0xFF);
    coop_log::info("coop_mod: [BEAST] built a golden wolf ({} joints, glow {} shimmer {})",
        data->getJointNum(), rig.core != nullptr, rig.skin != nullptr);
    return true;
}

bool rig_build(Rig& rig, int look) {
    if (look == kLookGold) return gold_build(rig);
    J3DModelData* data = private_arc_load(kArc, kModel);
    if (data == nullptr) return false;
    if (data->getJointNum() > kMaxJoints) {
        private_arc_free_data(data);
        return false;
    }

    J3DModel* model = coop_create_model(data, 0x80000, 0x11000284);
    if (model == nullptr) {
        private_arc_free_data(data);
        return false;
    }
    if (!s_animsLoaded) {
        s_animsLoaded = true;
        s_stanceAnm = static_cast<J3DAnmTransform*>(private_arc_load_anm(kArc, kStance));
        s_coreAnm = static_cast<J3DAnmTevRegKey*>(private_arc_load_anm(kArc, "mgn_core.brk"));
        s_skinAnm = static_cast<J3DAnmTextureSRTKey*>(private_arc_load_anm(kArc, "mgn_exit.btk"));
    }
    learn(data, s_stanceAnm);
    for (u16 j = 0; j < data->getJointNum(); ++j) {
        data->getJointNodePointer(j)->setCallBack(pose_callback);
    }

    JKRHeap* heap = private_arc_heap_if_any();
    JKRHeap* previous = heap != nullptr ? heap->becomeCurrentHeap() : nullptr;
    if (s_coreAnm != nullptr) {
        rig.core = JKR_NEW mDoExt_brkAnm();
        if (rig.core != nullptr && !rig.core->init(data, s_coreAnm, 1, 2, 1.0f, 0, -1)) rig.core = nullptr;
    }
    if (s_skinAnm != nullptr) {
        rig.skin = JKR_NEW mDoExt_btkAnm();
        if (rig.skin != nullptr && !rig.skin->init(data, s_skinAnm, 1, 0, 0.0f, 0, -1)) rig.skin = nullptr;
    }
    if (previous != nullptr) previous->becomeCurrentHeap();
    rig.model = model;
    rig.look = kLookBeast;
    rig.used = true;
    coop_log::info("coop_mod: [BEAST] built a beast ({} joints, standing pose {}, core {} skin {})",
        s_count, s_stanceAnm != nullptr, rig.core != nullptr, rig.skin != nullptr);
    return true;
}

void rig_free(Rig& rig) {
    if (rig.model != nullptr) {
        J3DModelData* data = rig.model->getModelData();
        coop_free_model(rig.model);
        private_arc_free_data(data);
    }
    if (rig.core != nullptr) JKR_DELETE(rig.core);
    if (rig.skin != nullptr) JKR_DELETE(rig.skin);
    rig = Rig{};
}

void retarget(Rig& rig, J3DModel* wolf, const WolfRest& wr) {
    Mtx rot[kMaxJoints];
    Vec pos[kMaxJoints];
    const f32 s = wr.scale;
    MtxP base = wolf->getBaseTRMtx();
    Mtx baseRot, baseInv;
    rotation_only(base, baseRot);
    MTXInverse(base, baseInv);

    MtxP c = wolf->getAnmMtx(0);
    const Vec centre = {c[0][3], c[1][3], c[2][3]};
    Vec body;
    MTXMultVec(baseInv, &centre, &body);
    const Vec hipsBody = {(body.x - wr.centre.x) * kSize + s_hipRef.x * s,
        (body.y - wr.centre.y) * kSize + s_hipRef.y * s, (body.z - wr.centre.z) * kSize + s_hipRef.z * s};
    Vec hips;
    MTXMultVec(base, &hipsBody, &hips);

    for (int k = 0; k < s_count; ++k) {
        const int j = s_order[k];
        const int p = s_parent[j];
        const int l = wr.follow[j];
        if (l >= 0 && l < wr.joints) {
            Mtx now, inv, delta;
            rotation_only(wolf->getAnmMtx(l), now);
            MTXInverse(wr.rest[l], inv);
            MTXConcat(now, inv, delta);
            MTXConcat(delta, s_refWorld[j], rot[j]);
        } else if (p >= 0) {
            MTXConcat(rot[p], s_refLocal[j], rot[j]);
        } else {
            MTXConcat(baseRot, s_refWorld[j], rot[j]);
        }
        if (p < 0) {
            pos[j].x = base[0][3];
            pos[j].y = base[1][3];
            pos[j].z = base[2][3];
        } else if (j == s_hips[0] || j == s_hips[1]) {
            pos[j] = hips;
        } else {
            Vec along;
            MTXMultVec(rot[p], &s_offset[j], &along);
            pos[j].x = pos[p].x + along.x * s;
            pos[j].y = pos[p].y + along.y * s;
            pos[j].z = pos[p].z + along.z * s;
        }
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col) {
                rig.pose[j][r][col] = rot[j][r][col] * s;
                rig.rigid[j][r][col] = rot[j][r][col];
            }
        }
        rig.pose[j][0][3] = rig.rigid[j][0][3] = pos[j].x;
        rig.pose[j][1][3] = rig.rigid[j][1][3] = pos[j].y;
        rig.pose[j][2][3] = rig.rigid[j][2][3] = pos[j].z;
    }
}

void gold_draw(Rig& rig, J3DModel* wolf, dKy_tevstr_c* tev, bool mirrored) {
    J3DModelData* data = rig.model->getModelData();
    const int count = data->getJointNum() < wolf->getModelData()->getJointNum()
                          ? data->getJointNum()
                          : wolf->getModelData()->getJointNum();
    for (int j = 0; j < count; ++j) cMtx_copy(wolf->getAnmMtx(j), rig.pose[j]);
    if (rig.core != nullptr) {
        rig.core->entry(data);
        rig.core->play();
    }
    if (rig.skin != nullptr) {
        rig.skin->entry(data);
        rig.skin->play();
    }
    rig.model->setBaseTRMtx(wolf->getBaseTRMtx());
    s_posing = rig.pose;
    s_posingCount = count;
    rig.model->calc();
    s_posing = nullptr;
    const int room = tev != nullptr ? tev->room_no : 0;
    if (room != rig.tevRoom) {
        dKy_tevstr_init(&rig.tev, static_cast<s8>(room), 0xFF);
        rig.tevRoom = room;
    }
    MtxP base = wolf->getBaseTRMtx();
    cXyz at(base[0][3], base[1][3], base[2][3]);
    g_env_light.settingTevStruct(5, &at, &rig.tev);
    g_env_light.setLightTevColorType_MAJI(rig.model, &rig.tev);
    mDoExt_modelEntryDL(rig.model);
    if (mirrored) daMirror_c::entry(rig.model);
}

void rig_draw(Rig& rig, J3DModel* wolf, dKy_tevstr_c* tev, bool mirrored) {
    if (rig.look == kLookGold) {
        gold_draw(rig, wolf, tev, mirrored);
        return;
    }
    WolfRest* wr = wolf_rest(wolf->getModelData(), rig.model->getModelData());
    if (wr == nullptr) return;
    retarget(rig, wolf, *wr);
    J3DModelData* data = rig.model->getModelData();
    if (rig.core != nullptr) {
        rig.core->entry(data);
        rig.core->play();
    }
    if (rig.skin != nullptr) rig.skin->entry(data, 0.0f);

    if (data->getMaterialNum() > 3) {
        J3DMaterial* jewel = data->getMaterialNodePointer(3);
        if (jewel != nullptr && jewel->getTevBlock() != nullptr) {
            jewel->getTevColor(0)->r = 0;
            jewel->getTevColor(0)->g = 0;
            jewel->getTevColor(0)->b = 0;
        }
    }
    rig.model->setBaseTRMtx(wolf->getBaseTRMtx());
    s_posing = rig.pose;
    s_posingCount = s_count;
    rig.model->calc();
    s_posing = nullptr;
    g_env_light.setLightTevColorType_MAJI(rig.model, tev);
    mDoExt_modelEntryDL(rig.model);
    if (mirrored) daMirror_c::entry(rig.model);
}

bool s_localFailed[2] = {false, false};
ConfigVarHandle s_wolfVar = 0;
int s_wolfCountdown = 150;

int look_named(const std::string& skin);

int chosen() {
    return look_named(skins_local_slot(kSkinChoiceWolf));
}

int local_look(daAlink_c* alink) {
    if (alink == nullptr || alink->mClothesChangeWaitTimer != 0 || !alink->checkWolf() ||
        alink->mpLinkModel == nullptr) {
        return -1;
    }
    return chosen();
}

bool standing_in(daAlink_c* alink) {
    return s_local.model != nullptr && local_look(alink) >= 0 && s_local.look == local_look(alink);
}

bool local_ready(daAlink_c* alink) {
    const int look = local_look(alink);
    if (look < 0 || !arc_ready(look)) return false;
    if (s_local.used && s_local.look != look) rig_free(s_local);
    if (s_local.model != nullptr) return true;
    if (s_localFailed[look]) return false;
    if (!rig_build(s_local, look)) {
        s_localFailed[look] = true;
        coop_log::warn("coop_mod: [BEAST] could not build look {}", look);
        features_toast("Couldn't load that wolf", "Its model failed to load - see the log.");
        return false;
    }
    return true;
}

bool s_headSwapped = false;
Mtx s_headSaved;
bool s_backSwapped = false;
Mtx s_backSaved;

HookAction on_wolf_items_pre(ModContext*, void* args, void*, void*) {
    s_headSwapped = false;
    s_backSwapped = false;
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    if (alink != daAlink_getAlinkActorClass() || !standing_in(alink)) return HOOK_CONTINUE;
    if (s_local.look != kLookBeast) return HOOK_CONTINUE;
    J3DModel* wolf = alink->mpLinkModel;
    WolfRest* wr = wolf_rest(wolf->getModelData(), s_local.model->getModelData());
    if (wr == nullptr) return HOOK_CONTINUE;
    retarget(s_local, wolf, *wr);
    if (s_nose >= 0) {
        MtxP head = wolf->getAnmMtx(kWolfHead);
        cMtx_copy(head, s_headSaved);

        const Vec held = {28.0f, 14.0f, 0.0f};
        Vec turned;
        MTXMultVecSR(head, &held, &turned);
        head[0][3] = s_local.rigid[s_nose][0][3] - turned.x;
        head[1][3] = s_local.rigid[s_nose][1][3] - turned.y;
        head[2][3] = s_local.rigid[s_nose][2][3] - turned.z;
        s_headSwapped = true;
    }
    if (s_backJoint >= 0) {
        MtxP back = wolf->getAnmMtx(kWolfBack);
        cMtx_copy(back, s_backSaved);
        Vec wolfTop, hisTop;
        MTXMultVec(back, &wr->back, &wolfTop);
        const Vec local = {s_back.x * wr->scale, s_back.y * wr->scale, s_back.z * wr->scale};
        MTXMultVec(s_local.rigid[s_backJoint], &local, &hisTop);
        back[0][3] += hisTop.x - wolfTop.x;
        back[1][3] += hisTop.y - wolfTop.y;
        back[2][3] += hisTop.z - wolfTop.z;
        s_backSwapped = true;
    }
    if (s_seatJoint >= 0) {
        MtxP seat = wolf->getAnmMtx(kWolfMidnaSeat);
        Vec at;
        const Vec local = {s_seat.x * wr->scale, s_seat.y * wr->scale, s_seat.z * wr->scale};
        MTXMultVec(s_local.rigid[s_seatJoint], &local, &at);
        seat[0][3] = at.x;
        seat[1][3] = at.y;
        seat[2][3] = at.z;
    }
    return HOOK_CONTINUE;
}

void on_wolf_items_post(ModContext*, void* args, void*, void*) {
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    J3DModel* wolf = alink != nullptr ? alink->mpLinkModel : nullptr;
    if (wolf != nullptr && s_headSwapped) cMtx_copy(s_headSaved, wolf->getAnmMtx(kWolfHead));
    if (wolf != nullptr && s_backSwapped) cMtx_copy(s_backSaved, wolf->getAnmMtx(kWolfBack));
    s_headSwapped = false;
    s_backSwapped = false;
}

u32 s_frame = 0;
const u32 kRigIdleFrames = 180;

Rig* puppet_rig_any(int pupId) {
    for (int i = 1; i < kRigs; ++i) {
        if (s_rigs[i].used && s_rigs[i].owner == pupId) return &s_rigs[i];
    }
    return nullptr;
}

Rig* puppet_rig(int pupId, int look) {
    if (look != kLookBeast && look != kLookGold) return nullptr;
    if (Rig* rig = puppet_rig_any(pupId)) {
        if (rig->look == look) return rig;
        rig_free(*rig);
    }
    if (!arc_ready(look)) return nullptr;
    for (int i = 1; i < kRigs; ++i) {
        Rig& rig = s_rigs[i];
        if (rig.used) continue;
        if (!rig_build(rig, look)) return nullptr;
        rig.owner = pupId;
        coop_log::info("coop_mod: [BEAST] player {} is look {} (rig {})", pupId, look, i);
        return &rig;
    }
    return nullptr;
}

int look_named(const std::string& skin) {
    if (skin == kBeastGanonSkin) return kLookBeast;
    if (skin == kHerosShadeSkin) return kLookGold;
    return -1;
}

void sweep_rigs() {
    for (int i = 1; i < kRigs; ++i) {
        Rig& rig = s_rigs[i];
        if (!rig.used || s_frame - rig.lastUsed < kRigIdleFrames) continue;
        coop_log::info("coop_mod: [BEAST] player {} is not look {} any more (rig {})", rig.owner,
            rig.look, i);
        rig_free(rig);
    }
}

}

const char* const kBeastGanonSkin = "Beast Ganon";

bool beast_debug_toggle() {
    const bool on = skins_local_slot(kSkinChoiceWolf) != kBeastGanonSkin;
    skins_set_local_slot(kSkinChoiceWolf, on ? kBeastGanonSkin : "");
    coop_log::info("coop_mod: [BEAST] Debug switch {}", on ? "on" : "off");
    return on;
}

int wolf_standin(const char* skin) {
    return skin != nullptr ? look_named(skin) : -1;
}

bool beast_model_draw(daAlink_c* alink, J3DModel* model, int noDraw) {
    if (!alink->checkWolf()) return false;
    bool chain = false;
    for (int i = 0; i < 4; ++i) chain = chain || model == alink->mpWlChainModels[i];
    if (model != alink->mpLinkModel && !chain) return false;
    if (!local_ready(alink)) return false;
    if (model == alink->mpLinkModel && noDraw == 0) rig_draw(s_local, model, &alink->tevStr, true);
    return true;
}

bool beast_local_active() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    return alink != nullptr && standing_in(alink);
}

J3DModel* beast_local_model() {
    return beast_local_active() ? s_local.model : nullptr;
}

bool beast_puppet_ready(int pupId, int look) {
    Rig* rig = puppet_rig(pupId, look);
    if (rig == nullptr) return false;
    rig->lastUsed = s_frame;
    return true;
}

void beast_puppet_draw(int pupId, J3DModel* body, dKy_tevstr_c* tev) {
    Rig* rig = puppet_rig_any(pupId);
    if (rig == nullptr || body == nullptr) return;
    rig->lastUsed = s_frame;
    rig_draw(*rig, body, tev, false);
}

J3DModel* beast_puppet_model(int pupId) {
    for (int i = 1; i < kRigs; ++i) {
        if (s_rigs[i].used && s_rigs[i].owner == pupId) return s_rigs[i].model;
    }
    return nullptr;
}

void beast_frame(daAlink_c* alink) {
    ++s_frame;
    sweep_rigs();

    const int look = chosen();
    for (int l = 0; l < 2; ++l) {
        if (s_arcs[l] == kArcMounting || (s_arcs[l] == kArcNone && look == l)) arc_ready(l);
    }
    if (s_wolfCountdown > 0 && cfg_bool(s_wolfVar, false) && alink != nullptr &&
        !dComIfGp_event_runCheck()) {
        if (--s_wolfCountdown == 0 && !alink->checkWolf()) {
            coop_log::info("coop_mod: [BEAST] debug: turning into the wolf");
            coop_debug_force_transform();
        }
    }
}

void beast_init() {
    ConfigVarDesc wolfDesc = CONFIG_VAR_DESC_INIT;
    wolfDesc.name = "debug_wolf";
    wolfDesc.type = CONFIG_VAR_BOOL;
    wolfDesc.default_bool = false;
    if (svc_config->register_var(mod_ctx, &wolfDesc, &s_wolfVar) != MOD_OK) s_wolfVar = 0;
    const ModResult pre = mods::hook::add_pre<BeastWolfItemsHook>(on_wolf_items_pre);
    const ModResult post = mods::hook::add_post<BeastWolfItemsHook>(on_wolf_items_post);
    coop_log::info("coop_mod: [BEAST] wolf items hook: {}/{}", static_cast<int>(pre),
        static_cast<int>(post));
}



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
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphBase/J3DTransform.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JUtility/JUTNameTab.h"

#include <cstdlib>
#include <cstring>

extern const ConfigService* svc_config;

DEFINE_HOOK_SYMBOL("daAlink_c::modelDraw", void(daAlink_c*, J3DModel*, int), GanonModelDrawHook);

DEFINE_HOOK_SYMBOL("daAlink_c::setItemMatrix", void(daAlink_c*, int), GanonItemMatrixHook);

DEFINE_HOOK_SYMBOL("daAlink_c::shadowDraw", void(daAlink_c*), GanonShadowHook);

namespace {

const int kMaxJoints = 64;

const int kRigs = 6;

enum ArcState { kArcNone, kArcMounting, kArcReady, kArcFailed };

struct Finger {
    const char* name;
    f32 closeDegrees;
    bool right;
};

struct BodyDesc {
    const char* skin;
    const char* tag;
    const char* arc;
    const char* model;
    const char* ghost;
    const char* hidden[2];
    Finger fingers[4];
    f32 backDepth;
    bool bossAnims;

    f32 headScale;
};

struct Body {
    BodyDesc d;
    ArcState arc = kArcNone;
    bool localFailed = false;

    bool learned = false;
    int count = 0;
    int parent[kMaxJoints];
    int order[kMaxJoints];
    Mtx restWorld[kMaxJoints];
    Mtx restLocal[kMaxJoints];
    S16Vec restAngle[kMaxJoints];
    Vec offset[kMaxJoints];
    f32 rootHeight = 1.0f;
    int fingerJoint[4] = {-1, -1, -1, -1};
    int hiddenJoint[2] = {-1, -1};
    bool inHead[kMaxJoints] = {};

    bool animsLoaded = false;
    J3DAnmTextureSRTKey* eyeAnm = nullptr;
    J3DAnmTexPattern* blinkAnm = nullptr;
    J3DAnmTevRegKey* coreAnm = nullptr;
};

const int kGanon = 0;
const int kShade = 1;
const int kBodies = 2;
Body s_bodies[kBodies] = {
    {{"Ganondorf", "GANON", "B_gnd", "egnd.bmd", nullptr, {nullptr, nullptr},
        {{"fingerL1", -72.0f, false}, {"fingerL2", -85.0f, false}, {"fingerR1", -72.0f, true},
            {"fingerR2", -85.0f, true}},
        1.8f, true, 1.2f}},
    {{"Hero's Shade", "SHADE", "KN_a", "kn_a.bmd", "kn_am_gt.bmd", {"weaponL", "weaponR"},
        {{"fingerL1", -65.0f, false}, {"fingerL2", -55.0f, false}, {"fingerR1", -65.0f, true},
            {"fingerR2", -55.0f, true}},
        1.5f, false, 1.0f}},
};

Body* body_named(const char* skin) {
    if (skin == nullptr || skin[0] == '\0') return nullptr;
    for (Body& b : s_bodies) {
        if (std::strcmp(b.d.skin, skin) == 0) return &b;
    }
    return nullptr;
}

int body_index(const Body* b) {
    return b == nullptr ? -1 : static_cast<int>(b - s_bodies);
}

bool arc_ready(Body& b) {
    if (b.arc == kArcReady) return true;
    if (b.arc == kArcFailed) return false;
    if (b.arc == kArcNone) {
        b.arc = private_arc_request(b.d.arc) ? kArcMounting : kArcFailed;
        if (b.arc == kArcFailed) {
            coop_log::warn("coop_mod: [{}] could not start loading {}", b.d.tag, b.d.arc);
            return false;
        }
    }
    const int ready = private_arc_poll(b.d.arc);
    if (ready == 0) return false;
    b.arc = ready > 0 ? kArcReady : kArcFailed;
    if (b.arc == kArcFailed) coop_log::warn("coop_mod: [{}] {} did not load", b.d.tag, b.d.arc);
    return b.arc == kArcReady;
}

void rotation_only(const Mtx in, Mtx out) {
    cMtx_copy(in, out);
    out[0][3] = out[1][3] = out[2][3] = 0.0f;
}

int joint_named(J3DModelData* data, const char* name) {
    JUTNameTab* names = data->getJointName();
    if (names == nullptr || name == nullptr) return -1;
    for (u16 i = 0; i < data->getJointNum(); ++i) {
        const char* n = names->getName(i);
        if (n != nullptr && std::strcmp(n, name) == 0) return i;
    }
    return -1;
}

void walk_him(Body& b, J3DJoint* joint, const Mtx parentWorld, int parent, Mtx* world, int& n) {
    for (; joint != nullptr; joint = joint->getYounger()) {
        const int j = joint->getJntNo();
        if (j >= kMaxJoints) continue;
        const J3DTransformInfo& info = joint->getTransformInfo();
        Mtx local;
        J3DGetTranslateRotateMtx(info, local);
        MTXConcat(parentWorld, local, world[j]);
        rotation_only(local, b.restLocal[j]);
        b.restAngle[j] = info.mRotation;
        b.offset[j] = info.mTranslate;
        b.parent[j] = parent;
        b.order[n++] = j;
        walk_him(b, joint->getChild(), world[j], j, world, n);
    }
}

void walk_world(J3DJoint* joint, const Mtx parentWorld, Mtx* world) {
    for (; joint != nullptr; joint = joint->getYounger()) {
        const int j = joint->getJntNo();
        if (j >= kMaxJoints) continue;
        Mtx local;
        J3DGetTranslateRotateMtx(joint->getTransformInfo(), local);
        MTXConcat(parentWorld, local, world[j]);
        walk_world(joint->getChild(), world[j], world);
    }
}

void learn(Body& b, J3DModelData* data) {
    if (b.learned) return;
    b.count = data->getJointNum();
    Mtx identity;
    MTXIdentity(identity);
    Mtx world[kMaxJoints];
    int n = 0;
    walk_him(b, data->getJointNodePointer(0), identity, -1, world, n);
    for (int j = 0; j < b.count; ++j) rotation_only(world[j], b.restWorld[j]);
    b.rootHeight = world[0][1][3] > 1.0f ? world[0][1][3] : 1.0f;
    for (int f = 0; f < 4; ++f) b.fingerJoint[f] = joint_named(data, b.d.fingers[f].name);
    for (int h = 0; h < 2; ++h) b.hiddenJoint[h] = joint_named(data, b.d.hidden[h]);
    const int head = joint_named(data, "head");
    for (int k = 0; k < b.count; ++k) {
        const int j = b.order[k];
        b.inHead[j] = j == head || (b.parent[j] >= 0 && b.inHead[b.parent[j]]);
    }
    b.learned = true;
}

struct LinkRest {
    J3DModelData* data = nullptr;
    const Body* body = nullptr;
    u16 joints = 0;
    f32 rootY = 0.0f;
    Mtx rest[kMaxJoints];
    int follow[kMaxJoints];
    f32 scale = 1.0f;
};
LinkRest s_linkRests[8];
int s_linkRestNext = 0;

LinkRest* link_rest(J3DModelData* data, const Body& b, J3DModelData* his) {
    if (data == nullptr || data->getJointNum() == 0 || data->getJointNum() > kMaxJoints) return nullptr;
    const f32 rootY = data->getJointNodePointer(0)->getTransformInfo().mTranslate.y;
    for (LinkRest& r : s_linkRests) {
        if (r.data == data && r.body == &b && r.joints == data->getJointNum() && r.rootY == rootY) {
            return &r;
        }
    }
    LinkRest& r = s_linkRests[s_linkRestNext];
    s_linkRestNext = (s_linkRestNext + 1) % 8;
    Mtx identity;
    MTXIdentity(identity);
    Mtx world[kMaxJoints];
    walk_world(data->getJointNodePointer(0), identity, world);
    for (int j = 0; j < data->getJointNum(); ++j) rotation_only(world[j], r.rest[j]);
    r.data = data;
    r.body = &b;
    r.joints = data->getJointNum();
    r.rootY = rootY;
    r.scale = world[0][1][3] > 1.0f ? world[0][1][3] / b.rootHeight : 1.0f;
    JUTNameTab* names = his->getJointName();
    for (int j = 0; j < b.count; ++j) {
        const char* name = names != nullptr ? names->getName(static_cast<u16>(j)) : nullptr;
        r.follow[j] = name != nullptr ? joint_named(data, name) : -1;
    }
    return &r;
}

struct Rig {
    bool used = false;
    Body* body = nullptr;
    J3DModel* model = nullptr;
    J3DModel* ghost = nullptr;

    mDoExt_invisibleModel inv;
    bool invReady = false;
    dKy_tevstr_c ghostTev;
    int ghostTevRoom = -1000;
    mDoExt_btkAnm* eyes = nullptr;
    mDoExt_btpAnm* blink = nullptr;
    mDoExt_brkAnm* core = nullptr;
    int blinkWait = 120;
    f32 blinkFrame = 0.0f;
    f32 grip[2] = {0.0f, 0.0f};
    f32 scale = 1.0f;
    Mtx pose[kMaxJoints];
    Mtx rigid[kMaxJoints];
    int owner = -1;
    u32 lastUsed = 0;
};
Rig s_rigs[kRigs];

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

void rig_free(Rig& rig) {
    if (rig.invReady && rig.inv.mpPackets != nullptr) JKR_DELETE_ARRAY(rig.inv.mpPackets);
    for (J3DModel* m : {rig.model, rig.ghost}) {
        if (m == nullptr) continue;
        J3DModelData* data = m->getModelData();
        coop_free_model(m);
        private_arc_free_data(data);
    }
    if (rig.eyes != nullptr) JKR_DELETE(rig.eyes);
    if (rig.blink != nullptr) JKR_DELETE(rig.blink);
    if (rig.core != nullptr) JKR_DELETE(rig.core);
    rig = Rig{};
    rig.inv.mpPackets = nullptr;
    rig.inv.mModel = nullptr;
}

J3DModel* posed_model(const Body& b, const char* file) {
    J3DModelData* data = private_arc_load(b.d.arc, file);
    if (data == nullptr) return nullptr;
    if (data->getJointNum() > kMaxJoints) {
        private_arc_free_data(data);
        return nullptr;
    }
    J3DModel* model = coop_create_model(data, 0x80000, 0x11000284);
    if (model == nullptr) {
        private_arc_free_data(data);
        return nullptr;
    }
    for (u16 j = 0; j < data->getJointNum(); ++j) data->getJointNodePointer(j)->setCallBack(pose_callback);
    return model;
}

bool rig_build(Rig& rig, Body& b) {
    rig.inv.mpPackets = nullptr;
    rig.inv.mModel = nullptr;
    rig.body = &b;
    rig.model = posed_model(b, b.d.model);
    if (rig.model == nullptr) {
        rig_free(rig);
        return false;
    }
    J3DModelData* data = rig.model->getModelData();
    learn(b, data);
    JKRHeap* heap = private_arc_heap_if_any();
    if (b.d.ghost != nullptr) {
        rig.ghost = posed_model(b, b.d.ghost);

        JKRHeap* previous = heap != nullptr ? heap->becomeCurrentHeap() : nullptr;
        rig.invReady = rig.ghost != nullptr && rig.inv.create(rig.ghost, 1) != 0;
        if (previous != nullptr) previous->becomeCurrentHeap();
        if (!rig.invReady) {
            coop_log::warn("coop_mod: [{}] his ghost did not load", b.d.tag);
            rig_free(rig);
            return false;
        }
        dKy_tevstr_init(&rig.ghostTev, 0, 0xFF);
    }
    if (b.d.bossAnims) {
        if (!b.animsLoaded) {
            b.animsLoaded = true;
            b.eyeAnm = static_cast<J3DAnmTextureSRTKey*>(private_arc_load_anm(b.d.arc, "eye_default.btk"));
            b.blinkAnm = static_cast<J3DAnmTexPattern*>(private_arc_load_anm(b.d.arc, "egnd_mepachi.btp"));
            b.coreAnm = static_cast<J3DAnmTevRegKey*>(private_arc_load_anm(b.d.arc, "egnd_core_beat.brk"));
        }

        JKRHeap* previous = heap != nullptr ? heap->becomeCurrentHeap() : nullptr;
        if (b.eyeAnm != nullptr) {
            rig.eyes = JKR_NEW mDoExt_btkAnm();
            if (rig.eyes != nullptr && !rig.eyes->init(data, b.eyeAnm, 1, 0, 1.0f, 0, -1)) rig.eyes = nullptr;
        }
        if (b.blinkAnm != nullptr) {
            rig.blink = JKR_NEW mDoExt_btpAnm();
            if (rig.blink != nullptr && !rig.blink->init(data, b.blinkAnm, 1, 2, 1.0f, 0, -1)) {
                rig.blink = nullptr;
            }
        }
        if (b.coreAnm != nullptr) {
            rig.core = JKR_NEW mDoExt_brkAnm();
            if (rig.core != nullptr && !rig.core->init(data, b.coreAnm, 1, 2, 1.0f, 0, -1)) rig.core = nullptr;
        }
        if (previous != nullptr) previous->becomeCurrentHeap();
    }
    rig.used = true;
    rig.blinkWait = 60 + std::rand() % 180;
    coop_log::info("coop_mod: [{}] built one ({} joints, ghost {}, eyes {} blink {} core {})", b.d.tag,
        b.count, rig.invReady, rig.eyes != nullptr, rig.blink != nullptr, rig.core != nullptr);
    return true;
}

int finger_of(const Body& b, int joint) {
    for (int f = 0; f < 4; ++f) {
        if (b.fingerJoint[f] == joint) return f;
    }
    return -1;
}

void retarget(Rig& rig, J3DModel* link, const LinkRest& lr) {
    const Body& b = *rig.body;
    Mtx rot[kMaxJoints];
    Vec pos[kMaxJoints];
    const f32 s = rig.scale;
    for (int k = 0; k < b.count; ++k) {
        const int j = b.order[k];
        const int p = b.parent[j];
        const int l = lr.follow[j];
        const int finger = finger_of(b, j);
        if (l >= 0) {
            Mtx now, inv, delta;
            rotation_only(link->getAnmMtx(l), now);
            MTXInverse(lr.rest[l], inv);
            MTXConcat(now, inv, delta);
            MTXConcat(delta, b.restWorld[j], rot[j]);
        } else if (p >= 0 && finger >= 0) {
            const Finger& f = b.d.fingers[finger];
            const f32 bend = rig.grip[f.right ? 1 : 0] * f.closeDegrees * (f.right ? -1.0f : 1.0f);
            const S16Vec& a = b.restAngle[j];
            Mtx local;
            J3DGetTranslateRotateMtx(a.x, static_cast<s16>(a.y + bend * (32768.0f / 180.0f)), a.z,
                0.0f, 0.0f, 0.0f, local);
            MTXConcat(rot[p], local, rot[j]);
        } else if (p >= 0) {
            MTXConcat(rot[p], b.restLocal[j], rot[j]);
        } else {
            rotation_only(link->getAnmMtx(0), rot[j]);
        }
        if (p < 0) {

            MtxP centre = link->getAnmMtx(l >= 0 ? l : 0);
            pos[j].x = centre[0][3];
            pos[j].y = centre[1][3];
            pos[j].z = centre[2][3];
        } else {
            Vec along;
            MTXMultVec(rot[p], &b.offset[j], &along);

            const f32 bone = b.inHead[p] ? s * b.d.headScale : s;
            pos[j].x = pos[p].x + along.x * bone;
            pos[j].y = pos[p].y + along.y * bone;
            pos[j].z = pos[p].z + along.z * bone;
        }

        const f32 size = b.inHead[j] ? s * b.d.headScale : s;
        const f32 drawn = (j == b.hiddenJoint[0] || j == b.hiddenJoint[1]) ? s * 0.0001f : size;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                rig.pose[j][r][c] = rot[j][r][c] * drawn;
                rig.rigid[j][r][c] = rot[j][r][c];
            }
        }
        rig.pose[j][0][3] = rig.rigid[j][0][3] = pos[j].x;
        rig.pose[j][1][3] = rig.rigid[j][1][3] = pos[j].y;
        rig.pose[j][2][3] = rig.rigid[j][2][3] = pos[j].z;
    }
}

int his_joint_for(const Rig& rig, const LinkRest& lr, int l) {
    for (int j = 0; j < rig.body->count; ++j) {
        if (lr.follow[j] == l) return j;
    }
    return -1;
}

void blink(Rig& rig) {
    if (rig.blink == nullptr) return;
    if (rig.blinkFrame > 0.0f || --rig.blinkWait <= 0) {
        rig.blinkFrame += 1.0f;
        if (rig.blinkFrame > rig.blink->getEndFrame()) {
            rig.blinkFrame = 0.0f;
            rig.blinkWait = 90 + std::rand() % 210;
        }
    }
}

void approach(f32& value, f32 target) {
    const f32 step = 0.2f;
    if (value < target) {
        value = value + step > target ? target : value + step;
    } else {
        value = value - step < target ? target : value - step;
    }
}

void calc_posed(J3DModel* model, J3DModel* link, const Rig& rig) {

    model->setBaseTRMtx(link->getBaseTRMtx());
    s_posing = rig.pose;
    s_posingCount = rig.body->count;
    model->calc();
    s_posing = nullptr;
}

void rig_draw(Rig& rig, J3DModel* link, bool gripL, bool gripR, dKy_tevstr_c* tev, bool mirrored) {
    LinkRest* lr = link_rest(link->getModelData(), *rig.body, rig.model->getModelData());
    if (lr == nullptr) return;
    rig.scale = lr->scale;
    approach(rig.grip[0], gripL ? 1.0f : 0.0f);
    approach(rig.grip[1], gripR ? 1.0f : 0.0f);
    retarget(rig, link, *lr);

    J3DModelData* data = rig.model->getModelData();
    if (rig.eyes != nullptr) rig.eyes->entry(data, 0.0f);
    if (rig.core != nullptr) {
        rig.core->entry(data);
        rig.core->play();
    }
    blink(rig);
    if (rig.blink != nullptr) rig.blink->entry(data, static_cast<s16>(rig.blinkFrame));

    calc_posed(rig.model, link, rig);
    g_env_light.setLightTevColorType_MAJI(rig.model, tev);
    mDoExt_modelEntryDL(rig.model);

    if (mirrored) daMirror_c::entry(rig.model);

    if (rig.ghost != nullptr && rig.invReady) {

        calc_posed(rig.ghost, link, rig);
        const int room = tev != nullptr ? tev->room_no : 0;
        if (room != rig.ghostTevRoom) {
            dKy_tevstr_init(&rig.ghostTev, static_cast<s8>(room), 0xFF);
            rig.ghostTevRoom = room;
        }
        MtxP base = link->getBaseTRMtx();
        cXyz at(base[0][3], base[1][3], base[2][3]);
        g_env_light.settingTevStruct(7, &at, &rig.ghostTev);

        rig.ghostTev.TevColor.r = 0;
        rig.ghostTev.TevColor.g = 0;
        rig.ghostTev.TevColor.b = 0;
        rig.ghostTev.TevColor.a = 255;
        g_env_light.setLightTevColorType_MAJI(rig.ghost, &rig.ghostTev);
        J3DDrawBuffer* saved0 = j3dSys.getDrawBuffer(0);
        J3DDrawBuffer* saved1 = j3dSys.getDrawBuffer(1);
        if (dKy_darkworld_check()) dComIfGd_setListDark();
        rig.inv.entryDL(nullptr);
        j3dSys.setDrawBuffer(saved0, 0);
        j3dSys.setDrawBuffer(saved1, 1);
        if (mirrored) daMirror_c::entry(rig.ghost);
    }
}

bool gripping(daAlink_c* alink, J3DShape* shown) {
    if (shown == nullptr || alink->mpLinkHandModel == nullptr) return false;
    J3DModelData* hands = alink->mpLinkHandModel->getModelData();
    for (u16 i = 0; i < hands->getMaterialNum(); ++i) {
        J3DMaterial* mat = hands->getMaterialNodePointer(i);
        if (mat != nullptr && mat->getShape() == shown) return true;
    }
    return false;
}

const f32 kSwordScale = 0.555f;
const f32 kSwordGrip = 38.3f;
const f32 kSheathMouth = 60.9f;

J3DModelData* s_swordData = nullptr;
J3DModelData* s_sheathData = nullptr;
J3DModel* s_swordModel = nullptr;
J3DModel* s_sheathModel = nullptr;
bool s_swordFailed = false;

const f32 kSheathAtGuard = 11.0f;

J3DModelData* rehung(const char* file, f32 along, bool sheath) {
    J3DModelData* data = private_arc_load(s_bodies[kGanon].d.arc, file);
    if (data == nullptr || data->getJointNum() < 2) return data;
    J3DTransformInfo& root = data->getJointNodePointer(0)->getTransformInfo();
    J3DTransformInfo& mesh = data->getJointNodePointer(1)->getTransformInfo();
    if (!sheath) {

        root.mScale.x = root.mScale.y = root.mScale.z = kSwordScale;
        root.mRotation.x = 0;
        root.mRotation.y = static_cast<s16>(0xC000);
        root.mRotation.z = static_cast<s16>(0xC000);
        root.mTranslate.x = root.mTranslate.y = root.mTranslate.z = 0.0f;
        mesh.mRotation.x = mesh.mRotation.y = mesh.mRotation.z = 0;
        mesh.mTranslate.x = 0.0f;
        mesh.mTranslate.y = -along;
        mesh.mTranslate.z = 0.0f;
    } else {

        root.mScale.x = root.mScale.y = root.mScale.z = 1.0f;
        root.mRotation.x = 0;
        root.mRotation.y = cM_deg2s(33.1f);
        root.mRotation.z = 0;
        root.mTranslate.x = -18.5f;
        root.mTranslate.y = 0.14f;
        root.mTranslate.z = 12.2f;
        mesh.mScale.x = mesh.mScale.y = mesh.mScale.z = kSwordScale;
        mesh.mRotation.x = 0;
        mesh.mRotation.y = static_cast<s16>(0xC000);
        mesh.mRotation.z = static_cast<s16>(0xC000);
        mesh.mTranslate.x = kSheathAtGuard - along * kSwordScale;
        mesh.mTranslate.y = 0.0f;
        mesh.mTranslate.z = 0.0f;
    }

    for (u16 i = 0; i < data->getMaterialNum(); ++i) {
        J3DMaterial* mat = data->getMaterialNodePointer(i);
        J3DAlphaComp* comp = mat != nullptr && mat->getPEBlock() != nullptr
                                 ? mat->getPEBlock()->getAlphaComp()
                                 : nullptr;
        if (comp == nullptr) continue;
        const J3DAlphaCompInfo always = {GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0};
        comp->setAlphaCompInfo(always);
    }
    return data;
}

bool sword_data() {
    if (s_swordData != nullptr && s_sheathData != nullptr) return true;
    if (s_swordFailed || !arc_ready(s_bodies[kGanon])) return false;
    if (s_swordData == nullptr) s_swordData = rehung("egnd_sword.bmd", kSwordGrip, false);
    if (s_sheathData == nullptr) s_sheathData = rehung("egnd_sheath.bmd", kSheathMouth, true);
    s_swordFailed = s_swordData == nullptr || s_sheathData == nullptr;
    return !s_swordFailed;
}

bool build_sword() {
    if (s_swordModel != nullptr) return true;
    if (!sword_data()) return false;
    s_swordModel = coop_create_model(s_swordData, 0x80000, 0x11000284);
    s_sheathModel = coop_create_model(s_sheathData, 0x80000, 0x11000284);
    s_swordFailed = s_swordModel == nullptr || s_sheathModel == nullptr;
    coop_log::info("coop_mod: [GANON] his sword {}", s_swordFailed ? "did not load" : "is ready");
    return !s_swordFailed;
}

const f32 kShadeWeaponScale = 103.0f / 154.5f;

J3DModelData* s_shadeSwordData = nullptr;
J3DModelData* s_shadeShieldData = nullptr;
J3DModelData* s_shadeSheathData = nullptr;
J3DModel* s_shadeSword = nullptr;
J3DModel* s_shadeShield = nullptr;
J3DModel* s_shadeSheath = nullptr;
bool s_shadeWeaponsFailed = false;

J3DModelData* shade_weapon(const char* keep) {
    Body& b = s_bodies[kShade];
    J3DModelData* data = private_arc_load(b.d.arc, b.d.model);
    if (data == nullptr) return nullptr;
    const int k = joint_named(data, keep);
    bool compensated = true;
    for (u16 j = 1; j < data->getJointNum(); ++j) {
        compensated = compensated && data->getJointNodePointer(j)->getScaleCompensate() != 0;
    }
    if (k < 0 || !compensated) {
        coop_log::warn("coop_mod: [SHADE] his {} cannot be cut out (joint {}, compensated {})", keep, k,
            compensated);
        private_arc_free_data(data);
        return nullptr;
    }
    for (u16 j = 0; j < data->getJointNum(); ++j) {
        J3DTransformInfo& info = data->getJointNodePointer(j)->getTransformInfo();
        const f32 scale = j == k ? kShadeWeaponScale : 0.0001f;
        info.mScale.x = info.mScale.y = info.mScale.z = scale;
        info.mRotation.x = info.mRotation.y = info.mRotation.z = 0;
        info.mTranslate.x = info.mTranslate.y = info.mTranslate.z = 0.0f;
    }
    return data;
}

J3DModelData* shade_sheath() {
    J3DModelData* data = private_arc_load(s_bodies[kShade].d.arc, "kn_pod.bmd");
    if (data == nullptr) return nullptr;
    if (data->getJointNum() < 2) {
        private_arc_free_data(data);
        return nullptr;
    }
    J3DTransformInfo& root = data->getJointNodePointer(0)->getTransformInfo();
    root.mScale.x = root.mScale.y = root.mScale.z = 1.0f;
    root.mRotation.x = 0;
    root.mRotation.y = cM_deg2s(33.1f);
    root.mRotation.z = 0;
    root.mTranslate.x = -18.5f;
    root.mTranslate.y = 0.14f;
    root.mTranslate.z = 12.2f;
    J3DTransformInfo& mesh = data->getJointNodePointer(1)->getTransformInfo();
    mesh.mScale.x = mesh.mScale.y = mesh.mScale.z = kShadeWeaponScale;
    mesh.mRotation.x = cM_deg2s(90.0f);
    mesh.mRotation.y = 0;
    mesh.mRotation.z = cM_deg2s(-20.58f);
    mesh.mTranslate.x = 16.102f;
    mesh.mTranslate.y = 0.036f;
    mesh.mTranslate.z = 0.544f;
    JUTNameTab* names = data->getMaterialName();
    for (u16 i = 0; i < data->getMaterialNum(); ++i) {
        const char* name = names != nullptr ? names->getName(i) : nullptr;
        J3DMaterial* mat = data->getMaterialNodePointer(i);
        if (name == nullptr || std::strcmp(name, "KN_grip_m") != 0 || mat == nullptr ||
            mat->getShape() == nullptr) {
            continue;
        }
        mat->getShape()->hide();
    }
    return data;
}

bool shade_weapon_data() {
    if (s_shadeSwordData != nullptr && s_shadeShieldData != nullptr && s_shadeSheathData != nullptr) {
        return true;
    }
    if (s_shadeWeaponsFailed || !arc_ready(s_bodies[kShade])) return false;
    if (s_shadeSwordData == nullptr) s_shadeSwordData = shade_weapon("weaponL");
    if (s_shadeShieldData == nullptr) s_shadeShieldData = shade_weapon("weaponR");
    if (s_shadeSheathData == nullptr) s_shadeSheathData = shade_sheath();
    s_shadeWeaponsFailed =
        s_shadeSwordData == nullptr || s_shadeShieldData == nullptr || s_shadeSheathData == nullptr;
    return !s_shadeWeaponsFailed;
}

bool build_shade_weapons() {
    if (s_shadeSword != nullptr && s_shadeShield != nullptr && s_shadeSheath != nullptr) return true;
    if (!shade_weapon_data()) return false;
    if (s_shadeSword == nullptr) s_shadeSword = coop_create_model(s_shadeSwordData, 0x80000, 0x11000284);
    if (s_shadeShield == nullptr) s_shadeShield = coop_create_model(s_shadeShieldData, 0x80000, 0x11000284);
    if (s_shadeSheath == nullptr) s_shadeSheath = coop_create_model(s_shadeSheathData, 0x80000, 0x11000284);
    s_shadeWeaponsFailed = s_shadeSword == nullptr || s_shadeShield == nullptr || s_shadeSheath == nullptr;
    coop_log::info("coop_mod: [SHADE] his sword and shield {}",
        s_shadeWeaponsFailed ? "did not load" : "are ready");
    return !s_shadeWeaponsFailed;
}

dKy_tevstr_c s_swordTev;
int s_swordTevRoom = -1000;

void draw_in_place(J3DModel* ours, J3DModel* links, daAlink_c* alink) {
    ours->setBaseTRMtx(links->getBaseTRMtx());
    ours->calc();

    dKy_tevstr_c* tev = &alink->tevStr;
    if (ours == s_swordModel) {
        const int room = fopAcM_GetRoomNo(alink);
        if (room != s_swordTevRoom) {
            dKy_tevstr_init(&s_swordTev, static_cast<s8>(room), 0xFF);
            s_swordTevRoom = room;
        }
        g_env_light.settingTevStruct(5, &alink->current.pos, &s_swordTev);
        tev = &s_swordTev;
    }
    g_env_light.setLightTevColorType_MAJI(ours, tev);
    mDoExt_modelEntryDL(ours);
}

bool s_on = false;

ConfigVarHandle s_autoVar = 0;
bool s_autoChecked = false;
Rig& s_local = s_rigs[0];

Body* local_body(daAlink_c* alink) {
    if (alink == nullptr || alink->mClothesChangeWaitTimer != 0) return nullptr;
    if (alink->checkWolf() || alink->mpLinkModel == nullptr) return nullptr;
    if (s_on) return &s_bodies[kGanon];
    return body_named(skins_local_slot(skins_slot_for_outfit(local_skin_outfit())).c_str());
}

bool standing_in(daAlink_c* alink) {
    return s_local.model != nullptr && s_local.body != nullptr && s_local.body == local_body(alink);
}

std::string local_piece(int slot) {
    const std::string piece = skins_local_slot(slot);
    return piece.empty() ? skins_local_slot(kSkinChoiceEquipment) : piece;
}

bool local_sword_wanted() {
    return local_piece(kSkinChoiceMasterSword) == kGanondorfSkin;
}

HookAction on_model_draw_pre(ModContext*, void* args, void*, void*) {
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    J3DModel* model = mods::arg<J3DModel*>(args, 1);
    if (alink != daAlink_getAlinkActorClass() || model == nullptr) return HOOK_CONTINUE;

    if ((model == alink->mSwordModel || model == alink->mSheathModel || model == alink->mShieldModel) &&
        sumo_hides_equipment(coop_net_local_id())) {
        return HOOK_SKIP_ORIGINAL;
    }

    if (beast_model_draw(alink, model, mods::arg<int>(args, 2))) return HOOK_SKIP_ORIGINAL;

    if ((model == alink->mSwordModel || model == alink->mSheathModel) &&
        alink->mSwordModel == alink->mpSwMModel && !alink->checkWolf() && local_sword_wanted() &&
        build_sword()) {
        if (mods::arg<int>(args, 2) == 0) {
            draw_in_place(model == alink->mSwordModel ? s_swordModel : s_sheathModel, model, alink);
        }
        return HOOK_SKIP_ORIGINAL;
    }

    if (model == alink->mSwordModel && alink->mSwordModel == alink->mpSwAModel &&
        local_piece(kSkinChoiceOrdonSword) == kHerosShadeSkin && build_shade_weapons()) {
        if (mods::arg<int>(args, 2) == 0) draw_in_place(s_shadeSword, model, alink);
        return HOOK_SKIP_ORIGINAL;
    }
    if (model == alink->mSheathModel && alink->mSheathModel == alink->mpSwASheathModel &&
        local_piece(kSkinChoiceOrdonSword) == kHerosShadeSkin && build_shade_weapons()) {
        if (mods::arg<int>(args, 2) == 0) draw_in_place(s_shadeSheath, model, alink);
        return HOOK_SKIP_ORIGINAL;
    }
    if (model == alink->mShieldModel && alink->mShieldChangeWaitTimer == 0 &&
        alink->mShieldArcName != nullptr && std::strcmp(alink->mShieldArcName, "HyShd") == 0 &&
        local_piece(kSkinChoiceHylianShield) == kHerosShadeSkin && build_shade_weapons()) {
        if (mods::arg<int>(args, 2) == 0) draw_in_place(s_shadeShield, model, alink);
        return HOOK_SKIP_ORIGINAL;
    }
    if (!standing_in(alink)) return HOOK_CONTINUE;
    const bool body = model == alink->mpLinkModel || model == alink->mpLinkHandModel ||
                      model == alink->mpLinkHatModel || model == alink->mpLinkFaceModel ||
                      model == alink->mpLinkBootModels[0] || model == alink->mpLinkBootModels[1];
    return body ? HOOK_SKIP_ORIGINAL : HOOK_CONTINUE;
}

struct HeldSwap {
    int link;
    Mtx saved;
};
const int kHeldMax = 8;
HeldSwap s_held[kHeldMax];
int s_heldCount = 0;

void hold_begin(Rig& rig, J3DModel* link, const LinkRest& lr, const int* joints, int count, int pod) {
    s_heldCount = 0;
    for (int n = 0; n < count; ++n) {
        const int l = joints[n];
        if (l < 0 || l >= lr.joints) continue;
        bool already = false;
        for (int i = 0; i < s_heldCount; ++i) already = already || s_held[i].link == l;
        const int j = his_joint_for(rig, lr, l);
        if (already || j < 0 || s_heldCount >= kHeldMax) continue;
        HeldSwap& h = s_held[s_heldCount++];
        h.link = l;
        cMtx_copy(link->getAnmMtx(l), h.saved);
        link->setAnmMtx(l, rig.rigid[j]);
    }

    const int spine = pod >= 0 && pod < lr.joints ? his_joint_for(rig, lr, 2) : -1;
    if (spine >= 0 && s_heldCount < kHeldMax) {
        const J3DTransformInfo& info =
            link->getModelData()->getJointNodePointer(static_cast<u16>(pod))->getTransformInfo();
        Mtx local, at;
        J3DGetTranslateRotateMtx(info.mRotation.x, info.mRotation.y, info.mRotation.z,
            info.mTranslate.x, info.mTranslate.y * rig.body->d.backDepth, info.mTranslate.z, local);
        MTXConcat(rig.rigid[spine], local, at);
        HeldSwap& h = s_held[s_heldCount++];
        h.link = pod;
        cMtx_copy(link->getAnmMtx(pod), h.saved);
        link->setAnmMtx(pod, at);
    }
}

HeldSwap s_localHeld[kHeldMax];
int s_localHeldCount = 0;

void hold_end(J3DModel* link) {
    if (link != nullptr) {
        for (int i = 0; i < s_heldCount; ++i) link->setAnmMtx(s_held[i].link, s_held[i].saved);
    }
    s_heldCount = 0;
}

HookAction on_item_matrix_pre(ModContext*, void* args, void*, void*) {
    s_heldCount = 0;
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    if (alink != daAlink_getAlinkActorClass()) return HOOK_CONTINUE;
    s_localHeldCount = 0;
    if (!standing_in(alink)) return HOOK_CONTINUE;
    J3DModel* link = alink->mpLinkModel;
    LinkRest* lr = link_rest(link->getModelData(), *s_local.body, s_local.model->getModelData());
    if (lr == nullptr) return HOOK_CONTINUE;
    s_local.scale = lr->scale;
    retarget(s_local, link, *lr);
    const int joints[3] = {alink->mLeftItemJntNo, alink->mRightItemJntNo, 4};
    hold_begin(s_local, link, *lr, joints, 3, alink->field_0x30b6);
    for (int i = 0; i < s_heldCount; ++i) {
        s_localHeld[i].link = s_held[i].link;
        cMtx_copy(link->getAnmMtx(s_held[i].link), s_localHeld[i].saved);
    }
    s_localHeldCount = s_heldCount;
    return HOOK_CONTINUE;
}

void on_item_matrix_post(ModContext*, void* args, void*, void*) {
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    hold_end(alink != nullptr ? alink->mpLinkModel : nullptr);
}

struct ShadowSwap {
    bool on = false;
    J3DModel* body = nullptr;
    J3DModel* face = nullptr;
    J3DModel* hat = nullptr;
    J3DModel* hand = nullptr;
};
ShadowSwap s_shadow;

HookAction on_shadow_pre(ModContext*, void* args, void*, void*) {
    s_shadow.on = false;
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    if (alink != daAlink_getAlinkActorClass()) return HOOK_CONTINUE;
    const bool wolf = alink->checkWolf();
    J3DModel* ours = wolf ? beast_local_model() : (standing_in(alink) ? s_local.model : nullptr);
    if (ours == nullptr) return HOOK_CONTINUE;
    s_shadow.on = true;
    s_shadow.body = alink->mpLinkModel;
    s_shadow.face = alink->mpLinkFaceModel;
    s_shadow.hat = alink->mpLinkHatModel;
    s_shadow.hand = alink->mpLinkHandModel;
    alink->mpLinkModel = ours;
    if (!wolf) {
        alink->mpLinkFaceModel = nullptr;
        alink->mpLinkHatModel = nullptr;
        alink->mpLinkHandModel = nullptr;
    }
    return HOOK_CONTINUE;
}

void on_shadow_post(ModContext*, void* args, void*, void*) {
    if (!s_shadow.on) return;
    s_shadow.on = false;
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    if (alink == nullptr) return;
    alink->mpLinkModel = s_shadow.body;
    alink->mpLinkFaceModel = s_shadow.face;
    alink->mpLinkHatModel = s_shadow.hat;
    alink->mpLinkHandModel = s_shadow.hand;
}

bool any_local_choice(const Body& b) {
    if (&b == &s_bodies[kGanon] && s_on) return true;
    for (int slot = kSkinChoiceHero; slot <= kSkinChoiceMagic; ++slot) {
        if (skins_local_slot(slot) == b.d.skin) return true;
    }
    if (&b == &s_bodies[kGanon]) return local_piece(kSkinChoiceMasterSword) == kGanondorfSkin;
    return local_piece(kSkinChoiceOrdonSword) == kHerosShadeSkin ||
           local_piece(kSkinChoiceHylianShield) == kHerosShadeSkin;
}

u32 s_frame = 0;
const u32 kRigIdleFrames = 180;

Rig* puppet_rig(int pupId, int body) {
    if (body < 0 || body >= kBodies) return nullptr;
    Body& b = s_bodies[body];
    for (int i = 1; i < kRigs; ++i) {
        Rig& rig = s_rigs[i];
        if (!rig.used || rig.owner != pupId) continue;
        if (rig.body == &b) return &rig;
        coop_log::info("coop_mod: [{}] player {} changed looks (rig {})", rig.body->d.tag, pupId, i);
        rig_free(rig);
    }
    if (!arc_ready(b)) return nullptr;
    for (int i = 1; i < kRigs; ++i) {
        Rig& rig = s_rigs[i];
        if (rig.used) continue;
        if (!rig_build(rig, b)) return nullptr;
        rig.owner = pupId;
        coop_log::info("coop_mod: [{}] player {} is {} (rig {})", b.d.tag, pupId, b.d.skin, i);
        return &rig;
    }
    return nullptr;
}

Rig* puppet_rig_any(int pupId) {
    for (int i = 1; i < kRigs; ++i) {
        if (s_rigs[i].used && s_rigs[i].owner == pupId) return &s_rigs[i];
    }
    return nullptr;
}

struct PuppetSword {
    int owner = -1;
    u32 lastUsed = 0;
    J3DModel* sword = nullptr;
    J3DModel* sheath = nullptr;
    J3DModel* shadeSword = nullptr;
    J3DModel* shadeShield = nullptr;
    J3DModel* shadeSheath = nullptr;
};
PuppetSword s_puppetSwords[kRigs];

void puppet_sword_free(PuppetSword& ps) {
    for (J3DModel* m : {ps.sword, ps.sheath, ps.shadeSword, ps.shadeShield, ps.shadeSheath}) {
        if (m != nullptr) coop_free_model(m);
    }
    ps = PuppetSword{};
}

PuppetSword* puppet_sword(int pupId) {
    for (PuppetSword& ps : s_puppetSwords) {
        if (ps.owner == pupId) return &ps;
    }
    for (PuppetSword& ps : s_puppetSwords) {
        if (ps.owner >= 0) continue;
        ps.owner = pupId;
        return &ps;
    }
    return nullptr;
}

J3DModel* piece_model(J3DModel*& slot, J3DModelData* data) {
    if (slot == nullptr && data != nullptr) slot = coop_create_model(data, 0x80000, 0x11000284);
    return slot;
}

void sweep_rigs() {
    for (int i = 1; i < kRigs; ++i) {
        Rig& rig = s_rigs[i];
        if (!rig.used || s_frame - rig.lastUsed < kRigIdleFrames) continue;
        coop_log::info("coop_mod: [{}] player {} is not {} any more (rig {})", rig.body->d.tag,
            rig.owner, rig.body->d.skin, i);
        rig_free(rig);
    }
    for (PuppetSword& ps : s_puppetSwords) {
        if (ps.owner >= 0 && s_frame - ps.lastUsed >= kRigIdleFrames) puppet_sword_free(ps);
    }
}

const int kPuppetHeld[5] = {4, 9, 10, 14, 15};

}

const char* const kGanondorfSkin = "Ganondorf";
const char* const kHerosShadeSkin = "Hero's Shade";

int standin_body(const char* skin) {
    return body_index(body_named(skin));
}

bool standin_puppet_ready(int pupId, int body) {
    Rig* rig = puppet_rig(pupId, body);
    if (rig == nullptr) return false;
    rig->lastUsed = s_frame;
    return true;
}

void standin_puppet_draw(int pupId, J3DModel* body, uint8_t handL, uint8_t handR, dKy_tevstr_c* tev) {
    Rig* rig = puppet_rig_any(pupId);
    if (rig == nullptr || body == nullptr) return;
    rig->lastUsed = s_frame;
    rig_draw(*rig, body, handL < 0x80, handR < 0x80, tev, false);
}

void standin_puppet_hold_begin(int pupId, J3DModel* body, int podJoint) {
    Rig* rig = puppet_rig_any(pupId);
    if (rig == nullptr || body == nullptr || rig->model == nullptr) return;
    LinkRest* lr = link_rest(body->getModelData(), *rig->body, rig->model->getModelData());
    if (lr == nullptr) return;
    hold_begin(*rig, body, *lr, kPuppetHeld, 5, podJoint);
}

void standin_puppet_hold_end(J3DModel* body) {
    hold_end(body);
}

MtxP standin_local_joint_mtx(J3DModel* link, int joint) {
    for (int i = 0; i < s_localHeldCount; ++i) {
        if (s_localHeld[i].link == joint) return s_localHeld[i].saved;
    }
    return link->getAnmMtx(joint);
}

J3DModel* standin_puppet_model(int pupId) {
    Rig* rig = puppet_rig_any(pupId);
    return rig != nullptr ? rig->model : nullptr;
}

bool ganon_debug_is_on() {
    return s_on;
}

J3DModel* ganon_puppet_sword(int pupId, bool sheath) {
    if (!sword_data()) return nullptr;
    PuppetSword* ps = puppet_sword(pupId);
    if (ps == nullptr) return nullptr;
    ps->lastUsed = s_frame;
    return sheath ? piece_model(ps->sheath, s_sheathData) : piece_model(ps->sword, s_swordData);
}

J3DModel* shade_puppet_weapon(int pupId, int piece) {
    if (!shade_weapon_data()) return nullptr;
    PuppetSword* ps = puppet_sword(pupId);
    if (ps == nullptr) return nullptr;
    ps->lastUsed = s_frame;
    switch (piece) {
    case kShadePieceSword: return piece_model(ps->shadeSword, s_shadeSwordData);
    case kShadePieceShield: return piece_model(ps->shadeShield, s_shadeShieldData);
    case kShadePieceSheath: return piece_model(ps->shadeSheath, s_shadeSheathData);
    default: return nullptr;
    }
}

bool standin_owns_data(const J3DModelData* data) {
    return data != nullptr &&
           (data == s_swordData || data == s_sheathData || data == s_shadeSwordData ||
               data == s_shadeShieldData || data == s_shadeSheathData);
}

bool shade_skin_ships(const char* file) {
    return file != nullptr &&
           (std::strcmp(file, "al_swa.bmd") == 0 || std::strcmp(file, "al_poda.bmd") == 0 ||
               std::strcmp(file, "al_sha.bmd") == 0);
}

bool ganon_skin_ships(const char* file) {
    return file != nullptr &&
           (std::strcmp(file, "al_swm.bmd") == 0 || std::strcmp(file, "al_podm.bmd") == 0);
}

J3DModelData* ganon_skin_equipment(const char*) {
    return nullptr;
}

void ganon_init() {
    ConfigVarDesc autoDesc = CONFIG_VAR_DESC_INIT;
    autoDesc.name = "debug_ganon";
    autoDesc.type = CONFIG_VAR_BOOL;
    autoDesc.default_bool = false;
    if (svc_config->register_var(mod_ctx, &autoDesc, &s_autoVar) != MOD_OK) s_autoVar = 0;
    const ModResult r = mods::hook::add_pre<GanonModelDrawHook>(on_model_draw_pre);
    const ModResult held = mods::hook::add_pre<GanonItemMatrixHook>(on_item_matrix_pre);
    const ModResult heldPost = mods::hook::add_post<GanonItemMatrixHook>(on_item_matrix_post);
    const ModResult shadow = mods::hook::add_pre<GanonShadowHook>(on_shadow_pre);
    const ModResult shadowPost = mods::hook::add_post<GanonShadowHook>(on_shadow_post);
    coop_log::info("coop_mod: [GANON] body-draw hook: {}, held items: {}/{}, shadow: {}/{}",
        static_cast<int>(r), static_cast<int>(held), static_cast<int>(heldPost),
        static_cast<int>(shadow), static_cast<int>(shadowPost));
    beast_init();
}

bool ganon_debug_toggle() {
    s_on = !s_on;
    coop_log::info("coop_mod: [GANON] Debug switch {}", s_on ? "on" : "off");
    return s_on;
}

void ganon_draw_post(daAlink_c* alink) {
    ++s_frame;
    sweep_rigs();
    beast_frame(alink);
    if (!s_autoChecked) {
        s_autoChecked = true;
        if (!s_on && cfg_bool(s_autoVar, false)) ganon_debug_toggle();
    }

    for (Body& b : s_bodies) {
        if (b.arc == kArcMounting || (b.arc == kArcNone && any_local_choice(b))) arc_ready(b);
    }
    Body* want = local_body(alink);
    if (want == nullptr || !arc_ready(*want)) return;
    if (s_local.used && s_local.body != want) rig_free(s_local);
    if (s_local.model == nullptr) {
        if (want->localFailed) return;
        if (!rig_build(s_local, *want)) {
            want->localFailed = true;
            coop_log::warn("coop_mod: [{}] could not build him", want->d.tag);
            features_toast("Couldn't load that model", "It failed to load - see the log.");
            return;
        }
    }
    const bool gripL = gripping(alink, alink->field_0x06d0);
    const bool gripR = gripping(alink, alink->field_0x06d4);
    rig_draw(s_local, alink->mpLinkModel, gripL, gripR, &alink->tevStr, true);
}

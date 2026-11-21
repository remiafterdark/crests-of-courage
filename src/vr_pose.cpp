#include "vr_pose.hpp"

#include "print.hpp"
#include "vr_pose_api.h"

#include "mods/svc/hook.hpp"

#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphBase/J3DTransform.h"
#include "d/actor/d_a_alink.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------------------------
// Small self-contained math on the game's 3x4 row-major matrices (column 3 = translation).
// ---------------------------------------------------------------------------------------------

struct V3 {
    float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(V3 a) { return std::sqrt(dot(a, a)); }

typedef float M34[3][4];

constexpr float kPi = 3.14159265f;

V3 mpos(const M34 m) { return {m[0][3], m[1][3], m[2][3]}; }
void mset_pos(M34 m, V3 p) {
    m[0][3] = p.x;
    m[1][3] = p.y;
    m[2][3] = p.z;
}
void midentity(M34 m) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) m[r][c] = r == c ? 1.0f : 0.0f;
    }
}
void mcopy(const M34 a, M34 out) { std::memcpy(out, a, sizeof(M34)); }
// out = a * b (affine). out may alias neither.
void mconcat(const M34 a, const M34 b, M34 out) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            float v = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
            if (c == 3) v += a[r][3];
            out[r][c] = v;
        }
    }
}
V3 mrot(const M34 m, V3 v) {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
        m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
        m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}
V3 mmul(const M34 m, V3 v) { return mrot(m, v) + mpos(m); }
// General affine inverse (the joint matrices can carry a little scale).
bool minverse(const M34 m, M34 out) {
    const float a = m[0][0], b = m[0][1], c = m[0][2];
    const float d = m[1][0], e = m[1][1], f = m[1][2];
    const float g = m[2][0], h = m[2][1], i = m[2][2];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.0f / det;
    out[0][0] = A * inv;
    out[0][1] = -(b * i - c * h) * inv;
    out[0][2] = (b * f - c * e) * inv;
    out[1][0] = B * inv;
    out[1][1] = (a * i - c * g) * inv;
    out[1][2] = -(a * f - c * d) * inv;
    out[2][0] = C * inv;
    out[2][1] = -(a * h - b * g) * inv;
    out[2][2] = (a * e - b * d) * inv;
    const V3 t = mrot(out, mpos(m));
    mset_pos(out, {-t.x, -t.y, -t.z});
    return true;
}
void mrot_y(float rad, M34 out) {
    midentity(out);
    const float s = std::sin(rad), c = std::cos(rad);
    out[0][0] = c;
    out[0][2] = s;
    out[2][0] = -s;
    out[2][2] = c;
}
void mrot_axis(V3 axis, float rad, M34 out) {
    midentity(out);
    const float s = std::sin(rad), c = std::cos(rad), t = 1.0f - c;
    const float x = axis.x, y = axis.y, z = axis.z;
    out[0][0] = t * x * x + c;
    out[0][1] = t * x * y - s * z;
    out[0][2] = t * x * z + s * y;
    out[1][0] = t * x * y + s * z;
    out[1][1] = t * y * y + c;
    out[1][2] = t * y * z - s * x;
    out[2][0] = t * x * z - s * y;
    out[2][1] = t * y * z + s * x;
    out[2][2] = t * z * z + c;
}

// Quaternions (x, y, z, w) <-> rotation part of a matrix.
void quat_normalize(float q[4]) {
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l < 1e-6f) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 4; ++i) q[i] /= l;
}
// Columns are normalized first, so a matrix with a little scale still gives its rotation.
void quat_from_mtx(const M34 mIn, float q[4]) {
    M34 m;
    mcopy(mIn, m);
    for (int c = 0; c < 3; ++c) {
        const float l = std::sqrt(m[0][c] * m[0][c] + m[1][c] * m[1][c] + m[2][c] * m[2][c]);
        if (l > 1e-6f) {
            for (int r = 0; r < 3; ++r) m[r][c] /= l;
        }
    }
    const float tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        q[3] = (m[2][1] - m[1][2]) / s;
        q[0] = 0.25f * s;
        q[1] = (m[0][1] + m[1][0]) / s;
        q[2] = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        q[3] = (m[0][2] - m[2][0]) / s;
        q[0] = (m[0][1] + m[1][0]) / s;
        q[1] = 0.25f * s;
        q[2] = (m[1][2] + m[2][1]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        q[3] = (m[1][0] - m[0][1]) / s;
        q[0] = (m[0][2] + m[2][0]) / s;
        q[1] = (m[1][2] + m[2][1]) / s;
        q[2] = 0.25f * s;
    }
    quat_normalize(q);
}
void mtx_from_quat(const float q[4], M34 out) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    out[0][0] = 1 - 2 * (y * y + z * z);
    out[0][1] = 2 * (x * y - z * w);
    out[0][2] = 2 * (x * z + y * w);
    out[1][0] = 2 * (x * y + z * w);
    out[1][1] = 1 - 2 * (x * x + z * z);
    out[1][2] = 2 * (y * z - x * w);
    out[2][0] = 2 * (x * z - y * w);
    out[2][1] = 2 * (y * z + x * w);
    out[2][2] = 1 - 2 * (x * x + y * y);
    out[0][3] = out[1][3] = out[2][3] = 0.0f;
}
// Normalized lerp along the short way round; fine for the small per-tick steps it eases.
void quat_nlerp(float q[4], const float to[4], float t) {
    const float d = q[0] * to[0] + q[1] * to[1] + q[2] * to[2] + q[3] * to[3];
    const float sign = d < 0.0f ? -1.0f : 1.0f;
    for (int i = 0; i < 4; ++i) q[i] = q[i] + (to[i] * sign - q[i]) * t;
    quat_normalize(q);
}

float yaw_rad(int16_t s) { return static_cast<float>(s) * (kPi / 32768.0f); }

int16_t pack_unit(float v) {
    const float s = std::round(std::max(-1.0f, std::min(1.0f, v)) * kVrQuatScale);
    return static_cast<int16_t>(s);
}
int16_t pack_pos(float v) {
    const float s = std::round(v * kVrPosScale);
    return static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, s)));
}
void pack_quat(const float qIn[4], int16_t out[4]) {
    float q[4] = {qIn[0], qIn[1], qIn[2], qIn[3]};
    if (q[3] < 0.0f) {
        for (float& v : q) v = -v;
    }
    for (int i = 0; i < 4; ++i) out[i] = pack_unit(q[i]);
}
void unpack_quat(const int16_t in[4], float q[4]) {
    for (int i = 0; i < 4; ++i) q[i] = static_cast<float>(in[i]) / kVrQuatScale;
    quat_normalize(q);
}

// ---------------------------------------------------------------------------------------------
// Sender
// ---------------------------------------------------------------------------------------------

DuskVrGetPlayerPoseFn s_getPose = nullptr;
bool s_resolved = false;

void resolve_pose_fn() {
    if (s_resolved || svc_hook == nullptr) return;
    s_resolved = true;
    void* addr = nullptr;
    if (svc_hook->resolve(mod_ctx, DUSK_VR_GET_PLAYER_POSE_SYMBOL, &addr, nullptr) == MOD_OK &&
        addr != nullptr)
    {
        s_getPose = reinterpret_cast<DuskVrGetPlayerPoseFn>(addr);
    }
    coop_log::info("coop_mod: [VR] game {} the VR pose API - {}", s_getPose != nullptr ? "has" : "lacks",
        s_getPose != nullptr ? "sending our head and hands" : "flatscreen only, still showing VR players");
}

// ---------------------------------------------------------------------------------------------
// Receiver: pose the puppet (runs from J3DModel::calc()'s calc callback)
// ---------------------------------------------------------------------------------------------

constexpr int kHeadJoint = 4;
constexpr int kLeftHandJoint = 9;
constexpr int kRightHandJoint = 0xE;
constexpr int kMaxJoints = 256;
// The head joint's eye point in its own frame (the VR port's setBodyPartPos() localEye); the
// VR camera sits here, so hands sent relative to the camera are relative to this point.
constexpr V3 kHeadLocalEye = {12.0f, -8.0f, 0.0f};
// Past Link's full reach the arm bones lengthen together up to this much (the VR port's
// game.vrArmStretchMax default).
constexpr float kArmMaxStretch = 1.25f;
// Keeps a puppet's neck sane if its body faces somewhere else than the sender's did.
constexpr float kMaxHeadTurnRad = 110.0f * kPi / 180.0f;
// Snapshots older than this (puppet ticks) stop posing.
constexpr uint32_t kStaleTicks = 15;
// How far the drawn pose moves toward the latest one per tick.
constexpr float kEase = 0.6f;

struct SkeletonCache {
    const J3DModelData* data = nullptr;
    u16 jointNum = 0;
    int parent[kMaxJoints];
    bool headRestValid = false;
    M34 headRest;  // rest rotation of the head joint relative to the model root
};
SkeletonCache s_skel;

void build_parents(J3DJoint* joint, int parent) {
    for (; joint != nullptr; joint = joint->getYounger()) {
        const int no = joint->getJntNo();
        if (no >= 0 && no < kMaxJoints) s_skel.parent[no] = parent;
        build_parents(joint->getChild(), no);
    }
}

bool ensure_skeleton(J3DModelData* data) {
    const u16 n = data->getJointNum();
    if (n == 0 || n > kMaxJoints) return false;
    if (s_skel.data == data && s_skel.jointNum == n) return true;
    s_skel.data = data;
    s_skel.jointNum = n;
    for (int& p : s_skel.parent) p = -1;
    build_parents(data->getJointNodePointer(0), -1);

    s_skel.headRestValid = false;
    if (n > kHeadJoint) {
        midentity(s_skel.headRest);
        for (int j = kHeadJoint; j >= 0; j = s_skel.parent[j]) {
            Mtx local;
            J3DGetTranslateRotateMtx(data->getJointNodePointer(static_cast<u16>(j))->getTransformInfo(), local);
            M34 tmp;
            mconcat(local, s_skel.headRest, tmp);
            mcopy(tmp, s_skel.headRest);
        }
        mset_pos(s_skel.headRest, {0, 0, 0});
        s_skel.headRestValid = true;
    }
    return true;
}

bool in_subtree(int joint, int root) {
    for (int j = joint; j >= 0; j = s_skel.parent[j]) {
        if (j == root) return true;
    }
    return false;
}

// anm[j] = delta * anm[j] for root and everything under it.
void apply_to_subtree(J3DModel* model, int root, const M34 delta) {
    for (u16 j = 0; j < s_skel.jointNum; ++j) {
        if (!in_subtree(j, root)) continue;
        M34 out;
        mconcat(delta, model->getAnmMtx(j), out);
        mcopy(out, model->getAnmMtx(j));
    }
}

void rot_from_to(V3 a, V3 b, M34 out) {
    midentity(out);
    const float la = len(a), lb = len(b);
    if (la < 1e-4f || lb < 1e-4f) return;
    a = a * (1.0f / la);
    b = b * (1.0f / lb);
    V3 axis = cross(a, b);
    const float s = len(axis);
    const float c = dot(a, b);
    if (s < 1e-6f) {
        if (c > 0.0f) return;
        axis = std::fabs(a.x) < 0.9f ? cross({1, 0, 0}, a) : cross({0, 1, 0}, a);
        mrot_axis(axis * (1.0f / len(axis)), kPi, out);
        return;
    }
    mrot_axis(axis * (1.0f / s), std::atan2(s, c), out);
}

// Rigid delta: rotate by rot's 3x3 about pivot.
void pivot_delta(const M34 rot, V3 pivot, M34 out) {
    mcopy(rot, out);
    mset_pos(out, {0, 0, 0});
    mset_pos(out, pivot - mrot(out, pivot));
}

// Scale by k along unit u through pivot.
void stretch_delta(V3 pivot, V3 u, float k, M34 out) {
    const float s = k - 1.0f;
    const float uv[3] = {u.x, u.y, u.z};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out[r][c] = (r == c ? 1.0f : 0.0f) + s * uv[r] * uv[c];
    }
    mset_pos(out, {0, 0, 0});
    mset_pos(out, pivot - mrot(out, pivot));
}

// Skips zero-length helper/twist joints, so the chain is hand -> forearm -> upper arm.
int parent_with_length(J3DModel* model, int joint) {
    const V3 p = mpos(model->getAnmMtx(joint));
    for (int j = s_skel.parent[joint]; j >= 0; j = s_skel.parent[j]) {
        if (len(mpos(model->getAnmMtx(j)) - p) > 3.0f) return j;
    }
    return -1;
}

// Two-bone arm IK, ported from the VR port's vr_link::detail::ikSolveArm(): upper arm swings
// about the shoulder so the elbow lands on the reach circle toward the pole, forearm aims at
// the target keeping the animation's forearm->hand twist, hand = target exactly.
void solve_arm(J3DModel* model, int hand, int elbow, int shoulder, const M34 target, V3 poleDir) {
    const V3 S = mpos(model->getAnmMtx(shoulder));
    const V3 E0 = mpos(model->getAnmMtx(elbow));
    const V3 W0 = mpos(model->getAnmMtx(hand));
    const V3 T = mpos(target);
    const float L1 = len(E0 - S);
    const float L2 = len(W0 - E0);
    const V3 toT = T - S;
    const float dRaw = len(toT);
    if (L1 < 1e-3f || L2 < 1e-3f || dRaw < 1e-3f) return;
    const V3 dir = toT * (1.0f / dRaw);
    const float stretch = std::min(std::max(dRaw / (L1 + L2), 1.0f), kArmMaxStretch);
    const float L1s = L1 * stretch;
    const float L2s = L2 * stretch;
    const float d = std::min(std::max(dRaw, std::fabs(L1s - L2s) + 0.01f), (L1s + L2s) * 0.999f);

    const float a = (L1s * L1s + d * d - L2s * L2s) / (2.0f * d);
    const float h = std::sqrt(std::max(0.0f, L1s * L1s - a * a));
    V3 p = poleDir - dir * dot(poleDir, dir);
    if (len(p) < 1e-4f) p = V3{0, -1, 0} - dir * (-dir.y);
    if (len(p) < 1e-4f) return;
    p = p * (1.0f / len(p));
    const V3 E = S + dir * a + p * h;

    M34 rot, delta;
    rot_from_to(E0 - S, E - S, rot);
    pivot_delta(rot, S, delta);
    apply_to_subtree(model, shoulder, delta);
    const bool stretched = stretch > 1.0001f;
    if (stretched) {
        const V3 u = (E - S) * (1.0f / len(E - S));
        stretch_delta(S, u, stretch, delta);
        apply_to_subtree(model, shoulder, delta);
    }

    float (*F)[4] = model->getAnmMtx(elbow);
    float (*H)[4] = model->getAnmMtx(hand);
    M34 fRot, hRot, fInv, rel, relInv, ideal, tRot;
    mcopy(F, fRot);
    mset_pos(fRot, {0, 0, 0});
    mcopy(H, hRot);
    mset_pos(hRot, {0, 0, 0});
    if (!minverse(fRot, fInv)) return;
    mconcat(fInv, hRot, rel);
    if (!minverse(rel, relInv)) return;
    mcopy(target, tRot);
    mset_pos(tRot, {0, 0, 0});
    mconcat(tRot, relInv, ideal);
    const V3 Ecur = mpos(F);
    const V3 boneLocal = mrot(fInv, mpos(H) - Ecur);
    const V3 boneIdeal = mrot(ideal, boneLocal);
    M34 swing, newF, curInv;
    rot_from_to(boneIdeal, T - Ecur, swing);
    mconcat(swing, ideal, newF);
    mset_pos(newF, Ecur);
    if (!minverse(F, curInv)) return;
    mconcat(newF, curInv, delta);
    apply_to_subtree(model, elbow, delta);
    if (stretched) {
        V3 v = T - Ecur;
        if (len(v) > 1e-4f) {
            v = v * (1.0f / len(v));
            stretch_delta(Ecur, v, stretch, delta);
            apply_to_subtree(model, elbow, delta);
        }
    }

    if (!minverse(model->getAnmMtx(hand), curInv)) return;
    mconcat(target, curInv, delta);
    apply_to_subtree(model, hand, delta);
}

// Turns the head joint (and everything on it) to the headset: head world rotation =
// headset * rest (the VR port's ikAttachHeadToHeadset()), about the head joint itself so it
// stays on the neck, limited to kMaxHeadTurnRad away from the animated head.
void pose_head(J3DModel* model, const M34 headWorld) {
    float (*cur)[4] = model->getAnmMtx(kHeadJoint);
    M34 want, curRot, curInv, rot;
    mconcat(headWorld, s_skel.headRest, want);
    mcopy(cur, curRot);
    mset_pos(curRot, {0, 0, 0});
    if (!minverse(curRot, curInv)) return;
    mconcat(want, curInv, rot);

    float q[4];
    quat_from_mtx(rot, q);
    const float angle = 2.0f * std::acos(std::min(1.0f, std::fabs(q[3])));
    if (angle > kMaxHeadTurnRad) {
        const float ident[4] = {0, 0, 0, 1};
        float limited[4] = {ident[0], ident[1], ident[2], ident[3]};
        quat_nlerp(limited, q, kMaxHeadTurnRad / angle);
        mtx_from_quat(limited, rot);
    }
    M34 delta;
    pivot_delta(rot, mpos(cur), delta);
    apply_to_subtree(model, kHeadJoint, delta);
}

void ease_toward_latest(VrPuppetPose& pose) {
    if (!pose.smoothedValid) {
        std::memcpy(pose.drawHeadQuat, pose.headQuat, sizeof(pose.headQuat));
        std::memcpy(pose.drawHandPos, pose.handPos, sizeof(pose.handPos));
        std::memcpy(pose.drawHandQuat, pose.handQuat, sizeof(pose.handQuat));
        pose.smoothedValid = true;
        return;
    }
    quat_nlerp(pose.drawHeadQuat, pose.headQuat, kEase);
    for (int s = 0; s < 2; ++s) {
        quat_nlerp(pose.drawHandQuat[s], pose.handQuat[s], kEase);
        for (int i = 0; i < 3; ++i) {
            pose.drawHandPos[s][i] += (pose.handPos[s][i] - pose.drawHandPos[s][i]) * kEase;
        }
    }
}

void apply_pose(J3DModel* model, VrPuppetPose& pose, int16_t yaw) {
    J3DModelData* data = model->getModelData();
    if (data == nullptr || data->getJointNum() <= kRightHandJoint) return;
    if (!ensure_skeleton(data) || !s_skel.headRestValid) return;

    M34 facing;
    mrot_y(yaw_rad(yaw), facing);

    M34 headLocal, headWorld;
    mtx_from_quat(pose.drawHeadQuat, headLocal);
    mconcat(facing, headLocal, headWorld);
    pose_head(model, headWorld);

    const V3 eye = mmul(model->getAnmMtx(kHeadJoint), kHeadLocalEye);
    const int hands[2] = {kLeftHandJoint, kRightHandJoint};
    const uint8_t handFlags[2] = {kVrPoseLeftHand, kVrPoseRightHand};
    int elbows[2], shoulders[2];
    for (int s = 0; s < 2; ++s) {
        elbows[s] = parent_with_length(model, hands[s]);
        shoulders[s] = elbows[s] >= 0 ? parent_with_length(model, elbows[s]) : -1;
    }
    if (shoulders[0] >= 0 && shoulders[1] >= 0 && (pose.flags & (kVrPoseLeftHand | kVrPoseRightHand)) != 0) {
        // Pole: elbows point down, outward (away from the other shoulder) and a little back.
        V3 out = mpos(model->getAnmMtx(shoulders[0])) - mpos(model->getAnmMtx(shoulders[1]));
        out.y = 0.0f;
        if (len(out) > 1e-3f) out = out * (1.0f / len(out));
        const float yr = yaw_rad(yaw);
        const V3 back = {-std::sin(yr), 0.0f, -std::cos(yr)};
        const V3 down = {0.0f, -1.0f, 0.0f};
        for (int s = 0; s < 2; ++s) {
            if ((pose.flags & handFlags[s]) == 0) continue;
            M34 handLocal, target;
            mtx_from_quat(pose.drawHandQuat[s], handLocal);
            mconcat(facing, handLocal, target);
            const V3 rel = mrot(facing, {pose.drawHandPos[s][0], pose.drawHandPos[s][1], pose.drawHandPos[s][2]});
            mset_pos(target, eye + rel);
            const V3 pole = down + out * (s == 0 ? 0.6f : -0.6f) + back * 0.3f;
            solve_arm(model, hands[s], elbows[s], shoulders[s], target, pole);
        }
    }

    if (data->getWEvlpMtxNum() > 0) model->calcWeightEnvelopeMtx();
}

struct CalcContext {
    VrPuppetPose* pose = nullptr;
    int16_t yaw = 0;
    J3DCalcCallBack chained = nullptr;
};
CalcContext s_calc;

void pose_calc_callback(J3DModel* model, u32 timing) {
    if (s_calc.chained != nullptr) s_calc.chained(model, timing);
    if (s_calc.pose == nullptr || model == nullptr) return;
    apply_pose(model, *s_calc.pose, s_calc.yaw);
}

}  // namespace

bool vr_pose_game_has_vr() {
    resolve_pose_fn();
    return s_getPose != nullptr;
}

void vr_pose_capture(daAlink_c* alink, VrPoseSnapshot& out, int16_t* angleY) {
    std::memset(&out, 0, sizeof(out));
    resolve_pose_fn();
    if (s_getPose == nullptr || alink == nullptr) return;
    DuskVrPlayerPose pose{};
    pose.size = sizeof(pose);
    if (!s_getPose(&pose) || (pose.flags & DUSK_VR_POSE_HEAD) == 0) return;

    // The facing we send: the drawn (headset-turned) yaw when the VR port turns the body, else
    // his real facing. Everything below is relative to it, and the receiver rebuilds it on the
    // puppet's facing, which comes from this same angle.
    int16_t facing = alink->shape_angle.y;
    if ((pose.flags & DUSK_VR_POSE_BODY_YAW) != 0) {
        facing = static_cast<int16_t>(pose.bodyYaw);
        if (angleY != nullptr) *angleY = facing;
    }

    // Into Link's frame: undo his facing.
    M34 unface;
    mrot_y(-yaw_rad(facing), unface);

    M34 headWorld, headLocal;
    mcopy(pose.head, headWorld);
    mset_pos(headWorld, {0, 0, 0});
    mconcat(unface, headWorld, headLocal);
    float q[4];
    quat_from_mtx(headLocal, q);
    pack_quat(q, out.headQuat);
    out.flags |= kVrPoseHead;

    const V3 eye = mpos(pose.head);
    const float (*handMtx[2])[4] = {pose.leftHand, pose.rightHand};
    const uint32_t apiFlags[2] = {DUSK_VR_POSE_LEFT_HAND, DUSK_VR_POSE_RIGHT_HAND};
    const uint8_t snapFlags[2] = {kVrPoseLeftHand, kVrPoseRightHand};
    for (int s = 0; s < 2; ++s) {
        if ((pose.flags & apiFlags[s]) == 0) continue;
        M34 handWorld, handLocal;
        mcopy(handMtx[s], handWorld);
        const V3 rel = mrot(unface, mpos(handWorld) - eye);
        mset_pos(handWorld, {0, 0, 0});
        mconcat(unface, handWorld, handLocal);
        quat_from_mtx(handLocal, q);
        pack_quat(q, out.handQuat[s]);
        out.handPos[s][0] = pack_pos(rel.x);
        out.handPos[s][1] = pack_pos(rel.y);
        out.handPos[s][2] = pack_pos(rel.z);
        out.flags |= snapFlags[s];
    }
}

void vr_pose_reset(VrPuppetPose& pose) {
    pose = VrPuppetPose{};
}

void vr_pose_receive(VrPuppetPose& pose, const VrPoseSnapshot& snap, uint32_t frame) {
    if ((snap.flags & kVrPoseHead) == 0) {
        pose.flags = 0;
        pose.smoothedValid = false;
        return;
    }
    // A hand that just started tracking snaps instead of easing in from wherever it last was.
    const uint8_t newlyOn = static_cast<uint8_t>(snap.flags & ~pose.flags);
    pose.flags = snap.flags;
    pose.lastFrame = frame;
    unpack_quat(snap.headQuat, pose.headQuat);
    for (int s = 0; s < 2; ++s) {
        unpack_quat(snap.handQuat[s], pose.handQuat[s]);
        for (int i = 0; i < 3; ++i) pose.handPos[s][i] = static_cast<float>(snap.handPos[s][i]) / kVrPosScale;
        if ((newlyOn & (s == 0 ? kVrPoseLeftHand : kVrPoseRightHand)) != 0) {
            std::memcpy(pose.drawHandQuat[s], pose.handQuat[s], sizeof(pose.handQuat[s]));
            std::memcpy(pose.drawHandPos[s], pose.handPos[s], sizeof(pose.handPos[s]));
        }
    }
}

bool vr_pose_active(const VrPuppetPose& pose, uint32_t frame, uint8_t flags) {
    return (pose.flags & flags) == flags && (pose.flags & kVrPoseHead) != 0 &&
           frame - pose.lastFrame <= kStaleTicks;
}

VrPoseCalcScope::VrPoseCalcScope(J3DModel* model, VrPuppetPose& pose, int16_t yaw, uint32_t frame,
    bool wolf) {
    if (model == nullptr || wolf || !vr_pose_active(pose, frame, kVrPoseHead)) return;
    if (pose.easedFrame != frame) {
        pose.easedFrame = frame;
        ease_toward_latest(pose);
    }
    model_ = model;
    saved_ = reinterpret_cast<void*>(model->mCalcCallBack);
    s_calc.pose = &pose;
    s_calc.yaw = yaw;
    s_calc.chained = model->mCalcCallBack;
    model->mCalcCallBack = pose_calc_callback;
    installed_ = true;
}

VrPoseCalcScope::~VrPoseCalcScope() {
    if (!installed_) return;
    model_->mCalcCallBack = reinterpret_cast<J3DCalcCallBack>(saved_);
    s_calc = CalcContext{};
}

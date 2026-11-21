#pragma once

// Crests of Courage VR: shows VR players' head and tracked hands to everyone else.
//
// Sending: on the Dusklight VR port, the game exports dusk_vr_get_player_pose()
// (vr_pose_api.h). vr_pose_capture() reads it each snapshot and packs it into
// PlayerSnapshot::vr. On regular Dusklight the symbol isn't there and nothing is sent, so the
// mod still works there as a normal (flatscreen) client that can see VR players.
//
// Receiving: vr_pose_receive() keeps the latest pose per puppet. While the puppet's body model
// calcs, vr_pose_install_calc() makes J3DModel::calc() call back into vr_pose.cpp after the
// animated pose and skinning matrices are built and before frame interpolation records them:
// the head joint is turned to the headset and both arms are solved onto the hands (two-bone
// IK, the same solver the VR port uses for Link's own body), then the skinning matrices are
// rebuilt. Hands are placed relative to the puppet's own head, so they stay on the body.

#include "net/protocol.hpp"

#include <cstdint>

class daAlink_c;
class J3DModel;

struct VrPuppetPose {
    uint8_t flags = 0;
    uint32_t lastFrame = 0;
    uint32_t easedFrame = ~0u;
    bool smoothedValid = false;
    // Latest received pose (targets) and what's drawn (eased toward them each tick).
    float headQuat[4] = {0, 0, 0, 1};
    float handPos[2][3] = {};
    float handQuat[2][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}};
    float drawHeadQuat[4] = {0, 0, 0, 1};
    float drawHandPos[2][3] = {};
    float drawHandQuat[2][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}};
};

// Sender. Fills snapshot.vr (all zero when there's no VR pose to show). `angleY` is the facing
// being sent (shape_angle.y): when the VR port draws the body turned to the headset, it's
// replaced with that drawn yaw, so others see the body facing the way the VR player sees it.
void vr_pose_capture(daAlink_c* alink, VrPoseSnapshot& out, int16_t* angleY);
// True once the game turned out to be the VR port (dusk_vr_get_player_pose resolved).
bool vr_pose_game_has_vr();

// Receiver.
void vr_pose_reset(VrPuppetPose& pose);
void vr_pose_receive(VrPuppetPose& pose, const VrPoseSnapshot& snap, uint32_t frame);
// The puppet's VR pose is current: recent enough and has these flags.
bool vr_pose_active(const VrPuppetPose& pose, uint32_t frame, uint8_t flags);

// Around the puppet body's calc(): installs the calc callback that applies `pose` (with the
// puppet facing `yaw`), and puts back whatever callback the model had. `wolf` puppets are left
// alone (no human arms or head to pose).
struct VrPoseCalcScope {
    VrPoseCalcScope(J3DModel* model, VrPuppetPose& pose, int16_t yaw, uint32_t frame, bool wolf);
    ~VrPoseCalcScope();
    VrPoseCalcScope(const VrPoseCalcScope&) = delete;
    VrPoseCalcScope& operator=(const VrPoseCalcScope&) = delete;

private:
    J3DModel* model_ = nullptr;
    void* saved_ = nullptr;
    bool installed_ = false;
};

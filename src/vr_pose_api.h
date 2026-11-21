#pragma once

/*
 * Copy of the Dusklight VR port's sdk/include/mods/vr_pose.h (JoeyAW/TPVR), kept here so the
 * mod also builds against regular Dusklight, which doesn't have it. The struct only grows at
 * the end, and the game reads `size` to see how much of it we have, so an older copy here
 * keeps working against a newer game.
 */

#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DUSK_VR_GET_PLAYER_POSE_SYMBOL "dusk_vr_get_player_pose"

enum DuskVrPoseFlags {
    DUSK_VR_POSE_HEAD = 1u << 0u,
    DUSK_VR_POSE_LEFT_HAND = 1u << 1u,
    DUSK_VR_POSE_RIGHT_HAND = 1u << 2u,
    DUSK_VR_POSE_BODY_YAW = 1u << 3u,
};

typedef struct DuskVrPlayerPose {
    uint32_t size;
    uint32_t flags;
    /* Rotation columns: Link's left, up, forward. Translation: the camera eye point. */
    float head[3][4];
    /* World matrices of Link's hand joints (9 = left, 0xE = right) while tracking. */
    float leftHand[3][4];
    float rightHand[3][4];
    /* With DUSK_VR_POSE_BODY_YAW: the yaw (shape_angle.y units) Link's body is DRAWN facing,
     * which the VR port turns to the headset while his real facing stays put. */
    int32_t bodyYaw;
} DuskVrPlayerPose;

typedef bool (*DuskVrGetPlayerPoseFn)(DuskVrPlayerPose* out);

#ifdef __cplusplus
}
#endif

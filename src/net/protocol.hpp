#pragma once

#include <cstdint>

enum PuppetOutfit : uint8_t {
    kPuppetOutfitDefault = 0,
    kPuppetOutfitCasual = 1,
    kPuppetOutfitZora = 2,
    kPuppetOutfitMagicArmor = 3,

    kPuppetOutfitWolf = 4,
};

enum PuppetSwordModel : uint8_t {
    kPuppetSwordNone = 0,
    kPuppetSwordOrdon = 1,
    kPuppetSwordMaster = 2,
    kPuppetSwordWood = 3,
};
enum PuppetSheathModel : uint8_t {
    kPuppetSheathNone = 0,
    kPuppetSheathOrdon = 1,
    kPuppetSheathMaster = 2,
};

enum PuppetHeldItem : uint8_t {
    kPuppetHeldNone = 0,
    kPuppetHeldBow = 1,
    kPuppetHeldSling = 2,
    kPuppetHeldHookshot = 3,
    kPuppetHeldIronBall = 4,
    kPuppetHeldCopyRod = 5,
    kPuppetHeldBottle = 6,
    kPuppetHeldHookTip = 7,
    kPuppetHeldLantern = 8,

    kPuppetHeldBoomerang = 9,

    kPuppetHeldWolfChain = 10,

    kPuppetHeldTransform = 11,

    kPuppetHeldSpinner = 12,

    kPuppetHeldFishRod = 13,

    kPuppetHeldLanternGlow = 14,

    kPuppetHeldRodFloat = 15,
    kPuppetHeldRodFloatTip = 16,
    kPuppetHeldRodHookA = 17,
    kPuppetHeldRodHookB = 18,

    kPuppetHeldArrow = 19,
    kPuppetHeldBombArrow = 20,
    kPuppetHeldSlingStone = 21,

    kPuppetHeldBoomerangWind = 22,

    kPuppetHeldBoomerangAimWind = 23,

    kPuppetHeldGetItem = 24,

    kPuppetHeldBomb = 25,

    kPuppetHeldRide = 26,

    kPuppetHeldRideExtra = 27,
    kPuppetHeldCount = 28,
};

const uint16_t kPuppetHeldJointRoot = 0xFFFF;

const int kPuppetAttachSlots = 8;

enum PuppetChainKind : uint8_t {
    kPuppetChainNone = 0,
    kPuppetChainHookshot = 1,
    kPuppetChainIronBall = 2,
    kPuppetChainRodUki = 3,
    kPuppetChainRodLure = 4,
};
const int kPuppetChainPts = 16;

enum PuppetSpinVfx : uint8_t {
    kSpinVfxNone = 0,
    kSpinVfxNormal = 1,
    kSpinVfxLight = 2,
    kSpinVfxLarge = 3,
    kSpinVfxWater = 4,
    kSpinVfxWolf = 5,
};
const uint8_t kSpinVfxVariantMask = 0x0F;
const uint8_t kSpinVfxFlagWaterLarge = 0x40;
const uint8_t kSpinVfxFlagNoLocalRot = 0x80;

const uint8_t kSpinVfxFlagWolfEmitB = 0x10;
const uint8_t kSpinVfxFlagWolfFlip = 0x20;

#pragma pack(push, 1)

struct AttachedModelSnapshot {
    uint8_t kind;
    uint16_t joint;

    uint16_t bckResIdx;
    float frame;
    float mtx[12];

    float scale;
};

struct AnmSlotSnapshot {
    uint16_t resIdx;
    float frame;
    float ratio;

    float rate;
};

struct PlayerSnapshot {
    uint32_t seq;

    uint8_t playerId;
    float posX, posY, posZ;
    int16_t angleX, angleY, angleZ;
    int8_t roomNo;
    uint8_t outfit;
    AnmSlotSnapshot under[3];
    AnmSlotSnapshot upper[3];

    uint8_t handL;
    uint8_t handR;

    uint8_t sword;
    uint8_t sheath;

    uint8_t swordVisible;
    uint8_t shieldVisible;

    uint8_t swordInHand;
    uint8_t shieldInHand;
    uint16_t swordJoint;
    uint16_t sheathJoint;
    uint16_t shieldJoint;

    char shieldArc[16];
    char rodArc[16];
    char rideArc[16];

    AttachedModelSnapshot attached[kPuppetAttachSlots];

    int16_t bodyRotX;
    int16_t bodyRotY;
    int16_t bodyRotZ;

    uint8_t bootsVisible;
    uint8_t chainKind;
    uint8_t chainCount;
    int16_t chainStopTime;
    float chainPts[kPuppetChainPts][3];

    uint8_t rootClearMask;
    float rootClearX;
    float rootClearY;
    float rootClearZ;

    int16_t footAngles[4][3];
    uint8_t vfxSpin;
    uint8_t vfxJumpLand;

    uint8_t vfxWolfDig;
    int16_t vfxDigAngleX;
    float vfxDigPos[3];

    uint8_t warpOn;
    float warpScroll;
    float warpDissolve;

    uint32_t worldTick;
};

const uint32_t kMidnaSnapshotMagic = 0x4D444E31u;

const int kMidnaJoints = 29;

enum PuppetMidnaMode : uint8_t {
    kMidnaModeNone = 0,

    kMidnaModeSolid = 1,

    kMidnaModeShadow = 2,
};

const uint8_t kMidnaFlagMask = 0x01;
const uint8_t kMidnaFlagHairhand = 0x02;
const uint8_t kMidnaFlagTevColor = 0x04;

const uint8_t kMidnaFlagWorldBase = 0x08;

struct MidnaJointSnapshot {
    int16_t rot[9];
    float pos[3];
};
const float kMidnaRotFixed = 8192.0f;

struct MidnaSnapshot {
    uint32_t magic;
    uint32_t seq;
    uint8_t playerId;
    uint8_t mode;
    uint8_t flags;
    uint8_t hairShape;

    uint16_t leftHand;
    uint16_t rightHand;
    float baseScale;
    float baseMtx[12];
    float hairMtx[12];
    int16_t tevColor[4];
    int16_t hairColor[4];
    uint8_t hairK1[4];
    uint8_t hairK2[4];
    MidnaJointSnapshot joints[kMidnaJoints];
};
#pragma pack(pop)

static_assert(sizeof(MidnaJointSnapshot) == 18 + 12, "MidnaJointSnapshot must stay packed");

const uint32_t kHorseSnapshotMagic = 0x31535248u;
const int kHorseJoints = 48;
const uint8_t kHorseFlagRiding = 0x01;

const uint8_t kHorseFlagIdle = 0x02;
const float kHorseQuatScale = 32767.0f;
const float kHorsePosScale = 16.0f;

struct HorseJointSnapshot {
    int16_t q[4];
    int16_t p[3];
};

const int kHorseReinPoints = 75;

struct HorseSnapshot {
    uint32_t magic;
    uint32_t seq;
    uint8_t playerId;
    uint8_t flags;
    uint8_t jointCount;
    uint8_t reinCount;
    float baseMtx[12];
    HorseJointSnapshot joints[kHorseJoints];
    int16_t reins[kHorseReinPoints][3];

    uint16_t idleAnm;
    float idleFrame;
    float idleRate;

    int8_t room;
    uint8_t pad2[3];
};
static_assert(sizeof(HorseJointSnapshot) == 14, "HorseJointSnapshot must stay packed");
static_assert(sizeof(HorseSnapshot) == 4 + 4 + 4 + 48 + kHorseJoints * 14 + kHorseReinPoints * 6 + 2 + 8 + 4,
    "HorseSnapshot must stay packed");

static_assert(sizeof(HorseSnapshot) <= 1200, "HorseSnapshot must fit one small datagram");
static_assert(sizeof(MidnaSnapshot) ==
        4 + 4 + 1 + 1 + 1 + 1 + 2 + 2 + 4 + 48 + 48 + 8 + 8 + 4 + 4 + (kMidnaJoints * 30),
    "MidnaSnapshot must stay packed");

static_assert(sizeof(AnmSlotSnapshot) == 2 + 4 + 4 + 4,
    "AnmSlotSnapshot must stay tightly packed");
static_assert(sizeof(AttachedModelSnapshot) == 1 + 2 + 2 + 4 + (12 * 4) + 4,
    "AttachedModelSnapshot must stay tightly packed");
static_assert(sizeof(PlayerSnapshot) ==
        4 + 1 + 12 + 6 + 1 + 1 + (3 * 14) + (3 * 14) + 1 + 1 + 6 + 6 + 16 +
            16 + 16 + (kPuppetAttachSlots * 61) + 6 + 1 + 1 + 1 + 2 +
            (kPuppetChainPts * 3 * 4) + 1 + 12 + 24 + 2 + 1 + 2 + 12 + 1 + 4 + 4 +
            4  ,
    "PlayerSnapshot must stay tightly packed");

static_assert(sizeof(MidnaSnapshot) != sizeof(PlayerSnapshot),
    "MidnaSnapshot and PlayerSnapshot must differ in size - handle_udp_event dispatches on it");
static_assert(sizeof(HorseSnapshot) != sizeof(PlayerSnapshot) &&
                  sizeof(HorseSnapshot) != sizeof(MidnaSnapshot),
    "HorseSnapshot must differ in size from the other datagrams - handle_udp_event dispatches on it");

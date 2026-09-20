#pragma once

#include <cstdint>

const uint16_t kCoopProtocolVersion = 41;

const int kCoopMaxPlayers = 4;
const uint8_t kCoopHostId = 0;
const uint8_t kCoopNoPlayer = 0xFF;

const uint16_t kCoopMaxMessagePayload = 8192;
const int kCoopNameMax = 24;
const int kCoopMaxSoundsPerMessage = 48;

enum CoopMsgType : uint8_t {
    kMsgHello = 1,
    kMsgPresence = 2,
    kMsgItem = 3,
    kMsgBottle = 4,
    kMsgTime = 5,
    kMsgSounds = 6,
    kMsgColors = 7,
    kMsgArrowShot = 8,
    kMsgParticles = 9,
    kMsgPvpState = 10,
    kMsgPvpHit = 11,
    kMsgWorldDelta = 12,
    kMsgJoinSync = 13,
    kMsgEnemyState = 14,
    kMsgEnemyGone = 15,
    kMsgEnemyDamage = 16,
    kMsgWorldSyncRequest = 17,
    kMsgWorldFull = 18,
    kMsgEnemyHit = 19,
    kMsgRoomClaim = 20,
    kMsgRoomOwner = 21,
    kMsgEnemyTargets = 22,
    kMsgActorSpawn = 23,
    kMsgActorState = 24,
    kMsgActorGone = 25,
    kMsgBossState = 26,
    kMsgBossReady = 27,
    kMsgBossHit = 28,
    kMsgWorldDigest = 29,
    kMsgDeathLink = 30,
    kMsgBossStem = 31,

    kMsgAssignId = 32,

    kMsgRoster = 33,
    kMsgItemTaken = 34,
    kMsgPing = 35,
    kMsgPong = 36,
    kMsgPillarShake = 37,
    kMsgBossBombEat = 38,

    kMsgObjectHit = 39,
    kMsgHorse = 40,
    kMsgObjectMove = 41,
    kMsgPause = 42,
    kMsgGrassCut = 43,
    kMsgSessionSettings = 44,
};

struct MsgPause {
    uint8_t paused;
};

struct MsgBossBombEat {
    uint8_t index;
    uint8_t phase2;
};

struct MsgPillarShake {
    int8_t room;
    uint8_t swBit;
    uint8_t shake;
    float homeX;
    float homeY;
    float homeZ;
};

struct MsgPing {
    uint32_t token;
};

struct MsgItemTaken {
    float home[3];
    uint8_t itemNo;
};

struct MsgAssignId {
    uint8_t playerId;
    uint8_t maxPlayers;
};

struct MsgRoster {
    uint8_t present;
};

enum CoopBossCollider : uint8_t {
    kBossColliderBody = 0,
    kBossColliderCore = 1,
    kBossColliderTentacle = 2,

    kBossColliderOok = 3,
};

const int16_t kBossProcDiababa = 0x20C;
const int16_t kBossProcTentacle = 0x20B;

const int16_t kBossProcOok = 0x1DC;
const int16_t kBossProcHelper = 0x1DB;
const int16_t kBossProcOokBoomerang = 0x2ED;
const int16_t kBossProcPillar = 0x033;

enum CoopBossKind : uint8_t {
    kBossKindNone = 0,
    kBossKindDiababa = 1,
    kBossKindTentacle = 2,
    kBossKindOok = 3,
    kBossKindHelper = 4,
    kBossKindOokBoomerang = 5,
};

enum CoopActorFlags : uint8_t {
    kActorFlagCarried = 1 << 0,
    kActorFlagHidden = 1 << 1,

    kActorFlagOwnerInCutscene = 1 << 2,
};

enum CoopRoomOwner : uint8_t {

    kRoomOwnerNone = 0,
    kRoomOwnerHost = 1,
    kRoomOwnerJoiner = 2,
};

const int kCoopMaxEnemiesPerMessage = 96;

enum CoopEnemyFlags : uint8_t {
    kEnemyFlagHidden = 1 << 0,

    kEnemyFlagCarried = 1 << 1,
};

const int kCoopColorSlots = 14;

enum CoopBottleEvent : uint8_t {
    kBottleFillEmpty = 0,
    kBottleNewFilled = 1,
};

enum CoopSoundKind : uint8_t {
    kSoundOneShot = 0,
    kSoundLevel = 1,
};

const int kCoopMaxParticlesPerMessage = 48;

enum CoopParticleFlags : uint8_t {
    kParticleKeyed = 1 << 0,
    kParticleHasRot = 1 << 1,
    kParticleHasScale = 1 << 2,
    kParticleHasPrm = 1 << 3,
    kParticleHasEnv = 1 << 4,
    kParticleHasState = 1 << 5,
};

#pragma pack(push, 1)

struct MsgHeader {
    uint16_t size;
    uint8_t type;
    uint8_t from;
};

struct MsgHello {
    uint16_t version;
    char name[kCoopNameMax];
};

struct MsgPresence {
    char name[kCoopNameMax];
    char stage[8];
    int8_t startRoom;
    int8_t curRoom;
    int8_t layer;
    int16_t point;
    float x, y, z;
    int16_t angleY;
    uint8_t inGame;

    uint16_t life;
    uint16_t maxLife;
};

struct MsgItem {
    uint8_t item;
};

struct MsgBottle {
    uint8_t event;
    uint8_t item;
};

struct MsgTime {
    float time;
};

struct MsgSoundEntry {
    uint32_t id;
    uint8_t kind;
    float rel[3];
};

struct MsgParticleEntry {
    uint16_t id;
    uint8_t flags;
    uint8_t type;
    uint32_t key;
    float rel[3];
    int16_t rot[3];
    float scale[3];

    uint8_t alpha;
    uint8_t prm[4];
    uint8_t env[4];
    float blend;

    float rate;
    uint8_t livePrm[4];
    uint8_t liveEnv[4];
    float gscl[3];
    float pscl[2];
    int16_t life;
    uint16_t vol;
    float awayCenter;
    float awayAxis;
    float dirSpeed;
    float dir[3];
    float grot[9];
};

struct MsgArrowShot {
    uint8_t type;
    uint8_t param;
    float startPos[3];
    int16_t angleX, angleY;
    int16_t shapeX, shapeY;
    float speed[3];
    float flyMax;
    float flySpeed;
};

struct MsgJoinSyncHeader {
    uint16_t version;
    uint16_t size;
};

struct MsgWorldDelta {
    char stage[8];
    int8_t saveNo;
    uint8_t region;
    int8_t room;
    uint8_t offset;
    uint8_t size;
    uint8_t set[32];
    uint8_t clr[32];
};

struct MsgEnemyEntry {
    uint32_t key;
    int8_t room;
    int16_t procName;
    float pos[3];

    int16_t angle[3];
    int16_t health;
    uint8_t flags;

    int16_t anmId;
    float anmFrame;
    float anmRate;

    uint8_t anmMode;

    int16_t action;
    int16_t mode;

    int16_t timers[5];
    uint8_t timerCount;

    int8_t extraState;
};

const int8_t kEnemyNoState = INT8_MIN;

const int16_t kEnemyNoAction = INT16_MIN;

struct MsgEnemyGone {
    uint32_t key;
    int8_t room;
};

struct MsgEnemyDamage {
    uint32_t key;
    int8_t room;
    int16_t amount;
};

struct MsgWorldDigest {
    char stage[8];
    int8_t saveNo;
    uint32_t memoryHash;
    uint32_t danHash;
    uint32_t tmpHash;

    int8_t room;
    uint32_t zoneHash;

    uint32_t visitedHash;
    uint32_t lightDropHash;
    uint32_t collectHash;

    uint32_t eventHash;
    uint32_t statusBHash;
};

struct MsgDeathLink {
    char name[kCoopNameMax];
};

struct MsgWorldSyncRequest {
    char stage[8];
    int8_t saveNo;
    int8_t room;
};

struct MsgWorldFull {
    char stage[8];
    int8_t saveNo;
    uint8_t region;
    int8_t room;
    uint8_t size;
    uint8_t data[32];
};

struct MsgEnemyHit {
    uint32_t key;
    int8_t room;
    uint32_t atType;
    uint8_t atp;
    uint8_t spl;
    uint8_t mtrl;
    float from[3];
    float at[3];
};

struct MsgSessionSettings {
    uint16_t flags;
};

const int kGrassCutsPerMsg = 12;

const uint8_t kGrassKindGrass = 0;
const uint8_t kGrassKindFlower = 1;
struct MsgGrassCut {
    int8_t room;
    uint8_t count;
    uint8_t kind;
    uint8_t bits[kGrassCutsPerMsg];
    float pos[kGrassCutsPerMsg][3];
};

struct MsgObjectMove {
    uint32_t key;
    int8_t room;
    float pos[3];
    int16_t angle[3];
};

struct MsgHorse {
    float pos[3];
    int16_t shapeAngleY;
    int16_t shapeAngleX;
    float speedF;
    uint8_t procID;

    uint8_t riding;

    uint16_t anmIdx[2];
    float anmRatio;
    float anmFrame[2];
};

struct MsgActorSpawn {
    uint32_t netId;
    int16_t procName;
    uint32_t param;
    float pos[3];
    int16_t angle[3];
    int8_t room;

    int16_t state;

    uint8_t kind;
};

struct MsgActorState {
    uint32_t netId;
    float pos[3];
    int16_t angle[3];
    uint8_t flags;

    int16_t state;

    uint32_t param;
};

struct MsgActorGone {
    uint32_t netId;
};

struct MsgBossActor {
    uint8_t kind;
    uint8_t index;

    uint8_t inDemo;
    uint8_t fightOver;
    float pos[3];
    int16_t angle[3];
    int16_t shapeAngle[3];
    int16_t health;
    int32_t anmId;

    float anmFrame;
    float anmRate;
    int16_t action;
    int16_t mode;
    int16_t timers[5];
    int16_t extra[17];
    float extraF;
    float targetPos[3];
};

struct MsgBossHit {
    uint8_t kind;
    uint8_t index;
    uint8_t collider;
    int16_t attackerName;
    uint32_t atType;
    uint8_t atp;
    uint8_t spl;
    uint8_t mtrl;
    uint8_t cutJump;

    uint8_t cutCount;

    uint8_t fastCut;
};

struct MsgBossStem {
    uint8_t index;
    float pos[18][3];
    int16_t angle[18][3];
};

struct MsgBossReady {
    int8_t room;
    uint8_t inRoom;
    uint8_t goAnyway;
    char stage[8];
};

struct MsgEnemyTarget {
    uint32_t key;
    int8_t room;
    uint8_t targetPlayer;
};

struct MsgRoomClaim {
    char stage[8];
    int8_t saveNo;
    int8_t room;
};

struct MsgRoomOwner {
    char stage[8];
    int8_t saveNo;
    int8_t room;
    uint8_t owner;
};

struct MsgPvpState {
    uint8_t enabled;
    uint16_t damagePercent;
};

struct MsgPvpHit {
    uint32_t atType;
    uint8_t atp;
    uint8_t spl;
    uint8_t mtrl;
    uint8_t source;
    float from[3];
};

struct MsgColorEntry {
    uint8_t set;
    uint8_t r, g, b;
};
#pragma pack(pop)

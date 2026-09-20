#pragma once

#include <cstddef>
#include <cstdint>

struct EnemyActLayoutExt {
    int16_t procName;
    uint16_t actionOffset;
    uint16_t modeOffset;
    uint8_t actionSize;
    uint8_t modeSize;
};

struct EnemyAnmLayoutExt {
    int16_t procName;
    uint16_t morfOffset;
    bool morfIsMca;
    uint16_t arcOffset;
    const char* arc;
};

struct EnemyTmrLayoutExt {
    int16_t procName;
    uint16_t offset;
    uint8_t count;
};

const uint16_t kEnemyNoModeOffset = 0xFFFF;

const EnemyActLayoutExt* coop_enemy_act_wb(int& count);
const EnemyTmrLayoutExt* coop_enemy_tmr_wb(int& count);
const EnemyActLayoutExt* coop_enemy_act_yk(int& count);
const EnemyActLayoutExt* coop_enemy_act_po(int& count);
const EnemyAnmLayoutExt* coop_enemy_anm_wb(int& count);
const EnemyAnmLayoutExt* coop_enemy_anm_yk(int& count);
const EnemyAnmLayoutExt* coop_enemy_anm_po(int& count);
const EnemyTmrLayoutExt* coop_enemy_tmr_yk(int& count);

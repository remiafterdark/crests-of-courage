

#include "enemy_layout.hpp"

#include "m_Do/m_Do_ext.h"
#include "d/d_particle.h"
#include "d/d_kankyo.h"
#include "d/d_com_inf_game.h"

#include "d/actor/d_a_e_wb.h"

namespace {

const EnemyActLayoutExt kAct[] = {
    { (int16_t)0x00EF, (uint16_t)offsetof(e_wb_class, action),
      (uint16_t)offsetof(e_wb_class, ride_mode),
      (uint8_t)sizeof(((e_wb_class*)nullptr)->action),
      (uint8_t)sizeof(((e_wb_class*)nullptr)->ride_mode) },
};

const EnemyTmrLayoutExt kTmr[] = {
    { (int16_t)0x00EF, (uint16_t)offsetof(e_wb_class, timer),
      (uint8_t)(sizeof(((e_wb_class*)nullptr)->timer) /
                sizeof(((e_wb_class*)nullptr)->timer[0])) },
};

}

const EnemyActLayoutExt* coop_enemy_act_wb(int& count) {
    count = (int)(sizeof(kAct) / sizeof(kAct[0]));
    return kAct;
}

const EnemyTmrLayoutExt* coop_enemy_tmr_wb(int& count) {
    count = (int)(sizeof(kTmr) / sizeof(kTmr[0]));
    return kTmr;
}

namespace {
const EnemyAnmLayoutExt kAnm[] = {
    { (int16_t)0x00EF, (uint16_t)offsetof(e_wb_class, anm_p), false, (uint16_t)(uint16_t)offsetof(e_wb_class, resName), nullptr },
};
}

const EnemyAnmLayoutExt* coop_enemy_anm_wb(int& count) {
    count = (int)(sizeof(kAnm) / sizeof(kAnm[0]));
    return kAnm;
}

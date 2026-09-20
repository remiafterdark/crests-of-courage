

#include "enemy_layout.hpp"

#include "m_Do/m_Do_ext.h"
#include "d/d_particle.h"
#include "d/d_kankyo.h"
#include "d/d_com_inf_game.h"

#include "d/actor/d_a_e_yk.h"

namespace {

const EnemyActLayoutExt kAct[] = {
    { (int16_t)0x01F9, (uint16_t)offsetof(e_yk_class, mAction),
      (uint16_t)offsetof(e_yk_class, mActionPhase),
      (uint8_t)sizeof(((e_yk_class*)nullptr)->mAction),
      (uint8_t)sizeof(((e_yk_class*)nullptr)->mActionPhase) },
};

const EnemyTmrLayoutExt kTmr[] = {
    { (int16_t)0x01F9, (uint16_t)offsetof(e_yk_class, mActionTimers),
      (uint8_t)(sizeof(((e_yk_class*)nullptr)->mActionTimers) /
                sizeof(((e_yk_class*)nullptr)->mActionTimers[0])) },
};

}

const EnemyActLayoutExt* coop_enemy_act_yk(int& count) {
    count = (int)(sizeof(kAct) / sizeof(kAct[0]));
    return kAct;
}

const EnemyTmrLayoutExt* coop_enemy_tmr_yk(int& count) {
    count = (int)(sizeof(kTmr) / sizeof(kTmr[0]));
    return kTmr;
}

namespace {
const EnemyAnmLayoutExt kAnm[] = {
    { (int16_t)0x01F9, (uint16_t)offsetof(e_yk_class, mpMorfSO), false, (uint16_t)0xFFFF, "E_YK" },
};
}

const EnemyAnmLayoutExt* coop_enemy_anm_yk(int& count) {
    count = (int)(sizeof(kAnm) / sizeof(kAnm[0]));
    return kAnm;
}

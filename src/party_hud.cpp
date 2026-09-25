

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"

#include "JSystem/J2DGraph/J2DGrafContext.h"
#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/J2DGraph/J2DPane.h"
#include "JSystem/J2DGraph/J2DScreen.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/TColor.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "d/d_drawlist.h"
#include "d/d_meter2.h"
#include "d/d_meter2_draw.h"
#include "d/d_meter2_info.h"
#include "d/d_pane_class.h"
#include "m_Do/m_Do_ext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

DEFINE_HOOK_SYMBOL("dMeter2_c::_create", int(dMeter2_c*), SquadMeterCreateHook);
DEFINE_HOOK_SYMBOL("dMeter2_c::_delete", int(dMeter2_c*), SquadMeterDeleteHook);

namespace {

dMeter2_c* s_liveMeter = nullptr;
bool s_meterHooked = false;

void on_meter_create_post(ModContext*, void* args, void*, void*) {
    s_liveMeter = mods::arg<dMeter2_c*>(args, 0);
}

HookAction on_meter_delete_pre(ModContext*, void* args, void*, void*) {
    if (mods::arg<dMeter2_c*>(args, 0) == s_liveMeter) s_liveMeter = nullptr;
    return HOOK_CONTINUE;
}

dMeter2_c* live_meter() {
    dMeter2_c* meter = dMeter2Info_getMeterClass();
    if (!s_meterHooked) return meter;
    return meter == s_liveMeter ? meter : nullptr;
}

const int kHeartSlots = 20;
const int kHeartsPerRow = 10;

const f32 kDefaultScale = 0.55f;

ConfigVarHandle s_enableVar = 0;
ConfigVarHandle s_scaleVar = 0;
ConfigVarHandle s_offsetYVar = 0;
ConfigVarHandle s_offsetXVar = 0;
ConfigVarHandle s_sideVar = 0;
ConfigVarHandle s_hurtOnlyVar = 0;
ConfigVarHandle s_hurtSecondsVar = 0;
ConfigVarHandle s_hurtFadeVar = 0;
ConfigVarHandle s_worldSizeVar = 0;

struct SquadScreen {
    J2DScreen* screen = nullptr;
    bool failed = false;
    J2DPane* heartN = nullptr;
    CPaneMgr* heartMgr = nullptr;
    J2DPane* parts[kHeartSlots] = {};
    J2DPane* fullS[kHeartSlots] = {};
    J2DPane* full[kHeartSlots] = {};
    J2DPane* mark[kHeartSlots] = {};
    J2DPane* bigHeart = nullptr;
    J2DPane* bigQuarter[4] = {};
    J2DPane* row[2] = {};
};
SquadScreen s_ours;

const u64 kPartTag[kHeartSlots] = {
    MULTI_CHAR('hpb_00'), MULTI_CHAR('hpb_01'), MULTI_CHAR('hpb_02'), MULTI_CHAR('hpb_03'),
    MULTI_CHAR('hpb_04'), MULTI_CHAR('hpb_05'), MULTI_CHAR('hpb_06'), MULTI_CHAR('hpb_07'),
    MULTI_CHAR('hpb_08'), MULTI_CHAR('hpb_09'), MULTI_CHAR('hpb_10'), MULTI_CHAR('hpb_11'),
    MULTI_CHAR('hpb_12'), MULTI_CHAR('hpb_13'), MULTI_CHAR('hpb_14'), MULTI_CHAR('hpb_15'),
    MULTI_CHAR('hpb_16'), MULTI_CHAR('hpb_17'), MULTI_CHAR('hpb_18'), MULTI_CHAR('hpb_19'),
};
const u64 kMarkTag[kHeartSlots] = {
    MULTI_CHAR('heartn00'), MULTI_CHAR('heartn01'), MULTI_CHAR('heartn02'), MULTI_CHAR('heartn03'),
    MULTI_CHAR('heartn04'), MULTI_CHAR('heartn05'), MULTI_CHAR('heartn06'), MULTI_CHAR('heartn07'),
    MULTI_CHAR('heartn08'), MULTI_CHAR('heartn09'), MULTI_CHAR('heartn10'), MULTI_CHAR('heartn11'),
    MULTI_CHAR('heartn12'), MULTI_CHAR('heartn13'), MULTI_CHAR('heartn14'), MULTI_CHAR('heartn15'),
    MULTI_CHAR('heartn16'), MULTI_CHAR('heartn17'), MULTI_CHAR('heartn18'), MULTI_CHAR('heartn19'),
};
const u64 kFullSTag[kHeartSlots] = {
    MULTI_CHAR('hear_00s'), MULTI_CHAR('hear_01s'), MULTI_CHAR('hear_02s'), MULTI_CHAR('hear_03s'),
    MULTI_CHAR('hear_04s'), MULTI_CHAR('hear_05s'), MULTI_CHAR('hear_06s'), MULTI_CHAR('hear_07s'),
    MULTI_CHAR('hear_08s'), MULTI_CHAR('hear_09s'), MULTI_CHAR('hear_10s'), MULTI_CHAR('hear_11s'),
    MULTI_CHAR('hear_12s'), MULTI_CHAR('hear_13s'), MULTI_CHAR('hear_14s'), MULTI_CHAR('hear_15s'),
    MULTI_CHAR('hear_16s'), MULTI_CHAR('hear_17s'), MULTI_CHAR('hear_18s'), MULTI_CHAR('hear_19s'),
};
const u64 kFullTag[kHeartSlots] = {
    MULTI_CHAR('hear_00'), MULTI_CHAR('hear_01'), MULTI_CHAR('hear_02'), MULTI_CHAR('hear_03'),
    MULTI_CHAR('hear_04'), MULTI_CHAR('hear_05'), MULTI_CHAR('hear_06'), MULTI_CHAR('hear_07'),
    MULTI_CHAR('hear_08'), MULTI_CHAR('hear_09'), MULTI_CHAR('hear_10'), MULTI_CHAR('hear_11'),
    MULTI_CHAR('hear_12'), MULTI_CHAR('hear_13'), MULTI_CHAR('hear_14'), MULTI_CHAR('hear_15'),
    MULTI_CHAR('hear_16'), MULTI_CHAR('hear_17'), MULTI_CHAR('hear_18'), MULTI_CHAR('hear_19'),
};
const u64 kQuarterTag[4] = {MULTI_CHAR('bigh_00'), MULTI_CHAR('bigh_01'), MULTI_CHAR('bigh_02'),
    MULTI_CHAR('bigh_03')};
const u64 kRowTag[2] = {MULTI_CHAR('heart_ln'), MULTI_CHAR('heart_un')};
const u64 kHeartGroupTag = MULTI_CHAR('heart_n');
const u64 kBigHeartTag = MULTI_CHAR('bigh_n');

bool is_ancestor_of(J2DPane* maybe, J2DPane* of) {
    for (J2DPane* p = of->getParentPane(); p != nullptr; p = p->getParentPane()) {
        if (p == maybe) return true;
    }
    return false;
}

void isolate(J2DPane* pane, J2DPane* keep) {
    for (J2DPane* c = pane->getFirstChildPane(); c != nullptr; c = c->getNextChildPane()) {
        if (c == keep) {
            c->show();
        } else if (is_ancestor_of(c, keep)) {
            c->show();
            isolate(c, keep);
        } else {
            c->hide();
        }
    }
}

bool build_screen() {
    if (s_ours.screen != nullptr) return true;
    if (s_ours.failed) return false;
    JKRArchive* arc = dComIfGp_getMain2DArchive();
    if (arc == nullptr) return false;

    JKRHeap* old = mDoExt_setCurrentHeap(mDoExt_getZeldaHeap());
    J2DScreen* screen = JKR_NEW J2DScreen();
    const bool loaded = screen != nullptr &&
                        screen->setPriority("zelda_game_image.blo", 0x20000, arc);
    J2DPane* group = loaded ? screen->search(kHeartGroupTag) : nullptr;
    CPaneMgr* mgr = group != nullptr ? JKR_NEW CPaneMgr(screen, kHeartGroupTag, 2, nullptr) : nullptr;
    mDoExt_setCurrentHeap(old);

    if (group == nullptr || mgr == nullptr) {
        s_ours.failed = true;
        coop_log::warn("coop_mod: [SQUAD] could not build the heart layout - squad health is off");
        return false;
    }

    s_ours.screen = screen;
    s_ours.heartN = group;
    s_ours.heartMgr = mgr;
    for (int i = 0; i < kHeartSlots; ++i) {
        s_ours.parts[i] = screen->search(kPartTag[i]);
        s_ours.mark[i] = screen->search(kMarkTag[i]);
        s_ours.fullS[i] = screen->search(kFullSTag[i]);
        s_ours.full[i] = screen->search(kFullTag[i]);
        if (s_ours.parts[i] == nullptr || s_ours.fullS[i] == nullptr || s_ours.full[i] == nullptr) {
            s_ours.failed = true;
        }
    }
    s_ours.bigHeart = screen->search(kBigHeartTag);
    for (int q = 0; q < 4; ++q) {
        s_ours.bigQuarter[q] = screen->search(kQuarterTag[q]);
        if (s_ours.bigQuarter[q] == nullptr) s_ours.failed = true;
    }
    for (int r = 0; r < 2; ++r) {
        s_ours.row[r] = screen->search(kRowTag[r]);
        if (s_ours.row[r] == nullptr) s_ours.failed = true;
    }
    if (s_ours.bigHeart == nullptr) s_ours.failed = true;
    if (s_ours.failed) {
        coop_log::warn("coop_mod: [SQUAD] the heart layout is missing panes - squad health is off");
        return false;
    }

    isolate(screen, group);
    coop_log::info("coop_mod: [SQUAD] heart layout ready");
    return true;
}

void set_hearts(u16 maxLife, u16 life) {
    const int maxHearts = maxLife / 5;
    int filled = life / 4;
    int quarters = life % 4;
    if (life == maxHearts * 4) quarters = 0;
    if (quarters == 0) --filled;

    s_ours.bigHeart->hide();

    for (int i = 0; i < kHeartSlots; ++i) {
        if (i >= maxHearts) {
            s_ours.parts[i]->hide();
            s_ours.fullS[i]->hide();
            s_ours.full[i]->hide();
            continue;
        }
        s_ours.parts[i]->show();
        if (i < filled) {
            s_ours.fullS[i]->show();
            s_ours.full[i]->show();
        } else {
            s_ours.fullS[i]->hide();
            s_ours.full[i]->hide();
        }
        if (i == filled && life != 0 && quarters == 0) {

            s_ours.fullS[i]->show();
            s_ours.full[i]->show();
        }
        if (i == filled && life != 0 && quarters != 0) {
            s_ours.bigHeart->show();

            for (int q = 0; q < 4; ++q) {
                if (q == quarters) s_ours.bigQuarter[q]->show();
                else s_ours.bigQuarter[q]->hide();
            }
            J2DPane* rowPane = s_ours.row[i / kHeartsPerRow];
            s_ours.bigHeart->translate(
                s_ours.parts[i]->getTranslateX() +
                    (rowPane->getTranslateX() - s_ours.row[0]->getTranslateX()),
                rowPane->getTranslateY() + s_ours.parts[i]->getTranslateY());
        }
    }
}

void copy_heart_scales(dMeter2Draw_c* real) {
    for (int i = 0; i < kHeartSlots; ++i) {
        if (s_ours.mark[i] == nullptr || real->mpHeartMark[i] == nullptr) continue;
        J2DPane* src = real->mpHeartMark[i]->getPanePtr();
        if (src != nullptr) s_ours.mark[i]->scale(src->getScaleX(), src->getScaleY());
    }

    if (real->mpHeartMark[0] != nullptr && real->mpHeartMark[0]->getPanePtr() != nullptr) {
        J2DPane* src = real->mpHeartMark[0]->getPanePtr();
        s_ours.bigHeart->scale(src->getScaleX(), src->getScaleY());
    }
}

void copy_ancestors(J2DPane* realGroup) {
    J2DPane* mine = s_ours.heartN->getParentPane();
    J2DPane* theirs = realGroup->getParentPane();
    while (mine != nullptr && theirs != nullptr) {
        mine->translate(theirs->getTranslateX(), theirs->getTranslateY());
        mine->scale(theirs->getScaleX(), theirs->getScaleY());
        mine = mine->getParentPane();
        theirs = theirs->getParentPane();
    }
}

f32 pane_top(J2DPane* pane) {
    f32 v = pane->getGlbVtx(0).y;
    for (u8 i = 1; i < 4; ++i) {
        const f32 y = pane->getGlbVtx(i).y;
        if (y < v) v = y;
    }
    return v;
}

f32 pane_bottom(J2DPane* pane) {
    f32 v = pane->getGlbVtx(0).y;
    for (u8 i = 1; i < 4; ++i) {
        const f32 y = pane->getGlbVtx(i).y;
        if (y > v) v = y;
    }
    return v;
}

f32 pane_left(J2DPane* pane) {
    f32 v = pane->getGlbVtx(0).x;
    for (u8 i = 1; i < 4; ++i) {
        const f32 x = pane->getGlbVtx(i).x;
        if (x < v) v = x;
    }
    return v;
}

f32 pane_right(J2DPane* pane) {
    f32 v = pane->getGlbVtx(0).x;
    for (u8 i = 1; i < 4; ++i) {
        const f32 x = pane->getGlbVtx(i).x;
        if (x > v) v = x;
    }
    return v;
}

struct ParentSpace {
    f32 sx = 0.0f, sy = 0.0f, tx = 0.0f, ty = 0.0f;
    bool valid() const { return sx > 1e-4f || sx < -1e-4f; }
    f32 x(f32 glb) const { return (glb - tx) / sx; }
    f32 y(f32 glb) const { return (glb - ty) / sy; }
};

ParentSpace parent_space_of(J2DPane* pane) {
    ParentSpace ps;
    J2DPane* parent = pane->getParentPane();
    if (parent == nullptr) {
        ps.sx = ps.sy = 1.0f;
        return ps;
    }
    MtxP m = parent->getGlbMtx();
    ps.sx = m[0][0];
    ps.sy = m[1][1];
    ps.tx = m[0][3];
    ps.ty = m[1][3];
    if (!(ps.sy > 1e-4f || ps.sy < -1e-4f)) ps.sx = 0.0f;
    return ps;
}

bool s_haveAnchor = false;
f32 s_anchorDx = 0.0f;
f32 s_anchorDy = 0.0f;
f32 s_anchorScale = -1.0f;

struct AnchorLine {
    int have = 0;
    f32 s[2] = {};
    f32 dx[2] = {};
    f32 dy[2] = {};
    f32 parentSy = 0.0f;
};
AnchorLine s_line;

void note_anchor_line(f32 effScale, f32 dx, f32 dy, f32 parentSy) {
    if (s_line.parentSy != parentSy) s_line = AnchorLine{};
    s_line.parentSy = parentSy;
    for (int i = 0; i < s_line.have; ++i) {
        if (std::fabs(s_line.s[i] - effScale) < 0.02f) {
            s_line.dx[i] = dx;
            s_line.dy[i] = dy;
            return;
        }
    }
    const int slot = s_line.have < 2 ? s_line.have++ : 1;
    s_line.s[slot] = effScale;
    s_line.dx[slot] = dx;
    s_line.dy[slot] = dy;
}

bool anchor_on_line(f32 effScale, f32 parentSy, f32& dx, f32& dy) {
    if (s_line.have < 2 || s_line.parentSy != parentSy) return false;
    const f32 span = s_line.s[1] - s_line.s[0];
    if (std::fabs(span) < 0.02f) return false;
    const f32 t = (effScale - s_line.s[0]) / span;
    dx = s_line.dx[0] + (s_line.dx[1] - s_line.dx[0]) * t;
    dy = s_line.dy[0] + (s_line.dy[1] - s_line.dy[0]) * t;
    return true;
}

void measure_anchor(const ParentSpace& ps, f32 usedTx, f32 usedTy, f32 scale) {
    J2DPane* first = s_ours.parts[0];
    s_anchorDx = ps.x(pane_left(first)) - usedTx;
    s_anchorDy = ps.y(pane_top(first)) - usedTy;
    s_anchorScale = scale;
    s_haveAnchor = true;
    note_anchor_line(s_ours.heartN->getScaleY(), s_anchorDx, s_anchorDy, ps.sy);
}

const f32 kSquadFloorY = 222.0f;

const int kMaxFullEntries = 3;

const int kMaxTextRows = 6;
const f32 kFitSteps[] = {1.0f, 0.85f, 0.7f, 0.6f, 0.5f};
const int kFitStepCount = sizeof(kFitSteps) / sizeof(kFitSteps[0]);
int s_fitStep = 0;

f32 s_memberH[kCoopMaxPlayers + 4] = {};
f32 s_memberHScale = 0.0f;

struct SquadMember {
    uint8_t id;
    int rank;
    f32 dist;
    std::string name;
    u16 life;
    u16 maxLife;
    bool low;
    bool paused;
};

void draw_name(const std::string& name, f32 x, f32 y, f32 cell, u8 alpha, bool low, bool paused,
    bool alignRight = false) {
    JUTFont* font = mDoExt_getMesgFont();
    if (font == nullptr || name.empty()) return;
    font->setGX();
    std::string text = name;
    if (paused) text += "  (menu)";
    if (alignRight) x -= coop_text_width(font, text.c_str(), cell);
    const f32 shadow = cell * 0.08f;
    font->setCharColor(JUtility::TColor(0, 0, 0, static_cast<u8>(alpha * 0.7f)));
    font->drawString_scale(x + shadow, y + shadow, cell, cell, text.c_str(), true);

    font->setCharColor(low ? JUtility::TColor(255, 90, 80, alpha)
                           : JUtility::TColor(255, 255, 255, alpha));
    font->drawString_scale(x, y, cell, cell, text.c_str(), true);
}

const f32 kWorldHeartScale = 0.55f;

const int kHurtShowTicks = 150;
const int kHurtFadeTicks = 45;
const int kTicksPerSecond = 30;

int hurt_show_ticks() {
    const int seconds = static_cast<int>(cfg_int(s_hurtSecondsVar, 5));
    return (seconds > 0 ? seconds : 5) * kTicksPerSecond;
}

int hurt_fade_ticks() {
    const int tenths = static_cast<int>(cfg_int(s_hurtFadeVar, 15));
    return tenths * kTicksPerSecond / 10;
}
struct HurtWatch {
    uint16_t life = 0;
    bool known = false;
    int show = 0;
};
HurtWatch s_hurt[kCoopMaxPlayers];

f32 hurt_alpha(uint8_t id, const CoopPeer& peer) {
    if (id >= kCoopMaxPlayers) return 1.0f;
    HurtWatch& w = s_hurt[id];
    if (!peer.present || !peer.lifeKnown) {
        w.known = false;
        w.show = 0;
        return 0.0f;
    }
    if (w.known && peer.life < w.life) w.show = hurt_show_ticks();
    w.life = peer.life;
    w.known = true;
    if (w.show <= 0) return 0.0f;
    --w.show;
    const int fade = hurt_fade_ticks();
    if (fade <= 0 || w.show >= fade) return 1.0f;
    return static_cast<f32>(w.show) / static_cast<f32>(fade);
}

const f32 kWorldFadeStart = 3000.0f;
const f32 kWorldFadeEnd = 4500.0f;

void draw_world_hearts(J2DPane* realGroup, dMeter2Draw_c* real, J2DGrafContext* graf,
    f32 alphaRate) {
    if (!puppet_hook_health_enabled()) return;
    const bool hurtOnly = cfg_bool(s_hurtOnlyVar, true);
    J2DOrthoGraph* og = static_cast<J2DOrthoGraph*>(graf);
    const auto* o = og->getOrtho();
    const f32 realSx = realGroup->getScaleX();
    const f32 realSy = realGroup->getScaleY();
    J2DPane* r0 = real->mpLifeParts[0] != nullptr ? real->mpLifeParts[0]->getPanePtr() : nullptr;
    J2DPane* r1 = real->mpLifeParts[1] != nullptr ? real->mpLifeParts[1]->getPanePtr() : nullptr;
    if (r0 == nullptr || r1 == nullptr || !(std::fabs(realSy) > 1e-4f)) return;

    const f32 realStep = pane_left(r1) - pane_left(r0);
    if (!(realStep > 0.1f)) return;

    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || !coop_net_player_present(id)) continue;
        const CoopPeer& p = features_peer_of(id);
        if (!p.present || !p.lifeKnown || p.maxLife < 5) continue;
        f32 u = 0.0f, v = 0.0f, cell = 0.0f, camDist = 0.0f;
        if (!puppet_hook_health_anchor(id, &u, &v, &cell, &camDist)) continue;
        f32 fade = 1.0f;
        if (camDist > kWorldFadeStart) {
            fade = 1.0f - (camDist - kWorldFadeStart) / (kWorldFadeEnd - kWorldFadeStart);
        }
        if (hurtOnly) fade *= hurt_alpha(id, p);
        if (fade <= 0.02f) continue;

        const f32 pick = static_cast<f32>(cfg_int(s_worldSizeVar, 55)) / 100.0f;
        const f32 k = (kWorldHeartScale / 0.55f) * pick * (cell / 18.0f);
        const f32 eff = realSy * k;
        s_ours.heartN->scale(realSx * k, eff);
        s_ours.heartMgr->setAlphaRate(alphaRate * fade);
        set_hearts(p.maxLife, p.life);

        ParentSpace ps = parent_space_of(s_ours.heartN);
        f32 dx = 0.0f, dy = 0.0f;
        if (!ps.valid() || !anchor_on_line(eff, ps.sy, dx, dy)) continue;

        int hearts = p.maxLife / 5;
        if (hearts > kHeartsPerRow) hearts = kHeartsPerRow;
        const f32 rowWidth = static_cast<f32>(hearts) * realStep * (k);
        const f32 gx = o->i.x + u * (o->f.x - o->i.x);
        const f32 gy = o->i.y + v * (o->f.y - o->i.y);
        const f32 tx = ps.x(gx - rowWidth * 0.5f) - dx;
        const f32 ty = ps.y(gy + realStep * k * 0.7f) - dy;
        s_ours.heartN->translate(tx, ty);
        s_ours.screen->draw(0.0f, 0.0f, graf);

        ps = parent_space_of(s_ours.heartN);
        if (ps.valid()) {
            note_anchor_line(eff, ps.x(pane_left(s_ours.parts[0])) - tx,
                ps.y(pane_top(s_ours.parts[0])) - ty, ps.sy);
        }
        graf->setPort();
        graf->setup2D();
    }
    s_ours.heartMgr->setAlphaRate(alphaRate);
}

void draw_squad() {
    const bool listOn = cfg_bool(s_enableVar, false);
    const bool worldOn = puppet_hook_health_enabled();
    if (!coop_net_connected() || (!listOn && !worldOn)) return;
    dMeter2_c* meter = live_meter();
    if (meter == nullptr) return;
    dMeter2Draw_c* real = meter->getMeterDrawPtr();
    if (real == nullptr || real->mpLifeParent == nullptr || real->mpScreen == nullptr) return;
    J2DPane* realGroup = real->mpLifeParent->getPanePtr();
    if (realGroup == nullptr) return;

    const f32 alphaRate = real->mpLifeParent->getAlphaRate();
    if (alphaRate <= 0.01f) return;

    SquadMember members[kCoopMaxPlayers + 4];
    int count = 0;
    daAlink_c* me = daAlink_getAlinkActorClass();
    const char* myStage = dComIfGp_getStartStageName();
    const int myRoom = me != nullptr ? fopAcM_GetRoomNo(me) : -1;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || !coop_net_player_present(id)) continue;
        const CoopPeer& p = features_peer_of(id);
        if (!p.present || !p.lifeKnown) continue;
        SquadMember& m = members[count++];
        m.id = id;
        m.name = p.name;
        m.life = p.life;
        m.maxLife = p.maxLife;
        m.low = p.maxLife >= 5 && p.life <= p.maxLife / 5;
        m.paused = coop_player_paused(id);
        const bool sameStage = myStage != nullptr && std::strncmp(myStage, p.stage, 8) == 0;
        m.rank = !sameStage ? 2 : (p.curRoom == myRoom ? 0 : 1);
        m.dist = 1.0e9f;
        f32 px = 0.0f, py = 0.0f, pz = 0.0f, sx = 0.0f, sz = 0.0f;
        short ang = 0;
        if (me != nullptr && sameStage && puppet_hook_get_pose_of(id, &px, &py, &pz, &ang, &sx, &sz)) {
            const f32 dx = px - me->current.pos.x;
            const f32 dy = py - me->current.pos.y;
            const f32 dz = pz - me->current.pos.z;
            m.dist = dx * dx + dy * dy + dz * dz;
        }
    }
    {
        static uint8_t s_order[kCoopMaxPlayers] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        static int s_orderAge = 0;

        if (++s_orderAge >= 120) {
            s_orderAge = 0;
            std::stable_sort(members, members + count, [](const SquadMember& a, const SquadMember& b) {
                if (a.rank != b.rank) return a.rank < b.rank;
                return a.dist < b.dist;
            });
            for (int i = 0; i < kCoopMaxPlayers; ++i) s_order[i] = i < count ? members[i].id : 0xFF;
        } else {
            std::stable_sort(members, members + count, [](const SquadMember& a, const SquadMember& b) {
                const auto pos = [](uint8_t id) {
                    for (int i = 0; i < kCoopMaxPlayers; ++i) {
                        if (s_order[i] == id) return i;
                    }
                    return kCoopMaxPlayers;
                };
                return pos(a.id) < pos(b.id);
            });
        }
    }

    {
        static bool s_hiddenForCrowd = false;
        const bool crowd = listOn && count > kMaxFullEntries;
        if (!coop_net_connected()) s_hiddenForCrowd = false;
        if (crowd != s_hiddenForCrowd) {
            s_hiddenForCrowd = crowd;
            if (crowd) {
                coop_toast("Party HUD hidden", "It shows up to 3 other players. It comes back when "
                                               "there are 3 or fewer.");
            } else if (coop_net_connected()) {
                coop_toast("Party HUD", "Back on. 3 or fewer other players.");
            }
        }
        if (crowd) count = 0;
    }
    if (!listOn) count = 0;
    if (count == 0 && !worldOn) return;
    if (!build_screen()) return;

    J2DGrafContext* graf = dComIfGp_getCurrentGrafPort();
    if (graf == nullptr) return;

    graf->setPort();
    graf->setup2D();
    copy_ancestors(realGroup);
    copy_heart_scales(real);

    f32 scalePct = static_cast<f32>(cfg_int(s_scaleVar, static_cast<int64_t>(kDefaultScale * 100)));
    if (scalePct < 20.0f) scalePct = 20.0f;
    if (scalePct > 100.0f) scalePct = 100.0f;
    const f32 realSx = realGroup->getScaleX();
    const f32 realSy = realGroup->getScaleY();

    int myHearts = dComIfGs_getMaxLife() / 5;
    if (myHearts < 1) myHearts = 1;
    if (myHearts > kHeartSlots) myHearts = kHeartSlots;
    J2DPane* realFirst = real->mpLifeParts[0] != nullptr ? real->mpLifeParts[0]->getPanePtr() : nullptr;
    J2DPane* realLast =
        real->mpLifeParts[myHearts - 1] != nullptr ? real->mpLifeParts[myHearts - 1]->getPanePtr() : nullptr;
    if (realFirst == nullptr || realLast == nullptr) return;

    const f32 slotH = pane_bottom(realFirst) - pane_top(realFirst);
    if (!(slotH > 0.5f)) return;

    {
        ParentSpace probePs = parent_space_of(s_ours.heartN);
        const bool needProbe = !probePs.valid() || s_line.have < 2 ||
                               s_line.parentSy != probePs.sy;
        if (worldOn && needProbe) {
            const f32 tx = realGroup->getTranslateX();
            const f32 ty = realGroup->getTranslateY();
            s_ours.heartMgr->setAlphaRate(0.0f);
            set_hearts(5, 0);
            for (const f32 probe : {0.3f, 0.7f}) {
                s_ours.heartN->scale(realGroup->getScaleX() * probe, realGroup->getScaleY() * probe);
                s_ours.heartN->translate(tx, ty);
                s_ours.screen->draw(0.0f, 0.0f, graf);
                probePs = parent_space_of(s_ours.heartN);
                if (probePs.valid()) {
                    note_anchor_line(realGroup->getScaleY() * probe,
                        probePs.x(pane_left(s_ours.parts[0])) - tx,
                        probePs.y(pane_top(s_ours.parts[0])) - ty, probePs.sy);
                }
            }
            graf->setPort();
            graf->setup2D();
            s_ours.heartMgr->setAlphaRate(alphaRate);
        }
    }
    if (worldOn) draw_world_hearts(realGroup, real, graf, alphaRate);
    if (count == 0) return;
    const f32 left = pane_left(realFirst);

    const bool onRight = cfg_int(s_sideVar, 0) == 1;
    const f32 inset = static_cast<f32>(cfg_int(s_offsetXVar, 0));
    f32 rightEdge = 0.0f;
    f32 realPitch = 0.0f;
    const f32 realSlotW = pane_right(realFirst) - pane_left(realFirst);
    if (onRight) {
        const auto* ortho = static_cast<J2DOrthoGraph*>(graf)->getOrtho();
        rightEdge = ortho->f.x - (left - ortho->i.x) - inset;
        J2DPane* realSecond =
            real->mpLifeParts[1] != nullptr ? real->mpLifeParts[1]->getPanePtr() : nullptr;
        if (realSecond != nullptr) realPitch = pane_left(realSecond) - left;
        if (!(realPitch > 0.0f)) realPitch = realSlotW;
    }
    const f32 rowLeft = left + inset;
    f32 bottom = pane_bottom(realFirst);
    if (pane_bottom(realLast) > bottom) bottom = pane_bottom(realLast);

    const bool gaugeUp =
        real->getMeterGaugeAlphaRate(1) > 0.01f || real->getMeterGaugeAlphaRate(2) > 0.01f;
    if (gaugeUp && real->mpMagicParent != nullptr && real->mpMagicParent->getPanePtr() != nullptr) {
        const f32 gaugeBottom = pane_bottom(real->mpMagicParent->getPanePtr());

        if (gaugeBottom > bottom && gaugeBottom < bottom + slotH * 4.0f) bottom = gaugeBottom;
    }

    const f32 baseK = scalePct / 100.0f;

    const f32 room = kSquadFloorY - bottom;
    if (s_memberHScale > 0.0f && room > 0.0f) {
        f32 measured = 0.0f;
        for (int n = 0; n < count; ++n) measured += s_memberH[n];
        int best = kFitStepCount - 1;
        for (int i = 0; i < kFitStepCount; ++i) {
            const f32 need = measured * (baseK * kFitSteps[i]) / s_memberHScale;
            const f32 allowed = i < s_fitStep ? room * 0.92f : room;
            if (need <= allowed) {
                best = i;
                break;
            }
        }
        s_fitStep = best;
    }
    const f32 k = baseK * kFitSteps[s_fitStep];
    const f32 ourSlotH = slotH * k;

    const f32 gap = ourSlotH * 0.25f;
    const f32 nameCell = ourSlotH * 0.8f;

    s_ours.heartMgr->setAlphaRate(alphaRate);
    const f32 scale = k;
    s_ours.heartN->scale(realSx * k, realSy * k);

    ParentSpace ps = parent_space_of(s_ours.heartN);
    if (!ps.valid() || !s_haveAnchor || s_anchorScale != scale) {
        const f32 tx = realGroup->getTranslateX();
        const f32 ty = realGroup->getTranslateY();
        s_ours.heartN->translate(tx, ty);
        s_ours.heartMgr->setAlphaRate(0.0f);
        set_hearts(5, 0);
        s_ours.screen->draw(0.0f, 0.0f, graf);
        graf->setPort();
        graf->setup2D();
        s_ours.heartMgr->setAlphaRate(alphaRate);
        ps = parent_space_of(s_ours.heartN);
        if (!ps.valid()) return;
        measure_anchor(ps, tx, ty, scale);
    }

    const u8 alpha = static_cast<u8>(255.0f * alphaRate);
    f32 lineTop = bottom + gap + static_cast<f32>(cfg_int(s_offsetYVar, 0));
    bool compact = false;
    int textRows = 0;
    for (int n = 0; n < count; ++n) {
        const SquadMember& m = members[n];
        const f32 memberTop = lineTop;

        const f32 expectH = s_memberHScale > 0.0f ? s_memberH[n] * k / s_memberHScale : 0.0f;
        if (compact || n >= kMaxFullEntries || (n > 0 && lineTop + expectH > kSquadFloorY)) {
            compact = true;
            graf->setPort();
            graf->setup2D();
            char line[64];
            if (textRows >= kMaxTextRows) {

                std::snprintf(line, sizeof(line), "+%d more", count - n);
                lineTop += nameCell * 1.15f;
                draw_name(line, onRight ? rightEdge : rowLeft, lineTop, nameCell, alpha, false,
                    false, onRight);
                graf->setPort();
                graf->setup2D();
                break;
            }
            ++textRows;
            std::snprintf(line, sizeof(line), "%s  %u/%u", m.name.c_str(),
                static_cast<unsigned>((m.life + 3) / 4), static_cast<unsigned>(m.maxLife / 5));
            lineTop += nameCell * 1.15f;
            draw_name(line, onRight ? rightEdge : rowLeft, lineTop, nameCell, alpha, m.low,
                m.paused, onRight);
            graf->setPort();
            graf->setup2D();
            continue;
        }

        const f32 heartsTop = lineTop + nameCell;
        set_hearts(m.maxLife, m.life);
        f32 rowX = rowLeft;
        if (onRight) {
            int perRow = m.maxLife / 5;
            if (perRow < 1) perRow = 1;
            if (perRow > kHeartsPerRow) perRow = kHeartsPerRow;
            rowX = rightEdge - ((perRow - 1) * realPitch + realSlotW) * k;
        }
        const f32 tx = ps.x(rowX) - s_anchorDx;
        const f32 ty = ps.y(heartsTop) - s_anchorDy;
        s_ours.heartN->translate(tx, ty);
        s_ours.screen->draw(0.0f, 0.0f, graf);
        ps = parent_space_of(s_ours.heartN);
        measure_anchor(ps, tx, ty, scale);

        const f32 heartsLeft = pane_left(s_ours.parts[0]);
        const f32 heartsTopDrawn = pane_top(s_ours.parts[0]);
        static int s_diag = 0;
        if (n == 0 && (s_diag++ % 300) == 0) {
            J2DOrthoGraph* og = static_cast<J2DOrthoGraph*>(graf);
            const auto* o = og->getOrtho();
            const auto* b = og->getBounds();
            coop_log::trace("coop_mod: [SQUAD-DIAG] ortho=({:.1f},{:.1f})-({:.1f},{:.1f}) port=({:.1f},{:.1f})-({:.1f},{:.1f}) "
                            "realFirst top={:.1f} left={:.1f} bottom={:.1f} ours top={:.1f} left={:.1f} "
                            "tx={:.1f} ty={:.1f} ps=({:.3f},{:.3f},{:.1f},{:.1f}) nameCell={:.1f} realGroupT=({:.1f},{:.1f}) "
                            "realGroupBounds=({:.1f},{:.1f}) realS={:.3f}",
                o->i.x, o->i.y, o->f.x, o->f.y, b->i.x, b->i.y, b->f.x, b->f.y,
                pane_top(realFirst), pane_left(realFirst), pane_bottom(realFirst), heartsTopDrawn, heartsLeft,
                tx, ty, ps.sx, ps.sy, ps.tx, ps.ty, nameCell, realGroup->getTranslateX(),
                realGroup->getTranslateY(), realGroup->getBounds().i.x, realGroup->getBounds().i.y, realSx);
        }

        graf->setPort();
        graf->setup2D();
        draw_name(m.name, onRight ? rightEdge : heartsLeft, heartsTopDrawn - nameCell * 0.12f,
            nameCell, alpha, m.low, m.paused, onRight);
        graf->setPort();
        graf->setup2D();

        int theirHearts = m.maxLife / 5;
        if (theirHearts < 1) theirHearts = 1;
        if (theirHearts > kHeartSlots) theirHearts = kHeartSlots;
        f32 ourBottom = pane_bottom(s_ours.parts[0]);
        if (pane_bottom(s_ours.parts[theirHearts - 1]) > ourBottom) {
            ourBottom = pane_bottom(s_ours.parts[theirHearts - 1]);
        }
        lineTop = ourBottom + gap;
        s_memberH[n] = lineTop - memberTop;
    }

    if (!compact) s_memberHScale = k;
}

class SquadHudDlst : public dDlst_base_c {
public:
    virtual void draw() { draw_squad(); }
};
SquadHudDlst s_dlst;

}

void squad_hud_register_vars() {
    s_meterHooked = mods::hook::add_post<SquadMeterCreateHook>(on_meter_create_post) == MOD_OK &&
                    mods::hook::add_pre<SquadMeterDeleteHook>(on_meter_delete_pre) == MOD_OK;
    coop_log::info("coop_mod: [HUD] watching the game's HUD come and go: {}",
        s_meterHooked ? "yes" : "NO - trusting its pointer");
    if (svc_config == nullptr) return;
    ConfigVarDesc on = CONFIG_VAR_DESC_INIT;
    on.name = "squad_health";
    on.type = CONFIG_VAR_BOOL;
    on.default_bool = false;
    if (svc_config->register_var(mod_ctx, &on, &s_enableVar) != MOD_OK) s_enableVar = 0;

    ConfigVarDesc hurt = CONFIG_VAR_DESC_INIT;
    hurt.name = "world_hearts_hurt_only";
    hurt.type = CONFIG_VAR_BOOL;
    hurt.default_bool = true;
    if (svc_config->register_var(mod_ctx, &hurt, &s_hurtOnlyVar) != MOD_OK) s_hurtOnlyVar = 0;

    ConfigVarDesc hurtSeconds = CONFIG_VAR_DESC_INIT;
    hurtSeconds.name = "world_hearts_seconds";
    hurtSeconds.type = CONFIG_VAR_INT;
    hurtSeconds.default_int = kHurtShowTicks / kTicksPerSecond;
    if (svc_config->register_var(mod_ctx, &hurtSeconds, &s_hurtSecondsVar) != MOD_OK) {
        s_hurtSecondsVar = 0;
    }

    ConfigVarDesc hurtFade = CONFIG_VAR_DESC_INIT;
    hurtFade.name = "world_hearts_fade_tenths";
    hurtFade.type = CONFIG_VAR_INT;
    hurtFade.default_int = kHurtFadeTicks * 10 / kTicksPerSecond;
    if (svc_config->register_var(mod_ctx, &hurtFade, &s_hurtFadeVar) != MOD_OK) s_hurtFadeVar = 0;

    ConfigVarDesc worldSize = CONFIG_VAR_DESC_INIT;
    worldSize.name = "world_hearts_size";
    worldSize.type = CONFIG_VAR_INT;
    worldSize.default_int = 55;
    if (svc_config->register_var(mod_ctx, &worldSize, &s_worldSizeVar) != MOD_OK) {
        s_worldSizeVar = 0;
    }

    ConfigVarDesc scale = CONFIG_VAR_DESC_INIT;
    scale.name = "squad_health_size";
    scale.type = CONFIG_VAR_INT;
    scale.default_int = static_cast<int64_t>(kDefaultScale * 100);
    if (svc_config->register_var(mod_ctx, &scale, &s_scaleVar) != MOD_OK) s_scaleVar = 0;

    ConfigVarDesc offset = CONFIG_VAR_DESC_INIT;
    offset.name = "squad_health_offset_y";
    offset.type = CONFIG_VAR_INT;
    offset.default_int = 0;
    if (svc_config->register_var(mod_ctx, &offset, &s_offsetYVar) != MOD_OK) s_offsetYVar = 0;

    ConfigVarDesc offsetX = CONFIG_VAR_DESC_INIT;
    offsetX.name = "squad_health_offset_x";
    offsetX.type = CONFIG_VAR_INT;
    offsetX.default_int = 0;
    if (svc_config->register_var(mod_ctx, &offsetX, &s_offsetXVar) != MOD_OK) s_offsetXVar = 0;

    ConfigVarDesc side = CONFIG_VAR_DESC_INIT;
    side.name = "squad_health_side";
    side.type = CONFIG_VAR_INT;
    side.default_int = 0;
    if (svc_config->register_var(mod_ctx, &side, &s_sideVar) != MOD_OK) s_sideVar = 0;
}

ConfigVarHandle squad_hud_offset_x_var() {
    return s_offsetXVar;
}

ConfigVarHandle squad_hud_side_var() {
    return s_sideVar;
}

ConfigVarHandle squad_hud_enabled_var() {
    return s_enableVar;
}

ConfigVarHandle squad_hud_size_var() {
    return s_scaleVar;
}

ConfigVarHandle squad_hud_offset_var() {
    return s_offsetYVar;
}

ConfigVarHandle squad_hud_hurt_seconds_var() {
    return s_hurtSecondsVar;
}

ConfigVarHandle squad_hud_hurt_fade_var() {
    return s_hurtFadeVar;
}

ConfigVarHandle squad_hud_world_size_var() {
    return s_worldSizeVar;
}

ConfigVarHandle squad_hud_hurt_only_var() {
    return s_hurtOnlyVar;
}

void squad_hud_queue() {
    if (!coop_net_connected()) return;
    dDlst_list_c& lists = g_dComIfG_gameInfo.drawlist;
    for (dDlst_base_c** it = lists.mp2DXluDrawLists; it < lists.mp2DXluStart; ++it) {
        if (*it == &s_dlst) return;
    }
    dComIfGd_set2DXlu(&s_dlst);
}



#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"
#include "util.hpp"

#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"

#include "JSystem/J2DGraph/J2DGrafContext.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/TColor.h"
#include "SSystem/SComponent/c_math.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_map.h"
#include "d/d_map_path_dmap.h"
#include "d/d_menu_dmap.h"
#include "d/d_menu_dmap_map.h"
#include "d/d_menu_fmap.h"
#include "d/d_menu_fmap2D.h"
#include "d/d_menu_map_common.h"
#include "d/d_meter_map.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"

#include <gx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

extern const ConfigService* svc_config;

#if defined(_WIN32)
DEFINE_HOOK_SYMBOL("?drawIcon@dMenu_Fmap_c@@QEAAXM_N@Z", void(dMenu_Fmap_c*, f32, bool),
    MapFmapIconsHook);
#else
DEFINE_HOOK_SYMBOL("_ZN12dMenu_Fmap_c8drawIconEfb", void(dMenu_Fmap_c*, f32, bool),
    MapFmapIconsHook);
#endif
DEFINE_HOOK_SYMBOL("dMenu_Dmap_c::getPlayerIconPos", void(dMenu_Dmap_c*, s8, f32),
    MapDmapPlayerIconHook);
DEFINE_HOOK_SYMBOL("dMenuMapCommon_c::drawIcon", void(dMenuMapCommon_c*, f32, f32, f32, f32),
    MapCommonDrawIconHook);
DEFINE_HOOK_SYMBOL("dMenuMapCommon_c::clearIconInfo", void(dMenuMapCommon_c*),
    MapCommonClearHook);
DEFINE_HOOK_SYMBOL("dMeterMap_c::draw", void(dMeterMap_c*), MapMeterDrawHook);
#if defined(_WIN32)
DEFINE_HOOK_SYMBOL("?draw@J2DPicture@@UEAAXMMMM_N00@Z",
    void(J2DPicture*, f32, f32, f32, f32, bool, bool, bool), MapPictureDrawHook);
#else
DEFINE_HOOK_SYMBOL("_ZN10J2DPicture4drawEffffbbb",
    void(J2DPicture*, f32, f32, f32, f32, bool, bool, bool), MapPictureDrawHook);
#endif

namespace {

ConfigVarHandle s_fullMapVar = 0;
ConfigVarHandle s_dungeonMapVar = 0;
ConfigVarHandle s_namesVar = 0;
ConfigVarHandle s_nameSizeVar = 0;
ConfigVarHandle s_minimapVar = 0;
ConfigVarHandle s_arrowSizeVar = 0;
ConfigVarHandle s_edgeVar = 0;
ConfigVarHandle s_otherFloorVar = 0;
ConfigVarHandle s_colorsVar = 0;

const u8 kPlayerColors[kCoopMaxPlayers][3] = {
    {90, 190, 255},
    {255, 110, 90},
    {110, 230, 120},
    {235, 120, 235},
    {255, 160, 60},
    {170, 135, 255},
    {70, 225, 205},
    {255, 255, 255},
    {150, 210, 60},
    {255, 140, 180},
    {120, 150, 255},
    {230, 200, 150},
    {200, 90, 60},
    {100, 180, 170},
    {210, 170, 255},
    {180, 180, 180},
};

JUtility::TColor player_color(uint8_t id, u8 alpha) {
    if (cfg_int(s_colorsVar, 0) == 1 || id >= kCoopMaxPlayers) {
        return JUtility::TColor(255, 255, 255, alpha);
    }
    return JUtility::TColor(kPlayerColors[id][0], kPlayerColors[id][1], kPlayerColors[id][2], alpha);
}

bool mirror_mode() {
    static bool s_mirror = false;
    static std::chrono::steady_clock::time_point s_readAt{};
    static bool s_read = false;
    const auto now = std::chrono::steady_clock::now();
    if (!s_read || now - s_readAt > std::chrono::seconds(5)) {
        s_read = true;
        s_readAt = now;
        std::string value;
        s_mirror = coop_config_json_value("game.enableMirrorMode", &value) && value == "true";
    }
    return s_mirror;
}

struct Spot {
    bool sameStage = false;
    int room = -1;
    f32 x = 0.0f, y = 0.0f, z = 0.0f;
    s16 angle = 0;
};

void to_map_space(int room, f32 x, f32 y, f32 z, s16 angle, Spot* out) {
    out->x = x;
    out->y = y;
    out->z = z;
    out->angle = angle;
    if (room < 0 || room >= 64) return;
    BE(Vec) at;
    at.x = x;
    at.y = y;
    at.z = z;
    BE(Vec) ahead;
    ahead.x = x + 100.0f * cM_ssin(angle);
    ahead.y = y;
    ahead.z = z + 100.0f * cM_scos(angle);
    dMapInfo_n::correctionOriginPos(static_cast<s8>(room), &at);
    dMapInfo_n::correctionOriginPos(static_cast<s8>(room), &ahead);
    out->x = at.x;
    out->y = at.y;
    out->z = at.z;
    const f32 dx = static_cast<f32>(ahead.x) - out->x;
    const f32 dz = static_cast<f32>(ahead.z) - out->z;
    if (dx * dx + dz * dz > 1.0f) out->angle = cM_atan2s(dx, dz);
}

bool other_player(uint8_t id) {
    return id != coop_net_local_id() && coop_net_player_present(id);
}

bool spot_of(uint8_t id, Spot* out) {
    if (!other_player(id)) return false;
    const CoopPeer& p = features_peer_of(id);
    if (!p.present || !p.inGame || p.stage[0] == '\0') return false;
    const char* ours = dComIfGp_getStartStageName();
    out->sameStage = ours != nullptr && std::strncmp(ours, p.stage, 8) == 0;
    out->room = p.curRoom;
    f32 x = p.x, y = p.y, z = p.z;
    short angle = p.angleY;
    if (out->sameStage) {
        f32 lx = 0.0f, ly = 0.0f, lz = 0.0f;
        short la = 0;
        if (puppet_hook_get_pose_of(id, &lx, &ly, &lz, &la, nullptr, nullptr)) {
            x = lx;
            y = ly;
            z = lz;
            angle = la;
        }
        to_map_space(out->room, x, y, z, angle, out);
    } else {
        out->x = x;
        out->y = y;
        out->z = z;
        out->angle = angle;
    }
    return true;
}

std::string name_of(uint8_t id) {
    const CoopPeer& p = features_peer_of(id);
    return p.name.empty() ? std::string("Player") : p.name;
}

struct Mark {
    dMenuMapCommon_c* list = nullptr;
    u16 index = 0;
    uint8_t player = 0;

    dMenu_DmapMapCtrl_c* ctrl = nullptr;
    s8 floor = 0;
    f32 alpha = 1.0f;
};
std::vector<Mark> s_marks;

bool s_loggedFull = false;
bool s_loggedDungeon = false;
bool s_loggedMinimap = false;

void drop_marks(const dMenuMapCommon_c* list) {
    s_marks.erase(std::remove_if(s_marks.begin(), s_marks.end(),
                      [list](const Mark& m) { return m.list == list; }),
        s_marks.end());
}

const u16 kIconRoom = 120;

bool add_face(dMenuMapCommon_c* list, f32 x, f32 y, f32 alpha, s16 angle, f32 scale) {
    if (list->mIconNum >= kIconRoom) return false;
    const f32 rotation = cM_sht2d(mirror_mode() ? static_cast<s16>(-angle) : angle);
    return list->setIconInfo(ICON_LINK_e, x, y, alpha, rotation, scale, 1);
}

void on_fmap_icons_post(ModContext*, void* args, void*, void*) {
    auto* fmap = mods::arg<dMenu_Fmap_c*>(args, 0);
    const bool positionOnly = mods::arg<bool>(args, 2);
    if (fmap == nullptr || fmap->mpDraw2DBack == nullptr || positionOnly) return;
    dMenu_Fmap2DBack_c* back = fmap->mpDraw2DBack;
    dMenuMapCommon_c* list = back;
    drop_marks(list);
    if (!coop_net_connected() || !cfg_bool(s_fullMapVar, false)) return;
    if (back->mpStages == nullptr) return;
    const u8 region = back->getRegionCursor();
    if (region >= 8) return;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        Spot s;
        if (!spot_of(id, &s)) continue;

        const dMenuMapCommon_c::Stage_c::data* stage = nullptr;
        const dMenuMapCommon_c::Stage_c::data* all = back->mpStages->mData;
        const CoopPeer& p = features_peer_of(id);
        for (int k = 0; k < back->mStageDataNum; ++k) {
            if (all[k].mRegionNo == region + 1 && std::strncmp(all[k].mName, p.stage, 8) == 0) {
                stage = &all[k];
                break;
            }
        }
        if (stage == nullptr) continue;
        const f32 wx = s.x + back->mRegionOriginX[region] + static_cast<f32>(stage->mOffsetX);
        const f32 wz = s.z + back->mRegionOriginZ[region] + static_cast<f32>(stage->mOffsetZ);
        f32 px = 0.0f, py = 0.0f;
        back->calcAllMapPos2D(wx - back->mStageTransX, wz - back->mStageTransZ, &px, &py);
        const u16 index = list->mIconNum;
        if (!add_face(list, px, py, 1.0f, s.angle, back->mMapZoomRate)) break;
        Mark m;
        m.list = list;
        m.index = index;
        m.player = id;
        s_marks.push_back(m);
        if (!s_loggedFull) {
            s_loggedFull = true;
            coop_log::info("coop_mod: [MAP] {} on the full map at ({:.0f}, {:.0f}) in region {}",
                name_of(id), px, py, static_cast<int>(region));
        }
    }
}

bool dmap_place(dMenu_DmapMapCtrl_c* ctrl, uint8_t id, s8 floor, f32* x, f32* y, s16* angle,
    bool* onFloor) {
    Spot s;
    if (!spot_of(id, &s) || !s.sameStage) return false;
    ctrl->cnvPosTo2Dpos(s.x, s.z, x, y);
    *angle = s.angle;
    *onFloor = dMapInfo_c::calcFloorNo(s.y, true, s.room) == floor;
    return true;
}

f32 dmap_alpha(bool onFloor, f32 alpha) {
    if (onFloor) return alpha;
    return cfg_bool(s_otherFloorVar, true) ? alpha * 0.35f : 0.0f;
}

void on_dmap_player_icon_post(ModContext*, void* args, void*, void*) {
    auto* dmap = mods::arg<dMenu_Dmap_c*>(args, 0);
    const s8 floor = mods::arg<s8>(args, 1);
    const f32 alpha = mods::arg<f32>(args, 2);
    if (dmap == nullptr || dmap->mMapCtrl == nullptr || dmap->mpDrawBg == nullptr) return;
    if (!coop_net_connected() || !cfg_bool(s_dungeonMapVar, false)) return;
    dMenuMapCommon_c* list = dmap->mpDrawBg;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        f32 x = 0.0f, y = 0.0f;
        s16 angle = 0;
        bool onFloor = false;
        if (!dmap_place(dmap->mMapCtrl, id, floor, &x, &y, &angle, &onFloor)) continue;
        const u16 index = list->mIconNum;
        if (!add_face(list, x, y, dmap_alpha(onFloor, alpha), angle, 1.0f)) break;
        Mark m;
        m.list = list;
        m.index = index;
        m.player = id;
        m.ctrl = dmap->mMapCtrl;
        m.floor = floor;
        m.alpha = alpha;
        s_marks.push_back(m);
        if (!s_loggedDungeon) {
            s_loggedDungeon = true;
            coop_log::info("coop_mod: [MAP] {} on the dungeon map at ({:.0f}, {:.0f}), floor {} ({})",
                name_of(id), x, y, static_cast<int>(floor), onFloor ? "this one" : "another");
        }
    }
}

void on_clear_icons_post(ModContext*, void* args, void*, void*) {
    drop_marks(mods::arg<dMenuMapCommon_c*>(args, 0));
}

bool mark_valid(const Mark& m, const dMenuMapCommon_c* list) {
    return m.list == list && m.index < list->mIconNum &&
           list->mIconInfo[m.index].icon_no == ICON_LINK_e;
}

HookAction on_draw_icons_pre(ModContext*, void* args, void*, void*) {
    auto* list = mods::arg<dMenuMapCommon_c*>(args, 0);
    for (Mark& m : s_marks) {
        if (m.ctrl == nullptr || !mark_valid(m, list)) continue;
        dMenuMapCommon_c::IconInfo_s& info = list->mIconInfo[m.index];
        f32 x = 0.0f, y = 0.0f;
        s16 angle = 0;
        bool onFloor = false;
        if (!coop_net_connected() || !dmap_place(m.ctrl, m.player, m.floor, &x, &y, &angle, &onFloor)) {
            info.alpha_rate = 0.0f;
            continue;
        }
        info.pos_x = x;
        info.pos_y = y;
        info.rotation = cM_sht2d(mirror_mode() ? static_cast<s16>(-angle) : angle);
        info.alpha_rate = dmap_alpha(onFloor, m.alpha);
    }
    return HOOK_CONTINUE;
}

void on_draw_icons_post(ModContext*, void* args, void*, void*) {
    auto* list = mods::arg<dMenuMapCommon_c*>(args, 0);
    const f32 originX = mods::arg<f32>(args, 1);
    const f32 originY = mods::arg<f32>(args, 2);
    const f32 fade = mods::arg<f32>(args, 3) * mods::arg<f32>(args, 4);
    if (s_marks.empty() || !cfg_bool(s_namesVar, true)) return;
    JUTFont* font = mDoExt_getMesgFont();
    J2DGrafContext* graf = dComIfGp_getCurrentGrafPort();
    if (font == nullptr || graf == nullptr) return;
    bool setUp = false;
    const f32 cell = 15.0f * static_cast<f32>(cfg_int(s_nameSizeVar, 100)) / 100.0f;
    const f32 below = list->getIconSizeY(ICON_LINK_e) * 0.5f;
    for (const Mark& m : s_marks) {
        if (!mark_valid(m, list)) continue;
        const dMenuMapCommon_c::IconInfo_s& info = list->mIconInfo[m.index];
        const f32 a = std::clamp(fade * info.alpha_rate, 0.0f, 1.0f);
        if (a < 0.05f) continue;
        if (!setUp) {
            graf->setup2D();
            font->setGX();
            setUp = true;
        }
        const std::string name = name_of(m.player);
        f32 x = originX + info.pos_x;
        if (mirror_mode()) x = list->getMirrorCenterPosX(x, 0.0f);
        const f32 width = font->drawString_scale(0.0f, 0.0f, cell, cell, name.c_str(), false);
        const f32 left = x - width * 0.5f;
        const f32 top = originY + info.pos_y + below;
        const u8 alpha = static_cast<u8>(255.0f * a);
        const f32 shadow = cell * 0.08f;
        font->setCharColor(JUtility::TColor(0, 0, 0, static_cast<u8>(alpha * 0.8f)));
        font->drawString_scale(left + shadow, top + shadow, cell, cell, name.c_str(), true);
        font->setCharColor(player_color(m.player, alpha));
        font->drawString_scale(left, top, cell, cell, name.c_str(), true);
    }
}

void triangle(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy, u32 rgba) {
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(ax, ay, 0.0f);
    GXColor1u32(rgba);
    GXPosition3f32(bx, by, 0.0f);
    GXColor1u32(rgba);
    GXPosition3f32(cx, cy, 0.0f);
    GXColor1u32(rgba);
    GXEnd();
}

u32 rgba_of(const JUtility::TColor& c) {
    return (static_cast<u32>(c.r) << 24) | (static_cast<u32>(c.g) << 16) |
           (static_cast<u32>(c.b) << 8) | c.a;
}

void arrow(f32 x, f32 y, f32 dx, f32 dy, f32 size, const JUtility::TColor& color) {
    const f32 nx = -dy, ny = dx;
    const auto shape = [&](f32 s, u32 rgba) {
        const f32 tipX = x + dx * s * 0.6f, tipY = y + dy * s * 0.6f;
        const f32 backX = x - dx * s * 0.4f, backY = y - dy * s * 0.4f;
        const f32 notchX = x - dx * s * 0.15f, notchY = y - dy * s * 0.15f;
        const f32 wing = s * 0.45f;
        triangle(tipX, tipY, backX + nx * wing, backY + ny * wing, notchX, notchY, rgba);
        triangle(tipX, tipY, notchX, notchY, backX - nx * wing, backY - ny * wing, rgba);
    };
    shape(size * 1.4f, rgba_of(JUtility::TColor(0, 0, 0, static_cast<u8>(color.a * 0.85f))));
    shape(size, rgba_of(color));
}

dMeterMap_c* s_meterDrawing = nullptr;
struct Rect {
    bool valid = false;
    f32 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};
Rect s_minimapRect;

HookAction on_picture_draw_pre(ModContext*, void* args, void*, void*) {
    if (s_meterDrawing == nullptr) return HOOK_CONTINUE;
    if (mods::arg<J2DPicture*>(args, 0) != s_meterDrawing->mMapJ2DPicture) return HOOK_CONTINUE;
    s_minimapRect.valid = true;
    s_minimapRect.x = mods::arg<f32>(args, 1);
    s_minimapRect.y = mods::arg<f32>(args, 2);
    s_minimapRect.w = mods::arg<f32>(args, 3);
    s_minimapRect.h = mods::arg<f32>(args, 4);
    return HOOK_CONTINUE;
}

HookAction on_meter_map_draw_pre(ModContext*, void* args, void*, void*) {
    s_meterDrawing = mods::arg<dMeterMap_c*>(args, 0);
    s_minimapRect.valid = false;
    return HOOK_CONTINUE;
}

void on_meter_map_draw_post(ModContext*, void* args, void*, void*) {
    s_meterDrawing = nullptr;
    auto* self = mods::arg<dMeterMap_c*>(args, 0);
    if (!s_minimapRect.valid) return;
    if (self == nullptr || self->mMap == nullptr || !self->mMap->isDraw()) return;
    if (!coop_net_connected() || !cfg_bool(s_minimapVar, false)) return;
    dMap_c* map = self->mMap;
    const f32 spanX = map->field_0x8;
    const f32 spanZ = map->field_0xc;
    if (!(spanX > 1.0f) || !(spanZ > 1.0f) || self->mMapAlpha == 0) return;

    const f32 left = s_minimapRect.x;
    const f32 top = s_minimapRect.y;
    const f32 w = s_minimapRect.w;
    const f32 h = s_minimapRect.h;
    if (!(w > 1.0f) || !(h > 1.0f)) return;

    const f32 hud = self->mSizeW > 1.0f ? std::clamp(w / self->mSizeW, 0.25f, 4.0f) : 1.0f;

    const bool mirror = mirror_mode();
    const bool edge = cfg_bool(s_edgeVar, true);
    const bool otherFloors = cfg_bool(s_otherFloorVar, true);
    daAlink_c* alink = daAlink_getAlinkActorClass();
    const int stay = dComIfGp_roomControl_getStayNo();
    const s8 ourFloor = alink != nullptr ? dMapInfo_c::calcFloorNo(alink->current.pos.y, true, stay)
                                         : static_cast<s8>(0);
    const f32 size = 9.0f * hud * static_cast<f32>(cfg_int(s_arrowSizeVar, 100)) / 100.0f;
    const f32 baseAlpha = static_cast<f32>(self->mMapAlpha) / 255.0f;

    J2DGrafContext* graf = dComIfGp_getCurrentGrafPort();
    if (graf == nullptr) return;
    bool setUp = false;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        Spot s;
        if (!spot_of(id, &s) || !s.sameStage) continue;
        f32 alpha = baseAlpha;
        if (dMapInfo_c::calcFloorNo(s.y, true, s.room) != ourFloor) {
            if (!otherFloors) continue;
            alpha *= 0.45f;
        }

        f32 u = (s.x - map->mPosX) / spanX;
        const f32 v = (s.z - map->mPosZ) / spanZ + 0.5f;
        u = mirror ? 0.5f - u : 0.5f + u;
        f32 sx = left + u * w;
        f32 sy = top + v * h;
        f32 dx = cM_ssin(s.angle) * (mirror ? -1.0f : 1.0f);
        f32 dy = cM_scos(s.angle);
        const f32 inset = size * 0.7f;
        const bool outside = sx < left + inset || sx > left + w - inset || sy < top + inset ||
                             sy > top + h - inset;
        if (outside) {
            if (!edge) continue;

            const f32 cx = left + w * 0.5f, cy = top + h * 0.5f;
            f32 ox = sx - cx, oy = sy - cy;
            const f32 len = std::sqrt(ox * ox + oy * oy);
            if (len < 0.001f) continue;
            dx = ox / len;
            dy = oy / len;
            sx = std::clamp(sx, left + inset, left + w - inset);
            sy = std::clamp(sy, top + inset, top + h - inset);
            alpha *= 0.8f;
        }
        if (!setUp) {
            graf->setup2D();
            GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_SET);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
            setUp = true;
        }
        arrow(sx, sy, dx, dy, size, player_color(id, static_cast<u8>(255.0f * alpha)));
        if (!s_loggedMinimap) {
            s_loggedMinimap = true;
            coop_log::info("coop_mod: [MAP] {} on the minimap at ({:.0f}, {:.0f}){} - minimap at "
                           "({:.0f}, {:.0f}) {:.0f}x{:.0f}", name_of(id), sx, sy,
                outside ? " (at the edge)" : "", left, top, w, h);
        }
    }
    if (setUp) {

        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_CLR_RGBA, GX_RGBA4, 0);
    }
}

ConfigVarHandle register_bool(const char* name, bool fallback) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = CONFIG_VAR_BOOL;
    desc.default_bool = fallback;
    ConfigVarHandle handle = 0;
    return svc_config->register_var(mod_ctx, &desc, &handle) == MOD_OK ? handle : 0;
}

ConfigVarHandle register_int(const char* name, int64_t fallback) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = CONFIG_VAR_INT;
    desc.default_int = fallback;
    ConfigVarHandle handle = 0;
    return svc_config->register_var(mod_ctx, &desc, &handle) == MOD_OK ? handle : 0;
}

}

void map_markers_init() {
    if (svc_config != nullptr) {

        s_fullMapVar = register_bool("map_players", false);
        s_dungeonMapVar = register_bool("map_players_dungeon", false);
        s_namesVar = register_bool("map_player_names", true);
        s_nameSizeVar = register_int("map_player_name_size", 100);
        s_minimapVar = register_bool("minimap_players", false);
        s_arrowSizeVar = register_int("minimap_arrow_size", 100);
        s_edgeVar = register_bool("minimap_players_edge", true);
        s_otherFloorVar = register_bool("map_players_other_floors", true);
        s_colorsVar = register_int("map_player_colors", 0);
    }
    const int fmap = static_cast<int>(mods::hook::add_post<MapFmapIconsHook>(on_fmap_icons_post));
    const int dmap =
        static_cast<int>(mods::hook::add_post<MapDmapPlayerIconHook>(on_dmap_player_icon_post));
    const int drawPre = static_cast<int>(mods::hook::add_pre<MapCommonDrawIconHook>(on_draw_icons_pre));
    const int drawPost =
        static_cast<int>(mods::hook::add_post<MapCommonDrawIconHook>(on_draw_icons_post));
    const int clear = static_cast<int>(mods::hook::add_post<MapCommonClearHook>(on_clear_icons_post));
    const int meterPre = static_cast<int>(mods::hook::add_pre<MapMeterDrawHook>(on_meter_map_draw_pre));
    const int meter = static_cast<int>(mods::hook::add_post<MapMeterDrawHook>(on_meter_map_draw_post));
    const int picture = static_cast<int>(mods::hook::add_pre<MapPictureDrawHook>(on_picture_draw_pre));
    coop_log::info("coop_mod: [MAP] hooks: full={} dungeon={} draw={}/{} clear={} minimap={}/{} "
                   "picture={}", fmap, dmap, drawPre, drawPost, clear, meterPre, meter, picture);
}

ConfigVarHandle map_markers_full_var() { return s_fullMapVar; }
ConfigVarHandle map_markers_dungeon_var() { return s_dungeonMapVar; }
ConfigVarHandle map_markers_names_var() { return s_namesVar; }
ConfigVarHandle map_markers_name_size_var() { return s_nameSizeVar; }
ConfigVarHandle map_markers_minimap_var() { return s_minimapVar; }
ConfigVarHandle map_markers_arrow_size_var() { return s_arrowSizeVar; }
ConfigVarHandle map_markers_edge_var() { return s_edgeVar; }
ConfigVarHandle map_markers_other_floor_var() { return s_otherFloorVar; }
ConfigVarHandle map_markers_colors_var() { return s_colorsVar; }

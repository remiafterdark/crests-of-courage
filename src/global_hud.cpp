

#include "mod.hpp"
#include "util.hpp"
#include "net/messages.hpp"

#include "mods/svc/config.h"

#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/TColor.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"

#include <algorithm>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

extern const ConfigService* svc_config;

namespace {

ConfigVarHandle s_on = 0;
ConfigVarHandle s_x = 0;
ConfigVarHandle s_y = 0;
ConfigVarHandle s_size = 0;
ConfigVarHandle s_opacity = 0;
ConfigVarHandle s_showAll = 0;
ConfigVarHandle s_showPvp = 0;
bool s_dragging = false;
f32 s_dragDX = 0.0f;
f32 s_dragDY = 0.0f;

ConfigVarHandle reg(const char* name, ConfigVarType type, int64_t fallback, bool fallbackBool) {
    if (svc_config == nullptr) return 0;
    ConfigVarDesc d = CONFIG_VAR_DESC_INIT;
    d.name = name;
    d.type = type;
    d.default_int = fallback;
    d.default_bool = fallbackBool;
    ConfigVarHandle h = 0;
    if (svc_config->register_var(mod_ctx, &d, &h) != MOD_OK) return 0;
    return h;
}

struct Line {
    std::string text;
    bool red;
};

}

void global_hud_register_vars() {
    s_on = reg("global_hud", CONFIG_VAR_BOOL, 0, false);
    s_x = reg("global_hud_x", CONFIG_VAR_INT, 50, false);
    s_y = reg("global_hud_y", CONFIG_VAR_INT, 8, false);
    s_size = reg("global_hud_size", CONFIG_VAR_INT, 75, false);
    s_opacity = reg("global_hud_opacity", CONFIG_VAR_INT, 82, false);
    s_showAll = reg("global_hud_all", CONFIG_VAR_BOOL, 0, false);
    s_showPvp = reg("global_hud_pvp", CONFIG_VAR_BOOL, 0, false);
}

ConfigVarHandle global_hud_var() { return s_on; }
ConfigVarHandle global_hud_x_var() { return s_x; }
ConfigVarHandle global_hud_y_var() { return s_y; }
ConfigVarHandle global_hud_size_var() { return s_size; }
ConfigVarHandle global_hud_opacity_var() { return s_opacity; }
ConfigVarHandle global_hud_all_var() { return s_showAll; }
ConfigVarHandle global_hud_pvp_var() { return s_showPvp; }

void global_hud_reset_place() {
    if (svc_config == nullptr) return;
    svc_config->set_int(mod_ctx, s_x, 50);
    svc_config->set_int(mod_ctx, s_y, 8);
    svc_config->set_int(mod_ctx, s_size, 75);
    svc_config->set_int(mod_ctx, s_opacity, 82);
}

void global_hud_draw(J2DOrthoGraph& ortho, f32 minX, f32 minY, f32 width, f32 height) {
    if (!cfg_bool(s_on, false) || !global_active() || global_connecting()) return;
    JUTFont* font = mDoExt_getMesgFont();
    if (font == nullptr) return;

    int here = 1;
    int pvpHere = global_pvp_on() ? 1 : 0;
    for (uint8_t slot = 1; slot < kCoopMaxPlayers; ++slot) {
        if (!global_slot_present(slot)) continue;
        ++here;
        if (global_slot_pvp(slot)) ++pvpHere;
    }
    std::vector<Line> lines;
    if (cfg_bool(s_showAll, false)) {
        lines.push_back({"Players in Hyrule - " + std::to_string(std::max<uint32_t>(global_total(), 1)), false});
    }
    lines.push_back({"Players in this area - " + std::to_string(here), false});
    if (cfg_bool(s_showPvp, false)) lines.push_back({"PvP players in this area - " + std::to_string(pvpHere), true});

    const f32 scale = std::clamp<f32>(static_cast<f32>(cfg_int(s_size, 75)) / 100.0f, 0.5f, 2.0f);
    const u8 alpha = static_cast<u8>(std::clamp<int64_t>(cfg_int(s_opacity, 82), 20, 100) * 255 / 100);
    const f32 cell = 16.0f * scale;
    const f32 padX = 13.0f * scale;
    const f32 padY = 8.0f * scale;
    f32 textW = 0.0f;
    for (const Line& l : lines) textW = std::max(textW, coop_text_width(font, l.text.c_str(), cell));
    const f32 boxW = textW + padX * 2.0f;
    const f32 boxH = cell * static_cast<f32>(lines.size()) + padY * 2.0f;
    f32 cx = minX + width * static_cast<f32>(std::clamp<int64_t>(cfg_int(s_x, 50), 0, 100)) / 100.0f;
    f32 cy = minY + height * static_cast<f32>(std::clamp<int64_t>(cfg_int(s_y, 8), 0, 100)) / 100.0f;
    const auto place = [&](f32& x0, f32& y0) {
        x0 = std::clamp(cx - boxW * 0.5f, minX + 4.0f, minX + width - boxW - 4.0f);
        y0 = std::clamp(cy - boxH * 0.5f, minY + 4.0f, minY + height - boxH - 4.0f);
    };
    f32 x0 = 0.0f, y0 = 0.0f;
    place(x0, y0);

#if defined(_WIN32)

    static bool s_wasDown = false;
    POINT mouse{};
    RECT client{};
    HWND hwnd = GetForegroundWindow();
    if (chat_game_in_front() && hwnd != nullptr && GetCursorPos(&mouse) && ScreenToClient(hwnd, &mouse) &&
        GetClientRect(hwnd, &client) && client.right > 0 && client.bottom > 0) {
        const f32 mx = minX + static_cast<f32>(mouse.x) / static_cast<f32>(client.right) * width;
        const f32 my = minY + static_cast<f32>(mouse.y) / static_cast<f32>(client.bottom) * height;
        const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const bool over = mx >= x0 && mx <= x0 + boxW && my >= y0 && my <= y0 + boxH;
        if (over && down && !s_wasDown) {
            s_dragging = true;
            s_dragDX = mx - cx;
            s_dragDY = my - cy;
        }
        if (!down) s_dragging = false;
        if (s_dragging && svc_config != nullptr) {
            cx = std::clamp(mx - s_dragDX, minX, minX + width);
            cy = std::clamp(my - s_dragDY, minY, minY + height);
            svc_config->set_int(mod_ctx, s_x, static_cast<int64_t>((cx - minX) * 100.0f / width));
            svc_config->set_int(mod_ctx, s_y, static_cast<int64_t>((cy - minY) * 100.0f / height));
            place(x0, y0);
        }
        s_wasDown = down;
    }
#endif

    ortho.setColor(JUtility::TColor(12, 14, 20, alpha));
    ortho.fillBox(JGeometry::TBox2<f32>(x0, y0, x0 + boxW, y0 + boxH));
    ortho.setColor(JUtility::TColor(220, 188, 64, alpha));
    ortho.fillBox(JGeometry::TBox2<f32>(x0, y0, x0 + 4.0f * scale, y0 + boxH));
    font->setGX();
    const f32 tx = x0 + padX;
    const f32 ty = y0 + padY + cell * 0.85f;
    const f32 shadow = cell * 0.07f;
    for (size_t i = 0; i < lines.size(); ++i) {
        const f32 ly = ty + cell * static_cast<f32>(i);
        font->setCharColor(JUtility::TColor(0, 0, 0, static_cast<u8>(alpha * 0.85f)));
        font->drawString_scale(tx + shadow, ly + shadow, cell, cell, lines[i].text.c_str(), true);
        font->setCharColor(lines[i].red ? JUtility::TColor(255, 70, 70, alpha) : JUtility::TColor(255, 255, 255, alpha));
        font->drawString_scale(tx, ly, cell, cell, lines[i].text.c_str(), true);
    }
}

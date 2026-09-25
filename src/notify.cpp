

#include "mod.hpp"
#include "print.hpp"
#include "util.hpp"

#include "mods/svc/config.h"
#include "mods/svc/ui.h"

#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/TColor.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_drawlist.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <string>

extern const ConfigService* svc_config;
extern const UiService* svc_ui;

namespace {

ConfigVarHandle s_onVar = 0;
ConfigVarHandle s_kindVar[kNotifyKinds] = {};
ConfigVarHandle s_sideVar = 0;
ConfigVarHandle s_edgeVar = 0;
ConfigVarHandle s_fromSideVar = 0;
ConfigVarHandle s_fromEdgeVar = 0;
ConfigVarHandle s_busyVar = 0;
ConfigVarHandle s_hurryVar = 0;
ConfigVarHandle s_moreVar = 0;
ConfigVarHandle s_slideVar = 0;
ConfigVarHandle s_previewVar = 0;
ConfigVarHandle s_testAmountVar = 0;
ConfigVarHandle s_cycleVar = 0;
bool s_previewRespawn = false;
int64_t s_savedFromEdge = -1;
ConfigVarHandle s_secondsVar = 0;
ConfigVarHandle s_sizeVar = 0;
ConfigVarHandle s_maxVar = 0;
ConfigVarHandle s_engineVar = 0;
ConfigVarHandle s_testVar = 0;
uint32_t s_testTicks = 0;

using Clock = std::chrono::steady_clock;

struct Note {
    NotifyKind kind = kNotifyOther;
    std::string title;
    std::string body;
    Clock::time_point born;

    Clock::time_point shownAt{};
    uint32_t lifeMs = 5000;
    bool preview = false;
};
std::deque<Note> s_notes;
const size_t kNotesKept = 64;

const f32 kSlideMs = 220.0f;
const f32 kFadeOutMs = 350.0f;

void engine_toast(const std::string& title, const std::string& body, uint32_t ms) {
    if (svc_ui == nullptr) return;
    std::string t, b;
    const auto esc = [](const std::string& in, std::string& out) {
        for (char c : in) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
            }
        }
    };
    esc(title, t);
    esc(body, b);
    UiToastDesc desc = UI_TOAST_DESC_INIT;
    desc.title_rml = t.c_str();
    desc.body_rml = b.c_str();
    desc.duration_ms = ms;
    svc_ui->push_toast(mod_ctx, &desc);
}

enum Where { kTopLeft, kTopMiddle, kTopRight, kBottomLeft, kBottomMiddle, kBottomRight };

JUtility::TColor kind_color(NotifyKind kind, u8 alpha) {
    switch (kind) {
    case kNotifyItems: return JUtility::TColor(255, 200, 70, alpha);
    case kNotifyPlayers: return JUtility::TColor(110, 220, 120, alpha);
    case kNotifyRando: return JUtility::TColor(190, 130, 255, alpha);
    case kNotifyTeleport: return JUtility::TColor(90, 180, 255, alpha);
    default: return JUtility::TColor(200, 200, 200, alpha);
    }
}

f32 text_width(JUTFont* font, const std::string& text, f32 cell) {
    if (text.empty()) return 0.0f;
    return coop_text_width(font, text.c_str(), cell);
}

void draw_shadowed(JUTFont* font, f32 x, f32 y, f32 cell, const char* text, JUtility::TColor color) {
    const f32 shadow = cell * 0.07f;
    font->setCharColor(JUtility::TColor(0, 0, 0, static_cast<u8>(color.a * 0.8f)));
    font->drawString_scale(x + shadow, y + shadow, cell, cell, text, true);
    font->setCharColor(color);
    font->drawString_scale(x, y, cell, cell, text, true);
}

class NotifyDlst : public dDlst_base_c {
public:
    virtual void draw() {
        if (s_notes.empty()) return;
        static bool s_saidDrawing = false;
        if (!s_saidDrawing) {
            s_saidDrawing = true;
            coop_log::info("coop_mod: [NOTIFY] drawing our own ({} queued)", s_notes.size());
        }
        JUTFont* font = mDoExt_getMesgFont();
        if (font == nullptr) return;
        const f32 minX = mDoGph_gInf_c::getMinXF();
        const f32 minY = mDoGph_gInf_c::getMinYF();
        const f32 width = mDoGph_gInf_c::getWidthF();
        const f32 height = mDoGph_gInf_c::getHeightF();
        J2DOrthoGraph ortho(0.0f, 0.0f, static_cast<f32>(FB_WIDTH), static_cast<f32>(FB_HEIGHT),
            -1.0f, 1.0f);
        ortho.setOrtho(minX, minY, width, height, -1.0f, 1.0f);
        ortho.setPort();

        const f32 scale = static_cast<f32>(cfg_int(s_sizeVar, 80)) / 100.0f;
        const f32 titleCell = 17.0f * scale;
        const f32 bodyCell = 14.0f * scale;
        const f32 pad = 8.0f * scale;
        const f32 stripe = 4.0f * scale;
        const f32 gap = 6.0f * scale;
        const f32 margin = 16.0f;
        const f32 minW = 210.0f * scale;
        const int column = static_cast<int>(std::clamp<int64_t>(cfg_int(s_sideVar, 2), 0, 2));
        const bool top = cfg_int(s_edgeVar, 1) == 0;
        const f32 fromSide = static_cast<f32>(cfg_int(s_fromSideVar, 0));
        const f32 fromEdge = static_cast<f32>(cfg_int(s_fromEdgeVar, 50));

        const f32 burstMs = static_cast<f32>(std::clamp<int64_t>(cfg_int(s_busyVar, 25), 5, 100)) * 100.0f;
        const int hurryAt = static_cast<int>(std::clamp<int64_t>(cfg_int(s_hurryVar, 1), 1, 30));
        const bool slides = cfg_bool(s_slideVar, true);

        const int maxShown = static_cast<int>(std::clamp<int64_t>(cfg_int(s_maxVar, 4), 1, 8));
        const int waiting = std::max(0, static_cast<int>(s_notes.size()) - maxShown);
        const Clock::time_point now = Clock::now();

        struct Shown {
            Note* note;
            f32 alpha;
            f32 slide;
            f32 boxW;
            f32 boxH;
        };
        Shown vis[8];
        int count = 0;

        const bool holdPreview = cfg_bool(s_previewVar, false) && !cfg_bool(s_cycleVar, false);
        for (Note& n : s_notes) {
            if (count >= maxShown) break;
            if (n.shownAt == Clock::time_point{}) n.shownAt = now;
            f32 age = static_cast<f32>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - n.shownAt).count());
            if (n.preview && holdPreview) {

                if (age > kSlideMs) {
                    n.shownAt = now - std::chrono::milliseconds(static_cast<int>(kSlideMs));
                    age = kSlideMs;
                }
                n.lifeMs = std::max<uint32_t>(n.lifeMs, static_cast<uint32_t>(kSlideMs + kFadeOutMs + 1000.0f));
            }

            if (!(n.preview && holdPreview) && waiting >= hurryAt &&
                static_cast<f32>(n.lifeMs) > burstMs) {
                n.lifeMs = static_cast<uint32_t>(std::max(burstMs, age + kFadeOutMs));
            }
            const f32 life = static_cast<f32>(n.lifeMs);
            if (age >= life) continue;
            Shown& v = vis[count++];
            v.note = &n;
            v.alpha = 1.0f;
            if (life - age < kFadeOutMs) v.alpha = (life - age) / kFadeOutMs;

            v.slide = 0.0f;
            if (age < kSlideMs && slides) {
                const f32 t = age / kSlideMs;
                v.slide = (1.0f - t) * (1.0f - t);
                v.alpha = std::min(v.alpha, 0.35f + 0.65f * t);
            }
            const f32 titleW = text_width(font, n.title, titleCell);
            const f32 bodyW = text_width(font, n.body, bodyCell);
            v.boxW = std::max(minW, std::max(titleW, bodyW) + pad * 2.0f + stripe);
            v.boxH = pad * 2.0f + titleCell + (n.body.empty() ? 0.0f : bodyCell + 3.0f);
        }
        if (count == 0) return;

        const bool narrowFirst = column == 1 && !top;
        std::stable_sort(vis, vis + count, [narrowFirst](const Shown& l, const Shown& r) {
            return narrowFirst ? l.boxW < r.boxW : l.boxW > r.boxW;
        });

        f32 totalH = 0.0f;
        for (int i = 0; i < count; ++i) totalH += vis[i].boxH + (i > 0 ? gap : 0.0f);
        const f32 stackTop =
            top ? minY + margin + fromEdge : minY + height - margin - fromEdge - totalH;
        f32 y = stackTop;
        for (int i = 0; i < count; ++i) {
            const Shown& v = vis[i];
            const Note& n = *v.note;
            f32 x0 = minX + margin + fromSide;
            if (column == 1) x0 = minX + (width - v.boxW) * 0.5f;
            if (column == 2) x0 = minX + width - margin - fromSide - v.boxW;
            f32 y0 = y;
            if (column == 0) x0 -= v.slide * (v.boxW + margin);
            if (column == 2) x0 += v.slide * (v.boxW + margin);
            if (column == 1) y0 += (top ? -1.0f : 1.0f) * v.slide * (v.boxH + margin);

            ortho.setColor(JUtility::TColor(12, 14, 20, static_cast<u8>(205.0f * v.alpha)));
            ortho.fillBox(JGeometry::TBox2<f32>(x0, y0, x0 + v.boxW, y0 + v.boxH));
            ortho.setColor(kind_color(n.kind, static_cast<u8>(255.0f * v.alpha)));
            ortho.fillBox(JGeometry::TBox2<f32>(x0, y0, x0 + stripe, y0 + v.boxH));

            font->setGX();
            const u8 a = static_cast<u8>(255.0f * v.alpha);
            const f32 tx = x0 + stripe + pad;
            const f32 ty = y0 + pad + titleCell * 0.85f;
            draw_shadowed(font, tx, ty, titleCell, n.title.c_str(), JUtility::TColor(255, 255, 255, a));
            if (!n.body.empty()) {
                draw_shadowed(font, tx, ty + bodyCell + 3.0f, bodyCell, n.body.c_str(),
                    JUtility::TColor(200, 208, 220, a));
            }

            ortho.setPort();
            y += v.boxH + gap;
        }

        if (waiting > 0 && cfg_bool(s_moreVar, true)) {
            char more[32];
            std::snprintf(more, sizeof(more), "+%d more", waiting);
            const f32 w = text_width(font, more, bodyCell);
            f32 mx = minX + margin + fromSide;
            if (column == 1) mx = minX + (width - w) * 0.5f;
            if (column == 2) mx = minX + width - margin - fromSide - w;
            const f32 my = top ? y + bodyCell : stackTop - 4.0f;
            font->setGX();
            draw_shadowed(font, mx, my, bodyCell, more, JUtility::TColor(225, 230, 240, 235));
            ortho.setPort();
        }
    }
};
NotifyDlst s_dlst;

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

void notify_register_vars(ConfigVarHandle itemsVar) {
    if (svc_config == nullptr) return;
    s_onVar = register_bool("notify_on", true);

    s_kindVar[kNotifyItems] = itemsVar;
    s_kindVar[kNotifyPlayers] = register_bool("notify_players", true);
    s_kindVar[kNotifyRando] = register_bool("notify_rando", true);
    s_kindVar[kNotifyTeleport] = register_bool("notify_teleport", true);
    s_kindVar[kNotifyOther] = register_bool("notify_other", true);

    s_sideVar = register_int("notify_side", 2);
    s_edgeVar = register_int("notify_edge", 1);
    s_fromSideVar = register_int("notify_from_side", 0);
    s_fromEdgeVar = register_int("notify_from_edge", 50);
    s_secondsVar = register_int("notify_seconds", 5);
    s_busyVar = register_int("notify_busy_tenths", 25);
    s_hurryVar = register_int("notify_hurry_at", 1);
    s_sizeVar = register_int("notify_size_pct", 80);
    s_maxVar = register_int("notify_max", 4);
    s_moreVar = register_bool("notify_more_line", true);
    s_slideVar = register_bool("notify_slide", true);
    s_previewVar = register_bool("notify_preview", false);
    s_testAmountVar = register_int("notify_test_amount", 6);
    s_cycleVar = register_bool("notify_test_cycle", false);

    const auto previewChanged = [](ModContext*, ConfigVarHandle, const ConfigVarValue*,
                                    const ConfigVarValue*, void*) { s_previewRespawn = true; };
    svc_config->subscribe(mod_ctx, s_previewVar, previewChanged, nullptr, nullptr);
    svc_config->subscribe(mod_ctx, s_testAmountVar, previewChanged, nullptr, nullptr);

    svc_config->subscribe(mod_ctx, s_sideVar,
        [](ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue* previous,
            void*) {
            if (value == nullptr || previous == nullptr || s_fromEdgeVar == 0) return;
            const bool nowMiddle = value->int_value == 1;
            const bool wasMiddle = previous->int_value == 1;
            if (nowMiddle && !wasMiddle) {
                s_savedFromEdge = cfg_int(s_fromEdgeVar, 50);
                svc_config->set_int(mod_ctx, s_fromEdgeVar, 0);
            } else if (!nowMiddle && wasMiddle) {
                svc_config->set_int(mod_ctx, s_fromEdgeVar, s_savedFromEdge >= 0 ? s_savedFromEdge : 50);
                s_savedFromEdge = -1;
            }
        },
        nullptr, nullptr);
    s_engineVar = register_bool("notify_use_dusklight", false);
    s_testVar = register_int("debug_notify_test_ticks", 0);
}

void coop_notify(NotifyKind kind, const std::string& title, const std::string& body,
    uint32_t durationMs) {
    if (title.empty() && body.empty()) return;
    if (!cfg_bool(s_onVar, true)) return;
    if (kind >= 0 && kind < kNotifyKinds && s_kindVar[kind] != 0 && !cfg_bool(s_kindVar[kind], true)) {
        return;
    }
    const int64_t seconds = std::clamp<int64_t>(cfg_int(s_secondsVar, 5), 1, 30);

    const uint32_t ms = durationMs != 0 ? durationMs : static_cast<uint32_t>(seconds * 1000);

    if (cfg_bool(s_engineVar, false) || daAlink_getAlinkActorClass() == nullptr) {
        static int s_saidEngine = 0;
        if (s_saidEngine++ < 3) {
            coop_log::info("coop_mod: [NOTIFY] '{}' to Dusklight's toast - {}", title,
                cfg_bool(s_engineVar, false) ? "the setting asks for it" : "no file loaded");
        }
        engine_toast(title, body, ms);
        return;
    }
    Note n;
    n.kind = kind;
    n.title = title;
    n.body = body;
    n.born = Clock::now();
    n.lifeMs = ms;
    s_notes.push_back(n);
    while (s_notes.size() > kNotesKept) s_notes.pop_front();
}

void notify_debug_test();

void feed_preview() {
    const bool on = cfg_bool(s_previewVar, false);
    const bool respawn = s_previewRespawn;
    s_previewRespawn = false;
    bool anyPreview = false;
    for (const Note& n : s_notes) anyPreview = anyPreview || n.preview;
    if (!on || respawn) {
        s_notes.erase(std::remove_if(s_notes.begin(), s_notes.end(),
                          [](const Note& n) { return n.preview; }),
            s_notes.end());
        anyPreview = false;
    }
    if (!on) return;
    if (anyPreview) return;
    if (!respawn && !cfg_bool(s_cycleVar, false)) return;
    static const NotifyKind kKinds[] = {kNotifyItems, kNotifyPlayers, kNotifyRando,
        kNotifyTeleport, kNotifyOther};
    const int amount = static_cast<int>(std::clamp<int64_t>(cfg_int(s_testAmountVar, 6), 1, 40));
    const uint32_t lifeMs =
        static_cast<uint32_t>(std::clamp<int64_t>(cfg_int(s_secondsVar, 5), 1, 30) * 1000);
    for (int i = 0; i < amount; ++i) {
        Note n;
        n.kind = kKinds[i % 5];
        n.title = "Custom notif #" + std::to_string(i + 1);
        n.born = Clock::now();
        n.lifeMs = lifeMs;
        n.preview = true;
        s_notes.push_back(n);
    }
    while (s_notes.size() > kNotesKept) s_notes.pop_front();
}

void notify_queue() {
    feed_preview();

    if (s_testVar != 0 && cfg_int(s_testVar, 0) > 0 && s_testTicks <= static_cast<uint32_t>(cfg_int(s_testVar, 0))) {
        if (++s_testTicks == static_cast<uint32_t>(cfg_int(s_testVar, 0))) notify_debug_test();
    }

    const Clock::time_point now = Clock::now();
    s_notes.erase(std::remove_if(s_notes.begin(), s_notes.end(),
                      [now](const Note& n) {
                          return n.shownAt != Clock::time_point{} &&
                                 now - n.shownAt > std::chrono::milliseconds(n.lifeMs);
                      }),
        s_notes.end());
    dDlst_list_c& lists = g_dComIfG_gameInfo.drawlist;
    for (dDlst_base_c** it = lists.mp2DXluDrawLists; it < lists.mp2DXluStart; ++it) {
        if (*it == &s_dlst) return;
    }
    dComIfGd_set2DXlu(&s_dlst);
}

ConfigVarHandle notify_on_var() { return s_onVar; }
ConfigVarHandle notify_kind_var(NotifyKind kind) {
    return (kind >= 0 && kind < kNotifyKinds) ? s_kindVar[kind] : 0;
}
ConfigVarHandle notify_side_var() { return s_sideVar; }
ConfigVarHandle notify_edge_var() { return s_edgeVar; }
ConfigVarHandle notify_from_side_var() { return s_fromSideVar; }
ConfigVarHandle notify_from_edge_var() { return s_fromEdgeVar; }
ConfigVarHandle notify_busy_var() { return s_busyVar; }
ConfigVarHandle notify_hurry_var() { return s_hurryVar; }
ConfigVarHandle notify_more_var() { return s_moreVar; }
ConfigVarHandle notify_slide_var() { return s_slideVar; }
ConfigVarHandle notify_preview_var() { return s_previewVar; }
ConfigVarHandle notify_test_amount_var() { return s_testAmountVar; }
ConfigVarHandle notify_cycle_var() { return s_cycleVar; }

void notify_stop_preview() {
    if (s_previewVar == 0 || svc_config == nullptr || !cfg_bool(s_previewVar, false)) return;
    svc_config->set_bool(mod_ctx, s_previewVar, false);

}
ConfigVarHandle notify_seconds_var() { return s_secondsVar; }
ConfigVarHandle notify_size_var() { return s_sizeVar; }
ConfigVarHandle notify_max_var() { return s_maxVar; }
ConfigVarHandle notify_engine_var() { return s_engineVar; }

void notify_debug_test() {
    static const struct {
        NotifyKind kind;
        const char* title;
        const char* body;
    } kSamples[] = {
        {kNotifyPlayers, "SwiftPawn35 joined", ""},
        {kNotifyItems, "Rom found the Slingshot", "You got it too."},
        {kNotifyRando, "Got the host's randomizer seed", "Blizzeta Coro Puppet"},
        {kNotifyTeleport, "Teleporting", "Heading to Remi."},
        {kNotifyOther, "Remi", "died, so did you."},
        {kNotifyItems, "LesOp bottled Red Potion", "You got one too."},
        {kNotifyItems, "Rom found a Piece of Heart", "You got it too."},
        {kNotifyPlayers, "the thundernet joined", ""},
        {kNotifyItems, "SwiftPawn35 found the Lantern", "You got it too."},
        {kNotifyItems, "Remi found a Golden Bug", "You got it too."},
    };
    for (int round = 0; round < 2; ++round) {
        for (const auto& sample : kSamples) coop_notify(sample.kind, sample.title, sample.body);
    }
}

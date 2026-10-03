

#include "mod.hpp"
#include "print.hpp"
#include "util.hpp"

#include "mods/svc/hook.hpp"
#include "mods/svc/ui.h"

#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/JUtility/JUTFont.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "m_Do/m_Do_controller_pad.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

struct ChatKeys {
    std::string typed;
    int backspaces = 0;
    bool enter = false;
    bool escape = false;
    bool paste = false;
};
bool chat_keys_supported();
void chat_keys_begin();
void chat_keys_poll(ChatKeys& out);

DEFINE_HOOK(&mDoCPd_c::read, ChatPadReadHook);

namespace {

using Clock = std::chrono::steady_clock;

struct Line {
    uint32_t id = 0;
    std::string tag;
    std::string name;
    std::string text;
    Clock::time_point at;
};
std::deque<Line> s_lines;
const size_t kLinesKept = 40;
const size_t kChatMax = 100;

ConfigVarHandle s_showVar = 0;
ConfigVarHandle s_sizeVar = 0;
ConfigVarHandle s_fadeVar = 0;

bool s_typing = false;
std::string s_typed;

UiWindowHandle s_window = 0;
UiElementHandle s_input = 0;
std::string s_draft;
bool s_closeWindow = false;
bool s_focusInput = false;

void push(Line line) {
    s_lines.push_back(std::move(line));
    while (s_lines.size() > kLinesKept) s_lines.pop_front();
}

void send(std::string text) {
    while (!text.empty() && text.back() == ' ') text.pop_back();
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    if (text.empty()) return;
    if (text.size() > kChatMax) text.resize(kChatMax);
    if (!global_send_chat(text)) chat_note("Hyrule Online isn't connected.");
}

void on_draft_get(ModContext*, void*, UiControlValue* out) {
    out->string_value = s_draft.c_str();
}

void on_draft_set(ModContext*, void*, const UiControlValue* value) {
    const std::string text = value->string_value != nullptr ? value->string_value : "";
    s_draft.clear();
    if (text.empty()) return;
    send(text);
    s_closeWindow = true;
}

GlobalPlayer s_blockTargets[5];

void block_pressed(ModContext*, void* data) {
    const GlobalPlayer& who = *static_cast<GlobalPlayer*>(data);
    global_set_blocked(who, true);
    chat_note("Blocked " + who.name + ".");
    s_closeWindow = true;
}

ModResult build_tab(ModContext*, UiWindowHandle, UiElementHandle pane, UiElementHandle, void*,
    ModError*) {
    UiControlDesc input = UI_CONTROL_DESC_INIT;
    input.kind = UI_CONTROL_STRING;
    input.label = "Message";
    input.binding = UI_BINDING_CALLBACKS;
    input.get = on_draft_get;
    input.set = on_draft_set;
    input.max_length = static_cast<int32_t>(kChatMax);
    input.string_set_mode = UI_STRING_SET_ON_COMMIT;
    svc_ui->pane_add_control(mod_ctx, pane, &input, &s_input);
    s_focusInput = true;

    int n = 0;
    for (auto it = s_lines.rbegin(); it != s_lines.rend() && n < 5; ++it) {
        if (it->name.empty() || it->id == global_my_id() || it->tag.empty()) continue;
        bool listed = false;
        for (int k = 0; k < n; ++k) listed = listed || s_blockTargets[k].tag == it->tag;
        if (listed || global_blocked(it->tag)) continue;
        if (n == 0) svc_ui->pane_add_section(mod_ctx, pane, "Block");
        s_blockTargets[n] = GlobalPlayer{it->id, it->name, it->tag};
        static std::string s_labels[5];
        s_labels[n] = it->name;
        UiControlDesc button = UI_CONTROL_DESC_INIT;
        button.kind = UI_CONTROL_BUTTON;
        button.label = s_labels[n].c_str();
        button.on_pressed = block_pressed;
        button.user_data = &s_blockTargets[n];
        svc_ui->pane_add_control(mod_ctx, pane, &button, nullptr);
        ++n;
    }
    return MOD_OK;
}

ModResult update_tab(ModContext*, void*, ModError*) {
    if (s_focusInput && s_input != 0) {
        s_focusInput = false;
        svc_ui->elem_focus(mod_ctx, s_input);
    }
    return MOD_OK;
}

void open_window() {
    if (svc_ui == nullptr || s_window != 0) return;
    UiTabDesc tabs[1] = {UI_TAB_DESC_INIT};
    tabs[0].title = "Chat";
    tabs[0].build = build_tab;
    tabs[0].update = update_tab;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 1;
    desc.on_closed = [](ModContext*, UiWindowHandle, void*) {
        s_window = 0;
        s_input = 0;
    };
    if (svc_ui->window_push(mod_ctx, &desc, &s_window) != MOD_OK) s_window = 0;
}

void start_typing() {
    s_typing = true;
    s_typed.clear();
    chat_keys_begin();
}

void stop_typing() {
    s_typing = false;
    s_typed.clear();
}

void type_update() {
    ChatKeys keys;
    chat_keys_poll(keys);
    if (keys.escape) {
        stop_typing();
        return;
    }
    for (int i = 0; i < keys.backspaces && !s_typed.empty(); ++i) s_typed.pop_back();
    s_typed += keys.typed;
    if (keys.paste && svc_ui != nullptr && svc_ui->get_clipboard_text != nullptr) {
        char clip[256] = {};
        if (svc_ui->get_clipboard_text(mod_ctx, clip, sizeof(clip), nullptr) == MOD_OK) {
            for (const char* c = clip; *c != '\0'; ++c) {
                if (*c >= 0x20 && *c < 0x7F) s_typed.push_back(*c);
            }
        }
    }
    if (s_typed.size() > kChatMax) s_typed.resize(kChatMax);
    if (keys.enter) {
        send(s_typed);
        stop_typing();
    }
}

void on_pad_read_post(ModContext*, void*, void*, void*) {
    if (!s_typing) return;
    interface_of_controller_pad& p = mDoCPd_c::getCpadInfo(0);
    p.mMainStickPosX = p.mMainStickPosY = p.mMainStickValue = 0.0f;
    p.mMainStickAngle = 0;
    p.mCStickPosX = p.mCStickPosY = p.mCStickValue = 0.0f;
    p.mCStickAngle = 0;
    p.mAnalogA = p.mAnalogB = p.mTriggerLeft = p.mTriggerRight = 0.0f;
    p.mButtonFlags = 0;
    p.mPressedButtonFlags = 0;
}

std::vector<std::string> wrap(JUTFont* font, const std::string& text, f32 cell, f32 width) {
    std::vector<std::string> out;
    std::string line;
    const auto fits = [&](const std::string& s) { return coop_text_width(font, s.c_str(), cell) <= width; };
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(' ', at);
        if (end == std::string::npos) end = text.size();
        std::string word = text.substr(at, end - at);
        at = end + 1;
        const std::string joined = line.empty() ? word : line + " " + word;
        if (fits(joined)) {
            line = joined;
            continue;
        }
        if (!line.empty()) out.push_back(line);
        line.clear();
        while (!word.empty() && !fits(word)) {
            size_t n = word.size();
            while (n > 1 && !fits(word.substr(0, n))) --n;
            out.push_back(word.substr(0, n));
            word.erase(0, n);
        }
        line = word;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

void shadowed(JUTFont* font, f32 x, f32 y, f32 cell, const std::string& text, JUtility::TColor color) {
    const f32 shadow = cell * 0.07f;
    font->setCharColor(JUtility::TColor(0, 0, 0, static_cast<u8>(color.a * 0.8f)));
    font->drawString_scale(x + shadow, y + shadow, cell, cell, text.c_str(), true);
    font->setCharColor(color);
    font->drawString_scale(x, y, cell, cell, text.c_str(), true);
}

}

void chat_register_vars() {
    ConfigVarDesc show = CONFIG_VAR_DESC_INIT;
    show.name = "chat_show";
    show.type = CONFIG_VAR_BOOL;
    show.default_bool = true;
    if (svc_config->register_var(mod_ctx, &show, &s_showVar) != MOD_OK) s_showVar = 0;
    ConfigVarDesc size = CONFIG_VAR_DESC_INIT;
    size.name = "chat_size";
    size.type = CONFIG_VAR_INT;
    size.default_int = 100;
    if (svc_config->register_var(mod_ctx, &size, &s_sizeVar) != MOD_OK) s_sizeVar = 0;
    ConfigVarDesc fade = CONFIG_VAR_DESC_INIT;
    fade.name = "chat_fade_seconds";
    fade.type = CONFIG_VAR_INT;
    fade.default_int = 10;
    if (svc_config->register_var(mod_ctx, &fade, &s_fadeVar) != MOD_OK) s_fadeVar = 0;
    const bool hooked = mods::hook::add_post<ChatPadReadHook>(on_pad_read_post) == MOD_OK;
    coop_log::info("coop_mod: [CHAT] pad hook {}", hooked ? "attached" : "missing - typing moves Link");
}

ConfigVarHandle chat_show_var() { return s_showVar; }
ConfigVarHandle chat_size_var() { return s_sizeVar; }
ConfigVarHandle chat_fade_var() { return s_fadeVar; }

void chat_on_message(uint32_t id, const std::string& tag, const std::string& name, const std::string& text) {
    if (global_blocked(tag)) return;
    push(Line{id, tag, name, text, Clock::now()});
}

std::vector<GlobalPlayer> chat_recent_speakers() {
    std::vector<GlobalPlayer> out;
    for (auto it = s_lines.rbegin(); it != s_lines.rend(); ++it) {
        if (it->name.empty() || it->tag.empty() || it->id == global_my_id()) continue;
        bool listed = false;
        for (const GlobalPlayer& p : out) listed = listed || p.tag == it->tag;
        if (!listed) out.push_back(GlobalPlayer{it->id, it->name, it->tag});
    }
    return out;
}

void chat_forget_player(const std::string& tag) {
    s_lines.erase(std::remove_if(s_lines.begin(), s_lines.end(),
                      [&](const Line& l) { return !l.name.empty() && l.tag == tag; }),
        s_lines.end());
}

void chat_on_rename(uint32_t id, const std::string& name) {
    if (id == 0 || name.empty()) return;
    for (Line& line : s_lines) {
        if (line.id == id && !line.name.empty()) line.name = name;
    }
    for (GlobalPlayer& t : s_blockTargets) {
        if (t.id == id) t.name = name;
    }
}

void chat_note(const std::string& text) {
    push(Line{0, "", "", text, Clock::now()});
}

void chat_open() {
    if (!global_active()) {
        features_toast("Chat", "Turn on Hyrule Online first.");
        return;
    }
    open_window();
}

void chat_update() {
    if (s_closeWindow) {
        s_closeWindow = false;
        if (s_window != 0) svc_ui->window_close(mod_ctx, s_window);
    }
    if (!global_active() || daAlink_getAlinkActorClass() == nullptr) {
        if (s_typing) stop_typing();
        return;
    }
    bool menuUp = false;
    if (svc_ui != nullptr && svc_ui->is_any_document_visible != nullptr) {
        svc_ui->is_any_document_visible(mod_ctx, &menuUp);
    }
    if (s_typing) {
        if (menuUp) {
            stop_typing();
            return;
        }
        type_update();
        return;
    }
    if (chat_keys_supported() && chat_key_pressed() && !menuUp && s_window == 0) start_typing();
}

const char* chat_how_text() {
#if defined(_WIN32)
    return "Press T in game. On a controller, press Select and pick Chat.";
#elif defined(__linux__) && !defined(__ANDROID__)
    const char* deck = std::getenv("SteamDeck");
    if (deck != nullptr && std::strcmp(deck, "1") == 0) {
        return "Press View and pick Chat. With a keyboard, press T in game.";
    }
    return "Press T in game. On a controller, press Select and pick Chat.";
#elif defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
    return "Tap the screen with three fingers, then tap Chat.";
#else
    return "Press Esc, or Select on a controller, and pick Chat.";
#endif
}

void chat_first_online() {
    s_lines.erase(std::remove_if(s_lines.begin(), s_lines.end(), [](const Line& l) { return !l.name.empty(); }),
        s_lines.end());
    static bool s_told = false;
    if (s_told) return;
    s_told = true;
features_toast("Hyrule Online", chat_how_text());
}

void chat_draw() {
    if (!global_active() || !cfg_bool(s_showVar, true)) return;
    if (s_lines.empty() && !s_typing) return;
    JUTFont* font = mDoExt_getMesgFont();
    if (font == nullptr) return;
    const f32 minX = mDoGph_gInf_c::getMinXF();
    const f32 minY = mDoGph_gInf_c::getMinYF();
    const f32 width = mDoGph_gInf_c::getWidthF();
    const f32 height = mDoGph_gInf_c::getHeightF();
    J2DOrthoGraph ortho(0.0f, 0.0f, static_cast<f32>(FB_WIDTH), static_cast<f32>(FB_HEIGHT), -1.0f, 1.0f);
    ortho.setOrtho(minX, minY, width, height, -1.0f, 1.0f);
    ortho.setPort();

    const f32 scale = static_cast<f32>(std::clamp<int64_t>(cfg_int(s_sizeVar, 100), 50, 200)) / 100.0f;
    const f32 cell = 13.0f * scale;
    const f32 pad = 6.0f * scale;
    const f32 stripe = 3.0f * scale;
    const Clock::time_point now = Clock::now();

    if (s_typing) {
        const f32 panelW = std::min(width * 0.46f, 440.0f * scale);
        const f32 x0 = minX + (width - panelW) * 0.5f;
        const f32 textX = x0 + stripe + pad;
        const f32 textW = panelW - stripe - pad * 2.0f;
        const f32 bottom = minY + height - 18.0f;
        const f32 boxH = cell + pad * 2.0f;
        const f32 top = bottom - boxH;
        ortho.setColor(JUtility::TColor(12, 14, 20, 225));
        ortho.fillBox(JGeometry::TBox2<f32>(x0, top, x0 + panelW, bottom));
        ortho.setColor(JUtility::TColor(255, 214, 120, 255));
        ortho.fillBox(JGeometry::TBox2<f32>(x0, top, x0 + stripe, bottom));
        font->setGX();
        const bool caretOn = (std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now.time_since_epoch()).count() / 500) % 2 == 0;

        std::string shown = s_typed;
        while (!shown.empty() && coop_text_width(font, (shown + "_").c_str(), cell) > textW) {
            shown.erase(shown.begin());
        }
        const f32 ty = top + pad + cell * 0.85f;
        if (s_typed.empty()) {
            shadowed(font, textX, ty, cell, "Say something...  (Enter to send, Esc to close)",
                JUtility::TColor(150, 156, 168, 255));
        } else {
            shadowed(font, textX, ty, cell, shown + (caretOn ? "_" : ""),
                JUtility::TColor(255, 255, 255, 255));
        }
        ortho.setPort();
    }

    const f32 lineGap = cell * 1.25f;
    const f32 above = cell * 0.9f;
    const f32 below = cell * 0.3f;
    const f32 lineW = std::min(width * 0.36f, 300.0f * scale);
    struct Corner {
        f32 left, bottom, highest;
    };
    static Corner s_corner = {0.0f, 0.0f, 0.0f};
    static bool s_cornerKnown = false;
    f32 mapTop = 0.0f, crossTop = 0.0f;
    f32 heartsBottom = minY + height * 0.25f;
    const bool map = map_markers_minimap_top(&mapTop);
    const bool cross = squad_hud_corner(&crossTop, &heartsBottom);
    if (map || cross || !s_cornerKnown) {
        Corner c;
        c.left = minX + 14.0f;
        c.highest = heartsBottom + 4.0f + above;
        f32 floor = minY + height - 30.0f;
        if (map) floor = std::min(floor, mapTop);
        if (cross && crossTop > heartsBottom) floor = std::min(floor, crossTop);
        c.bottom = floor - 6.0f - below;

        c.highest = std::min(c.highest, c.bottom);
        s_corner = c;
        s_cornerKnown = map || cross;
    }
    const f32 left = s_corner.left;
    const f32 highest = s_corner.highest;
    f32 y = s_corner.bottom;
    const float fadeSecs = static_cast<float>(std::clamp<int64_t>(cfg_int(s_fadeVar, 10), 2, 120));
    const int most = s_typing ? 10 : 5;
    int shown = 0;
    font->setGX();
    for (auto it = s_lines.rbegin(); it != s_lines.rend() && shown < most; ++it) {
        float alpha = 1.0f;
        if (!s_typing) {
            const float age = std::chrono::duration<float>(now - it->at).count();
            if (age > fadeSecs + 1.0f) continue;
            if (age > fadeSecs) alpha = 1.0f - (age - fadeSecs);
        }
        const u8 a = static_cast<u8>(255.0f * std::clamp(alpha, 0.0f, 1.0f));
        const std::string label = it->name.empty() ? "" : it->name + ": ";
        const std::vector<std::string> rows = wrap(font, label + it->text, cell, lineW);
        for (auto row = rows.rbegin(); row != rows.rend() && y >= highest; ++row) {
            const bool first = row + 1 == rows.rend();
            if (first && !label.empty() && row->rfind(label, 0) == 0) {
                shadowed(font, left, y, cell, label, JUtility::TColor(255, 214, 120, a));
                const f32 nameW = coop_text_width(font, label.c_str(), cell);
                shadowed(font, left + nameW, y, cell, row->substr(label.size()), JUtility::TColor(255, 255, 255, a));
            } else {
                shadowed(font, left, y, cell, *row,
                    it->name.empty() ? JUtility::TColor(190, 190, 190, a) : JUtility::TColor(255, 255, 255, a));
            }
            y -= lineGap;
        }
        ++shown;
    }
}

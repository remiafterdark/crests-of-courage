

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/log.hpp"
#include "print.hpp"
#include "mods/svc/ui.h"

#include "d/d_com_inf_game.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

struct SurfaceHandles {
    UiElementHandle status = 0;
    UiElementHandle peer = 0;
    UiListHandle players = 0;
    std::string lastStatus;
    std::string lastPeer;
    std::string lastList;

    std::string lastBackup;
    bool listPushed = false;

    std::vector<uint8_t> rowIds;
    std::vector<std::string> rowLabels;
};

SurfaceHandles s_panel;
SurfaceHandles s_window;
UiWindowHandle s_windowHandle = 0;

void open_window();

bool net_active(ModContext*, void*) {
    return coop_net_connected() || coop_net_connecting();
}

bool net_idle(ModContext*, void*) {
    return !(coop_net_connected() || coop_net_connecting());
}

void add_control(UiElementHandle pane, const UiControlDesc& desc) {
    if (svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr) != MOD_OK) {
        coop_log::warn("coop_mod: [UI] could not add '{}'", desc.label != nullptr ? desc.label : "?");
    }
}

void add_button(UiElementHandle pane, const char* label, UiPressedFn onPressed,
    UiPredicateFn isDisabled = nullptr) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_BUTTON;
    desc.label = label;
    desc.on_pressed = onPressed;
    desc.is_disabled = isDisabled;
    add_control(pane, desc);
}

void add_toggle(UiElementHandle pane, const char* label, ConfigVarHandle var) {
    if (var == 0) return;
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_TOGGLE;
    desc.label = label;
    desc.binding = UI_BINDING_CONFIG_VAR;
    desc.config_var = var;
    add_control(pane, desc);
}

void add_string(UiElementHandle pane, const char* label, ConfigVarHandle var, int32_t maxLength) {
    if (var == 0) return;
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_STRING;
    desc.label = label;
    desc.binding = UI_BINDING_CONFIG_VAR;
    desc.config_var = var;
    desc.max_length = maxLength;
    add_control(pane, desc);
}

void add_number(UiElementHandle pane, const char* label, ConfigVarHandle var, int64_t min,
    int64_t max, int64_t step, const char* suffix) {
    if (var == 0) return;
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_NUMBER;
    desc.label = label;
    desc.binding = UI_BINDING_CONFIG_VAR;
    desc.config_var = var;
    desc.min = min;
    desc.max = max;
    desc.step = step;
    desc.suffix = suffix;
    add_control(pane, desc);
}

const char* const kColorPresets[] = {
    "ab706e", "6382a0", "94749a", "ec8644", "b9ab00", "ec9fc8", "505154", "f8f7f4", "91723e",
};

void add_color(UiElementHandle pane, const char* label, ConfigVarHandle var) {
    if (var == 0) return;
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_COLOR;
    desc.label = label;
    desc.binding = UI_BINDING_CONFIG_VAR;
    desc.config_var = var;
    desc.color_presets = kColorPresets;
    desc.color_preset_count = sizeof(kColorPresets) / sizeof(kColorPresets[0]);
    add_control(pane, desc);
}

std::string status_text() {
    return coop_net_status();
}

std::string ping_text(uint8_t id) {
    const int32_t ms = coop_net_ping_ms(id);
    if (ms < 0) return "";
    return "  -  " + std::to_string(ms) + " ms";
}

std::string health_text(const CoopPeer& peer) {
    if (!peer.lifeKnown) return "";
    const uint32_t quarters = peer.life;
    const uint32_t maxHearts = peer.maxLife / 5u;
    std::string out = "  -  " + std::to_string(quarters / 4);
    static const char* const kFraction[4] = {"", ".25", ".5", ".75"};
    out += kFraction[quarters % 4];
    out += "/" + std::to_string(maxHearts) + " hearts";
    return out;
}

std::string where_text(const CoopPeer& peer) {
    const std::string stage(peer.stage);
    if (stage.empty()) return "?";
    const char* mine = dComIfGp_getStartStageName();
    if (mine != nullptr && std::strncmp(mine, peer.stage, 8) == 0) return "nearby";
    return stage;
}

std::string peer_text() {
    if (!coop_net_connected()) return "Not connected.";
    std::string out;
    int seen = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || !coop_net_player_present(id)) continue;
        const CoopPeer& p = features_peer_of(id);
        ++seen;
        if (!p.present) {
            out += "Player " + std::to_string(id) + " is joining. ";
        } else if (!p.inGame) {
            out += p.name + " is loading. ";
        } else {
            continue;
        }
    }
    if (seen == 0) return "Connected. Waiting for players.";
    return out;
}

void push_players(SurfaceHandles& h) {
    if (h.players == 0) return;

    std::string joined;
    h.rowIds.clear();
    h.rowLabels.clear();
    if (coop_net_connected()) {
        for (int i = 0; i < kCoopMaxPlayers; ++i) {
            const uint8_t id = static_cast<uint8_t>(i);
            if (id == coop_net_local_id() || !coop_net_player_present(id)) continue;
            const CoopPeer& p = features_peer_of(id);
            if (!p.present || !p.inGame) continue;
            h.rowIds.push_back(id);
            h.rowLabels.push_back(p.name + (coop_player_paused(id) ? " (paused)" : "") + "  -  " +
                                  where_text(p) + health_text(p) + ping_text(id));
            joined += h.rowLabels.back() + " ";
        }
    }
    if (h.listPushed && joined == h.lastList) return;
    h.lastList = joined;
    h.listPushed = true;
    if (h.rowIds.empty()) {
        svc_ui->list_set_items(mod_ctx, h.players, nullptr, 0);
        return;
    }
    std::vector<UiListItem> items;
    items.reserve(h.rowIds.size());
    for (size_t i = 0; i < h.rowIds.size(); ++i) {
        UiListItem item = UI_LIST_ITEM_INIT;
        item.key = h.rowIds[i];
        item.label = h.rowLabels[i].c_str();
        items.push_back(item);
    }
    svc_ui->list_set_items(mod_ctx, h.players, items.data(), items.size());
}

void refresh(SurfaceHandles& h) {
    if (h.status != 0) {
        const std::string text = status_text();
        if (text != h.lastStatus) {
            h.lastStatus = text;
            svc_ui->elem_set_text(mod_ctx, h.status, h.lastStatus.c_str());
        }
    }
    if (h.peer != 0) {
        const std::string text = peer_text();
        if (text != h.lastPeer) {
            h.lastPeer = text;
            svc_ui->elem_set_text(mod_ctx, h.peer, h.lastPeer.c_str());
        }
    }
    push_players(h);
}

void on_player_pressed(ModContext*, UiListHandle, uint64_t key, void*) {

    if (key < kCoopMaxPlayers) features_teleport_to_player(static_cast<uint8_t>(key));
}

void build_connect(UiElementHandle pane, SurfaceHandles& h) {
    const CoopFeatureVars& vars = features_vars();
    svc_ui->pane_add_section(mod_ctx, pane, "You");
    add_string(pane, "Name", vars.name, kCoopNameMax - 1);

    svc_ui->pane_add_section(mod_ctx, pane, "Connection");
    h.lastStatus = status_text();
    svc_ui->pane_add_text(mod_ctx, pane, h.lastStatus.c_str(), &h.status);
    add_string(pane, "Host address", coop_net_address_var(), 64);
    add_number(pane, "Port", coop_net_port_var(), 1024, 65535, 1, nullptr);
    add_button(pane, "Host", [](ModContext*, void*) { coop_net_host(); }, net_active);
    add_button(pane, "Join", [](ModContext*, void*) { coop_net_join(); }, net_active);
    add_button(pane, "Disconnect", [](ModContext*, void*) { coop_net_disconnect(); }, net_idle);
    add_toggle(pane, "Connect on launch", coop_net_autoconnect_var());

    svc_ui->pane_add_section(mod_ctx, pane, "Your save");
    svc_ui->pane_add_text(mod_ctx, pane,
        "Joining uses the host's progress. Yours is backed up first.", nullptr);
    add_button(pane, "Restore latest backup",
        [](ModContext*, void*) { joinsync_restore_backup(false); }, net_active);
    add_button(pane, "Restore oldest backup",
        [](ModContext*, void*) { joinsync_restore_backup(true); }, net_active);

    h.lastBackup = joinsync_backup_summary();
    svc_ui->pane_add_text(mod_ctx, pane, h.lastBackup.c_str(), nullptr);

    static const std::string kVersionLine =
        "Version " + std::to_string(kCoopProtocolVersion) + " - everyone needs the same one.";
    svc_ui->pane_add_text(mod_ctx, pane, kVersionLine.c_str(), nullptr);
}

void build_players(UiElementHandle pane, SurfaceHandles& h) {
    svc_ui->pane_add_section(mod_ctx, pane, "Players");
    h.lastPeer = peer_text();
    svc_ui->pane_add_text(mod_ctx, pane, h.lastPeer.c_str(), &h.peer);

    UiListDesc list = UI_LIST_DESC_INIT;
    list.on_pressed = on_player_pressed;
    if (svc_ui->pane_add_list(mod_ctx, pane, &list, &h.players) != MOD_OK) {
        h.players = 0;
    }
    h.listPushed = false;
    push_players(h);
}

void build_settings(UiElementHandle pane) {
    const CoopFeatureVars& vars = features_vars();
    svc_ui->pane_add_section(mod_ctx, pane, "Sharing");
    if (coop_session_from_host()) {
        svc_ui->pane_add_text(mod_ctx, pane,
            "Connected - the host's settings are used for sharing, enemies and bosses.", nullptr);
    }
    add_toggle(pane, "Share items", vars.syncInventory);
    add_toggle(pane, "Share dungeon progress", world_dungeon_var());
    add_toggle(pane, "Share story progress", world_story_var());
    add_toggle(pane, "Sync world objects", enemies_breakables_var());
    add_toggle(pane, "Sync time of day", vars.syncTime);
    add_toggle(pane, "Death link", vars.deathLink);

    svc_ui->pane_add_section(mod_ctx, pane, "Sound");
    add_toggle(pane, "Hear other players", vars.syncSounds);
    add_number(pane, "Their volume", vars.soundVolume, 0, 100, 5, "%");

    svc_ui->pane_add_section(mod_ctx, pane, "Display");
    add_toggle(pane, "Name tags", vars.nametags);
    add_toggle(pane, "Off-screen name tags", vars.nametagsEdge);
    add_toggle(pane, "Show their health", vars.nametagsHealth);
    add_toggle(pane, "Hide far name tags", vars.nametagsHideFar);
    add_toggle(pane, "Party hearts", squad_hud_enabled_var());
    add_number(pane, "Party hearts size", squad_hud_size_var(), 20, 100, 5, "%");
    add_number(pane, "Party hearts offset", squad_hud_offset_var(), 0, 200, 2, nullptr);
    add_toggle(pane, "Item pickup messages", vars.notifyItems);
    add_toggle(pane, "Show their Midna", vars.puppetMidna);
    add_toggle(pane, "Their lantern light", vars.puppetLanternLight);

    svc_ui->pane_add_section(mod_ctx, pane, "Experimental");
    add_toggle(pane, "Enemy sync", enemies_enabled_var());
    add_toggle(pane, "Boss sync", boss_enabled_var());
    add_toggle(pane, "Wait for everyone at boss doors", boss_wait_var());
    svc_ui->pane_add_text(mod_ctx, pane,
        "Only Ook and Diababa are synced so far - other bosses are left alone. "
        "Boss sync can softlock a fight. If that happens, turn it off and re-enter the room.",
        nullptr);

    if (features_debug_menu()) {
        svc_ui->pane_add_section(mod_ctx, pane, "Dev");
        add_toggle(pane, "Enemy decisions", enemies_decisions_var());
        add_toggle(pane, "Pushed objects", enemies_movers_var());
        add_toggle(pane, "Epona sync", horse_enabled_var());
        add_toggle(pane, "Warp effect on others (can crash)", vars.puppetWarpFx);
        add_number(pane, "Fake party members", squad_hud_fake_var(), 0, 4, 1, nullptr);
    }
}

void build_colors(UiElementHandle pane) {
    std::string lastGroup;
    for (int i = 0; i < colors_slot_count(); ++i) {
        const std::string group = colors_slot_group(i);
        if (group != lastGroup) {
            svc_ui->pane_add_section(mod_ctx, pane, group.c_str());
            lastGroup = group;
        }
        add_color(pane, colors_slot_label(i), colors_slot_var(i));
    }
    add_button(pane, "Reset colors", [](ModContext*, void*) { colors_reset_mine(); });
}

void build_debug(UiElementHandle pane) {
    svc_ui->pane_add_section(mod_ctx, pane, "Debug");
    add_button(pane, "Spawn test puppet", [](ModContext*, void*) { coop_debug_spawn_puppet(); });
    add_button(pane, "Transform", [](ModContext*, void*) { coop_debug_force_transform(); });
    add_button(pane, "Give Midna", [](ModContext*, void*) { coop_debug_give_midna(); });
}

ModResult build_panel(ModContext*, UiElementHandle pane, void*, ModError*) {
    s_panel = SurfaceHandles{};
    UiControlDesc open = UI_CONTROL_DESC_INIT;
    open.kind = UI_CONTROL_GROUP;
    open.label = "Open Co-op";
    open.on_pressed = [](ModContext*, void*) { open_window(); };
    add_control(pane, open);

    build_connect(pane, s_panel);
    build_players(pane, s_panel);
    build_settings(pane);
    build_colors(pane);
    if (features_debug_menu()) build_debug(pane);
    return MOD_OK;
}

ModResult update_panel(ModContext*, void*, ModError*) {
    refresh(s_panel);
    return MOD_OK;
}

ModResult tab_connect(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_connect(left, s_window);
    svc_ui->pane_add_text(mod_ctx, right,
        "One player hosts, everyone else joins with the host's address. "
        "The host needs the port open (TCP and UDP), or everyone can use Tailscale "
        "and join with the host's Tailscale address.",
        nullptr);
    return MOD_OK;
}

ModResult tab_players(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_players(left, s_window);
    svc_ui->pane_add_text(mod_ctx, right, "Pick a player to teleport to them.", nullptr);
    return MOD_OK;
}

ModResult tab_settings(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_settings(left);
    svc_ui->pane_add_text(mod_ctx, right,
        "Shared items don't include rupees or ammo. Small keys are shared - "
        "if one of you uses one, it's gone for both.",
        nullptr);
    return MOD_OK;
}

ModResult tab_colors(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_colors(left);
    svc_ui->pane_add_text(mod_ctx, right, "Your colors. Other players see you in these.", nullptr);
    return MOD_OK;
}

ModResult tab_debug(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_debug(left);
    return MOD_OK;
}

ModResult update_window(ModContext*, void*, ModError*) {
    refresh(s_window);
    return MOD_OK;
}

void on_window_closed(ModContext*, UiWindowHandle, void*) {
    s_windowHandle = 0;
    s_window = SurfaceHandles{};
}

void open_window() {
    if (s_windowHandle != 0) return;

    const size_t tabCount = features_debug_menu() ? 5 : 4;
    UiTabDesc tabs[5] = {
        UI_TAB_DESC_INIT, UI_TAB_DESC_INIT, UI_TAB_DESC_INIT, UI_TAB_DESC_INIT, UI_TAB_DESC_INIT};
    tabs[0].title = "Connect";
    tabs[0].build = tab_connect;
    tabs[0].update = update_window;
    tabs[1].title = "Players";
    tabs[1].build = tab_players;
    tabs[1].update = update_window;
    tabs[2].title = "Settings";
    tabs[2].build = tab_settings;
    tabs[2].update = update_window;
    tabs[3].title = "Colors";
    tabs[3].build = tab_colors;
    tabs[3].update = update_window;
    tabs[4].title = "Debug";
    tabs[4].build = tab_debug;
    tabs[4].update = update_window;

    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = tabCount;
    desc.on_closed = on_window_closed;
    if (svc_ui->window_push(mod_ctx, &desc, &s_windowHandle) != MOD_OK) {
        s_windowHandle = 0;
        coop_log::warn("coop_mod: [UI] failed to open the co-op window");
    }
}

}

void ui_init() {
    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_panel;
    panel.update = update_panel;
    if (svc_ui->register_mods_panel(mod_ctx, &panel) != MOD_OK) {
        coop_log::warn("coop_mod: [UI] failed to register the mods panel");
    }

    UiMenuTabDesc tab = UI_MENU_TAB_DESC_INIT;
    tab.label = "Co-op";
    tab.on_selected = [](ModContext*, void*) { open_window(); };
    UiMenuTabHandle handle = 0;
    if (svc_ui->register_menu_tab(mod_ctx, &tab, &handle) != MOD_OK) {
        coop_log::warn("coop_mod: [UI] failed to add the Co-op menu bar tab");
    }
}

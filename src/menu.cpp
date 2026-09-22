

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
    UiElementHandle models = 0;
    UiElementHandle party = 0;
    UiElementHandle invite = 0;
    UiListHandle players = 0;
    std::string lastStatus;
    std::string lastPeer;
    std::string lastModels;
    std::string lastParty;
    std::string lastInvite;
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

std::string worn_text();
std::string party_text();
std::string invite_text();
std::string escape_rml(const std::string& text);

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
    UiPredicateFn isDisabled = nullptr, const char* help = nullptr) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_BUTTON;
    desc.label = label;
    desc.on_pressed = onPressed;
    desc.is_disabled = isDisabled;
    desc.help_rml = help;
    add_control(pane, desc);
}

void add_choice(UiElementHandle pane, const char* label, UiPressedFn onPressed, void* userData,
    UiPredicateFn isSelected, const char* help = nullptr) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_BUTTON;
    desc.label = label;
    desc.on_pressed = onPressed;
    desc.user_data = userData;
    desc.is_selected = isSelected;
    desc.help_rml = help;
    add_control(pane, desc);
}

bool depends_on(ModContext*, void* data) {
    const ConfigVarHandle var = static_cast<ConfigVarHandle>(reinterpret_cast<uintptr_t>(data));
    return !cfg_bool(var, false);
}

void* as_data(ConfigVarHandle var) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(var));
}

void add_toggle(UiElementHandle pane, const char* label, ConfigVarHandle var,
    const char* help = nullptr, ConfigVarHandle needs = 0) {
    if (var == 0) return;
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_TOGGLE;
    desc.label = label;
    desc.binding = UI_BINDING_CONFIG_VAR;
    desc.config_var = var;
    desc.help_rml = help;
    if (needs != 0) {
        desc.is_disabled = depends_on;
        desc.user_data = as_data(needs);
    }
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
    int64_t max, int64_t step, const char* suffix, const char* help = nullptr,
    ConfigVarHandle needs = 0) {
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
    if (needs != 0) {
        desc.is_disabled = depends_on;
        desc.user_data = as_data(needs);
    }
    desc.help_rml = help;
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
    return ", " + std::to_string(ms) + " ms";
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

std::string party_text() {
    if (!coop_net_connected()) return "Not connected.\n\nHost or join a game from the Connect tab.";
    std::string out;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || !coop_net_player_present(id)) continue;
        const CoopPeer& p = features_peer_of(id);
        if (!out.empty()) out += "\n\n";
        if (!p.present) {
            out += "Player " + std::to_string(id) + "\njoining";
            continue;
        }
        out += p.name + "\n";
        if (!p.inGame) {
            out += "loading";
            continue;
        }
        out += where_text(p);
        const std::string ping = ping_text(id);
        if (!ping.empty()) out += ping;
        if (coop_player_paused(id)) out += ", paused";
    }
    if (out.empty()) return "Connected.\n\nWaiting for somebody to join.";
    return out;
}

std::string invite_text() {
    if (!coop_net_is_host() || !(coop_net_connected() || coop_net_connecting())) return "";

    const std::string code = online_room_code();
    const std::string online = online_status();
    const std::string address = upnp_external_address();
    if (!code.empty()) {
        std::string out = "Room code: " + code;
        if (!online.empty()) out += ". " + online;
        return out;
    }
    if (!online.empty()) return online;
    if (!address.empty()) return "Others can join you at " + address;
    return upnp_status();
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
            h.rowLabels.push_back(p.name + (coop_player_paused(id) ? " (paused)" : "") + ", " +
                                  where_text(p) + ping_text(id));
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
    if (h.models != 0) {
        const std::string text = worn_text();
        if (text != h.lastModels) {
            h.lastModels = text;
            svc_ui->elem_set_text(mod_ctx, h.models, h.lastModels.c_str());
        }
    }
    if (h.invite != 0) {
        const std::string text = invite_text();
        if (text != h.lastInvite) {
            h.lastInvite = text;
            svc_ui->elem_set_text(mod_ctx, h.invite, h.lastInvite.c_str());
        }
    }
    if (h.party != 0) {
        const std::string text = party_text();
        if (text != h.lastParty) {
            h.lastParty = text;
            svc_ui->elem_set_text(mod_ctx, h.party, h.lastParty.c_str());
        }
    }
    push_players(h);
}

void on_player_pressed(ModContext*, UiListHandle, uint64_t key, void*) {

    if (key < kCoopMaxPlayers) features_teleport_to_player(static_cast<uint8_t>(key));
}

using GroupFn = ModResult (*)(ModContext*, UiElementHandle, void*, ModError*);

void add_panel_header(UiElementHandle pane, const char* title, const char* subtitle = nullptr,
    const char* tone = "") {
    std::string rml = "<div class=\"coop-head ";
    rml += tone;
    rml += "\">";
    rml += escape_rml(title);
    rml += "</div>";
    if (subtitle != nullptr && subtitle[0] != '\0') {
        rml += "<div class=\"coop-sub\">";
        rml += escape_rml(subtitle);
        rml += "</div>";
    }
    svc_ui->pane_add_rml(mod_ctx, pane, rml.c_str(), nullptr);
}

void add_group_or_section(UiElementHandle left, UiElementHandle detail, const char* title,
    GroupFn build, void* data = nullptr) {
    if (detail != 0) {
        UiGroupDesc group = UI_GROUP_DESC_INIT;
        group.label = title;
        group.build = build;
        group.user_data = data;
        if (svc_ui->pane_add_group(mod_ctx, left, detail, &group, nullptr) == MOD_OK) return;
        coop_log::warn("coop_mod: [UI] could not add the '{}' group", title);
    }
    svc_ui->pane_add_section(mod_ctx, left, title);
    build(mod_ctx, left, data, nullptr);
}

void add_step(UiElementHandle pane, int number, const char* text) {

    std::string rml = "<div class=\"coop-step\"><span class=\"coop-step-n\">";
    rml += std::to_string(number);
    rml += "</span><span class=\"coop-step-t\">";
    rml += escape_rml(text);
    rml += "</span></div>";
    svc_ui->pane_add_rml(mod_ctx, pane, rml.c_str(), nullptr);
}

ModResult build_online_guide(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Playing together");
    svc_ui->pane_add_text(mod_ctx, pane,
        "Everyone types the same Room. One of you presses Host, the rest press Join.", nullptr);

    svc_ui->pane_add_section(mod_ctx, pane, "Same wifi");
    svc_ui->pane_add_text(mod_ctx, pane,
        "If the room won't connect, use Join by address in Advanced with the host's local "
        "address.", nullptr);

    svc_ui->pane_add_section(mod_ctx, pane, "If the code won't connect");
    svc_ui->pane_add_text(mod_ctx, pane,
        "Some networks are too strict. Use Tailscale instead. It's free and works everywhere.",
        nullptr);
    add_step(pane, 1, "Everyone installs Tailscale and signs in.");
    add_step(pane, 2, "The host invites the others at login.tailscale.com, under Users.");
    add_step(pane, 3, "The host presses Host.");
    add_step(pane, 4, "The others use Join by address in Advanced with the host's Tailscale "
                      "address. It starts with 100.");

    svc_ui->pane_add_section(mod_ctx, pane, "Firewall");
    svc_ui->pane_add_text(mod_ctx, pane,
        "If your device asks to let Dusklight through, allow it.", nullptr);
    return MOD_OK;
}

ModResult group_advanced(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Advanced");
    svc_ui->pane_add_section(mod_ctx, pane, "Join by address");
    add_string(pane, "Address", coop_net_address_var(), 64);
    add_number(pane, "Port", coop_net_join_port_var(), 1024, 65535, 1, nullptr);
    add_button(pane, "Join by address", [](ModContext*, void*) { coop_net_join(); }, net_active,
        "Same wifi, Tailscale, or a forwarded port.");

    svc_ui->pane_add_section(mod_ctx, pane, "Hosting");
    add_number(pane, "Port to listen on", coop_net_port_var(), 1024, 65535, 1, nullptr);
    add_toggle(pane, "Room codes", coop_net_rooms_var());
    add_toggle(pane, "Open the port for me", coop_net_upnp_var());
    add_toggle(pane, "Connect on launch", coop_net_autoconnect_var());

    svc_ui->pane_add_section(mod_ctx, pane, "Your save");
    add_button(pane, "Restore latest backup",
        [](ModContext*, void*) { joinsync_restore_backup(false); }, net_active);
    add_button(pane, "Restore oldest backup",
        [](ModContext*, void*) { joinsync_restore_backup(true); }, net_active);

    static std::string s_backup;
    s_backup = joinsync_backup_summary();
    svc_ui->pane_add_text(mod_ctx, pane, s_backup.c_str(), nullptr);

    static const std::string kVersionLine = "Version " + std::to_string(kCoopProtocolVersion);
    svc_ui->pane_add_text(mod_ctx, pane, kVersionLine.c_str(), nullptr);
    return MOD_OK;
}

void build_connect(UiElementHandle pane, SurfaceHandles& h, UiElementHandle detail) {
    const CoopFeatureVars& vars = features_vars();
    h.lastStatus = status_text();
    svc_ui->pane_add_text(mod_ctx, pane, h.lastStatus.c_str(), &h.status);
    h.lastInvite = invite_text();
    svc_ui->pane_add_text(mod_ctx, pane, h.lastInvite.c_str(), &h.invite);

    add_string(pane, "Name", vars.name, kCoopNameMax - 1);
    add_string(pane, "Room", coop_net_room_code_var(), static_cast<int32_t>(kRoomNameMax));
    add_button(pane, "Host", [](ModContext*, void*) { coop_net_host(); }, net_active,
        "Empty Room gets a random code.");
    add_button(pane, "Join", [](ModContext*, void*) { coop_net_join_code(); }, net_active);
    add_button(pane, "Disconnect", [](ModContext*, void*) { coop_net_disconnect(); }, net_idle);

    add_group_or_section(pane, detail, "Advanced", group_advanced);
    add_group_or_section(pane, detail, "How to play together", build_online_guide);
}

void build_players(UiElementHandle pane, SurfaceHandles& h) {

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

ModResult group_sharing(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Share");
    const CoopFeatureVars& vars = features_vars();
    add_toggle(pane, "Items", vars.syncInventory,
        "Not rupees or ammo. Small keys are shared: one of you spends it, it is gone for all.");
    add_toggle(pane, "Dungeon progress", world_dungeon_var(),
        "Chests, switches and doors. Open one and it is open for everybody.");
    add_toggle(pane, "World objects", enemies_breakables_var(),
        "Pots, grass, blocks and the like.");
    add_toggle(pane, "Time of day", vars.syncTime,
        "One clock for everybody, so it is not day for you and night for them.");
    return MOD_OK;
}

ModResult group_together(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Playing together");
    add_toggle(pane, "Death link", features_vars().deathLink,
        "One of you dies, everybody dies.");
    return MOD_OK;
}

ModResult group_unfinished(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Unfinished", nullptr, "coop-danger");

    add_toggle(pane, "Story progress", world_story_var(),
        "Cutscenes and quest flags are shared. Can break a quest.");
    add_toggle(pane, "Enemy sync", enemies_enabled_var(),
        "Enemies fight all of you: one set shared between you, instead of a copy each. Can leave an "
        "enemy alive on one screen and dead on the other.");
    add_toggle(pane, "Boss sync", boss_enabled_var(),
        "Ook and Diababa only; other bosses are untouched. CAN SOFTLOCK THE FIGHT. If it does, "
        "turn this off and re-enter the room.");
    add_toggle(pane, "Hold boss fights until everyone's there", boss_wait_var(),
        "The fight waits at the door until the whole party has walked in. Can leave you stuck at "
        "the door if someone never arrives.");
    return MOD_OK;
}

ModResult group_dev(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Dev");
    add_toggle(pane, "Enemy decisions", enemies_decisions_var());
    add_toggle(pane, "Pushed objects", enemies_movers_var());
    add_toggle(pane, "Warp effect on others (can crash)", features_vars().puppetWarpFx);
    add_toggle(pane, "Log puppet materials on build", puppet_warp_dump_var(),
        "Dumps every material of each model as a puppet is built. Noisy, and it reads engine "
        "structures directly. Only turn it on to chase a model bug.");
    return MOD_OK;
}

void build_game(UiElementHandle pane, UiElementHandle detail) {
    if (coop_session_from_host()) {
        svc_ui->pane_add_text(mod_ctx, pane,
            "You are connected, so the host's settings are the ones in use.", nullptr);
    }
    add_group_or_section(pane, detail, "Share", group_sharing);
    add_group_or_section(pane, detail, "Playing together", group_together);
    add_group_or_section(pane, detail, "Unfinished: these can break your game", group_unfinished);
    if (features_debug_menu()) add_group_or_section(pane, detail, "Dev", group_dev);
}

ModResult group_nametags(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Name tags");
    const CoopFeatureVars& vars = features_vars();
    add_toggle(pane, "Show their name", vars.nametags,
        "Above them, smaller the further away they are.");
    add_toggle(pane, "Track them off screen", vars.nametagsEdge,
        "An arrow at the edge of the screen pointing at players you cannot see.", vars.nametags);
    add_toggle(pane, "Hide when far away", vars.nametagsHideFar,
        "Tags disappear past about a field's width instead of following them forever.",
        vars.nametags);
    return MOD_OK;
}

ModResult group_health(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Health");

    svc_ui->pane_add_section(mod_ctx, pane, "Player");
    const ConfigVarHandle world = features_vars().nametagsHealth;
    add_toggle(pane, "Show hearts under them", world,
        "Real hearts at their feet, sized like their name tag.");
    add_toggle(pane, "Only when they get hurt", squad_hud_hurt_only_var(),
        "Hidden until they take damage, then shown for a while.", world);
    add_number(pane, "Size", squad_hud_world_size_var(), 20, 150, 5, "%",
        "Against the size of their name tag, so they still shrink with distance.", world);
    add_number(pane, "Stay up for", squad_hud_hurt_seconds_var(), 1, 30, 1, "s",
        "How long after a hit the hearts remain.", squad_hud_hurt_only_var());
    add_number(pane, "Fade out over", squad_hud_hurt_fade_var(), 0, 50, 1, "/10s",
        "The tail end of that time is spent fading. Zero cuts them off instead.",
        squad_hud_hurt_only_var());

    svc_ui->pane_add_section(mod_ctx, pane, "HUD");
    add_toggle(pane, "Show players on HUD", squad_hud_enabled_var(),
        "Everybody's hearts under your own. Past three players the rest become one line each.");
    add_number(pane, "Size", squad_hud_size_var(), 20, 100, 5, "%",
        "How big theirs are next to your own hearts.", squad_hud_enabled_var());
    add_number(pane, "Distance below yours", squad_hud_offset_var(), 0, 200, 2, nullptr,
        "Nudge the party down if it crowds your own hearts.", squad_hud_enabled_var());
    return MOD_OK;
}

ModResult group_sound(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Sound");
    const CoopFeatureVars& vars = features_vars();
    add_toggle(pane, "Hear other players", vars.syncSounds,
        "Their sword swings, footsteps and voice, from where they are standing.");
    add_number(pane, "Their volume", vars.soundVolume, 0, 100, 5, "%",
        "Quieter than your own by default, so the room does not double up.", vars.syncSounds);
    return MOD_OK;
}

ModResult group_messages(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Messages");
    add_toggle(pane, "Say what they pick up", features_vars().notifyItems,
        "A note when somebody finds something that matters: a key, an item, a heart piece.");
    return MOD_OK;
}

void build_screen(UiElementHandle pane, UiElementHandle detail) {
    add_group_or_section(pane, detail, "Name tags", group_nametags);
    add_group_or_section(pane, detail, "Health", group_health);
    add_group_or_section(pane, detail, "Sound", group_sound);
    add_group_or_section(pane, detail, "Messages", group_messages);
}

std::vector<std::string> s_modelNames;
std::vector<std::string> s_modelTitles;
std::vector<std::string> s_modelAbout;

std::string escape_rml(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

void add_rml_list(std::string& out, const char* heading, const std::vector<std::string>& items,
    const char* itemClass) {
    if (items.empty()) return;
    out += "<div class=\"coop-listhead\">";
    out += escape_rml(heading);
    out += "</div>";
    for (const std::string& item : items) {
        out += "<div class=\"";
        out += itemClass;
        out += "\">";
        out += escape_rml(item);
        out += "</div>";
    }
}

std::string model_help_rml(int index) {
    std::string out;
    const std::string credit = skins_author_text(index);
    if (!credit.empty()) {
        out += "<div class=\"coop-credit\">" + escape_rml(credit) + "</div>";
    }
    const std::string own = skins_own_words(index);
    if (!own.empty()) {
        out += "<div class=\"coop-note\">" + escape_rml(own) + "</div>";
    }
    std::vector<std::string> outfits;
    skins_outfit_list(index, outfits);
    add_rml_list(out, "Outfits", outfits, "coop-item");

    std::vector<std::string> has;
    std::vector<std::string> missing;
    skins_equipment_list(index, has, missing);
    add_rml_list(out, "Equipment", has, "coop-item");

    add_rml_list(out, "Not included", missing, "coop-item coop-missing");
    return out;
}

std::vector<std::string> s_slotOptions[kSkinChoiceCount];
std::vector<const char*> s_slotOptionPtrs[kSkinChoiceCount];
std::vector<std::string> s_slotModelNames[kSkinChoiceCount];
std::string s_slotHelp[kSkinChoiceCount];

void* pack_model(int index) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(index));
}
int model_of(void* data) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(data));
    return index >= 0 && index < static_cast<int>(s_modelNames.size()) ? index : -1;
}

std::string worn_text() {
    if (skins_all_same("")) return "Wearing Link's own model.";
    for (int i = 0; i < skins_count(); ++i) {
        if (skins_all_same(skins_name(i))) return "Wearing " + skins_title(i) + ".";
    }
    return "Wearing a mix of models.";
}

bool add_slot_dropdown(UiElementHandle pane, int slot) {
    s_slotOptions[slot].clear();
    s_slotOptionPtrs[slot].clear();
    s_slotModelNames[slot].clear();

    s_slotOptions[slot].push_back(slot >= kSkinChoiceFirstItem ? "Same as Equipment" : "Default");
    for (size_t i = 0; i < s_modelNames.size(); ++i) {
        if (!skins_covers_slot(s_modelNames[i].c_str(), slot)) continue;
        s_slotOptions[slot].push_back(s_modelTitles[i]);
        s_slotModelNames[slot].push_back(s_modelNames[i]);
    }
    for (const std::string& option : s_slotOptions[slot]) {
        s_slotOptionPtrs[slot].push_back(option.c_str());
    }
    if (s_slotModelNames[slot].empty()) return false;

    UiControlDesc desc = UI_CONTROL_DESC_INIT;

    desc.kind = UI_CONTROL_DROPDOWN;
    desc.label = skins_slot_label(slot);
    desc.options = s_slotOptionPtrs[slot].data();
    desc.option_count = s_slotOptionPtrs[slot].size();
    desc.user_data = reinterpret_cast<void*>(static_cast<intptr_t>(slot));
    desc.get = [](ModContext*, void* data, UiControlValue* value) {
        const int slot = static_cast<int>(reinterpret_cast<intptr_t>(data));
        const std::string worn = skins_local_slot(slot);
        int index = 0;
        for (size_t i = 0; i < s_slotModelNames[slot].size(); ++i) {
            if (s_slotModelNames[slot][i] == worn) index = static_cast<int>(i) + 1;
        }
        value->int_value = index;
    };
    desc.set = [](ModContext*, void* data, const UiControlValue* value) {
        const int slot = static_cast<int>(reinterpret_cast<intptr_t>(data));
        const int index = static_cast<int>(value->int_value);
        const bool known = index > 0 && index <= static_cast<int>(s_slotModelNames[slot].size());
        skins_set_local_slot(slot, known ? s_slotModelNames[slot][index - 1].c_str() : "");
    };
    add_control(pane, desc);
    return true;
}

ModResult build_colors_detail(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Colors");

    add_button(pane, "Reset colors", [](ModContext*, void*) { colors_reset_mine(); });
    std::string lastGroup;
    for (int i = 0; i < colors_slot_count(); ++i) {
        const std::string group = colors_slot_group(i);
        if (group != lastGroup) {
            svc_ui->pane_add_section(mod_ctx, pane, group.c_str());
            lastGroup = group;
        }
        add_color(pane, colors_slot_label(i), colors_slot_var(i));
    }
    return MOD_OK;
}

ModResult build_equipment_detail(ModContext*, UiElementHandle pane, void*, ModError*) {
    add_panel_header(pane, "Equipment");
    svc_ui->pane_add_text(mod_ctx, pane,
        "One model for everything you hold, and any piece below can come from somewhere else. A "
        "piece left on Same as Equipment follows the choice above it.", nullptr);
    add_slot_dropdown(pane, kSkinChoiceEquipment);

    svc_ui->pane_add_section(mod_ctx, pane, "Pieces");
    int offered = 0;
    for (int slot = kSkinChoiceFirstItem; slot < kSkinChoiceCount; ++slot) {
        if (add_slot_dropdown(pane, slot)) ++offered;
    }
    if (offered == 0) {
        svc_ui->pane_add_text(mod_ctx, pane,
            "None of your models have a sword or shield of their own.", nullptr);
    }
    return MOD_OK;
}

void build_models(UiElementHandle pane, SurfaceHandles& h, UiElementHandle detail) {

    s_modelNames.clear();
    s_modelTitles.clear();
    s_modelAbout.clear();
    for (int i = 0; i < skins_count(); ++i) {
        s_modelNames.push_back(skins_name(i));
        s_modelTitles.push_back(skins_title(i));
        s_modelAbout.push_back(model_help_rml(i));
    }

    h.lastModels = worn_text();
    svc_ui->pane_add_text(mod_ctx, pane, h.lastModels.c_str(), &h.models);

    svc_ui->pane_add_section(mod_ctx, pane, "Presets");
    add_choice(pane, "Link", [](ModContext*, void*) { skins_set_local_all(""); }, nullptr,
        [](ModContext*, void*) { return skins_all_same(""); }, "Link as the game draws him.");
    for (size_t i = 0; i < s_modelNames.size(); ++i) {
        add_choice(pane, s_modelTitles[i].c_str(),
            [](ModContext*, void* d) {
                const int index = model_of(d);
                if (index >= 0) skins_set_local_all(s_modelNames[index].c_str());
            },
            pack_model(static_cast<int>(i)),
            [](ModContext*, void* d) {
                const int index = model_of(d);
                return index >= 0 && skins_all_same(s_modelNames[index].c_str());
            },
            s_modelAbout[i].c_str());
    }
    if (s_modelNames.empty()) {
        svc_ui->pane_add_text(mod_ctx, pane,
            "Nothing installed yet. Put a model folder in the folder below and press Reload.",
            nullptr);
    }

    svc_ui->pane_add_section(mod_ctx, pane, "Mix-n-match");

    if (detail != 0) {
        UiGroupDesc group = UI_GROUP_DESC_INIT;
        group.label = "Equipment";
        group.build = build_equipment_detail;
        svc_ui->pane_add_group(mod_ctx, pane, detail, &group, nullptr);
    } else {
        add_slot_dropdown(pane, kSkinChoiceEquipment);
    }
    if (detail != 0) {
        UiGroupDesc colors = UI_GROUP_DESC_INIT;
        colors.label = "Colors";
        colors.build = build_colors_detail;
        svc_ui->pane_add_group(mod_ctx, pane, detail, &colors, nullptr);
    }
    for (int slot = 0; slot < kSkinChoiceCount; ++slot) {

        if (slot == kSkinChoiceCutscenes) continue;

        if (slot >= kSkinChoiceFirstItem) continue;

        if (slot == kSkinChoiceEquipment) continue;
        add_slot_dropdown(pane, slot);
    }
    svc_ui->pane_add_section(mod_ctx, pane, "Your models folder");
    if (skins_can_open_folder()) {
        add_button(pane, "Open the folder", [](ModContext*, void*) { skins_open_folder(); });
    } else {

        static std::string s_folder;
        s_folder = skins_folder_path();
        if (!s_folder.empty()) svc_ui->pane_add_text(mod_ctx, pane, s_folder.c_str(), nullptr);
    }
    add_button(pane, "Reload", [](ModContext*, void*) { skins_refresh(); }, nullptr,
        "Press after adding or changing a folder.");
}

std::vector<std::string> s_colorGroups;

ModResult group_colors(ModContext*, UiElementHandle pane, void* data, ModError*) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(data));
    if (index < 0 || index >= static_cast<int>(s_colorGroups.size())) return MOD_OK;
    add_panel_header(pane, s_colorGroups[index].c_str());
    for (int i = 0; i < colors_slot_count(); ++i) {
        if (s_colorGroups[index] != colors_slot_group(i)) continue;
        add_color(pane, colors_slot_label(i), colors_slot_var(i));
    }
    return MOD_OK;
}

void build_colors(UiElementHandle pane, UiElementHandle detail) {

    add_button(pane, "Reset colors", [](ModContext*, void*) { colors_reset_mine(); });
    s_colorGroups.clear();
    for (int i = 0; i < colors_slot_count(); ++i) {
        const std::string group = colors_slot_group(i);
        if (s_colorGroups.empty() || s_colorGroups.back() != group) s_colorGroups.push_back(group);
    }
    for (size_t i = 0; i < s_colorGroups.size(); ++i) {
        add_group_or_section(pane, detail, s_colorGroups[i].c_str(), group_colors,
            reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }
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

    build_connect(pane, s_panel, 0);
    build_players(pane, s_panel);
    build_game(pane, 0);
    build_screen(pane, 0);
    build_models(pane, s_panel, 0);
    build_colors(pane, 0);
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
    build_connect(left, s_window, right);
    svc_ui->pane_add_text(mod_ctx, right,
        "Everyone types the same Room. One of you presses Host, the rest press Join. "
        "Leave Room empty and Host makes a code for you to share.\n\n"
        "Joining loads the host's game where they are standing. Your own save is backed up "
        "first, and you can restore it from Advanced.\n\n"
        "If it won't connect, open How to play together.",
        nullptr);
    return MOD_OK;
}

ModResult tab_players(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_players(left, s_window);

    s_window.lastParty = party_text();
    svc_ui->pane_add_text(mod_ctx, right, s_window.lastParty.c_str(), &s_window.party);
    svc_ui->pane_add_text(mod_ctx, right, "Pick one to teleport to them.", nullptr);
    return MOD_OK;
}

ModResult tab_game(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_game(left, right);
    svc_ui->pane_add_text(mod_ctx, right,
        "The rules of the shared world. Whoever hosts decides them for everybody.", nullptr);
    return MOD_OK;
}

ModResult tab_screen(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_screen(left, right);
    svc_ui->pane_add_text(mod_ctx, right,
        "Only your screen. None of this changes the game for anyone else.", nullptr);
    return MOD_OK;
}

ModResult tab_models(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    s_window = SurfaceHandles{};
    build_models(left, s_window, right);

    svc_ui->pane_add_text(mod_ctx, right,
        "Pick a model to see what it has and choose what to wear.\n\n"
        "Other players see your model only if they have it too. If they don't, they see Link.",
        nullptr);
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

const char* const kWindowStyle = R"RCSS(

section-heading {
    opacity: 0.85;
    font-size: var(--font-size-lg);
    letter-spacing: 1dp;
    padding-bottom: var(--space-xs);
    border-bottom: 1dp rgba(var(--color-border-rgb), 30%);
    margin-bottom: var(--space-xs);
}

section-heading:not(:first-of-type) {
    padding-top: var(--space-md);
}

pane > div {
    color: rgba(var(--color-text-rgb), 72%);
    line-height: 1.45;
    max-width: 560dp;
    padding-bottom: var(--space-xs);
}

pane {
    padding: var(--space-lg);
    gap: var(--space-xs);
}

select-button {
    margin-bottom: 1dp;
}

select-button key {
    font-weight: bold;
}

select-button.group-button {
    padding-top: var(--space-xs);
    padding-bottom: var(--space-xs);
    border-bottom-width: 1dp;
    border-bottom-color: rgba(var(--color-border-rgb), 22%);
}

select-button.group-button key {
    font-family: var(--font-family-heading);
    font-size: var(--font-size-2xl);
}

.coop-head {
    display: block;
    font-family: var(--font-family-heading);
    font-weight: bold;
    text-transform: uppercase;
    letter-spacing: 1dp;
    font-size: var(--font-size-3xl);
    color: var(--color-accent);
    padding-bottom: var(--space-xs);
    border-bottom-width: 1dp;
    border-bottom-color: rgba(var(--color-accent-rgb), 35%);
}

.coop-head.coop-danger {
    color: var(--color-error);
    border-bottom-color: rgba(var(--color-error-rgb), 45%);
}

.coop-sub {
    display: block;
    color: rgba(var(--color-text-rgb), 55%);
    font-size: var(--font-size-md);
    line-height: 1.45;
    padding-top: var(--space-xs);
    padding-bottom: var(--space-sm);
}

.coop-step {
    display: flex;
    align-items: flex-start;
    gap: var(--space-sm);
    padding-bottom: var(--space-sm);
}

.coop-step-n {
    flex: 0 0 22dp;
    width: 22dp;
    height: 22dp;
    line-height: 22dp;
    text-align: center;
    border-radius: 11dp;
    background-color: rgba(var(--color-accent-rgb), 20%);
    color: var(--color-accent);
    font-family: var(--font-family-heading);
    font-weight: bold;
    font-size: var(--font-size-2xs);
}

.coop-step-t {
    flex: 1 1 auto;
    color: rgba(var(--color-text-rgb), 88%);
    font-size: var(--font-size-md);
    line-height: 1.5;
}

.coop-credit {
    display: block;
    font-family: var(--font-family-heading);
    font-weight: bold;
    text-transform: uppercase;
    letter-spacing: 1dp;
    font-size: var(--font-size-md);
    color: var(--color-accent);
    padding-bottom: var(--space-xs);
}

.coop-note {
    display: block;
    color: rgba(var(--color-text-rgb), 72%);
    font-size: var(--font-size-md);
    line-height: 1.45;
    padding-bottom: var(--space-xs);
}

.coop-listhead {
    display: block;
    font-family: var(--font-family-heading);
    font-weight: bold;
    text-transform: uppercase;
    letter-spacing: 1dp;
    font-size: var(--font-size-2xs);
    color: rgba(var(--color-text-rgb), 45%);
    padding-top: var(--space-sm);
    padding-bottom: var(--space-2xs);
}

.coop-item {
    display: block;
    font-size: var(--font-size-md);
    color: rgba(var(--color-text-rgb), 88%);
    padding-top: 2dp;
    padding-bottom: 2dp;
    padding-left: var(--space-sm);
    border-left-width: 2dp;
    border-left-color: rgba(var(--color-accent-rgb), 55%);
    margin-bottom: 2dp;
}

.coop-item.coop-missing {
    color: rgba(var(--color-text-rgb), 38%);
    border-left-color: rgba(var(--color-border-rgb), 28%);
}
)RCSS";

void open_window() {
    if (s_windowHandle != 0) return;

    const size_t tabCount = features_debug_menu() ? 6 : 5;
    UiTabDesc tabs[6] = {UI_TAB_DESC_INIT, UI_TAB_DESC_INIT, UI_TAB_DESC_INIT, UI_TAB_DESC_INIT,
        UI_TAB_DESC_INIT, UI_TAB_DESC_INIT};
    tabs[0].title = "Connect";
    tabs[0].build = tab_connect;
    tabs[0].update = update_window;

    tabs[1].title = "Host";
    tabs[1].build = tab_game;
    tabs[1].update = update_window;
    tabs[2].title = "Local";
    tabs[2].build = tab_screen;
    tabs[2].update = update_window;
    tabs[3].title = "Players";
    tabs[3].build = tab_players;
    tabs[3].update = update_window;
    tabs[4].title = "Customization";
    tabs[4].build = tab_models;
    tabs[4].update = update_window;

    tabs[5].title = "Debug";
    tabs[5].build = tab_debug;
    tabs[5].update = update_window;

    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = tabCount;
    desc.rcss = kWindowStyle;
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

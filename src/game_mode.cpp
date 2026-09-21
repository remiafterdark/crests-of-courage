

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"

#include "mods/service.hpp"
#include "mods/svc/game_mode.h"
#include "mods/svc/save.h"
#include "mods/svc/ui.h"

#include <cstdint>
#include <cstring>

IMPORT_OPTIONAL_SERVICE(GameModeService, svc_game_mode);
IMPORT_OPTIONAL_SERVICE(SaveService, svc_save);

namespace {

const char* const kGameModeId = "dev.remiafterdark.coop_mod.coop";

const char* const kSaveName = "crests-of-courage";

bool s_active = false;

GameModeNewSaveState* s_newSaveState = nullptr;
UiWindowHandle s_newSaveWindow = 0;

void finish_new_save(ModContext* ctx) {
    if (s_newSaveState != nullptr) *s_newSaveState = GAME_MODE_STATE_PROCEED;
    if (s_newSaveWindow != 0 && svc_ui != nullptr) svc_ui->window_close(ctx, s_newSaveWindow);
}

void add_button(UiElementHandle pane, const char* label, UiPressedFn onPressed) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_BUTTON;
    desc.label = label;
    desc.on_pressed = onPressed;
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

void add_number(UiElementHandle pane, const char* label, ConfigVarHandle var) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_NUMBER;
    desc.label = label;
    desc.binding = UI_BINDING_CONFIG_VAR;
    desc.config_var = var;
    desc.min = 1024;
    desc.max = 65535;
    desc.step = 1;
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

ModResult build_new_save_tab(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, left, "Host");
    add_number(left, "Port", coop_net_port_var());
    add_button(left, "Host", [](ModContext* ctx, void*) {
        coop_net_host();
        finish_new_save(ctx);
    });

    svc_ui->pane_add_section(mod_ctx, left, "Join");
    UiControlDesc address = UI_CONTROL_DESC_INIT;
    address.kind = UI_CONTROL_STRING;
    address.label = "Address";
    address.binding = UI_BINDING_CONFIG_VAR;
    address.config_var = coop_net_address_var();
    address.max_length = 64;
    svc_ui->pane_add_control(mod_ctx, left, &address, nullptr);
    add_number(left, "Port", coop_net_join_port_var());
    add_button(left, "Join", [](ModContext* ctx, void*) {
        coop_net_join();
        finish_new_save(ctx);
    });

    svc_ui->pane_add_section(mod_ctx, left, "Alone");
    add_button(left, "Start without co-op", [](ModContext* ctx, void*) { finish_new_save(ctx); });

    svc_ui->pane_add_text(mod_ctx, right,
        "Co-op files are separate from your single-player ones.", nullptr);
    return MOD_OK;
}

ModResult on_new_save_select(void*, GameModeNewSaveState* state, ModError* outError) {
    s_newSaveState = state;
    if (svc_ui == nullptr) {

        if (state != nullptr) *state = GAME_MODE_STATE_PROCEED;
        return MOD_OK;
    }
    UiTabDesc tabs[1] = {UI_TAB_DESC_INIT};
    tabs[0].title = "Co-op";
    tabs[0].build = build_new_save_tab;

    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 1;
    desc.on_closed = [](ModContext*, UiWindowHandle, void*) {

        if (s_newSaveState != nullptr && *s_newSaveState == GAME_MODE_STATE_PENDING) {
            *s_newSaveState = GAME_MODE_STATE_RETURN;
        }
        s_newSaveWindow = 0;
    };
    const ModResult result = svc_ui->window_push(mod_ctx, &desc, &s_newSaveWindow);
    if (result != MOD_OK) {
        return mods::set_error(outError, result, "could not open the co-op start window");
    }
    return MOD_OK;
}

struct CoopSaveBlob {
    uint32_t version;

    char lastHostName[kCoopNameMax];
    char lastHostAddress[64];
};
const uint32_t kBlobVersion = 1;

ModResult on_activated(void*, ModError*) {
    s_active = true;
    coop_log::info("coop_mod: [MODE] co-op mode active - this file is its own, single-player saves"
                   " are not touched");
    return MOD_OK;
}

ModResult on_deactivated(void*, ModError*) {
    s_active = false;
    return MOD_OK;
}

ModResult on_save_loaded(void*, ModError*) {
    if (svc_save == nullptr) return MOD_OK;
    CoopSaveBlob blob{};
    size_t size = sizeof(blob);
    if (svc_save->get_blob(mod_ctx, "coop", &blob, &size) != MOD_OK || size != sizeof(blob) ||
        blob.version != kBlobVersion) {
        return MOD_OK;
    }
    if (blob.lastHostName[0] != '\0') {
        coop_log::info("coop_mod: [MODE] this file was last played with '{}'", blob.lastHostName);
    }
    coop_remember_last_host(blob.lastHostName, blob.lastHostAddress);
    return MOD_OK;
}

}

void game_mode_init() {
    if (svc_game_mode == nullptr) {
        coop_log::info("coop_mod: [MODE] no game mode service - co-op runs in the ordinary mode");
        return;
    }
    GameModeDesc desc = {};
    desc.struct_size = sizeof(desc);
    desc.game_mode_id = kGameModeId;
    desc.full_name = "Co-op";

    std::strncpy(const_cast<char*>(desc.save_name), kSaveName, sizeof(desc.save_name) - 1);
    desc.user_data = nullptr;
    desc.on_activated = on_activated;
    desc.on_deactivated = on_deactivated;
    desc.on_save_loaded = on_save_loaded;
    desc.on_new_save_select = on_new_save_select;
    if (svc_game_mode->register_game_mode(mod_ctx, &desc) != MOD_OK) {
        coop_log::warn("coop_mod: [MODE] could not register the co-op game mode");
        return;
    }
    coop_log::info("coop_mod: [MODE] co-op registered, saving to '{}'", kSaveName);
}

bool game_mode_is_coop() {
    if (!s_active && svc_game_mode != nullptr) {

        bool active = false;
        if (svc_game_mode->is_active(mod_ctx, kGameModeId, &active) == MOD_OK) return active;
    }
    return s_active;
}

void game_mode_remember_host(const char* name, const char* address) {
    if (svc_save == nullptr || !game_mode_is_coop()) return;
    CoopSaveBlob blob{};
    blob.version = kBlobVersion;
    if (name != nullptr) std::strncpy(blob.lastHostName, name, sizeof(blob.lastHostName) - 1);
    if (address != nullptr) {
        std::strncpy(blob.lastHostAddress, address, sizeof(blob.lastHostAddress) - 1);
    }

    svc_save->set_blob(mod_ctx, "coop", &blob, sizeof(blob));
}

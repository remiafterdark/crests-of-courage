#pragma once

#include "util.hpp"

#include "mods/svc/config.h"

#include <cstddef>
#include <cstdint>
#include <string>

bool coop_net_connected();
bool coop_net_connecting();
bool coop_net_is_host();

uint32_t coop_net_ticks_since_rx();

uint32_t coop_net_ticks_since_player(uint8_t playerId);

uint32_t coop_ticks_since_world(uint8_t playerId);

bool coop_world_stalled(uint8_t playerId);

bool coop_player_paused(uint8_t playerId);

bool coop_local_paused();
void coop_net_set_player_paused(uint8_t playerId, bool paused);

uint32_t coop_local_world_frames();

uint32_t coop_net_rtt_ticks(uint8_t playerId);

int32_t coop_net_ping_ms(uint8_t playerId);
const char* coop_net_status();
void coop_net_host();
void coop_net_join();
void coop_net_disconnect();

void coop_net_send(uint8_t type, const void* payload, size_t size);

void coop_net_send_to(uint8_t playerId, uint8_t type, const void* payload, size_t size);

uint8_t coop_net_local_id();

uint8_t coop_net_roster();
bool coop_net_player_present(uint8_t playerId);

void coop_net_set_local_id(uint8_t playerId, uint8_t hostMaxPlayers);
void coop_net_set_roster(uint8_t roster);

void features_on_roster_changed();
ConfigVarHandle coop_net_port_var();
ConfigVarHandle coop_net_address_var();
ConfigVarHandle coop_net_autoconnect_var();
void coop_debug_spawn_puppet();
void coop_debug_force_transform();
void coop_debug_give_midna();

struct CoopPeer {
    bool present = false;
    bool inGame = false;
    std::string name;
    char stage[9] = {};
    int8_t startRoom = 0;
    int8_t curRoom = 0;
    int8_t layer = -1;
    int16_t point = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    int16_t angleY = 0;

    bool lifeKnown = false;
    uint16_t life = 0;
    uint16_t maxLife = 0;
};

struct CoopFeatureVars {
    ConfigVarHandle name = 0;
    ConfigVarHandle syncInventory = 0;
    ConfigVarHandle notifyItems = 0;
    ConfigVarHandle nametags = 0;
    ConfigVarHandle nametagsHideFar = 0;
    ConfigVarHandle nametagsEdge = 0;
    ConfigVarHandle nametagsHealth = 0;
    ConfigVarHandle syncSounds = 0;
    ConfigVarHandle soundVolume = 0;
    ConfigVarHandle syncTime = 0;
    ConfigVarHandle syncVfx = 0;

    ConfigVarHandle puppetWarpFx = 0;

    ConfigVarHandle puppetWarpWarm = 0;

    ConfigVarHandle puppetMidna = 0;

    ConfigVarHandle debugMenu = 0;

    ConfigVarHandle puppetLanternLight = 0;

    ConfigVarHandle deathLink = 0;

    ConfigVarHandle debugAutowarp = 0;

    ConfigVarHandle debugShiftTicks = 0;
    ConfigVarHandle debugShiftX = 0;
    ConfigVarHandle debugShiftZ = 0;
};

void features_register_vars();
void features_init();
void features_update();
void features_on_connected();
void features_on_disconnected();
void features_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);
const CoopPeer& features_peer();

const CoopPeer& features_peer_of(uint8_t playerId);

bool features_any_peer_on_stage(const char* stage);
const CoopFeatureVars& features_vars();

bool features_puppet_warp_fx();

bool features_puppet_warp_warm();

bool features_puppet_lantern_light();

bool features_puppet_midna();

bool features_debug_menu();
std::string features_local_name();
void features_teleport_to_player(uint8_t playerId);

void grass_update();
void grass_reset();
void grass_on_message(const uint8_t* payload, size_t size, uint8_t from);
void horse_register_vars();
void horse_update();
void horse_reset();
void horse_on_message(const uint8_t* payload, size_t size, uint8_t from);
ConfigVarHandle horse_enabled_var();

void squad_hud_register_vars();
void squad_hud_queue();
ConfigVarHandle squad_hud_enabled_var();
ConfigVarHandle squad_hud_size_var();
ConfigVarHandle squad_hud_offset_var();
ConfigVarHandle squad_hud_fake_var();

class J3DModel;
void colors_register_vars();
void colors_init();
void colors_update();
void colors_on_connected();
void colors_on_disconnected();
void colors_on_message(const uint8_t* payload, size_t size);
int colors_slot_count();
const char* colors_slot_label(int slot);
const char* colors_slot_group(int slot);
ConfigVarHandle colors_slot_var(int slot);
void colors_reset_mine();

void colors_attach_puppet_model(J3DModel* model);

void colors_detach_puppet_models();

class fopAc_ac_c;
void projectiles_update();
void projectiles_on_message(const uint8_t* payload, size_t size);

bool projectiles_is_remote(fopAc_ac_c* actor);

void fx_init();
void fx_update();

void fx_on_sounds(const uint8_t* payload, size_t size, uint8_t from);
void fx_on_particles(const uint8_t* payload, size_t size, uint8_t from);

void fx_owner_window(bool active);

void pvp_register_vars();
void pvp_update();
void pvp_on_connected();
void pvp_on_disconnected();
void pvp_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);

bool pvp_active();

void enemies_register_vars();
void enemies_init();
void enemies_update();
void enemies_on_connected();
void enemies_on_disconnected();
void enemies_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);
ConfigVarHandle enemies_enabled_var();

enum CoopSessionFlag : uint16_t {
    kSessEnemies = 1 << 0,
    kSessBosses = 1 << 1,
    kSessBossWait = 1 << 2,
    kSessWorldObjects = 1 << 3,
    kSessDungeon = 1 << 4,
    kSessStory = 1 << 5,
    kSessItems = 1 << 6,
    kSessTime = 1 << 7,
    kSessDeathLink = 1 << 8,
};
bool coop_session(uint16_t flag, bool localValue);

bool coop_session_from_host();

ConfigVarHandle enemies_decisions_var();

ConfigVarHandle enemies_breakables_var();
void enemies_on_local_unpause();

uint8_t enemies_room_owner_player(int room);

ConfigVarHandle enemies_movers_var();

void spawns_register_vars();
void spawns_init();
void spawns_update();
void spawns_on_connected();
void spawns_on_disconnected();
void spawns_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);

bool spawns_replicates_procname(int16_t procName);

void boss_register_vars();
void boss_init();
void boss_update();
void boss_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);
void boss_on_connected();
ConfigVarHandle boss_enabled_var();
ConfigVarHandle boss_wait_var();

bool boss_is_supported_procname(short procName);

bool boss_waiting_for_peer();

void boss_queue_overlay();

bool boss_local_demo_running();

void world_register_vars();
void world_update();
void world_on_connected();
void world_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);
ConfigVarHandle world_dungeon_var();
ConfigVarHandle world_story_var();

void joinsync_register_vars();
void joinsync_update();
void joinsync_on_connected();
void joinsync_on_message(const uint8_t* payload, size_t size);

void joinsync_restore_backup(bool oldest);

std::string joinsync_backup_summary();

void features_reset_sync_baselines();
void features_toast(const char* title, const char* body);

void ui_init();

void puppet_hook_release_player(uint8_t playerId);
bool puppet_hook_player_active(uint8_t playerId);
bool puppet_hook_get_pose_of(uint8_t playerId, float* x, float* y, float* z, short* angleY,
    float* speedX, float* speedZ);

uint8_t puppet_hook_nearest_player(float x, float y, float z);

bool puppet_hook_get_anim(uint8_t playerId, uint16_t* resIdx, float* frame);

bool puppet_hook_get_position(float* x, float* y, float* z);

void puppet_hook_set_edge_tags(bool enabled);
void puppet_hook_set_nametag_health(bool enabled);
bool puppet_hook_health_enabled();

bool puppet_hook_health_anchor(uint8_t playerId, float* u, float* v, float* cell, float* camDist);

void puppet_hook_set_player_low_health(uint8_t playerId, bool low);
void puppet_hook_set_player_nametag(uint8_t playerId, const char* name, bool enabled,
    bool hideFar);
void puppet_hook_set_player_visible(uint8_t playerId, bool visible);
void puppet_hook_set_vfx_enabled(bool enabled);
void puppet_hook_request_release();
bool puppet_hook_is_wolf();

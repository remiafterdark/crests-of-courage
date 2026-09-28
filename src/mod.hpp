#pragma once

#include "util.hpp"

#include "mods/svc/config.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

bool coop_net_connected();
bool coop_net_connecting();
bool coop_net_is_host();

uint32_t coop_net_ticks_since_rx();

uint32_t coop_net_ticks_since_player(uint8_t playerId);

uint32_t coop_ticks_since_world(uint8_t playerId);

bool coop_world_stalled(uint8_t playerId);

bool coop_player_paused(uint8_t playerId);

bool coop_player_unheard(uint8_t playerId);

bool coop_local_paused();
void coop_net_set_player_paused(uint8_t playerId, bool paused);

uint32_t coop_local_world_frames();

uint32_t coop_net_rtt_ticks(uint8_t playerId);

void coop_net_snapshot_counts(uint8_t playerId, uint32_t* in, uint32_t* stale, uint32_t* unknown);

struct CoopNetTraffic {
    uint64_t txBytes = 0;
    uint64_t rxBytes = 0;
    uint64_t txPackets = 0;
    uint64_t rxPackets = 0;
};
CoopNetTraffic coop_net_traffic();

struct CoopLinkInfo {
    bool udp = false;
    uint32_t rttMs = 0;
    uint64_t resends = 0;
    size_t queuedBytes = 0;
};
bool coop_net_link_info(uint8_t playerId, CoopLinkInfo* out);
int32_t coop_net_ping_ms(uint8_t playerId);
const char* coop_net_status();
void coop_net_host();
void coop_net_join();
void coop_net_disconnect();

void coop_net_send(uint8_t type, const void* payload, size_t size);

void coop_net_send_to(uint8_t playerId, uint8_t type, const void* payload, size_t size);

uint8_t coop_net_local_id();

uint16_t coop_net_roster();
bool coop_net_player_present(uint8_t playerId);

void coop_net_set_local_id(uint8_t playerId, uint8_t hostMaxPlayers);
void coop_net_set_roster(uint16_t roster);

void features_on_roster_changed();
ConfigVarHandle coop_net_port_var();
ConfigVarHandle coop_net_address_var();
ConfigVarHandle coop_net_join_port_var();
ConfigVarHandle coop_net_autoconnect_var();

bool features_reload_at_player(uint8_t playerId);

void features_debug_fake_peer(uint8_t id, bool on, const char* name, const float* pos, uint16_t life,
    uint16_t maxLife);
void coop_debug_spawn_puppet();
void coop_debug_force_transform();
void coop_debug_give_midna();

class daAlink_c;
void ganon_init();
bool ganon_debug_toggle();
bool ganon_debug_is_on();
void ganon_draw_post(daAlink_c* alink);

extern const char* const kGanondorfSkin;
extern const char* const kHerosShadeSkin;
extern const char* const kBeastGanonSkin;
bool beast_debug_toggle();
class J3DModelData;
J3DModelData* ganon_skin_equipment(const char* file);
bool ganon_skin_ships(const char* file);
bool shade_skin_ships(const char* file);

class J3DModel;
class dKy_tevstr_c;
int standin_body(const char* skin);
bool standin_puppet_ready(int pupId, int body);
void standin_puppet_draw(int pupId, J3DModel* body, uint8_t handL, uint8_t handR, dKy_tevstr_c* tev);
void standin_puppet_hold_begin(int pupId, J3DModel* body, int podJoint);
void standin_puppet_hold_end(J3DModel* body);

float (*standin_local_joint_mtx(J3DModel* link, int joint))[4];
J3DModel* standin_puppet_model(int pupId);
J3DModel* ganon_puppet_sword(int pupId, bool sheath);
enum ShadePiece { kShadePieceSword, kShadePieceShield, kShadePieceSheath };
bool standin_owns_data(const J3DModelData* data);
J3DModel* shade_puppet_weapon(int pupId, int piece);

class cXyz;
class csXyz;
void sumo_init();
void sumo_after_player(daAlink_c* alink);
void sumo_on_message(const uint8_t* payload, size_t size, uint8_t from);
bool sumo_puppet_transform(uint8_t playerId, cXyz* pos, csXyz* angle);
bool sumo_local_demo_running();
bool sumo_hides_equipment(uint8_t playerId);
void sumo_on_state(const uint8_t* payload, size_t size, uint8_t from);
void fx_play_for(uint8_t playerId, uint32_t soundId, const float* pos);
bool sumo_shows_in_cutscene(uint8_t playerId);

bool sumo_puppet_anim(uint8_t playerId, uint16_t* underIdx, uint16_t* upperIdx, float* frame);

void beast_init();
void beast_frame(daAlink_c* alink);
bool beast_model_draw(daAlink_c* alink, J3DModel* model, int noDraw);
bool beast_local_active();
J3DModel* beast_local_model();
int wolf_standin(const char* skin);
bool beast_puppet_ready(int pupId, int look);
void beast_puppet_draw(int pupId, J3DModel* body, dKy_tevstr_c* tev);
J3DModel* beast_puppet_model(int pupId);

enum SkinOutfit {
    kSkinOutfitHero = 0,
    kSkinOutfitOrdon,
    kSkinOutfitZora,
    kSkinOutfitMagic,
    kSkinOutfitWolf,
    kSkinOutfitCount,
};

const size_t kSkinNameMax = 32;

enum SkinChoiceSlot {
    kSkinChoiceHero = 0,
    kSkinChoiceOrdon,
    kSkinChoiceZora,
    kSkinChoiceMagic,
    kSkinChoiceWolf,
    kSkinChoiceEquipment,
    kSkinChoiceCutscenes,
    kSkinChoiceVoice,

    kSkinChoiceWoodSword,
    kSkinChoiceOrdonSword,
    kSkinChoiceMasterSword,
    kSkinChoiceHylianShield,
    kSkinChoiceOrdonShield,
    kSkinChoiceHookshot,
    kSkinChoiceBoomerang,
    kSkinChoiceBombs,
    kSkinChoiceBow,
    kSkinChoiceSlingshot,
    kSkinChoiceBallAndChain,
    kSkinChoiceSpinner,
    kSkinChoiceFishingRod,
    kSkinChoiceBottle,
    kSkinChoiceCount,

    kSkinChoiceFirstItem = kSkinChoiceWoodSword,
};

struct SkinChoices {
    char name[kSkinChoiceCount][kSkinNameMax];
    uint32_t hash[kSkinChoiceCount];
};

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

    SkinChoices skins = {};
    uint32_t skinStamp = 0;

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

    ConfigVarHandle debugMenu = 0;

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

class J3DModel;

enum SkinPart {
    kSkinPartBody = 0,
    kSkinPartFace,
    kSkinPartHead,
    kSkinPartHands,
    kSkinPartSword,
    kSkinPartShield,
    kSkinPartCount,
};

void skins_init();
void skins_refresh();
int skins_count();
bool skins_hidden(int index);
const char* skins_name(int index);
std::string skins_parts_text(int index);

std::string skins_title(int index);
std::string skins_about_text(int index);

std::string skins_author_text(int index);
std::string skins_own_words(int index);

std::string skins_equipment_text(int index);

void skins_equipment_list(int index, std::vector<std::string>& has,
    std::vector<std::string>& missing);
void skins_outfit_list(int index, std::vector<std::string>& out);

std::string skins_folder_path();

const char* const kCoopShippedModelsVersion = "4";

void skins_cycle_update();

void skins_outfit_cycle_update();

bool skins_local_disabled();

void icons_update();
void icons_shutdown();
void skins_open_folder();

bool skins_can_open_folder();

bool skins_covers_outfit(const char* name, int outfit);

bool skins_covers_slot(const char* name, int slot);

int skins_slot_for_equipment_file(const char* file);
class J3DModelData;

const char* skins_aram_file_for_index(uint16_t index);
J3DModelData* skins_local_aram_data(uint16_t index);

void skins_warn_update(int outfit);

std::string skins_local_slot(int slot);
void skins_set_local_slot(int slot, const char* name);
void skins_local_choices(SkinChoices* out);

void skins_set_local_all(const char* name);

bool skins_all_same(const char* name);

const char* skins_slot_label(int slot);

ConfigVarHandle skins_files_var();
void skins_files_changed();

void voices_init();

void game_mode_init();

class fopAc_ac_c;
struct cXyz;
void horses_init();
bool horses_spawn_for(uint8_t player, const cXyz& pos, int16_t angleY);
void horses_release(uint8_t player);
void horses_on_disconnected();

fopAc_ac_c* horses_actor_for(uint8_t player);

void horses_update();

bool horse_peer_riding(uint8_t owner);

bool horse_sync_enabled();
fopAc_ac_c* horses_local();

bool game_mode_is_coop();

void game_mode_remember_host(const char* name, const char* address);

void coop_remember_last_host(const char* name, const char* address);

void coop_toast(const char* title, const char* body);
uint32_t local_skin_stamp();
void send_skin_choices();

std::string coop_net_join_address();
void voices_begin(const char* skinName);

void voices_begin_local();

extern const char* const kSilentVoice;
bool voices_local_silent();
inline bool voices_is_link_voice(uint32_t soundId) { return soundId >= 0x10000u && soundId <= 0x100C7u; }

void voices_begin_remote(const char* skinName);
void voices_end_remote();
void voices_end_local();
void voices_update();
void voices_on_disconnected();

bool skins_have(const char* name, uint32_t hash);

J3DModel* skins_part_model(const char* name, int outfit, int part, float scale);

class J3DModelData;
J3DModelData* skins_equipment_data(const char* name, const char* file);
J3DModelData* skins_local_equipment_data(const char* file);

int skins_slot_for_outfit(int outfit);
class J3DModelData;
J3DModelData* skins_part_data(const char* name, int outfit, int part);

J3DModelData* skins_local_part_data(int outfit, int part);

J3DModelData* skins_local_cutscene_data(const char* file);

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

enum NotifyKind { kNotifyItems, kNotifyPlayers, kNotifyRando, kNotifyTeleport, kNotifyOther,
    kNotifyKinds };
void notify_register_vars(ConfigVarHandle itemsVar);

void coop_notify(NotifyKind kind, const std::string& title, const std::string& body,
    uint32_t durationMs = 0);
inline void coop_notify_c(NotifyKind kind, const char* title, const char* body) {
    coop_notify(kind, title != nullptr ? title : "", body != nullptr ? body : "");
}
void notify_queue();
void notify_debug_test();
ConfigVarHandle notify_on_var();
ConfigVarHandle notify_kind_var(NotifyKind kind);
ConfigVarHandle notify_side_var();
ConfigVarHandle notify_edge_var();
ConfigVarHandle notify_from_side_var();
ConfigVarHandle notify_from_edge_var();
ConfigVarHandle notify_busy_var();
ConfigVarHandle notify_hurry_var();
ConfigVarHandle notify_more_var();
ConfigVarHandle notify_slide_var();
ConfigVarHandle notify_preview_var();
ConfigVarHandle notify_test_amount_var();
ConfigVarHandle notify_cycle_var();
void notify_stop_preview();
ConfigVarHandle notify_seconds_var();
ConfigVarHandle notify_size_var();
ConfigVarHandle notify_max_var();
ConfigVarHandle notify_engine_var();

std::string coop_mem_status();
void squad_hud_queue();
ConfigVarHandle squad_hud_enabled_var();
ConfigVarHandle squad_hud_size_var();
ConfigVarHandle squad_hud_offset_var();
ConfigVarHandle squad_hud_offset_x_var();
ConfigVarHandle squad_hud_side_var();

ConfigVarHandle squad_hud_hurt_only_var();

ConfigVarHandle squad_hud_hurt_seconds_var();
ConfigVarHandle squad_hud_hurt_fade_var();
ConfigVarHandle squad_hud_world_size_var();

class J3DModel;
void colors_register_vars();

bool coop_config_json_value(const char* key, std::string* out);
void colors_init();
void colors_update();
void colors_on_connected();
void colors_on_disconnected();
void colors_on_message(const uint8_t* payload, size_t size, uint8_t from);
int colors_slot_count();
const char* colors_slot_label(int slot);
const char* colors_slot_group(int slot);
ConfigVarHandle colors_slot_var(int slot);
void colors_reset_mine();

void colors_attach_puppet_model(J3DModel* model, uint8_t owner);

void coop_crash_trail(const char* step);

bool private_arc_request(const char* name);
class JKRHeap* private_arc_heap_if_any();
int private_arc_poll(const char* name);
void private_arc_release(const char* name);
J3DModelData* private_arc_load(const char* name, const char* file);
J3DModelData* private_arc_load_idx(const char* name, uint32_t index);
void* private_arc_load_anm(const char* name, const char* file);
void* private_arc_load_anm_idx(const char* name, uint32_t index);
const char* private_arc_file_name(const char* name, uint32_t index);
void private_arc_free_data(J3DModelData* data);

void colors_attach_local_model(J3DModel* model);

void colors_detach_local_models();

void colors_detach_puppet_models();

void colors_detach_model(J3DModel* model);

class fopAc_ac_c;
void projectiles_update();
void projectiles_on_message(const uint8_t* payload, size_t size);

bool projectiles_is_remote(fopAc_ac_c* actor);

struct layer_class;
layer_class* coop_enter_actor_layer();
void coop_leave_actor_layer(layer_class* prev);
struct CoopActorLayer {
    layer_class* prev;
    CoopActorLayer() : prev(coop_enter_actor_layer()) {}
    ~CoopActorLayer() { coop_leave_actor_layer(prev); }
    CoopActorLayer(const CoopActorLayer&) = delete;
    CoopActorLayer& operator=(const CoopActorLayer&) = delete;
};
bool puppet_hook_torso_of(uint8_t playerId, cXyz* out);

bool puppet_hook_joint_pos(uint8_t playerId, int joint, cXyz* out);
bool projectiles_blast_is_ours(fopAc_ac_c* blast);

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
bool pvp_lock_on();
void pvp_after_player(daAlink_c* alink);

void spinner_after_player(daAlink_c* alink);
ConfigVarHandle pvp_enabled_var();
ConfigVarHandle pvp_damage_var();
ConfigVarHandle pvp_lock_on_var();

void rival_init();

void version_update();
void version_remind();
bool version_outdated();
void rival_update();
void rival_forget_all();
bool rival_is(fopAc_ac_c* actor);
struct MsgPvpHit;
int pvp_predict_damage(const MsgPvpHit& hit, uint8_t combat);
void features_guess_damage(uint8_t playerId, int dmg);
uint16_t features_shown_life(uint8_t playerId);
bool rival_hooked_pos(uint8_t player, cXyz* pos);
uint8_t puppet_hook_combat_of(uint8_t playerId);

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
bool spawns_is_replica(fopAc_ac_c* actor);
bool enemies_carried_by_other(fopAc_ac_c* actor);

void enemies_note_boomerang_bomb();

void enemies_note_enemy_bomb();

bool spawns_ride_bomb_on_boomerang_of(uint8_t owner, fopAc_ac_c* bomb);

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

void skipvote_init();
void skills_init();

void rando_init();

void checks_init();

void map_markers_init();
ConfigVarHandle map_markers_full_var();
ConfigVarHandle map_markers_dungeon_var();
ConfigVarHandle map_markers_names_var();
ConfigVarHandle map_markers_name_size_var();
ConfigVarHandle map_markers_minimap_var();
ConfigVarHandle map_markers_arrow_size_var();
ConfigVarHandle map_markers_edge_var();
ConfigVarHandle map_markers_other_floor_var();
ConfigVarHandle map_markers_colors_var();
void checks_update();
void checks_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);

void features_check_found(uint8_t from, const char* check, uint8_t item);
void checks_on_join_synced();
std::vector<std::string> checks_debug_names_with(const char* prefix);

void selftest_init();
void selftest_update();
void rando_debug_enter_randomizer();
void rando_debug_set_local_seed(const char* hash);
std::string rando_debug_local_seed();
std::string rando_debug_any_seed();
bool rando_debug_host_seed_ready();
void features_debug_receive_item(uint8_t item, uint8_t from);
bool checks_collected(const char* name);
void rando_update();
void rando_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);
bool rando_active();

const char* check_place_name(const char* check);
const char* rando_item_name(uint8_t item);
bool rando_join_sync_allowed();
bool rando_join_sync_wait();
void game_mode_prompt_connect();
void skills_update();
void skills_on_message(const uint8_t* payload, size_t size);

uint8_t skills_tmp_private(int byte);

uint8_t skills_event_private(int byte);

void drops_init();

void twilight_update();
void twilight_on_message(uint8_t type, const uint8_t* payload, size_t size);
void skipvote_update();
void skipvote_on_message(const uint8_t* payload, size_t size, uint8_t from);

void world_register_vars();
void world_update();
void world_on_connected();
bool world_hold_story_flag(uint16_t flag);
void world_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from);
ConfigVarHandle world_dungeon_var();
ConfigVarHandle world_story_var();

void joinsync_register_vars();

ConfigVarHandle joinsync_keep_consumables_var();
void joinsync_update();
void joinsync_on_connected();
void joinsync_on_message(const uint8_t* payload, size_t size);

void joinsync_restore_backup(bool oldest);

std::string joinsync_backup_summary();

void features_reset_sync_baselines();
void features_toast(const char* title, const char* body);
std::string features_peer_name(uint8_t id);

void ui_init();

void puppet_hook_release_player(uint8_t playerId);
bool puppet_hook_player_active(uint8_t playerId);

bool puppet_hook_sword_mtx(uint8_t playerId, float out[3][4], bool* master);
bool puppet_hook_pose_fresh(uint8_t playerId);
bool puppet_hook_get_pose_of(uint8_t playerId, float* x, float* y, float* z, short* angleY,
    float* speedX, float* speedZ);
bool puppet_hook_is_wolf_of(uint8_t playerId);

bool puppet_hook_rides_spinner(uint8_t playerId);

uint8_t puppet_hook_nearest_player(float x, float y, float z);

bool puppet_hook_get_anim(uint8_t playerId, uint16_t* resIdx, float* frame);

bool puppet_hook_get_position(float* x, float* y, float* z);

void puppet_hook_set_edge_tags(bool enabled);

void puppet_register_vars();
ConfigVarHandle puppet_warp_dump_var();
void puppet_hook_set_nametag_health(bool enabled);

void puppet_hook_peer_skin_changed(uint8_t playerId);

void local_skin_rebuild_link();

ConfigVarHandle coop_net_upnp_var();

void upnp_begin(int port);
void upnp_release();
void upnp_update();
bool upnp_ready();

std::string upnp_external_address();

std::string upnp_status();

ConfigVarHandle coop_net_room_code_var();
ConfigVarHandle coop_net_room_server_var();
ConfigVarHandle coop_net_rooms_var();
ConfigVarHandle coop_net_host_key_var();
void coop_net_join_code();

void online_host_begin(int udpPort, const std::string& name);

const size_t kRoomNameMin = 4;
const size_t kRoomNameMax = 24;
std::string normalize_room_code(const std::string& typed);

void online_join_begin(const std::string& code, int localUdpPort);
void online_stop();
void online_update();
bool online_active();

std::string online_room_code();
std::string online_status();

bool online_accept_token(uint64_t token);

bool online_on_datagram(const std::string& from, const uint8_t* data, size_t size);

void coop_udp_send_raw(const std::string& endpoint, const void* data, size_t size);

void coop_online_punched(const std::string& endpoint, uint64_t token);

void coop_online_failed(const std::string& why, const std::string& upnpFallback);

void local_skin_equipment_update();

void local_skin_colors_update();

bool coop_local_models_unsafe();

inline bool coop_ptr_looks_live(const void* ptr) {
    uintptr_t v = reinterpret_cast<uintptr_t>(ptr);
#if defined(__aarch64__) || defined(_M_ARM64)
    v &= 0x00FFFFFFFFFFFFFFull;
#endif
    return v >= 0x10000ull && v < 0x0001000000000000ull && (v & 3) == 0;
}

int local_skin_outfit();

void local_skin_suppress(bool suppress);

void colors_invalidate_self();
bool puppet_hook_health_enabled();

bool puppet_hook_health_anchor(uint8_t playerId, float* u, float* v, float* cell, float* camDist);

void puppet_hook_set_player_low_health(uint8_t playerId, bool low);
void puppet_hook_set_player_nametag(uint8_t playerId, const char* name, bool enabled,
    bool hideFar);
void puppet_hook_set_player_visible(uint8_t playerId, bool visible);
void puppet_hook_set_vfx_enabled(bool enabled);
void puppet_hook_request_release();
bool puppet_hook_is_wolf();

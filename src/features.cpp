

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/item.h"
#include "mods/svc/log.hpp"
#include "print.hpp"
#include "mods/svc/ui.h"

#include "Z2AudioLib/Z2AudioMgr.h"
#include "Z2AudioLib/Z2LinkMgr.h"
#include "Z2AudioLib/Z2SeMgr.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_item.h"
#include "m_Do/m_Do_main.h"
#include "d/d_item_data.h"
#include "d/actor/d_a_itembase.h"
#include "f_op/f_op_actor_mng.h"

#include <cmath>
#include <cstring>
#include <string>

bool s_devLogging = false;

uint16_t s_hostSession = 0;
bool s_haveHostSession = false;
namespace {
void send_session_settings();
}

namespace {

CoopFeatureVars s_vars;

CoopPeer s_peers[kCoopMaxPlayers];

CoopPeer& peer_slot(uint8_t id) {
    static CoopPeer dummy;
    return id < kCoopMaxPlayers ? s_peers[id] : dummy;
}

CoopPeer& first_peer() {
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (s_peers[i].present) return s_peers[i];
    }
    static CoopPeer none;
    none = CoopPeer{};
    return none;
}

bool s_peerAnnounced[kCoopMaxPlayers] = {};

uint16_t s_lastRoster = 0;

std::string sender_name(uint8_t from) {
    if (from < kCoopMaxPlayers && s_peers[from].present && !s_peers[from].name.empty()) {
        return s_peers[from].name;
    }
    return "Another player";
}
uint32_t s_presenceTicks = 0;
uint32_t s_timeTicks = 0;

ConfigVarHandle register_var(
    const char* name, ConfigVarType type, bool defBool, int64_t defInt, const char* defString) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = type;
    desc.default_bool = defBool;
    desc.default_int = defInt;
    desc.default_string = defString;
    ConfigVarHandle handle = 0;
    if (svc_config->register_var(mod_ctx, &desc, &handle) != MOD_OK) {
        coop_log::warn("coop_mod: failed to register config var '{}'", name);
    }
    return handle;
}

std::string sanitize_name(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (c >= 0x20 && c < 0x7F) out.push_back(c);
        if (out.size() >= static_cast<size_t>(kCoopNameMax - 1)) break;
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? std::string("Player") : out;
}

void copy_name(char* dst, const std::string& name) {
    std::memset(dst, 0, kCoopNameMax);
    std::strncpy(dst, name.c_str(), kCoopNameMax - 1);
}

std::string s_knownName[kCoopMaxPlayers];
bool s_nameGapLogged[kCoopMaxPlayers] = {};

void remember_name(uint8_t from, const std::string& name) {
    if (from >= kCoopMaxPlayers || name.empty()) return;
    s_knownName[from] = name;
    s_nameGapLogged[from] = false;
}

std::string wire_name(const char* buf) {
    const void* end = std::memchr(buf, '\0', kCoopNameMax);
    const size_t len = (end != nullptr) ? static_cast<size_t>(static_cast<const char*>(end) - buf)
                                        : static_cast<size_t>(kCoopNameMax);
    return sanitize_name(std::string(buf, len));
}

std::string rml_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

void toast(const std::string& title, const std::string& body, uint32_t durationMs = 4500) {
    if (svc_ui == nullptr) return;
    const std::string titleRml = rml_escape(title);
    const std::string bodyRml = rml_escape(body);
    UiToastDesc desc = UI_TOAST_DESC_INIT;
    desc.title_rml = titleRml.c_str();
    desc.body_rml = bodyRml.c_str();
    desc.duration_ms = durationMs;
    svc_ui->push_toast(mod_ctx, &desc);
}

bool in_gameplay() {
    return daAlink_getAlinkActorClass() != nullptr;
}

}

void coop_toast(const char* title, const char* body) {
    if (title == nullptr) return;
    toast(title, body != nullptr ? body : "");
}

namespace {

bool item_is_relayed(uint8_t item) {
    if (item >= dItemNo_SWORD_e && item <= dItemNo_WEAR_ZORA_e) return true;
    if (item >= dItemNo_WALLET_LV1_e && item <= dItemNo_WALLET_LV3_e) return true;
    if (item >= dItemNo_ZORAS_JEWEL_e && item <= dItemNo_COPY_ROD_2_e) return true;
    if (item >= dItemNo_ARROW_LV1_e && item <= dItemNo_ARROW_LV3_e) return true;
    if (item >= dItemNo_BEE_ROD_e && item <= dItemNo_JEWEL_WORM_ROD_e) return true;
    if (item >= dItemNo_LETTER_e && item <= dItemNo_HORSE_FLUTE_e) return true;
    if (item >= dItemNo_MIRROR_PIECE_2_e && item <= dItemNo_MIRROR_PIECE_4_e) return true;
    if (item >= dItemNo_SMELL_YELIA_POUCH_e && item <= dItemNo_SMELL_MEDICINE_e) return true;
    if (item >= dItemNo_M_BEETLE_e && item <= dItemNo_F_MAYFLY_e) return true;
    switch (item) {
    case dItemNo_KAKERA_HEART_e:
    case dItemNo_UTAWA_HEART_e:
    case dItemNo_MAGIC_LV1_e:
    case dItemNo_BOMB_BAG_LV2_e:
    case dItemNo_BOMB_BAG_LV1_e:
    case dItemNo_LIGHT_ARROW_e:
    case dItemNo_LURE_ROD_e:
    case dItemNo_EMPTY_BOTTLE_e:
    case dItemNo_RAFRELS_MEMO_e:
    case dItemNo_ASHS_SCRIBBLING_e:
    case dItemNo_POU_SPIRIT_e:
    case dItemNo_ANCIENT_DOCUMENT_e:
    case dItemNo_AIR_LETTER_e:
    case dItemNo_ANCIENT_DOCUMENT2_e:
    case dItemNo_TOMATO_PUREE_e:
    case dItemNo_TASTE_e:
    case dItemNo_SURFBOARD_e:
    case dItemNo_KANTERA2_e:
        return true;
    default:
        return false;
    }
}

bool item_is_check_extra(uint8_t item) {
    switch (item) {

    case 0x14: case 0x15: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x4D: case 0x4E:
    case 0x52: case 0x57: case 0x8F: case 0xAE: case 0xAF: case 0xBF: case 0xE8:
        return true;
    default:
        break;
    }
    if (item >= 0x85 && item <= 0x8E) return true;
    if (item >= 0x92 && item <= 0x98) return true;
    if (item >= 0x99 && item <= 0x9B) return true;
    if (item >= 0xA8 && item <= 0xAD) return true;
    if (item >= 0xB6 && item <= 0xBE) return true;
    if (item >= 0xD8 && item <= 0xDB) return true;
    if (item >= 0xE1 && item <= 0xE7) return true;

    if ((item >= 0xF9 && item <= 0xFB) || item == 0xFD) return rando_active();
    return false;
}

const uint8_t kChainSword[] = {0x3F, 0x28, 0x29, 0x49};
const uint8_t kChainBow[] = {0x43, 0x55, 0x56};
const uint8_t kChainClawshot[] = {0x44, 0x47};
const uint8_t kChainRod[] = {0x46, 0x4C};
const uint8_t kChainFishing[] = {0x4A, 0x3D};
const uint8_t kChainWallet[] = {0x35, 0x36};
const uint8_t kChainSkill[] = {0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7};
const uint8_t kChainKeyShard[] = {0xF9, 0xFA, 0xFD};
const uint8_t kChainMirror[] = {0xDB, 0xA5, 0xA6, 0xA7};
const uint8_t kChainShadow[] = {0xD8, 0xD9, 0xDA};

struct Chain {
    const uint8_t* items;
    int count;
};
#define COOP_CHAIN(a) {a, static_cast<int>(sizeof(a) / sizeof(a[0]))}
const Chain kChains[] = {
    COOP_CHAIN(kChainSword), COOP_CHAIN(kChainBow), COOP_CHAIN(kChainClawshot),
    COOP_CHAIN(kChainRod), COOP_CHAIN(kChainFishing), COOP_CHAIN(kChainWallet),
    COOP_CHAIN(kChainSkill), COOP_CHAIN(kChainKeyShard), COOP_CHAIN(kChainMirror),
    COOP_CHAIN(kChainShadow),
};
#undef COOP_CHAIN

uint8_t progressive_step(uint8_t item) {
    if (!rando_active()) return item;
    for (const Chain& chain : kChains) {
        bool inChain = false;
        for (int i = 0; i < chain.count; ++i) inChain = inChain || chain.items[i] == item;
        if (!inChain) continue;

        if (checkItemGet(item, 1) == 0) return item;
        for (int i = 0; i < chain.count; ++i) {
            if (checkItemGet(chain.items[i], 1) == 0) return chain.items[i];
        }
        return dItemNo_NONE_e;
    }
    return item;
}

bool item_is_stackable(uint8_t item) {
    switch (item) {
    case dItemNo_KAKERA_HEART_e:
    case dItemNo_UTAWA_HEART_e:
    case dItemNo_POU_SPIRIT_e:
    case dItemNo_EMPTY_BOTTLE_e:
    case dItemNo_BOMB_BAG_LV1_e:
        return true;
    default:
        return false;
    }
}

const char* item_name(uint8_t item) {
    if (item >= dItemNo_M_BEETLE_e && item <= dItemNo_F_MAYFLY_e) return "a Golden Bug";
    if (item >= dItemNo_MIRROR_PIECE_2_e && item <= dItemNo_MIRROR_PIECE_4_e) {
        return "a Mirror Shard";
    }
    if (item >= dItemNo_SMELL_YELIA_POUCH_e && item <= dItemNo_SMELL_MEDICINE_e) return "a scent";
    if (item >= dItemNo_BEE_ROD_e && item <= dItemNo_JEWEL_WORM_ROD_e) {
        return "a Fishing Rod upgrade";
    }
    if (item >= dItemNo_WALLET_LV1_e && item <= dItemNo_WALLET_LV3_e) return "a bigger Wallet";
    if (item >= dItemNo_CHUCHU_RARE_e && item <= dItemNo_CHUCHU_PURPLE_e) return "Chu Jelly";
    switch (item) {
    case dItemNo_KAKERA_HEART_e: return "a Piece of Heart";
    case dItemNo_UTAWA_HEART_e: return "a Heart Container";
    case dItemNo_SWORD_e: return "the Ordon Sword";
    case dItemNo_MASTER_SWORD_e: return "the Master Sword";
    case dItemNo_WOOD_SHIELD_e: return "the Wooden Shield";
    case dItemNo_SHIELD_e: return "the Ordon Shield";
    case dItemNo_HYLIA_SHIELD_e: return "the Hylian Shield";
    case dItemNo_TKS_LETTER_e: return "a letter";
    case dItemNo_WEAR_CASUAL_e: return "the Ordon Clothes";
    case dItemNo_WEAR_KOKIRI_e: return "the Hero's Clothes";
    case dItemNo_ARMOR_e: return "the Magic Armor";
    case dItemNo_WEAR_ZORA_e: return "the Zora Armor";
    case dItemNo_MAGIC_LV1_e: return "the Shadow Crystal";
    case dItemNo_ZORAS_JEWEL_e: return "the Coral Earring";
    case dItemNo_HAWK_EYE_e: return "the Hawkeye";
    case dItemNo_WOOD_STICK_e: return "the Wooden Sword";
    case dItemNo_BOOMERANG_e: return "the Gale Boomerang";
    case dItemNo_SPINNER_e: return "the Spinner";
    case dItemNo_IRONBALL_e: return "the Ball and Chain";
    case dItemNo_BOW_e: return "the Hero's Bow";
    case dItemNo_HOOKSHOT_e: return "the Clawshot";
    case dItemNo_HVY_BOOTS_e: return "the Iron Boots";
    case dItemNo_COPY_ROD_e: return "the Dominion Rod";
    case dItemNo_W_HOOKSHOT_e: return "the Double Clawshots";
    case dItemNo_KANTERA_e: return "the Lantern";
    case dItemNo_KANTERA2_e: return "the Lantern";
    case dItemNo_LIGHT_SWORD_e: return "the Master Sword's light";
    case dItemNo_FISHING_ROD_1_e: return "the Fishing Rod";
    case dItemNo_PACHINKO_e: return "the Slingshot";
    case dItemNo_COPY_ROD_2_e: return "the restored Dominion Rod";
    case dItemNo_BOMB_BAG_LV1_e: return "a Bomb Bag";
    case dItemNo_BOMB_BAG_LV2_e: return "the Giant Bomb Bag";
    case dItemNo_LIGHT_ARROW_e: return "Light Arrows";
    case dItemNo_ARROW_LV1_e: return "a Quiver";
    case dItemNo_ARROW_LV2_e: return "the Big Quiver";
    case dItemNo_ARROW_LV3_e: return "the Giant Quiver";
    case dItemNo_LURE_ROD_e: return "a fishing lure";
    case dItemNo_EMPTY_BOTTLE_e: return "an Empty Bottle";
    case dItemNo_LETTER_e: return "Renado's Letter";
    case dItemNo_BILL_e: return "the Invoice";
    case dItemNo_WOOD_STATUE_e: return "the Wooden Statue";
    case dItemNo_IRIAS_PENDANT_e: return "Ilia's Charm";
    case dItemNo_HORSE_FLUTE_e: return "the Horse Call";
    case dItemNo_RAFRELS_MEMO_e: return "Auru's Memo";
    case dItemNo_ASHS_SCRIBBLING_e: return "Ashei's Sketch";
    case dItemNo_POU_SPIRIT_e: return "a Poe Soul";
    case dItemNo_ANCIENT_DOCUMENT_e: return "an Ancient Sky Book";
    case dItemNo_AIR_LETTER_e: return "an Ancient Sky Character";
    case dItemNo_ANCIENT_DOCUMENT2_e: return "the Ancient Sky Book";
    case dItemNo_TOMATO_PUREE_e: return "the Ordon Pumpkin";
    case dItemNo_TASTE_e: return "the Ordon Goat Cheese";
    case dItemNo_SURFBOARD_e: return "the Snowboard";
    case dItemNo_RED_BOTTLE_e: return "Red Potion";
    case dItemNo_RED_BOTTLE_2_e: return "Red Potion";
    case dItemNo_GREEN_BOTTLE_e: return "Green Potion";
    case dItemNo_BLUE_BOTTLE_e: return "Blue Potion";
    case dItemNo_MILK_BOTTLE_e: return "Milk";
    case dItemNo_HALF_MILK_BOTTLE_e: return "Half Milk";
    case dItemNo_OIL_BOTTLE_e: return "Lantern Oil";
    case dItemNo_OIL_BOTTLE_2_e: return "Lantern Oil";
    case dItemNo_OIL_BOTTLE3_e: return "Lantern Oil";
    case dItemNo_OIL_e: return "Lantern Oil";
    case dItemNo_OIL2_e: return "Lantern Oil";
    case dItemNo_WATER_BOTTLE_e: return "Water";
    case dItemNo_UGLY_SOUP_e: return "Nasty Soup";
    case dItemNo_HOT_SPRING_e: return "Hot Spring Water";
    case dItemNo_HOT_SPRING_2_e: return "Hot Spring Water";
    case dItemNo_FAIRY_e: return "a Fairy";
    case dItemNo_FAIRY_DROP_e: return "Great Fairy's Tears";
    case dItemNo_DROP_BOTTLE_e: return "Great Fairy's Tears";
    case dItemNo_WORM_e: return "a Worm";
    case dItemNo_BEE_CHILD_e: return "Bee Larva";
    case dItemNo_SHOP_BEE_CHILD_e: return "Bee Larva";
    case dItemNo_CHUCHU_YELLOW2_e: return "Chu Jelly";
    case dItemNo_CHUCHU_BLACK_e: return "Chu Jelly";
    case dItemNo_LV1_SOUP_e: return "Simple Soup";
    case dItemNo_LV2_SOUP_e: return "Good Soup";
    case dItemNo_LV3_SOUP_e: return "Superb Soup";
    default: return "an item";
    }
}

const int kItemIdCount = 256;
uint8_t s_invLastBits[kItemIdCount] = {};
uint8_t s_invLastSlots[MAX_ITEM_SLOTS] = {};
uint8_t s_invLastWallet = 0;
bool s_haveInv = false;
uint32_t s_invTick = 0;
uint32_t s_invExpectUntil[kItemIdCount] = {};

uint32_t s_invOwedSince[kItemIdCount] = {};
const uint32_t kOwedRetryTicks = 600;

bool on_title_screen() {
    const char* stage = dComIfGp_getStartStageName();
    return stage != nullptr && (std::strcmp(stage, "F_SP102") == 0 || std::strcmp(stage, "title") == 0);
}

void relay_item(uint8_t item) {
    MsgItem msg{item};
    coop_net_send(kMsgItem, &msg, sizeof(msg));
    coop_log::info("coop_mod: [INV] relayed item {:#x}", item);
}

void retry_owed_items() {
    if (svc_item == nullptr || dComIfGp_event_runCheck() || dComIfGp_isEnableNextStage()) return;
    for (int i = 0; i < kItemIdCount; ++i) {
        if (s_invOwedSince[i] == 0) continue;
        const uint8_t item = static_cast<uint8_t>(i);
        if (dComIfGs_isItemFirstBit(item)) {
            s_invOwedSince[i] = 0;
            continue;
        }
        if (s_invTick - s_invOwedSince[i] < kOwedRetryTicks) continue;
        coop_log::info("coop_mod: [INV] item {:#x} never landed - asking for it again", item);
        svc_item->give_item(mod_ctx, nullptr, item, ITEM_GIVE_SILENT);
        s_invOwedSince[i] = s_invTick;
        s_invExpectUntil[i] = s_invTick + 1200;
    }
}

void scan_inventory() {
    ++s_invTick;
    if (on_title_screen()) {
        s_haveInv = false;
        std::memset(s_invOwedSince, 0, sizeof(s_invOwedSince));
        return;
    }
    if (!in_gameplay() || (s_invTick % 30) != 0) return;
    if (coop_net_connected()) retry_owed_items();

    uint8_t bits[kItemIdCount] = {};
    for (int i = 0; i < kItemIdCount; ++i) {
        const uint8_t item = static_cast<uint8_t>(i);
        if (item_is_relayed(item) && !item_is_stackable(item) && dComIfGs_isItemFirstBit(item)) {
            bits[i] = 1;
        }
    }
    uint8_t slots[MAX_ITEM_SLOTS] = {};
    for (int s = 0; s < MAX_ITEM_SLOTS; ++s) {

        slots[s] = (s >= SLOT_11 && s <= SLOT_14) ? static_cast<uint8_t>(dItemNo_NONE_e)
                                                  : dComIfGs_getItem(s, false);
    }
    const uint8_t wallet = dComIfGs_getWalletSize();

    if (!s_haveInv) {
        std::memcpy(s_invLastBits, bits, sizeof(bits));
        std::memcpy(s_invLastSlots, slots, sizeof(slots));
        s_invLastWallet = wallet;
        s_haveInv = true;
        return;
    }

    bool handled[kItemIdCount] = {};
    auto consider = [&](uint8_t item) {
        if (handled[item]) return;
        handled[item] = true;
        if (!item_is_relayed(item) || item_is_stackable(item)) return;
        if (s_invExpectUntil[item] != 0 && s_invExpectUntil[item] >= s_invTick) {
            s_invExpectUntil[item] = 0;
            return;
        }
        relay_item(item);
    };

    for (int i = 0; i < kItemIdCount; ++i) {
        if (bits[i] && !s_invLastBits[i]) consider(static_cast<uint8_t>(i));
    }
    for (int s = 0; s < MAX_ITEM_SLOTS; ++s) {
        const uint8_t item = slots[s];
        if (item == dItemNo_NONE_e || item == s_invLastSlots[s]) continue;
        bool hadBefore = false;
        for (int t = 0; t < MAX_ITEM_SLOTS; ++t) {
            if (s_invLastSlots[t] == item) hadBefore = true;
        }
        if (!hadBefore) consider(item);
    }
    if (wallet > s_invLastWallet) {
        if (wallet >= BIG_WALLET && s_invLastWallet < BIG_WALLET) consider(dItemNo_WALLET_LV2_e);
        if (wallet >= GIANT_WALLET && s_invLastWallet < GIANT_WALLET) consider(dItemNo_WALLET_LV3_e);
    }

    std::memcpy(s_invLastBits, bits, sizeof(bits));
    std::memcpy(s_invLastSlots, slots, sizeof(slots));
    s_invLastWallet = wallet;
}

static uint32_t s_heartGraceUntil = 0;

uint16_t s_lastLife = 0xFFFF;
uint32_t s_deathLinkQuietUntil = 0;

void update_death_link() {
    if (!coop_net_connected() || !coop_session(kSessDeathLink, cfg_bool(s_vars.deathLink, false))) {
        s_lastLife = 0xFFFF;
        return;
    }
    if (daAlink_getAlinkActorClass() == nullptr || dComIfGp_event_runCheck()) return;
    const uint16_t life = dComIfGs_getLife();
    const uint16_t was = s_lastLife;
    s_lastLife = life;
    if (was == 0xFFFF || was == 0 || life != 0) return;
    if (s_invTick < s_deathLinkQuietUntil) return;
    MsgDeathLink msg{};
    const std::string me = features_local_name();
    std::strncpy(msg.name, me.c_str(), kCoopNameMax - 1);
    coop_net_send(kMsgDeathLink, &msg, sizeof(msg));
    coop_log::info("coop_mod: [DEATH] we died - telling the other player");
}

void apply_death_link(const MsgDeathLink& msg) {
    if (!coop_session(kSessDeathLink, cfg_bool(s_vars.deathLink, false))) return;
    if (daAlink_getAlinkActorClass() == nullptr) return;
    if (dComIfGs_getLife() == 0) return;

    s_deathLinkQuietUntil = s_invTick + 300;
    dComIfGs_setLife(0);
    char who[kCoopNameMax];
    std::strncpy(who, msg.name, kCoopNameMax - 1);
    who[kCoopNameMax - 1] = '\0';
    coop_log::info("coop_mod: [DEATH] {} died, and so do we", who);
    toast(who[0] != '\0' ? who : "Your partner", "died, so did you.");
}

void on_item_given(ModContext*, const ItemGiveInfo* info, void*) {
    if (info == nullptr || info->origin != ITEM_GIVE_ORIGIN_GAME) return;
    if (!coop_net_connected() || !coop_session(kSessItems, cfg_bool(s_vars.syncInventory, true))) return;

    if (info->item == dItemNo_UTAWA_HEART_e) s_heartGraceUntil = s_invTick + 120;

    if (item_is_check_extra(info->item) && info->check_name != nullptr) {
        MsgItem msg{info->item};
        coop_net_send(kMsgItem, &msg, sizeof(msg));
        coop_log::info("coop_mod: [INV] relayed {:#x} from check '{}'", info->item,
            info->check_name);
        return;
    }
    if (!item_is_relayed(info->item)) return;

    if (!item_is_stackable(info->item)) return;
    MsgItem msg{info->item};
    coop_net_send(kMsgItem, &msg, sizeof(msg));
    coop_log::info("coop_mod: [INV] relayed item {:#x}", info->item);
}

bool grant_equipment_without_equipping(uint8_t item) {
    switch (item) {
    case dItemNo_SWORD_e: dComIfGs_setCollectSword(COLLECT_ORDON_SWORD); break;
    case dItemNo_MASTER_SWORD_e: dComIfGs_setCollectSword(COLLECT_MASTER_SWORD); break;
    case dItemNo_WOOD_STICK_e: dComIfGs_setCollectSword(COLLECT_WOODEN_SWORD); break;
    case dItemNo_LIGHT_SWORD_e: dComIfGs_setCollectSword(COLLECT_LIGHT_SWORD); break;
    case dItemNo_WOOD_SHIELD_e: dComIfGs_setCollectShield(COLLECT_WOODEN_SHIELD); break;
    case dItemNo_WEAR_KOKIRI_e: dComIfGs_setCollectClothes(KOKIRI_CLOTHES_FLAG); break;
    case dItemNo_SHIELD_e:
    case dItemNo_HYLIA_SHIELD_e:
    case dItemNo_WEAR_CASUAL_e:
    case dItemNo_WEAR_ZORA_e:
    case dItemNo_ARMOR_e:
        break;
    default:
        return false;
    }
    dComIfGs_onItemFirstBit(item);
    return true;
}

bool local_on_stage(const char* stage8);

const int16_t kProcItem = 0x218;
const int16_t kProcLifeContainer = 0x21B;

const int16_t kProcDemoItem = fpcNm_Demo_Item_e;

bool is_field_item(fopAc_ac_c* actor) {
    if (actor == nullptr) return false;
    const s16 name = fopAcM_GetName(actor);
    return name == kProcItem || name == kProcDemoItem;
}

fpc_ProcID s_announcedItemId = fpcM_ERROR_PROCESS_ID_e;

void announce_item_taken() {
    if (!coop_net_connected()) {
        s_announcedItemId = fpcM_ERROR_PROCESS_ID_e;
        return;
    }
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) {
        s_announcedItemId = fpcM_ERROR_PROCESS_ID_e;
        return;
    }
    fopAc_ac_c* partner = fopAcM_getItemEventPartner(alink);
    if (!is_field_item(partner)) {
        s_announcedItemId = fpcM_ERROR_PROCESS_ID_e;
        return;
    }

    const u32 itemBitNo = (fopAcM_GetParam(partner) >> 8) & 0xFF;
    if (itemBitNo == 0xFF) {
        s_announcedItemId = fopAcM_GetID(partner);
        return;
    }
    const fpc_ProcID id = fopAcM_GetID(partner);
    if (id == s_announcedItemId) return;
    s_announcedItemId = id;

    auto* item = static_cast<daItemBase_c*>(partner);
    MsgItemTaken msg{};

    msg.home[0] = partner->home.pos.x;
    msg.home[1] = partner->home.pos.y;
    msg.home[2] = partner->home.pos.z;
    msg.itemNo = item->getItemNo();
    coop_net_send(kMsgItemTaken, &msg, sizeof(msg));
    coop_log::info("coop_mod: [ITEM] taking item {:#x} at ({:.0f},{:.0f},{:.0f}) - "
                    "telling the other players to clear theirs",
        static_cast<int>(msg.itemNo), msg.home[0], msg.home[1], msg.home[2]);
}

struct ItemTakenSearch {
    cXyz home;
    uint8_t itemNo;
    int hidden;
};

void* hide_taken_item(void* proc, void* data) {
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    auto* want = static_cast<ItemTakenSearch*>(data);
    if (!is_field_item(actor)) return nullptr;
    auto* item = static_cast<daItemBase_c*>(actor);
    if (item->getItemNo() != want->itemNo) return nullptr;

    const f32 dx = actor->home.pos.x - want->home.x;
    const f32 dy = actor->home.pos.y - want->home.y;
    const f32 dz = actor->home.pos.z - want->home.z;
    if (dx * dx + dy * dy + dz * dz > 80.0f * 80.0f) return nullptr;

    fopAcM_OnStatus(actor, fopAcStts_NODRAW_e);
    item->mCcCyl.OffCoSetBit();
    ++want->hidden;
    return nullptr;
}

void apply_item_taken(const MsgItemTaken& msg) {
    ItemTakenSearch want{};
    want.home.set(msg.home[0], msg.home[1], msg.home[2]);
    want.itemNo = msg.itemNo;
    want.hidden = 0;
    fopAcM_Search(hide_taken_item, &want);
    coop_log::info("coop_mod: [ITEM] the other player took item {:#x} - cleared {} copy/copies "
                    "from our room", static_cast<int>(msg.itemNo), want.hidden);
}

struct HeartSweep {
    fopAc_ac_c* found[8];
    int count;
};

static void* collect_heart_item(void* proc, void* data) {
    auto* sweep = static_cast<HeartSweep*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (actor == nullptr || sweep->count >= 8) return nullptr;
    const s16 name = fopAcM_GetName(actor);
    if (name != kProcItem && name != kProcLifeContainer) return nullptr;
    if ((fopAcM_GetParam(actor) & 0xFF) != dItemNo_UTAWA_HEART_e) return nullptr;
    sweep->found[sweep->count++] = actor;
    return nullptr;
}

static void remove_local_heart_containers(uint8_t from) {

    const CoopPeer& sender = features_peer_of(from);
    if (!sender.inGame || !local_on_stage(sender.stage)) {
        coop_log::info("coop_mod: [INV] their heart container was in another stage - leaving "
                        "ours alone");
        return;
    }
    HeartSweep sweep;
    sweep.count = 0;
    fopAcM_Search(collect_heart_item, &sweep);
    for (int i = 0; i < sweep.count; ++i) {
        coop_log::info("coop_mod: [INV] removing our copy of the heart container - they already "
                        "took theirs and we were given the hearts for it");
        fopAcM_delete(sweep.found[i]);
    }
}

void apply_remote_item(uint8_t item, uint8_t from) {
    if (!coop_session(kSessItems, cfg_bool(s_vars.syncInventory, true))) return;
    if (svc_item == nullptr) return;
    const bool extra = item_is_check_extra(item);
    if (!item_is_relayed(item) && !extra) return;

    const uint8_t relayed = item;
    item = progressive_step(item);
    if (item == dItemNo_NONE_e) {
        coop_log::info("coop_mod: [INV] {:#x} from {} - we already have every tier", relayed,
            sender_name(from));
        return;
    }
    if (item != relayed) {
        coop_log::info("coop_mod: [INV] {:#x} from {} while we had it already - both checks "
                       "count, so {:#x}", relayed, sender_name(from), item);
    } else if (!extra && !item_is_stackable(item) && dComIfGs_isItemFirstBit(item)) {
        return;
    }

    if (item == dItemNo_UTAWA_HEART_e && s_invTick < s_heartGraceUntil) {

        coop_log::info("coop_mod: [INV] ignoring their heart container - we just took one");
        return;
    }

    if (item == dItemNo_UTAWA_HEART_e) {

        for (int i = 0; i < 5; ++i) {
            svc_item->give_item(mod_ctx, nullptr, dItemNo_KAKERA_HEART_e, ITEM_GIVE_SILENT);
        }

        remove_local_heart_containers(from);
    } else if (!grant_equipment_without_equipping(item)) {
        svc_item->give_item(mod_ctx, nullptr, item, ITEM_GIVE_SILENT);
        if (!item_is_stackable(item) && !extra) {
            s_invOwedSince[item] = s_invTick != 0 ? s_invTick : 1;
        }
    }

    s_invExpectUntil[item] = s_invTick + 1200;
    coop_log::info("coop_mod: [INV] received item {:#x} from {}", item, sender_name(from));

    if (cfg_bool(s_vars.notifyItems, true)) {
        const std::string who = sender_name(from);
        toast(who + " found " + item_name(item), "You got it too.");
    }
}

const int kBottleSlots = 4;
uint8_t s_lastBottles[kBottleSlots] = {};
bool s_haveBottles = false;

bool is_empty_bottle(uint8_t v) {
    return v == dItemNo_EMPTY_BOTTLE_e;
}

void read_bottles(uint8_t* out) {
    for (int i = 0; i < kBottleSlots; ++i) {
        out[i] = dComIfGs_getItem(SLOT_11 + i, true);
    }
}

void scan_bottles() {

    if (!in_gameplay()) {
        s_haveBottles = false;
        return;
    }
    uint8_t now[kBottleSlots];
    read_bottles(now);
    if (!s_haveBottles) {
        std::memcpy(s_lastBottles, now, sizeof(now));
        s_haveBottles = true;
        return;
    }
    for (int i = 0; i < kBottleSlots; ++i) {
        const uint8_t before = s_lastBottles[i];
        const uint8_t after = now[i];
        if (before == after || after == dItemNo_NONE_e || is_empty_bottle(after)) continue;
        MsgBottle msg{};
        msg.item = after;
        if (is_empty_bottle(before)) {
            msg.event = kBottleFillEmpty;
        } else if (before == dItemNo_NONE_e) {
            msg.event = kBottleNewFilled;
        } else {
            continue;
        }
        coop_net_send(kMsgBottle, &msg, sizeof(msg));
        coop_log::info("coop_mod: [INV] relayed bottle event={} item={:#x}", msg.event, msg.item);
    }
    std::memcpy(s_lastBottles, now, sizeof(now));
}

void apply_remote_bottle(uint8_t event, uint8_t item, uint8_t from) {
    if (!coop_session(kSessItems, cfg_bool(s_vars.syncInventory, true)) || !in_gameplay()) return;
    if (item == dItemNo_NONE_e || is_empty_bottle(item)) return;

    uint8_t now[kBottleSlots];
    read_bottles(now);
    bool applied = false;
    if (event == kBottleFillEmpty) {
        for (int i = 0; i < kBottleSlots; ++i) {
            if (is_empty_bottle(now[i])) {
                dComIfGs_setEmptyBottleItemIn(item);
                applied = true;
                break;
            }
        }
    } else if (event == kBottleNewFilled) {
        for (int i = 0; i < kBottleSlots; ++i) {
            if (now[i] == dItemNo_NONE_e) {
                dComIfGs_setEmptyBottle(item);
                applied = true;
                break;
            }
        }
    }

    read_bottles(s_lastBottles);
    s_haveBottles = true;

    if (applied && cfg_bool(s_vars.notifyItems, true)) {
        const std::string who = sender_name(from);
        if (event == kBottleFillEmpty) {
            toast(who + " bottled " + item_name(item), "You got one too.");
        } else {
            toast(who + " got a bottle of " + std::string(item_name(item)),
                "You got one too.");
        }
    }
}

void send_hello() {
    MsgHello msg{};
    msg.version = kCoopProtocolVersion;
    copy_name(msg.name, features_local_name());
    coop_net_send(kMsgHello, &msg, sizeof(msg));
}

void send_presence() {
    MsgPresence msg{};
    copy_name(msg.name, features_local_name());
    daAlink_c* alink = daAlink_getAlinkActorClass();
    const char* stage = dComIfGp_getStartStageName();
    msg.inGame = (alink != nullptr && stage != nullptr && stage[0] != '\0') ? 1 : 0;
    msg.skinStamp = local_skin_stamp();
    if (stage != nullptr) {
        std::memcpy(msg.stage, stage, strnlen(stage, sizeof(msg.stage)));
    }
    msg.startRoom = dComIfGp_getStartStageRoomNo();
    msg.layer = dComIfGp_getStartStageLayer();
    msg.point = dComIfGp_getStartStagePoint();
    if (alink != nullptr) {
        msg.curRoom = fopAcM_GetRoomNo(alink);
        msg.x = alink->current.pos.x;
        msg.y = alink->current.pos.y;
        msg.z = alink->current.pos.z;
        msg.angleY = alink->shape_angle.y;
        msg.life = dComIfGs_getLife();
        msg.maxLife = dComIfGs_getMaxLife();
    }
    coop_net_send(kMsgPresence, &msg, sizeof(msg));
}

void announce_peer_once(uint8_t from) {
    if (from >= kCoopMaxPlayers || s_peerAnnounced[from] || !s_peers[from].present) return;
    s_peerAnnounced[from] = true;
    toast(sender_name(from) + " joined", "");
}

void on_hello(const uint8_t* payload, size_t size, uint8_t from) {

    if (from >= kCoopMaxPlayers) return;
    if (size < sizeof(MsgHello)) return;
    MsgHello msg;
    std::memcpy(&msg, payload, sizeof(msg));
    peer_slot(from).present = true;
    peer_slot(from).name = wire_name(msg.name);
    remember_name(from, peer_slot(from).name);
    coop_log::info("coop_mod: hello from '{}' (protocol {})", peer_slot(from).name, msg.version);
    if (msg.version != kCoopProtocolVersion) {

        coop_log::info("coop_mod: refusing '{}' - protocol {} against our {}",
            peer_slot(from).name, msg.version, kCoopProtocolVersion);
        toast("Different mod version",
            peer_slot(from).name + " has version " + std::to_string(msg.version) + ", you have " +
                std::to_string(kCoopProtocolVersion) + ". Everyone needs the same version.",
            12000);
        coop_net_disconnect();
        return;
    }
    announce_peer_once(from);
}

void on_presence(const uint8_t* payload, size_t size, uint8_t from) {
    if (from >= kCoopMaxPlayers) return;
    if (size < sizeof(MsgPresence)) return;
    MsgPresence msg;
    std::memcpy(&msg, payload, sizeof(msg));
    peer_slot(from).present = true;
    peer_slot(from).name = wire_name(msg.name);
    remember_name(from, peer_slot(from).name);
    std::memcpy(peer_slot(from).stage, msg.stage, sizeof(msg.stage));
    peer_slot(from).stage[8] = '\0';
    peer_slot(from).inGame = msg.inGame != 0;
    peer_slot(from).startRoom = msg.startRoom;
    peer_slot(from).curRoom = msg.curRoom;
    peer_slot(from).layer = msg.layer;
    peer_slot(from).point = msg.point;
    peer_slot(from).x = msg.x;
    peer_slot(from).y = msg.y;
    peer_slot(from).z = msg.z;
    peer_slot(from).angleY = msg.angleY;

    if (peer_slot(from).skinStamp != msg.skinStamp) {
        peer_slot(from).skinStamp = msg.skinStamp;
        coop_net_send_to(from, kMsgSkinRequest, nullptr, 0);
    }
    peer_slot(from).lifeKnown = msg.inGame != 0 && msg.maxLife != 0;
    peer_slot(from).life = msg.life;
    peer_slot(from).maxLife = msg.maxLife;
    announce_peer_once(from);
}

struct PendingTeleport {
    bool active = false;
    uint8_t playerId = kCoopNoPlayer;
    char stage[9] = {};
    uint32_t ticks = 0;
    uint32_t settledTicks = 0;
};
PendingTeleport s_teleport;

const uint32_t kTeleportTimeoutTicks = 60 * 30;
const uint32_t kTeleportSettleTicks = 45;
const uint32_t kTeleportMinTicks = 3;

bool local_on_stage(const char* stage8) {
    const char* stage = dComIfGp_getStartStageName();
    return stage != nullptr && std::strncmp(stage, stage8, 8) == 0;
}

bool ground_under_player(uint8_t playerId) {
    const CoopPeer& who = features_peer_of(playerId);
    f32 x = who.x, y = who.y, z = who.z;
    f32 live[3];
    if (puppet_hook_get_pose_of(playerId, &live[0], &live[1], &live[2], nullptr, nullptr, nullptr)) {
        x = live[0];
        y = live[1];
        z = live[2];
    }
    cXyz at(x, y + 60.0f, z);
    if (!fopAcM_gc_c::gndCheck(&at)) return false;
    const f32 ground = fopAcM_gc_c::getGroundY();
    return ground > y - 400.0f && ground < y + 120.0f;
}

void place_at_player(daAlink_c* alink, uint8_t playerId) {
    const CoopPeer& who = features_peer_of(playerId);

    f32 px = who.x, py = who.y, pz = who.z;
    f32 live[3] = {0.0f, 0.0f, 0.0f};
    if (puppet_hook_get_pose_of(playerId, &live[0], &live[1], &live[2], nullptr, nullptr,
            nullptr)) {
        px = live[0];
        py = live[1];
        pz = live[2];
    }

    cXyz pos(px, py, pz);

    alink->setPlayerPosAndAngle(&pos, who.angleY, TRUE);
    coop_log::info("coop_mod: placed at {} ({:.0f}, {:.0f}, {:.0f})", who.name, pos.x, pos.y,
        pos.z);
}

void update_pending_teleport() {
    if (!s_teleport.active) return;
    if (++s_teleport.ticks > kTeleportTimeoutTicks) {
        s_teleport.active = false;
        toast("Teleport cancelled", "It took too long.");
        return;
    }
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr || !local_on_stage(s_teleport.stage)) {
        s_teleport.settledTicks = 0;
        return;
    }
    if (dComIfGp_isEnableNextStage() || dComIfGp_event_runCheck()) {
        s_teleport.settledTicks = 0;
        return;
    }

    ++s_teleport.settledTicks;
    if (s_teleport.settledTicks < kTeleportMinTicks) return;
    if (s_teleport.settledTicks < kTeleportSettleTicks && !ground_under_player(s_teleport.playerId)) {
        return;
    }
    s_teleport.active = false;

    const CoopPeer& who = features_peer_of(s_teleport.playerId);
    if (who.present && who.inGame && std::strncmp(who.stage, s_teleport.stage, 8) == 0) {
        place_at_player(alink, s_teleport.playerId);
    } else {
        toast("Teleport cancelled", who.name + " left before we got there.");
    }
}

void send_time() {
    MsgTime msg{};
    msg.time = dComIfGs_getTime();
    coop_net_send(kMsgTime, &msg, sizeof(msg));
}

void on_time(const uint8_t* payload, size_t size) {
    if (size < sizeof(MsgTime) || coop_net_is_host()) return;
    if (!coop_session(kSessTime, cfg_bool(s_vars.syncTime, true)) || !in_gameplay()) return;
    MsgTime msg;
    std::memcpy(&msg, payload, sizeof(msg));
    if (!std::isfinite(msg.time) || msg.time < 0.0f) return;
    const f32 local = dComIfGs_getTime();
    f32 diff = msg.time - local;
    if (msg.time < 360.0f && local < 360.0f) {
        while (diff > 180.0f) diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;
    }
    if (std::fabs(diff) > 2.0f) {
        dComIfGs_setTime(msg.time);
    }
}

}

void features_register_vars() {
    s_vars.name = register_var("player_name", CONFIG_VAR_STRING, false, 0, "Player");
    s_vars.syncInventory = register_var("sync_inventory", CONFIG_VAR_BOOL, true, 0, nullptr);
    s_vars.notifyItems = register_var("notify_items", CONFIG_VAR_BOOL, true, 0, nullptr);
    s_vars.nametags = register_var("show_nametags", CONFIG_VAR_BOOL, true, 0, nullptr);
    s_vars.nametagsEdge = register_var("nametag_offscreen", CONFIG_VAR_BOOL, true, 0, nullptr);
    s_vars.nametagsHealth = register_var("nametag_health", CONFIG_VAR_BOOL, true, 0, nullptr);
    s_vars.nametagsHideFar = register_var("nametag_hide_far", CONFIG_VAR_BOOL, false, 0, nullptr);
    s_vars.syncSounds = register_var("sync_sounds", CONFIG_VAR_BOOL, true, 0, nullptr);
    s_vars.soundVolume = register_var("sound_volume", CONFIG_VAR_INT, false, 35, nullptr);
    s_vars.syncTime = register_var("sync_time", CONFIG_VAR_BOOL, true, 0, nullptr);
    s_vars.syncVfx = register_var("sync_attack_effects", CONFIG_VAR_BOOL, true, 0, nullptr);

    s_vars.puppetWarpFx = register_var("puppet_warp_fx", CONFIG_VAR_BOOL, false, 0, nullptr);

    s_vars.puppetWarpWarm = register_var("puppet_warp_warmup", CONFIG_VAR_BOOL, false, 0, nullptr);

    s_vars.debugMenu = register_var("debug_menu", CONFIG_VAR_BOOL, false, 0, nullptr);
    s_devLogging = cfg_bool(s_vars.debugMenu, false);

    s_vars.debugAutowarp = register_var("debug_autowarp_ticks", CONFIG_VAR_INT, false, 0, nullptr);

    s_vars.deathLink = register_var("death_link", CONFIG_VAR_BOOL, false, 0, nullptr);
    s_vars.debugShiftTicks = register_var("debug_shift_ticks", CONFIG_VAR_INT, false, 0, nullptr);
    s_vars.debugShiftX = register_var("debug_shift_x", CONFIG_VAR_INT, false, 0, nullptr);
    s_vars.debugShiftZ = register_var("debug_shift_z", CONFIG_VAR_INT, false, 0, nullptr);
    colors_register_vars();
    horse_register_vars();
    squad_hud_register_vars();
    pvp_register_vars();
    spawns_register_vars();
    boss_register_vars();
    world_register_vars();
    enemies_register_vars();
    joinsync_register_vars();
}

void features_init() {
    fx_init();
    enemies_init();
    spawns_init();
    boss_init();

    if (svc_item != nullptr) {
        const ModResult observe = svc_item->observe_gives(mod_ctx, on_item_given, nullptr, nullptr);
        coop_log::info("coop_mod: item observer: {}", static_cast<int>(observe));
    } else {
        coop_log::warn("coop_mod: item service unavailable - inventory sync disabled");
    }
    colors_init();
}

static void run_debug_autowarp() {
    int64_t every = 0;
    if (s_vars.debugAutowarp != 0) svc_config->get_int(mod_ctx, s_vars.debugAutowarp, &every);
    if (every <= 0) return;
    static uint32_t ticks = 0;
    if (daAlink_getAlinkActorClass() == nullptr || dComIfGp_event_runCheck() ||
        dComIfGp_isEnableNextStage())
    {
        ticks = 0;
        return;
    }
    if (++ticks < static_cast<uint32_t>(every)) return;
    ticks = 0;
    const char* stage = dComIfGp_getStartStageName();
    if (stage == nullptr || stage[0] == '\0') return;
    coop_log::info("coop_mod: [AUTOWARP] reloading stage {} point={} room={} layer={}", stage,
        dComIfGp_getStartStagePoint(), static_cast<int>(dComIfGp_getStartStageRoomNo()),
        static_cast<int>(dComIfGp_getStartStageLayer()));
    dComIfGp_setNextStage(stage, dComIfGp_getStartStagePoint(), dComIfGp_getStartStageRoomNo(),
        dComIfGp_getStartStageLayer());
}

static void run_debug_shift() {
    int64_t after = 0;
    if (s_vars.debugShiftTicks != 0) svc_config->get_int(mod_ctx, s_vars.debugShiftTicks, &after);
    if (after <= 0) return;
    static uint32_t ticks = 0;
    static bool done = false;
    if (done) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr || dComIfGp_event_runCheck() || dComIfGp_isEnableNextStage()) {
        ticks = 0;
        return;
    }
    if (++ticks < static_cast<uint32_t>(after)) return;
    done = true;

    int64_t dx = 0, dz = 0;
    if (s_vars.debugShiftX != 0) svc_config->get_int(mod_ctx, s_vars.debugShiftX, &dx);
    if (s_vars.debugShiftZ != 0) svc_config->get_int(mod_ctx, s_vars.debugShiftZ, &dz);
    cXyz pos(alink->current.pos.x + static_cast<f32>(dx), alink->current.pos.y,
        alink->current.pos.z + static_cast<f32>(dz));

    alink->setPlayerPosAndAngle(&pos, alink->shape_angle.y, TRUE);
    coop_log::info("coop_mod: [SHIFT] moved to ({:.0f}, {:.0f}, {:.0f})", pos.x, pos.y, pos.z);
}

void coop_remember_last_host(const char* name, const char* address) {
    if (name == nullptr || name[0] == '\0') return;
    std::string body = "Last played with " + std::string(name);
    if (address != nullptr && address[0] != '\0') {
        const std::string where(address);
        body += (where.rfind("room ", 0) == 0 ? " in " : " at ") + where;
    }
    features_toast("Co-op save", (body + ".").c_str());
}

uint32_t local_skin_stamp() {
    SkinChoices choices;
    skins_local_choices(&choices);
    uint32_t stamp = 2166136261u;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&choices);
    for (size_t i = 0; i < sizeof(choices); ++i) {
        stamp = (stamp ^ bytes[i]) * 16777619u;
    }
    return stamp;
}

void send_skin_choices() {
    if (!coop_net_connected()) return;
    SkinChoices choices;
    skins_local_choices(&choices);
    MsgSkinChoices msg{};
    static_assert(sizeof(msg.name) == sizeof(choices.name), "slot count changed");
    std::memcpy(msg.name, choices.name, sizeof(msg.name));
    std::memcpy(msg.hash, choices.hash, sizeof(msg.hash));
    coop_net_send(kMsgSkinChoices, &msg, sizeof(msg));
}

void on_skin_choices(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < sizeof(MsgSkinChoices) || from >= kCoopMaxPlayers) return;
    MsgSkinChoices msg;
    std::memcpy(&msg, payload, sizeof(msg));
    SkinChoices& theirs = peer_slot(from).skins;
    bool changed = false;
    for (int i = 0; i < kSkinChoiceCount; ++i) {
        if (std::strncmp(theirs.name[i], msg.name[i], kSkinNameMax - 1) != 0 ||
            theirs.hash[i] != msg.hash[i]) {
            changed = true;
        }
        std::memset(theirs.name[i], 0, kSkinNameMax);
        std::strncpy(theirs.name[i], msg.name[i], kSkinNameMax - 1);
        theirs.hash[i] = msg.hash[i];
    }

    if (changed) puppet_hook_peer_skin_changed(from);
}

void features_update() {
    s_devLogging = features_debug_menu();
    voices_update();
    local_skin_colors_update();
    local_skin_equipment_update();
    icons_update();
    skins_cycle_update();
    skins_outfit_cycle_update();
    send_session_settings();

    static bool s_devModeDone = false;
    if (!s_devModeDone && features_debug_menu()) {
        s_devModeDone = true;
        if (mDoMain::developmentMode == 0) mDoMain::developmentMode = 1;
    }
    run_debug_autowarp();
    run_debug_shift();
    update_death_link();
    announce_item_taken();
    colors_update();
    projectiles_update();
    fx_update();
    pvp_update();
    spawns_update();
    boss_update();
    world_update();
    enemies_update();
    horse_update();
    grass_update();
    joinsync_update();
    skipvote_update();
    skills_update();
    twilight_update();
    const bool connected = coop_net_connected();

    const bool nametagsOn = cfg_bool(s_vars.nametags, true);
    const bool nametagsFar = cfg_bool(s_vars.nametagsHideFar, false);
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id()) continue;
        const CoopPeer& p = s_peers[i];
        bool visible = true;
        if (connected && p.present) {
            visible = p.inGame && local_on_stage(p.stage);
        }
        puppet_hook_set_player_visible(id, visible);
        const char* tag = "Player";
        if (p.present) {
            tag = p.name.c_str();
        } else if (!s_knownName[i].empty()) {
            tag = s_knownName[i].c_str();
            if (connected && coop_net_player_present(id) && !s_nameGapLogged[i]) {
                s_nameGapLogged[i] = true;
                coop_log::info("coop_mod: player {} ('{}') is in the roster but we have no "
                               "presence for them - keeping their name up", i, s_knownName[i]);
            }
        }
        puppet_hook_set_player_nametag(id, tag, nametagsOn, nametagsFar);

        const bool low = p.lifeKnown && p.maxLife >= 5 && p.life <= p.maxLife / 5;
        puppet_hook_set_player_low_health(id, low);
    }
    puppet_hook_set_edge_tags(cfg_bool(s_vars.nametagsEdge, true));
    puppet_hook_set_nametag_health(cfg_bool(s_vars.nametagsHealth, true));
    puppet_hook_set_vfx_enabled(true);

    update_pending_teleport();

    if (!connected) {
            s_haveBottles = false;
        return;
    }

    if (++s_presenceTicks >= 30) {
        s_presenceTicks = 0;
        send_presence();
    }
    if (coop_net_is_host() && ++s_timeTicks >= 60) {
        s_timeTicks = 0;
        if (in_gameplay() && coop_session(kSessTime, cfg_bool(s_vars.syncTime, true))) send_time();
    }
    if (coop_session(kSessItems, cfg_bool(s_vars.syncInventory, true))) {
        scan_inventory();
        scan_bottles();
    } else {
        s_haveBottles = false;
    }
}

void features_on_roster_changed() {
    if (!coop_net_connected()) return;

    const uint16_t now = coop_net_roster();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if ((s_lastRoster & bit) == 0 || (now & bit) != 0) continue;
        if (static_cast<uint8_t>(i) == coop_net_local_id()) continue;
        if (s_peers[i].present) {
            toast(sender_name(static_cast<uint8_t>(i)) + " left", "");
        }
        s_peers[i] = CoopPeer{};
        s_peerAnnounced[i] = false;
    }
    s_lastRoster = now;
    send_hello();
}

void features_on_connected() {
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        s_peers[i] = CoopPeer{};
        s_peerAnnounced[i] = false;
    }
    s_lastRoster = coop_net_roster();
    s_haveBottles = false;
    s_haveInv = false;
    s_presenceTicks = 0;
    s_timeTicks = 0;
    send_hello();
    send_presence();
    colors_on_connected();
    send_skin_choices();
    pvp_on_connected();
    spawns_on_connected();
    boss_on_connected();
    world_on_connected();
    enemies_on_connected();
    joinsync_on_connected();
}

void features_on_disconnected() {
    int others = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (s_peers[i].present) ++others;
    }
    if (others == 1) {
        toast(first_peer().name + " left", "Disconnected.");
    } else if (others > 1) {
        toast("Disconnected", "");
    }
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        s_peers[i] = CoopPeer{};
        s_peerAnnounced[i] = false;
    }
    s_lastRoster = coop_net_roster();
    s_haveBottles = false;
    s_teleport.active = false;
    colors_on_disconnected();
    pvp_on_disconnected();
    spawns_on_disconnected();
    enemies_on_disconnected();
    horse_reset();

    horses_on_disconnected();
    grass_reset();
    s_haveHostSession = false;
}

void features_reset_sync_baselines() {
    s_haveInv = false;
    s_haveBottles = false;
}

void features_toast(const char* title, const char* body) {
    toast(title != nullptr ? title : "", body != nullptr ? body : "");
}

bool worth_crumbing(uint8_t type) {
    switch (type) {
    case kMsgSounds:
    case kMsgParticles:
    case kMsgHorse:
    case kMsgAnimal:
    case kMsgTorch:
    case kMsgEnemyState:
    case kMsgEnemyTargets:
    case kMsgActorState:
    case kMsgObjectMove:
    case kMsgBossState:
    case kMsgGrassCut:
    case kMsgPing:
    case kMsgPong:
    case kMsgSkipVote:
    case kMsgPause:
    case kMsgCarry:
        return false;
    default:
        return true;
    }
}

void features_on_message(uint8_t type, const uint8_t* payload, size_t size, uint8_t from) {
    if (worth_crumbing(type)) {
        char line[64];
        std::snprintf(line, sizeof(line), "msg type=%d from=%d size=%d", static_cast<int>(type),
            static_cast<int>(from), static_cast<int>(size));
        coop_crash_trail(line);
    }
    switch (type) {
    case kMsgAssignId: {
        if (size < sizeof(MsgAssignId)) break;
        MsgAssignId msg;
        std::memcpy(&msg, payload, sizeof(msg));
        coop_net_set_local_id(msg.playerId, msg.maxPlayers);
        break;
    }
    case kMsgRoster: {
        if (size < sizeof(MsgRoster)) break;
        MsgRoster msg;
        std::memcpy(&msg, payload, sizeof(msg));
        coop_net_set_roster(msg.present);
        break;
    }
    case kMsgHello: on_hello(payload, size, from); break;
    case kMsgSkinChoices: on_skin_choices(payload, size, from); break;
    case kMsgSkinRequest: send_skin_choices(); break;
    case kMsgPause:
        if (size >= sizeof(MsgPause)) coop_net_set_player_paused(from, payload[0] != 0);
        break;
    case kMsgPresence: on_presence(payload, size, from); break;
    case kMsgItem:
        if (size >= sizeof(MsgItem)) apply_remote_item(payload[0], from);
        break;
    case kMsgBottle:
        if (size >= sizeof(MsgBottle)) apply_remote_bottle(payload[0], payload[1], from);
        break;
    case kMsgItemTaken: {
        if (size < sizeof(MsgItemTaken)) break;
        MsgItemTaken msg;
        std::memcpy(&msg, payload, sizeof(msg));
        apply_item_taken(msg);
        break;
    }
    case kMsgDeathLink: {
        if (size < sizeof(MsgDeathLink)) break;
        MsgDeathLink msg;
        std::memcpy(&msg, payload, sizeof(msg));
        apply_death_link(msg);
        break;
    }
    case kMsgTime: on_time(payload, size); break;
    case kMsgSounds: fx_on_sounds(payload, size, from); break;
    case kMsgParticles: fx_on_particles(payload, size, from); break;
    case kMsgPvpState:
    case kMsgPvpHit: pvp_on_message(type, payload, size, from); break;
    case kMsgWorldDelta:
    case kMsgWorldSyncRequest:
    case kMsgWorldFull:
    case kMsgTbox2:
    case kMsgWorldDigest: world_on_message(type, payload, size, from); break;
    case kMsgEnemyState:
    case kMsgEnemyGone:
    case kMsgEnemyDamage:
    case kMsgEnemyHit:
    case kMsgObjectHit:
    case kMsgObjectMove:
    case kMsgCarry:
    case kMsgObjectPush:
    case kMsgTorch:
    case kMsgAnimal:
    case kMsgRoomClaim:
    case kMsgRoomOwner:
    case kMsgEnemyTargets: enemies_on_message(type, payload, size, from); break;

    case kMsgSkipVote: skipvote_on_message(payload, size, from); break;
    case kMsgSkills: skills_on_message(payload, size); break;
    case kMsgRandoSeed:
    case kMsgRandoSeedRequest:
    case kMsgRandoChunk: rando_on_message(type, payload, size, from); break;
    case kMsgCheckTaken:
    case kMsgCheckList: checks_on_message(type, payload, size); break;
    case kMsgTwilightBug:
    case kMsgTearGot: twilight_on_message(type, payload, size); break;
    case kMsgActorSpawn:
    case kMsgActorState:
    case kMsgActorGone: spawns_on_message(type, payload, size, from); break;
    case kMsgBossState:
    case kMsgBossReady:
    case kMsgBossHit:
    case kMsgPillarShake:
    case kMsgBossStem: boss_on_message(type, payload, size, from); break;
    case kMsgJoinSync: joinsync_on_message(payload, size); break;
    case kMsgColors: colors_on_message(payload, size, from); break;
    case kMsgArrowShot: projectiles_on_message(payload, size); break;
    case kMsgHorse: horse_on_message(payload, size, from); break;
    case kMsgGrassCut: grass_on_message(payload, size, from); break;
    case kMsgSessionSettings:
        if (size >= sizeof(MsgSessionSettings) && !coop_net_is_host()) {
            MsgSessionSettings msg;
            std::memcpy(&msg, payload, sizeof(msg));
            if (!s_haveHostSession || msg.flags != s_hostSession) {
                coop_log::info("coop_mod: [SESSION] using the host's settings (flags={:#x})",
                    msg.flags);
            }
            s_hostSession = msg.flags;
            s_haveHostSession = true;
        }
        break;
    default:
        break;
    }
}

const CoopPeer& features_peer_of(uint8_t playerId) {
    static const CoopPeer none;
    return playerId < kCoopMaxPlayers ? s_peers[playerId] : none;
}

bool features_any_peer_on_stage(const char* stage) {
    if (stage == nullptr) return false;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (static_cast<uint8_t>(i) == coop_net_local_id()) continue;
        const CoopPeer& p = s_peers[i];
        if (p.present && p.inGame && std::strncmp(stage, p.stage, 8) == 0) return true;
    }
    return false;
}

const CoopPeer& features_peer() {
    return first_peer();
}

const CoopFeatureVars& features_vars() {
    return s_vars;
}

bool features_puppet_warp_fx() {
    return cfg_bool(s_vars.puppetWarpFx, false);
}

bool features_puppet_warp_warm() {
    return cfg_bool(s_vars.puppetWarpWarm, true);
}

bool features_debug_menu() {
    return cfg_bool(s_vars.debugMenu, false);
}

bool coop_dev_logging() {
    return s_devLogging;
}

bool coop_session_from_host() {
    return coop_net_connected() && !coop_net_is_host() && s_haveHostSession;
}

bool coop_session(uint16_t flag, bool localValue) {
    if (!coop_session_from_host()) return localValue;
    return (s_hostSession & flag) != 0;
}

namespace {

uint16_t local_session_flags() {
    uint16_t f = 0;
    if (cfg_bool(enemies_enabled_var(), false)) f |= kSessEnemies;
    if (cfg_bool(boss_enabled_var(), false)) f |= kSessBosses;
    if (cfg_bool(boss_wait_var(), false)) f |= kSessBossWait;
    if (cfg_bool(enemies_breakables_var(), true)) f |= kSessWorldObjects;
    if (cfg_bool(world_dungeon_var(), true)) f |= kSessDungeon;
    if (cfg_bool(world_story_var(), false)) f |= kSessStory;
    if (cfg_bool(s_vars.syncInventory, true)) f |= kSessItems;
    if (cfg_bool(s_vars.syncTime, true)) f |= kSessTime;
    if (cfg_bool(s_vars.deathLink, false)) f |= kSessDeathLink;
    return f;
}

void send_session_settings() {
    static uint32_t s_sessionTick = 0;
    if (!coop_net_connected() || !coop_net_is_host()) return;
    if (++s_sessionTick % 30 != 0) return;
    MsgSessionSettings msg{};
    msg.flags = local_session_flags();
    coop_net_send(kMsgSessionSettings, &msg, sizeof(msg));
}

}

bool features_puppet_midna() {
    return true;
}

bool features_puppet_lantern_light() {
    return true;
}

std::string features_local_name() {
    return sanitize_name(cfg_string(s_vars.name, "Player"));
}

void features_teleport_to_player(uint8_t playerId) {
    const CoopPeer& who = features_peer_of(playerId);
    if (!coop_net_connected() || !who.present) {
        toast("Can't teleport", "Nobody is connected.");
        return;
    }
    if (!who.inGame || who.stage[0] == '\0') {
        toast("Can't teleport", who.name + " is not in the game yet.");
        return;
    }
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) {
        toast("Can't teleport", "Load your save first.");
        return;
    }
    if (dComIfGp_event_runCheck() || dComIfGp_isEnableNextStage()) {
        toast("Can't teleport right now", "Wait for the cutscene or loading screen.");
        return;
    }

    if (local_on_stage(who.stage) && fopAcM_GetRoomNo(alink) == who.curRoom) {
        place_at_player(alink, playerId);
        return;
    }

    s_teleport = PendingTeleport{};
    s_teleport.active = true;
    s_teleport.playerId = playerId;
    std::memcpy(s_teleport.stage, who.stage, 9);
    dComIfGp_setNextStage(who.stage, who.point, who.curRoom, who.layer);
    coop_log::info(
        "coop_mod: teleporting to {} stage={} point={} room={} layer={} (local stage={} room={})",
        who.name, who.stage, who.point, who.curRoom, who.layer,
        dComIfGp_getStartStageName(), fopAcM_GetRoomNo(alink));
    toast("Teleporting", "Heading to " + who.name + ".", 2500);
}

void features_debug_fake_peer(uint8_t id, bool on, const char* name, const float* pos, uint16_t life,
    uint16_t maxLife) {
    if (id >= kCoopMaxPlayers) return;
    CoopPeer& p = peer_slot(id);
    if (!on) {
        p = CoopPeer{};
        return;
    }
    daAlink_c* alink = daAlink_getAlinkActorClass();
    const char* stage = dComIfGp_getStartStageName();
    p.present = true;
    p.inGame = alink != nullptr;
    if (name != nullptr) p.name = name;
    if (stage != nullptr) {
        std::strncpy(p.stage, stage, 8);
        p.stage[8] = '\0';
    }
    p.curRoom = alink != nullptr ? static_cast<int8_t>(fopAcM_GetRoomNo(alink)) : 0;
    if (pos != nullptr) {
        p.x = pos[0];
        p.y = pos[1];
        p.z = pos[2];
    }
    p.lifeKnown = maxLife != 0;
    p.life = life;
    p.maxLife = maxLife;
}

bool features_reload_at_player(uint8_t playerId) {
    const CoopPeer& who = features_peer_of(playerId);
    if (!who.present || !who.inGame || who.stage[0] == '\0') return false;
    if (daAlink_getAlinkActorClass() == nullptr || dComIfGp_isEnableNextStage()) return false;
    s_teleport = PendingTeleport{};
    s_teleport.active = true;
    s_teleport.playerId = playerId;
    std::memcpy(s_teleport.stage, who.stage, 9);
    dComIfGp_setNextStage(who.stage, who.point, who.curRoom, who.layer);
    coop_log::info("coop_mod: [JOIN] loading into {} room {} where {} is", who.stage, who.curRoom,
        who.name);
    return true;
}

void features_debug_receive_item(uint8_t item, uint8_t from) {
    apply_remote_item(item, from);
}
